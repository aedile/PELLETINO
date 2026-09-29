/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * missile.h - Atari Missile Command (1980) board emulation
 *
 * One 6502 at 1.25 MHz, a POKEY for sound, a trackball and three fire buttons.
 *
 * The unusual part is the video: there is no tilemap and no sprites. The screen is a
 * 256x231 bitmap living in the CPU's own 16 KB of RAM, two bits per pixel, and the game
 * writes to it through a side channel the schematics call MADSEL. When the CPU fetches an
 * opcode whose low five bits are 1 (the indexed-indirect addressing modes), the data access
 * five cycles later is diverted away from normal address decoding and into video RAM as a
 * two-bit pixel write. The bottom of the screen gets a third bit from a scattered set of
 * addresses, which is what gives the ground and cities their extra colours.
 *
 * Timing and memory map follow MAME's missile.cpp.
 */
#ifndef MISSILE_H
#define MISSILE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define MC_CPU_CLOCK        1250000    /* 10 MHz / 8; halves to /16 during the bottom 32 lines */
#define MC_LINES            256
#define MC_CYCLES_PER_FRAME 19200      /* 224 lines at 80 cycles + 32 lines at 40 */
#define MC_FB_W 256
#define MC_FB_H 231
#define MC_PALETTE_SIZE 8

typedef struct {
    const uint8_t *rom;          /* 12 KB, mapped at 0x5000-0x7FFF */
    const uint8_t *writeprom;    /* 32 bytes: the video RAM write masks */
} mc_roms_t;

typedef struct {
    int8_t track_x, track_y;     /* trackball counts this frame (positive = right / up) */
    uint8_t fire1, fire2, fire3; /* the three base buttons */
    uint8_t start1, start2, coin1;
} mc_input_t;

void mc_init(const mc_roms_t *roms);
void mc_reset(void);
/* R10 (0x4A00) DIP bank and R8 (0x4000 read) DIP bank, as the game sees them */
void mc_set_dips(uint8_t r10, uint8_t r8);
mc_input_t *mc_input(void);

void mc_run_frame(void);
/* one frame of palette indices, MC_FB_W x MC_FB_H */
void mc_render(uint8_t *fb);
/* the eight palette entries as of now, RGB565 */
void mc_palette(uint16_t out[MC_PALETTE_SIZE]);
void mc_render_audio(int16_t *buf, int samples, int rate);

/* diagnostics */
uint16_t mc_pc(void);
uint32_t mc_frame_count(void);
uint8_t *mc_mem(uint16_t addr);      /* a byte of RAM by its CPU address, or NULL */
uint32_t mc_irq_count(void);
/* CPU cycles skipped since the last call because the game was waiting for the frame tick */
uint32_t mc_idle_cycles(void);
const uint8_t *mc_ram(void);        /* the 16 KB that is both work RAM and the bitmap */

#ifdef __cplusplus
}
#endif
#endif
