/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
#pragma once
#include "dkong.h"
extern dk_roms_t dk_roms;
extern uint8_t dk_vram[0x400];      /* 0x7400-0x77FF: the 32x32 character map */
extern uint8_t dk_sprram[0x400];    /* 0x7000-0x73FF: the sprite list, filled by DMA */
extern uint8_t dk_gfxbank, dk_palettebank, dk_spritebank, dk_flip;

void dk_video_init(void);
void dk_sound_init(void);
void dk_sound_reset(void);
void dk_sound_latch_w(uint8_t data);     /* 0x7C00: the command latch */
void dk_sound_sig_w(int bit, int state); /* 0x7D00-0x7D07: the discrete effect lines */
void dk_sound_irq_w(int state);          /* 0x7D80: interrupt the 8035 */
void dk_sound_run(int32_t z80_cycles);   /* advance the 8035 alongside the Z80 */
uint16_t dk_sound_pc(void);
int dk_sound_status(void);       /* the MCU status line the main CPU polls on IN2 bit 6 */
