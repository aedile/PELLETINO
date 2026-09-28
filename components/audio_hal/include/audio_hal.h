/*
 * audio_hal.h - ES8311 Audio HAL for FIESTA26
 *
 * Provides I2S audio output through ES8311 codec
 */

#ifndef AUDIO_HAL_H
#define AUDIO_HAL_H

#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#endif

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
/* Supplied by the application (components/chiptune provides it): fill buf with
 * `samples` mono 16-bit samples at `rate`. audio_update() calls this to top up
 * the I2S DMA queue, so production is paced by the DAC rather than the CPU. */
void audio_render(int16_t *buf, int samples, int rate);

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
 * For checking that sound is really being made: how many bytes the DAC has
 * taken since the channel was opened, and the loudest sample rendered since
 * this was last called.
 */
void audio_get_stats(uint32_t *played_bytes, int *peak);

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
