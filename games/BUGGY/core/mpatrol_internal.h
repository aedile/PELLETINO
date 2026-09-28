/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
#pragma once
#include "mpatrol.h"
extern mp_roms_t mp_roms;
extern uint8_t mp_vram[0x400], mp_cram[0x400], mp_sprram[0x100];
extern uint8_t mp_scroll, mp_bgx[2], mp_bgy[2], mp_bgctl;
void mp_video_init(void);
void mp_sound_init(void);
void mp_sound_reset(void);
void mp_sound_cmd(uint8_t d);
void mp_sound_run(int32_t cycles);
