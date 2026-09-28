/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
#pragma once
#include "mrdo.h"
extern md_roms_t md_roms;
extern uint8_t md_bgram[0x800], md_fgram[0x800], md_sprram[0x100];
extern uint8_t md_flip, md_scrollx, md_scrolly;
void md_video_init(void);
