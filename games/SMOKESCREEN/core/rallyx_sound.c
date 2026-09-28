/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * rallyx_sound.c - the Namco 3-voice WSG (the same part Pac-Man and Galaga use, written
 * through the same pacman_sound_w register layout) and the discrete BANG circuit.
 */
#include "rallyx_internal.h"
#include <string.h>
#include <stdlib.h>

/* ---- WSG: registers 0x00-0x1F, written here at 0xA100-0xA11F ---- */
static uint8_t regs[32];
static const uint8_t *wave_prom;          /* 8 waves x 32 samples, low nibble */
static uint32_t cnt[3];                   /* 32-bit phase; top 5 bits index the wave */
#define WSG_CLOCK 96000                   /* 3.072 MHz / 32 */

void rx_wsg_init(const uint8_t *prom) { wave_prom = prom; rx_wsg_reset(); }
void rx_wsg_reset(void) { memset(regs, 0, sizeof(regs)); memset(cnt, 0, sizeof(cnt)); }
void rx_wsg_write(int reg, uint8_t d) { regs[reg & 0x1f] = d & 0x0f; }

void rx_wsg_render(int16_t *buf, int samples, int sample_rate)
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


/* ---- BANG ----
 * The explosion on this board is not the WSG at all: bit 0 of the main latch gates a discrete
 * noise circuit, mixed in after the WSG and ahead of the power amp. This is a filtered noise
 * burst with an envelope, which is what that circuit sounds like.
 */
static float bang_env, bang_lp;
static uint32_t bang_rng = 1;

void rx_bang(int state)
{
    if (state) bang_env = 1.0f;               /* retriggered on the rising edge */
}

void rx_bang_render(int16_t *buf, int samples, int sample_rate)
{
    if (bang_env <= 0.001f) return;
    const float decay = 3.2f / (float)sample_rate;
    for (int i = 0; i < samples; i++) {
        uint32_t bit = ((bang_rng >> 0) ^ (bang_rng >> 3)) & 1;
        bang_rng = (bang_rng >> 1) | (bit << 16);
        float n = (bang_rng & 1) ? 1.0f : -1.0f;
        bang_lp += (n - bang_lp) * 0.06f;
        int32_t s = buf[i] + (int32_t)(bang_lp * bang_env * 7000.0f);
        buf[i] = (int16_t)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
        bang_env -= bang_env * decay;
        if (bang_env <= 0.001f) { bang_env = 0.0f; break; }
    }
}
