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

    /*
     * The eight pot inputs. Tempest does not have paddles on them: each one is wired to one
     * bit of an input port through a resistor, so a set bit charges the capacitor immediately
     * and a clear bit takes 228 scanlines. The game writes POTGO, waits, and reads ALLPOT,
     * where a bit still set means that pot has not finished - which is a whole input port
     * read one bit at a time, and the reason the joystick and the spinner come through the
     * sound chips at all.
     */
    uint8_t pot_value[8];    /* 0 = finishes at once, 228 = takes its time */
    uint8_t pot_done;        /* bitmask of pots that have completed since POTGO */
    uint16_t pot_count;      /* scanlines since POTGO */
} pokey_t;

void pokey_init(pokey_t *p);
void pokey_reset(pokey_t *p);
void pokey_write(pokey_t *p, int reg, uint8_t data);
/* register read: RANDOM (0x0A) from the polynomial counter at `clock` POKEY cycles; others read as unused */
uint8_t pokey_read(pokey_t *p, int reg, uint32_t clock);
/* mix `samples` samples into buf (adds to existing content) */
void pokey_render(pokey_t *p, int16_t *buf, int samples, int sample_rate);

/* Set what each pot line will read; call whenever the input it reflects changes. */
void pokey_set_pot(pokey_t *p, int n, uint8_t value);
/* Advance the pot counters by one scanline (about 64 us). */
void pokey_pot_scanline(pokey_t *p);

#ifdef __cplusplus
}
#endif

#endif
