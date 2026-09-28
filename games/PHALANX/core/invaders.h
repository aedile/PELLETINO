/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * invaders.h - Taito/Midway Space Invaders (1978) board emulation
 *
 * An Intel 8080 at 1.9968 MHz, 8 KB of program ROM, 1 KB of work RAM and 7 KB of video RAM
 * holding a 256x224 one-bit-per-pixel frame. There is no video hardware to speak of: the
 * CPU draws every pixel itself, and the only help it gets is a 16-bit shift register on the
 * I/O ports that turns "shift this byte by n bits" into two OUTs and an IN.
 *
 * The monitor stands on its side, so the 256-wide frame is shown 224 wide and 256 tall, and
 * the colour is a plastic gel stuck to the glass: red across the UFO's lane, green over the
 * shields and the ground. That gel is what the palette here reproduces.
 *
 * The 8080 is run on Marat Fayzullin's Z80 core: the Z80 is a superset, and this program
 * touches nothing that differs.
 *
 * Timing, memory map and I/O follow MAME's mw8080bw.cpp.
 */
#ifndef INVADERS_H
#define INVADERS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SI_CPU_CLOCK         1996800               /* 19.968 MHz / 10 */
#define SI_FPS               60
#define SI_CYCLES_PER_FRAME  (SI_CPU_CLOCK / SI_FPS) /* 33280 */

/* the monitor is rotated, so the picture is taller than it is wide */
#define SI_FB_W 224
#define SI_FB_H 256
#define SI_PALETTE_SIZE 4                          /* black, white, green, red */

typedef struct {
    const uint8_t *rom;        /* 8 KB, 0x0000-0x1FFF: invaders.h, .g, .f, .e in that order */
} si_roms_t;

typedef struct {
    uint8_t left, right, fire;
    uint8_t start1, start2, coin1;
} si_input_t;

void si_init(const si_roms_t *roms);
void si_reset(void);
/* the DIP switches on IN2: bits 0-1 lives (0=3 .. 3=6), bit 3 bonus (0=1500, 1=1000),
 * bit 7 coin info on the attract screen (0=shown) */
void si_set_dips(uint8_t in2);
si_input_t *si_input(void);

void si_run_frame(void);
void si_render(uint8_t *fb);                        /* SI_FB_W * SI_FB_H palette indices */
void si_palette(uint16_t out[SI_PALETTE_SIZE]);     /* RGB565 */
void si_render_audio(int16_t *buf, int samples, int rate);

/* diagnostics */
uint16_t si_pc(void);
uint32_t si_frame_count(void);
const uint8_t *si_ram(void);

/* the sound board, fed from OUT 3 and OUT 5 (invaders_sound.c) */
void si_sound_reset(void);
void si_sound_write(int port, uint8_t v);

#ifdef __cplusplus
}
#endif
#endif
