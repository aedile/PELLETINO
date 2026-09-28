# PELLETINO

**Twenty-six arcade games on a wearable Fiesta medal. Tilt to browse, hold the button to pick one, and it boots straight into that game forever after. One $20 ESP32-C6 board, 16 MB of flash, no SD card, no PSRAM.**

PELLETINO is the menu. Every game is a separate firmware image in its own flash
slot, and the launcher chain-boots them — so a game gets the whole chip to itself
while it runs. The partition table, the artwork and the menu are all generated
from the ROMs you supply. You never hand-edit a partition table, a header, or a
makefile.

> **Licensing in one line:** the code written here is 0BSD, but the assembled
> bundle is **free to share, not to sell** — most games embed a non-commercial
> Z80 core, and four are GPL-3.0. GitHub's sidebar says "0BSD"; that covers our
> code only. Read [LICENSING.md](LICENSING.md) before you distribute.
>
> **No ROMs, no marquee art and no music are included.** Those belong to their
> owners; supplying them is your part.

| | |
|---|---|
| Games | **26** playable, 27 approved in `games.toml`, **16** flash slots per build |
| Launcher firmware | 303 KB in a 512 KB slot |
| Marquee artwork | 481 KB blob, 17 marquees, its own partition |
| Free heap with the menu up | 237 KB of 512 KB |
| Cold boot to the splash | ~1.2 s |
| Splash | ~12 s, skippable with any button |
| Hardware | Waveshare ESP32-C6-LCD-1.69 — 240×280 ST7789V2, ES8311 codec, QMI8658 IMU, two buttons, LiPo |

Numbers come from the device over serial, not from a spec sheet.

