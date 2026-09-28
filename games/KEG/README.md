# KEG

**Bally Midway's 1984 Root Beer Tapper, emulated on an ESP32-C6 Fiesta medal.**
The MCR 91490 board, the 91464 Super Video Generator, and the SSIO sound board's
two AY-3-8910s — a 512×480 picture drawn at half size.

*A keg is what you are pouring from.*

A San Antonio Fiesta medal is a collectible pin. This one plays Root Beer Tapper.

---

## 🎮 Quick Start Guide

### How to Play

| Control | What it does |
|---|---|
| **Twist left / right** | Run along the bar |
| **Tip toward / away** | Change bars |
| **Middle button (BOOT)** | Pour — **hold** to keep filling |
| **Power button, short press** | Insert a coin, then start |
| **Power button, hold 1 s** | Power off |
| **BOOT, hold 3 s** | Sound off / on |
| **BOOT, hold 10 s** | Back to the PELLETINO menu |

Pour is a **hold**, not a tap: the longer you hold BOOT the fuller the glass, and
letting go sends it. That is the cabinet's handle, and it is the one control here
where how long you press matters as much as when.

Tilt is measured against however you are holding the medal *right now*. Coin up
to re-centre.

### Charging

USB-C. Holding the power button for a second cuts the battery rail.

### Troubleshooting

**You keep changing bars by accident.** The two axes are close together in a
hurried game; coin up to re-centre, and lean more deliberately along the bar.

---

## 🔨 Building Your Own

KEG has no repository of its own — it was written inside PELLETINO and this
tree is its only home.

```sh
git clone https://github.com/aedile/PELLETINO.git
cd PELLETINO/games/KEG
python3 tools/convert_roms.py rbtapper
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 \
    idf.py -B build_docker -DIDF_TARGET=esp32c6 build
```

Flashing runs **from the host**. To build it into a multi-game medal instead,
drop `rbtapper.zip` in PELLETINO's `roms/` and run `./pelletino build`.

### The ROMs

Not included. You need MAME's `rbtapper` set — the root beer version, not the
licensed-beer original.

---

## 🔬 Technical Details

### The original hardware

Bally Midway's **MCR** family: a 91490 CPU board with a Z80 and a **Z80 CTC**
that paces the game with IM2 interrupts, and an SSIO sound board carrying a
second Z80 and two AY-3-8910s behind four data latches. The picture comes from
the 91464 Super Video Generator — a 16×16 tile map with 32×32 sprites over it.

### Half a picture

The hardware draws **512×480**. The medal's panel is 240×280. Tiles are drawn at
half their resolution rather than scaling a finished frame, which keeps the tile
edges clean instead of smearing them, at the cost of dropping every other pixel
of the sprites.

### Two CPUs on one core

Both processors are Marat Fayzullin's Z80, which keeps its state in globals. A
selector says whose bus a memory callback belongs to, so game and sound board are
stepped in turn without a second copy of the emulator.

---

## 📁 Project Structure

```
core/           platform-independent emulation, shared with the host harness
  tapper.c        the 91490 board, the CTC, both CPUs, the SSIO latches
  tapper_video.c  the 91464's tiles and sprites, halved
  ay8910.c        the two sound chips
  z80/            Marat Fayzullin's portable Z80 (non-commercial)
main/           the ESP32 application
components/     display, IMU and audio HAL for the Waveshare board
host/           builds the same core on a desktop
tools/          ROM converter
```

## ⚙️ Configuration

| What | Where |
|---|---|
| DIP switches | `core/tapper.c` |
| Tilt axes and thresholds | `main/input.cpp` |
| Pour-hold behaviour | `main/input.cpp` |

## 📌 Status and Known Gaps

Runs at full speed with sound.

- Sprites lose half their pixels to the half-resolution draw.

## 📄 Legal Notice

### ROM files

No ROMs here. Root Beer Tapper is © 1984 Bally Midway. This project ships a
converter, not a game.

### Third-party code

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The machine model and video
are written from MAME (BSD-3-Clause); the CPUs are Marat Fayzullin's Z80, which
is **non-commercial** — a build containing this game may be shared but not sold.

### Disclaimer

Not affiliated with, endorsed by, or connected to Bally Midway, its successors,
or the Fiesta San Antonio Commission.

## 📜 License

[0BSD](LICENSE) — no attribution required, no conditions. The Z80 core it builds
against is not; see above.
