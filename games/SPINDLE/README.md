# SPINDLE

**Atari's 1981 Tempest, emulated on an ESP32-C6 Fiesta medal.** A colour vector
game, running at sixty frames a second with every one of them drawn.

A San Antonio Fiesta medal is a collectible pin. This one plays Tempest.

---

## 🎮 Quick Start Guide

### How to Play

Hold the medal upright and **twist it**. Tempest's cabinet control is a spinner,
and a twist is the closest thing a medal has to one — this is the best fit of
any game in the set.

| Control | What it does |
|---|---|
| **Twist left / right** | The claw, around the rim of the web |
| **Middle button** | Fire |
| **Middle button, double tap** | Superzapper |
| **Both buttons together** | Sound: loud, quiet, off |
| **Power button, short press** | Insert a coin and start |
| **Power button, hold 1 second** | Power off |

A real spinner has no centre — it is a free-running counter, and the game only
ever sees how far it moved since it last looked. So what gets handed over here
is the *change* in twist angle: turn and hold, and the claw stops where you left
it, exactly as a knob behaves. Four degrees of twist moves it one position, and
a full lap of the web is seventy-two.

The superzapper is a double tap because the cabinet has two buttons and the
medal has one. That suits it: you get two a level, and you never want one by
accident in the middle of firing.

### Charging

USB-C. Holding the power button for a second cuts the battery rail.

### Troubleshooting

**The claw goes the wrong way.** `SPIN_SIGN` in `main/input.cpp`.

**Too twitchy, or too slow.** `DEG_PER_COUNT` in the same file — how far you
have to twist to move one position.

---

## 🔨 Building Your Own

```sh
git clone https://github.com/aedile/SPINDLE.git
cd SPINDLE
python3 tools/convert_roms.py /path/to/tempest
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 \
    idf.py -B build_docker build
cd build_docker && esptool --chip esp32c6 -p /dev/cu.usbmodemXXXX \
    -b 460800 write_flash @flash_args
```

Flashing has to run **from inside `build_docker`** and **from the host** —
Docker Desktop on macOS cannot reach USB.

### The ROMs

Not included. You need MAME's `tempest` set — revision 3, revised hardware. The
converter checks every CRC. The six math box PROMs in that set are not used; see
below.

---

## 🔬 Technical Details

### The original hardware

A 6502 at 1.512 MHz, an Analog Vector Generator with colour, two POKEYs, a math
box on its own board, and an EAROM holding the high scores.

### The inputs come through the sound chips

There is no input port for the spinner. Each POKEY has eight pot inputs meant
for paddles, and on this board every one of them is wired through a resistor to
a single bit of an input port: a set bit charges its capacitor immediately, a
clear bit takes 228 scanlines. The game writes POTGO, waits, and reads ALLPOT —
and what comes back is a whole input port, read one bit at a time. That is how
the spinner, the fire buttons and the cabinet switches get in.

So `core/pokey.c` had to grow a pot section that has nothing to do with sound.

### The vector generator never stops

Asteroids and Star Wars run their display list to a halt instruction, and you
draw when the beam stops. Tempest leaves the AVG running in an endless loop and
rewrites vector RAM underneath it. There is no halt to wait for, so a frame ends
where the sequencer jumps back to address zero — which is a frame boundary
invented by the emulator, because on a real vector monitor there are no frames
at all.

### Two things about the AVG that are easy to get wrong

Both of these produce a sequencer that runs, consumes cycles, and draws nothing
you would recognise:

- **The program counter is not a CPU address.** It is relative to the base of
  vector RAM, which on this board is 0x2000.
- **The low address bit is flipped on every fetch.** Vector memory is sixteen
  bits wide and its two halves sit the other way round from the CPU's view. The
  Star Wars AVG does *not* do this, which is exactly why inheriting that code
  without checking gave a screen with one stray line on it.

### The colour does not fit in a pixel

The vector colour is an index into sixteen bytes of colour RAM, and every bit is
inverted on the way to the DACs. A full RGB565 frame buffer at 240×280 is 134 KB,
which this board cannot spare next to the emulator — so a pixel is one byte:
three bits of hue and five of brightness. Where two vectors of the same hue
cross, the brightnesses add, which is most of what makes a vector picture look
like one. Where different hues cross, the brighter wins. Real phosphor would mix
them; Tempest almost never crosses colours, and 67 KB against 134 KB is the whole
argument.

### The math box

A separate board of arithmetic hardware. The address the CPU writes is the
opcode — thirty-two of them, most just loading half a register, a handful
kicking off a multiply, a divide, a perspective step or a distance
approximation. The real thing is a bit-slice ALU running microcode out of six
PROMs. MAME simulates the arithmetic it performs rather than the microcode that
performs it, and so does this, which is why those PROMs are in the ROM set but
not in the build.

---

## 📁 Project Structure

```
core/           platform-independent emulation, shared with the host harness
  tempest.c       6502, memory map, the pot-wired inputs, the EAROM
  avg.c           the Analog Vector Generator, Tempest variant
  mathbox.c       the math box
  pokey.c         POKEY sound, and the pot inputs the game reads its buttons on
  m6502fast.h     an instruction-stepped NMOS 6502
main/           the ESP32 application
components/     display, IMU, audio and shared medal input for the Waveshare board
host/           builds the same core on a desktop; frames to PPM, audio to WAV
tools/          ROM converter
```

## 💻 Running It on Your Computer

```sh
cd host && make
./harness /tmp/out 22 --every 1 --wav /tmp/tempest.wav \
    --script "3:coin=1,3.2:coin=0,5:start=1,5.3:start=0,9:spin=2,11:spin=0,12:fire=1"
```

The harness writes the picture the right way up, 280×280, with the same
anti-aliased stroke the medal draws with.

## ⚙️ Configuration

| What | Where |
|---|---|
| DIP switches | `tp_set_dips()` in `main/main.cpp` |
| Twist direction | `SPIN_SIGN` in `main/input.cpp` |
| Twist sensitivity | `DEG_PER_COUNT` in `main/input.cpp` |

## 📌 Status and Known Gaps

Measured on hardware: **300 emulated frames per 5 seconds — 60.0 Hz — with all
300 drawn, none skipped and none dropped.**

- The eight hues are the eight the colour PROM can produce at full brightness.
  The real DAC has a dim-red bit as well, which is folded in rather than given
  its own shade.
- The EAROM keeps high scores for as long as the medal is powered. They are not
  written to flash, so they do not survive a power cycle.
- Cocktail flip is decoded from the control latch but not applied.

## 📄 Legal Notice

### ROM files

No ROMs here. Tempest is © 1981 Atari. This project ships a converter, not a
game.

### Third-party code

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The machine, the vector
generator and the math box are written from MAME (BSD-3-Clause).

### Disclaimer

Not affiliated with, endorsed by, or connected to Atari, its successors, or the
Fiesta San Antonio Commission.

## 🙏 Credits

The MAME team, and Eric Smith for the math box. The AVG's flipped address bit
and vector-RAM-relative program counter are documented nowhere else that is easy
to find.

## 📜 License

[0BSD](LICENSE).
