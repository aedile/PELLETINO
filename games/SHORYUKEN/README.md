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

**The 68000 holds real time, with room to spare.** Emulating the 10 MHz 68000 takes
296 ms of every second (30% of the core) in the first attract fight, against an
estimate of 50 to 60%. What gets it there is skipping the game's idle loop exactly
(without that it is 671 ms) and running the 147 instruction handlers the game uses
from RAM.

**With the sound off, it holds real time** in five of the eight video
configurations: SCALE drawing 1 frame in 2 (29.8 fps on the panel), 1 in 3 or 1 in 4,
and CROP drawing 1 in 3 or 1 in 4. Drawing every frame it runs at 67% of real time
(40 fps): at 60 fps the video alone would need about 115% of the core, against an
estimate of 50 to 65%.

**With the sound on, it does not quite hold real time.** The Z80 and the sound chips
take 300 to 550 ms a second depending on the rate. The best configuration is SCALE,
1 frame in 4, all the sound at 11 kHz, with `FRAME_SKIP_AUTO=1`: 59.0 of 59.6 frames a
second (99%), 14.7 drawn. The same at 22 kHz with `YM_QUALITY=0` does 59.1. At 22 kHz
with the full YM2151 it is 55.5 (93%).

Against the brief's budget, per second, in the fight:

| Stage | Estimate | Measured |
|---|---|---|
| 68000 at 10 MHz | 50 to 60% | 30% |
| Z80 at 3.58 MHz | about 20% | 13 to 15% |
| YM2151 | 15 to 35% at 44.1 kHz | 40% at 44.1 kHz, 24% at 22 kHz, 17% at 11 kHz (with the MSM6295 and the mix) |
| Video, four layers, 60 fps, SCALE | 50 to 65% | about 115% (video and strips) |
| Flash traffic for tiles | about 5 ms a frame | not measurable: reading every tile from RAM instead was slower |

For the smoothest picture: sound off, SCALE, `FRAME_SKIP=1`. For the game with its
sound: SCALE, `FRAME_SKIP=3`, `SOUND=FM_ADPCM`, `SOUND_RATE=11025`.

---

## Results

All 56 combinations of `VIDEO_MODE`, `FRAME_SKIP`, `SOUND` and `SOUND_RATE`, each 25
seconds of the first attract fight, measured on the board with `FRAME_SKIP_AUTO=0`.
The rows were measured before the main loop was allowed to catch up by twelve frames
rather than four, which moves only the full-sound rows, by about 1%.
Times are milliseconds per second. "Machine fps" is how many frames of the arcade board
were emulated; 59.6 is real time, and "speed" is that as a percentage. "Needs" is what
the work would take if the machine ran at full speed: under 1000 it holds real time.
The rate is left out where the sound is off.

