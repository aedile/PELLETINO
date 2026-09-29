# TRENCHRUNNER

**Atari's 1983 *Star Wars* arcade game on a Fiesta medal.**

This is an emulator of the original arcade board, not a remake. The medal runs
the game's original ROM code, on the original hardware's terms: two 6809 CPUs,
the vector generator, the 3D matrix processor, four POKEY sound chips and the
speech synthesizer, all emulated in plain C on a $20 Waveshare ESP32-C6 module
with a 1.69 inch screen. Hold it upright like any other medal, tilt to fly,
press the button to fire, and "Red Five standing by" comes out of the little
speaker on your shirt.

<!-- Photo of the assembled medal goes here, e.g.
<p align="center">
  <img src="photos/trenchrunner_medal.jpg" alt="TRENCHRUNNER Fiesta medal" width="600"/>
</p>
-->

<!-- Video link goes here, e.g.

-->

<p align="center">
  <a href="https://github.com/espressif/esp-idf"><img src="https://img.shields.io/badge/ESP--IDF-v5.3-blue" alt="ESP-IDF"></a>
  <a href="https://www.espressif.com/en/products/socs/esp32-c6"><img src="https://img.shields.io/badge/hardware-ESP32--C6-green" alt="Hardware"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-0BSD_%2B_GPL--3.0_core-orange" alt="License"></a>
</p>

