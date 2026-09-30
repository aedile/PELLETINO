/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro */
/*
 * ym2151.h - Yamaha YM2151 (OPM): eight channels of four-operator FM, and two timers.
 */
#ifndef YM2151_H
#define YM2151_H
#include <stdint.h>

#define YM2151_NATIVE_RATE 55930     /* clock / 64 at 3.579545 MHz */

void ym2151_init(int clock, int rate);           /* the chip's clock, and the rate to synthesise at */
void ym2151_reset(void);
void ym2151_write(int port, uint8_t v);           /* port 0 selects a register, port 1 writes it */
uint8_t ym2151_status(void);

/* the timers run on the chip's clock, not on the sound: tell it how much has passed */
void ym2151_advance(int clocks);
int ym2151_irq(void);                             /* the level of the IRQ pin */
int ym2151_clocks_to_overflow(void);              /* how long until a timer next overflows */

/* adds the chip's output, both sides summed, to mix[] */
void ym2151_render(int32_t *mix, int samples);

#endif
