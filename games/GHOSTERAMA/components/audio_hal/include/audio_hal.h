/*
 * audio_hal.h - ES8311 Audio HAL for FIESTA26
 *
 * Provides I2S audio output through ES8311 codec
 */

#ifndef AUDIO_HAL_H
#define AUDIO_HAL_H

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Audio configuration
#define AUDIO_SAMPLE_RATE 20050 // Output sample rate (Hz)
#define AUDIO_MAX_SAMPLES 1024  // Max samples rendered per audio_update() (~50 ms catch-up cap)
#define AUDIO_DMA_FRAME_NUM 256 // Samples per DMA descriptor
#define AUDIO_DMA_BUFFERS 8     // DMA descriptors: 8 x 256 = ~100 ms of queue

// I2S Pin definitions (FIESTA26)
#define PIN_I2S_MCK GPIO_NUM_19
#define PIN_I2S_BCK GPIO_NUM_20
#define PIN_I2S_LRCK GPIO_NUM_22
#define PIN_I2S_DOUT GPIO_NUM_23
#define PIN_I2S_DIN GPIO_NUM_21

// I2C for ES8311 control (shared bus)
#define PIN_I2C_SDA GPIO_NUM_8
#define PIN_I2C_SCL GPIO_NUM_7
#define ES8311_ADDR 0x18

/**
 * Initialize audio subsystem (ES8311 + I2S)
 */
void audio_init(void);

/**
 * Update audio - call every frame (any frame rate).
 * Renders exactly as many samples as wall-clock time has elapsed since the
 * previous call, so the I2S DMA queue neither starves nor overflows.
 */
void audio_update(void);

/**
 * Number of I2S DMA underruns since boot (queue ran dry). Diagnostic.
 */
uint32_t audio_get_underrun_count(void);

/**
 * Set master volume
 * @param volume 0-255
 */
void audio_set_volume(uint8_t volume);

/**
 * Get pointer to sound registers (for Z80 memory mapping)
 * @return Pointer to 32-byte sound register array
 */
uint8_t *audio_get_sound_registers(void);

/**
 * Control ES8311 power state for battery savings
 * @param enabled true to power on codec, false to put in low-power mode
 */
void audio_set_power_state(bool enabled);

/**
 * Set mute state
 * @param muted true to mute audio, false to unmute
 */
void audio_set_mute(bool muted);

/**
 * Get current mute state
 * @return true if muted, false if not
 */
bool audio_get_mute(void);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_HAL_H
