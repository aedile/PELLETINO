/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
#pragma once
#include "frogger.h"
/* shared between the machine and the video, which are separate translation units so the
 * video can be tuned without rebuilding the CPU side */
extern fr_roms_t fr_roms;
extern uint8_t fr_vram[0x400];
extern uint8_t fr_oram[0x100];
extern uint8_t fr_flip_x, fr_flip_y;
