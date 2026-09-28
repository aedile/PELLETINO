/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
#pragma once
#include "timeplt.h"
extern tp_roms_t tp_roms;
extern uint8_t tp_vram[0x400], tp_cram[0x400];   /* 0xA400 codes, 0xA000 attributes */
extern uint8_t tp_spr0[0x100], tp_spr1[0x100];   /* 0xB000: x and code; 0xB400: attributes and y */
extern uint8_t tp_flip, tp_video_enable;
void tp_video_init(void);
