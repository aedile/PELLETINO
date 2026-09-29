/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * digdug.h - Namco Dig Dug (1982) board emulation
 *
 * Three Z80s at 3.072 MHz sharing one memory map, the Namco 3-voice WSG, and the 06XX bus
 * controller fronting two custom I/O chips: a 51XX for the joystick, coins and credits, and a
 * 53XX that reads the DIP switches. The same arrangement as Galaga, minus the starfield and
 * with a 53XX where Galaga has the 54XX noise generator.
 *
 * The video is three layers: a background built from a ROM that says which tile goes where -
 * the dirt you dig through - a text layer over it, and 16x16 sprites on top.
 *
 * Timing, memory map and video follow MAME's namco/galaga.cpp and namco/digdug.cpp.
 */
#ifndef DIGDUG_H
#define DIGDUG_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define DD_MASTER_CLOCK  18432000
#define DD_CPU_CLOCK     (DD_MASTER_CLOCK / 6)      /* 3.072 MHz */
#define DD_CYCLES_PER_LINE   192
#define DD_LINES             264
#define DD_CYCLES_PER_FRAME  (DD_CYCLES_PER_LINE * DD_LINES)   /* 50688 */
#define DD_VBLANK_LINE       224

/* the native frame, before the cabinet turns it upright - the renderer does the rotation */
#define DD_FB_W 288
#define DD_FB_H 224
#define DD_PALETTE_SIZE 32

typedef struct {
    const uint8_t *cpu1, *cpu2, *cpu3;
    const uint8_t *chr;        /* 2 KB, 1bpp text characters */
    const uint8_t *spr;        /* 16 KB, 16x16 2bpp sprites */
    const uint8_t *bgt;        /* 4 KB, 8x8 2bpp background tiles */
    const uint8_t *play;       /* 4 KB playfield map: four screens of tile numbers */
    const uint8_t *pal;        /* 32-byte colour PROM */
    const uint8_t *lut_spr;    /* 256-byte sprite colour lookup */
    const uint8_t *lut_bg;     /* 256-byte background colour lookup */
    const uint8_t *wave;       /* 256-byte WSG waveform PROM */
} dd_roms_t;

typedef struct {
    uint8_t up, down, left, right, fire;
    uint8_t start1, start2, coin1, coin2, service;
} dd_input_t;

void dd_init(const dd_roms_t *roms);
void dd_reset(void);
void dd_set_dips(uint8_t a, uint8_t b);
dd_input_t *dd_input(void);

void dd_run_frame(void);
void dd_render(uint8_t *fb);                     /* DD_FB_W * DD_FB_H palette indices */
void dd_palette(uint16_t out[DD_PALETTE_SIZE]);  /* RGB565 */
void dd_render_audio(int16_t *buf, int samples, int rate);

uint16_t dd_pc(int cpu);
uint32_t dd_frame_count(void);
uint8_t *dd_mem(uint16_t addr);      /* a byte of RAM by its CPU address, or NULL */
uint32_t dd_halt_cycles(int cpu);
int dd_credits(void);

#ifdef __cplusplus
}
#endif
#endif
