# HARDWARE.md

**Device:** ESP32-C6 Development Board (FIESTA26)  
**Architecture:** RISC-V  
**Reference:** Demo projects in `demo/ESP-IDF/`

---

## GPIO Pinout

| Signal/Function | GPIO | Direction | Pull | Notes |
|---|---|---|---|---|
| **SPI (LCD)** |
| LCD MOSI | GPIO_NUM_2 | Output | - | SPI data out to LCD |
| LCD SCLK | GPIO_NUM_1 | Output | - | SPI clock |
| LCD CS | GPIO_NUM_5 | Output | - | Chip select |
| LCD DC | GPIO_NUM_3 | Output | - | Data/command select |
| LCD RST | GPIO_NUM_4 | Output | - | Hardware reset |
| LCD BL | GPIO_NUM_6 | Output | - | Backlight PWM control |
| LCD MISO | GPIO_NUM_NC | - | - | Not connected |
| **I2C (Sensors & Peripherals)** |
| I2C SDA | GPIO_NUM_8 | Bidirectional | Internal | Data line |
| I2C SCL | GPIO_NUM_7 | Output | Internal | Clock line |
| **I2S (Audio Codec ES8311)** |
| I2S MCK | GPIO_NUM_19 | Output | - | Master clock |
| I2S BCK | GPIO_NUM_20 | Output | - | Bit clock |
| I2S LRCK | GPIO_NUM_22 | Output | - | Left/right clock (WS) |
| I2S DOUT | GPIO_NUM_23 | Output | - | Data out to codec |
| I2S DIN | GPIO_NUM_21 | Input | - | Data in from codec |
| **Buttons & Control** |
| BOOT Button | GPIO_NUM_9 | Input | External | User/boot button (Button 1) |
| PWR Button | GPIO_NUM_18 | Input | External | Power/user button (Button 2) |
| RST Button | CHIP_PU (EN) | Input | External | Hardware reset (not GPIO-accessible) |
| **Power Management** |
| BAT_EN | GPIO_NUM_15 | Output | - | Battery enable control |
| **ADC** |
| Battery Voltage | ADC1_CH0 | Input (Analog) | - | Battery voltage sense |

**Source Files:**
- `demo/ESP-IDF/*/components/esp_bsp/bsp_display.h`
- `demo/ESP-IDF/*/components/esp_bsp/bsp_i2c.h`
- `demo/ESP-IDF/*/components/esp_bsp/bsp_pwr.h`
- `demo/ESP-IDF/*/components/esp_bsp/bsp_es8311.c`
- `demo/ESP-IDF/*/main/main.c`
- `demo/Arduino/examples/02_button_example/02_button_example.ino`
- `demo/Arduino/examples/03_battery_example/03_battery_example.ino`

---

## Hardware Buttons

The device has **three physical buttons**:

1. **BOOT Button (GPIO 9)** — User-programmable button, also used for entering bootloader mode when held during reset. Active-low with external pullup.
   - Arduino example: `PIN_INPUT1 = 9`
   - ESP-IDF: `GPIO_NUM_9`
   
2. **PWR Button (GPIO 18)** — User-programmable power/function button. Active-low with external pullup.
   - Arduino example: `PIN_INPUT2 = 18`
   - ESP-IDF: `PWR_KEY_PIN = GPIO_NUM_18`
   - Used for power management in `bsp_pwr.c`
   
3. **RST Button (CHIP_PU/EN)** — Hardware reset button connected to the ESP32-C6 enable pin. **Not GPIO-accessible**. Resets the entire chip when pressed.
   - This is a standard ESP32 hardware reset mechanism
   - Cannot be read or controlled via software
   - Pressing RST resets the MCU to initial state

**Source:**
- `demo/Arduino/examples/02_button_example/02_button_example.ino` (demonstrates both BOOT and PWR buttons)
- `demo/ESP-IDF/*/components/esp_bsp/bsp_pwr.h` (PWR button definition)

---

## Peripheral Configurations

### SPI Bus (LCD)
| Parameter | Value | Notes |
|---|---|---|
| Host | SPI2_HOST | ESP32-C6 SPI peripheral 2 |
| Pixel Clock | 40 MHz | `EXAMPLE_LCD_PIXEL_CLOCK_HZ` |
| Mode | 0 | CPOL=0, CPHA=0 |
| DMA | SPI_DMA_CH_AUTO | Auto DMA channel allocation |
| Max Transfer Size | `EXAMPLE_LCD_H_RES * EXAMPLE_LCD_DRAW_BUFF_HEIGHT` | Typically 240×50 = 12,000 bytes |

