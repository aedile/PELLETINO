# BUGGY

**Irem's 1982 Moon Patrol, emulated on an ESP32-C6 Fiesta medal.** The M52
board's Z80, three independently scrolling paintings of the horizon, and a 6803
sound board.

*A moon buggy is what you are driving.*

A San Antonio Fiesta medal is a collectible pin. This one plays Moon Patrol.

---

## 🎮 Quick Start Guide

### How to Play

| Control | What it does |
|---|---|
| **Twist left / right** | Slow down and speed up |
| **Middle button (BOOT)** | Jump |
| **Power button, short press** | Insert a coin, then start |
| **Power button, hold 1 s** | Power off |
| **Both buttons together** | Sound off / on |
| **BOOT, hold 5 s** | Back to the PELLETINO menu |

**The guns fire themselves.** Moon Patrol's cabinet had separate fire buttons,
but on a two-button medal the shooting is automatic and the buttons go to the
things only you can time — speed and jumping. That is the whole game: get the
speed right, jump the crater.

Tilt is measured against however you are holding the medal *right now*. Coin up
to re-centre.

### Charging

USB-C. Holding the power button for a second cuts the battery rail.

### Troubleshooting

**The buggy creeps or races on its own.** Stale neutral pose — coin up while
holding the medal the way you mean to drive.

---

## 🔨 Building Your Own

BUGGY has no repository of its own — it was written inside PELLETINO and this
tree is its only home.

```sh
git clone https://github.com/aedile/PELLETINO.git
cd PELLETINO/games/BUGGY
python3 tools/convert_roms.py mpatrolw
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 \
    idf.py -B build_docker -DIDF_TARGET=esp32c6 build
```

Flashing runs **from the host**. To build it into a multi-game medal instead,
drop `mpatrolw.zip` in PELLETINO's `roms/` and run `./pelletino build`.

### The ROMs

Not included. You need MAME's `mpatrolw` set — the Williams-licensed US release,
not the Japanese `mpatrol`.

---

## 🔬 Technical Details

### The original hardware

Irem's **M52** board: a Z80 for the game with a character map and sprites, and a
separate M52 sound board built on a 6803 driving an AY-3-8910. A vblank interrupt
paces the game, and the sound board is stepped per frame alongside it.

### Three paintings

The parallax is not a tilemap trick. Behind the playfield sit **three separate
paintings**, each 256×64 pixels at 2 bits per pixel, each scrolling at its own
rate: distant mountains at the back, then either the hills or the city depending
on the stage. In front of all of it, the sprite palette's colour 0 is laid down
first as the sky.

Getting the order right is most of the renderer: sky, mountains, hills or city,
character map, sprites.

### A faster Z80 inner loop

`z80_fastop.h` carries a hot-path variant of the instruction dispatch used by
this core. Moon Patrol spends its frame scrolling three paintings as well as
running the game, so the CPU budget is tighter here than on a single-layer board.

---

## 📁 Project Structure

```
core/           platform-independent emulation, shared with the host harness
  mpatrol.c       the M52 memory and port maps, inputs, vblank, sound pacing
  mpatrol_video.c the sky, the three paintings, characters and sprites
  mpatrol_sound.c the 6803 sound board
  ay8910.c        the sound chip
  m6800.h         the 6803
  z80/            Marat Fayzullin's portable Z80 (non-commercial)
  z80_fastop.h    the hot-path dispatch
main/           the ESP32 application
components/     display, IMU and audio HAL for the Waveshare board
host/           builds the same core on a desktop
tools/          ROM converter
```

## ⚙️ Configuration

| What | Where |
|---|---|
| DIP switches | `core/mpatrol.c` |
| Tilt-to-speed response | `main/input.cpp` |

## 📌 Status and Known Gaps

Runs at full speed with sound.

- Firing is automatic; there is no button left for it on a two-button medal.

## 📄 Legal Notice

### ROM files

No ROMs here. Moon Patrol is © 1982 Irem. This project ships a converter, not a
game.

### Third-party code

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The machine model and video
are written from MAME (BSD-3-Clause); the CPU is Marat Fayzullin's Z80, which is
**non-commercial** — a build containing this game may be shared but not sold.

### Disclaimer

Not affiliated with, endorsed by, or connected to Irem, its successors, or the
Fiesta San Antonio Commission.

## 📜 License

[0BSD](LICENSE) — no attribution required, no conditions. The Z80 core it builds
against is not; see above.
