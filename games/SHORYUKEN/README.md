# SHORYUKEN

**Street Fighter II: The World Warrior, the real ROM, emulated on the Waveshare
ESP32-C6-LCD-1.69.**

Not a game to play. A bench for finding out what a Capcom CPS-1 costs on one
160 MHz RISC-V core with 512 KB of RAM and no PSRAM. It boots the MAME `sf2` set
to its attract mode, prints where every second went over serial, and puts every
expensive part behind a compile-time switch, so that the trade-offs can be
measured rather than guessed.

The other Street Fighter II in this repository, `games/HADOUKEN`, plays a video
of the attract mode. This one runs the game's own code: a 68000 at 10 MHz, a Z80
at 3.58 MHz, a YM2151, an OKI MSM6295, three tile layers and 256 sprites.

No ROMs are included. See `LICENSE` and `THIRD_PARTY_NOTICES.md`.

---

## The answer

@@SUMMARY@@

---

## Results

@@TABLE@@

---

## Reproducing a row

Once, to put the ROMs on the board (the `sf2` zip from MAME, or its folder):

```sh
tools/convert_roms.py ~/roms/sf2.zip               # writes roms.bin, 7.4 MB
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 \
    idf.py -B build_docker -DIDF_TARGET=esp32c6 build
esptool.py --chip esp32c6 -p /dev/cu.usbmodem1101 -b 460800 write_flash \
    --flash_mode dio --flash_size 16MB --flash_freq 80m \
    0x0 build_docker/bootloader/bootloader.bin \
    0x8000 build_docker/partition_table/partition-table.bin \
    0x10000 build_docker/shoryuken.bin 0x110000 roms.bin
```

Then each row is one command, which builds in Docker, flashes the application,
resets the board and prints its output until the bench ends:

```sh
tools/bench.sh /dev/cu.usbmodem1101 VIDEO_MODE=SCALE FRAME_SKIP=3 SOUND=FM_ADPCM SOUND_RATE=22050 FRAME_SKIP_AUTO=0
```

Every knob that is not named goes back to its default, so one run inherits
nothing from the last. `tools/sweep.py /dev/cu.usbmodem1101` runs all 56 rows
into `results.csv` (about ninety minutes), and `tools/sweep.py --markdown
results.csv` prints the table above. Each row's command is in the last column
of `results.csv`.

A bench does not wait out the attract mode: the machine runs flat out, undrawn
and unheard, to frame 3400 (the first fight, 57 seconds in, reached in about 23
seconds), and the measuring starts there. It lasts 25 seconds, prints a `stats`
line every second and one `bench` line at the end with the same fields averaged
over the whole 25, then leaves for the launcher if the launcher started it.

---

## The stats line

```
stats fps=14.9 m68k=295.7ms z80=0.0ms ym=0.0ms video=247.5ms strips=78.4ms idle=364.6ms skipped=45/60 heap=79344 minheap=79176 vmode=SCALE skip=3 sound=OFF emu=59.6 input=1.9ms other=11.2ms rate=22050 core=MUSASHI under=0 frame=4238 strips=10/135 pc=002C2E
```

| field | what |
|---|---|
| `fps` | frames drawn on the panel in the last second |
| `m68k` | time in the 68000, and in the machine around it (latches, interrupts), not counting the Z80 |
| `z80` | time in the sound CPU; it is run when the 68000 writes a sound command, and at the end of each frame |
| `ym` | time making the sound: the YM2151, the MSM6295, the mix and handing it to I2S |
| `video` | drawing strips into RAM |
| `strips` | sending them to the panel: SPI and waiting for the DMA |
| `idle` | asleep, because the emulation is ahead of the clock |
| `skipped` | frames emulated but not drawn, of frames emulated |
| `heap`, `minheap` | free heap now, and the least it has been since boot |
| `emu` | frames the machine ran in the last second; 59.6 is real time |
| `input` | reading the buttons |
| `other` | what is left of the second after all the above: the loop itself, and interrupts |
| `under` | times the I2S queue ran dry since the bench began |
| `strips=a/b` | strips not drawn because nothing in them changed, of strips in drawn frames |

`m68k + z80 + ym + video + strips + idle + input + other` is the length of the
interval, which is a second give or take a frame; `other` is worked out as the
difference, so a large `other` is itself the warning that something is
unaccounted for. Each figure is `esp_timer_get_time()` either side of the stage.

`needs` in the table is `m68k + z80 + ym + video + strips + input + other`
scaled to the machine running at its full 59.64 frames a second. Under 1000 ms
the configuration holds real time; over it, it cannot.

