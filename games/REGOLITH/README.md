# REGOLITH

**Atari's 1979 Lunar Lander, emulated on an ESP32-C6 Fiesta medal.** A 6502, a
Digital Vector Generator, and an analogue throttle driven by how far you tip the
medal away from you.

*Regolith is the loose dust and broken rock covering the Moon* — the stuff you
are trying not to hit too hard.

A San Antonio Fiesta medal is a collectible pin. This one plays Lunar Lander.

---

## 🎮 Quick Start Guide

### How to Play

| Control | What it does |
|---|---|
| **Twist left / right** | Rotate the lander |
| **Tip away from you** | Throttle — **analogue**, the further you tip the harder it burns |
| **Middle button (BOOT)** | Abort |
| **Power button, short press** | Insert a coin, then start |
| **Power button, hold 1 s** | Power off |
| **BOOT, hold 3 s** | Sound off / on |
| **BOOT, hold 10 s** | Back to the PELLETINO menu |

The throttle is the one genuinely analogue control on the medal, and it is the
reason this game plays well on a tilt sensor at all: the cabinet had a physical
lever, and tip angle maps onto it directly rather than being quantised to a
direction.

Tilt is measured against however you are holding the medal *right now*. Coin up
to re-centre in your playing posture — **especially here**, since a stale neutral
means a permanent gentle burn.

### Charging

USB-C. Holding the power button for a second cuts the battery rail.

### Troubleshooting

**The engine is always slightly on.** The neutral pose is stale. Coin up.

---

## 🔨 Building Your Own

REGOLITH has no repository of its own — it was written inside PELLETINO and this
tree is its only home.

```sh
git clone https://github.com/aedile/PELLETINO.git
cd PELLETINO/games/REGOLITH
python3 tools/convert_roms.py llander
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 \
    idf.py -B build_docker -DIDF_TARGET=esp32c6 build
```

Flashing runs **from the host**. To build it into a multi-game medal instead,
drop `llander.zip` in PELLETINO's `roms/` and run `./pelletino build`.

### The ROMs

Not included. You need MAME's `llander` set.

---

## 🔬 Technical Details

### The original hardware

Asteroids' board a few months earlier: the same 6502, the same **Digital Vector
Generator**, and the same trick of drawing with a beam instead of pixels. 1 KB of
work RAM at `0x0000` mirrored up through `0x1FFF`, and a 2 KB display list at
`0x4000` that the DVG walks as a little program of its own — draw, scale, jump,
return.

What Lunar Lander adds over Asteroids is the **analogue throttle**, read through
an ADC rather than as a switch.

### Shared with AEROLITE

The DVG here is [AEROLITE](../AEROLITE)'s, unchanged — Asteroids and Lunar
Lander really did share it. The 6502 is the instruction-stepped core in
`m6502fast.h` rather than a cycle-stepped one; nothing in this game depends on
the video hardware stealing bus cycles, so the faster model is sound.

### Drawing vectors on an LCD

The DVG produces line segments with an intensity, not pixels. They are rasterised
into the portrait frame with the intensity carried into the colour, so the
brightness ramp the vector monitor gave you for free is approximated rather than
lost.

---

## 📁 Project Structure

```
core/           platform-independent emulation, shared with the host harness
  llander.c       memory map, the DVG, sound, the throttle ADC
  m6502fast.h     instruction-stepped 6502
main/           the ESP32 application
components/     display, IMU and audio HAL for the Waveshare board
host/           builds the same core on a desktop
tools/          ROM converter
```

## ⚙️ Configuration

| What | Where |
|---|---|
| DIP switches (fuel, coinage) | `core/llander.c` |
| Throttle range and deadband | `main/input.cpp` |
| Tilt direction | `main/input.cpp` |

## 📌 Status and Known Gaps

Runs at full speed with sound.

- The sounds are synthesised; the cabinet made them with analogue circuits.
- The vector picture is rasterised, so very short segments can alias.

## 📄 Legal Notice

### ROM files

No ROMs here. Lunar Lander is © 1979 Atari. This project ships a converter, not
a game.

### Third-party code

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The machine model is
written from MAME (BSD-3-Clause). **This game carries no non-commercial core** —
it is one of the fully permissive titles in PELLETINO's
[LICENSING.md](../../LICENSING.md).

### Disclaimer

Not affiliated with, endorsed by, or connected to Atari, its successors, or the
Fiesta San Antonio Commission.

## 📜 License

[0BSD](LICENSE) — no attribution required, no conditions.