It is the third game in the family, after [PELLETINO](https://github.com/aedile/PELLETINO)
(Pac-Man and Ms. Pac-Man) and [SWARMFIGHTER](https://github.com/aedile/SWARMFIGHTER)
(Galaga), and it shares their case, battery and drivers. If you're not from
San Antonio: Fiesta medals are the pins people collect and trade every April,
and one-upping each other's medals is half the point.

---

## Why

When I was a kid there was a Star Wars cabinet in the hotel my family stayed at
every year. I spent hours on it, and just as many hours standing in front of it
watching the attract mode cycle when I was out of quarters. Owning one became
the standard-issue childhood dream. Years later I got the Arcade1Up version,
which scratches part of the itch, but from the moment I saw this tiny board
with a screen on it the real dream became this: the actual game, the actual
code, running on a pin on my shirt.

---

## 🎮 Quick Start Guide

### How to Play

**The three buttons**, top to bottom as you hold the medal:

- **TOP button (side of the board):** power. Press to turn the medal on; hold
  for a second to turn it off. A short press during a game also inserts a
  coin and re-centers the yoke.
- **MIDDLE button:** fire. It also starts a game and picks your Death Star on
  the selection screen. Holding it does two more things, described below.
- **BOTTOM button:** hardware reset.
- **Tilt the medal** to fly. It's the flight yoke: tip the top edge toward you
  to pull up, away to dive, and turn the medal like a wheel to steer.

**A game, start to finish:**

1. The game runs in free play, so just press the MIDDLE button on the title
   screen.
2. Pick a starting Death Star: tilt to move the cursor to the easy, medium or
   hard one and fire. The countdown picks for you if you wait.
3. **Space.** TIE fighters come at you and Darth Vader's ship lurks among
   them. Shoot the fireballs they throw or take them on your shields.
4. **The Death Star surface.** Fly over the towers and laser bunkers; shoot
   the tops off every tower for a bonus.
5. **The trench.** Dodge the catwalks, shoot the gun emplacements, and put a
   torpedo in the exhaust port when it opens. Then it all starts again,
   faster, until your shields are gone.

Your shield count is at the top of the screen. You start with six and get one
back for finishing a wave. High scores live only until the medal powers off.

**Hold the medal the way you want to fly before your first shot.** The medal
records how you're holding it the first time you press fire and treats that as
centered. If it feels off, a short press of the TOP button re-centers it.

**Left alone, it flies itself.** Fifteen seconds into the attract screen the
medal starts a game and plays it: it lets the countdown pick the easy Death
Star, shoots the TIE fighters and their fireballs, flies the trench shooting
only the fireballs (never the wall guns), and puts a shot into the exhaust
port when it opens. It takes the trigger the way a pilot would, a shot a second
on average in short bursts, rather than a barrage. Touch any control and it
hands over at once and stays out until your game is over. How trigger-happy it
is lives in three numbers at the top of `core/autoplay.c` (`SHOT_REFILL_US`,
`SHOT_BURST`, `SHOT_GAP_US`).

### Sound off

Hold the MIDDLE button for three seconds and let go: the sound toggles off, or
back on. That's for wearing the medal somewhere it needs to be quiet. The game
keeps running silently underneath.

### The easter egg

Keep holding the MIDDLE button. At thirteen seconds the sound comes on, the
game stops, and the medal plays a video clip from its flash. When the clip
ends the game restarts at its attract screen, like a fresh power-up. Each long
hold plays the next clip in turn, and pressing fire during a clip ends it
early.

The clips are not part of this repository. Adding your own is covered under
*Building from Source* below. A medal without a media image simply ignores the
long hold.

### Charging

- **Charging port:** USB-C on the side of the medal
- **Battery life:** several hours of play on a full charge
- **Battery:** 803040 3.7 V LiPo, 1000 mAh, the same one PELLETINO uses
- Plenty of USB-C cables are charge-only. That's fine for charging, but not
  for flashing.

### Troubleshooting

**Medal won't turn on**
- Charge it over USB-C for at least 30 minutes.
- Press the TOP button once. Holding it turns the medal *off*.

**Tilt is backwards or off-center**
- Press fire once while holding the medal comfortably, or short-press the TOP
  button, to re-center. If a direction is genuinely reversed, see
  *Configuration* below for the two sign flips.

**No sound**
- Hold the MIDDLE button for three seconds and release: the sound may simply
  have been switched off.
- The first second after power-on is silent while the game boots; that's
  normal.

**Long hold does nothing**
- The medal has no media image. See *Adding video clips* below.

**Screen shows a red square**
- The firmware found no media partition. Reflash with the partition table in
  this repository (16 MB flash).

---

## 🔨 Building Your Own TRENCHRUNNER

The medal is electrically identical to PELLETINO: the same Waveshare board,
the same battery, the same 3D-printed three-piece case. Everything about the
enclosure, the screws and the assembly order is in the
[PELLETINO README](https://github.com/aedile/PELLETINO#-building-your-own-pelletino),
and the STL files are in that repository's `model/` directory. The only
difference is what you flash onto it.

**Shopping list**

- Waveshare [ESP32-C6-LCD-1.69](https://www.waveshare.com/esp32-c6-lcd-1.69.htm),
  the version **with the IMU**. The plain LCD version has no tilt sensor.
- 803040 3.7 V LiPo 1000 mAh battery with a 1.25 mm connector
- 4x M2×4 mm and 4x M2×16 mm screws
- Printed case from PELLETINO's `model/` folder

---

## 🛠️ Building from Source

### What You'll Need

- **Docker**, or a native ESP-IDF 5.3 install. All the commands below use
  Docker so there is nothing else to set up.
- **Python 3** for the ROM converter, and **esptool** for flashing
  (`pip install esptool`).
- **ffmpeg**, only if you want to add video clips.
- **The ROMs.** See below.

### Hardware Specifications

| Component | Specification |
|-----------|---------------|
| **MCU** | ESP32-C6, single RISC-V core at 160 MHz |
| **RAM** | 512 KB SRAM (about 270 KB used by the emulator) |
| **Flash** | 16 MB (1 MB firmware, the rest for video clips) |
| **Display** | ST7789 240×280, 40 MHz SPI with DMA |
| **Audio** | ES8311 codec over I2S, mono, 20 050 Hz |
| **IMU** | QMI8658 6-axis, used as the flight yoke |
| **Buttons** | GPIO9 (fire), GPIO18 (power/coin) |
| **Battery** | 803040 3.7 V LiPo 1000 mAh |

### The ROMs

The game needs the Star Wars ROM set as MAME knows it, named `starwars`.
**The ROMs are not included and I can't help you find them.** You need the
files from your own board or another legitimate source. The set is:

| File | Size | What it is |
|---|---|---|
| `136021.102`, `136021.203`, `136021.104`, `136021.206` | 8 KB each | main program |
| `136021.214` | 16 KB | banked program ROM |
| `136021.105` | 4 KB | vector ROM (fixed display-list routines) |
| `136021.110`, `.111`, `.112`, `.113` | 1 KB each | matrix processor microcode |
| `136021-105.1l` | 256 bytes | AVG state PROM |
| `136021.107`, `136021.208` | 8 KB each | sound program |

The converter checks every file's size and CRC and tells you which one is
wrong if any is.

### Build and Flash

1. Clone this repository and put the ROM files in a folder called `starwars/`
   at the top level. That folder is ignored by git, as is everything the
   converter produces.

   ```bash
   git clone https://github.com/aedile/TRENCHRUNNER.git
   cd TRENCHRUNNER
   mkdir starwars      # copy the ROM files in here
   ```

2. Convert the ROMs into the C header the firmware embeds:

   ```bash
   python3 tools/convert_roms.py starwars
   ```

3. Build:

   ```bash
   docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 \
       idf.py -B build_docker build
   ```

   With a native ESP-IDF it is `idf.py set-target esp32c6` once, then
   `idf.py build`.

4. Plug the medal in over USB-C. It shows up as `/dev/cu.usbmodem*` on a Mac
   and `/dev/ttyACM*` on Linux; macOS asks once whether to allow the accessory.
   Then flash **from inside the build directory**, because the paths in
   `flash_args` are relative to it:

   ```bash
   cd build_docker
   python3 -m esptool --chip esp32c6 --port /dev/cu.usbmodem101 -b 460800 \
       write_flash @flash_args
   cd ..
   ```

   The medal reboots into the game. If the board never appears on USB, the
   cable is almost always the reason.

### Adding video clips

Put a couple of short videos somewhere and run:

```bash
tools/encode_media.sh clip1.mp4 clip2.mp4
python3 -m esptool --chip esp32c6 --port /dev/cu.usbmodem101 -b 921600 \
    write_flash 0x110000 media/media.bin
```

The script encodes each clip as 240×136 MJPEG at 24 frames per second with
32 kHz mono MP3 audio, packs them into one image, and the second command
writes that image to the `media` partition. Reflashing the firmware leaves the
clips alone. Roughly 15 MB is available, which is around a minute of video per
5 MB. The `media/` folder is ignored by git.

### The marquee

The black bars above and below the picture can carry text in the game's own
vector lettering: one line per bar, static and centered or scrolling, in any
of the vector colors, with asterisks in a color of their own. It is off by
default. The `marquee_set` calls in `main/main.cpp`, behind the `MARQUEE_DEMO`
define, show how to switch it on; build with
`-e EXTRA_CXXFLAGS=-DMARQUEE_DEMO` added to the Docker command (and
`reconfigure` before `build`) to see the sample text.

---

## 🔬 Technical Details

### The original hardware

The Star Wars board is a good deal more exotic than Pac-Man's.

- **Two Motorola 6809E CPUs at 1.512 MHz.** One runs the game, the other runs
  the sound board. They talk through a pair of latches and a handshake.
- **Atari's Analog Vector Generator (AVG).** There is no frame buffer. The CPU
  writes a display list into RAM and a small sequencer, driven by a 256-byte
  state PROM, walks the list and steers the beam of a color vector monitor.
- **A matrix processor** built from AMD 2901 bit-slice chips and four
  microcode PROMs, plus a hardware divider. This is what does the 3D math:
  the CPU loads a matrix and a stream of points, kicks the processor, and
  polls until it's done.
- **The sound board:** a MOS 6532 RIOT for timing and I/O, four POKEY chips
  for music and effects, and a TMS5220 speech synthesizer.

### How the emulator is put together

Everything platform-independent lives in `core/` and is plain C99 with no
dependencies. The same files run unchanged on the medal and on a Mac or Linux
host, which is how the project was developed: every piece was made to work on
the host first, where frames can be dumped as images and sound as WAV files,
then moved to the medal.

| File | What it is |
|---|---|
| `core/starwars.c` | The main board: memory map, I/O, the matrix processor and divider (ported from MAME), ROM banking, inputs, the 3 kHz interrupt timer, and the real-time scheduling of both CPUs. |
| `core/e6809.c` | The 6809 CPU core from vecx, compiled twice (once per board) so that each board's memory accesses inline. |
| `core/avg.c` | The vector generator. A faithful port of MAME's PROM-driven sequencer, and next to it a direct interpreter that runs the same handler sequence per opcode without stepping the PROM. The medal uses the direct version; the host harness runs both on every frame and checks the points are identical. |
| `core/sound.c` | The sound board: the second 6809, the 6532 RIOT with its timer and PA7 edge interrupt, the two latches, and the mixer. |
| `core/pokey.c` | Four POKEYs: polynomial counters, dividers, filters and volume, rendered at the output rate. |
| `core/tms5220.c` | The TMS5220: LPC-10 frame decoding, the lattice filter and the chirp table, following MAME. It synthesizes in emulated time, driven by the sound CPU's cycles, into a ring buffer the mixer drains, so its FIFO and READY line behave the way the sound program expects. |
| `main/main.cpp` | Runs the emulation against the wall clock, tops up the audio DMA, reads the buttons, and dispatches the long-hold gestures. |
| `main/render.cpp` | Rasterizes each vector list into an 8-bit frame buffer in its own task and streams it to the panel, so the 15 ms transfer overlaps with emulation. |
| `main/input.cpp` | Turns the accelerometer into a yoke, debounces the buttons, and times the long holds. |
| `main/egg.cpp` | The clip player: JPEG frames through the decoder in the ESP32-C6's ROM straight into 16-row panel strips, MP3 through libhelix into the audio stream. It borrows the renderer's frame buffer while the game is stopped and needs about 36 KB of heap on top. |
| `main/marquee.cpp` | The stroke font and scroller for the letterbox bars. |
| `components/` | The display, IMU and audio drivers shared with PELLETINO, and the libhelix MP3 decoder. |

### Making it fit in 160 MHz

Two 1.5 MHz 6809s plus the vector generator and the matrix processor are more
than a straight interpreter can manage on this chip. What makes it fit:

- **The direct AVG interpreter.** Stepping the state PROM one micro-instruction
  at a time cost three times what walking the display list with the equivalent
  handlers does, and the host harness proves the two agree on every point.
- **Cores in internal RAM.** The 6809 core and the AVG together overflow the
  32 KB flash cache; the linker script pins them, and the ROMs the CPU and AVG
  fetch from are copied into RAM at boot.
- **Skipping the wait loops.** The main program spends most of its time
  spinning on a flag the interrupt handler sets, or polling the matrix
  processor's busy bit, and the sound program idles waiting for its timer.
  When either CPU is parked in one of those loops with no interrupt pending,
  nothing observable can happen until the next event, so the emulator jumps
  straight to it. That recovers about a third of the main CPU and nearly all
  of the sound CPU.
- **Rendering in a second task.** The emulator hands over a vector list and
  keeps going; the renderer rasterizes and streams the previous one to the
  panel meanwhile.

The result runs the game in real time with roughly a fifth of the CPU to
spare. Vector games draw at whatever rate the display list takes, about 27
frames per second on the text screens and about 40 in the battle, which is
what the arcade did too.

### Video

The picture is 240 by 280 pixels, held upright. The arcade monitor showed a
4:3 landscape picture, so the game is letterboxed across the middle of the
panel at 240 by 180 with 50-pixel bars above and below; that's where the
marquee text lives. Line intensity comes straight from the AVG's intensity
bits, and the three color bits map the way MAME's `color111` does: bit 2 red,
bit 1 green, bit 0 blue.

### Audio

The four POKEYs and the speech chip are mixed to signed 16-bit mono at
20 050 Hz and fed to the ES8311 over I2S from an eight-descriptor DMA queue.
The mixer renders exactly as many samples as the DAC has consumed, so
production is locked to the I2S clock: no drift, no growing latency. Samples
the driver isn't ready to take yet wait in a pending buffer, which matters in
the first couple of seconds after boot when the DMA accounting settles;
without it, most of the sound board's first seconds were dropped on the floor
and the boot speech came out garbled.

### Flash layout

| Offset | Size | Contents |
|---|---|---|
| `0x0000` | 32 KB | bootloader |
| `0x8000` | 4 KB | partition table |
| `0x9000` | 24 KB | NVS (unused) |
| `0x10000` | 1 MB | firmware, ROMs embedded |
| `0x110000` | 14.9 MB | `media` partition: the packed video clips |

---

## 📁 Project Structure

```
TRENCHRUNNER/
├── core/                   Platform-independent emulator (C99)
│   ├── starwars.c/h          main board, matrix processor, scheduling
│   ├── e6809.c/h             6809 CPU core (vecx, GPL-3.0)
│   ├── avg.c/h               analog vector generator
│   ├── sound.c/h             sound board: RIOT, latches, mixer
│   ├── pokey.c/h             POKEY sound chip
│   └── tms5220.c/h           TMS5220 speech synthesizer
├── main/                   ESP32 application
│   ├── main.cpp              real-time loop, gestures, stats
│   ├── render.cpp/h          vector rasterizer and panel output task
│   ├── input.cpp/h           tilt yoke, buttons, long holds
│   ├── egg.cpp/h             video clip player
│   ├── marquee.cpp/h         letterbox-bar text
│   └── roms/                 generated ROM header (ignored by git)
├── components/
│   ├── display/              ST7789 driver (from PELLETINO)
│   ├── imu/                  QMI8658 driver (from PELLETINO)
│   ├── audio_hal/            ES8311 + I2S with DMA-locked mixing
│   ├── emu/                  builds core/ for the ESP32, pins it in RAM
│   └── helix_mp3/            libhelix MP3 decoder (RPSL)
├── host/                   Mac/Linux harness for the core
│   ├── harness.c             boots the ROMs, scripts inputs, dumps frames and WAV
│   └── ppm2png.py            frame converter
├── tools/
│   ├── convert_roms.py       ROM set -> C header, with CRC checks
│   ├── encode_media.sh       videos -> MJPEG + MP3
│   └── pack_media.py         clips -> media.bin
├── partitions.csv          16 MB flash layout
├── sdkconfig.defaults      ESP-IDF configuration
├── LICENSE                 0BSD for this project's code
├── THIRD_PARTY_NOTICES.md  vecx, MAME, libhelix, TJpgDec
└── LICENSES/GPL-3.0.txt
```

---

## ⚙️ Configuration

Everything below is a define or a single call; rebuild and reflash after
changing it.

**Game settings**, `main/main.cpp`:

```cpp
sw_set_dips(0x90, 0x00);   // 6 shields, easy, 1 bonus shield, demo sounds; free play
```

The DIP switch bits are documented next to the call. The second byte's low two
bits set the coinage; `0x02` is one coin per play.

**Tilt**, `main/input.cpp`:

```cpp
#define FULL_DEFLECTION_DEG 20.0f   // this much tilt = yoke at its stop
#define DEADBAND_DEG        1.5f    // no response inside this
#define YAW_SIGN   (+1.0f)          // flip if steering is reversed
#define PITCH_SIGN (+1.0f)          // flip if pitch is reversed
```

**Long holds**, `main/input.cpp`:

```cpp
#define HOLD_SOUND_US   3000000     // sound toggle
#define HOLD_EGG_US    13000000     // video clip
#define PWR_LONG_PRESS_MS 1000      // power off
```

**Orientation**, `main/render.cpp`: `ORIENTATION_PORTRAIT 1` is the upright
layout; `0` restores the sideways layout, which uses more of the panel but
loses the marquee bars.

**Marquee**, `main/main.cpp`: see *The marquee* above.

---

## 💻 Running It on Your Computer

The host harness boots the same core on a Mac or Linux machine:

```bash
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
speech chip, `0x0f` only the POKEYs. `PCTOP=1` and `SNDTOP=1` print the
hottest program counters on either CPU, which is how the wait loops were
found. The harness also checks the two vector generator implementations
against each other on every frame and reports any mismatch.

---

## 🔧 Developer Troubleshooting

**`convert_roms.py` complains about a file**
- The size or CRC doesn't match the `starwars` set. A different revision of a
  ROM will usually still run, but the converter is telling you it isn't the
  one this was tested with.

**esptool says it can't find `bootloader/bootloader.bin`**
- Run it from inside `build_docker/`. The paths in `flash_args` are relative
  to the build directory. This one bites everybody once.

**A `-D` flag in `EXTRA_CXXFLAGS` seems to have no effect**
- CMake caches the flags. Add `reconfigure` before `build` in the `idf.py`
  command whenever you change them.

**The board is missing from `/dev`**
- Charge-only cable, a hub that isn't passing data, or the board is in a bad
  state. Plug it straight into the computer with a known data cable and press
  the BOTTOM (reset) button.

**`no media image at 0x110000` in the serial log**
- Expected until you flash `media/media.bin`. The game runs normally; only the
  long hold is inert.

**`scratch ... < ...` or `MP3 decoder init failed` in the log**
- The clip player couldn't get memory. It needs the renderer's frame buffer
  plus about 36 KB of heap; check what else the firmware is allocating.

**Reading the serial log**
- 115200 baud on the same USB port. Every five seconds the firmware prints a
  line with emulated and drawn frame counts, milliseconds per second spent in
  each part of the emulator, the wait-loop skip percentages, audio underruns,
  and free heap. `underruns 0` and `emu-total` under 1000 is healthy.

---

## 📌 Status and Known Gaps

- Plays start to finish with music, effects and speech.
- High scores are not saved across power cycles; the X2212 NVRAM is modelled
  as plain RAM.
- The sound board's self-test is not implemented; nothing in normal play uses
  it.
- The vecx 6809 core is GPL-3.0, which makes a built binary GPL-3.0 as a
  whole. Everything written for this project is 0BSD, so swapping that one
  file for a permissively licensed core would make the entire project 0BSD.

---

## 📄 Legal Notice

### ROM files

This project requires the original Star Wars arcade ROMs, which are **not
included** and are not licensed by this project:

- ROM files and the generated header are excluded from version control.
- You must own the original board or obtain the ROMs from a legitimate source.
- The game code is copyright Atari and Lucasfilm. This project is for
  education and preservation.

### Third-party code

- **vecx 6809 core** by Valavan Manohararajah, `core/e6809.c`, GPL-3.0. The
  modifications made here are listed in the file's header.
- **MAME** ports (BSD-3-Clause): the vector generator, the matrix processor,
  the POKEY and the TMS5220 are written from MAME's `avgdvg.cpp`,
  `starwars_m.cpp`, `pokey.cpp` and `tms5220.cpp`.
- **libhelix-mp3** by RealNetworks, `components/helix_mp3/`, RealNetworks
  Public Source License. Used by the clip player only.
- **TJpgDec** by ChaN, as shipped in the ESP32-C6 ROM, used by the clip player.
- **ESP-IDF** by Espressif Systems, Apache 2.0.
- The ST7789, QMI8658 and ES8311 drivers come from PELLETINO and are 0BSD.

The full notices are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

### Disclaimer

This project is not affiliated with, endorsed by or sponsored by Atari,
Lucasfilm or Disney. *Star Wars* is a trademark of Lucasfilm Ltd. The game is
Atari's, 1983.

---

## 🙏 Credits

- **Steve Baines and Frank Palazzolo** documented and emulated this board in
  MAME, and **Mathis Rosenhauer and Eric Smith** did the vector generator.
  Those files are what the C ports here are based on.
- **Valavan Manohararajah** wrote the vecx 6809 core.
- **The MAME team** for the POKEY and TMS5220 models and thirty years of
  documentation.
- **Waveshare** for putting a screen, a codec, an IMU and a battery charger on
  one small board.
- **Claude** (Anthropic) for coding and documentation assistance.
- **Atari, 1983**: Mike Hally, Greg Rivera, Norm Avellar and Earl Vickers. And
  that hotel game room.

## 📜 License

This project's own code is released under the Zero-Clause BSD license; see
[LICENSE](LICENSE). Because the 6809 core is GPL-3.0, a built binary is
GPL-3.0 as a whole; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

---

<p align="center">
  <strong>¡Viva Fiesta! May the Force be with you.</strong>
  <br><br>
  <em>Questions? Found a bug?</em>
  <br>
  Open an issue on <a href="https://github.com/aedile/TRENCHRUNNER">GitHub</a>
</p>
