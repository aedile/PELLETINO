# VAUS

**Taito's 1986 Arkanoid, emulated on an ESP32-C6 Fiesta medal.** A Z80 for the
game, a 68705 microcontroller guarding it, and a YM2149 for the sound — and the
one arcade control a tilt sensor is genuinely good at.

A San Antonio Fiesta medal is a collectible pin. This one plays Arkanoid.

---

## 🎮 Quick Start Guide

### How to Play

You hold the medal upright, like a phone. Arkanoid's monitor was already
vertical, so the picture stands up the same way you do.

| Control | What it does |
|---|---|
| **Twist left / right** | Move the Vaus |
| **Middle button** | Fire (once you have the laser) |
| **Both buttons together** | Sound off and on |
| **Power button, short press** | Insert a coin and start |
| **Power button, hold 1 second** | Power off |

Arkanoid's cabinet had a **spinner**, and the closest thing a medal has to one
is a twist — rotate it in your fingers rather than leaning it. What the IMU
gives us is the direction of gravity within the panel's own plane, so the twist
maps straight onto an absolute paddle position: where you hold the medal is
where the Vaus is. No velocity to build up, no drift to fight. Thirty-two
degrees each way covers the whole playfield.

The centre is whatever twist you were holding when you first picked the medal
up, and it is captured again on every coin. It **cannot** be captured while the
medal is lying flat: gravity then points straight out of the screen and the
in-plane angle is noise.

### Charging

USB-C. Holding the power button for a second cuts the battery rail.

### Troubleshooting

**The Vaus goes the wrong way.** `X_SIGN` in `main/input.cpp`.

**The paddle runs out of travel too soon, or too late.** `PADDLE_SWEEP_DEG` in
the same file. Larger means you have to twist further for the same movement.

**It feels twitchy, or it feels laggy.** `PADDLE_SMOOTH` — the fraction of each
new reading that is taken, the rest being history. Higher is more immediate and
more jittery.

---

## 🔨 Building Your Own

```sh
git clone https://github.com/aedile/VAUS.git
cd VAUS
python3 tools/convert_roms.py /path/to/arkanoid
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 \
    idf.py -B build_docker build
cd build_docker && esptool --chip esp32c6 -p /dev/cu.usbmodemXXXX \
    -b 460800 write_flash @flash_args
```

Flashing has to run **from inside `build_docker`** and **from the host** —
Docker Desktop on macOS cannot reach USB.

### The ROMs

Not included. You need MAME's `arknoidu` set — the US release. The converter
checks the CRCs, with one exception noted in its header: the MCU image in that
set is a reconstruction rather than a dump, so it is accepted loosely. It is
valid 6805 code and the emulator runs it.

---

## 🔬 Technical Details

### The original hardware

A Z80 at 6 MHz, a **68705P5 microcontroller** wired between the CPU and the
inputs, and a YM2149 at 1.5 MHz for the sound.

### The MCU is not optional

Arkanoid's protection is not a checksum you can patch out. The 68705 owns the
inputs, and the two talk through a pair of semaphore flip-flops: the Z80 leaves
a byte in a latch and sets one flag, the MCU takes it and clears that flag, then
answers and sets the other. Get any part of it wrong and the game puts **BAD
HARDWARE** on the screen and stops. `core/m6805.h` is a 6805 interpreter written
for this project so that the real MCU program runs.

Three details had to be right before it would boot at all:

- **Bit 7 of the control latch holds the MCU in reset**, and the board holds it
  there from power-on. The game releases it just before asking for the first
  byte, so the MCU has to start from its reset vector at that moment rather than
  having been running since power-up and wandered off.
- **Bit 2 of the address separates 0xD008 from 0xD00C.** Decoding on the higher
  bits alone sends the semaphore reads to an unused joystick port, and the game
  concludes the hardware is broken.
- **Port C bit 1 is inverted** relative to the flag it reports: it is high when
  the host has *taken* the MCU's last byte, not when the MCU has sent one.

### The boot self-test, and why interleaving matters

Before the title screen the Z80 hands the MCU **all 48 KB of its program ROM**,
one byte at a time, waiting on the semaphore after each. Running the two
processors on a fixed time slice caps that at one byte per slice — twelve
seconds of black screen. On the board they run concurrently and the answer comes
back in microseconds, so here the MCU runs on demand the moment the Z80 gives it
something to do, with the cycles charged against its frame allowance. Boot is
about a second and a half.