| # | video | skip | sound | rate | drawn fps | machine fps | speed | 68000 | Z80 | sound out | video | strips | idle | needs | free heap |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | SCALE | 0 | OFF | - | 40.1 | 40.1 | 67% | 209 | 0 | 0 | 582 | 187 | 9 | 1474 | 77 KB |
| 1 | SCALE | 0 | FM | 11025 | 30.7 | 30.7 | 51% | 168 | 83 | 118 | 469 | 148 | 1 | 1941 | 44 KB |
| 2 | SCALE | 0 | FM | 22050 | 28.7 | 28.7 | 48% | 157 | 77 | 179 | 436 | 138 | 1 | 2076 | 43 KB |
| 3 | SCALE | 0 | FM | 44100 | 24.9 | 24.9 | 42% | 135 | 67 | 294 | 374 | 118 | 1 | 2393 | 37 KB |
| 4 | SCALE | 0 | FM_ADPCM | 11025 | 30.5 | 30.5 | 51% | 166 | 82 | 130 | 462 | 147 | 1 | 1954 | 42 KB |
| 5 | SCALE | 0 | FM_ADPCM | 22050 | 28.4 | 28.4 | 48% | 154 | 76 | 193 | 427 | 136 | 1 | 2098 | 41 KB |
| 6 | SCALE | 0 | FM_ADPCM | 44100 | 24.6 | 24.6 | 41% | 132 | 65 | 310 | 364 | 116 | 1 | 2422 | 35 KB |
| 7 | SCALE | 1 | OFF | - | 29.8 | 59.6 | 100% | 302 | 0 | 0 | 461 | 146 | 77 | 924 | 77 KB |
| 8 | SCALE | 1 | FM | 11025 | 23.1 | 46.2 | 77% | 242 | 123 | 143 | 362 | 114 | 2 | 1288 | 44 KB |
| 9 | SCALE | 1 | FM | 22050 | 21.4 | 42.8 | 72% | 225 | 114 | 205 | 336 | 106 | 1 | 1392 | 43 KB |
| 10 | SCALE | 1 | FM | 44100 | 18.1 | 36.2 | 61% | 194 | 96 | 318 | 287 | 90 | 1 | 1646 | 37 KB |
| 11 | SCALE | 1 | FM_ADPCM | 11025 | 22.9 | 45.7 | 77% | 238 | 121 | 156 | 355 | 113 | 2 | 1302 | 42 KB |
| 12 | SCALE | 1 | FM_ADPCM | 22050 | 21.2 | 42.3 | 71% | 220 | 111 | 222 | 328 | 104 | 1 | 1408 | 41 KB |
| 13 | SCALE | 1 | FM_ADPCM | 44100 | 17.8 | 35.5 | 60% | 189 | 93 | 337 | 279 | 88 | 1 | 1679 | 35 KB |
| 14 | SCALE | 2 | OFF | - | 19.9 | 59.6 | 100% | 299 | 0 | 0 | 324 | 102 | 262 | 739 | 77 KB |
| 15 | SCALE | 2 | FM | 11025 | 18.2 | 54.4 | 91% | 285 | 145 | 157 | 297 | 93 | 8 | 1087 | 44 KB |
| 16 | SCALE | 2 | FM | 22050 | 17.0 | 50.8 | 85% | 264 | 135 | 220 | 276 | 86 | 4 | 1169 | 43 KB |
| 17 | SCALE | 2 | FM | 44100 | 14.5 | 43.3 | 73% | 226 | 115 | 334 | 236 | 74 | 1 | 1376 | 37 KB |
| 18 | SCALE | 2 | FM_ADPCM | 11025 | 18.0 | 53.9 | 90% | 280 | 142 | 172 | 291 | 92 | 7 | 1098 | 42 KB |
| 19 | SCALE | 2 | FM_ADPCM | 22050 | 16.7 | 50.1 | 84% | 258 | 132 | 238 | 270 | 85 | 3 | 1187 | 41 KB |
| 20 | SCALE | 2 | FM_ADPCM | 44100 | 14.1 | 42.3 | 71% | 218 | 111 | 356 | 228 | 72 | 1 | 1408 | 35 KB |
| 21 | SCALE | 3 | OFF | - | 14.9 | 59.6 | 100% | 296 | 0 | 0 | 248 | 78 | 365 | 636 | 77 KB |
| 22 | SCALE | 3 | FM | 11025 | 14.7 | 58.6 | 98% | 307 | 156 | 160 | 246 | 77 | 40 | 977 | 44 KB |
| 23 | SCALE | 3 | FM | 22050 | 13.9 | 55.7 | 93% | 291 | 148 | 227 | 233 | 72 | 14 | 1056 | 43 KB |
| 24 | SCALE | 3 | FM | 44100 | 12.1 | 48.3 | 81% | 250 | 128 | 343 | 201 | 63 | 2 | 1232 | 37 KB |
| 25 | SCALE | 3 | FM_ADPCM | 11025 | 14.6 | 58.3 | 98% | 302 | 154 | 177 | 242 | 76 | 34 | 988 | 42 KB |
| 26 | SCALE | 3 | FM_ADPCM | 22050 | 13.8 | 55.1 | 92% | 285 | 145 | 245 | 228 | 72 | 10 | 1071 | 41 KB |
| 27 | SCALE | 3 | FM_ADPCM | 44100 | 11.8 | 47.2 | 79% | 241 | 124 | 364 | 194 | 61 | 2 | 1261 | 35 KB |
| 28 | CROP | 0 | OFF | - | 27.8 | 27.8 | 47% | 149 | 0 | 0 | 635 | 204 | 1 | 2144 | 77 KB |
| 29 | CROP | 0 | FM | 11025 | 23.2 | 23.2 | 39% | 126 | 62 | 106 | 528 | 166 | 1 | 2569 | 44 KB |
| 30 | CROP | 0 | FM | 22050 | 21.8 | 21.8 | 37% | 118 | 58 | 166 | 492 | 154 | 1 | 2734 | 43 KB |
| 31 | CROP | 0 | FM | 44100 | 19.0 | 19.0 | 32% | 103 | 51 | 276 | 426 | 133 | 1 | 3137 | 37 KB |
| 32 | CROP | 0 | FM_ADPCM | 11025 | 23.2 | 23.2 | 39% | 124 | 61 | 117 | 520 | 166 | 1 | 2569 | 42 KB |
| 33 | CROP | 0 | FM_ADPCM | 22050 | 21.8 | 21.8 | 37% | 116 | 57 | 178 | 483 | 153 | 1 | 2734 | 40 KB |
| 34 | CROP | 0 | FM_ADPCM | 44100 | 18.9 | 18.9 | 32% | 101 | 50 | 292 | 415 | 131 | 1 | 3153 | 34 KB |
| 35 | CROP | 1 | OFF | - | 23.6 | 47.3 | 79% | 236 | 0 | 0 | 564 | 180 | 7 | 1251 | 77 KB |
| 36 | CROP | 1 | FM | 11025 | 17.8 | 35.6 | 60% | 190 | 94 | 126 | 437 | 137 | 1 | 1674 | 44 KB |
| 37 | CROP | 1 | FM | 22050 | 16.5 | 33.0 | 55% | 177 | 88 | 185 | 408 | 128 | 1 | 1806 | 43 KB |
| 38 | CROP | 1 | FM | 44100 | 14.2 | 28.4 | 48% | 153 | 75 | 299 | 350 | 109 | 1 | 2098 | 37 KB |
| 39 | CROP | 1 | FM_ADPCM | 11025 | 17.8 | 35.5 | 60% | 188 | 93 | 139 | 430 | 137 | 1 | 1679 | 42 KB |
| 40 | CROP | 1 | FM_ADPCM | 22050 | 16.4 | 32.7 | 55% | 174 | 86 | 200 | 400 | 127 | 1 | 1822 | 40 KB |
| 41 | CROP | 1 | FM_ADPCM | 44100 | 14.0 | 27.9 | 47% | 149 | 73 | 318 | 339 | 107 | 1 | 2136 | 34 KB |
| 42 | CROP | 2 | OFF | - | 19.6 | 58.9 | 99% | 294 | 0 | 0 | 494 | 156 | 42 | 970 | 77 KB |
| 43 | CROP | 2 | FM | 11025 | 14.8 | 44.2 | 74% | 232 | 117 | 142 | 375 | 118 | 2 | 1347 | 44 KB |
| 44 | CROP | 2 | FM | 22050 | 13.8 | 41.2 | 69% | 214 | 110 | 204 | 348 | 109 | 2 | 1445 | 43 KB |
| 45 | CROP | 2 | FM | 44100 | 11.6 | 34.8 | 58% | 186 | 92 | 317 | 298 | 93 | 1 | 1713 | 37 KB |
| 46 | CROP | 2 | FM_ADPCM | 11025 | 14.7 | 44.0 | 74% | 228 | 116 | 156 | 367 | 117 | 2 | 1352 | 42 KB |
| 47 | CROP | 2 | FM_ADPCM | 22050 | 13.7 | 40.9 | 69% | 210 | 108 | 219 | 340 | 108 | 1 | 1456 | 40 KB |
| 48 | CROP | 2 | FM_ADPCM | 44100 | 11.4 | 34.2 | 57% | 181 | 90 | 335 | 288 | 91 | 1 | 1743 | 34 KB |
| 49 | CROP | 3 | OFF | - | 14.9 | 59.6 | 100% | 295 | 0 | 0 | 384 | 122 | 185 | 816 | 77 KB |
| 50 | CROP | 3 | FM | 11025 | 12.7 | 50.8 | 85% | 264 | 134 | 149 | 331 | 103 | 4 | 1169 | 44 KB |
| 51 | CROP | 3 | FM | 22050 | 11.8 | 47.3 | 79% | 244 | 125 | 212 | 307 | 96 | 2 | 1258 | 43 KB |
| 52 | CROP | 3 | FM | 44100 | 10.1 | 40.4 | 68% | 209 | 107 | 327 | 261 | 82 | 1 | 1474 | 37 KB |
| 53 | CROP | 3 | FM_ADPCM | 11025 | 12.6 | 50.5 | 85% | 259 | 132 | 164 | 324 | 103 | 4 | 1176 | 42 KB |
| 54 | CROP | 3 | FM_ADPCM | 22050 | 11.7 | 46.8 | 78% | 239 | 122 | 230 | 299 | 94 | 2 | 1272 | 40 KB |
| 55 | CROP | 3 | FM_ADPCM | 44100 | 9.9 | 39.6 | 66% | 203 | 104 | 346 | 252 | 80 | 1 | 1504 | 34 KB |

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
nothing from the last.

