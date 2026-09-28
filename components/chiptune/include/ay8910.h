/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * ay8910.h - General Instrument AY-3-8910 programmable sound generator
 *
 * Three square-wave tone channels, one noise source shared between them, and one envelope
 * generator that any channel can use instead of a fixed level. The two I/O ports are read-only
 * here: Frogger wires port A to the sound latch and port B to a counter chain, so the host
 * supplies them through a callback.
 */
#ifndef AY8910_H
#define AY8910_H
#include <stdint.h>

typedef uint8_t (*ay_port_read_fn)(void *ctx, int port);   /* port 0 = A, 1 = B */
typedef void (*ay_port_write_fn)(void *ctx, int port, uint8_t v);

typedef struct {
    uint8_t reg[16];
    int32_t dc;                 /* running DC estimate for the output high pass */
    uint8_t latch;                 /* the register the next data write lands in */
    /* tone: a counter per channel, clocked at clock/8, toggling the output on underflow */
    uint16_t tone_count[3];
    uint16_t period[3], noise_period;
    uint32_t env_period;           /* two clock/8 ticks per envelope step, so up to 0x1FFFE */
    uint8_t tone_out[3];
    uint16_t noise_count;
    uint8_t noise_out;
    uint32_t noise_rng;
    uint32_t env_count;
    uint8_t env_step, env_out, env_hold, env_attack;
    uint32_t clock;
    uint32_t acc;                  /* 16.16 fraction of an AY step carried between samples */
    ay_port_read_fn port_read;
    ay_port_write_fn port_write;
    void *port_wctx;
    void *port_ctx;
} ay8910_t;

void ay_init(ay8910_t *ay, uint32_t clock, ay_port_read_fn port_read, void *ctx);
void ay_set_port_write(ay8910_t *ay, ay_port_write_fn fn, void *ctx);   /* registers 14 and 15 written */
void ay_reset(ay8910_t *ay);
void ay_address_w(ay8910_t *ay, uint8_t reg);
void ay_data_w(ay8910_t *ay, uint8_t data);
uint8_t ay_data_r(ay8910_t *ay);
/* mix `samples` of output into buf (adds, does not overwrite) */
void ay_render(ay8910_t *ay, int16_t *buf, int samples, int rate);

#endif
