/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * mario.h - Nintendo Mario Bros. (1983) board emulation
 *
 * Donkey Kong's kind of board, grown up: a Z80 at 4.096 MHz, an 8039 for the sound that plays
 * the tunes by writing samples to a DAC, a 32x32 character map with a vertical scroll, and
 * sixteen-colour sprites from three bit planes. The monitor stands the normal way up, 256
 * wide by 224 tall, so on the medal the picture is letterboxed.
 *
 * Timing, memory map and video follow MAME's nintendo/mario.cpp, mario_v.cpp and mario_a.cpp.
 */
#ifndef MARIO_H
#define MARIO_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define MB_CPU_CLOCK     4096000                        /* 24.576 MHz / 6 */
#define MB_SND_CLOCK     11000000                       /* the 8039 */
#define MB_CYCLES_PER_FRAME 67584                       /* 384 x 264 at 6.144 MHz = 60.606 Hz */

#define MB_FB_W 256
#define MB_FB_H 224
#define MB_PALETTE_SIZE 256

typedef struct {
    const uint8_t *rom;        /* 24 KB Z80 program at 0x0000, then 4 KB at 0xF000 (28 KB) */
    const uint8_t *snd;        /* 4 KB 8039 program; its second half doubles as sample pages */
    const uint8_t *chr;        /* 8 KB characters: 3f then 3j */
    const uint8_t *spr;        /* 24 KB sprites: three planes of 8 KB, each plane two halves */
    const uint8_t *prom;       /* 512-byte colour PROM; the first 256 are the monitor we use */
} mb_roms_t;

typedef struct {
    uint8_t left, right, jump;                /* player 1 */
    uint8_t start1, start2, coin1;
} mb_input_t;

void mb_init(const mb_roms_t *roms);
void mb_reset(void);
void mb_set_dips(uint8_t dsw);
mb_input_t *mb_input(void);

void mb_run_frame(void);
void mb_video_init(void);
void mb_render(uint8_t *fb);                     /* MB_FB_W * MB_FB_H palette indices */
void mb_palette(uint16_t out[MB_PALETTE_SIZE]);  /* RGB565 */
void mb_render_audio(int16_t *buf, int samples, int rate);

uint16_t mb_pc(void);
uint16_t mb_snd_pc(void);
uint32_t mb_frame_count(void);
extern uint32_t mb_dbg_writes[16];               /* diagnostics: writes to the latches at 0x7C00.. */

#ifdef __cplusplus
}
#endif
#endif
