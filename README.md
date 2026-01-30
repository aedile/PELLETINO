# PELLETINO

ESP32-C6 Pac-Man arcade simulator for the Waveshare FIESTA26 platform.

## Overview

PELLETINO is a Pac-Man arcade simulator targeting the ESP32-C6 with:
- **Display:** 240×280 ST7789 LCD (near-perfect fit for 224×288 game)
- **Audio:** ES8311 I2S codec with Namco WSG wavetable synthesis
- **Input:** GPIO buttons + optional IMU tilt control

Based on learnings from [TRENCHRUNNER](../TRENCHRUNNER) and code patterns from [Galagino](https://github.com/harbaum/galagino).

## Architecture

**Single-Core Design** (ESP32-C6 has only one core at 160MHz)

```
┌─────────────────────────────────────────────────────────────┐
│                 PELLETINO ARCHITECTURE                       │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌──────────────────────────────────────────────────────┐   │
│  │              MAIN LOOP (Core 0)                       │   │
│  │  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐  │   │
│  │  │Z80 Exec │→│ Render  │→│ Audio   │→│ Input   │  │   │
│  │  │(1 frame)│  │ Tiles   │  │ Buffer  │  │ Poll    │  │   │
│  │  └─────────┘  └─────────┘  └─────────┘  └─────────┘  │   │
│  └──────────────────────────────────────────────────────┘   │
│                           │                                  │
│           ┌───────────────┴───────────────┐                 │
│           ▼                               ▼                 │
│  ┌─────────────────┐            ┌─────────────────┐         │
│  │  SPI DMA (LCD)  │            │  I2S (ES8311)   │         │
│  │  Background TX  │            │  DMA Audio      │         │
│  └─────────────────┘            └─────────────────┘         │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

## Quick Start

### Prerequisites

1. **ESP-IDF 5.x** installed and configured
2. **Pac-Man ROM files** (not included - extract from original hardware)
3. **Z80 emulator** by Marat Fayzullin

### ROM Setup

Place your legally obtained ROM files in `main/roms/`:
```
pacman.6e    (4KB) - Program ROM 1
pacman.6f    (4KB) - Program ROM 2
pacman.6h    (4KB) - Program ROM 3
pacman.6j    (4KB) - Program ROM 4
pacman.5e    (4KB) - Character/tile graphics
pacman.5f    (4KB) - Sprite graphics
82s123.7f   (32B)  - Color PROM
82s126.4a   (256B) - Palette PROM
82s126.1m   (256B) - Sound PROM 1
82s126.3m   (256B) - Sound PROM 2
```

Then run the ROM conversion:
```bash
cd tools
python3 convert_roms.py
```

### Build & Flash

```bash
idf.py set-target esp32c6
idf.py build
idf.py flash monitor
```

## Hardware

**Target Platform:** Waveshare ESP32-C6-LCD-1.69 (FIESTA26)

| Component | Specification |
|-----------|---------------|
| MCU | ESP32-C6 RISC-V @ 160MHz |
| RAM | 512KB HP SRAM |
| Display | ST7789 240×280 @ 40MHz SPI |
| Audio | ES8311 I2C Codec |
| IMU | QMI8658 6-axis |
| Buttons | GPIO9 (BOOT), GPIO18 (PWR) |

## Project Structure

```
PELLETINO/
├── main/                    # Application entry point
│   ├── include/
│   ├── src/
│   └── roms/               # Converted ROM headers (gitignored)
├── components/
│   ├── audio_hal/          # ES8311 + Namco WSG synthesis
│   ├── display/            # ST7789 driver (240×280)
│   ├── z80_cpu/            # Z80 emulator wrapper
│   └── pacman_hw/          # Pac-Man memory map & I/O
├── docs/
└── tools/                  # ROM conversion scripts
```

## Audio

Full Namco WSG wavetable synthesis with 3 channels:
- **Waka-waka** - Pac-Man eating dots
- **Ghost eaten** - Chomping ghosts
- **Death** - Pac-Man dies
- **Fruit** - Bonus fruit collected
- **Intermission** - Cutscene music

## Legal Notice

This project requires original Pac-Man ROM files which are **NOT included**.
You must extract these from your own legally owned arcade hardware.
ROM files are explicitly excluded from version control.

## Credits

- **Galagino** by Till Harbaum - Reference implementation
- **Z80 Emulator** by Marat Fayzullin
- **TRENCHRUNNER** - Sister project, ESP-IDF patterns