**Source:** `bsp_display.c`, `bsp_display.h`

### I2C Bus (Sensors, RTC, Codec)
| Parameter | Value | Notes |
|---|---|---|
| Port | I2C_PORT_NUM (0) | ESP32-C6 I2C master port 0 |
| Clock Source | I2C_CLK_SRC_DEFAULT | Default clock source |
| Glitch Filter | 7 cycles | `glitch_ignore_cnt = 7` |
| Internal Pullup | Enabled | `enable_internal_pullup = 1` |

**Connected Devices:**
- QMI8658 IMU @ `0x6B`
- PCF85063 RTC @ `0x51`
- ES8311 Audio Codec @ `0x18` (CE pin low, `ES8311_ADDRESS_0`)
- CST816S Touch Controller (via I2C, pins shared - typically @ `0x15`)

**Source:** `bsp_i2c.c`, `bsp_i2c.h`, `demo/Arduino/examples/04_es8311_example/es8311.h`

### I2S (Audio Interface)
| Parameter | Value | Notes |
|---|---|---|
| Port | I2S_NUM_0 | ESP32-C6 I2S peripheral 0 |
| Role | I2S_ROLE_MASTER | Master mode |
| Sample Rate | 16,000 Hz | `sample_rate_hz = 16000` |
| Data Width | 16 bits | `data_bit_width = 16` |
| Slot Mode | I2S_SLOT_MODE_STEREO | Stereo audio |
| WS Width | 16 bits | `ws_width = 16` |
| MCLK Multiple | I2S_MCLK_MULTIPLE_256 | Master clock = 256 × sample rate |
| Clock Source | I2S_CLK_SRC_DEFAULT | Default clock source |

**Source:** `bsp_es8311.c`

### ADC (Battery Voltage)
| Parameter | Value | Notes |
|---|---|---|
| Unit | ADC_UNIT_1 | ADC unit 1 |
| Channel | ADC_CHANNEL_0 | Physical pin: ADC1_CH0 |
| Attenuation | ADC_ATTEN_DB_12 | 0-3.3V range (approx) |
| Bit Width | ADC_BITWIDTH_DEFAULT | Typically 12-bit |
| Calibration | Curve/Line Fitting | eFuse-based calibration if available |

**Source:** `bsp_battery.c`, `bsp_battery.h`

### LCD Backlight PWM
| Parameter | Value | Notes |
|---|---|---|
| Timer | LEDC_TIMER_0 | LEDC timer 0 |
| Speed Mode | LEDC_LOW_SPEED_MODE | Low-speed mode |
| Channel | LEDC_CHANNEL_0 | Channel 0 |
| Frequency | 5,000 Hz | `LCD_BL_LEDC_FREQUENCY = 5000` |
| Duty Resolution | 10 bits | `LEDC_TIMER_10_BIT` (0-1023) |
| GPIO | GPIO_NUM_6 | Backlight pin |

**Source:** `bsp_display.h`, `bsp_display.c`

---

## Display Specifications

| Parameter | Value | Notes |
|---|---|---|
| Controller | ST7789 | LCD driver IC |
| Resolution | 240 × 280 pixels | `EXAMPLE_LCD_H_RES × EXAMPLE_LCD_V_RES` |
| Color Depth | 16 bpp | RGB565 format |
| RGB Element Order | RGB | `LCD_RGB_ELEMENT_ORDER_RGB` |
| Invert Colors | Enabled | `esp_lcd_panel_invert_color(..., true)` |
| LVGL Draw Buffer | 240 × 50 pixels | `EXAMPLE_LCD_DRAW_BUFF_HEIGHT = 50` |
| Double Buffering | Configurable | `EXAMPLE_LCD_DRAW_BUFF_DOUBLE` |

**Source:** `main.c`, `bsp_display.c`

---

## Touch Controller (CST816S)

| Parameter | Value | Notes |
|---|---|---|
| IC | CST816S | Capacitive touch controller |
| Interface | I2C | Shares I2C bus (GPIO_8/GPIO_7) |
| Driver | `esp_lcd_touch_cst816s` | IDF component v1.0.6+ |
| I2C Address | Typically `0x15` | Check component datasheet |
| Interrupt Pin | Not explicitly defined | May use touch semaphore in code |

**Note:** Touch input device code is commented out in `main.c` (lines 183-185). Touch controller configuration is not fully active in the factory demo but is listed as a dependency.

**Source:** `main/idf_component.yml`, `main.c`

---

## I2C Device Register Maps