Think of it as a tilt-controlled [Galagino](https://github.com/harbaum/galagino)
on a smaller board: no cabinet, no joystick, no SD card — a medal you pin to a
shirt, and the accelerometer is the controller.

**PELLETINO 1.0 was Pac-Man alone.** This is the same medal grown into a
platform; that first release is preserved at tag
[`v1.0`](https://github.com/aedile/PELLETINO/releases/tag/v1.0), and it now
ships as one of the seventeen games under the codename `GHOSTERAMA`.

Same board as [DIABLITO](https://github.com/aedile/DIABLITO) (shareware Doom)
and [FIESTA-ENTERTAINMENT-SYSTEM](https://github.com/aedile/FIESTA-ENTERTAINMENT-SYSTEM)
(an NES).

---

## Contents

- [Quick start](#quick-start)
- [Getting it running](#getting-it-running)
- [Choosing a build — `pelletino pick`](#choosing-a-build--pelletino-pick)
- [Controls](#controls)
- [The splash](#the-splash)
- [Adding a video "game"](#adding-a-video-game)
- [Marquee art](#marquee-art)
- [Two games, one slot](#two-games-one-slot)
- [How a build is laid out](#how-a-build-is-laid-out)
- [Status and known gaps](#status-and-known-gaps)
- [Repository layout](#repository-layout)
- [Building without Docker](#building-without-docker)
- [Credits and license](#credits-and-license)

---

## Quick start

```sh
./pelletino games      # the approved list, and which ROMs you already have
# put ROM zips in roms/ using their MAME names
./pelletino pick       # (only if more games are present than fit) choose a build
./pelletino build      # artwork + partition table + launcher firmware
./pelletino flash      # write it all to a connected medal
```

`./pelletino` on its own reports what the current build contains and what it is
missing, and changes nothing — it is always safe to run. `./pelletino help`
lists every command.

A game appears in the menu only if its ROM is in `roms/`. Games you don't have a
ROM for are simply absent — no slot, no menu entry, no wasted flash.

---

## Getting it running

### 1. Prerequisites

- **Docker**, running. The toolchain lives in `espressif/idf:v5.3.4`; nothing
  else is installed on your machine.
- **`esptool.py`** on the host (`brew install esptool`, or `pip install esptool`).
  Flashing cannot run inside Docker — Docker Desktop on macOS can't reach USB.
- **Python 3.11+** for the build tooling. It creates its own `.venv` on first
  build; you don't need to prepare anything.

### 2. Clone

```sh
git clone https://github.com/aedile/PELLETINO.git
cd PELLETINO
```

Every game's source is vendored in, so one clone builds everything.

### 3. Supply ROMs

```sh
./pelletino games          # prints the approved list, * marks ROMs you have
cp ~/wherever/galaga.zip roms/
```

Use MAME names. We ship no ROMs and finding them is your responsibility.

### 4. Build

```sh
./pelletino build
```

Packs the artwork, generates `partitions.csv` and the build manifest, then
compiles the launcher in Docker. First run pulls the IDF image, which is large
and slow exactly once.

### 5. Flash

```sh
./pelletino flash          # add a port if it guesses wrong: ./pelletino flash /dev/cu.usbmodem101
```

Writes the bootloader, the partition table, the launcher, the artwork blob, and
each game that has been built, to its own slot. Games you haven't built leave
their slot empty and show as `NOT INSTALLED` in the menu — flash them later
without rebuilding anything else.

### 6. First boot

The medal plays the splash, then lands in the carousel. Tilt to browse; hold
**BOOT** for two seconds on a game to pick it. From then on the medal boots
straight into that game. To come back, hold **BOOT** for ten seconds in the
game, or hold it while powering on.

### Troubleshooting

| Symptom | Cause |
|---|---|
| `docker is installed but not running` | Start Docker Desktop. |
| `nothing to build` | `roms/` has no approved ROM zip. Run `./pelletino games`. |
| `N games enabled but ESP-IDF allows at most 16` | Run `./pelletino pick`. |
| Menu says `NO ARTWORK` | The `mqart` partition was never written. Re-run `./pelletino flash`. |
| Every game says `NOT INSTALLED` | Expected before any game firmware is built. The launcher works; the slots are empty. |
| Tilt browses the wrong way | Flip the sign in `read_roll()` in `main/input.cpp`. |
| Splash is silent | No `music/splash.mid`. See [The splash](#the-splash). |

---

## Choosing a build — `pelletino pick`

The medal has room for **16 game slots** inside **16 MB** of flash, shared with
the launcher and the artwork. More games are approved than fit, and you may hold
more ROMs than fit. When a build overflows, `./pelletino pick` walks the list of
everything you have and prices each choice live — slots used, flash used, space
free — so you can land a build that fits before you flash it:

```
   1. [x] Ms. Pac-Man          1024 KB   mspacman
   2. [x] Pole Position        1024 KB   polepos
   3. [ ] Star Wars             768 KB   starwars
   ...
  slots 11/16   flash 12.75/16 MB   3.25 MB free
```

Type a number to toggle a game, `a` to auto-pick everything that fits, `s` to
save, `q` to quit. It writes `selection.txt`, which the build honours. Delete
that file (or `./pelletino pick --clear`) to go back to "every ROM present is
included." Picking is entirely optional — reach for it only when a build overflows.

---

## Controls

Every game is played the same way physically: **hold the medal upright and twist
or tip it** — the tilt sensor is the joystick, spinner, wheel, or yoke. Two
buttons do the rest.

| Button | Short press | Hold |
|---|---|---|
| **BOOT** (top) | the game's action — fire / jump / hop / pump | 3 s: sound off/on · 10 s: back to the menu |
| **PWR** (side) | insert a coin (then auto-start ½ s later) | 1 s: power the medal off |

> A **coin** is always a coin and a **start** is always a start, on every game.
> Sound-off (mute) and back-to-menu are the same gesture everywhere. Picking a
> game from the menu is a button *hold*; a knock won't do it.

> **The tilt centre is wherever you are holding the medal when you press coin**
> (and again at start). So hold it the way you mean to play before you press.
> If one direction stops registering mid-game, press coin again in your
> playing posture and it re-centres. A medal lying flat on a table is not
> "held" and its tilt is ignored until it is picked up.

Per-game, the tilt and the BOOT action are:

| Game | ROM | Tilt does | BOOT does |
|---|---|---|---|
| Pac-Man | `pacman` | steer (4-way maze) | — |
| Ms. Pac-Man | `mspacman` | steer (4-way maze) | — |
| Galaga | `galaga` | move the fighter L/R | fire |
| Galaxian | `galaxian` | move the fighter L/R | fire |
| Space Invaders | `invaders` | move the cannon L/R | fire |
| Lunar Lander | `llander` | twist = rotate, tip away = throttle (analogue) | abort |
| Dig Dug | `digdug` | move L/R (dominant axis) | pump |
| Mr. Do! | `mrdo` | dig in 4 directions (dominant axis) | throw the power ball |
| Burger Time | `btime` | walk in 4 directions (dominant axis) | pepper |
| Root Beer Tapper | `rbtapper` | twist along the bar, tip between bars | pour (hold to fill) |
| Joust | `joust` | twist to run left and right | flap |
| Moon Patrol | `mpatrolw` | twist to slow down and speed up | jump (the guns fire themselves) |
| Donkey Kong | `dkong` | run / climb (dominant axis) | jump |
| Mario Bros. | `mario` | run L/R | jump |
| Frogger | `frogger` | hop L/R (dominant axis) | hop forward |
| Rally-X | `rallyx` | drive L/R | lay a smoke screen |
| Centipede | `centiped` | trackball L/R (angle = speed) | fire |
| Missile Command | `missile` | trackball L/R (angle = speed) | fire (cycles the 3 bases) |
| Asteroids | `asteroid` | twist = rotate, tip away = thrust | fire (hold 0.7 s: hyperspace) |
| Tempest | `tempest` | claw around the rim | fire (short 2nd press: superzapper) |
| Gyruss | `gyruss` | move around the ring | fire |
| Time Pilot | `timeplt` | 8-way: twist and tip to point the plane | fire |
| Arkanoid | `arkanoidu` | paddle, absolute (±32° sweep) | fire (once the laser is fitted) |
| Star Wars | `starwars` | flight yoke — twist yaws, tip pitches | fire (also starts, in free play) |
| Pole Position | `polepos` | steer like a wheel | — (throttle is automatic; BOOT taps shift gear) |
| Street Fighter II | `sf2` | — (attract-mode video, see below) | — |

Pole Position starts on a coin (free-play) and holds the accelerator down for
you, so the whole game is one wheel plus the gear tap — every other gesture then
matches the rest of the medal.

---

## The splash

Powering on runs a ~12 second opening before the carousel: the title races past,
three hard cuts (fireworks over the Tower of the Americas, a piñata, an arcade
cabinet mid-game), then a Fiesta scene with papel picado, confetti and a dancing
stage. Any button press skips it. It only plays on the way to the menu — a medal
with a game selected boots straight into the game instead.

Every pixel of it is drawn from primitives in `components/fest`: there is no
artwork, no sprite sheet and no bitmap font in the repository.

**Music is yours to supply.** Drop a Standard MIDI File at `music/splash.mid` and
it is embedded on the next build; with no file the splash runs silent and the
build still works. See [`music/README.md`](music/README.md).

```sh
cp ~/Downloads/something.mid music/splash.mid
./pelletino build && ./pelletino flash
```

Playback is three square-wave channels on an emulated AY-3-8910 — the same chip
four of the games already emulate — so at most three notes sound at once and a
fourth steals the quietest voice. Something written for a chip, or a simple
lead-plus-accompaniment arrangement, suits it far better than dense piano or
orchestral music. Percussion (MIDI channel 10) is dropped.

`host/midi/test_midi.c` runs the player on your machine against any MIDI and
fails if a voice holds one pitch too long — the shape a stuck note takes.

---

## Adding a video "game"

A game slot can hold a looping video clip instead of an emulator — Street Fighter
II ships as its attract-mode reel. The clip lives in a data partition of its own.

- Encode and pack a clip with `games/HADOUKEN/tools/pack_media.py` — it letterboxes
  to the portrait panel and writes `media.bin`. See that script for the size limit
  and encoding settings; a longer clip needs a bigger `data_kb` in `games.toml`.
- The video slot is switched on by `games/HADOUKEN/media.bin` existing, the same
  way a ROM zip switches on an emulated game.

Only one game per build may carry a data partition (it is labelled `media`, which
is the label the player looks for).

---

## Marquee art

The carousel shows a marquee for each game. **Game logos are copyrighted, so we
ship none.** You have two options:

- **Fetch:** `./pelletino art` pulls marquees from a third-party archive into
  `marquees/`. See `tools/fetch_marquees.py` for the source and the `PELLETINO_ART_BASE`
  override.
- **Supply your own:** drop a PNG named `marquees/<rom>.png` (any resolution; it's
  fitted to a 208×104 box).

Any game without a marquee gets a plain generated text banner, so the build never
blocks on missing art.

---

## Two games, one slot

Pac-Man and Ms. Pac-Man are carried in **one image** that picks which to run at
boot, so the pair costs one slot instead of two. In `games.toml`, Ms. Pac-Man owns
the slot and Pac-Man rides it (`boots = "mspacman"`); both still appear as separate
menu entries, and selecting either records which one the shared image should run.
The same mechanism (`boots = "<owner>"`) works for any two games that share an image.

---

## How a build is laid out

```
nvs / otadata / phy_init         housekeeping
launcher            0x20000      the PELLETINO menu (factory app)
mqart                            marquee artwork blob (lcd/marquees.bin)
ota_0..ota_N                     one app slot per game, labelled with its ROM
media               (optional)   a video clip's data partition
```

`tools/configure.py` generates `partitions.csv` and `build/manifest.json` from
`games.toml` plus whatever is in `roms/`, so the firmware and the flasher can
never disagree about where a game lives. The launcher finds a game by its
partition label, so adding a game never recompiles the launcher.

Turning a game off **frees its slot** — the partition table is generated, not
maintained, so an eight-game build gets eight slots and the rest of the flash
stays empty.

[`ARCHITECTURE.md`](ARCHITECTURE.md) covers the boot handshake every game owes
the menu, the memory and byte-order rules, and why chain-booting beat one
monolithic image.

---

## Status and known gaps

- **ESP-IDF allows at most 16 OTA slots** (`ota_0`–`ota_15`), which is the hard
  ceiling on games per build, not flash. `pelletino pick` exists because of it.
  Collapsing a pair that shares hardware onto one image frees a slot.
- **Games must be built individually** to fill their slots; the launcher alone
  gives you a browsable menu where everything reads `NOT INSTALLED`.
- **The battery gauge is uncalibrated.** The ×3 divider and the 3.3–4.2 V linear
  map are at the top of `main/battery.c`; a real lithium curve is not a straight
  line, so the middle of the range reads optimistically.
- **A game that crashes before its first act** (pointing the boot partition back
  at the launcher) can boot-loop, since control never reaches the menu to rescue
  it. `ARCHITECTURE.md` covers the handshake.
- **Empire Strikes Back (`esb`) is approved but not playable.** It runs on the
  same vector core as Star Wars but the scene is heavier and it does not hold
  frame rate; it has no row in the controls table for that reason.
- **No hero video or photos yet** in this repository.

---

## Repository layout

- `games/` — every game's source, vendored in so one clone builds everything. Each
  folder is also mirrored at its own `aedile/*` GitHub repo; `VENDORED.md` records
  the upstream and commit for each.
- `components/` — code shared by the launcher: `display` (ST7789), `imu`
  (QMI8658), `gfx` (menu drawing), `fest` (the splash's framebuffer and
  decorations), `mqart` (artwork), `chiptune` (MIDI on an AY-3-8910), `audio_hal`
  (ES8311 over I2S), `medalboot` (which game boots, and the way back out).
  Games carry their own shared components, including `medal_input`.
- `main/` — the launcher: menu, splash, input, battery, chain-boot.
- `tools/` — the build tooling behind `./pelletino`.
- `host/` — harnesses that run on your machine rather than the medal.
- `games.toml` — the one place that decides what a build *can* contain.

---

## Building without Docker

`./pelletino build` uses the `espressif/idf:v5.3.4` Docker image so you need
nothing installed but Docker. If you have ESP-IDF v5.3.4 natively, you can run the
underlying steps yourself; see the commands `./pelletino` prints.

Docker Desktop on macOS can't reach USB, so **flashing always runs on the host** —
that is what `tools/flash_all.sh` (behind `./pelletino flash`) does.

---

## Credits and license

The emulator cores under `games/*/core/` were written for this project with MAME's
drivers used as hardware documentation — memory maps, interrupt timing, palette
decoding and measured sound levels. `LICENSING.md` records the line-by-line
comparison against the MAME source that backs that claim.

Third-party code, each keeping its own terms: **Marat Fayzullin's Z80** (the
non-commercial core most games are built on), the **vecx MC6809** (GPL-3.0, the
four 6809 titles), **MAME** (BSD-3-Clause, as reference), **TinyMidiLoader** by
Bernhard Schelling (zlib, splash MIDI parsing), **font8x8** by Daniel Hepper
(public domain), and **ESP-IDF** by Espressif.

PELLETINO's own code is Zero-Clause BSD (`LICENSE`) — free for everyone, no
conditions. The assembled bundle is **not for sale**; see
[LICENSING.md](LICENSING.md) for the full picture, and each game's own `LICENSE`
and `THIRD_PARTY_NOTICES.md` for the authoritative per-game terms.

No game ROMs, marquee art or music are distributed here.
