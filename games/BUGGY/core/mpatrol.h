/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * mpatrol.h - Irem M52 board emulation for Moon Patrol (1982): a Z80 at 3.072 MHz, a 32x32
 * character map with a scrolling bottom quarter, three 256x64 scrolling background paintings
 * (mountains, hills, city), 64 sprites of 16x16, and the M52 sound board - a 6803 at 3.58 MHz
 * with two AY-3-8910s and an MSM5205 ADPCM chip whose sample clock paces it.
 *
 * Memory map, video and sound follow MAME's irem/m52.cpp and irem/irem.cpp.
 */
#ifndef MPATROL_H
#define MPATROL_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define MP_MAIN_CLOCK   3072000                         /* 18.432 MHz / 6 */
#define MP_SND_CLOCK    894886                          /* 3.579545 MHz / 4, the 6803's E */
#define MP_AY_CLOCK     894886
#define MP_MSM_CLOCK    384000
#define MP_LINES        282                             /* 6.144 MHz pixel clock, 384 a line: 56.7 Hz */
#define MP_CYCLES_PER_LINE 192
#define MP_CYCLES_PER_FRAME (MP_LINES * MP_CYCLES_PER_LINE)   /* 54144 */
#define MP_SND_CYCLES_PER_FRAME 15772
#define MP_AUDIO_RATE   20050
#define MP_FRAME_US     17627                           /* 1 / 56.74 Hz */

#define MP_FB_W 240
#define MP_FB_H 252
#define MP_PALETTE_SIZE 256                             /* 0-127 backgrounds/sprites, 128-255 unused */

typedef struct {
    const uint8_t *rom;        /* 16 KB program */
    const uint8_t *snd;        /* 4 KB sound program, 0x7000-0x7FFF */
    const uint8_t *tx;         /* 8 KB characters, two planes */
    const uint8_t *sp;         /* 8 KB sprites, two planes */
    const uint8_t *bg;         /* 12 KB backgrounds, three of 4 KB */
    const uint8_t *proms;      /* 0x340: tx palette 512, bg palette 32, sprite palette 32, sprite lookup 256 */
} mp_roms_t;

typedef struct {
    uint8_t left, right, jump, fire;
    uint8_t start1, start2, coin1;
} mp_input_t;

void mp_init(const mp_roms_t *roms);
void mp_reset(void);
void mp_set_dips(uint8_t dsw1, uint8_t dsw2);
mp_input_t *mp_input(void);

void mp_run_frame(void);
void mp_render(uint8_t *fb);                      /* MP_FB_W * MP_FB_H colour indices, see mp_palette */
void mp_palette(uint16_t out[MP_PALETTE_SIZE]);   /* RGB565; fixed, from the PROMs */
void mp_render_audio(int16_t *buf, int samples, int rate);

uint16_t mp_pc(void);
uint16_t mp_snd_pc(void);
uint32_t mp_frame_count(void);
uint8_t *mp_mem(uint16_t addr);      /* a byte of RAM by its CPU address, or NULL */

#ifdef __cplusplus
}
#endif
#endif
