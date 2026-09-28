/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * timeplt.h - Konami Time Pilot (1982) board emulation
 *
 * A Z80 at 3.072 MHz for the game and, on the Pooyan sound board, a second Z80 at 1.79 MHz
 * with two AY-3-8910s. A 32x32 character map, 24 sprites of 16x16, a 32-entry palette from two
 * PROMs, and lookup PROMs for the characters' and sprites' colours. The monitor stands on its
 * side, so the picture is taller than it is wide.
 *
 * Timing, memory map and video follow MAME's konami/timeplt.cpp and shared/timeplt_a.cpp.
 */
#ifndef TIMEPLT_H
#define TIMEPLT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define TP_MAIN_CLOCK   3072000                 /* 18.432 MHz / 6 */
#define TP_SND_CLOCK    1789772                 /* 14.318 MHz / 8, for the Z80 and both AYs */
#define TP_MAIN_CYCLES_PER_FRAME 51200          /* 60 Hz */
#define TP_SND_CYCLES_PER_FRAME  29830

/* the frame as displayed: the hardware's 256x224 turned on its side */
#define TP_FB_W 224
#define TP_FB_H 256
#define TP_PALETTE_SIZE 32

typedef struct {
    const uint8_t *rom;        /* 24 KB program */
    const uint8_t *snd;        /* 4 KB sound program */
    const uint8_t *chr;        /* 8 KB characters */
    const uint8_t *spr;        /* 16 KB sprites */
    const uint8_t *prom;       /* 0x240 bytes: two palette PROMs, sprite lookup, character lookup */
} tp_roms_t;

typedef struct {
    uint8_t up, down, left, right, fire;
    uint8_t start1, start2, coin1;
} tp_input_t;

void tp_init(const tp_roms_t *roms);
void tp_reset(void);
void tp_set_dips(uint8_t dsw0, uint8_t dsw1);
tp_input_t *tp_input(void);

void tp_run_frame(void);
void tp_video_init(void);
void tp_render(uint8_t *fb);                     /* TP_FB_W * TP_FB_H palette indices */
void tp_palette(uint16_t out[TP_PALETTE_SIZE]);  /* RGB565 */
void tp_render_audio(int16_t *buf, int samples, int rate);

uint16_t tp_pc(void);
uint16_t tp_snd_pc(void);
uint32_t tp_frame_count(void);

#ifdef __cplusplus
}
#endif
#endif
