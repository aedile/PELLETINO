/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * galaga.h - Namco Galaga (1981) board emulation
 *
 * Three Z80s at 3.072 MHz sharing RAM and I/O; Namco 06XX interface to the
 * 51XX (coins/joystick/credits) and 54XX (noise) custom MCUs, which are
 * modelled at a high level because their internal ROMs are not part of the
 * standard ROM set; 05XX starfield; 36x28 tile layer with 64 sprites; 3-voice
 * Namco WSG sound. Timing and memory map follow MAME's galaga.cpp.
 */
#ifndef GALAGA_H
#define GALAGA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GA_CPU_CLOCK        3072000
#define GA_LINES            264
#define GA_CYCLES_PER_LINE  192
#define GA_CYCLES_PER_FRAME (GA_LINES * GA_CYCLES_PER_LINE)   /* 50688: 60.6 Hz */
#define GA_VBLANK_LINE      224

/* native (landscape) frame buffer; the cabinet rotates it 90 degrees to portrait */
#define GA_FB_W 288
#define GA_FB_H 224
#define GA_PALETTE_SIZE 97      /* 0-31 PROM colors, 32-95 star colors, 96 black */

typedef struct {
    const uint8_t *rom_cpu1;      /* 16KB */
    const uint8_t *rom_cpu2;      /* 4KB */
    const uint8_t *rom_cpu3;      /* 4KB */
    const uint8_t *tiles;         /* 256 x 64 decoded 2-bit pixels */
    const uint8_t *sprites;       /* 128 x 256 decoded 2-bit pixels */
    const uint8_t *prom_palette;  /* 32 */
    const uint8_t *prom_charlut;  /* 256 */
    const uint8_t *prom_spritelut;/* 256 */
    const uint8_t *prom_wave;     /* 256: 8 waveforms x 32 nibbles */
} ga_roms_t;

typedef struct {
    uint8_t left, right;      /* player 1 joystick */
    uint8_t fire;
    uint8_t start1, start2;
    uint8_t coin1, coin2, service;
} ga_input_t;

void ga_init(const ga_roms_t *roms);
void ga_reset(void);
/* DSW A/B as the game sees them (active low bits). Defaults: A=0xF7 (easy, demo sounds,
 * upright), B=0x97 (1C/1C, 20K/70K bonus, 3 lives). */
void ga_set_dips(uint8_t dswa, uint8_t dswb);
ga_input_t *ga_input(void);

/* Run one frame (50688 cycles per CPU). The frame buffer is rendered at vblank. */
void ga_run_frame(void);
/* Render the current video state into fb (GA_FB_W * GA_FB_H palette indices) */
void ga_render(uint8_t *fb);
/* RGB565 (not byte swapped) palette for the frame buffer indices */
const uint16_t *ga_palette(void);

/* audio: mixes WSG voices and the 54XX noise into signed 16-bit mono */
void ga_render_audio(int16_t *buf, int samples, int sample_rate);

/* diagnostics */
uint16_t ga_pc(int cpu);
uint32_t ga_frame_count(void);
uint8_t *ga_mem(uint16_t addr);      /* a byte of RAM by its CPU address, or NULL */
uint32_t ga_halt_cycles(int cpu);      /* cycles skipped while halted, since last call */
int ga_credits(void);
uint8_t ga_starfield_ctl(void);

#ifdef __cplusplus
}
#endif

#endif
