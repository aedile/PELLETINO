/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
#pragma once
#include "mario.h"
extern mb_roms_t mb_roms;
extern uint8_t mb_vram[0x400];      /* 0x7400-0x77FF: the 32x32 character map */
extern uint8_t mb_sprram[0x400];    /* 0x7000-0x73FF: the sprite list */
extern uint8_t mb_gfxbank, mb_palettebank, mb_flip, mb_scroll;

void mb_video_init(void);
void mb_dma_write(uint8_t v);            /* a byte to the Z80 DMA's register port */
void mb_dma_go(void);                    /* DMA SET on the main latch: move the sprite list */
void mb_sound_init(void);
void mb_sound_reset(void);
void mb_sound_latch_w(uint8_t data);     /* 0x7E00: the tune */
void mb_sound_sig_w(int bit, int state); /* 0x7F00-0x7F07: interrupt, test pins, port 1 bits, skid */
void mb_sound_run_w(int who, uint8_t d); /* 0x7C00 / 0x7C80: a footstep */
void mb_sound_run(int32_t z80_cycles);   /* advance the 8039 alongside the Z80 */
uint16_t mb_sound_pc(void);