On the ESP32-C6's USB serial, opening the port resets the chip, and every so often
the board gets into a state where every reset over USB brings it up in download
mode ("waiting for download"), which only its reset button clears. `bench.sh`
exits with status 3 when that happens; `tools/sweep.py` says so and waits for the
button, then carries on with the row. `tools/sweep.py /dev/cu.usbmodem1101` runs all 56 rows
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

| Knob | Values (default first) | What it costs and buys, measured |
|---|---|---|
| `VIDEO_MODE` | `SCALE`, `CROP` | SCALE draws 240×140 of the 384×224 picture; CROP draws the middle 240×224 at 1:1. CROP costs 55% more video (384 against 248 ms/s at `FRAME_SKIP=3`) and 55% more strips. |
| `FRAME_SKIP` | 3, 0, 1, 2 | Draw one frame in N+1. The machine runs every frame regardless. See the table. |
| `FRAME_SKIP_AUTO` | 1, 0 | Drop the next drawn frame whenever the last one overran. Free when the machine keeps up. The results table is taken with it off, so that `FRAME_SKIP` means what it says. |
| `LAYERS` | 15 (bitmask) | Scroll1 (8×8, the text) 1, scroll2 (16×16, the stage) 2, scroll3 (32×32, the sky) 4, sprites 8. Alone at `FRAME_SKIP=3`: scroll1 49 ms/s of video, scroll3 40, sprites 39; none at all, 0.1. For profiling. |
| `ROWSCROLL` | 1, 0 | Scroll2 scrolled line by line: the floor of a fight. Off saves 15 ms/s of video and flattens the floor. |
| `SOUND` | `OFF`, `FM`, `FM_ADPCM` | OFF never runs the Z80. FM adds the Z80 (about 145 ms/s) and the YM2151; FM_ADPCM adds the MSM6295, about 10 ms/s more. |
| `SOUND_RATE` | 22050, 11025, 44100 | The YM2151's operators are computed at this rate, so their cost follows it; its envelopes and timers run at the chip's own rate whatever this is. |
| `YM_QUALITY` | 1, 0 | 0: no LFO, no noise, operators at half the rate with a straight line between. Sound output 245 → 169 ms/s at 22 kHz. It sounds it: the vibrato goes. |
| `CPU_CORE` | `MUSASHI`, `OWN` | `OWN` was to be a 68000 of this project's own if Musashi could not hold 10 MHz. Musashi holds it with room to spare (296 ms/s), so it has not been written, and asking for it stops the build with the reason. |
| `TILE_CACHE_KB` | 0 | Not built, because its best case was measured first: drawing every tile out of RAM instead of flash (`EXPERIMENT=EXPERIMENT_RAM_TILES`, which draws the wrong picture) made the video slower, 248 → 297 ms/s. The flash cache already holds what a frame reads. Only 0 is accepted. |
| `STATS` | 1, 0 | The stats line; printing it costs about 3 ms/s. |
| `BENCH_SECONDS` | 0, N | 0 runs for ever. N: go to the first fight, measure N seconds, print the `bench` line, leave for the launcher. `BENCH_FROM_FRAME` (3400) is where the measuring starts. |
| `IDLE_SKIP` | 1, 0 | The game's scheduler and the sound program both spin waiting for an interrupt. With this on, once a pass of the loop is proved to change nothing, the rest of the time slice is taken off the clock in whole passes, so the interrupt still arrives at the instruction and the cycle it would have. 68000: 671 → 296 ms/s; with the sound on, the machine goes from 62% of real time to 92%. The host harness gets identical RAM, pictures and sound either way. |
| `HOT_HANDLERS` | 1, 0 | The 147 of Musashi's 1700 instruction handlers that attract mode uses 99% of the time, in RAM instead of flash. 68000: 375 → 296 ms/s, for 19 KB. The list is `components/emu/linker.lf`, made by `tools/hot.py` from a host profile. |
| `OPCODE_TABLE_RAM` | 0, 1 | The 21 KB table from opcode to handler in RAM. 346 → 282 ms/s without `HOT_HANDLERS`, but only 296 → 282 with it, and the sound needs the RAM. Off. |
| `PROG_CACHE_KB` | 0, 4 to 64 | Copies of the program pages the 68000 is busiest in, chosen as it runs. Worthless: 375 → 379 ms/s at 32 KB. Off. |
| `OCCLUSION` | 0, 1 | Skip painting pixels an opaque tile above will cover (the converter marks such tiles). On the host it removes 20% of the painting in a fight; on the board, working out what is covered costs more than it saves (258 → 331 ms/s). Off. |
| `STRIP_REUSE` | 1, 0 | A 16-row strip whose inputs have not changed since it was sent is neither drawn nor sent. About 3 ms/s in a fight; on the title screen most strips are spared. |

