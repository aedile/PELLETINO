/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * polepos.h - Namco Pole Position (1982) board emulation
 *
 * Z80 (sound, I/O, coordination) plus two Zilog Z8002 16-bit CPUs (game logic
 * and the road/scenery), all at 3.072 MHz sharing 16-bit video RAM. Namco 06XX
 * interface to the 51XX (switches), 53XX (steering, DIPs), 52XX (speech samples)
 * and 54XX (noise) MCUs, modelled at protocol level. Video: scrolling background,
 * road generator, 64 zoomable sprites, text layer. Sound: 8-voice Namco WSG,
 * engine sample generator, 52XX samples, 54XX noise. Follows MAME's polepos.cpp.
 */
#ifndef POLEPOS_H
#define POLEPOS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define PP_CPU_CLOCK        3072000
#define PP_LINES            264
#define PP_CYCLES_PER_LINE  192
#ifndef PP_SLICE_LINES
#define PP_SLICE_LINES      4        /* CPUs are interleaved every 4 lines (IRQ/NVI lines are multiples of 4) */
#endif
#define PP_CYCLES_PER_FRAME (PP_LINES * PP_CYCLES_PER_LINE)   /* 60.6 Hz */
#define PP_FB_W 256
#define PP_FB_H 224          /* native rows 16..239 */
#define PP_COLORS 128

typedef struct {
    const uint8_t *rom_z80;      /* 0x3000 */
    const uint8_t *rom_sub1;     /* 0x8000, big-endian words */
    const uint8_t *rom_sub2;     /* 0x8000 */
    const uint8_t *chars;        /* 256 x 64, 2-bit pixels */
    const uint8_t *tiles;        /* 256 x 64 */
    const uint8_t *sprites;      /* 128 x 256, 4-bit pixels */
    const uint8_t *bigsprites;   /* 128 x 1024 */
    const uint8_t *road;         /* 0x5000 */
    const uint8_t *scalelut;     /* 0x1000 */
    const uint8_t *proms;        /* 0x1000 */
    const uint8_t *wave;         /* 0x100 */
    const uint8_t *engine;       /* 0x4000 */
    const uint8_t *voice;        /* 0x6000 */
} pp_roms_t;

typedef struct {
    uint8_t steer;               /* free-running wheel position, 8-bit; the game reads deltas */
    uint8_t accel, brake;        /* 0..0x90 */
    uint8_t gear;                /* 1 = high gear */
    uint8_t coin1, coin2, service, test;
} pp_input_t;

void pp_init(const pp_roms_t *roms);
void pp_reset(void);
void pp_set_dips(uint8_t dswa, uint8_t dswb);   /* defaults 0xff, 0x6b */
pp_input_t *pp_input(void);
void pp_run_frame(void);
void pp_render(uint8_t *fb);                    /* PP_FB_W*PP_FB_H color indices */
const uint16_t *pp_palette(void);               /* 128 RGB565 entries */
void pp_render_audio(int16_t *buf, int samples, int sample_rate);

/* diagnostics */
typedef struct { uint64_t cpu_us[3]; } pp_stats_t;
void pp_set_time_source(uint64_t (*now_us)(void));   /* enables per-CPU host time accounting */
pp_stats_t *pp_stats(void);
uint16_t pp_pc(int cpu);          /* 0 = Z80, 1 = sub1, 2 = sub2 */
uint32_t pp_frame_count(void);
uint8_t *pp_mem(uint16_t addr);      /* a byte of RAM by its CPU address, or NULL */
uint32_t pp_idle_cycles(int cpu); /* skipped cycles since last call */
uint8_t pp_latch(void);
const uint8_t *pp_nvram(void);    /* 0x800 bytes, for persistence */
void pp_nvram_load(const uint8_t *data);

#ifdef __cplusplus
}
#endif
#endif
