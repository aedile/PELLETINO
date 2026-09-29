/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * centiped.h - Atari Centipede (1980) board emulation
 *
 * One 6502 at 1.512 MHz, 8 KB of program ROM, 2 KB of RAM (playfield and sprite RAM
 * included), a POKEY for sound and randomness, an ER2055 EAROM for high scores, two
 * 4-bit trackball counters, an LS259 output latch and DIP switches. The video board
 * draws a 32x30 layer of 8x8 tiles and 16 sprites of 8x16 pixels, 2 bits per pixel,
 * with the colors coming from eight bytes of palette RAM. Timing and memory map follow
 * MAME's centiped.cpp.
 */
#ifndef CENTIPED_H
#define CENTIPED_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define CE_CPU_CLOCK        1512000
#define CE_LINES            256
#define CE_CYCLES_PER_FRAME 25200      /* 60 Hz */
#define CE_FB_W 256                    /* native frame: 256 wide, 240 tall, the cabinet turns it upright */
#define CE_FB_H 240
#define CE_PALETTE_SIZE 8              /* 0-3 tile colors, 4-7 sprite colors */

typedef struct {
    const uint8_t *rom;          /* 8 KB: 136001-407, 408, 409, 410, mapped at 0x2000 */
    const uint8_t *tiles;        /* 256 x 64: 8x8 tiles, one pixel per byte, values 0-3 */
    const uint8_t *sprites;      /* 128 x 128: 8x16 sprites, one pixel per byte */
} ce_roms_t;

typedef struct {
    int8_t track_x, track_y;     /* trackball motion per frame, in counts (positive = right / up) */
    uint8_t fire, start1, start2, coin1, coin2, test;
    uint8_t joy_up, joy_down, joy_left, joy_right;   /* the joystick inputs on IN3 (upright player) */
} ce_input_t;

void ce_init(const ce_roms_t *roms);
void ce_reset(void);
/* DSW1 (0x0800): bits 0-1 language, 2-3 lives (0=2..3=5), 4-5 bonus life, 6 easy (1), 7 credit min.
 * DSW2 (0x0801): bits 0-1 coinage (0 = free play, 2 = 1C/1C), 2-3 right coin, 4 left coin, 5-7 bonus coins. */
void ce_set_dips(uint8_t dsw1, uint8_t dsw2);
ce_input_t *ce_input(void);

void ce_run_frame(void);
/* one native frame of palette indices (CE_FB_W x CE_FB_H) */
void ce_render(uint8_t *fb);
/* the palette as of now, RGB565 */
void ce_palette(uint16_t out[CE_PALETTE_SIZE]);
/* mix `samples` of audio at `rate` into buf (overwrites) */
void ce_render_audio(int16_t *buf, int samples, int rate);

/* diagnostics */
uint16_t ce_pc(void);
uint32_t ce_frame_count(void);
uint8_t *ce_mem(uint16_t addr);      /* a byte of RAM by its CPU address, or NULL */
uint32_t ce_irq_count(void);
uint32_t ce_idle_cycles(void);       /* CPU cycles skipped in the idle loop since the last call */
const uint8_t *ce_earom(void);       /* 64 bytes */
const uint8_t *ce_ram(void);         /* 2 KB */

#ifdef __cplusplus
}
#endif
#endif
