# ARMADA

**Namco's 1979 Galaxian, emulated on an ESP32-C6 Fiesta medal.** A Z80, a
per-column-scrolling tilemap, and the shift-register starfield that made the
cabinet famous.

*An armada is a fleet* — and this one dives at you.

A San Antonio Fiesta medal is a collectible pin. This one plays Galaxian.

---

## 🎮 Quick Start Guide

### How to Play

Hold the medal upright; Galaxian's monitor was vertical.

| Control | What it does |
|---|---|
| **Tilt left / right** | Move the fighter |
| **Middle button (BOOT)** | Fire |
| **Power button, short press** | Insert a coin, then start |
| **Power button, hold 1 s** | Power off |
| **BOOT, hold 3 s** | Sound off / on |
| **BOOT, hold 10 s** | Back to the PELLETINO menu |

Tilt is measured against however you are holding the medal *right now*. Coin up
to re-centre.

### Charging

USB-C. Holding the power button for a second cuts the battery rail.

### Troubleshooting

**The fighter drifts.** Coin up again in your playing posture; that re-centres.

---

## 🔨 Building Your Own

ARMADA has no repository of its own — it was written inside PELLETINO and this
tree is its only home.

```sh
git clone https://github.com/aedile/PELLETINO.git
cd PELLETINO/games/ARMADA
python3 tools/convert_roms.py galaxian
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 \
    idf.py -B build_docker -DIDF_TARGET=esp32c6 build
```

Flashing runs **from the host**. To build it into a multi-game medal instead,
drop `galaxian.zip` in PELLETINO's `roms/` and run `./pelletino build`.

### The ROMs

Not included. You need MAME's `galaxian` set.

---

## 🔬 Technical Details

### The original hardware

One Z80, a 32×32 character map at `0x5000`, eight sprites, eight "shells" (the
single-pixel bullets, which are their own hardware), and a handful of latches for
everything else. This is the board Konami later built Frogger on, which is why
[RIBBIT](../RIBBIT) shares so much of this code.

### Per-column scrolling

Galaxian scrolls the character map **a column at a time**, not as a whole
picture: each of the 32 columns carries its own vertical offset. The renderer
walks columns rather than rows because of it.

### The starfield

A shift register clocked off the pixel clock, tapped for "is there a star here",
with the star's colour taken from the register's own bits. It blinks because the
tap is compared against a slowly-changing counter. Reproduced as the same
generator rather than as a table of positions, so it drifts the way the cabinet's
did.

---

## 📁 Project Structure

```
core/           platform-independent emulation, shared with the host harness
  galaxian.c      memory map, latches, inputs
  galaxian_video.c character map, sprites, shells, starfield
  galaxian_sound.c the sound board
  z80/            Marat Fayzullin's portable Z80 (non-commercial)
main/           the ESP32 application
components/     display, IMU and audio HAL for the Waveshare board
host/           builds the same core on a desktop
tools/          ROM converter
```

## ⚙️ Configuration

| What | Where |
|---|---|
| DIP switches | `core/galaxian.c` |
| Tilt sensitivity and direction | `main/input.cpp` |

## 📌 Status and Known Gaps

Runs at full speed with sound.

- The sound board is synthesised rather than sampled.

## 📄 Legal Notice

### ROM files

No ROMs here. Galaxian is © 1979 Namco. This project ships a converter, not a
game.

### Third-party code

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The machine model and video
are written from MAME (BSD-3-Clause); the CPU is Marat Fayzullin's Z80, which is
**non-commercial** — a build containing this game may be shared but not sold.

### Disclaimer

Not affiliated with, endorsed by, or connected to Namco, its successors, or the
Fiesta San Antonio Commission.

## 📜 License

[0BSD](LICENSE) — no attribution required, no conditions. The Z80 core it builds
against is not; see above.
