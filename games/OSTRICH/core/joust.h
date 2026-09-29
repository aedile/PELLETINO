/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * joust.h - Williams Joust (1982) board emulation: the 6809 CPU board with its Special Chip 1
 * blitter, the 4-bit bitmap video, and the Williams sound board's 6808 and 8-bit DAC.
 *
 * A 6809E at 1 MHz, 48 KB of video RAM the program draws into directly and through the SC1
 * blitter, a 16-entry palette in RAM, three 6821 PIAs (one for the muxed sticks, one for coins
 * and the scanline interrupts, one on the sound board), and a 6808 at 895 kHz that makes every
 * sound in software and writes it to an MC1408 DAC. The screen is 292x240 of a 304x256 bitmap.
 *
 * Memory map, timing, blitter and video follow MAME's williams/williams.cpp and friends.
 */
#ifndef JOUST_H
#define JOUST_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define JO_MAIN_CLOCK   1000000                         /* 12 MHz / 3 / 4 */
#define JO_SND_CLOCK    894886                          /* 3.579545 MHz / 4 */
#define JO_LINES        260                             /* 8 MHz pixel clock, 512 per line: 60.1 Hz */
#define JO_CYCLES_PER_LINE 64
#define JO_SND_CYCLES_PER_FRAME 14891
#define JO_AUDIO_RATE   20050                           /* the DAC is sampled at this rate */

#define JO_SCREEN_X0 6                                  /* the visible part of the bitmap */
#define JO_SCREEN_Y0 7
#define JO_SCREEN_W 292
#define JO_SCREEN_H 240

#define JO_FB_W 292                                     /* what jo_render() makes: the visible screen */
#define JO_FB_H 240
#define JO_PALETTE_SIZE 16

/* the visible screen as the hardware keeps it: a byte holds two pixels side by side, even
 * pixel in the high nibble, and the bytes run down a column - 146 columns of 240 bytes */
#define JO_SNAP_COLS 146
#define JO_SNAP_SIZE (JO_SNAP_COLS * JO_SCREEN_H)

typedef struct {
    const uint8_t *rom_lo;     /* 36 KB, 0x0000-0x8FFF when banked over the video RAM */
    const uint8_t *rom_hi;     /* 12 KB, 0xD000-0xFFFF */
    const uint8_t *snd;        /* 4 KB, sound CPU 0xF000-0xFFFF */
} jo_roms_t;

typedef struct {
    uint8_t left, right, flap;
    uint8_t start1, start2, coin1;
} jo_input_t;

void jo_init(const jo_roms_t *roms);
void jo_reset(void);
jo_input_t *jo_input(void);

void jo_run_frame(void);
void jo_snapshot(uint8_t *snap);                  /* JO_SNAP_SIZE bytes, see above */
void jo_render(uint8_t *fb);                      /* JO_FB_W * JO_FB_H palette indices */
void jo_palette(uint16_t out[JO_PALETTE_SIZE]);   /* RGB565 */
void jo_render_audio(int16_t *buf, int samples, int rate);

uint16_t jo_pc(void);
uint16_t jo_snd_pc(void);
uint32_t jo_frame_count(void);
uint8_t *jo_mem(uint16_t addr);      /* a byte of RAM by its CPU address, or NULL */

#ifdef __cplusplus
}
#endif
#endif
