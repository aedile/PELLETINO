# BIGTOP

**Universal's 1982 Mr. Do!, emulated on an ESP32-C6 Fiesta medal.** A Z80, two
character maps, sixteen-colour sprites, two SN76489s — and a protection PAL that
has to be answered rather than emulated.

*The big top is the main circus tent*, which is where a clown with a power ball
belongs.

A San Antonio Fiesta medal is a collectible pin. This one plays Mr. Do!

---

## 🎮 Quick Start Guide

### How to Play

Hold the medal upright; Mr. Do!'s monitor was vertical.

| Control | What it does |
|---|---|
| **Twist / tip** | Dig in one of four directions — the larger tilt wins |
| **Middle button (BOOT)** | Throw the power ball |
| **Power button, short press** | Insert a coin, then start |
| **Power button, hold 1 s** | Power off |
| **BOOT, hold 3 s** | Sound off / on |
| **BOOT, hold 10 s** | Back to the PELLETINO menu |

Digging is four-way, so the **dominant axis wins**: tilt mostly sideways and you
dig sideways, mostly forward and you dig forward. A diagonal does not dig
diagonally, because the hardware has no such direction.

Tilt is measured against however you are holding the medal *right now*. Coin up
to re-centre.

### Charging

USB-C. Holding the power button for a second cuts the battery rail.

### Troubleshooting

**He digs a direction you didn't mean.** Four-way games amplify a stale neutral
pose — coin up in your playing posture.

---

## 🔨 Building Your Own

BIGTOP has no repository of its own — it was written inside PELLETINO and this
tree is its only home.

```sh
git clone https://github.com/aedile/PELLETINO.git
cd PELLETINO/games/BIGTOP
python3 tools/convert_roms.py mrdo
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 \
    idf.py -B build_docker -DIDF_TARGET=esp32c6 build
```

Flashing runs **from the host**. To build it into a multi-game medal instead,
drop `mrdo.zip` in PELLETINO's `roms/` and run `./pelletino build`.

### The ROMs

Not included. You need MAME's `mrdo` set.

---

## 🔬 Technical Details

### The original hardware

One Z80, **two** character maps — a background and a foreground, composited with
a priority rule — sixteen-colour sprites, and two SN76489 sound chips rather than
the single one most boards of the era carried.

### The protection PAL

Universal guarded the program with a PAL whose reads the code checks. Nobody has
dumped its equations; what everyone has is the set of answers it gives. This core
returns those answers, the same ones MAME returns, at `0x9803`. It is the one
place in this emulator that is a table of known-good responses rather than a
model of the hardware, and it is called out in the source as such.

### Two sound chips

Both SN76489s are emulated and summed. The second is not a luxury — a fair amount
of the game's noise floor lives on it.

---

## 📁 Project Structure

```
core/           platform-independent emulation, shared with the host harness
  mrdo.c          memory map, inputs, the protection PAL's answers
  mrdo_video.c    the two character maps and the sprites
  sn76489.h       the sound chips
  z80/            Marat Fayzullin's portable Z80 (non-commercial)
main/           the ESP32 application
components/     display, IMU and audio HAL for the Waveshare board
host/           builds the same core on a desktop
tools/          ROM converter
```

## ⚙️ Configuration

| What | Where |
|---|---|
| DIP switches (lives, difficulty) | `core/mrdo.c` |
| Four-way tilt threshold | `main/input.cpp` |

## 📌 Status and Known Gaps

Runs at full speed with sound.

- The protection PAL is answered from a table, not modelled.

## 📄 Legal Notice

### ROM files

No ROMs here. Mr. Do! is © 1982 Universal. This project ships a converter, not a
game.

### Third-party code

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The machine model and video
are written from MAME (BSD-3-Clause); the CPU is Marat Fayzullin's Z80, which is
**non-commercial** — a build containing this game may be shared but not sold.

### Disclaimer

Not affiliated with, endorsed by, or connected to Universal, its successors, or
the Fiesta San Antonio Commission.

## 📜 License

[0BSD](LICENSE) — no attribution required, no conditions. The Z80 core it builds
against is not; see above.
