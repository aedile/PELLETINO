# PELLETINO

A menu. It turns a wheel of game logos and chain-boots the games;
it runs no emulation itself.

Hardware: Waveshare ESP32-C6-LCD-1.69 — 240×280 ST7789V2, 16 MB flash,
512 KB SRAM, **no PSRAM**. QMI8658 IMU, BOOT (GPIO9) and PWR (GPIO18) buttons.

## Why chain-boot instead of one image

Six game firmwares already exist as independent projects, and their `display.cpp`
has diverged between them. Merging them into shared components would be a large
refactor with real regression risk on games that currently work, to reclaim about
3 MB of duplicated IDF runtime out of 16 MB that is otherwise unused.

Flash is the cheap resource here. Each game keeps its own repo and its own image.

## The partition trick

Every game's app partition is **labelled with its ROM name**, so the launcher
needs no table mapping games to slots:

```c
esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "galaga")
```

`NULL` means not installed, and the menu says so. Adding a game
means adding its artwork and a partition — **the launcher is never recompiled**.

## Sticky selection

It remembers what you picked. Once a game is selected it boots straight
into it and the menu never appears, which is what you want on something you hand to
someone. The selection lives in NVS and survives power cycles.

| | |
|---|---|
| In the menu, **hold the button 2 s** on a game | selects it and boots it, permanently |
| In a game, **hold the button 5 s** | forgets the selection, returns to the menu |
| **Hold the button while powering on** | forgets the selection, shows the menu |

Taps do nothing at all, so it cannot be started by a knock in a pocket. The
menu fills a progress bar while the button is held — without it nobody knows how
long "a few seconds" means.

The power-on escape is the one that always works, and it is worth knowing about
before you need it.

## What a game must do

Three lines, all from `components/medalboot` (copy it into the game project or
point `EXTRA_COMPONENT_DIRS` at it, so both ends agree on the NVS keys):

```c
void app_main(void)
{
    medalboot_game_startup();          // FIRST LINE, before anything risky
    ...init...
    medalboot_game_running();          // once stable, a few seconds in

    for (;;) {
        if (medalboot_exit_hold(button_is_down)) medalboot_exit_to_menu();
        ...
    }
}
```

`medalboot_game_startup()` points the boot partition back at the launcher before
anything can fail, so a panic, a watchdog bite or a brownout lands in the menu
rather than boot-looping a broken game. That is exactly why it must be the genuine
first statement of `app_main`: a panic *before* it runs never reaches the
launcher, so neither the attempts counter nor the power-on escape can rescue
the device. Putting it after display or IMU setup silently breaks the safety
property and nothing will complain. `medalboot_game_running()` clears the
loop guard: if a selected game is booted `MEDALBOOT_MAX_ATTEMPTS` times without
ever confirming it got that far, the launcher gives up on it and shows the menu
instead of retrying forever.

A game that carries more than one ROM set reads `medalboot_rom()` to find out
which one it was asked for.

## Memory

Everything the launcher shows is animated, so it is all drawn into one frame
buffer (`components/fest`): 240×280 at 8 bits a pixel, 67 KB, with a palette —
a 6×6×5 colour cube, a few named colours, and 56 entries the wheel reloads with
the colours of whichever screenshot is behind it. It is converted to the panel's
RGB565 twenty rows at a time on the way out, through a 10 KB strip, which is
also where the scanlines are put in. With the wheel up and music playing there
is 323 KB of heap free.

The artwork partition is memory-mapped and pictures are decoded a row at a time
straight out of flash into the frame buffer. **Flash-mapped addresses cannot be
used as a DMA source**, which is one more reason nothing goes from the artwork
to the panel directly.

## Input

The launcher is driven by the two buttons; tilt does nothing in it. BOOT steps
forward and PWR back, a two second hold on BOOT picks, a one second hold on PWR
cuts the battery rail, and both together is mute. `BAT_EN` (GPIO15) must stay
HIGH or the board powers itself off.

## Artwork pipeline

```
art/logo/*.png, art/snap/*.png    source art, any resolution (tools/fetch_art.py)
  └─ tools/pack_art.py
       ├─ lcd/preview/*.png   the screenshots as fitted, for eyeballing
       └─ lcd/marquees.bin    the blob that gets flashed
            └─ tools/flash_mqart.sh   writes it to the mqart partition
```

Each game gets its logo at the three sizes it rests at on the wheel and one
screenshot the size of the panel, all run-length coded a row at a time; the
header of `tools/pack_art.py` is the format's documentation. The app and the
artwork flash separately, so changing the art needs no rebuild.

## Flash budget (16 MB)

The numbers below are for the shipped lineup (9 September 2026): 16 game slots
plus the Street Fighter II video's 5.5 MB data partition. `./pelletino` prices
any other pick live, and `./pelletino pick` chooses one that fits.

| Region | Size | Notes |
|---|---|---|
| `launcher` | 512 KB | built: 245 KB |
| `mqart` | 640 KB | blob: 477 KB, 17 games |
| 16 game slots | ~9.1 MB | right-sized per game, not uniform |
| `media` (SF2 video) | 5.5 MB | the one data partition; what makes flash the binding limit |
| free | 0.31 MB | not enough for even the smallest game (384 KB) |

**All 16 OTA slots are used — `ota_0` through `ota_15` is ESP-IDF's hard cap.**
If a seventeenth game is ever wanted, collapse a pair that shares hardware onto
one image and select the ROM at boot: GHOSTERAMA already carries Pac-Man and
Ms. Pac-Man, and WALKERRUN carries Star Wars and Empire Strikes Back. Each merge
frees a slot and deletes a duplicated codebase.

## Things not to do

Rules that were learned the hard way and are easy to violate without noticing:

- **Do not hand-edit `partitions.csv`.** It is generated by `tools/configure.py`
  from `games.toml` plus whatever is in `roms/`, and overwritten on every build.
  Change `games.toml` (or the pick) instead.
- **Do not touch `lcd/marquees.bin` by hand.** Regenerate it with
  `tools/pack_art.py`. The launcher checks what it reads from it, but
  it is not a format to edit by hand.
- **Do not add a `#define`-style build switch.** Galagino's model (edit a
  `config.h`, run a converter by hand) was deliberately rejected: the presence
  of a ROM zip in `roms/` *is* the switch, and the partition table follows from
  it.
- **Do not merge the games into one monolithic image.** See "Why chain-boot
  instead of one image" above - a large refactor with real regression risk on
  games that work, to reclaim flash that is not needed.
- **Do not skip the hardware step.** A host harness that stubs the panel is only
  as honest as the stub: one verified pixels perfectly for two days while the
  real DMA path was throwing most of them away. Prove a change on the hardware.
- **Do not power the codec down without a path back up.** The shared
  `audio_hal` copied into every game deletes the I2S channel and sleeps the
  ES8311 on mute; for months nothing re-powered them, so the 3 s mute hold was
  one-way in every game. `audio_set_mute(false)` now calls
  `audio_set_power_state(true)`; keep that when the HAL is copied or rewritten,
  and test the hold twice, not once.

## Build

```sh
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 \
    idf.py -B build_docker -DIDF_TARGET=esp32c6 build
```

Flashing must happen from the host — Docker Desktop on macOS cannot reach USB.
