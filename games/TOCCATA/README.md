# TOCCATA

**Konami's 1983 Gyruss, emulated on an ESP32-C6 Fiesta medal.** Four processors
and five sound chips, playing Bach at you while you fly to Neptune.

A San Antonio Fiesta medal is a collectible pin. This one plays Gyruss.

---

## 🎮 Quick Start Guide

### How to Play

Hold the medal upright and **twist it**. Gyruss's ship flies around the rim of a
circle, and the cabinet stick only ever moves it left or right around that rim —
so a twist of the medal is the same gesture, and the ring on the screen turns the
way your hand does.

| Control | What it does |
|---|---|
| **Twist left / right** | Around the ring |
| **Middle button** | Fire |
| **Both buttons together** | Sound off and on |
| **Power button, short press** | Insert a coin and start |
| **Power button, hold 1 second** | Power off |

There is a little hysteresis on the twist — eight degrees to start moving, five
to stop — so a hand that is nearly still does not chatter the ship back and
forth.

### Charging

USB-C. Holding the power button for a second cuts the battery rail.

### Troubleshooting

**The ship goes the wrong way.** `X_SIGN` in `main/input.cpp`.

**Too sensitive, or too stiff.** `TURN_DEG` and `RELEASE_DEG` in the same file.

---

## 🔨 Building Your Own

```sh
git clone https://github.com/aedile/TOCCATA.git
cd TOCCATA
python3 tools/convert_roms.py /path/to/gyruss
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 \
    idf.py -B build_docker build
cd build_docker && esptool --chip esp32c6 -p /dev/cu.usbmodemXXXX \
    -b 460800 write_flash @flash_args
```

Flashing has to run **from inside `build_docker`** and **from the host** —
Docker Desktop on macOS cannot reach USB.

### The ROMs

Not included. You need MAME's `gyruss` set. The converter checks every CRC.

---

## 🔬 Technical Details

### The original hardware

More silicon than any other medal in this set:

| | | |
|---|---|---|
| Z80 | 3.072 MHz | the game |
| KONAMI-1 | 1.536 MHz | a 6809 with scrambled opcodes, driving the sprites |
| Z80 | 3.579 MHz | the sound board, with **five** AY-3-8910s on its I/O ports |
| 8039 | 8 MHz | a second sound processor doing nothing but feeding a DAC |

The two main processors share 2 KB of RAM — the Z80 sees it at 0xA000 and the
6809 at 0x6000 — and that is the whole of their conversation.

### The KONAMI-1

Konami's cheapest possible protection, and it is genuinely cheap: a 6809 whose
**opcodes**, and only its opcodes, are XORed with one of four masks chosen by two
address bits. Operands and data come through untouched, which is why the 6809
core here had to have its opcode fetch split out from its ordinary read.

Every opcode the sub-CPU executes lives in its own ROM, so that ROM is decrypted
once at start-up and the fetch becomes an array read.

### The music will not play without a counter nobody documents

The sound board has no timer of its own. Its tempo comes off its own clock: a
divide-by-1024 feeding an LS90 wired as a bi-quinary decade counter, whose four
outputs the program reads **through the third AY's port A**.

Get that wrong — return 0xFF, say, which is what an unwired port reads — and the
sound Z80 sits in its interrupt handler waiting for a count that never changes.
It never re-enables interrupts, so every command after the first one is ignored,
and the board falls silent a couple of seconds in. Everything else about it looks
perfectly healthy.

### Making four processors fit

Two of them spend most of their time waiting, and finding that is what made this
run at all:

- The **sub-CPU's** main loop ends at `F0B6`, which is `BRA` to itself: it has
  finished the frame's sprite work and is waiting for vblank. Nearly two thirds
  of its instructions are that one.
- The **game Z80** ends up at `05F4` spinning on the top bit of a byte in its own
  work RAM, waiting for the vblank NMI to clear it. Nothing else can clear it —
  the sub-CPU's address space does not reach that RAM.

