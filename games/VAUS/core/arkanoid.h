/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * arkanoid.h - Taito Arkanoid (1986) board emulation
 *
 * A Z80 at 6 MHz, a YM2149 for sound, and an MC68705P5 microcontroller whose whole job is to
 * read the spinner and the buttons and hand them to the Z80 through a two-byte handshake with
 * a pair of semaphore flip-flops. The game checks that handshake, so the MCU is emulated
 * rather than stubbed.
 *
 * Timing, memory map and video follow MAME's taito/arkanoid.cpp, arkanoid_m.cpp and
 * arkanoid_v.cpp.
 */
#ifndef ARKANOID_H
#define ARKANOID_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define AK_CPU_CLOCK  6000000                       /* 12 MHz / 2 */
#define AK_MCU_CLOCK  3000000                       /* 12 MHz / 4 */
/* 6 MHz over 384 x 264 pixels = 59.185 Hz */
#define AK_CYCLES_PER_FRAME 101376
#define AK_MCU_CYCLES_PER_FRAME 50688

/* the native frame, before the cabinet turns it upright */
#define AK_FB_W 256
#define AK_FB_H 224
#define AK_PALETTE_SIZE 256    /* one bank of the 512-colour PROM; see ak_palette */

typedef struct {
    const uint8_t *rom;        /* 64 KB map, the low 48 K populated */
    const uint8_t *mcu;        /* 2 KB 68705 program */
    const uint8_t *gfx;        /* 96 KB: three bit planes of 32 KB */
    const uint8_t *prom_r, *prom_g, *prom_b;
} ak_roms_t;

typedef struct {
    int16_t paddle;            /* the spinner, as an absolute position 0..255 */
    uint8_t fire;
    uint8_t start1, start2, coin1, service;
} ak_input_t;

void ak_init(const ak_roms_t *roms);
void ak_reset(void);
void ak_set_dips(uint8_t dsw);
ak_input_t *ak_input(void);

void ak_run_frame(void);
void ak_video_init(void);
void ak_render(uint8_t *fb);                     /* AK_FB_W * AK_FB_H palette indices */
void ak_palette(uint16_t out[AK_PALETTE_SIZE]);  /* RGB565 */
void ak_render_audio(int16_t *buf, int samples, int rate);

uint16_t ak_pc(void);
uint16_t ak_mcu_pc(void);
uint32_t ak_frame_count(void);

#ifdef __cplusplus
}
#endif
#endif
