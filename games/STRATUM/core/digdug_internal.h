/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
#pragma once
#include "digdug.h"
/* shared between the machine, the video and the sound */
extern dd_roms_t dd_roms;
extern uint8_t dd_videoram[0x400];    /* 0x8000-0x83FF: the text layer */
extern uint8_t dd_objram[0x400];      /* 0x8800-0x8BFF: work RAM and sprite codes */
extern uint8_t dd_posram[0x400];      /* 0x9000-0x93FF: work RAM and sprite positions */
extern uint8_t dd_flpram[0x400];      /* 0x9800-0x9BFF: work RAM and sprite flip bits */
extern uint8_t dd_videolatch;         /* 0xA000-0xA007: background select and colour mode */

void dd_video_init(void);
void dd_video_render(uint8_t *fb);

void dd_wsg_init(const uint8_t *prom_wave);
void dd_wsg_reset(void);
void dd_wsg_write(int reg, uint8_t data);
void dd_wsg_render(int16_t *buf, int samples, int sample_rate);