Both skips are exact. Build with `-DGY_NO_IDLE` and the frames come out
byte-for-byte identical; that is how they were checked, over 120 frames of
attract and gameplay.

### Five sound chips in a frame

The naive way to render an AY is to step it once per chip tick and read the
level — twelve times per output sample, per channel, per chip. Sixty of those
per sample across five chips does not fit.

Three things fixed it, in order of how much they bought:

1. **A chip whose three volume registers are all zero cannot make a sound.**
   Skip it entirely and just keep its sample clock moving. On a five-chip board
   most of them are idle most of the time.
2. **With no noise in the mix, a channel is a plain square wave**, and how many
   of the next dozen steps it spends high can be worked out a run at a time
   rather than a step at a time.
3. Channels at zero volume, and the noise and envelope generators when nothing
   is using them, are skipped.

The second one has an off-by-one waiting in it: the counter is incremented
before the comparison, so **the step that causes the toggle already carries the
new level**. The run at the old level is one step shorter than the distance to
it. The A/B against the stepping path caught that; with it fixed the two agree
on all 441,000 samples of a twenty-second capture.

---

## 📁 Project Structure

```
core/           platform-independent emulation, shared with the host harness
  gyruss.c        the four processors, their maps and the latches between them
  gyruss_video.c  tilemap, sprites, and the two colour lookup PROMs
  ay8910.c        the AY-3-8910, five instances of it
  mcs48.h         an Intel MCS-48 interpreter, for the 8039
  e6809.c         the 6809 from vecx (GPL-3.0 — see notices)
  z80/            Marat Fayzullin's Z80 (non-commercial — see notices)
main/           the ESP32 application
components/     display, IMU, audio and shared medal input for the Waveshare board
host/           builds the same core on a desktop; frames to PPM, audio to WAV
tools/          ROM converter
```

## 💻 Running It on Your Computer

```sh
cd host && make
./harness /tmp/out 25 --every 1 --wav /tmp/gyruss.wav \
    --script "3:coin=1,3.3:coin=0,5:start=1,5.3:start=0,12:fire=1,14:fire=0"
```

The harness writes the **native** 256×224 frame, which is the picture lying on
its side. Turn it 90° clockwise to look at it the right way up.

## ⚙️ Configuration

| What | Where |
|---|---|
| DIP switches | `gy_set_dips()` in `main/main.cpp` |
| Twist direction | `X_SIGN` in `main/input.cpp` |
| Twist thresholds | `TURN_DEG`, `RELEASE_DEG` in `main/input.cpp` |

## 📌 Status and Known Gaps

Measured on hardware over several five-second windows: **264 to 304 emulated
frames per five seconds — 53 to 61 Hz against a real board's 60.56.** It reaches
full speed in the lighter passages and sits around 53 Hz when all five sound
chips are busy at once. Four processors and five sound chips is the most this
board has been asked to do, and this is honestly where it lands rather than
where it was aimed.

- The discrete RC filter network the sound chips run through is not modelled;
  the five outputs are summed flat.
- Sprites are drawn a whole frame at a time rather than a scanline at a time.
- Cocktail flip is decoded and applied to the tilemap but has not been tested.

## 📄 Legal Notice

### ROM files

No ROMs here. Gyruss is © 1983 Konami. This project ships a converter, not a
game.

### Third-party code

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). **The 6809 core is
GPL-3.0, which makes a built binary GPL-3.0 as a whole**, and the Z80 core is
licensed for non-commercial use only.

### Disclaimer

Not affiliated with, endorsed by, or connected to Konami, its successors, or the
Fiesta San Antonio Commission.

## 🙏 Credits

The MAME team, whose `gyruss.cpp` documents the LS90 tempo counter — a comment
about a jellybean logic chip, without which there is no music.

## 📜 License

[0BSD](LICENSE) for the project's own code, but see the note above: the linked
6809 core is GPL-3.0 and the Z80 core is non-commercial.
