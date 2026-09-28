/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * sn76489.h - Texas Instruments SN76489 programmable sound generator.
 *
 * Three square-wave tones and a noise channel, each with a four-bit attenuator. A write with
 * bit 7 set latches a register (two bits of channel, one of tone-or-volume) and carries the
 * low four bits of data; a write with bit 7 clear carries the high six bits for the tone
 * register last latched. The noise is a 15-bit shift register clocked from a divider or
 * from tone 3, white or periodic. Fixed-point throughout: the ESP32-C6 has no FPU.
 */
#ifndef SN76489_H
#define SN76489_H
#include <stdint.h>

typedef struct {
    uint32_t clock;
    uint16_t tone[3];          /* 10-bit periods */
    uint8_t  vol[4];           /* 0 loud .. 15 silent */
    uint8_t  noise_ctl;        /* bit 2 white, bits 0-1 rate */
    uint8_t  latch;            /* last latched register: channel << 1 | is_volume */
    uint32_t cnt[4];           /* per-channel countdown, in chip clocks << 8 */
    uint8_t  out[3];           /* tone output levels */
    uint16_t lfsr;
    uint8_t  noise_out;
    uint32_t step;             /* chip clocks per output sample, 24.8 */
} sn76489_t;

static const int16_t sn76489_amp[16] = {   /* 2 dB steps, one channel's share of the mix */
    6000, 4766, 3786, 3007, 2389, 1897, 1507, 1197, 951, 755, 600, 477, 379, 301, 239, 0
};

static void sn76489_init(sn76489_t *c, uint32_t clock, int rate)
{
    c->clock = clock;
    for (int i = 0; i < 3; i++) { c->tone[i] = 0; c->out[i] = 1; c->cnt[i] = 0; }
    for (int i = 0; i < 4; i++) c->vol[i] = 15;
    c->noise_ctl = 0; c->latch = 0; c->cnt[3] = 0;
    c->lfsr = 0x4000; c->noise_out = 0;
    /* the chip divides its clock by 16 before the counters see it */
    c->step = (uint32_t)(((uint64_t)clock << 8) / ((uint32_t)rate * 16));
}

static void sn76489_write(sn76489_t *c, uint8_t d)
{
    if (d & 0x80) {
        c->latch = (uint8_t)((d >> 4) & 7);
        int ch = c->latch >> 1;
        if (c->latch & 1)      c->vol[ch] = d & 0x0f;
        else if (ch == 3)    { c->noise_ctl = d & 7; c->lfsr = 0x4000; }
        else                   c->tone[ch] = (uint16_t)((c->tone[ch] & 0x3f0) | (d & 0x0f));
    } else {
        int ch = c->latch >> 1;
        if (c->latch & 1)      c->vol[ch] = d & 0x0f;
        else if (ch == 3)    { c->noise_ctl = d & 7; c->lfsr = 0x4000; }
        else                   c->tone[ch] = (uint16_t)((c->tone[ch] & 0x00f) | ((d & 0x3f) << 4));
    }
}

/* add `samples` of output into buf (the caller mixes and clips) */
static void sn76489_render(sn76489_t *c, int32_t *buf, int samples)
{
    for (int i = 0; i < samples; i++) {
        int32_t v = 0;
        for (int t = 0; t < 3; t++) {
            uint32_t period = (c->tone[t] ? c->tone[t] : 1) << 8;
            c->cnt[t] += c->step;
            if (c->cnt[t] >= period) {                 /* however many half-periods went by */
                uint32_t n = c->cnt[t] / period;
                c->cnt[t] -= n * period;
                c->out[t] ^= (uint8_t)(n & 1);
            }
            /* a period of 1 (or 0) is above audio and comes out as a DC level */
            int on = (c->tone[t] <= 1) ? 1 : c->out[t];
            v += on ? sn76489_amp[c->vol[t]] : -sn76489_amp[c->vol[t]];
        }
        /* noise: clocked by a divider or by tone 3, on every other edge */
        uint32_t nper;
        switch (c->noise_ctl & 3) {
            case 0: nper = 0x10; break;
            case 1: nper = 0x20; break;
            case 2: nper = 0x40; break;
            default: nper = c->tone[2] ? c->tone[2] : 1; break;
        }
        nper <<= 9;
        c->cnt[3] += c->step;
        if (c->cnt[3] >= nper) {
            uint32_t n = c->cnt[3] / nper;
            c->cnt[3] -= n * nper;
            for (uint32_t k = 0; k < n && k < 8; k++) {
                int fb = (c->noise_ctl & 4) ? ((c->lfsr ^ (c->lfsr >> 1)) & 1) : (c->lfsr & 1);
                c->noise_out = (uint8_t)(c->lfsr & 1);
                c->lfsr = (uint16_t)((c->lfsr >> 1) | (fb << 14));
            }
        }
        v += c->noise_out ? sn76489_amp[c->vol[3]] : -sn76489_amp[c->vol[3]];
        buf[i] += v;
    }
}
#endif