---

## Knobs

All in `main/knobs.h`, each with its cost written next to it. On the command
line they are `-DKNOB=value` to `idf.py` (or `KNOB=value` to `tools/bench.sh`),
which `knobs.cmake` turns into a header in the build directory. The host harness
takes them as `make -C host KNOBS="-DFRAME_SKIP=1 -DKNOB_SOUND=FM"`: the knobs
that take a word are `KNOB_<name>` there.

@@KNOBS@@

---

## What was tried, and what it bought

Everything here was measured on the board, in the first attract fight, SCALE
video, sound off unless it says otherwise.

@@TRIED@@

---

## Memory

The machine's own 64 KB of work RAM and 192 KB of graphics RAM are ordinary
heap, allocated first, graphics RAM before work RAM because it wants the larger
block. The boot log lists every allocation and the heap after each step:

@@MEMORY@@

The program ROM, the graphics and the Z80's program are read in place from the
data partition, memory-mapped. The ESP32-C6 can map 8 MB of flash at once,
including the program itself, so the image is laid out with everything mapped
first (7.1 MB) and the MSM6295's samples and a 4 KB header after it; those are
read with `esp_partition_read()` as they are needed.

The 68000's opcode tables, which Musashi builds in RAM at start-up (576 KB),
are worked out at build time and packed into 30 KB (`tools/gen_m68k.py`,
`core/m68k/CHANGES.md`).

---

## Checking that nothing changed

The host harness runs the same core on a computer. It cannot say what anything
costs on the board (the flash cache and the RAM are the board's problems), but
it can say whether the machine is still the same machine:

```sh
make -C host
host/check.sh roms.bin /tmp/sf2ref make      # once, from a build you trust
host/check.sh roms.bin /tmp/sf2ref           # after every change
```

It runs 300 seconds of attract mode with the sound off and with it on and
compares a hash of every frame's picture, of graphics RAM after every frame, of
every frame's sound, and the whole sound, against the reference. The reference is
built with both idle skips off and every FM operator computed, so a pass also
says that skipping changes nothing. Every change listed above passed it.

`host/harness roms.bin out 60 --view native --every 5` writes a picture every five
seconds; `host/ppm2png.py out/*.ppm` makes them PNGs.

---

## Controls

The same as every other game here, from `components/medal_input`:

| | |
|---|---|
| **PWR** short press | coin, then start half a second later |
| **PWR** hold 1 s | power off |
| **BOOT** | jab punch |
| **BOOT** hold 5 s | back to the menu |
| **both together** | sound: loud, quiet, off |

The accelerometer is not read. It cost 84 ms of every second.

---

## Under PELLETINO

`games.toml` has SHORYUKEN as a `[[game]]` with `enabled = false`, because only
one game in a build may have a data partition and HADOUKEN's video has it. To
build a medal with this instead:

1. In `games.toml`, give HADOUKEN `enabled = false` and SHORYUKEN
   `enabled = true`.
2. Put `roms.bin` in `games/SHORYUKEN` (`tools/convert_roms.py`) and build
   `games/SHORYUKEN` with the knobs you want.
3. `./pelletino build && ./pelletino flash`.

The data partition is 7616 KB, 2 MB more than the video's, so a build with it
has room for fewer games. `./pelletino` says how many.

@@LAUNCHER@@

---

## The watchdog is off

`CONFIG_ESP_TASK_WDT_EN=n`, as in every game here. The main loop only sleeps
when the emulation is ahead of the clock, and with the sound on it never is, so
the idle task never runs and the task watchdog would reset a program that is
working as designed.

---

## Files

```
main/            main.cpp (the loop, the stats line, the bench), render.cpp (strips),
                 input.cpp, knobs.h
core/            cps1.c (the machine), cps1_video.c, cps1_sound.c, cps1_idle.c,
                 ym2151.c, okim6295.c, cps1_mem.h (the 68000's address space)
core/m68k/       Musashi, and what was changed in it (CHANGES.md)
core/z80/        Marat Fayzullin's Z80, unmodified
components/      copied from GRIDDLE; audio_hal takes its rate from SOUND_RATE,
                 emu is the core, with linker.lf saying what runs from RAM
host/            harness.c, Makefile, check.sh, ppm2png.py
tools/           convert_roms.py, bench.sh, sweep.py, capture.py,
                 gen_m68k.py (Musashi's tables), hot.py (which handlers go in RAM)
knobs.cmake      idf.py -D knobs into a header
partitions.csv   standalone: the application, and a 7616 KB data partition
```