### QMI8658 IMU (I2C Address: 0x6B)

**Key Registers:**
| Register | Address | Description |
|---|---|---|
| WHO_AM_I | 0x00 | Device ID |
| REVISION_ID | 0x01 | Revision ID |
| CTRL1–CTRL9 | 0x02–0x0A | Control registers |
| TEMP_L, TEMP_H | 0x33–0x34 | Temperature data |
| AX_L, AX_H | 0x35–0x36 | Accelerometer X (LSB, MSB) |
| AY_L, AY_H | 0x37–0x38 | Accelerometer Y |
| AZ_L, AZ_H | 0x39–0x3A | Accelerometer Z |
| GX_L, GX_H | 0x3B–0x3C | Gyroscope X (LSB, MSB) |
| GY_L, GY_H | 0x3D–0x3E | Gyroscope Y |
| GZ_L, GZ_H | 0x3F–0x40 | Gyroscope Z |
| RESET | 0x60 | Software reset |

**Data Structure:**
```c
typedef struct {
    int16_t acc_x, acc_y, acc_z;
    int16_t gyr_x, gyr_y, gyr_z;
    float AngleX, AngleY, AngleZ;
    float temp;
} qmi8658_data_t;
```

**Source:** `bsp_qmi8658.h`

### PCF85063 RTC (I2C Address: 0x51)

**Key Registers:**
| Register | Address | Description |
|---|---|---|
| CONTROL_1 | 0x00 | Control register 1 |
| CONTROL_2 | 0x01 | Control register 2 |
| OFFSET | 0x02 | Clock offset |
| RAM_BYTE | 0x03 | General-purpose RAM |
| SECONDS | 0x04 | Seconds (BCD) |
| MINUTES | 0x05 | Minutes (BCD) |
| HOURS | 0x06 | Hours (BCD) |
| DAYS | 0x07 | Day of month (BCD) |
| WEEKDAYS | 0x08 | Day of week |
| MONTHS | 0x09 | Month (BCD) |
| YEARS | 0x0A | Year (BCD, 00-99) |
| SECOND_ALARM | 0x0B | Alarm seconds |
| MINUTE_ALARM | 0x0C | Alarm minutes |
| HOUR_ALARM | 0x0D | Alarm hours |
| DAY_ALARM | 0x0E | Alarm day |
| WEEKDAY_ALARM | 0x0F | Alarm weekday |
| TIMER_VALUE | 0x10 | Timer countdown value |
| TIMER_MODE | 0x11 | Timer control |

**Source:** `bsp_pcf85063.h`

### ES8311 Audio Codec

| Parameter | Value | Notes |
|---|---|---|
| I2C Address | `0x18` | `ES8311_ADDRESS_0` (CE pin low) |
| Alternate Address | `0x19` | `ES8311_ADDRESS_1` (CE pin high) |
| Codec Mode | ESP_CODEC_DEV_WORK_MODE_BOTH | Simultaneous input/output |
| PA Pin | GPIO_NUM_NC | No external PA control |
| Use MCLK | Enabled | `use_mclk = true` |
| PA Voltage | 5.0 V | `pa_voltage = 5.0` |
| Codec DAC Voltage | 3.3 V | `codec_dac_voltage = 3.3` |
| Sample Rate | 16,000 Hz | Configurable |
| Channels | 1 (Mono) | `fs.channel = 1` |
| Bits per Sample | 16 | `fs.bits_per_sample = 16` |

**Source:** `bsp_es8311.c`, `demo/Arduino/examples/04_es8311_example/es8311.h`

---

## Power Management

### Battery
| Parameter | Value | Notes |
|---|---|---|
| Enable Pin | GPIO_NUM_15 (BAT_EN) | Controls battery enable/disable |
| Voltage Sense | ADC1_CH0 | Battery voltage measurement |
| ADC Attenuation | ADC_ATTEN_DB_12 | ~0–3.3V range |
| ADC Unit | ADC_UNIT_1 | |

**Notes:**
- Battery enable is set as an output in `bsp_pwr.c`.
- Voltage monitoring reads ADC1_CH0 and applies calibration (curve fitting or line fitting).

**Source:** `bsp_pwr.c`, `bsp_battery.c`

### Power Button
| Parameter | Value | Notes |
|---|---|---|
| GPIO | GPIO_NUM_18 (PWR_KEY_PIN) | Input |
| Function | Power on/off control | Monitored in `bsp_pwr_task` |
| Debounce | Handled by `iot_button` library | |

**Source:** `bsp_pwr.h`, `bsp_pwr.c`

