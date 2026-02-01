# PELLETINO Agent Reference

Quick reference for AI agents working on this project.

## ESP-IDF Environment Setup

**ESP-IDF Version:** 5.3+  
**Target:** ESP32-C6  
**Python:** 3.8+

### Source the Environment

```bash
# Adjust paths to your ESP-IDF installation
. $HOME/esp/esp-idf/export.sh

# Or if using custom path:
# . /path/to/esp-idf/export.sh
```

### Common Commands

```bash
# Set target (first time only)
idf.py set-target esp32c6

# Build only
idf.py build

# Build and flash
idf.py flash

# Flash and monitor (Ctrl+] to exit monitor)
idf.py flash monitor

# Monitor only
idf.py monitor

# Clean build
idf.py fullclean

# Menuconfig
idf.py menuconfig

# Size analysis
idf.py size-components
```

## ROM Conversion

### Required ROMs

Place Pac-Man ROM files in `tools/` directory:
- `pacman.6e`, `pacman.6f`, `pacman.6h`, `pacman.6j` (program ROMs)
- `pacman.5e` (tiles), `pacman.5f` (sprites)
- `82s123.7f`, `82s126.4a`, `82s126.1m`, `82s126.3m` (PROMs)

### Convert ROMs

```bash
cd tools
python3 convert_roms.py
```

Output files are generated in `main/roms/`:
- `pacman_rom.h` - Program code
- `pacman_tilemap.h` - Tile graphics
- `pacman_spritemap.h` - Sprite graphics
- `pacman_cmap.h` - Color palette
- `pacman_wavetable.h` - Audio waveforms

## Hardware Target

- **MCU:** ESP32-C6 RISC-V @ 160MHz
- **Display:** ST7789 240×280 @ 40MHz SPI
- **Audio:** ES8311 I2S codec @ 22.05kHz
- **Board:** Waveshare ESP32-C6-LCD-1.69
- **IMU:** QMI8658 6-axis (optional tilt control)

## Project Structure

```
PELLETINO/
├── main/
│   ├── main.c            # Main entry point
│   ├── src/              # Game logic
│   ├── include/          # Headers
│   └── roms/             # Generated ROM headers (gitignored)
├── components/
│   ├── audio_hal/        # ES8311 + WSG synthesis
│   ├── display/          # ST7789 driver
│   ├── z80_cpu/          # Z80 emulator
│   ├── pacman_hw/        # Hardware emulation
│   └── input/            # Button/IMU input
├── tools/
│   └── convert_roms.py   # ROM converter
└── docs/                 # Documentation
```

## Configuration

### Display Settings

`components/display/include/st7789.h`:
```c
#define LCD_WIDTH  240
#define LCD_HEIGHT 280
#define LCD_SPI_FREQ_HZ (40 * 1000 * 1000)
```

### Audio Settings

`components/audio_hal/include/wsg_synth.h`:
```c
#define WSG_SAMPLE_RATE 22050
#define WSG_CHANNELS 3
```

### Performance Tuning

In `idf.py menuconfig`:
- CPU frequency: 160 MHz
- FreeRTOS tick rate: 1000 Hz
- Compiler optimization: Performance (-O2)

## Development Workflow

1. **Make code changes**
2. **Build:** `idf.py build`
3. **Check errors:** Review compiler output
4. **Flash:** `idf.py flash`
5. **Monitor:** `idf.py monitor` (Ctrl+] to exit)
6. **Debug:** Use serial output and ESP-IDF logging

## Debugging

### Enable verbose logging

In code:
```c
#include "esp_log.h"
static const char *TAG = "PACMAN";

ESP_LOGI(TAG, "Info message");
ESP_LOGW(TAG, "Warning message");
ESP_LOGE(TAG, "Error message");
```

In `idf.py menuconfig`:
- Component config → Log output → Default log verbosity → Info

### Common Issues

**Build fails:** Ensure ROM headers exist (run `convert_roms.py`)
**Flash fails:** Check USB connection and port permissions
**No display:** Verify SPI connections and menuconfig settings
**No audio:** Check I2S/I2C wiring to ES8311

## Key Files

| File | Purpose |
|------|---------|
| `main/src/main.cpp` | Main loop, game selection |
| `tools/convert_roms.py` | ROM to C header converter |
| `sdkconfig.defaults` | ESP-IDF default configuration |
| `partitions.csv` | Flash partition layout |

## Debugging Tips

- Use `idf.py monitor` to see serial output
- Press Ctrl+] to exit monitor
- Check `build/pelletino.elf` for symbols
- Binary size should be ~340KB (84% of partition free)
