/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * frogger.h - Konami Frogger (1981) board emulation
 *
 * Galaxian-derived hardware: a Z80 at 3.072 MHz for the game, a second Z80 at 1.79 MHz that
 * does nothing but sound, one AY-3-8910, and a pair of 8255 PPIs carrying the inputs one way
 * and the sound latch the other.
 *
 * The video is a 32x32 tilemap of 8x8 characters with eight 16x16 sprites over it, and the
 * monitor is rotated 90 degrees - so the per-column vertical scroll the hardware provides comes
 * out as the sideways-moving traffic lanes and logs. Behind it all sits a flat blue field over
 * half the screen, which is the river.
 *
 * Timing, memory map and video follow MAME's galaxian.cpp and galaxian_v.cpp.
 */
#ifndef FROGGER_H
#define FROGGER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define FR_MAIN_CLOCK   3072000                 /* 18.432 MHz / 6 */
#define FR_AUDIO_CLOCK  1789773                 /* 14.318181 MHz / 8 */
/* 18.432 MHz pixel clock over 384 x 264 gives 60.606 Hz */
#define FR_FPS_NUM      18432000
#define FR_FPS_DEN      (384 * 264 * 3 / 3)
#define FR_MAIN_CYCLES_PER_FRAME  50688         /* 3072000 / 60.606 */
#define FR_AUDIO_CYCLES_PER_FRAME 29528         /* 1789773 / 60.606 */

/* the monitor is rotated, so the picture is taller than it is wide */
#define FR_FB_W 224
#define FR_FB_H 256
#define FR_PALETTE_SIZE 33                      /* 32 from the PROM, plus the river blue */
#define FR_PEN_RIVER 32

typedef struct {
    const uint8_t *rom;        /* 12 KB main program, 0x0000-0x2FFF */
    const uint8_t *audiorom;   /* 6 KB sound program, 0x0000-0x17FF */
    const uint8_t *gfx;        /* 4 KB: character plane 1 then plane 0 */
    const uint8_t *prom;       /* 32-byte colour PROM */
} fr_roms_t;

typedef struct {
    uint8_t up, down, left, right;
    uint8_t start1, start2, coin1;
} fr_input_t;

void fr_init(const fr_roms_t *roms);
void fr_reset(void);
/* IN1 and IN2 carry the DIP switches; see the table in frogger.c */
void fr_set_dips(uint8_t in1, uint8_t in2);
fr_input_t *fr_input(void);

void fr_run_frame(void);
void fr_video_init(void);                       /* expands the character ROM; call once */
void fr_render(uint8_t *fb);                    /* FR_FB_W * FR_FB_H palette indices */
void fr_palette(uint16_t out[FR_PALETTE_SIZE]); /* RGB565 */
void fr_render_audio(int16_t *buf, int samples, int rate);

/* diagnostics */
uint16_t fr_pc(void);
uint32_t fr_frame_count(void);
uint8_t *fr_mem(uint16_t addr);      /* a byte of RAM by its CPU address, or NULL */
const uint8_t *fr_ram(void);

#ifdef __cplusplus
}
#endif
#endif