### Boot Button
| Parameter | Value | Notes |
|---|---|---|
| GPIO | GPIO_NUM_9 | Input (external pullup/config) |
| Function | User input / factory boot | Configured in `main.c` |
| Library | `iot_button` (espressif/button) | v4.1.0+ |

**Source:** `main.c`

---

## Memory & LVGL Configuration

### LVGL Task
| Parameter | Value | Notes |
|---|---|---|
| Task Priority | 4 | `task_priority = 4` |
| Task Stack Size | 4096 bytes | `task_stack = 4096` |
| Task Affinity | -1 (no pinning) | `task_affinity = -1` |
| Max Sleep | 500 ms | `task_max_sleep_ms = 500` |
| Timer Period | 5 ms | `timer_period_ms = 5` |

**Source:** `main.c`

### Display Buffer
| Parameter | Value | Notes |
|---|---|---|
| Buffer Size | 240 × 50 pixels | `EXAMPLE_LCD_H_RES * EXAMPLE_LCD_DRAW_BUFF_HEIGHT` |
| Bytes per Pixel | 2 (RGB565) | 16-bit color |
| Total Buffer Size | 24,000 bytes | Per buffer (×2 if double buffering) |
| Double Buffering | Configurable | `EXAMPLE_LCD_DRAW_BUFF_DOUBLE = 1` |
| DMA Allocation | Enabled | `flags.buff_dma = true` |

**Source:** `main.c`

---

## Timing Specifications

### Display
| Parameter | Value | Notes |
|---|---|---|
| SPI Clock | 40 MHz | `EXAMPLE_LCD_PIXEL_CLOCK_HZ` |
| Backlight PWM | 5 kHz | `LCD_BL_LEDC_FREQUENCY = 5000` |

### Audio
| Parameter | Value | Notes |
|---|---|---|
| Sample Rate | 16,000 Hz | Configurable in `bsp_es8311.c` |
| I2S MCLK | 4.096 MHz | 256 × 16,000 Hz |
| I2S BCK | ~512 kHz | 32 × sample rate (stereo, 16-bit) |

### I2C
| Parameter | Value | Notes |
|---|---|---|
| Clock Speed | Not explicitly set | Defaults to ESP-IDF I2C master defaults (typically 100 kHz or 400 kHz) |
| Glitch Filter | 7 cycles | `glitch_ignore_cnt = 7` |

**Source:** `bsp_display.h`, `bsp_es8311.c`, `bsp_i2c.c`

---

## Device Dependencies (IDF Components)

| Component | Version | Purpose |
|---|---|---|
| `lvgl/lvgl` | ^8.4.0 | GUI library |
| `espressif/esp_lcd_touch_cst816s` | ^1.0.6 | Touch controller driver |
| `espressif/esp_lvgl_port` | ^2.5.0 | LVGL integration for ESP-IDF |
| `espressif/button` | ^4.1.0 | Button management library |
| `espressif/esp_codec_dev` | ^1.3.4 | Audio codec abstraction |

**Source:** `main/idf_component.yml`

---

## Additional Information for HAL Design

### Key Items for HAL Abstraction:
1. **Peripheral Init Sequence:**
   - Order: Power → I2C → Battery → Sensors (QMI8658, PCF85063, ES8311) → Display → LVGL → Buttons
   - Source: `main.c` `app_main()` function

2. **Hardware Capabilities:**
   - **IMU:** 6-axis motion (accel + gyro), temperature sensor
   - **RTC:** Real-time clock with alarm, timer, and battery backup
   - **Audio:** Full-duplex I2S codec with mic and speaker
   - **Display:** 240×280 RGB565 LCD with hardware SPI
   - **Touch:** Capacitive touch (CST816S, currently disabled in demos)

3. **Calibration Data:**
   - **ADC:** eFuse-based calibration for battery voltage (curve fitting or line fitting)
   - **RTC:** Clock offset register for fine-tuning

4. **Power Sequencing:**
   - Power button monitoring via `PWR_KEY_PIN`
   - Battery enable via `BAT_EN_PIN`
   - No explicit power-down sequence documented (requires user implementation)

5. **DMA & Memory:**
   - SPI uses DMA (`SPI_DMA_CH_AUTO`)
   - LVGL buffers are DMA-capable (`buff_dma = true`)
   - Consider heap fragmentation for large allocations

6. **Clock Configuration:**
   - SPI: 40 MHz pixel clock
   - I2S: 16 kHz sample rate, 256× MCLK
   - PWM: 5 kHz backlight, 10-bit resolution

