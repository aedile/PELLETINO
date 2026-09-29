/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * rallyx.h - Namco Rally-X (1980) board emulation
 *
 * One Z80 at 3.072 MHz, the Namco 3-voice WSG, and a discrete explosion circuit. The video is
 * two tilemaps out of one 4 KB block of RAM: a 32x32 playfield that scrolls in both directions,
 * and an 8x32 strip repeated down the right-hand quarter of the screen, which is the radar.
 * Sprites and the radar dots live in the first 64 bytes of that same block.
 *
 * Timing, memory map and video follow MAME's namco/rallyx.cpp and rallyx_v.cpp.
 */
#ifndef RALLYX_H
#define RALLYX_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define RX_MASTER_CLOCK  18432000
#define RX_CPU_CLOCK     (RX_MASTER_CLOCK / 6)     /* 3.072 MHz */
/* 6.144 MHz pixel clock over 384 x 264 gives 60.606 Hz */
#define RX_CYCLES_PER_FRAME 50688

#define RX_FB_W 288
#define RX_FB_H 224
#define RX_PALETTE_SIZE 32

typedef struct {
    const uint8_t *rom;      /* 16 KB program */
    const uint8_t *gfx;      /* 4 KB characters and sprites */
    const uint8_t *dots;     /* 256-byte dot PROM: radar blips and smoke */
    const uint8_t *pal;      /* 32-byte colour PROM */
    const uint8_t *lut;      /* 256-byte lookup: (colour, pixel) -> palette entry */
    const uint8_t *wave;     /* 256-byte WSG waveform PROM */
} rx_roms_t;

typedef struct {
    uint8_t up, down, left, right, smoke;
    uint8_t start1, start2, coin1;
} rx_input_t;

void rx_init(const rx_roms_t *roms);
void rx_reset(void);
void rx_set_dips(uint8_t dsw);
rx_input_t *rx_input(void);

void rx_run_frame(void);
void rx_video_init(void);
void rx_render(uint8_t *fb);                     /* RX_FB_W * RX_FB_H palette indices */
void rx_palette(uint16_t out[RX_PALETTE_SIZE]);  /* RGB565 */
void rx_render_audio(int16_t *buf, int samples, int rate);

uint16_t rx_pc(void);
uint32_t rx_frame_count(void);
uint8_t *rx_mem(uint16_t addr);      /* a byte of RAM by its CPU address, or NULL */
const uint8_t *rx_ram(void);

#ifdef __cplusplus
}
#endif
#endif
