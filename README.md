# PELLETINO

**An arcade in your pocket. Up to seventeen games at a time, picked from twenty-six. Spin the wheel, hold the button on a game, and it boots straight into that game from then on. One $20 ESP32-C6 board, 16 MB of flash, no SD card, no PSRAM.**

PELLETINO is the menu. Every game is a separate firmware image in its own flash
slot and the launcher chain-boots it, so a game gets the whole chip to itself
while it runs. The partition table, the artwork and the menu are all generated
from the ROMs you supply. You never hand-edit a partition table, a header or a
makefile.

> **Licensing in one line:** the code written here is 0BSD, but the assembled
> bundle is **free to share, not to sell**. Most games embed a non-commercial
> Z80 core, and four are GPL-3.0. GitHub's sidebar says "0BSD"; that covers our
> code only. Read [LICENSING.md](LICENSING.md) before you distribute.
>
> **No ROMs, no artwork and no music are included.** Those belong to their
> owners; supplying them is your part.

| | |
|---|---|
| Games | **26** playable, 27 approved in `games.toml`, **16** flash slots per build |
| Launcher firmware | 324 KB in a 512 KB slot |
| Artwork | 477 KB for 17 games (three logos and a screenshot each), in its own partition |
| Free heap in the menu with music playing | 320 KB of 512 KB |
| Menu frame time | 27 to 37 ms, so 30 fps most of the time |
| Cold boot to the title | ~1.2 s |
| Attract loop | title, instructions, the games, credits: about 1 min 45 s, until a button is pressed |
| Hardware | Waveshare ESP32-C6-LCD-1.69: 240×280 ST7789V2, ES8311 codec, QMI8658 IMU, two buttons plus reset, LiPo |

Every number here was measured on the device over serial.