7. **Interrupt Handling:**
   - Touch interrupt semaphore created but not fully wired (`touch_int_BinarySemaphore`)
   - Button library handles debounce and events

8. **Sensor Data Formats:**
   - QMI8658: 16-bit signed integers for raw accel/gyro, floats for angles
   - PCF85063: BCD-encoded time registers
   - Battery ADC: Raw voltage converted via calibration curves

9. **Button Functionality:**
   - **BOOT button (GPIO 9):** General-purpose user input, bootloader entry when held during reset
   - **PWR button (GPIO 18):** Power management, general-purpose user input, monitored in `bsp_pwr_task`
   - **RST button (CHIP_PU/EN):** Hardware reset only, not software-accessible
   - Both GPIO buttons are active-low with external pullups

10. **I2C Address Summary:**
    - ES8311 codec: `0x18` (CE pin low configuration)
    - QMI8658 IMU: `0x6B`
    - PCF85063 RTC: `0x51`
    - CST816S touch: typically `0x15` (not explicitly configured in demos)

---

## Additional Findings from Arduino Examples

**Key Differences / Clarifications:**

1. **ES8311 I2C Address Confirmed:**
   - Hardware uses `0x18` (CE pin tied low)
   - Alternate address `0x19` available if CE pin tied high
   - Source: `demo/Arduino/examples/04_es8311_example/es8311.h`

2. **Button Naming Consistency:**
   - ESP-IDF uses: `PWR_KEY_PIN` (GPIO 18), `GPIO_NUM_9` (boot)
   - Arduino uses: `PIN_INPUT2` (GPIO 18), `PIN_INPUT1` (GPIO 9)
   - Both refer to the same physical buttons

3. **Battery Enable Pattern:**
   - Must be set HIGH to enable battery voltage measurement
   - `pinMode(BAT_EN_PIN, OUTPUT); digitalWrite(BAT_EN_PIN, HIGH);`
   - Source: `demo/Arduino/examples/03_battery_example/03_battery_example.ino`

4. **Hardware Reset (RST Button):**
   - Connected to ESP32-C6 CHIP_PU/EN pin
   - Not GPIO-accessible or software-controllable
   - Standard ESP32 hardware feature for MCU reset

5. **LCD Pin Naming:**
   - Arduino examples use `LCD_RST` (GPIO 4) for LCD reset, not to be confused with the RST button
   - LCD_RST is a GPIO output for resetting the ST7789 display controller

**No Additional Hardware Found:**
- No additional GPIOs, UARTs, or peripherals beyond what was documented
- Touch controller (CST816S) is present as an IDF component dependency but init code is commented out in factory demo
- All three physical buttons accounted for: BOOT, PWR, RST

---

## References

**Source Code Locations:**
- **ESP-IDF:** `demo/ESP-IDF/01_factory/components/esp_bsp/` — BSP drivers for all peripherals
- **ESP-IDF:** `demo/ESP-IDF/01_factory/main/main.c` — Application initialization and LVGL setup
- **Arduino:** `demo/Arduino/examples/` — 14 example projects demonstrating all peripherals
- **Config:** `demo/ESP-IDF/*/sdkconfig.defaults` — ESP32-C6 target configuration

**Key Files:**
- `bsp_display.c/.h` — LCD SPI, backlight PWM
- `bsp_i2c.c/.h` — I2C bus configuration
- `bsp_es8311.c` — Audio codec and I2S
- `bsp_qmi8658.h` — IMU register map
- `bsp_pcf85063.h` — RTC register map
- `bsp_battery.c/.h` — ADC battery monitoring
- `bsp_pwr.c/.h` — Power button and battery enable
- `main/idf_component.yml` — Component dependencies
- `demo/Arduino/examples/02_button_example/` — Both button usage example
- `demo/Arduino/examples/04_es8311_example/es8311.h` — ES8311 I2C address definitions

**Datasheets & Schematics:**
- `docs/flatten_ESP32-C6-LCD-1.69-Schematic (1).pdf` — Hardware schematic (button wiring, pull-ups, etc.)
- `docs/ESP32-C6_Technical_Reference_Manual.pdf` — SoC documentation
- `docs/QMI8658C.pdf`, `docs/PCF85063A.pdf`, `docs/ES8311.DS.pdf` — Peripheral datasheets

**ESP32-C6 Documentation:**
- [ESP32-C6 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-c6_technical_reference_manual_en.pdf)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/)

---

**Document Version:** 1.0  
**Date:** 2025-12-15  
**Compiled by:** GitHub Copilot (Automated Hardware Documentation Tool)
