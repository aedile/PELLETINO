/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * btime.h - Data East Burger Time (1982) board emulation
 *
 * Two 6502s: one at 1.5 MHz for the game, inside DECO's CPU-7 block that scrambles the
 * opcodes it fetches after a write, and one at 500 kHz on the sound board with two
 * AY-3-8910s. A 32x32 map of three-plane characters, eight 16x16 sprites, an optional
 * scrolling background of 16x16 tiles, and a sixteen-register palette. No vblank interrupt:
 * the program polls the vblank line, and a coin is what interrupts it. The monitor stands on
 * its side.
 *
 * Timing, memory map and video follow MAME's dataeast/btime.cpp.
 */
#ifndef BTIME_H
#define BTIME_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define BT_CPU_CLOCK     1500000                        /* 12 MHz / 8 */
#define BT_SND_CLOCK      500000                        /* 12 MHz / 24 */
#define BT_AY_CLOCK      1500000
/* 6 MHz pixel clock over 384 x 272 gives 57.44 Hz */
#define BT_CYCLES_PER_FRAME     26112
#define BT_SND_CYCLES_PER_FRAME  8704

/* the frame in hardware coordinates, before the cabinet turns it */
#define BT_FB_W 240
#define BT_FB_H 240
#define BT_PALETTE_SIZE 16

typedef struct {
    const uint8_t *rom;        /* 20 KB program, CPU 0xB000-0xFFFF */
    const uint8_t *snd;        /* 4 KB sound program */
    const uint8_t *gfx1;       /* 24 KB: characters and sprites, three planes */
    const uint8_t *gfx2;       /* 6 KB: background tiles, three planes */
    const uint8_t *bgmap;      /* 2 KB: the background layout */
} bt_roms_t;

typedef struct {
    uint8_t up, down, left, right, fire;
    uint8_t start1, start2, coin1;
} bt_input_t;

void bt_init(const bt_roms_t *roms);
void bt_reset(void);
void bt_set_dips(uint8_t dsw1, uint8_t dsw2);
bt_input_t *bt_input(void);

void bt_run_frame(void);
void bt_video_init(void);
void bt_render(uint8_t *fb);                     /* BT_FB_W * BT_FB_H palette indices */
void bt_palette(uint16_t out[BT_PALETTE_SIZE]);  /* RGB565 */
void bt_render_audio(int16_t *buf, int samples, int rate);

uint16_t bt_pc(void);
uint16_t bt_snd_pc(void);
uint32_t bt_frame_count(void);

#ifdef __cplusplus
}
#endif
#endif
