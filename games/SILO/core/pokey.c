/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * pokey.c - POKEY audio (see pokey.h)
 *
 * Model: each channel is a divide-by-N counter clocked from 64 kHz-class
 * (clock/28), 15 kHz-class (clock/114) or the full POKEY clock. Every time a
 * counter expires the channel output is updated from its distortion setting:
 * pure tone toggles, noise samples the 4/5/9/17-bit polynomial counters, which
 * free-run at the POKEY clock. The polynomials are precomputed tables indexed
 * by the master clock count.
 */
#include "pokey.h"
#include <string.h>
#include <stdlib.h>

#define POLY4_LEN  15
#define POLY5_LEN  31
#define POLY9_LEN  511
#define POLY17_LEN 131071

static uint8_t poly4[POLY4_LEN], poly5[POLY5_LEN], poly9[POLY9_LEN];
static uint8_t *poly17_bits;       /* POLY17_LEN bits, packed */
static int polys_ready;

static void gen_poly(uint8_t *out, int len, int bits, int tap)
{
    uint32_t lfsr = 0;
    for (int i = 0; i < len; i++) {
        /* inverted feedback XOR of the two taps so the all-zero state is not a fixed point */
        uint32_t in = ((lfsr >> (bits - 1)) ^ (lfsr >> (tap - 1)) ^ 1) & 1;
        lfsr = ((lfsr << 1) | in) & ((1u << bits) - 1);
        out[i] = (uint8_t)(lfsr & 1);
    }
}

static void gen_polys(void)
{
    if (polys_ready) return;
    gen_poly(poly4, POLY4_LEN, 4, 3);
    gen_poly(poly5, POLY5_LEN, 5, 3);
    gen_poly(poly9, POLY9_LEN, 9, 5);
    poly17_bits = (uint8_t *)calloc(POLY17_LEN / 8 + 1, 1);
    uint32_t lfsr = 0;
    for (int i = 0; i < POLY17_LEN; i++) {
        uint32_t in = ((lfsr >> 16) ^ (lfsr >> 11) ^ 1) & 1;
        lfsr = ((lfsr << 1) | in) & 0x1ffff;
        if (lfsr & 1) poly17_bits[i >> 3] |= (uint8_t)(1 << (i & 7));
    }
    polys_ready = 1;
}

static inline int poly17_at(uint32_t i) { i %= POLY17_LEN; return (poly17_bits[i >> 3] >> (i & 7)) & 1; }

void pokey_init(pokey_t *p)
{
    gen_polys();
    pokey_reset(p);
}

void pokey_reset(pokey_t *p)
{
    memset(p, 0, sizeof(*p));
    p->skctl = 0x03;
}

void pokey_write(pokey_t *p, int reg, uint8_t data)
{
    switch (reg) {
        case 0: case 2: case 4: case 6: p->audf[reg >> 1] = data; break;
        case 1: case 3: case 5: case 7: p->audc[reg >> 1] = data; break;
        case 8: p->audctl = data; break;
        case 9: /* STIMER: restart the dividers */
            for (int i = 0; i < 4; i++) { p->counter[i] = 0; p->out[i] = 0; }
            break;
        case 15: p->skctl = data; break;
        default: break;
    }
}

uint8_t pokey_read(pokey_t *p, int reg, uint32_t clock)
{
    switch (reg & 0x0f) {
        case 0x0a: {                                   /* RANDOM: 8 bits of the 17-bit (or 9-bit) polynomial */
            gen_polys();
            uint8_t v = 0;
            for (int i = 0; i < 8; i++) v = (uint8_t)((v << 1) | ((p->audctl & 0x80) ? poly9[(clock + i) % POLY9_LEN] : poly17_at(clock + i)));
            return (uint8_t)~v;
        }
        case 0x08: return 0xff;                        /* ALLPOT */
        case 0x0e: return 0xff;                        /* IRQST */
        case 0x0f: return 0xff;                        /* SKSTAT */
        default:   return 0x00;                        /* POT0-7: no paddles */
    }
}

