# PELLETINO

A menu. It turns a wheel of game logos and chain-boots the games;
it runs no emulation itself.

Hardware: Waveshare ESP32-C6-LCD-1.69 — 240×280 ST7789V2, 16 MB flash,
512 KB SRAM, **no PSRAM**. QMI8658 IMU, BOOT (GPIO9) and PWR (GPIO18) buttons.

## Why chain-boot instead of one image

The games were written as independent firmwares, twenty-seven of them now, and
each carries its own copy of the shared components. Merging them into one image
would be a large refactor with real regression risk on games that work, to
reclaim the duplicated IDF runtime out of flash that the chain-boot layout
spends on it instead.

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

A tap turns the wheel and nothing more, so a knock in a pocket cannot start a
game. The menu fills a progress bar while the button is held, and swells the
logo under a rising tone, so that the hold can be felt as well as seen.

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
is 320 KB of heap free.

The artwork partition is memory-mapped and pictures are decoded a row at a time
straight out of flash into the frame buffer. **Flash-mapped addresses cannot be
used as a DMA source**, which is one more reason nothing goes from the artwork
to the panel directly.

## Input

The launcher is driven by the two buttons; tilt does nothing in it. BOOT steps
forward and PWR back, a two second hold on BOOT picks, a one second hold on PWR
cuts the battery rail, and both together steps the sound (loud, quiet, off). `BAT_EN` (GPIO15) must stay
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

The numbers below are for a lineup with the emulated Street Fighter II: 13
games in 12 slots, plus its data partition. `./pelletino` prices any other
pick live, and `./pelletino pick` chooses one that fits.

| Region | Size | Notes |
|---|---|---|
| housekeeping | 128 KB | bootloader, partition table, `nvs`, `otadata`, `phy_init` |
| `launcher` | 512 KB | built: 324 KB |
| `mqart` | 512 KB | about 30 KB a game, plus a quarter spare |
| 12 game slots | 7,360 KB | right-sized per game, not uniform |
| `media` (SF2 ROM) | 7,616 KB | the one data partition; what makes flash the binding limit |
| free | 256 KB | not enough for even the smallest game (384 KB) |

With the video in place of the emulator (5,504 KB) there is room for more games
than there are slots, and the limit becomes the slots: **`ota_0` through
`ota_15` is ESP-IDF's hard cap.**

Sixteen slots can hold 17 games: GHOSTERAMA carries both Pac-Man and
Ms. Pac-Man in one image and picks between them at boot. WALKERRUN's firmware
can carry Star Wars or Empire Strikes Back, but one at a time, from the ROM
header it is built with, so those two still take a slot each. Any other pair
that shares hardware could be merged the same way; each merge frees a slot.

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
- **Do not power the codec down without a path back up, and make the path back
  a cold start.** The shared `audio_hal` copied into every game deletes the I2S
  channel and sleeps the ES8311 when the sound is turned off. Twice this went
  wrong. First nothing re-powered them, so turning the sound off was one-way.
  Then they were re-powered by writing the codec's registers back, which left
  every register reading correctly and the speaker silent. Turning the sound on
  now runs the same code as power-on: codec from reset, then the I2S channel.
  Keep that when the HAL is copied or rewritten, and test off-then-on from a
  boot with the sound already off.

## Build

```sh
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.3.4 \
    idf.py -B build_docker -DIDF_TARGET=esp32c6 build
```

Flashing must happen from the host — Docker Desktop on macOS cannot reach USB.
