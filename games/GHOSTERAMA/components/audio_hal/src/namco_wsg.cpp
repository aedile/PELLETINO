/*
 * namco_wsg.cpp - Namco WSG (Waveform Sound Generator) Emulation
 *
 * Ported from Galagino by Till Harbaum
 */

#include "namco_wsg.h"
#include "audio_hal.h"
#include <cstring>

// The WSG adds each voice's 20-bit frequency word to a 20-bit accumulator at
// 96 kHz (3.072 MHz / 32); the top 5 accumulator bits index the 32-sample wave.
// We run the accumulator at AUDIO_SAMPLE_RATE instead, scaled up by 2^12 into a
// 32-bit counter so the wave index is simply the top 5 bits (>> 27).
#define WSG_CLOCK_HZ 96000
#define WSG_OVERSAMPLE 4      // sub-samples averaged per output sample (48 must divide by it)
static const uint32_t WSG_STEP_SCALE =
    (uint32_t)(((uint64_t)WSG_CLOCK_HZ * 4096 + AUDIO_SAMPLE_RATE / 2) / AUDIO_SAMPLE_RATE);

// Internal state for 3 channels
static uint32_t snd_cnt[WSG_CHANNELS] = {0, 0, 0};
static uint32_t snd_freq[WSG_CHANNELS] = {0, 0, 0};
static const int8_t *snd_wave[WSG_CHANNELS] = {nullptr, nullptr, nullptr};
static uint8_t snd_volume[WSG_CHANNELS] = {0, 0, 0};

// Wavetable pointer (set from ROM data)
static const int8_t *wavetable_data = nullptr;

// Default silent waveform
static const int8_t silent_wave[WSG_WAVE_SIZE] = {0};

void wsg_init(const int8_t *wavetable)
{
    wavetable_data = wavetable;

    for (int ch = 0; ch < WSG_CHANNELS; ch++) {
        snd_cnt[ch] = 0;
        snd_freq[ch] = 0;
        snd_wave[ch] = silent_wave;
        snd_volume[ch] = 0;
    }
}

void wsg_parse_registers(const uint8_t *regs)
{
    if (!regs) return;

    /*
     * Pac-Man sound register layout:
     *
     * Voice 0 (at 0x5040-0x5054 in memory, offset 0x00-0x14 here):
     *   0x10: frequency[3:0] (only voice 0 has this extra nibble)
     *   0x11: frequency[7:4]
     *   0x12: frequency[11:8]
     *   0x13: frequency[15:12]
     *   0x14: frequency[19:16]
     *   0x15: volume
     *
     * Voice 1 (at 0x5046-0x5055):
     *   0x00: frequency[3:0] + waveform (combined)
     *   0x01-0x04: frequency[7:4] to [19:16]
     *   0x06: waveform select (upper nibble sometimes)
     *   0x16: volume
     *
     * Actually, the layout from Galagino:
     *   Channel 0: regs 0x10..0x14 freq, 0x15 vol, 0x05 wave
     *   Channel 1: regs 0x11..0x14 freq (partial), 0x16 vol
     *   Channel 2: ...
     *
     * Let's use Galagino's proven parsing:
     */

    // Parse all three WSG channels
    for (int ch = 0; ch < WSG_CHANNELS; ch++) {
        // Channel volume
        snd_volume[ch] = regs[ch * 5 + 0x15] & 0x0F;

        if (snd_volume[ch]) {
            // Frequency (20-bit accumulator)
            // Channel 0 has extra low nibble at 0x10
            snd_freq[ch] = (ch == 0) ? (regs[0x10] & 0x0F) : 0;
            snd_freq[ch] |= (regs[ch * 5 + 0x11] & 0x0F) << 4;
            snd_freq[ch] |= (regs[ch * 5 + 0x12] & 0x0F) << 8;
            snd_freq[ch] |= (regs[ch * 5 + 0x13] & 0x0F) << 12;
            snd_freq[ch] |= (regs[ch * 5 + 0x14] & 0x0F) << 16;

            // Waveform select
            uint8_t wave_idx = regs[ch * 5 + 0x05] & 0x0F;
            if (wavetable_data) {
                snd_wave[ch] = wavetable_data + (wave_idx * WSG_WAVE_SIZE);
            } else {
                snd_wave[ch] = silent_wave;
            }
        } else {
            snd_freq[ch] = 0;
            snd_wave[ch] = silent_wave;
        }
    }
}

void wsg_render(int16_t *buffer, uint32_t samples)
{
    // Per-sample phase increments (32-bit wraparound is the intended modulo)
    const uint32_t step0 = snd_freq[0] * WSG_STEP_SCALE;
    const uint32_t step1 = snd_freq[1] * WSG_STEP_SCALE;
    const uint32_t step2 = snd_freq[2] * WSG_STEP_SCALE;

    /*
     * The real chip steps its 32-sample waves at 96 kHz; we output at 20 kHz. Sampling the
     * wave once per output sample aliases badly - the high notes come out as a crunchy
     * buzz - so take WSG_OVERSAMPLE points per output sample and average them. That is a
     * box filter at the sub-sample rate, which is enough to take the edge off, and it costs
     * three table lookups per point.
     */
    const uint32_t sub0 = step0 / WSG_OVERSAMPLE;
    const uint32_t sub1 = step1 / WSG_OVERSAMPLE;
    const uint32_t sub2 = step2 / WSG_OVERSAMPLE;
    const int8_t *w0 = snd_wave[0], *w1 = snd_wave[1], *w2 = snd_wave[2];
    const int32_t vol0 = snd_volume[0], vol1 = snd_volume[1], vol2 = snd_volume[2];

    for (uint32_t i = 0; i < samples; i++) {
        int32_t acc = 0;
        for (int k = 0; k < WSG_OVERSAMPLE; k++) {
            // wave index = top 5 bits of the counter
            acc += vol0 * w0[snd_cnt[0] >> 27] + vol1 * w1[snd_cnt[1] >> 27] + vol2 * w2[snd_cnt[2] >> 27];
            snd_cnt[0] += sub0;
            snd_cnt[1] += sub1;
            snd_cnt[2] += sub2;
        }

        // acc/OVERSAMPLE is roughly +/- 360 (3 voices * 15 vol * 8 wave amplitude); scale to 16-bit
        int32_t v = acc * (48 / WSG_OVERSAMPLE);
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        buffer[i] = (int16_t)v;
    }
}
