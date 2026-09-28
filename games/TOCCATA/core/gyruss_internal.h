/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
#pragma once
#include "gyruss.h"

extern gy_roms_t gy_roms;
extern uint8_t gy_colorram[0x400];   /* 0x8000-0x83FF */
extern uint8_t gy_videoram[0x400];   /* 0x8400-0x87FF */
extern uint8_t gy_spriteram[0x100];  /* the 6809's 0x4040-0x40FF, mirrored down to 0 */
extern uint8_t gy_flipscreen;

void gy_video_init(void);
void gy_video_render(uint8_t *fb);
