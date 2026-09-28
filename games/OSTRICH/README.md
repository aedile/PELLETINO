# OSTRICH

**Williams' 1982 Joust, emulated on an ESP32-C6 Fiesta medal.** A 6809, the
Special Chip 1 blitter, a 4-bit bitmap, and a sound board whose 6808 makes every
noise in software.

*You are riding an ostrich.* That is the whole premise and it is a good one.

A San Antonio Fiesta medal is a collectible pin. This one plays Joust.

---

## 🎮 Quick Start Guide

### How to Play

| Control | What it does |
|---|---|
| **Twist left / right** | Run |
| **Middle button (BOOT)** | Flap |
| **Power button, short press** | Insert a coin, then start |
| **Power button, hold 1 s** | Power off |
| **Both buttons together** | Sound off / on |
| **BOOT, hold 10 s** | Back to the PELLETINO menu |

Joust is a flap game: you gain height by tapping BOOT repeatedly, not by holding
it. Two controls total, which is why it suits a medal so well.

Tilt is measured against however you are holding the medal *right now*. Coin up
to re-centre.

### Charging

USB-C. Holding the power button for a second cuts the battery rail.

### Troubleshooting

**You drift when you mean to hover.** Stale neutral pose — coin up in your
playing posture.

---

## 🔨 Building Your Own

OSTRICH has no repository of its own — it was written inside PELLETINO and this
tree is its only home.

```sh
git clone https://github.com/aedile/PELLETINO.git
cd PELLETINO/games/OSTRICH
python3 tools/convert_roms.py joust
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 \
    idf.py -B build_docker -DIDF_TARGET=esp32c6 build
```

Flashing runs **from the host**. To build it into a multi-game medal instead,
drop `joust.zip` in PELLETINO's `roms/` and run `./pelletino build`.

### The ROMs

Not included. You need MAME's `joust` set.

---

## 🔬 Technical Details

### The original hardware

A Motorola **6809** with a bank of ROM or RAM switched over the low 36 KB, two
PIAs for the inputs, interrupts fired at specific scanlines, and **Special Chip
1** — a blitter that moves rectangles of the bitmap far faster than the CPU
could. The sound board is a separate 6808 that synthesises every effect in
software and pushes it to an 8-bit DAC; there are no sample ROMs.

### The bitmap

Not a tilemap — a raw 4-bit-per-pixel bitmap, laid out **column-major**: the byte
at `y + 256 × (x / 2)` holds pixels `x` and `x + 1` of line `y`, the even pixel in
the high nibble. The palette is sixteen bytes of RGB 3-3-2.

That layout is why the blitter exists, and why drawing it on a row-major LCD
means transposing as you go rather than copying spans.

### The 6809

vecx's `e6809`, included in-tree rather than linked so its bus accessors inline —
which matters, because this game leans on the CPU harder than the tilemap games
do. **That core is GPL-3.0, and it makes this game image GPL-3.0 as a whole.**

---

## 📁 Project Structure

```
core/           platform-independent emulation, shared with the host harness
  joust.c         memory map, the ROM/RAM bank, the PIAs, the SC1 blitter
  joust_video.c   the 4-bit column-major bitmap and its palette
  joust_sound.c   the sound board's 6808 and its DAC
  e6809.c         vecx's 6809 (GPL-3.0)
  m6800.h         the 6808 on the sound board
main/           the ESP32 application
components/     display, IMU and audio HAL for the Waveshare board
host/           builds the same core on a desktop
tools/          ROM converter
```

## ⚙️ Configuration

| What | Where |
|---|---|
| DIP switches | `core/joust.c` |
| Tilt sensitivity and direction | `main/input.cpp` |

## 📌 Status and Known Gaps

Runs at full speed with sound.

- Two-player simultaneous play is the point of Joust and the medal has one set of
  controls, so this is the single-player game.

## 📄 Legal Notice

### ROM files

No ROMs here. Joust is © 1982 Williams Electronics. This project ships a
converter, not a game.

### Third-party code

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The machine model and video
are written from MAME (BSD-3-Clause); the 6809 is vecx's, under **GPL-3.0**.

### Disclaimer

Not affiliated with, endorsed by, or connected to Williams Electronics, its
successors, or the Fiesta San Antonio Commission.

## 📜 License

The code written for this game is [0BSD](LICENSE). **Because it embeds vecx's
GPL-3.0 6809, the built game image is GPL-3.0 as a whole** — if you distribute a
medal carrying it, you must offer this source under the GPL, which this
repository provides. The full text is in [LICENSES/](LICENSES/).
