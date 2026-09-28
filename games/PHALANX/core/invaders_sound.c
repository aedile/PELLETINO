/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * invaders_sound.c - the Space Invaders sound board, approximated.
 *
 * The board is analogue: an SN76477 for the UFO's warble and a handful of discrete
 * oscillators and noise generators for everything else, each triggered by one bit of OUT 3
 * or OUT 5. MAME plays recordings of them. There are no recordings here, so each sound is
 * synthesised to the same shape - a warbling tone, a burst of noise that dies away, the four
 * descending thumps of the fleet. Everything is fixed-point: the ESP32-C6 has no FPU, and
 * this runs for every one of the 20050 samples a second.
 */
#include "invaders.h"
#include <string.h>

typedef struct {
    uint32_t ph, inc;          /* phase accumulator, full cycle = 2^32 */
    int32_t  amp, dec;         /* Q15 envelope and its per-sample decay */
    uint32_t rem;              /* samples left */
} voice_t;

static voice_t v_shot, v_flash, v_invdie, v_extra, v_fleet, v_ufohit;
static int      ufo_on;
static uint32_t ufo_ph, ufo_lfo, ufo_base, ufo_span, ufo_lfo_inc;
static int32_t  lp_shot, lp_flash, lp_hit, lp_fleet;
static uint32_t lfsr = 0x2545F491;
static uint8_t  last3, last5;
static int      rate = 20050;      /* until the first render says otherwise */

static uint32_t inc_for(uint32_t hz)   { return (uint32_t)(((uint64_t)hz << 32) / (uint32_t)rate); }
static uint32_t inc_for_c(uint32_t chz){ return (uint32_t)(((uint64_t)chz << 32) / ((uint32_t)rate * 100)); }

static void trigger(voice_t *v, uint32_t hz, uint32_t ms)
{
    v->ph = 0;
    v->inc = inc_for(hz);
    v->rem = (uint32_t)rate * ms / 1000;
    if (v->rem == 0) v->rem = 1;
    v->amp = 32767;
    v->dec = 32767 / (int32_t)v->rem;
    if (v->dec == 0) v->dec = 1;
}

static void retune(void)
{
    ufo_base    = inc_for(500);
    ufo_span    = inc_for(150);
    ufo_lfo_inc = inc_for_c(300);                    /* 3.00 Hz warble */
}

void si_sound_reset(void)
{
    memset(&v_shot, 0, sizeof v_shot);   memset(&v_flash, 0, sizeof v_flash);
    memset(&v_invdie, 0, sizeof v_invdie); memset(&v_extra, 0, sizeof v_extra);
    memset(&v_fleet, 0, sizeof v_fleet); memset(&v_ufohit, 0, sizeof v_ufohit);
    ufo_on = 0; ufo_ph = ufo_lfo = 0;
    lp_shot = lp_flash = lp_hit = lp_fleet = 0;
    last3 = last5 = 0;
    retune();
}

void si_sound_write(int port, uint8_t val)
{
    if (port == 3) {
        uint8_t rise = (uint8_t)(val & ~last3);
        ufo_on = val & 0x01;                         /* held for as long as the saucer flies */
        if (rise & 0x02) trigger(&v_shot,   0,  280);   /* the player's shot */
        if (rise & 0x04) trigger(&v_flash,  0, 1000);   /* the player blowing up */
        if (rise & 0x08) trigger(&v_invdie, 0,  140);   /* an invader hit */
        if (rise & 0x10) trigger(&v_extra, 880,  600);  /* extra ship */
        last3 = val;
    } else if (port == 5) {
        uint8_t rise = (uint8_t)(val & ~last5);
        static const uint16_t fleet_hz[4] = { 87, 78, 69, 62 };
        for (int i = 0; i < 4; i++)
            if (rise & (1u << i)) trigger(&v_fleet, fleet_hz[i], 110);
        if (rise & 0x10) trigger(&v_ufohit, 0, 700);    /* saucer hit */
        last5 = val;
    }
}

static inline int32_t tri(uint32_t ph)
{
    int32_t v = (int32_t)(ph >> 16);                 /* 0..65535 */
    v = v < 32768 ? v : 65535 - v;                   /* 0..32767..0 */
    return (v - 16384) * 2;                          /* -32768..32767 */
}
static inline int32_t sq(uint32_t ph) { return (ph & 0x80000000u) ? 32767 : -32767; }
static inline void step(voice_t *v)
{
    v->amp -= v->dec; if (v->amp < 0) v->amp = 0;
    if (v->rem) v->rem--;
}

void si_render_audio(int16_t *buf, int samples, int r)
{
    if (r != rate) { rate = r; retune(); }
    for (int i = 0; i < samples; i++) {
        int32_t mix = 0;

        /* white noise, shared by everything that hisses */
        lfsr ^= lfsr << 13; lfsr ^= lfsr >> 17; lfsr ^= lfsr << 5;
        int32_t n = (int32_t)(lfsr >> 16) - 32768;

        if (ufo_on) {
            ufo_lfo += ufo_lfo_inc;
            int32_t l = tri(ufo_lfo);
            ufo_ph += ufo_base + (uint32_t)(((int64_t)ufo_span * l) >> 15);
            mix += tri(ufo_ph) * 3 / 8;
        }
        if (v_shot.rem) {                            /* bright, quick */
            lp_shot += (n - lp_shot) >> 1;
            mix += ((lp_shot * v_shot.amp) >> 15) * 3 / 8;
            step(&v_shot);
        }
        if (v_flash.rem) {                           /* dull, long */
            lp_flash += (n - lp_flash) >> 4;
            mix += ((lp_flash * v_flash.amp) >> 15) * 5 / 8;
            step(&v_flash);
        }
        if (v_invdie.rem) {                          /* a pop */
            mix += ((n * v_invdie.amp) >> 15) / 3;
            step(&v_invdie);
        }
        if (v_extra.rem) {                           /* two alternating notes */
            uint32_t inc = ((v_extra.rem / (uint32_t)(rate / 10)) & 1) ? v_extra.inc : v_extra.inc * 3 / 4;
            v_extra.ph += inc;
            mix += ((sq(v_extra.ph) * v_extra.amp) >> 15) / 4;
            step(&v_extra);
        }
        if (v_fleet.rem) {                           /* the march: a low thump */
            v_fleet.ph += v_fleet.inc;
            lp_fleet += (sq(v_fleet.ph) - lp_fleet) >> 3;
            mix += ((lp_fleet * v_fleet.amp) >> 15) / 2;
            step(&v_fleet);
        }
        if (v_ufohit.rem) {                          /* a longer, rougher burst */
            lp_hit += (n - lp_hit) >> 2;
            mix += ((lp_hit * v_ufohit.amp) >> 15) / 2;
            step(&v_ufohit);
        }

        if (mix > 32767) mix = 32767; else if (mix < -32768) mix = -32768;
        buf[i] = (int16_t)mix;
    }
}
