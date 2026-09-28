/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * tempest.h - Atari Tempest (1981) board emulation
 *
 * A 6502 at 1.512 MHz, a colour Analog Vector Generator, two POKEYs, a math box and an EAROM
 * for the high scores. No tilemap, no sprites, no frame buffer: the picture is a list of
 * vectors the AVG walks continuously while the CPU rewrites it underneath.
 *
 * Three things about this board are worth knowing before reading the code:
 *
 *  - The inputs come through the sound chips. Each POKEY's eight pot lines are wired, one
 *    per bit, to an input port; a set bit charges its capacitor at once and a clear bit takes
 *    228 scanlines. The game writes POTGO, waits, and reads ALLPOT, which is how the spinner,
 *    the buttons and the cabinet switches get in.
 *
 *  - The AVG never stops. Tempest leaves it looping, and a frame is over when the sequencer
 *    jumps back to address zero.
 *
 *  - The math box is a separate arithmetic board. The address the CPU writes is the opcode.
 *
 * Timing and memory map follow MAME's tempest.cpp.
 */
#ifndef TEMPEST_H
#define TEMPEST_H
#include <stdint.h>
#include "avg.h"
#ifdef __cplusplus
extern "C" {
#endif

#define TP_MASTER_CLOCK 12096000
#define TP_CPU_CLOCK    (TP_MASTER_CLOCK / 8)      /* 1.512 MHz */
#define TP_CLOCK_3KHZ   (TP_MASTER_CLOCK / 4096)   /* 2953 Hz */
#define TP_IRQ_HZ       (TP_CLOCK_3KHZ / 12)       /* 246 Hz periodic IRQ */
#define TP_FPS          60
#define TP_CYCLES_PER_FRAME (TP_CPU_CLOCK / TP_FPS)

typedef struct {
    const uint8_t *rom;        /* 0x10000 image; only 0x9000-0xDFFF and 0xF000-0xFFFF are used */
    const uint8_t *vectorrom;  /* 4 KB at CPU 0x3000 */
    const uint8_t *avgprom;    /* 256-byte AVG state PROM */
} tp_roms_t;

typedef struct {
    int8_t  spin;              /* spinner movement this frame, signed; the game sees a 4-bit wheel */
    uint8_t fire, zap;         /* the two buttons */
    uint8_t start1, start2, coin1;
} tp_input_t;

void tp_init(const tp_roms_t *roms);
void tp_reset(void);
void tp_set_dips(uint8_t dsw1, uint8_t dsw2);
tp_input_t *tp_input(void);

/* run one 60 Hz frame: CPU and vector generator interleaved */
void tp_run_frame(void);

/* the vectors drawn by the last completed frame */
const avg_point_t *tp_points(int *count);

void tp_render_audio(int16_t *buf, int samples, int rate);

/* diagnostics */
uint16_t tp_pc(void);
uint32_t tp_frame_count(void);
int      tp_vector_count(void);

#ifdef __cplusplus
}
#endif
#endif
