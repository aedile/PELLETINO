/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * gyruss.h - Konami Gyruss (1983) board emulation
 *
 * Four processors, which is more than any other medal in this set:
 *
 *   Z80      3.072 MHz   the game
 *   KONAMI-1 1.536 MHz   a 6809 with its opcodes scrambled, driving the sprites
 *   Z80      3.579 MHz   the sound board, with five AY-3-8910s on its I/O ports
 *   8039     8 MHz       a second sound processor doing nothing but feeding a DAC
 *
 * The two main processors share 2 KB of RAM - the Z80 sees it at 0xA000 and the 6809 at
 * 0x6000 - and that is the whole of their conversation. The sound board is handed a byte
 * through a latch and interrupted.
 *
 * Timing, memory map and video follow MAME's gyruss.cpp.
 */
#ifndef GYRUSS_H
#define GYRUSS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define GY_MAIN_CLOCK   3072000            /* 18.432 MHz / 6 */
#define GY_SUB_CLOCK    1536000            /* 18.432 MHz / 12 */
#define GY_AUDIO_CLOCK  3579545            /* 14.318181 MHz / 4 */
#define GY_AY_CLOCK     1789772            /* 14.318181 MHz / 8 */
#define GY_FPS          61                 /* measured on a real PCB: 60.56 Hz */
#define GY_MAIN_CYCLES_PER_FRAME  (GY_MAIN_CLOCK / GY_FPS)
#define GY_SUB_CYCLES_PER_FRAME   (GY_SUB_CLOCK / GY_FPS)
#define GY_AUDIO_CYCLES_PER_FRAME (GY_AUDIO_CLOCK / GY_FPS)

#define GY_FB_W 256
#define GY_FB_H 224
#define GY_PALETTE_SIZE 256                /* 16*4 sprite pens + 16*16 char pens, folded */

typedef struct {
    const uint8_t *rom;        /* 32 KB Z80 program */
    const uint8_t *subrom;     /* 8 KB for the 6809, at 0xE000 */
    const uint8_t *audiorom;   /* 24 KB sound Z80 program (the last 8 KB is an empty socket) */
    const uint8_t *audio2rom;  /* 4 KB for the 8039 */
    const uint8_t *sprites;    /* 32 KB, four planes */
    const uint8_t *tiles;      /* 8 KB, two planes */
    const uint8_t *proms;      /* 0x220: palette, then the sprite and character lookups */
} gy_roms_t;

typedef struct {
    uint8_t up, down, left, right, fire;
    uint8_t start1, start2, coin1;
} gy_input_t;

void gy_init(const gy_roms_t *roms);
void gy_reset(void);
void gy_set_dips(uint8_t dsw1, uint8_t dsw2, uint8_t dsw3);
gy_input_t *gy_input(void);

void gy_run_frame(void);
void gy_render(uint8_t *fb);                       /* GY_FB_W x GY_FB_H palette indices */
void gy_palette(uint16_t out[GY_PALETTE_SIZE]);    /* RGB565 */
void gy_render_audio(int16_t *buf, int samples, int rate);

/* diagnostics */
uint16_t gy_pc(void);
uint16_t gy_audio_pc(void);
uint16_t gy_sub_pc(void);
uint16_t gy_mcu_pc(void);
uint8_t  gy_soundlatch(void);
uint8_t  gy_audio_iff(void);
uint32_t gy_frame_count(void);

#ifdef __cplusplus
}
#endif
#endif