---

## What was tried, and what it bought

Everything here was measured on the board, in the first attract fight, SCALE
video, sound off unless it says otherwise.

| Change | What moved | Kept |
|---|---|---|
| Musashi's opcode tables worked out at build time and packed | 576 KB of RAM at start-up to 30 KB, most of it in flash | yes |
| The idle loops skipped by proof (`IDLE_SKIP`) | 68000 671 → 296 ms/s; with sound, 62% → 92% of real time | yes |
| The 147 hot instruction handlers in RAM (`HOT_HANDLERS`) | 68000 375 → 296 ms/s | yes |
| The accelerometer not read | input 84 → 2 ms/s | yes |
| The YM2151's envelopes visited only when due; silent channels skipped exactly | 1,748 → 1,522 CPU cycles a sample | yes |
| Unrolled pixel loops for the two views | video about 3% | yes |
| Strips not redrawn when nothing in them changed (`STRIP_REUSE`) | about 3 ms/s in a fight | yes |
| Opcode table in RAM (`OPCODE_TABLE_RAM`) | 14 ms/s on top of the hot handlers, for 21 KB | no, off |
| Program pages in RAM (`PROG_CACHE_KB`) | nothing | no, off |
| Occlusion (`OCCLUSION`) | slower | no, off |
| Tiles from RAM, the ceiling of any tile cache | slower | not built |
| The Z80 core in RAM | would take 65 KB, and then the machine's graphics RAM does not fit (short by 4 KB) | no |

