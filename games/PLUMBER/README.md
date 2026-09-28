# PLUMBER

**Nintendo's 1983 Mario Bros., emulated on an ESP32-C6 Fiesta medal.** A Z80 for
the game, an 8039 playing the tunes into a DAC, and three-plane sprites over a
scrolling character map.

*Plumber* is the job the brothers actually have.

A San Antonio Fiesta medal is a collectible pin. This one plays Mario Bros.

---

## 🎮 Quick Start Guide

### How to Play

| Control | What it does |
|---|---|
| **Tilt left / right** | Run |
| **Middle button (BOOT)** | Jump |
| **Power button, short press** | Insert a coin, then start |
| **Power button, hold 1 s** | Power off |
| **Both buttons together** | Sound off / on |
| **BOOT, hold 10 s** | Back to the PELLETINO menu |

Tilt is measured against however you are holding the medal *right now*. Coin up
to re-centre.

### Charging

USB-C. Holding the power button for a second cuts the battery rail.

### Troubleshooting

**Mario creeps when you are not tilting.** Stale neutral pose — coin up in the
posture you mean to play in.

---

## 🔨 Building Your Own

PLUMBER has no repository of its own — it was written inside PELLETINO and this
tree is its only home.

```sh
git clone https://github.com/aedile/PELLETINO.git
cd PELLETINO/games/PLUMBER
python3 tools/convert_roms.py mario
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 \
    idf.py -B build_docker -DIDF_TARGET=esp32c6 build
```

Flashing runs **from the host**. To build it into a multi-game medal instead,
drop `mario.zip` in PELLETINO's `roms/` and run `./pelletino build`.

### The ROMs

Not included. You need MAME's `mario` set.

---

## 🔬 Technical Details

### The original hardware

Donkey Kong's kind of board grown up. A Z80 for the game with a 1 KB character
map and 1 KB of sprite RAM, and a separate **Intel 8039** microcontroller whose
only job is music — it plays samples and tunes out to a DAC while the Z80 gets
on with the game. The monitor is rotated, so the picture stands up.

### Two processors, one frame

The 8039 is emulated (`mcs48.h`) rather than stubbed with recordings, because
the tunes are generated, not sampled. It is stepped alongside the Z80 and its DAC
writes are mixed into the same output buffer the rest of the medal uses.

### Three-plane sprites

Sprite pixels come from three bit planes rather than a packed byte, which is
cheap in hardware and awkward in software. They are unpacked once at ROM
conversion time instead of per frame.

---

## 📁 Project Structure

```
core/           platform-independent emulation, shared with the host harness
  mario.c         memory map, inputs, interrupts
  mario_video.c   character map and sprites
  mario_sound.c   the 8039 and its DAC
  mcs48.h         the 8039 core
  z80/            Marat Fayzullin's portable Z80 (non-commercial)
main/           the ESP32 application
components/     display, IMU and audio HAL for the Waveshare board
host/           builds the same core on a desktop
tools/          ROM converter
```

## ⚙️ Configuration

| What | Where |
|---|---|
| DIP switches (lives, bonus) | `core/mario.c` |
| Tilt sensitivity and direction | `main/input.cpp` |

## 📌 Status and Known Gaps

Runs at full speed with sound.

- Two-player alternating play works; there is only one set of controls.

## 📄 Legal Notice

### ROM files

No ROMs here. Mario Bros. is © 1983 Nintendo. This project ships a converter,
not a game.

### Third-party code

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The machine model and video
are written from MAME (BSD-3-Clause); the CPU is Marat Fayzullin's Z80, which is
**non-commercial** — a build containing this game may be shared but not sold.

### Disclaimer

Not affiliated with, endorsed by, or connected to Nintendo, its successors, or
the Fiesta San Antonio Commission.

## 📜 License

[0BSD](LICENSE) — no attribution required, no conditions. The Z80 core it builds
against is not; see above.
