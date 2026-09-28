/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
#pragma once
#include "btime.h"
extern bt_roms_t bt_roms;
extern uint8_t bt_vram[0x400], bt_cram[0x400];
extern uint8_t bt_palram[16];
extern uint8_t bt_scroll[2];
extern uint8_t bt_flip;
void bt_video_init(void);
void bt_sound_init(void);
void bt_sound_reset(void);
void bt_sound_latch_w(uint8_t d);
void bt_sound_run(int32_t cycles, int nmi);
