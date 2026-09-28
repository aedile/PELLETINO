/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
#pragma once
#include "galaxian.h"
/* shared between the machine and the video, which are separate translation units so the
 * video can be tuned without rebuilding the CPU side */
extern gx_roms_t gx_roms;
extern uint8_t gx_vram[0x400];
extern uint8_t gx_oram[0x100];
extern uint8_t gx_flip_x, gx_flip_y, gx_stars_on;
extern uint32_t gx_stars_scroll;
extern uint32_t gx_frame_no;
