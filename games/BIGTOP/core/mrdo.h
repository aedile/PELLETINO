/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * mrdo.h - Universal Mr. Do! (1982) board emulation
 *
 * A Z80 at 4.1 MHz, 32 KB of program, two 32x32 character maps (one scrolling behind the
 * other), sixteen-colour 16x16 sprites, a palette from two PROMs through a resistor ladder,
 * and two SN76489 sound chips. A protection PAL guards the program; MAME answers its reads
 * from the program ROM at HL and so does this.
 *
 * Timing, memory map and video follow MAME's universal/mrdo.cpp and mrdo_v.cpp.
 */
#ifndef MRDO_H
#define MRDO_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define MD_CPU_CLOCK     4100000                        /* 8.2 MHz / 2 */
/* 19.6 MHz / 4 pixel clock over 312 x 262 gives 59.94 Hz */
#define MD_CYCLES_PER_FRAME 68400

/* the picture as the hardware makes it, before the cabinet turns the monitor */
#define MD_FB_W 240
#define MD_FB_H 192
#define MD_PALETTE_SIZE 256

typedef struct {
    const uint8_t *rom;        /* 32 KB program */
    const uint8_t *fg;         /* 8 KB foreground characters: s8-09 then u8-10 */
    const uint8_t *bg;         /* 8 KB background characters: r8-08 then n8-07 */
    const uint8_t *spr;        /* 8 KB sprites: h5-05 then k5-06 */
    const uint8_t *prom;       /* 128 bytes: palette high, palette low, sprite lookup, timing */
} md_roms_t;

typedef struct {
    uint8_t up, down, left, right, fire;
    uint8_t start1, start2, coin1;
} md_input_t;

void md_init(const md_roms_t *roms);
void md_reset(void);
void md_set_dips(uint8_t dsw1, uint8_t dsw2);
md_input_t *md_input(void);

void md_run_frame(void);
void md_video_init(void);
void md_render(uint8_t *fb);                     /* MD_FB_W * MD_FB_H palette indices */
void md_palette(uint16_t out[MD_PALETTE_SIZE]);  /* RGB565 */
void md_render_audio(int16_t *buf, int samples, int rate);

uint16_t md_pc(void);
uint32_t md_frame_count(void);

#ifdef __cplusplus
}
#endif
#endif
