# SWARMFIGHTER

**Namco's 1981 *Galaga* on a Fiesta medal.**

This is an emulator of the original arcade board, not a remake. The medal runs
the game's original ROM code on the original hardware's terms: three Z80s, the
Namco custom chips that count coins and make the explosions, the starfield
generator and the three-voice wavetable sound, all emulated in plain C on a $20
Waveshare ESP32-C6 module with a 1.69 inch screen. Hold it upright like a
Pac-Man medal, tilt to move the fighter, press the button to fire, and the
challenging-stage jingle comes out of the little speaker on your shirt.

<!-- Photo of the assembled medal goes here, e.g.
<p align="center">
  <img src="photos/swarmfighter_medal.jpg" alt="SWARMFIGHTER Fiesta medal" width="600"/>
</p>
-->

<!-- Video link goes here, e.g.

-->

<p align="center">
  <a href="https://github.com/espressif/esp-idf"><img src="https://img.shields.io/badge/ESP--IDF-v5.3-blue" alt="ESP-IDF"></a>
  <a href="https://www.espressif.com/en/products/socs/esp32-c6"><img src="https://img.shields.io/badge/hardware-ESP32--C6-green" alt="Hardware"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-0BSD_%2B_notices-orange" alt="License"></a>
</p>

It is the second game in the family, after [PELLETINO](https://github.com/aedile/PELLETINO)
(Pac-Man and Ms. Pac-Man) and alongside [TRENCHRUNNER](https://github.com/aedile/TRENCHRUNNER)
(Star Wars), and it shares their case, battery and drivers. If you're not from
San Antonio: Fiesta medals are the pins people collect and trade every April,
and one-upping each other's medals is half the point.

---

## 🎮 Quick Start Guide

### How to Play

**The three buttons**, top to bottom as you hold the medal:

- **TOP button (side of the board):** power. Press to turn the medal on; hold
  for a second to turn it off. A short press during the attract screen
  inserts a coin and presses start for you half a second later.
- **MIDDLE button:** fire.
- **BOTTOM button:** hardware reset.
- **Tilt the medal** left and right to move your fighter along the bottom of
  the screen. About 15 degrees does it.

**A game, start to finish:**

1. Press the TOP button once on the attract screen. The medal drops a coin
   and starts the game.
2. The Galaga fleet flies in, in formation. Tilt to dodge, fire to shoot.
   Bees dive at you; the boss Galagas need two hits.
3. **The tractor beam.** A boss will try to capture your fighter. Let it, then
   shoot that boss when it dives with your ship in tow and you get the ship
   back as a dual fighter, twice the firepower.
4. Every few stages is a **challenging stage**: nothing shoots back, hit all
   forty for the bonus.
5. It goes on until you run out of fighters. You start with three and earn
   more at 20,000 and 70,000 points.

High scores live only until the medal powers off.

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

**Fighter won't move, or drifts one way**
- The tilt reading is relative to gravity, so hold the medal upright and
  square. If a direction is reversed for how you hold it, see
  *Configuration* below.

**Coin doesn't register**
- Tap the TOP button, don't hold it; a one-second hold powers the medal off.

**No sound**
- The first second after power-on is silent while the game boots; that's
  normal. Otherwise check the speaker connection if you assembled it
  yourself.

---

## 🔨 Building Your Own SWARMFIGHTER

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
- **The ROMs.** See below.

### Hardware Specifications

| Component | Specification |
|-----------|---------------|
| **MCU** | ESP32-C6, single RISC-V core at 160 MHz |
| **RAM** | 512 KB SRAM |
| **Flash** | 4 MB used (the board has 16 MB) |
| **Display** | ST7789 240×280, 40 MHz SPI with DMA |
| **Audio** | ES8311 codec over I2S, mono, 20 050 Hz |
| **IMU** | QMI8658 6-axis, roll axis used as the joystick |
| **Buttons** | GPIO9 (fire), GPIO18 (power/coin) |
| **Battery** | 803040 3.7 V LiPo 1000 mAh |

### The ROMs

The game needs the Galaga ROM set as MAME knows it, named `galaga`. **The
ROMs are not included and I can't help you find them.** You need the files
from your own board or another legitimate source. The set is:

| File | Size | What it is |
|---|---|---|
| `gg1_1b.3p`, `gg1_2b.3m`, `gg1_3.2m`, `gg1_4b.2l` | 4 KB each | CPU 1 program (the game) |
| `gg1_5b.3f` | 4 KB | CPU 2 program (attack patterns and sprites) |
| `gg1_7b.2c` | 4 KB | CPU 3 program (sound) |
| `gg1_9.4l` | 4 KB | tiles |
| `gg1_11.4d`, `gg1_10.4f` | 4 KB each | sprites |
| `prom-5.5n` | 32 bytes | palette |
| `prom-4.2n`, `prom-3.1c` | 256 bytes each | tile and sprite color lookup |
| `prom-1.1d` | 256 bytes | sound waveforms |

The converter checks every file's size and CRC and tells you which one is
wrong if any is. It also decodes the tile and sprite graphics with MAME's
layouts so the firmware just indexes pixels.

### Build and Flash

1. Clone this repository and put the ROM files in a folder called `galaga/`
   at the top level. That folder is ignored by git, as is everything the
   converter produces.

   ```bash
   git clone https://github.com/aedile/SWARMFIGHTER.git
   cd SWARMFIGHTER
   mkdir galaga        # copy the ROM files in here
   ```

2. Convert the ROMs into the C header the firmware embeds:

   ```bash
   python3 tools/convert_roms.py galaga
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

---

## 🔬 Technical Details

### The original hardware

Galaga's board is three Z80s at 3.072 MHz sharing one memory space, each with
its own job:

- **CPU 1** runs the game.
- **CPU 2** runs the attack patterns and positions the sprites.
- **CPU 3** runs the sound, driven by a nonmaskable interrupt from the video
  hardware.

Around them sit Namco's custom chips. The **06XX** is the interface between
the CPUs and the custom microcontrollers, and it raises an NMI on CPU 1 a
fixed time after every transaction. The **51XX** counts coins, keeps the
credits, and reads the joystick and buttons; the game talks to it with a small
command set. The **54XX** is the noise generator that makes the explosions.
The **05XX** generates the scrolling starfield from a 16-bit linear-feedback
shift register. The video board draws a 36 by 28 tile layer with 64 sprites
over it, and music and effects come from the three-voice Namco wavetable
generator, the same chip Pac-Man has.

The 51XX and 54XX have internal ROMs that are not part of the standard ROM
set, so they are modelled here at the protocol level, the way MAME did before
those ROMs were dumped. The 54XX explosion sound is a filtered-noise
approximation tuned by ear.

### How the emulator is put together

Everything platform-independent lives in `core/` and is plain C99 with no
dependencies. The same files run unchanged on the medal and on a Mac or Linux
host, which is how the project was developed: every piece was made to work on
the host first, where frames can be dumped as images and sound as WAV files,
then moved to the medal.

| File | What it is |
|---|---|
| `core/galaga.c` | The board: the three CPUs and their shared memory map, the interrupt and NMI timing, the 06XX transaction model, the 51XX credit and switch protocol, DIP switches, and the per-scanline scheduler. |
| `core/galaga_video.c` | Palette decoding from the PROMs, the tile layer, the 64 sprites with flips, and the 05XX starfield. |
| `core/galaga_sound.c` | The three-voice Namco wavetable generator and the high-level 54XX noise model, mixed to 16-bit mono. |
| `core/z80/` | Marat Fayzullin's portable Z80 emulator, unmodified. |
| `main/main.cpp` | Runs the emulation against the wall clock, tops up the audio DMA, reads the buttons, and prints a stats line every five seconds. |
| `main/render.cpp` | Rotates each 288×224 native frame onto the 240×280 upright panel from its own task, so the 15 ms transfer overlaps with emulation. |
| `main/input.cpp` | Turns the accelerometer's roll into left and right with hysteresis, and handles the buttons. |
| `components/` | The display, IMU and audio drivers shared with PELLETINO. |

### Making it fit in 160 MHz

Three Z80s at 3 MHz is more than a straight interpreter can manage on this
chip. What makes it fit:

- **Skipping the wait loops.** The second and third CPUs spend most of their
  time in a two-instruction loop waiting for the next interrupt. When either is
  parked there with no interrupt pending, nothing observable can happen until
  it arrives, so the emulator jumps straight to it. That takes those two CPUs
  almost entirely off the bill.
- **Page tables.** Every memory access goes through a 256-entry table of
  256-byte pages, so RAM and ROM reads are a lookup and an index, and only the
  I/O pages take the slow path.
- **The starfield as a table.** The 05XX shift register has a fixed cycle of
  65,535 states, and a star appears at a fixed set of positions within that
  cycle. Instead of stepping the register for every pixel, the renderer keeps a
  table of those positions and looks up which of them fall in the current
  frame. The output is byte-identical to the per-pixel version, verified on
  the host.
- **Rendering in a second task.** The emulator hands over a finished frame and
  keeps going; the renderer rotates and streams the previous one to the panel
  meanwhile.

The result is every frame drawn at the game's own 60.6 Hz with room to spare.

### Video

Galaga's picture is 288 by 224 pixels on a monitor turned on its side, and the
medal is held the same way. The frame is rotated in software onto the 240 by
280 panel: 224 native rows become 224 of the panel's 240 columns, centered,
and 288 native columns become 280 rows with four cropped at the top and four
at the bottom, none of which the game draws in. Colors come straight from the
palette PROM through the tile and sprite lookup PROMs; the stars get their own
64 entries.

### Audio

The wavetable generator and the noise model are mixed to signed 16-bit mono
at 20 050 Hz and fed to the ES8311 over I2S from an eight-descriptor DMA
queue. The mixer renders exactly as many samples as the DAC has consumed, so
production is locked to the I2S clock: no drift, no growing latency. Samples
the driver isn't ready to take yet wait in a pending buffer, which matters in
the first couple of seconds after boot when the DMA accounting settles;
without it the coin-up and the first jingle came out garbled.

---

## 📁 Project Structure

```
SWARMFIGHTER/
├── core/                   Platform-independent emulator (C99)
│   ├── galaga.c/h            board, CPUs, custom-chip protocols, scheduling
│   ├── galaga_video.c        tiles, sprites, starfield, palette
│   ├── galaga_sound.c        wavetable generator, 54XX noise
│   ├── galaga_internal.h     shared state between the three
│   └── z80/                  Z80 CPU core (Marat Fayzullin, non-commercial)
├── main/                   ESP32 application
│   ├── main.cpp              real-time loop and stats
│   ├── render.cpp/h          rotation and panel output task
│   ├── input.cpp/h           tilt joystick and buttons
│   └── roms/                 generated ROM header (ignored by git)
├── components/
│   ├── display/              ST7789 driver (from PELLETINO)
│   ├── imu/                  QMI8658 driver (from PELLETINO)
│   ├── audio_hal/            ES8311 + I2S with DMA-locked mixing
│   └── emu/                  builds core/ for the ESP32
├── host/                   Mac/Linux harness for the core
│   ├── harness.c             boots the ROMs, scripts inputs, dumps frames and WAV
│   └── ppm2png.py            frame converter
├── tools/
│   └── convert_roms.py       ROM set -> C header, graphics decoded, CRC checks
├── partitions.csv          flash layout
├── sdkconfig.defaults      ESP-IDF configuration
├── LICENSE                 0BSD for this project's code
└── THIRD_PARTY_NOTICES.md  Z80 core, MAME
```

---

## ⚙️ Configuration

Everything below is a define or a single call; rebuild and reflash after
changing it.

**Game settings**, `main/main.cpp`:

```cpp
ga_set_dips(0xf7, 0x97);   // easy, demo sounds, upright; 1 coin 1 play, bonus at 20K/70K, 3 fighters
```

The bits are documented next to `ga_set_dips` in `core/galaga.h`, with the
same meaning as the DIP switches on the board.

**Tilt**, `main/input.cpp`:

```cpp
#define TILT_ON  20   // roll (about 1/128 g per unit) at which the fighter starts moving
#define TILT_OFF 12   // and at which it stops: the gap is hysteresis, so it doesn't chatter
```

The line `int lr = -roll;` in the same file sets the direction; drop the minus
sign if left and right are reversed for how you hold it.

**Buttons**, `main/input.cpp`: the coin-then-start delay and the one-second
power-off hold are constants at the top of `input_update`.

---

## 💻 Running It on Your Computer

The host harness boots the same core on a Mac or Linux machine:

```bash
cd host
make
./harness out 40 --every 1 --wav out/audio.wav \
    --script "20:coin=1,20.2:coin=0,21:start=1,21.2:start=0,23:fire=1,23.2:fire=0"
python3 ppm2png.py out/*.ppm
```

That runs 40 emulated seconds, drops a coin at 20 seconds, presses start at
21 and fires at 23, saves an upright frame every second, and writes the sound
to a WAV. `--script` takes a comma-separated list of `time:input=value`
events; the inputs are `coin`, `start`, `fire`, `left`, `right`. `--dswa` and
`--dswb` set the DIP switches. The harness reports each CPU's idle percentage
as it runs, which is how the wait loops were found, and it is where the
starfield table was checked against the per-pixel generator.

---

## 🔧 Developer Troubleshooting

**`convert_roms.py` complains about a file**
- The size or CRC doesn't match the `galaga` set. Other revisions of the
  game exist (`galagao`, `galagamw`, `gallag`) and are not what this was
  tested with.

**esptool says it can't find `bootloader/bootloader.bin`**
- Run it from inside `build_docker/`. The paths in `flash_args` are relative
  to the build directory. This one bites everybody once.

**The board is missing from `/dev`**
- Charge-only cable, a hub that isn't passing data, or the board is in a bad
  state. Plug it straight into the computer with a known data cable and press
  the BOTTOM (reset) button.

**Reading the serial log**
- 115200 baud on the same USB port. Every five seconds the firmware prints a
  line with emulated, drawn, skipped and dropped frame counts, milliseconds
  per second spent emulating, rendering, presenting and mixing, how much of
  the time the second and third CPUs were parked, free heap, and the three
  program counters. Roughly 300 frames per five seconds with `emu` well under
  1000 is healthy.

---

## 📌 Status and Known Gaps

- Plays start to finish with music and effects, including the tractor beam
  and the dual fighter.
- High scores are not saved across power cycles.
- The 54XX explosion sounds are an approximation; the real chip's program is
  not emulated.
- The Z80 core is free for non-commercial use only, which a built binary
  inherits. See `LICENSE` and `THIRD_PARTY_NOTICES.md`.

---

## 📄 Legal Notice

### ROM files

This project requires the original Galaga arcade ROMs, which are **not
included** and are not licensed by this project:

- ROM files and the generated header are excluded from version control.
- You must own the original board or obtain the ROMs from a legitimate source.
- The game code is copyright Namco. This project is for education and
  preservation.

### Third-party code

- **Z80 CPU emulator** by Marat Fayzullin, `core/z80/`, free for
  non-commercial use ([fms.komkon.org/EMUL8](http://fms.komkon.org/EMUL8/)).
- **MAME** (BSD-3-Clause): the board model, the 05XX starfield, the 51XX and
  54XX protocol models and the wavetable generator are written from MAME's
  Galaga driver and its documentation.
- **ESP-IDF** by Espressif Systems, Apache 2.0.
- The ST7789, QMI8658 and ES8311 drivers come from PELLETINO and are 0BSD.

The full notices are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

### Disclaimer

This project is not affiliated with, endorsed by or sponsored by Bandai Namco
Entertainment. *Galaga* is a trademark of Bandai Namco Entertainment Inc.

---

## 🙏 Credits

- **Nicola Salmoria, Aaron Giles, Mathis Rosenhauer and the MAME team**
  documented this board; the 05XX starfield description and the 51XX and
  54XX protocol models come from their work.
- **Marat Fayzullin** wrote the Z80 core.
- **Till Harbaum's [Galagino](https://github.com/harbaum/galagino)** showed
  that Galaga on an ESP32 was a sane idea in the first place.
- **Waveshare** for putting a screen, a codec, an IMU and a battery charger on
  one small board.
- **Claude** (Anthropic) for coding and documentation assistance.
- **Namco, 1981**: Shigeru Yokoyama and the team that made the best fixed
  shooter there is.

## 📜 License

This project's own code is released under the Zero-Clause BSD license; see
[LICENSE](LICENSE). The Z80 core's non-commercial terms apply to a built
binary; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

---

<p align="center">
  <strong>¡Viva Fiesta! 🎉</strong>
  <br><br>
  <em>Questions? Found a bug?</em>
  <br>
  Open an issue on <a href="https://github.com/aedile/SWARMFIGHTER">GitHub</a>
</p>
