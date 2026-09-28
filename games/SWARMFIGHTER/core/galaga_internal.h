/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
#ifndef GALAGA_INTERNAL_H
#define GALAGA_INTERNAL_H
#include <stdint.h>
#include "galaga.h"

extern uint8_t ga_ram1[0x400], ga_ram2[0x400], ga_ram3[0x400], ga_videoram[0x800];
extern uint8_t ga_videolatch;

void ga_video_init(const ga_roms_t *roms);
void ga_video_reset(void);
void ga_video_vblank(void);
void ga_video_render(uint8_t *fb);

void ga_wsg_init(const uint8_t *prom_wave);
void ga_wsg_reset(void);
void ga_wsg_write(int reg, uint8_t data);
void ga_wsg_render(int16_t *buf, int samples, int sample_rate);

void ga_n54_init(void);
void ga_n54_reset(void);
void ga_n54_write(uint8_t d);
void ga_n54_render(int16_t *buf, int samples, int sample_rate);

#endif
