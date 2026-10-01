# WALKERRUN

**Atari's The Empire Strikes Back (1985) on the Waveshare ESP32-C6-LCD-1.69, played by tilting it.**

Empire ran on the Star Wars board with a bigger program and a slapstic
protection chip, so this is [TRENCHRUNNER](../TRENCHRUNNER/) with a core that
knows both games. Everything about the hardware, the vector display and the
sound board is described in TRENCHRUNNER's README. This one covers what is
different.

It is part of [PELLETINO](../../README.md) and is built and flashed from there.

> **No ROMs are included.** You need the MAME `esb` set.

## How well it runs

Measured on the device over serial, the game playing itself for 100 seconds,
sound off. Full speed is 41 frames a second.

| | Star Wars | Empire Strikes Back |
|---|---|---|
| Average | 39.3 (96%) | 38.9 (95%) |
| Worst five seconds | 29.2 | 27.4 |
| Five-second stretches under 95% | 5 of 19 | 6 of 19 |
| 6809 time, ms per second | 640 | 800 |
| Free heap while playing | 71 KB | about 25 KB (estimated; 17 KB measured with one more piece in RAM) |

Empire is about as fast as Star Wars with the sound off. It works the 6809 a
quarter harder, so there is less to spare: with the sound on, expect it to fall
further behind in busy scenes than Star Wars does.

## Controls

The same as every other game. Tilt is the flight yoke.

| Control | Does |
|---|---|
| **Tilt** | twist yaws, tip pitches |
| **MIDDLE button** | fire (also starts, in free play) |
| **TOP button**, tap | insert a coin |
| **MIDDLE button**, hold 5 seconds | back to the menu |
| **TOP button**, hold 1 second | power off |
| **Both buttons together** | sound: loud, quiet, off |

Star Wars flies itself when left alone. Empire does not: the autopilot knows
Star Wars's targets and trench, and nothing about this game.

## What is different from Star Wars

- **The program is 64 KB, not 48**, in two pages switched by the same latch that
  pages Star Wars's banked ROM.
- **The slapstic.** Reads and writes in 0x8000-0x9FFF go through
  `core/slapstic.c`, which watches the addresses and switches between four
  8 KB banks. It is 0.3% of what the CPU executes.
- **The sound program is 32 KB** and its idle loop is also a software timer, so
  it is skipped a pass at a time with the timer counted down to match.
- **Memory.** The program goes into RAM in 8 KB pieces. The two pieces the game
  spends least time in are placed in two small regions of RAM that nothing else
  uses; one more stays in flash, along with the slapstic's ROM. See the comment
  in `main/main.cpp`.
- **High scores** are kept the same way, in the emulated X2212. The menu shows
  no figure for them yet.

## Building it on its own

```sh
python3 tools/convert_roms.py /path/to/esb      # writes main/roms/starwars_roms.h
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 \
    idf.py -B build_docker -DIDF_TARGET=esp32c6 build
```

The same converter takes a `starwars` set, and the same firmware then builds
Star Wars. Which game it is comes from the header the converter writes.

## Running it on your computer

```sh
cd host && make
./harness ../esb /tmp/esb 90 --coin 20 --fire 23
```

`REGIONS=1` prints where the 6809 spends its time by 8 KB region, which is what
the memory layout was chosen from. `PCTOP=1` prints the hottest program
counters.

## License

The 6809 core is vecx's (GPL-3.0), so this image is GPL-3.0 as a whole. See
`LICENSE`, `LICENSES/` and `THIRD_PARTY_NOTICES.md`.