### The palette bank does not fit in a pixel

The three colour PROMs hold 512 colours, addressed as 64 palettes of eight, and
a single latch bit picks which half of them the whole screen uses. A frame
buffer pixel is a byte, so an index of 480 comes back as 224 and the scene is
painted out of the wrong half of the PROM — which is what turned the story
screen's night sky white. The bank applies to the entire frame, so it belongs in
the palette rather than in the pixel: pixels carry the colour's low five bits
and the pen, and the palette handed to the display is whichever 256 entries were
current when the frame was drawn.

### The characters stay in flash

There are 4096 characters at three bits a pixel. Expanding them once at init —
the trick that buys the frame rate on the other medals — is a quarter of a
megabyte, which this board does not have; asking for it fails the boot outright.
So the three planes are read straight from the ROM, three bytes per tile row,
and the pixels shifted out of them. The graphics stay in flash and the reads go
through the instruction cache, which is cheap enough at this size.

### The sound chip

`core/ay8910.c` is a YM2149. Three things in it are worth naming, because all
three are audible:

- **The envelope is a sixteen-step ramp**, and a full cycle is 256 × EP clocks —
  so one step is two ticks of the chip's clock/8 timebase, not one. Register 13's
  four bits then decide whether there is a second cycle, whether it holds, and
  whether it turns round.
- **The chip toggles faster than we sample it**, nine or ten times between output
  samples. Reading the level once per sample aliases every tone above a couple of
  kHz. Counting how many of those steps the channel spent high and averaging is
  the cheap way to band-limit it.
- **The output only swings upward from zero**, so it carries a fat DC term that
  the cabinet's coupling capacitor removed. A one-pole high pass does the same
  job here.

---

## 📁 Project Structure

```
core/           platform-independent emulation, shared with the host harness
  arkanoid.c        Z80, memory map, the control latch, the MCU handshake
  arkanoid_video.c  tilemap and sprites, the two palette banks
  ay8910.c          the YM2149
  m6805.h           an MC6805 interpreter, for the 68705P5
  z80/              Marat Fayzullin's Z80 (non-commercial — see notices)
main/           the ESP32 application
components/     display, IMU and audio HAL for the Waveshare board
host/           builds the same core on a desktop; frames to PPM, audio to WAV
tools/          ROM converter
```

## 💻 Running It on Your Computer

```sh
cd host && make
./harness /tmp/out 30 --every 0.5 --wav /tmp/ak.wav \
    --script "4:coin=1,4.2:coin=0,5:start=1,5.2:start=0"
```

The harness writes the **native** 256×224 frame, which is the picture lying on
its side. Turn it 90° clockwise to look at it the right way up.

## ⚙️ Configuration

| What | Where |
|---|---|
| DIP switches | `ak_set_dips()` in `main/main.cpp` |
| Twist direction | `X_SIGN` in `main/input.cpp` |
| Twist sweep | `PADDLE_SWEEP_DEG` in `main/input.cpp` |
| Paddle smoothing | `PADDLE_SMOOTH` in `main/input.cpp` |

## 📌 Status and Known Gaps

The game runs at full speed — 59.2 Hz, correct timing — with sound.

- **The panel refreshes at about 38 of those frames.** The game logic and its
  timing are correct; the display is the limit, not the emulation.
- Sprites are drawn a whole frame at a time rather than a scanline at a time.
- Cocktail flip-screen is read from the latch and applied to sprites only.

## 📄 Legal Notice

### ROM files

No ROMs here. Arkanoid is © 1986 Taito. This project ships a converter, not a
game.

### Third-party code

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The machine model and
video are written from MAME (BSD-3-Clause). **`core/z80/` is Marat Fayzullin's
Z80 emulator, which its author licenses for non-commercial use only** — that
term applies to this repository as a whole in any commercial context.

### Disclaimer

Not affiliated with, endorsed by, or connected to Taito, its successors, or the
Fiesta San Antonio Commission.

## 🙏 Credits

The MAME team, whose `arkanoid.cpp` documents the semaphore protocol and the
control latch — neither of which you would work out from the outside without a
logic analyser and a lot of patience.

## 📜 License

[0BSD](LICENSE) for the project's own code — but see the note above about the
Z80 core, which is non-commercial.
