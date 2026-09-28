# MINIMAME

A menu medal. It browses marquee artwork and chain-boots the other game medals;
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

`NULL` means not installed, and the menu greys that marquee out. Adding a game
means adding a marquee and a partition — **the launcher is never recompiled**.

## Sticky selection

The medal remembers what you picked. Once a game is selected it boots straight
into it and the menu never appears, which is what you want on a medal you hand to
someone. The selection lives in NVS and survives power cycles.

| | |
|---|---|
| In the menu, **hold the button 2 s** on a game | selects it and boots it, permanently |
| In a game, **hold the button 10 s** | forgets the selection, returns to the menu |
| **Hold the button while powering on** | forgets the selection, shows the menu |

Taps do nothing at all, so a medal cannot be started by a knock in a pocket. The
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
the medal. Putting it after display or IMU setup silently breaks the safety
property and nothing will complain. `medalboot_game_running()` clears the
loop guard: if a selected game is booted `MEDALBOOT_MAX_ATTEMPTS` times without
ever confirming it got that far, the launcher gives up on it and shows the menu
instead of retrying forever.

A game that carries more than one ROM set reads `medalboot_rom()` to find out
which one it was asked for.

## Memory

There is no framebuffer. 240×280 RGB565 would be 131 KB of 512 KB with no PSRAM,
so the screen is drawn in horizontal bands (`components/gfx`): clear a band, draw
what intersects it, push it, move down. Two buffers are the whole UI cost — a
19 KB band and a 17 KB scratch for marquee rows.

**Every pixel in a band is RGB565 stored big-endian.** The marquee blob is packed
that way too, so blitting art is a `memcpy` rather than a per-pixel byte swap, and
bands go straight to `display_write_preswapped()`. Build colours with `gfx_rgb()`,
which returns a value already swapped — never write raw RGB565 into a band.

Marquees are read with `esp_partition_read()` rather than a memory map. That lands
pixels in RAM, which SPI DMA can reach — **flash-mapped addresses cannot be used as
a DMA source** — and avoids the MMU's limits on how much can be mapped at once.

## Input

Tilt browses, one game per detent. Raw tilt would rip through sixteen games in
half a second, so a step fires only when roll crosses 18°, and no further step can
fire until roll falls back inside 8°. Holding past the threshold auto-repeats,
slowly at first. PWR short-presses re-level the neutral pose; long-press cuts the
battery rail. `BAT_EN` (GPIO15) must stay HIGH or the medal powers itself off.

## Artwork pipeline

```
marquees/*.png            source art, any resolution
  └─ tools/pack_marquees.py
       ├─ lcd/preview/*.png   fitted PNGs, for eyeballing
       └─ lcd/marquees.bin    the blob that gets flashed
            └─ tools/flash_mqart.sh   writes it to the mqart partition
```

Art is fitted inside 208×104 preserving aspect. At 208 wide, most marquees land
between 49 and 81 px tall; Star Wars is the one outlier that is height-limited
instead, at 179×104. The app and the artwork flash separately, so updating every
marquee needs no rebuild.

## Flash budget (16 MB)

The numbers below are for the shipped lineup (9 September 2026): 16 game slots
plus the Street Fighter II video's 5.5 MB data partition. `./pelletino` prices
any other pick live, and `./pelletino pick` chooses one that fits.

| Region | Size | Notes |
|---|---|---|
| `launcher` | 512 KB | built: 245 KB |
| `mqart` | 640 KB | blob: 472 KB, 17 marquees |
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
  `tools/pack_marquees.py`; the launcher trusts its header.
- **Do not add a `#define`-style build switch.** Galagino's model (edit a
  `config.h`, run a converter by hand) was deliberately rejected: the presence
  of a ROM zip in `roms/` *is* the switch, and the partition table follows from
  it.
- **Do not merge the games into one monolithic image.** See "Why chain-boot
  instead of one image" above - a large refactor with real regression risk on
  games that work, to reclaim flash that is not needed.
- **Do not skip the hardware step.** A host harness that stubs the panel is only
  as honest as the stub: one verified pixels perfectly for two days while the
  real DMA path was throwing most of them away. Prove a change on the medal.
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
