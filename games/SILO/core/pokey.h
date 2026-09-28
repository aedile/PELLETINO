/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * pokey.h - Atari POKEY sound generator (4 channels) - audio registers only.
 * Own implementation, sample-based, in the spirit of Ron Fries' pokeysnd.
 */
#ifndef POKEY_H
#define POKEY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POKEY_CLOCK 1512000   /* Star Wars: 12.096 MHz / 8 */

typedef struct {
    uint8_t audf[4], audc[4], audctl, skctl;
    /* per-channel dividers, in POKEY clocks (16.16 fixed point) */
    int32_t counter[4];
    uint8_t out[4];          /* current output bit */
    uint32_t master;         /* POKEY clock count, for the polynomial counters */
    uint32_t master_frac;    /* 16.16 fractional accumulator */
} pokey_t;

void pokey_init(pokey_t *p);
void pokey_reset(pokey_t *p);
void pokey_write(pokey_t *p, int reg, uint8_t data);
/* register read: RANDOM (0x0A) from the polynomial counter at `clock` POKEY cycles; others read as unused */
uint8_t pokey_read(pokey_t *p, int reg, uint32_t clock);
/* mix `samples` samples into buf (adds to existing content) */
void pokey_render(pokey_t *p, int16_t *buf, int samples, int sample_rate);

#ifdef __cplusplus
}
#endif

#endif