/* period of channel ch in POKEY clocks (already accounts for joined 16-bit pairs) */
static inline uint32_t channel_period(const pokey_t *p, int ch, int *silent)
{
    uint8_t ctl = p->audctl;
    uint32_t base = (ctl & 0x01) ? 114 : 28;
    *silent = 0;
    if (ch == 1 && (ctl & 0x10)) {                 /* ch1+ch2 joined, ch2 carries the output */
        uint32_t n = ((uint32_t)p->audf[1] << 8) | p->audf[0];
        return (ctl & 0x40) ? (n + 7) : (n + 1) * base;
    }
    if (ch == 3 && (ctl & 0x08)) {
        uint32_t n = ((uint32_t)p->audf[3] << 8) | p->audf[2];
        return (ctl & 0x20) ? (n + 7) : (n + 1) * base;
    }
    if (ch == 0 && (ctl & 0x10)) { *silent = 1; return 0; }   /* low half of a joined pair */
    if (ch == 2 && (ctl & 0x08)) { *silent = 1; return 0; }
    if (ch == 0 && (ctl & 0x40)) return (uint32_t)p->audf[0] + 4;
    if (ch == 2 && (ctl & 0x20)) return (uint32_t)p->audf[2] + 4;
    return ((uint32_t)p->audf[ch] + 1) * base;
}

static inline void channel_event(pokey_t *p, int ch, uint32_t clock)
{
    uint8_t c = p->audc[ch];
    if (!(c & 0x80)) {                       /* 5-bit poly gates the clock */
        if (!poly5[clock % POLY5_LEN]) return;
    }
    if (c & 0x20) {
        p->out[ch] ^= 1;                     /* pure tone */
    } else if (c & 0x40) {
        p->out[ch] = poly4[clock % POLY4_LEN];
    } else if (p->audctl & 0x80) {
        p->out[ch] = poly9[clock % POLY9_LEN];
    } else {
        p->out[ch] = (uint8_t)poly17_at(clock);
    }
}

void pokey_render(pokey_t *p, int16_t *buf, int samples, int sample_rate)
{
    /* POKEY clocks per output sample, 16.16 */
    uint32_t step = (uint32_t)(((uint64_t)POKEY_CLOCK << 16) / (uint32_t)sample_rate);
    if ((p->skctl & 0x03) == 0) return;      /* initialization mode: silent */
    if (((p->audc[0] | p->audc[1] | p->audc[2] | p->audc[3]) & 0x0f) == 0) {
        /* all volumes zero: keep the clock running, skip the mixing */
        uint64_t adv = (uint64_t)p->master_frac + (uint64_t)step * (uint32_t)samples;
        p->master += (uint32_t)(adv >> 16);
        p->master_frac = (uint32_t)(adv & 0xffff);
        return;
    }

    for (int i = 0; i < samples; i++) {
        p->master_frac += step;
        uint32_t clocks = p->master_frac >> 16;
        p->master_frac &= 0xffff;
        uint32_t t0 = p->master;
        p->master += clocks;

        int v = 0;
        for (int ch = 0; ch < 4; ch++) {
            uint8_t c = p->audc[ch];
            int vol = c & 0x0f;
            if (c & 0x10) { v += vol; continue; }          /* volume-only (DAC) mode */
            int silent;
            uint32_t period = channel_period(p, ch, &silent);
            if (silent || vol == 0) continue;
            if (period == 0) period = 1;
            /* advance this channel's divider through the clocks of this sample */
            int32_t cnt = p->counter[ch] - (int32_t)clocks;
            int guard = 64;
            while (cnt <= 0 && guard--) {
                uint32_t event_clock = t0 + (uint32_t)(p->counter[ch]);   /* approximate event time */
                channel_event(p, ch, event_clock);
                cnt += (int32_t)period;
            }
            if (guard <= 0) cnt = (int32_t)period;
            p->counter[ch] = cnt;
            if (p->out[ch]) v += vol;
        }
        int32_t s = buf[i] + v * 120;         /* 4 chips * 60 max = 28800 */
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        buf[i] = (int16_t)s;
    }
}
