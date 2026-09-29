# QUALIFIER

Namco's 1982 *Pole Position*, running on a Fiesta medal.

Like [PELLETINO](https://github.com/aedile/PELLETINO) (Pac-Man),
[TRENCHRUNNER](https://github.com/aedile/TRENCHRUNNER) (Star Wars) and
[SWARMFIGHTER](https://github.com/aedile/SWARMFIGHTER) (Galaga), this is an
emulator of the original arcade board running the original ROM code on a
Waveshare ESP32-C6-LCD-1.69, the $20 module the San Antonio Fiesta medal is
built around. Hold it upright and turn it like a steering wheel to steer.

## The hardware being emulated

Pole Position was a big board for 1982. A Z80 handles sound, coins and the
controls, and two Zilog Z8002 16-bit CPUs do the driving: one runs the race,
the other computes the road and the scenery. All three run at 3.072 MHz and
share the video RAM. A Namco 06XX chip connects the Z80 to four custom
microcontrollers: the 51XX (coins, credits and the buttons), the 53XX (the
steering wheel and DIP switches), the 52XX (the "Prepare to qualify" speech
samples) and the 54XX (skid and crash noise). The video board draws a scrolling
mountain backdrop, a road generated line by line from a ROM with per-scanline
horizontal offsets, 64 sprites that can each be zoomed to any size, and a text
layer. Music and effects come from the 8-voice Namco wavetable generator, and
the engine note is a looped sample pushed through three analogue filters.

All of it is emulated here in plain C. The Z8002 core is generated from MAME's
Z8000 emulator. The four custom MCUs have internal ROMs that aren't part of the
standard ROM set, so they are modelled at the protocol level, the way MAME did
before those ROMs were dumped.

## What you need

* A Waveshare ESP32-C6-LCD-1.69 with the IMU.
* The MAME `polepos` ROM set. **The ROMs are not included.** The set is:

  | File | Size | What it is |
  |---|---|---|
  | `pp3_9.6h`, `pp1_10b.5h` | 8 KB, 4 KB | Z80 program |
  | `pp3_1.8m`, `pp3_2.8l` | 8 KB each | Z8002 #1 program (race) |
  | `pp3_5.4m`, `pp3_6.4l` | 8 KB each | Z8002 #2 program (road and scenery) |
  | `pp3_28.1f` | 4 KB | text characters |
  | `pp1_29.1e` | 4 KB | background tiles |
  | `pp3_25.1n`, `pp3_26.1m` | 8 KB each | 16x16 sprites |
  | `pp1_17.5n`, `pp1_19.4n`, `pp1_21.3n`, `pp1_18.5m`, `pp1_20.4m`, `pp1_22.3m` | 8 KB each | 32x32 sprites |
  | `pp1_30.3a`, `pp1_31.2a`, `pp1_32.1a` | 8 KB, 8 KB, 4 KB | road |
  | `pp1_27.1l` | 4 KB | sprite zoom table |
  | `pp1-7.8l`, `pp1-8.9l`, `pp1-9.10l` | 256 bytes each | palette |
  | `pp2-10.2h`, `pp1-11.4d`, `pp1-15.9a`, `pp1-16.10a`, `pp1-17.11a` | 256 bytes each | color lookups, vertical position |
  | `pp1-12.3c`, `pp3-6.6m` | 1 KB each | road and sprite color lookups |
  | `pp1-5.3b` | 256 bytes | sound waveforms |
  | `pp1_15.6a`, `pp1_16.5a` | 8 KB each | engine sound samples |
  | `pp2_11.2e`, `pp2_12.2f`, `pp2_13.1e` | 8 KB each | speech samples |

  The converter checks every file's CRC.
* Docker (or a native ESP-IDF 5.3), Python 3, and `esptool`.

## Building and flashing

```
python3 tools/convert_roms.py polepos           # ROMs in ./polepos, writes main/roms/polepos_roms.h
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 idf.py -B build_docker build
cd build_docker && python3 -m esptool --chip esp32c6 --port /dev/cu.usbmodem101 -b 460800 write_flash @flash_args
```

## Playing

Hold the medal upright, screen facing you. The picture is shown pixel for
pixel with black bars above and below (`ORIENTATION_PORTRAIT` in
`main/render.cpp` switches to the sideways layout).

* **Turn the medal** like a steering wheel to steer. The straight-ahead pose
  is captured when you insert a coin and on the first press of the
  accelerator, so hold it the way you want it first. About 30 degrees is
  full lock.
* **BOOT button** (middle) is the accelerator.
* **PWR button** (top), tap: insert a coin (the race starts by itself). Hold
  for half a second and release: shift between low and high gear. Hold for
  two seconds: power off.

DIP switches are set in `main/main.cpp` (`pp_set_dips`).

## Running it on your computer

```
cd host && make
./harness out 40 --every 1 --wav out/audio.wav --script "6:coin=1,6.3:coin=0,8:accel=0x90,12:gear=1,14:steer=0x30"
python3 ppm2png.py out/*.ppm
```

The harness runs the board on a Mac or Linux machine, saves frames and audio,
and reports each CPU's idle percentage. Script keys are `coin`, `gear`,
`accel`, `brake`, `steer` (an absolute wheel value) and `test`. The frames in
`host/ref/` are the reference output for that script; the emulator core is
checked against them byte for byte after every change.

## How it fits in 160 MHz

Three 3 MHz CPUs, one of them running an instruction set the ESP32-C6 has
never heard of, is a lot for a single 160 MHz RISC-V core. What makes it fit:

* The Z8002 core is generated from MAME's source by `tools/gen_z8002.py`,
  which turns the C++ handlers into C, builds a compact dispatch table, fetches
  through 256-byte page tables so every handler is a leaf function, and keeps
  the state of the running CPU in a fixed location. The handlers the game
  actually uses (listed in `core/z8000/hot_ops.txt`, from profiling) and the
  dispatch tables live in internal RAM; leaving the tables in flash, where the
  compiler quietly puts never-written statics, cost a third of the speed.
* The Z80 spends most of its time polling a mailbox the Z8002s write to; the
  emulator recognises that loop and skips to the next event.
* The steering wheel is an encoder the 53XX counts, and the game only follows
  it one count per read. The tilt control's position is fed to it a count at
  a time; handing it the position directly makes the game ignore the wheel.
* The two Z8002s never idle, so their code is simply run as fast as possible,
  interleaved every four scanlines.
* The engine filters, speech filter and noise generator are integer
  fixed-point. The ESP32-C6 has no floating-point unit, and the original
  double-precision biquads took half the CPU by themselves.
* Rendering runs in its own task and draws whatever frame is current while
  the emulator keeps going, so the 15 ms panel transfer overlaps with
  emulation. The game runs at its own 60.6 Hz with roughly half the frames
  reaching the panel.

## Credits

Ernesto Corvi, Juergen Buchmueller, Aaron Giles, Nicola Salmoria, Derrick
Renaud and the MAME team documented this board and wrote the Z8000 emulator
this one is generated from. Marat Fayzullin wrote the Z80 core. The drivers
come from PELLETINO. Pole Position is by Namco, 1982, designed by Kazunori
Sawano and Shinichiro Okamoto, and was distributed in North America by Atari.