---

## Memory

The machine's own 64 KB of work RAM and 192 KB of graphics RAM are ordinary
heap, allocated first, graphics RAM before work RAM because it wants the larger
block. The boot log lists every allocation and the heap after each step:

```
heap at start:          free 370072
heap after the panel:   free 353112
graphics RAM            196608 bytes
work RAM                 65536 bytes
heap with the machine:  free  90880
heap ready, sound off:  free  81524
heap ready, sound on:   free  45904   (largest block 20480)
```

Musashi's handlers that run from RAM take 19 KB, and the sound (the YM2151, the
MSM6295, the Z80's RAM, the I2S buffers) about 35 KB. The lowest free heap over
a whole bench is the `minheap` field and the last column of the table: never
below 37 KB.

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

Tested on the board with a launcher built that way: the launcher boots into
SHORYUKEN when it is the selection, a bench returns to the menu when it ends
(`medalboot: returning to the menu`, then the launcher's `software restart - a game
asked for the menu`), it can be picked from the wheel with the usual two-second hold,
both buttons together step the sound as in every other game, and holding BOOT for
five seconds goes back to the menu. The brief said ten seconds; every game here now
uses five, and so does this.

SHORYUKEN's slot is 768 KB: the largest build (CROP, all the sound) is 686 KB.

---

## The watchdog is off

`CONFIG_ESP_TASK_WDT_EN=n`, as in every game here. The main loop only sleeps
when the emulation is ahead of the clock, and with the sound on it is rarely ahead,
so the idle task can go long stretches without running and the task watchdog would
reset a program that is working as designed.

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
