# GRIDDLE

**Data East's 1982 Burger Time, emulated on an ESP32-C6 Fiesta medal.** Two
6502s — one sealed in DECO's epoxy block with its opcodes scrambled — a
three-plane character map, and two AY-3-8910s.

*A griddle is what you build the burger on.*

A San Antonio Fiesta medal is a collectible pin. This one plays Burger Time.

---

## 🎮 Quick Start Guide

### How to Play

Hold the medal upright; Burger Time's monitor was vertical.

| Control | What it does |
|---|---|
| **Twist / tip** | Walk in one of four directions — the larger tilt wins |
| **Middle button (BOOT)** | Pepper |
| **Power button, short press** | Insert a coin, then start |
| **Power button, hold 1 s** | Power off |
| **Both buttons together** | Sound off / on |
| **BOOT, hold 5 s** | Back to the PELLETINO menu |

Walking is four-way, so the **dominant axis wins** — a diagonal resolves to
whichever way you are leaning further. Ladders need a clean vertical lean.

Tilt is measured against however you are holding the medal *right now*. Coin up
to re-centre.

### Charging

USB-C. Holding the power button for a second cuts the battery rail.

### Troubleshooting

**He walks past the ladder instead of climbing.** Four-way games punish a stale
neutral pose. Coin up in your playing posture.

---

## 🔨 Building Your Own

GRIDDLE has no repository of its own — it was written inside PELLETINO and this
tree is its only home.

```sh
git clone https://github.com/aedile/PELLETINO.git
cd PELLETINO/games/GRIDDLE
python3 tools/convert_roms.py btime
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 \
    idf.py -B build_docker -DIDF_TARGET=esp32c6 build
```

Flashing runs **from the host**. To build it into a multi-game medal instead,
drop `btime.zip` in PELLETINO's `roms/` and run `./pelletino build`.

### The ROMs

Not included. You need MAME's `btime` set.

---

## 🔬 Technical Details

### The original hardware

Two 6502s. The game's lives inside **DECO's CPU-7 block** — an epoxy-potted
module that scrambles the opcode bus, so the program reads as nonsense unless you
unscramble it the way the block does. The second 6502 sits on the sound board
driving two AY-3-8910s.

The picture is a 32×32 map of 8×8 characters in **three bit planes**, eight 16×16
sprites, and an optional background of 16×16 tiles whose layout comes from a PROM
and which scrolls independently.

### Unscrambling CPU-7

The descramble is a fixed bit permutation applied to opcode fetches only —
operand reads come through untouched. Doing it at ROM conversion time rather than
per fetch would be wrong for exactly that reason, so the core distinguishes the
two kinds of read.

### The faster 6502

This uses the instruction-stepped core in `m6502fast.h` rather than a
cycle-stepped one. Nothing in Burger Time depends on the video hardware stealing
bus cycles, which is the case that model cannot represent.

---

## 📁 Project Structure

```
core/           platform-independent emulation, shared with the host harness
  btime.c         memory map, the CPU-7 descramble, interrupts
  btime_video.c   three-plane characters, sprites, the PROM background
  btime_sound.c   the sound board's 6502
  ay8910.c        the two sound chips
  m6502fast.h     instruction-stepped 6502
main/           the ESP32 application
components/     display, IMU and audio HAL for the Waveshare board
host/           builds the same core on a desktop
tools/          ROM converter
```

## ⚙️ Configuration

| What | Where |
|---|---|
| DIP switches (lives, bonus) | `core/btime.c` |
| Four-way tilt threshold | `main/input.cpp` |

## 📌 Status and Known Gaps

Runs at full speed with sound.

## 📄 Legal Notice

### ROM files

No ROMs here. Burger Time is © 1982 Data East. This project ships a converter,
not a game.

### Third-party code

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The machine model and video
are written from MAME (BSD-3-Clause). **This game carries no non-commercial
core** — it is one of the fully permissive titles in PELLETINO's
[LICENSING.md](../../LICENSING.md).

### Disclaimer

Not affiliated with, endorsed by, or connected to Data East, its successors, or
the Fiesta San Antonio Commission.

## 📜 License

[0BSD](LICENSE) — no attribution required, no conditions.
