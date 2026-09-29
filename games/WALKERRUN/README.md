# TRENCHRUNNER

Atari's 1983 *Star Wars* arcade game, running on a Fiesta medal.

This is an emulator of the original arcade board, not a remake. It runs the
original ROM code on a Waveshare ESP32-C6-LCD-1.69 module, the same $20 board
that powers [PELLETINO](https://github.com/aedile/PELLETINO), the
Pac-Man Fiesta medal. If you're not from San Antonio: Fiesta medals are the
pins people collect and trade every April, and one-upping each other's medals
is half the point. You hold it upright like any other medal, tilt to fly, and
press the button to fire. The music, the sound effects and the speech all come
out of the medal's little speaker.

## Why

When I was a kid there was a Star Wars cabinet in the hotel my family stayed at
every year. I spent hours on it, and just as many hours standing in front of it
watching the attract mode cycle when I was out of quarters. Owning one became
the standard-issue childhood dream. Years later I got the Arcade1Up version,
which scratches part of the itch, but from the moment I saw this tiny board
with a screen on it the real dream became this: the actual game, the actual
code, running on a pin on my shirt.

## What the original hardware is

The Star Wars board is a good deal more exotic than Pac-Man's.

* Two Motorola 6809E CPUs at 1.512 MHz. One runs the game, the other runs the
  sound board.
* Atari's Analog Vector Generator (AVG). There is no frame buffer; the CPU
  writes a display list into RAM and a small sequencer, driven by a 256-byte
  state PROM, walks the list and steers the beam of a color vector monitor.
* A custom matrix processor built from AMD 2901 bit-slice chips and four
  microcode PROMs, plus a hardware divider. This is what does the 3D math.
* On the sound side a MOS 6532 RIOT for timing, four POKEY chips for music and
  effects, and a TMS5220 speech synthesizer for "Red Five standing by" and the
  rest.

All of that is emulated here, in plain C, and it runs in real time on a single
160 MHz RISC-V core with about 20% to spare.

## What you need

* A Waveshare ESP32-C6-LCD-1.69 (with the IMU; the plain LCD version has no
  tilt sensor). The PELLETINO repository has the 3D-printable case and the
  battery details if you want to build a full medal.
* The Star Wars ROM set as MAME knows it, named `starwars`. **The ROMs are not
  included and I can't help you find them.** The set contains these files:

  | File | Size | What it is |
  |---|---|---|
  | `136021.102`, `136021.203`, `136021.104`, `136021.206` | 8 KB each | main program |
  | `136021.214` | 16 KB | banked program ROM |
  | `136021.105` | 4 KB | vector ROM (fixed display-list routines) |
  | `136021.110`, `.111`, `.112`, `.113` | 1 KB each | matrix processor microcode |
  | `136021-105.1l` | 256 bytes | AVG state PROM |
  | `136021.107`, `136021.208` | 8 KB each | sound program |

  The converter checks every file's CRC and tells you if one is wrong.
* Docker, or a native ESP-IDF 5.3 install. Everything below uses Docker so
  there is nothing to install beyond that.
* Python 3 for the ROM converter and `esptool` for flashing
  (`pip install esptool`).

## Building and flashing

The partition table gives the app 1 MB; the rest of the flash is unused when
the game is built on its own.

1. Clone this repository and put the ROM files in a folder called `starwars/`
   at the top level. That folder is ignored by git.

2. Convert the ROMs into a C header the firmware embeds:

   ```
   python3 tools/convert_roms.py starwars
   ```

   This writes `main/roms/starwars_roms.h` (also ignored by git).

3. Build the firmware:

   ```
   docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 \
       idf.py -B build_docker build
   ```

   With a native ESP-IDF install it's just `idf.py build` after
   `idf.py set-target esp32c6`.

4. Plug the medal in over USB-C. The board shows up as `/dev/cu.usbmodem*` on
   a Mac (`/dev/ttyACM*` on Linux). On macOS you'll get an "allow accessory to
   connect" prompt the first time. Then:

   ```
   python3 -m esptool --chip esp32c6 --port /dev/cu.usbmodem101 -b 460800 \
       write_flash @build_docker/flash_args
   ```

   If the board never appears, the cable is almost always the reason. Plenty of
   USB-C cables are charge-only.

## Playing

Hold the medal upright, screen facing you, the way it hangs on a pin. The
arcade monitor was a 4:3 landscape picture, so the game is drawn letterboxed
across the middle of the upright panel, with black bars above and below.
(`ORIENTATION_PORTRAIT` in `main/render.cpp` switches back to the sideways
layout.)

* **Tilt** is the flight yoke. Pitch and steer the way you'd move a yoke. The
  first time you press fire, the medal records how you're holding it and
  treats that as centered, so pick up the medal, hold it comfortably, then
  fire.
* **BOOT button** (the one nearest the USB port) fires. In free play it also
  starts the game and picks the Death Star on the selection screen.
* **PWR button**, short press: inserts a coin and re-centers the yoke. Long
  press for a second: powers the medal off.

The game is set to free play with six shields on the easy difficulty. To
change that, edit the `sw_set_dips` call in `main/main.cpp`; the DIP switch
bits are documented next to it.

### Sound off

Hold the fire button for three seconds and let go: the sound toggles off or
on, for wearing the medal somewhere it needs to be quiet.

### Marquee

The black bars can carry text in the game's own vector lettering: a line for
the top bar and one for the bottom, static and centred or scrolling, in any of
the vector colours, with asterisks in a colour of their own. It is off by
default; the `marquee_set` calls in `main/main.cpp` (behind `MARQUEE_DEMO`)
show how to switch it on. Build with `-DMARQUEE_DEMO` in `EXTRA_CXXFLAGS` to
try the sample text.

## Running it on your computer

The whole emulator is platform-independent C under `core/`, and `host/` has a
harness that boots the ROMs on a Mac or Linux box. That's how the project was
developed: everything was made to work on the host first, where you can dump
frames as images and audio as WAV files, then moved to the medal.

```
cd host
make
./harness ../starwars out 30 --dsw1 0 --script "3:fire=1,3.3:fire=0" --every 1 --wav out/audio.wav
python3 ppm2png.py out/*.ppm
```

That runs 30 emulated seconds, presses fire at three seconds to start a game,
saves a frame every second, and writes the sound board's output to a WAV.
`--script` takes a comma-separated list of `time:input=value` events; the
inputs are `fire`, `coin`, `pitch`, `yaw` (0 to 255, 128 centered), `b2`,
`b3`, `b4`. `--dsw1 0` selects free play. `RENDERMASK=0x10` renders only the
speech chip, `0x0f` only the POKEYs. Set `PCTOP=1` or `SNDTOP=1` to print the
hottest program counters on either CPU, which is how the wait loops below were
found.

## How it works, briefly

`core/starwars.c` is the main board: memory map, I/O, the matrix processor and
divider (ported from MAME), banking, inputs, and the interrupt timer. The 6809
core is vecx's, compiled into the file so that memory accesses inline.

`core/avg.c` is the vector generator. It contains a faithful port of MAME's
PROM-driven sequencer and, next to it, a direct interpreter that runs the same
handler sequence per opcode without stepping the PROM. The direct version is
what the medal uses; the host harness runs both on every frame and checks that
they produce identical points, which they do.

`core/sound.c` is the sound board: a second instance of the 6809 core, the
6532 RIOT with its timer and PA7 edge interrupt, the two latches between the
CPUs, and the POKEY and TMS5220 models in `pokey.c` and `tms5220.c`.

On the medal, `main/main.cpp` runs the emulation in real time against the
wall clock, and `main/render.cpp` rasterizes each vector list into an 8-bit
frame buffer from a separate task so the 15 ms panel transfer overlaps with
emulation. The picture is 240 by 280 pixels and vector games are drawn at
whatever rate the display list takes, about 27 frames per second on the text
screens and about 40 in the battle, which is what the arcade did too.

Making it fit in a 160 MHz core came down to a few things: the direct AVG
interpreter, keeping the CPU cores in internal RAM instead of the flash cache,
and skipping the wait loops. The main program spends most of its time spinning
on a flag the interrupt handler sets, or waiting for the matrix processor, and
the sound program idles waiting for its timer; when either CPU is parked in
one of those loops with no interrupt pending, the emulator jumps straight to
the next event.

## Status and known gaps

* Plays start to finish with music, effects and speech.
* High scores are not saved across power cycles; the NVRAM is plain RAM.
* No sound board self-test or diagnostics beyond what the game itself does.
* The vecx 6809 core is GPL-3.0, which makes a built binary GPL-3.0 as a whole.
  Everything written for this project is 0BSD, so swapping that one file for a
  permissively licensed core would make the entire project 0BSD. See
  `LICENSE` and `THIRD_PARTY_NOTICES.md`.

## Credits

Steve Baines and Frank Palazzolo documented and emulated this board in MAME,
and Mathis Rosenhauer and Eric Smith did the vector generator; those files are
what the C ports here are based on. Valavan Manohararajah wrote the vecx 6809
core. The hardware drivers come from PELLETINO. And the game itself is by
Atari, 1983, with Mike Hally, Greg Rivera, Norm Avellar and Earl Vickers, and
that hotel game room.
