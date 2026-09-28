/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * galaga_sound.c - Namco 3-voice WSG (as in Pac-Man/Galaga) and a high-level
 * model of the 54XX noise generator (explosions).
 */
#include "galaga_internal.h"
#include <string.h>
#include <stdlib.h>

/* ---- WSG: registers 0x00-0x1F as written to 0x6800-0x681F ---- */
static uint8_t regs[32];
static const uint8_t *wave_prom;          /* 8 waves x 32 samples, low nibble */
static uint32_t cnt[3];                   /* 32-bit phase; top 5 bits index the wave */
#define WSG_CLOCK 96000                   /* 3.072 MHz / 32 */

void ga_wsg_init(const uint8_t *prom) { wave_prom = prom; ga_wsg_reset(); }
void ga_wsg_reset(void) { memset(regs, 0, sizeof(regs)); memset(cnt, 0, sizeof(cnt)); }
void ga_wsg_write(int reg, uint8_t d) { regs[reg & 0x1f] = d & 0x0f; }

void ga_wsg_render(int16_t *buf, int samples, int sample_rate)
{
    uint32_t scale = (uint32_t)(((uint64_t)WSG_CLOCK * 4096 + sample_rate / 2) / sample_rate);
    uint32_t freq[3], step[3];
    int vol[3];
    const uint8_t *wave[3];
    for (int ch = 0; ch < 3; ch++) {
        vol[ch] = regs[ch * 5 + 0x15] & 0x0f;
        uint32_t f = (ch == 0) ? (regs[0x10] & 0x0f) : 0;
        f |= (uint32_t)(regs[ch * 5 + 0x11] & 0x0f) << 4;
        f |= (uint32_t)(regs[ch * 5 + 0x12] & 0x0f) << 8;
        f |= (uint32_t)(regs[ch * 5 + 0x13] & 0x0f) << 12;
        f |= (uint32_t)(regs[ch * 5 + 0x14] & 0x0f) << 16;
        freq[ch] = f;
        step[ch] = f * scale;
        wave[ch] = wave_prom + (regs[ch * 5 + 0x05] & 0x07) * 32;
    }
    for (int i = 0; i < samples; i++) {
        int32_t v = 0;
        for (int ch = 0; ch < 3; ch++) {
            if (vol[ch] && freq[ch]) {
                v += vol[ch] * ((int)(wave[ch][cnt[ch] >> 27] & 0x0f) - 8);
                cnt[ch] += step[ch];
            }
        }
        int32_t s = buf[i] + v * 40;      /* 3 voices x 15 x 8 = 360 max -> 14400 */
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        buf[i] = (int16_t)s;
    }
}

/* ---- 54XX high-level model ----
 * Commands: 1x play A, 2x play B, 3x set A params (+4 bytes), 4x set B params (+4),
 * 5x play C, 6x set C params (+5), 7x volume C. The real chip drives three 4-bit
 * DACs into RC filters; here each sound type is a filtered noise burst with an
 * envelope, tuned by ear to the explosion sounds in Galaga. */
typedef struct {
    int active;
    float env, decay;       /* envelope per sample */
    float lp, alpha;        /* one-pole low-pass state and coefficient */
    float gain;
} noise_t;
static noise_t nz[3];
static int param_left;
static uint32_t rng = 0x12345678;

void ga_n54_init(void) { ga_n54_reset(); }
void ga_n54_reset(void) { memset(nz, 0, sizeof(nz)); param_left = 0; }

static void trigger(int which, float decay_sec, float cutoff_hz, float gain, int sample_rate)
{
    nz[which].active = 1;
    nz[which].env = 1.0f;
    nz[which].decay = 1.0f - 1.0f / (decay_sec * sample_rate);
    nz[which].alpha = 1.0f - (float)(1.0 / (1.0 + sample_rate / (6.2832 * cutoff_hz)));
    nz[which].gain = gain;
}

static int pending_trigger[3];
void ga_n54_write(uint8_t d)
{
    if (param_left) { param_left--; return; }
    switch (d >> 4) {
        case 1: pending_trigger[0] = 1; break;
        case 2: pending_trigger[1] = 1; break;
        case 3: param_left = 4; break;
        case 4: param_left = 4; break;
        case 5: pending_trigger[2] = 1; break;
        case 6: param_left = 5; break;
        case 7: nz[2].gain = (d & 0x0f) / 15.0f; break;
        default: break;
    }
}

void ga_n54_render(int16_t *buf, int samples, int sample_rate)
{
    if (pending_trigger[0]) { trigger(0, 0.60f, 900.0f, 0.9f, sample_rate); pending_trigger[0] = 0; }
    if (pending_trigger[1]) { trigger(1, 0.25f, 2500.0f, 0.6f, sample_rate); pending_trigger[1] = 0; }
    if (pending_trigger[2]) { trigger(2, 1.0f, 1500.0f, nz[2].gain > 0 ? nz[2].gain : 0.5f, sample_rate); pending_trigger[2] = 0; }
    if (!nz[0].active && !nz[1].active && !nz[2].active) return;
    for (int i = 0; i < samples; i++) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        float white = ((int32_t)(rng & 0xffff) - 32768) / 32768.0f;
        float v = 0;
        for (int k = 0; k < 3; k++) {
            noise_t *n = &nz[k];
            if (!n->active) continue;
            n->lp += n->alpha * (white - n->lp);
            v += n->lp * n->env * n->gain;
            n->env *= n->decay;
            if (n->env < 0.002f) n->active = 0;
        }
        int32_t s = buf[i] + (int32_t)(v * 14000.0f);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        buf[i] = (int16_t)s;
    }
}