Think of it as a tilt-controlled [Galagino](https://github.com/harbaum/galagino)
on a smaller board: no cabinet, no joystick, no SD card. The accelerometer is
the controller. The two buttons browse the menu.

**PELLETINO 1.0 was Pac-Man alone.** This is the same board grown into a
platform. That first release is preserved at tag
[`v1.0`](https://github.com/aedile/PELLETINO/releases/tag/v1.0), and it now
ships as one of the games under the codename `GHOSTERAMA`.

Same board as [DIABLITO](https://github.com/aedile/DIABLITO) (shareware Doom)
and [FIESTA-ENTERTAINMENT-SYSTEM](https://github.com/aedile/FIESTA-ENTERTAINMENT-SYSTEM)
(an NES).

### Where it came from

PELLETINO started as a medal for Fiesta San Antonio 2026. Fiesta is San
Antonio's citywide festival every April, and collecting, trading and wearing
medals is a big part of it. This one had a screen, a battery and Pac-Man on it,
and it was built to be pinned to a shirt.

That history explains a few things. Some of the code still says `medal`
(`medalboot`, `medal_input`). It remembers the game you picked and boots
straight into it. Picking a game takes a deliberate two-second hold, because a
medal on a shirt gets bumped. It works fine as a pocket arcade with no Fiesta
involved, but that is where it came from.

---

## Contents

- [Quick start](#quick-start)
- [Getting it running](#getting-it-running)
- [Choosing a build: `pelletino pick`](#choosing-a-build-pelletino-pick)
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
run `./pelletino flash` once one is plugged in. At the end it lists what went
on the board, what was left out, and why.

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
missing. It changes nothing, so it is always safe to run. `./pelletino help`
lists every command.

A game appears in the menu only if its ROM is in `roms/`. Games you don't have a
ROM for are left out: no slot, no menu entry, no wasted flash.

---

## Getting it running

### 1. Prerequisites

- **Docker**, running. The toolchain lives in `espressif/idf:v5.3.4`; nothing
  else is installed on your machine.
- **`esptool.py`** on the host (`brew install esptool`, or `pip install esptool`).
  Flashing cannot run inside Docker, because Docker Desktop on macOS can't reach USB.
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
their slot empty and show as `NOT INSTALLED` in the menu. Flash them later
without rebuilding anything else.

### 6. First boot

It starts in attract mode. Press either button to reach the wheel. Tap the
middle button (**BOOT**) for the next game and the top button (**PWR**) for the
previous one, and hold the middle button for two seconds on a game to pick it.
From then on it boots straight into that game. To come back, hold the middle
button for five seconds in the game, or hold it while powering on.

### Troubleshooting

| Symptom | Cause |
|---|---|
| `docker is installed but not running` | Start Docker Desktop. |
| `nothing to build` | `roms/` has no approved ROM zip. Run `./pelletino games`. |
| `N slots needed but ESP-IDF allows at most 16` | Run `./pelletino pick`. |
| Menu says `NO ARTWORK` | The `mqart` partition was never written. Re-run `./pelletino flash`. |
| Every game says `NOT INSTALLED` | Expected before any game firmware is built. The launcher works; the slots are empty. |
| The launcher is silent | No music supplied, or the sound is off (`MUTED` in the header). See [Music](#music) and [Sound](#sound). |

---

## Choosing a build: `pelletino pick`

There is room for **16 game slots** inside **16 MB** of flash, shared with
the launcher and the artwork. More games are approved than fit, and you may have
more ROMs than fit. When a build overflows, `./pelletino pick` lists everything
you have and updates the totals as you choose (slots used, flash used, space
free), so you can settle on a build that fits before you flash it:

```
   1. [x] Ms. Pac-Man          1024 KB   mspacman
   2. [x] Pole Position        1024 KB   polepos
   3. [ ] Star Wars             768 KB   starwars
   ...
  slots 11/16   flash 12.75/16 MB   3.25 MB free
```

Type a number to toggle a game, `a` to auto-pick everything that fits, `s` to
save, `q` to quit. It writes `selection.txt`, which the build follows. Delete
that file (or run `./pelletino pick --clear`) to go back to including every ROM
present. You only need it when a build overflows.

---

## Controls

Every game is played the same way: **hold it upright and twist or tip it.** The
tilt sensor is the joystick, spinner, wheel or yoke. Two buttons do the rest.

| Button | Short press | Hold |
|---|---|---|
| **BOOT** (middle) | the game's action: fire, jump, hop, pump | 5 s: back to the menu |
| **both together** | sound: loud, quiet, off | |
| **PWR** (top) | insert a coin (then auto-start ½ s later) | 1 s: power off |

In attract mode and the menu the same two buttons drive the launcher. Tilt does
nothing there.

| Button | Short press | Hold |
|---|---|---|
| **BOOT** (middle) | next game (or leave attract mode) | 2 s: pick this game |
| **PWR** (top) | previous game (or leave attract mode) | 1 s: power off |
| **both together** | sound: loud, quiet, off | |

The third button on the board is reset.

### Sound

**Press both buttons together.** Each press steps the sound: loud, quiet, off,
then back to loud. It is the same in attract mode, the menu and every game.

It is **one setting for the whole device**. Set it anywhere and it holds
everywhere, including after a power cycle. Every change shows `SOUND LOUD`,
`SOUND QUIET` or `SOUND OFF` on the screen, and the menu's header reads `QUIET`
or `MUTED` when it is not loud. Off also powers the audio codec down to save
battery.

While both buttons are down neither one counts on its own, so changing the
sound never inserts a coin, fires or turns the wheel.

### Backlight

After two minutes with no input the screen dims to about 10%. It never turns
off. A button press or movement brings it back to full brightness, so a game
being played or a medal being worn stays lit, and one sitting on a table saves
its battery. In a game, a button press while the screen is dimmed only wakes it
and does not count as a coin or a shot. The timing is set at the top of
`medal_input.cpp` (games) and in `main/input.cpp` (launcher).

The launcher warns before the battery runs out: `BATTERY LOW` with two falling
notes at 15%, and `BATTERY DYING` at 5%. The games do not read the battery, so
the warning only appears in the launcher.

> A **coin** is always a coin and a **start** is always a start, in every game.
> Sound and back-to-menu are the same gestures everywhere. Picking a game from
> the menu is a button *hold*, so a bump won't do it.

> **The tilt center is wherever you are holding it when you press coin**, and
> again at start. Hold it the way you mean to play before you press. If one
> direction stops registering mid-game, press coin again in your playing
> posture and it re-centers. A board lying flat on a table is not being held,
> and its tilt is ignored until it is picked up.

What tilt and BOOT do in each game:

| Game | ROM | Tilt does | BOOT does |
|---|---|---|---|
| Pac-Man | `pacman` | steer (4-way maze) | insert a coin (PWR does too) |
| Ms. Pac-Man | `mspacman` | steer (4-way maze) | insert a coin (PWR does too) |
| Galaga | `galaga` | move the fighter L/R | fire |
| Galaxian | `galaxian` | move the fighter L/R | fire |
| Space Invaders | `invaders` | move the cannon L/R | fire |
| Lunar Lander | `llander` | twist = rotate, tip away = throttle (analog) | abort |
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
| Star Wars | `starwars` | flight yoke: twist yaws, tip pitches | fire (also starts, in free play) |
| Pole Position | `polepos` | steer like a wheel | shift gear (the throttle is automatic) |
| Street Fighter II | `sf2` | nothing (it is a video, see below) | nothing |

Pole Position starts on a coin (free play) and holds the accelerator down for
you, so the whole game is the wheel plus a tap to shift gear.

---

## Attract mode

Left alone, it loops through four screens until someone presses a button:

1. **Title.** The PELLETINO wordmark crosses a starfield and leaves the screen,
   the screen flashes white with a sword sound, and the title screen fades in:
   a perspective grid, a cabinet and the wordmark.
2. **How to play.** Which button does what in the menu, in a game and anywhere,
   one row at a time.
3. **The games.** The wheel turns on its own through every game on the device,
   each with its logo and a screenshot.
4. **Credits.**

**Press a button and you are in the menu.** Leave the menu alone for 45 seconds
and it goes back to the loop. One tune plays through all of it.

None of this runs when a game is selected. That boots straight into the game.

The title, the instructions and the credits are drawn in code
(`components/fest`). There is no artwork, sprite sheet or bitmap in the
repository, and no character from any game is reproduced.

## The wheel

The menu is a wheel of game logos. The selected game sits large in the middle
with a dimmed screenshot of it filling the screen behind. Its neighbors are
smaller and dimmer above and below. A tap turns the wheel one step, and the
screenshot changes halfway through the step. The footer shows who made the game
and when.

- **Position.** Pips down the right edge show where you are on the wheel.
- **Hold to pick.** While you hold the button the logo grows and a tone rises,
  so you can tell the hold is working without watching the progress bar.
  Letting go early cancels it.
- **Launch.** When the hold completes a coin drops, the other games slide away,
  the logo zooms toward you and the screen goes white into the game.
- **Rounded corners.** The panel's corners are rounded, so the header and footer
  are centered and nothing you need to read sits near a corner.

The whole screen is redrawn 30 times a second into a 67 KB frame buffer. Logos
are stored at the three sizes they sit at and are only scaled while moving.

### Sound effects

The flash, each step of the wheel, the hold, the coin and the battery warning
all have sounds. They are synthesized in code
(`components/chiptune/src/sfx.c`), so there is nothing to supply, and they are
mixed over the music, or over silence if you supplied none.

## Music

One tune plays through attract mode, the menu and the credits. **It does not ship
with the project.** Music belongs to whoever wrote it, so the launcher is silent
until you supply a file, and the build works either way.

```sh
tools/add_music.py splash ~/Downloads/some-game.nsf 3     # track 3 of that file
./pelletino build && ./pelletino flash
```

There is a second, optional slot: `tools/add_music.py credits <file>` gives the
Credits entry on the wheel its own tune.

| | NSF | MIDI |
|---|---|---|
| Played on | an emulated NES sound chip (2A03) | an emulated AY-3-8910 |
| Voices | two pulse, triangle, noise, samples | three square waves |
| Sounds like | the console it came from | a simplified version of what you gave it |

**NSF sounds much better.** An NSF is a game's own music driver (6502 code)
running on the chip it was written for. The launcher plays it with the same 6502
core the Atari games here use and a 2A03 written for this project, so there is
no NES emulator in the launcher and nothing GPL.

Everything you put in `music/` stays on your machine; `.gitignore` excludes it.
[`music/README.md`](music/README.md) covers choosing a track and listening to
one on your computer before you flash it.

## Credits, on the device

The credits roll is part of attract mode, and it is also the last entry on the
wheel: hold the button on **Credits**. Either way it scrolls through the people
who made each game, the authors of the emulator cores, the MAME team, and the
libraries in the launcher. It is built into the launcher, so it costs no flash
slot.

The music is credited there too. Put the composer of whatever you supplied in
`music/credits.txt` (there is a template next to it) and that text appears under
**MUSIC**.

---

## Adding a video "game"

A game slot can hold a looping video clip instead of an emulator. Street Fighter
II ships as its attract-mode reel. The clip lives in its own data partition.

- Encode and pack a clip with `games/HADOUKEN/tools/pack_media.py`. It
  letterboxes to the portrait panel and writes `media.bin`. See that script for
  the size limit and encoding settings; a longer clip needs a bigger `data_kb`
  in `games.toml`.
- The video slot is switched on by `games/HADOUKEN/media.bin` existing, the same
  way a ROM zip switches on an emulated game.

Only one game per build may carry a data partition (it is labelled `media`, which
is the label the player looks for).

---

## Artwork

The wheel shows a logo for each game and a screenshot behind it. **Both are
copyrighted, so we ship neither and host neither.** You have two options:

- **Fetch:** `./pelletino art` downloads them to your machine from
  [Arcade Database](http://adb.arcadeitalia.net), a third-party archive that
  files everything under the MAME name. `./install.sh` does this for you. See
  `tools/fetch_art.py` for the `PELLETINO_ART_BASE` override.
- **Supply your own:** `art/logo/<rom>.png` (transparent background) and
  `art/snap/<rom>.png`, at any resolution.

Both are optional. A game without a logo shows its title in text, and one
without a screenshot gets the title screen's stars and grid, so the build never
fails on missing art. `art/` is excluded by `.gitignore`.

`tools/pack_art.py` fits everything to the panel and writes the file the
launcher reads. The format is documented at the top of that script.

---

## Two games, one slot

Pac-Man and Ms. Pac-Man are carried in **one image** that picks which to run at
boot, so the pair costs one slot instead of two. In `games.toml`, Ms. Pac-Man owns
the slot and Pac-Man shares it (`boots = "mspacman"`). Both still appear as separate
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
`games.toml` plus whatever is in `roms/`, so the firmware and the flasher
always agree on where a game lives. The launcher finds a game by its
partition label, so adding a game never recompiles the launcher.

Turning a game off **frees its slot**. The partition table is generated, so an
eight-game build gets eight slots and the rest of the flash stays empty.

[`ARCHITECTURE.md`](ARCHITECTURE.md) covers the boot handshake between a game
and the menu, how memory is used, and why chain-booting beat one big image.

---

## Status and known gaps

- **ESP-IDF allows at most 16 OTA slots** (`ota_0` to `ota_15`). That is the hard
  limit on games per build, not flash size, and it is why `pelletino pick`
  exists. Two games that share hardware can share one image and one slot.
- **Arkanoid and Rally-X are switched off in `games.toml` as shipped**, to make
  room for the full Street Fighter II clip. Delete that line to bring one
  back, and leave something else out.
- **The battery percentage is an estimate.** It comes from a standard 3.7 V
  LiPo discharge curve (the table at the top of `main/battery.c`), read while
  the board is running, so it dips under load.
- **Roadmap:** Future exploration includes ESP-NOW peer-to-peer multiplayer and
  badge "score bumping" (trading high scores wirelessly between devices).
- **A game that crashes before its first line runs** (the one that points the
  boot partition back at the launcher) can boot-loop, because control never
  reaches the menu. `ARCHITECTURE.md` covers the handshake.
- **Empire Strikes Back (`esb`) is approved but not playable.** It runs on the
  same vector core as Star Wars, but the scene is heavier and it does not hold
  frame rate. That is why it has no row in the controls table.
- **No hero video or photos yet** in this repository.

---

## Repository layout

- `games/`: every game's source, vendored in so one clone builds everything.
  Most of them also live in their own `aedile/*` repos; `VENDORED.md` lists
  the upstream and commit for each.
- `components/`: code shared by the launcher. `display` (ST7789), `imu`
  (QMI8658), `fest` (the frame buffer everything is drawn in), `mqart`
  (artwork), `chiptune` (music and sound effects), `audio_hal` (ES8311 over
  I2S), `medalboot` (which game boots, and the way back out). Games carry their
  own copies of the shared components, including `medal_input`.
- `main/`: the launcher. Wheel, title, instructions, credits, input, battery,
  chain-boot.
- `tools/`: the build tooling behind `./pelletino` and `./install.sh`.
- `host/`: test harnesses that run on your computer instead of the device. One
  renders the wheel to image files, the others render music and sound effects
  to WAV and check them.
- `games.toml`: the one file that decides what a build *can* contain.

---

## Building without Docker

`./pelletino build` uses the `espressif/idf:v5.3.4` Docker image, so you need
nothing installed but Docker. If you have ESP-IDF v5.3.4 installed natively,
run the two generators and then build as usual:

```sh
python3 tools/pack_art.py && python3 tools/configure.py
idf.py -B build_docker -DIDF_TARGET=esp32c6 build
```

The generators need Python 3.11 and Pillow. Keep the build directory name,
because `./pelletino flash` looks there.

Docker Desktop on macOS can't reach USB, so **flashing always runs on the
host**. That is what `tools/flash_all.sh` (behind `./pelletino flash`) does.

---

## Credits and license

PELLETINO is built on a lot of other people's work. The credits roll on the
device names the same people.

### The games

Every game belongs to its maker. None of them ship with this project, and this
project is not affiliated with or endorsed by any of them.

| Game | Made by | Year |
|---|---|---|
| Arkanoid | Taito | 1986 |
| Asteroids | Atari | 1979 |
| Burger Time | Data East | 1982 |
| Centipede | Atari | 1981 |
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

| What | Who | Used for | License |
|---|---|---|---|
| [Z80 emulator](https://fms.komkon.org/EMUL8/) | Marat Fayzullin | the CPU in most of the games | free for non-commercial use |
| [vecx](https://github.com/jhawthorn/vecx) MC6809 | Valavan Manohararajah | Star Wars, Empire Strikes Back, Joust, Gyruss | GPL-3.0 |
| [chips](https://github.com/floooh/chips) 6502 | Andre Weissflog | Centipede | zlib |
| [MAME](https://www.mamedev.org/) ([source](https://github.com/mamedev/mame)) | Nicola Salmoria, Aaron Giles and every contributor since | the reference for how each machine behaves: memory maps, interrupt timing, palettes, sound levels | BSD-3-Clause |

The cores under `games/*/core/` were written for this project using MAME's
drivers as hardware documentation. `LICENSING.md` records the comparison
against the MAME source that backs that up. None of this would exist without
the decades of work the MAME team has put into documenting these machines.

### Software

| What | Who | Used for | License |
|---|---|---|---|
| [TinyMidiLoader](https://github.com/schellingb/TinySoundFont) | Bernhard Schelling | reading MIDI files | zlib |
| Helix MP3 decoder ([a widely used mirror](https://github.com/ultraembedded/libhelix-mp3)) | RealNetworks | the sound on the Street Fighter II video | RPSL |
| [font8x8](https://github.com/dhepper/font8x8) | Daniel Hepper | all the text on the screen | public domain |
| [ESP-IDF](https://github.com/espressif/esp-idf) | Espressif Systems | the framework all of it runs on | Apache-2.0 |

### Inspiration and sources

- [**Galagino**](https://github.com/harbaum/galagino) by Till Harbaum showed that
  arcade machines fit on an ESP32. The "you supply the ROMs" approach here
  follows his.
- [**Arcade Database**](http://adb.arcadeitalia.net/) is where `./pelletino art`
  downloads logos and screenshots from. They host them. This project does not.
- The board is Waveshare's
  [ESP32-C6-LCD-1.69](https://www.waveshare.com/wiki/ESP32-C6-LCD-1.69).
- Sister projects on the same board:
  [DIABLITO](https://github.com/aedile/DIABLITO) (shareware Doom) and
  [FIESTA-ENTERTAINMENT-SYSTEM](https://github.com/aedile/FIESTA-ENTERTAINMENT-SYSTEM)
  (an NES).

### Music

No music ships. Whatever you install is credited on the device from
`music/credits.txt`.

### License

PELLETINO's own code is Zero-Clause BSD (`LICENSE`): free for everyone, no
conditions. The assembled bundle is **not for sale**; see
[LICENSING.md](LICENSING.md) for the full picture, and each game's own `LICENSE`
and `THIRD_PARTY_NOTICES.md` for the authoritative per-game terms.

No game ROMs, artwork or music are distributed here.
