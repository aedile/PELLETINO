/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
#pragma once
#include "tapper.h"
extern tap_roms_t tap_roms;
extern uint8_t tap_vram[0x800], tap_sprram[0x200], tap_palram[0x80];
extern uint8_t tap_flip;
void tap_video_init(void);
void ctc_trigger(int ch);
