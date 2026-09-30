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

// Audio configuration. SHORYUKEN: the rate is a knob (main/knobs.h), and everything that
// was sized for 20050 Hz is sized from it.
#include "knobs.h"
#define AUDIO_SAMPLE_RATE SOUND_RATE                  // Output sample rate (Hz)
#define AUDIO_MAX_SAMPLES (AUDIO_SAMPLE_RATE / 20)    // Max samples rendered per audio_update() (50 ms catch-up cap)
#define AUDIO_DMA_FRAME_NUM (AUDIO_SAMPLE_RATE > 22050 ? 512 : 256) // Samples per DMA descriptor
#define AUDIO_DMA_BUFFERS 8     // DMA descriptors: ~93 ms of queue at 22050 and at 44100

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
