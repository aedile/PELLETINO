# PHALANX

**Taito's 1978 Space Invaders, emulated on an ESP32-C6 Fiesta medal.** An 8080,
a hardware shift register, and a frame of bits the CPU draws one pixel at a time.

*A phalanx is a massed infantry formation advancing in ranks* — which is what is
coming down the screen at you.

A San Antonio Fiesta medal is a collectible pin. This one plays Space Invaders.

---

## 🎮 Quick Start Guide

### How to Play

Hold the medal upright. Space Invaders' monitor was vertical, so the picture
stands up the same way you do.

| Control | What it does |
|---|---|
| **Tilt left / right** | Move the cannon |
| **Middle button (BOOT)** | Fire |
| **Power button, short press** | Insert a coin, then start |
| **Power button, hold 1 s** | Power off |
| **Both buttons together** | Sound off / on |
| **BOOT, hold 5 s** | Back to the PELLETINO menu |

Tilt is measured against however you are holding the medal *right now*. Coin up
to re-centre, in the posture you mean to play in.

### Charging

USB-C. Holding the power button for a second cuts the battery rail.

### Troubleshooting

**The cannon drifts.** The neutral pose is stale — press coin again while
holding the medal the way you intend to play.

**Nothing moves at all.** A medal lying flat is not "held", and its tilt is
ignored until picked up.

---

## 🔨 Building Your Own

PHALANX has no repository of its own — it was written inside PELLETINO and this
tree is its only home.

```sh
git clone https://github.com/aedile/PELLETINO.git
cd PELLETINO/games/PHALANX
python3 tools/convert_roms.py invaders
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 \
    idf.py -B build_docker -DIDF_TARGET=esp32c6 build
```

Flashing runs **from the host** — Docker Desktop on macOS cannot reach USB. To
build it as part of a multi-game medal instead, drop `invaders.zip` in
PELLETINO's `roms/` and run `./pelletino build`.

### The ROMs

Not included. You need MAME's `invaders` set. The converter checks every CRC.

---

## 🔬 Technical Details

### The original hardware

An Intel 8080 at 2 MHz, 8 KB of program ROM, and 1 KB of work RAM followed by a
frame buffer at `0x2400`. No video hardware worth the name: the picture is a
**1-bit bitmap the program writes itself**, and the cabinet got its colour from a
strip of coloured gel glued across the glass. Two interrupts a frame, at the
middle and the end of the screen.

The one piece of help the board gives the CPU is a **shift register** on the
ports — feed it bytes and an offset, read back a shifted result. That is how the
game moves sprites horizontally without the 8080 doing bit work it has no time
for.

### An 8080 on a Z80 core

The 8080 is a strict subset of the Z80, so this runs on Marat Fayzullin's
portable Z80 rather than carrying a second CPU emulator. The undocumented flag
behaviour the two chips disagree about is not something Space Invaders relies on.

### The colour gel

Reproduced as a lookup over the 1-bit frame rather than as a texture: bands by
scanline, with the player's area and the shields tinted the way the physical
overlay tinted them.

---

## 📁 Project Structure

```
core/           platform-independent emulation, shared with the host harness
  invaders.c      memory map, ports, the shift register, interrupts
  invaders_sound.c the discrete sounds, synthesised
  z80/            Marat Fayzullin's portable Z80 (non-commercial)
main/           the ESP32 application
components/     display, IMU and audio HAL for the Waveshare board
host/           builds the same core on a desktop
tools/          ROM converter
```

## ⚙️ Configuration

| What | Where |
|---|---|
| DIP switches (lives, bonus) | `dip_in2` in `core/invaders.c` |
| Tilt sensitivity and direction | `main/input.cpp` |

## 📌 Status and Known Gaps

Runs at full speed with sound.

- The sounds are synthesised, not sampled: the cabinet made them with discrete
  analogue circuits, and there are no recordings here.
- Cocktail flip-screen is not implemented.

## 📄 Legal Notice

### ROM files

No ROMs here. Space Invaders is © 1978 Taito. This project ships a converter,
not a game.

### Third-party code

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The machine model is
written from MAME (BSD-3-Clause); the CPU is Marat Fayzullin's Z80, which is
**non-commercial** — a build containing this game may be shared but not sold.

### Disclaimer

Not affiliated with, endorsed by, or connected to Taito, its successors, or the
Fiesta San Antonio Commission.

## 📜 License

[0BSD](LICENSE) — no attribution required, no conditions. The Z80 core it builds
against is not; see above.
