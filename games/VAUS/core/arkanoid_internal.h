/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
#pragma once
#include "arkanoid.h"
extern ak_roms_t ak_roms;
extern uint8_t ak_vram[0x800];      /* 0xE000-0xE7FF: two bytes a tile */
extern uint8_t ak_sprram[0x40];     /* 0xE800-0xE83F */
extern uint8_t ak_gfxbank, ak_palbank, ak_flipx, ak_flipy;
void ak_video_init(void);
void ak_video_render(uint8_t *fb);
