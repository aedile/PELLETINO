/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * cps1_internal.h - what the machine, the video and the sound share.
 */
#ifndef CPS1_INTERNAL_H
#define CPS1_INTERNAL_H

#include "cps1.h"

/* CPS-A registers, as word indexes from 800100 */
enum {
    CPS_A_OBJ_BASE = 0x00 / 2,
    CPS_A_SCROLL1_BASE = 0x02 / 2,
    CPS_A_SCROLL2_BASE = 0x04 / 2,
    CPS_A_SCROLL3_BASE = 0x06 / 2,
    CPS_A_OTHER_BASE = 0x08 / 2,
    CPS_A_PALETTE_BASE = 0x0a / 2,
    CPS_A_SCROLL1_X = 0x0c / 2,
    CPS_A_SCROLL1_Y = 0x0e / 2,
    CPS_A_SCROLL2_X = 0x10 / 2,
    CPS_A_SCROLL2_Y = 0x12 / 2,
    CPS_A_SCROLL3_X = 0x14 / 2,
    CPS_A_SCROLL3_Y = 0x16 / 2,
    CPS_A_ROWSCROLL_OFFS = 0x20 / 2,
    CPS_A_VIDEOCONTROL = 0x22 / 2,
};

typedef struct {
    cps1_roms_t roms;
    cps1_blob_t cfg;
    uint16_t a_regs[0x20];
    uint16_t b_regs[0x20];
    uint8_t dsw[3];
    uint8_t latch[2];            /* sound command, fade timer */
} cps1_state_t;

extern cps1_state_t cps1;

void cps1_video_init(void);
void cps1_palette_latch(void);
void cps1_objram_latch(void);

/* finds the game's idle loop and arranges for it to be skipped (cps1_idle.c) */
void cps1_idle_init(const uint16_t *prog);
extern unsigned cps1_slice;            /* counts the 68000's time slices */

void cps1_sound_reset(void);
/* bring the sound CPU up to the 68000, given how many 68000 cycles into the frame it is */
void cps1_sound_run_to(int32_t m68k_cycles);
void cps1_sound_end_frame(int32_t m68k_cycles);

#endif
