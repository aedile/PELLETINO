/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * galaxian_sound.c - the Galaxian sound board, approximated.
 *
 * The board is discrete, and what the program drives it with says what it is:
 *
 *   0x6800-0x6802  FS1-FS3  three 555 timers - the background hum, a low chord. Their control
 *                           voltage follows an LFO, so the chord wobbles.
 *   0x6004-0x6007  LFO      four capacitor-select lines for that LFO: more selected, slower.
 *                           The program leaves all four on while idle and drops them as the
 *                           formation thins, which is why the hum quickens as a wave goes on.
 *   0x7800         PITCH    preload for a counter chain clocked at 115.2 kHz whose output
 *                           steps a 16-step "toothsaw" wave. That is the dive whistle: the
 *                           program sweeps the preload a frame at a time. 0xFF is silence.
 *   0x6806-0x6807  VOL1/2   shape and loudness of the toothsaw.
 *   0x6805         FIRE     the shot, a tone that falls away.
 *   0x6803         HIT      a noise burst.
 *
 * MAME simulates the circuits; there is no room for that here, so each is synthesised to the
 * same shape. Everything is fixed-point: the ESP32-C6 has no FPU.
 */
#include "galaxian.h"
#include <string.h>

static int      rate = 20050;
static uint8_t  bg_en;             /* FS1-FS3 */
static uint8_t  noise_en, fire_en, vol_bits, lfo_bits;
static uint8_t  pitch = 0xff;
static uint32_t bg_ph[3], bg_base[3];
static uint32_t lfo_ph, lfo_inc;
static uint32_t tone_ph, tone_inc;
static uint32_t fire_ph, fire_inc, fire_rem;
static int32_t  fire_amp;
static int32_t  lp_noise, lp_bg, noise_amp;
static uint32_t lfsr = 0x2545F491;
uint32_t gx_snd_writes[0x21][2];   /* diagnostics: writes per register, by value bit 0 */
uint32_t gx_snd_pitch_hist[256];

static uint32_t inc_for_c(uint32_t chz)   /* centi-hertz to a 32-bit phase increment */
{
    return (uint32_t)(((uint64_t)chz << 32) / ((uint32_t)rate * 100));
}

static void retune(void)
{
    /* the three 555s: a low chord */
    bg_base[0] = inc_for_c(11000);
    bg_base[1] = inc_for_c(13800);
    bg_base[2] = inc_for_c(16500);
    /* the LFO: all four capacitors in is about 1 Hz, none is about 4 Hz */
    uint32_t chz = 400 - 20 * (uint32_t)(lfo_bits & 0x0f);
    if (chz < 60) chz = 60;
    lfo_inc = inc_for_c(chz);
    /* the toothsaw: 115.2 kHz / (256 - pitch) per period */
    tone_inc = (pitch == 0xff) ? 0 : inc_for_c(11520000u / (uint32_t)(256 - pitch));
}

void gx_sound_reset(void)
{
    bg_en = noise_en = fire_en = vol_bits = lfo_bits = 0;
    pitch = 0xff;
    memset(bg_ph, 0, sizeof bg_ph);
    lfo_ph = tone_ph = 0;
    fire_ph = fire_inc = fire_rem = 0; fire_amp = 0;
    lp_noise = lp_bg = noise_amp = 0;
    retune();
}

void gx_sound_write(int reg, uint8_t v)
{
    if (reg >= 0 && reg <= 0x20) gx_snd_writes[reg][v & 1]++;
    if (reg == 0x20) gx_snd_pitch_hist[v]++;
    switch (reg) {
        case 0: case 1: case 2:
            bg_en = (uint8_t)((bg_en & ~(1u << reg)) | ((v & 1) << reg));
            break;
        case 3:
            if ((v & 1) && !noise_en) noise_amp = 32767;   /* a hit: the burst starts full */
            noise_en = v & 1;
            break;
        case 5:
            if ((v & 1) && !fire_en) {               /* the shot starts on the rising edge */
                fire_rem = (uint32_t)rate * 30 / 100;
                fire_amp = 32767;
                fire_inc = inc_for_c(160000);        /* 1600 Hz, sweeping down */
            }
            fire_en = v & 1;
            break;
        case 6: case 7:
            vol_bits = (uint8_t)((vol_bits & ~(1u << (reg - 6))) | ((v & 1) << (reg - 6)));
            break;
        case 0x10: case 0x11: case 0x12: case 0x13:
            lfo_bits = (uint8_t)((lfo_bits & ~(1u << (reg - 0x10))) | ((v & 1) << (reg - 0x10)));
            retune();
            break;
        case 0x20:
            pitch = v;
            retune();
            break;
        default: break;
    }
}

static inline int32_t sq(uint32_t ph) { return (ph & 0x80000000u) ? 32767 : -32767; }
static inline int32_t tri(uint32_t ph)
{
    int32_t v = (int32_t)(ph >> 16);
    v = v < 32768 ? v : 65535 - v;
    return (v - 16384) * 2;
}
/* the toothsaw: sixteen steps up, then back to the bottom */
static inline int32_t saw16(uint32_t ph) { return (int32_t)(ph >> 28) * 4369 - 32768; }

void gx_render_audio(int16_t *buf, int samples, int r)
{
    if (r != rate) { rate = r; retune(); }
    const int32_t tone_gain = 1 + vol_bits;          /* 1..4, over 16 */
    for (int i = 0; i < samples; i++) {
        int32_t mix = 0;

        lfsr ^= lfsr << 13; lfsr ^= lfsr >> 17; lfsr ^= lfsr << 5;
        int32_t n = (int32_t)(lfsr >> 16) - 32768;

        /* the LFO runs whether or not anything listens to it */
        lfo_ph += lfo_inc;
        int32_t l = tri(lfo_ph);                     /* -32768..32767 */

        /* background hum: the 555s, each pulled up to half again as fast by the LFO */
        if (bg_en) {
            int32_t v = 0;
            for (int k = 0; k < 3; k++) {
                if (bg_en & (1u << k)) v += sq(bg_ph[k]) / 3;
                bg_ph[k] += bg_base[k] + (uint32_t)(((int64_t)(bg_base[k] / 4) * l) >> 15) + bg_base[k] / 4;
            }
            lp_bg += (v - lp_bg) >> 3;
            mix += lp_bg / 3;
        }
        /* the dive whistle */
        if (tone_inc) {
            tone_ph += tone_inc;
            mix += (saw16(tone_ph) * tone_gain) >> 4;
        }
        /* the shot: a tone falling away */
        if (fire_rem) {
            fire_ph += fire_inc;
            fire_inc -= fire_inc >> 9;
            mix += (sq(fire_ph) * fire_amp) >> 17;
            fire_amp -= fire_amp / (int32_t)fire_rem;
            fire_rem--;
        }
        /* a hit: noise with the top taken off, dying away while the line is held */
        if (noise_en && noise_amp > 0) {
            lp_noise += (n - lp_noise) >> 2;
            mix += (lp_noise * noise_amp) >> 16;
            noise_amp -= noise_amp >> 12;
        }

        if (mix > 32767) mix = 32767; else if (mix < -32768) mix = -32768;
        buf[i] = (int16_t)mix;
    }
}
