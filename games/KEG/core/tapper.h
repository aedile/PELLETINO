/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * tapper.h - Bally Midway Tapper (1983) board emulation: MCR 91490 CPU board, 91464 Super
 * Video Gen, and the SSIO sound board
 *
 * A Z80 at 5 MHz paced by a Z80 CTC (mode 2 interrupts from vblank and a free-running timer),
 * a 32x30 map of 16x16 four-bit tiles, 128 sprites of 32x32 read straight from 128 KB of ROM,
 * a 64-entry palette in RAM, and on the SSIO a second Z80 at 2 MHz with two AY-3-8910s behind
 * four data latches. The hardware draws 512x480 interlaced; this draws it at 256x240.
 *
 * Timing, memory map and video follow MAME's bally/mcr.cpp, mcr_v.cpp and midway_sound.cpp.
 */
#ifndef TAPPER_H
#define TAPPER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define TAP_MAIN_CLOCK   5000000
#define TAP_SND_CLOCK    2000000                        /* the SSIO's Z80 and both AYs */
#define TAP_MAIN_CYCLES_PER_FRAME 83333                 /* one 60 Hz field */
#define TAP_SND_CYCLES_PER_FRAME  33333
#define TAP_SSIO_TICKS_PER_FRAME  833                   /* the 14024 counts at 50 kHz */

#define TAP_FB_W 256
#define TAP_FB_H 240
#define TAP_PALETTE_SIZE 64

typedef struct {
    const uint8_t *rom;        /* 56 KB program */
    const uint8_t *snd;        /* 16 KB SSIO program */
    const uint8_t *gfx1;       /* 32 KB tiles */
    const uint8_t *gfx2;       /* 128 KB sprites, four quarters of column pairs */
} tap_roms_t;

typedef struct {
    uint8_t up, down, left, right, fire;
    uint8_t start1, start2, coin1;
} tap_input_t;

void tap_init(const tap_roms_t *roms);
void tap_reset(void);
void tap_set_dips(uint8_t ip3);
tap_input_t *tap_input(void);

void tap_run_frame(void);
void tap_video_init(void);
void tap_render(uint8_t *fb);                     /* TAP_FB_W * TAP_FB_H palette indices */
void tap_palette(uint16_t out[TAP_PALETTE_SIZE]); /* RGB565 */
void tap_render_audio(int16_t *buf, int samples, int rate);

uint16_t tap_pc(void);
uint16_t tap_snd_pc(void);
uint32_t tap_frame_count(void);

#ifdef __cplusplus
}
#endif
#endif
