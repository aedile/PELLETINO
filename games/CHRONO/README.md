# CHRONO

**Konami's 1982 Time Pilot, emulated on an ESP32-C6 Fiesta medal.** Two Z80s —
one for the game, one on the Pooyan sound board with a pair of AY-3-8910s — and
a free-scrolling sky you fly in any of eight directions.

*Chrono-*, from the Greek for time.

A San Antonio Fiesta medal is a collectible pin. This one plays Time Pilot.

---

## 🎮 Quick Start Guide

### How to Play

| Control | What it does |
|---|---|
| **Twist and tip** | Point the plane — **eight directions**, the way you lean is the way you fly |
| **Middle button (BOOT)** | Fire |
| **Power button, short press** | Insert a coin, then start |
| **Power button, hold 1 s** | Power off |
| **Both buttons together** | Sound off / on |
| **BOOT, hold 10 s** | Back to the PELLETINO menu |

Time Pilot is one of the few games here that is genuinely **eight-way**, so
diagonals matter and the tilt is read as an angle rather than as a dominant axis.
It is the most natural fit for a tilt sensor in the whole collection: the plane
always points where you lean.

Tilt is measured against however you are holding the medal *right now*. Coin up
to re-centre.

### Charging

USB-C. Holding the power button for a second cuts the battery rail.

### Troubleshooting

**The plane circles slowly on its own.** Stale neutral pose — coin up in your
playing posture.

---

## 🔨 Building Your Own

CHRONO has no repository of its own — it was written inside PELLETINO and this
tree is its only home.

```sh
git clone https://github.com/aedile/PELLETINO.git
cd PELLETINO/games/CHRONO
python3 tools/convert_roms.py timeplt
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 \
    idf.py -B build_docker -DIDF_TARGET=esp32c6 build
```

Flashing runs **from the host**. To build it into a multi-game medal instead,
drop `timeplt.zip` in PELLETINO's `roms/` and run `./pelletino build`.

### The ROMs

Not included. You need MAME's `timeplt` set.

---

## 🔬 Technical Details

### The original hardware

A Z80 for the game and a second Z80 on the **Pooyan sound board**, so named
because Konami used the same board across several titles. The sound board
carries two AY-3-8910s and takes commands through a latch. An LS259 addressable
latch handles the odd control bits — interrupt enable, screen flip, coin
counters — one bit at a time.

The picture is a 32×32 character map with **24 sprites** over it, and colour
comes through lookup PROMs rather than a writable palette.

### Characters in front of sprites

A handful of characters are flagged by the program to draw *over* the sprites
rather than under them, which is how the clouds pass in front of your plane. The
renderer makes two passes over the map for it: everything, then sprites, then the
flagged characters again.

### Two CPUs on one core

Both processors are Marat Fayzullin's Z80, which keeps its state in globals. A
selector says whose bus a memory callback belongs to, so the two are stepped in
turn without carrying two copies of the emulator.

---

## 📁 Project Structure

```
core/           platform-independent emulation, shared with the host harness
  timeplt.c       memory map, the LS259 latches, both CPUs
  timeplt_video.c character map, 24 sprites, the over-sprite pass
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
| DIP switches (lives, bonus) | `core/timeplt.c` |
| Eight-way tilt response | `main/input.cpp` |

## 📌 Status and Known Gaps

Runs at full speed with sound.

## 📄 Legal Notice

### ROM files

No ROMs here. Time Pilot is © 1982 Konami. This project ships a converter, not a
game.

### Third-party code

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The machine model and video
are written from MAME (BSD-3-Clause); the CPUs are Marat Fayzullin's Z80, which
is **non-commercial** — a build containing this game may be shared but not sold.

### Disclaimer

Not affiliated with, endorsed by, or connected to Konami, its successors, or the
Fiesta San Antonio Commission.

## 📜 License

[0BSD](LICENSE) — no attribution required, no conditions. The Z80 core it builds
against is not; see above.
