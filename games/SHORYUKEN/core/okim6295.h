/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro */
/*
 * okim6295.h - OKI MSM6295: four voices of 4-bit ADPCM out of a ROM.
 */
#ifndef OKIM6295_H
#define OKIM6295_H
#include <stdint.h>

/* rom, or if it is NULL, read: a function that fetches bytes of it */
void okim6295_init(const uint8_t *rom, void (*read)(uint32_t offset, uint8_t *dst, uint32_t len),
                   uint32_t rom_size, int clock, int rate);
void okim6295_reset(void);
void okim6295_write(uint8_t v);
uint8_t okim6295_read(void);
void okim6295_set_pin7(int high);                 /* high: clock / 132 samples a second; low: clock / 165 */
/* adds the chip's output, at 16 bits, to mix[] */
void okim6295_render(int32_t *mix, int samples);

#endif
