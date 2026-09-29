# PELLETINO

**An arcade in your pocket: up to seventeen games at a time, chosen from twenty-six. Spin the wheel, hold the button to pick one, and it boots straight into that game forever after. One $20 ESP32-C6 board, 16 MB of flash, no SD card, no PSRAM.**

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
> **No ROMs, no artwork and no music are included.** Those belong to their
> owners; supplying them is your part.

| | |
|---|---|
| Games | **26** playable, 27 approved in `games.toml`, **16** flash slots per build |
| Launcher firmware | 313 KB in a 512 KB slot |
| Artwork | 477 KB for 17 games — three logos and a screenshot each — in its own partition |
| Cold boot to the splash | ~1.2 s |
| Attract loop | title, instructions, the games, credits — about two minutes, until a button is pressed |
| Hardware | Waveshare ESP32-C6-LCD-1.69 — 240×280 ST7789V2, ES8311 codec, QMI8658 IMU, two buttons, LiPo |

Numbers come from the device over serial, not from a spec sheet.

Think of it as a tilt-controlled [Galagino](https://github.com/harbaum/galagino)
on a smaller board: no cabinet, no joystick, no SD card. You play by tilting it —
the accelerometer is the controller — and you browse with the two buttons.

**PELLETINO 1.0 was Pac-Man alone.** This is the same board grown into a
platform; that first release is preserved at tag
[`v1.0`](https://github.com/aedile/PELLETINO/releases/tag/v1.0), and it now
ships as one of the seventeen games under the codename `GHOSTERAMA`.

Same board as [DIABLITO](https://github.com/aedile/DIABLITO) (shareware Doom)
and [FIESTA-ENTERTAINMENT-SYSTEM](https://github.com/aedile/FIESTA-ENTERTAINMENT-SYSTEM)
(an NES).

### Where it came from

PELLETINO started as a Fiesta medal. Every April, San Antonio holds Fiesta, and
the city spends it trading and wearing medals — enamel pins, mostly, made by
everyone from the big parade organisations to somebody's dog. This project was
one of those: a medal that happened to have a screen, a battery and Pac-Man on
it, built to be pinned to a shirt and handed around. That is why some of the
code still says `medal` (`medalboot`, `medal_input`), why it remembers the game
you picked and boots straight into it, and why it takes a deliberate button hold
to do anything — it was designed to survive being worn. It has since outgrown
the occasion, but that is where it is from.

---

## Contents

- [Quick start](#quick-start)
- [Getting it running](#getting-it-running)
- [Choosing a build — `pelletino pick`](#choosing-a-build--pelletino-pick)
- [Controls](#controls)
- [Attract mode](#attract-mode)
- [The wheel](#the-wheel)
- [Music](#music)
- [Credits, on the device](#credits-on-the-device)
- [Adding a video "game"](#adding-a-video-game)
- [Artwork](#artwork)
- [Two games, one slot](#two-games-one-slot)
- [How a build is laid out](#how-a-build-is-laid-out)
- [Status and known gaps](#status-and-known-gaps)
- [Repository layout](#repository-layout)
- [Building without Docker](#building-without-docker)
- [Credits and license](#credits-and-license)

---

## Quick start

```sh
# put ROM zips in roms/ using their MAME names, plug the board in, then:
./install.sh
```

That is the whole install. It checks your machine, converts the ROMs you
supplied, builds each of those games and the launcher in Docker, fetches the
artwork, and flashes everything. With no board connected it builds and stops;
run `./pelletino flash` when one is. At the end it lists what went on, and what
was left out and why.

| | |
|---|---|
| `./install.sh --no-flash` | build everything, write nothing |
| `./install.sh --no-art` | skip the artwork download |
| `./install.sh /dev/cu.usbmodem101` | name the port if it guesses wrong |

More ROMs than fit? It picks a set that fits; run `./pelletino pick` first to
choose your own. Build logs land in `build/install-logs/`.

The steps it runs are also available one at a time:

```sh
./pelletino games      # the approved list, and which ROMs you already have
./pelletino pick       # choose a build when more games are present than fit
./pelletino build      # artwork + partition table + launcher firmware
./pelletino flash      # write it all to a connected board
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

It starts in attract mode. Press either button to reach the wheel, tap **BOOT**
and **PWR** to turn it one way and the other, and hold **BOOT** for two seconds
on a game to pick it. From then on it boots straight into that game. To come
back, hold **BOOT** for ten seconds in the game, or hold it while powering on.

### Troubleshooting

| Symptom | Cause |
|---|---|
| `docker is installed but not running` | Start Docker Desktop. |
| `nothing to build` | `roms/` has no approved ROM zip. Run `./pelletino games`. |
| `N games enabled but ESP-IDF allows at most 16` | Run `./pelletino pick`. |
| Menu says `NO ARTWORK` | The `mqart` partition was never written. Re-run `./pelletino flash`. |
| Every game says `NOT INSTALLED` | Expected before any game firmware is built. The launcher works; the slots are empty. |
| The launcher is silent | No music supplied, or the sound is off (`MUTED` in the header). See [Music](#music) and [Muting](#muting). |

---

## Choosing a build — `pelletino pick`

There is room for **16 game slots** inside **16 MB** of flash, shared with
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

Every game is played the same way physically: **hold it upright and twist
or tip it** — the tilt sensor is the joystick, spinner, wheel, or yoke. Two
buttons do the rest.

| Button | Short press | Hold |
|---|---|---|
| **BOOT** (middle) | the game's action — fire / jump / hop / pump | 10 s: back to the menu |
| **both together** | sound: loud, quiet, off | |
| **PWR** (top) | insert a coin (then auto-start ½ s later) | 1 s: power off |

In attract mode and the menu the same two buttons drive the launcher. Tilt does
nothing here:

| Button | Short press | Hold |
|---|---|---|
| **BOOT** (middle) | next game (or leave attract mode) | 2 s: pick this game |
| **PWR** (top) | previous game (or leave attract mode) | 1 s: power off |
| **both together** | sound: loud, quiet, off | |

### Muting

**Press both buttons together.** Each press steps the sound: loud, quiet, off,
and round again. It is the same gesture everywhere — attract mode, the menu, and
every game.

It is **one setting for the whole device**. Whatever it is left at anywhere is
what it is everywhere, including after it has been switched off and on again.
Every change shows `SOUND LOUD`, `SOUND QUIET` or `SOUND OFF` on the screen, and
the menu's header says `QUIET` or `MUTED` while it is not loud. Off powers the
audio codec down, so it is not spending battery on the speaker.

While both buttons are down neither counts as itself, so changing the sound
never also inserts a coin, fires, or turns the wheel.

### The backlight

Left alone for two minutes the screen dims. It never goes dark. A button
brings it back to full, and so does being moved — so a game being played, or a medal
being worn, stays lit, and one left on a table does not run its battery down
showing attract mode to nobody. In a game, a button pressed while the screen is
dimmed only wakes it; it is not a coin or a shot. The times are at the top of
`medal_input.cpp` (games) and in `main/input.cpp` (launcher).

The launcher also warns before the battery runs out: `BATTERY LOW` and a falling
pair of notes at 15%, `BATTERY DYING` at 5%.

> A **coin** is always a coin and a **start** is always a start, on every game.
> Mute and back-to-menu are the same gestures everywhere. Picking a
> game from the menu is a button *hold*; a knock won't do it.

> **The tilt centre is wherever you are holding it when you press coin**
> (and again at start). So hold it the way you mean to play before you press.
> If one direction stops registering mid-game, press coin again in your
> playing posture and it re-centres. A board lying flat on a table is not
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
matches the rest of the games.

---

## Attract mode

Left alone, it runs a loop, for as long as nobody touches it:

1. **The title.** The wordmark crosses a starfield and leaves, the screen
   flashes white to the sound of a blade being drawn, and the title screen comes
   up out of the flash: a perspective grid, a cabinet, the wordmark.
2. **How to play.** Which button does what, in the menu, in a game and anywhere,
   written out a row at a time.
3. **The games.** The wheel turns by itself through every game on the device,
   each with its logo and a screenshot.
4. **The credits.**

**Press a button and you are in the menu.** Leave the menu alone for 45 seconds
and it goes back to the loop. One tune plays straight through all of it.

None of this runs when a game is selected: that boots straight into the game.

The title, the instructions and the credits are drawn from primitives in
`components/fest`. There is no artwork, no sprite sheet and no bitmap in the
repository, and no character from any game is reproduced.

## The wheel

The menu is a wheel of game logos. The chosen game sits large in the middle with
a screenshot of it, dimmed, filling the panel behind; its neighbours are smaller
and dimmer above and below. A tap turns the wheel one place — fast off the mark,
settling as it arrives — and the screenshot changes half way through. The footer
says who made the game and when.

It is redrawn from scratch thirty times a second into a 67 KB frame buffer. The
logos are stored at the three sizes they rest at and scaled only while moving.

Pips down the right-hand edge show where on the wheel you are. Holding the
button to pick a game makes its logo swell while a tone climbs, so the hold can
be felt without reading the bar; letting go early takes both away. When the hold
completes a coin drops, the other games leave, the logo comes out of the screen
and the panel goes white into the game.

The panel's corners are rounded, so the header and the footer are centred and
nothing that has to be read sits near a corner.

### Sound effects

The flash, each step of the wheel, the hold, the coin and the battery warning
have sounds. They are synthesised — a few
ringing partials and a burst of noise, in `components/chiptune/src/sfx.c` — so
there is no sample to supply, and they are mixed over the music, or over
silence if you supplied none.

## Music

One tune plays through attract mode, the menu and the credits. **It does not ship
with the project** — music belongs to whoever wrote it — so the launcher is
silent until you supply one, and the build works either way.

```sh
tools/add_music.py splash ~/Downloads/some-game.nsf 3     # track 3 of that file
./pelletino build && ./pelletino flash
```

There is a second, optional slot: `tools/add_music.py credits <file>` gives the
Credits entry on the wheel a tune of its own.

| | NSF | MIDI |
|---|---|---|
| Played on | an emulated NES sound chip (2A03) | an emulated AY-3-8910 |
| Voices | two pulse, triangle, noise, samples | three square waves |
| Sounds like | the console it came from | a reduction of whatever you gave it |

**NSF is the better of the two by a distance.** It is a game's own music driver
— 6502 code — running on the chip it was written for. The launcher plays it with
the instruction-stepped 6502 the Atari games here already use and a 2A03 written
for this project, so there is no NES emulator in the launcher and nothing GPL.

Everything you put in `music/` stays on your machine; `.gitignore` excludes it.
[`music/README.md`](music/README.md) covers choosing a track and auditioning one
on your own computer before it goes near the device.

## Credits, on the device

The roll is part of attract mode, and it is also the last entry on the wheel:
hold the button on **Credits**. Either way it scrolls through everyone the
project owes something to: the people who made each game, the authors of the
emulator cores, the MAME team, and the libraries in the launcher. It is built
into the launcher, so it costs no flash slot.

The music is credited there too. Write who composed what you supplied in
`music/credits.txt` (there is a template beside it) and that text appears under
**MUSIC**, on the device that is playing their work.

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

## Artwork

The wheel shows a logo for each game and a screenshot behind it. **Both are
copyrighted, so we ship neither** — and we host neither. You have two options:

- **Fetch:** `./pelletino art` downloads them to your machine from
  [Arcade Database](http://adb.arcadeitalia.net), a third-party archive that
  files everything under the MAME name. `./install.sh` does this for you. See
  `tools/fetch_art.py` for the `PELLETINO_ART_BASE` override.
- **Supply your own:** `art/logo/<rom>.png` (transparent background) and
  `art/snap/<rom>.png`, at any resolution.

Both are optional. A game without a logo is its title in text, and one without a
screenshot sits over the splash's stars and grid, so the build never blocks on
missing art. `art/` is excluded by `.gitignore`.

`tools/pack_art.py` fits everything to the panel and writes the blob the
launcher reads; its header documents the format.

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
mqart                            the wheel's artwork (lcd/marquees.bin)
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
  gives you a wheel where everything reads `NOT INSTALLED`. `./install.sh`
  builds them all.
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
  (QMI8658), `fest` (the frame buffer everything is drawn in), `mqart`
  (artwork), `chiptune` (NSF and MIDI playback), `audio_hal`
  (ES8311 over I2S), `medalboot` (which game boots, and the way back out).
  Games carry their own shared components, including `medal_input`.
- `main/` — the launcher: wheel, splash, credits, input, battery, chain-boot.
- `tools/` — the build tooling behind `./pelletino`.
- `host/` — harnesses that run on your machine rather than the device: the wheel
  rendered to image files, and the music rendered to WAV.
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

PELLETINO stands on a great deal of other people's work. This is who, and where
to find them. The credits roll on the device names the same people.

### The games

Every game belongs to its maker. None of them ships with this project, and
nothing here is affiliated with or endorsed by any of them.

| Game | Made by | Year |
|---|---|---|
| Arkanoid | Taito | 1986 |
| Asteroids | Atari | 1979 |
| Burger Time | Data East | 1982 |
| Centipede | Atari | 1980 |
| Dig Dug | Namco | 1982 |
| Donkey Kong | Nintendo | 1981 |
| Empire Strikes Back | Atari | 1985 |
| Frogger | Konami | 1981 |
| Galaga | Namco | 1981 |
| Galaxian | Namco | 1979 |
| Gyruss | Konami | 1983 |
| Joust | Williams | 1982 |
| Lunar Lander | Atari | 1979 |
| Mario Bros. | Nintendo | 1983 |
| Missile Command | Atari | 1980 |
| Moon Patrol | Irem | 1982 |
| Mr. Do! | Universal | 1982 |
| Ms. Pac-Man | Midway | 1982 |
| Pac-Man | Namco | 1980 |
| Pole Position | Namco | 1982 |
| Rally-X | Namco | 1980 |
| Root Beer Tapper | Bally Midway | 1984 |
| Space Invaders | Taito | 1978 |
| Star Wars | Atari | 1983 |
| Street Fighter II | Capcom | 1991 |
| Tempest | Atari | 1981 |
| Time Pilot | Konami | 1982 |

### Emulation

| What | Who | Used for | Licence |
|---|---|---|---|
| [Z80 emulator](https://fms.komkon.org/EMUL8/) | Marat Fayzullin | the CPU in most of the games | free for non-commercial use |
| [vecx](https://github.com/jhawthorn/vecx) MC6809 | Valavan Manohararajah | Star Wars, Empire Strikes Back, Joust, Gyruss | GPL-3.0 |
| [chips](https://github.com/floooh/chips) 6502 | Andre Weissflog | Centipede | zlib |
| [MAME](https://www.mamedev.org/) ([source](https://github.com/mamedev/mame)) | Nicola Salmoria, Aaron Giles and every contributor since | the reference for how each machine behaves: memory maps, interrupt timing, palettes, sound levels | BSD-3-Clause |

The cores under `games/*/core/` were written for this project with MAME's
drivers used as hardware documentation. `LICENSING.md` records the line-by-line
comparison against the MAME source that backs that claim. None of this would
exist without the decades of work the MAME team has put into documenting these
machines.

### Software

| What | Who | Used for | Licence |
|---|---|---|---|
| [TinyMidiLoader](https://github.com/schellingb/TinySoundFont) | Bernhard Schelling | reading MIDI files | zlib |
| Helix MP3 decoder ([a widely used mirror](https://github.com/ultraembedded/libhelix-mp3)) | RealNetworks | the sound on the Street Fighter II video | RPSL |
| [font8x8](https://github.com/dhepper/font8x8) | Daniel Hepper | every letter on the screen | public domain |
| [ESP-IDF](https://github.com/espressif/esp-idf) | Espressif Systems | everything underneath | Apache-2.0 |

### Inspiration, and where things come from

- [**Galagino**](https://github.com/harbaum/galagino) by Till Harbaum showed that
  arcade machines fit on an ESP32, and how to be straight with people about ROMs.
  PELLETINO's manifest and its whole approach to "you supply the ROMs" follow it.
- [**Arcade Database**](http://adb.arcadeitalia.net/) is where `./pelletino art`
  downloads logos and screenshots from. They host them; this project does not.
- The board is Waveshare's
  [ESP32-C6-LCD-1.69](https://www.waveshare.com/wiki/ESP32-C6-LCD-1.69).
- Sister projects on the same board:
  [DIABLITO](https://github.com/aedile/DIABLITO) (shareware Doom) and
  [FIESTA-ENTERTAINMENT-SYSTEM](https://github.com/aedile/FIESTA-ENTERTAINMENT-SYSTEM)
  (an NES).

### Music

No music ships. Whatever you install is credited on the device from
`music/credits.txt`, so the composer is named on the thing playing their work.

### Licence

PELLETINO's own code is Zero-Clause BSD (`LICENSE`) — free for everyone, no
conditions. The assembled bundle is **not for sale**; see
[LICENSING.md](LICENSING.md) for the full picture, and each game's own `LICENSE`
and `THIRD_PARTY_NOTICES.md` for the authoritative per-game terms.

No game ROMs, artwork or music are distributed here.
