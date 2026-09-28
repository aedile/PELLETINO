/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * centiped.c - Atari Centipede board: memory map, I/O, interrupts, trackball, EAROM.
 * The 6502 is Andre Weissflog's cycle-stepped core from the chips project (m6502.h).
 */
#include "centiped.h"
#include "centiped_internal.h"
#include "pokey.h"
#include <string.h>
#include <stdlib.h>

#define CHIPS_IMPL
#include "m6502.h"

static ce_roms_t roms;
uint8_t ce_ram_bytes[0x800];             /* 0x0000-0x07FF: RAM, playfield (0x400), sprites (0x7C0) */
static uint8_t dsw1 = 0x54, dsw2 = 0x02;  /* 3 lives, 12000 bonus, easy; 1 coin 1 play */
static ce_input_t input;
static m6502_t cpu;
static uint64_t pins;
static pokey_t pokey;
static uint32_t frame_count, irq_count, idle_cycles;
static int irq_line;                      /* the 6502's IRQ input */
static int scanline;
static uint32_t cycle_in_frame;

/* ---- trackball: a 4-bit up/down counter per axis plus a direction latch ---- */
static uint8_t tb_pos[2], tb_sign[2];
static int tb_quarter;                    /* which of the four reads this frame we are at */

/* ---- ER2055 EAROM: 64 x 8, driven by control lines from the latch at 0x1680 ---- */
static uint8_t earom[64];
static uint8_t earom_addr, earom_data_in, earom_data_out;
static uint8_t earom_ctl;                 /* bit0 CK, bit1 C1, bit2 C2, bit3 CS1 */

static void earom_update(void)
{
    switch (earom_ctl & 0x06) {           /* C1, C2 */
        case 0x00: earom[earom_addr] &= earom_data_in; break;   /* write (erase first, or you AND) */
        case 0x04: earom[earom_addr] = 0xff; break;              /* erase */
        default: break;
    }
}
static void earom_control_w(uint8_t d)
{
    /* CK = D0, C1 = /D1, C2 = D2, CS1 = D3; CS2 is tied on */
    uint8_t old = earom_ctl;
    uint8_t ctl = (uint8_t)((d & 1) | ((d & 2) ? 0 : 2) | (d & 4) | (d & 8));
    /* control change with the chip selected */
    uint8_t nc = (uint8_t)((old & 1) | (ctl & 0x0e));
    if ((nc & 8) && nc != old) { earom_ctl = nc; earom_update(); }
    else earom_ctl = nc;
    /* clock: updates on the falling edge while selected */
    old = earom_ctl;
    earom_ctl = (uint8_t)((earom_ctl & 0x0e) | (ctl & 1));
    if ((earom_ctl & 8) && earom_ctl != old && !(ctl & 1)) {
        if (earom_ctl & 2) earom_data_out = earom[earom_addr];   /* read mode */
        earom_update();
    }
}

/* ---- output latch 0x1C00-0x1C07 (bit 7 of the data goes to latch bit addr&7) ---- */
static uint8_t outlatch;

/* ---- POKEY random: the game reads RANDOM for its dice ---- */
static uint32_t random_clock;

/* ---- bus ---- */
static inline uint8_t bus_read(uint16_t a)
{
    a &= 0x3fff;
    if (a < 0x0800) return ce_ram_bytes[a];
    if (a >= 0x2000) return roms.rom[a - 0x2000];
    switch (a & 0x3c00) {
        case 0x0800:
            return (a & 1) ? dsw2 : dsw1;
        case 0x0c00:
            switch (a & 3) {
                case 0: {   /* IN0: trackball X (0-3), cabinet (4: 0 = upright), service (5, active low), vblank (6), X sign (7) */
                    uint8_t v = (uint8_t)((tb_pos[0] & 0x0f) | tb_sign[0] | 0x20);
                    if (input.test) v &= ~0x20;
                    if (scanline >= 240) v |= 0x40;
                    return v;
                }
                case 1: {   /* IN1, active low: start1, start2, fire, fire (cocktail), tilt, coin1, coin2, service */
                    uint8_t v = 0xff;
                    if (input.start1) v &= ~0x01;
                    if (input.start2) v &= ~0x02;
                    if (input.fire)   v &= ~0x04;
                    if (input.coin1)  v &= ~0x20;
                    if (input.coin2)  v &= ~0x40;
                    return v;
                }
                case 2:     /* IN2: trackball Y */
                    return (uint8_t)((tb_pos[1] & 0x0f) | tb_sign[1]);
                default: {  /* IN3, active low: cocktail joystick (0-3), upright joystick up/down/left/right (4-7) */
                    uint8_t v = 0xff;
                    if (input.joy_up)    v &= ~0x10;
                    if (input.joy_down)  v &= ~0x20;
                    if (input.joy_left)  v &= ~0x40;
                    if (input.joy_right) v &= ~0x80;
                    return v;
                }
            }
        case 0x1000:
            return pokey_read(&pokey, a & 0x0f, random_clock + cycle_in_frame);
        case 0x1400: return 0xff;
        case 0x1800: return 0xff;
        default:
            if (a >= 0x1700 && a < 0x1740) return earom_data_out;
            return 0xff;
    }
}

static inline void bus_write(uint16_t a, uint8_t d)
{
    a &= 0x3fff;
    if (a < 0x0800) { ce_ram_bytes[a] = d; return; }
    if (a >= 0x2000) return;                              /* watchdog at 0x2000, ROM otherwise */
    switch (a & 0x3c00) {
        case 0x1000: pokey_write(&pokey, a & 0x0f, d); return;
        case 0x1400: ce_video_palette_w(a & 0x0f, d); return;
        case 0x1800: irq_line = 0; return;                /* IRQ acknowledge */
        case 0x1c00:
            if (d & 0x80) outlatch |= (uint8_t)(1 << (a & 7)); else outlatch &= (uint8_t)~(1 << (a & 7));
            return;
        default:
            if (a >= 0x1600 && a < 0x1640) { earom_addr = a & 0x3f; earom_data_in = d; return; }
            if (a == 0x1680) { earom_control_w(d); return; }
            return;
    }
}

/* ---- public ---- */
void ce_init(const ce_roms_t *r)
{
    roms = *r;
    pokey_init(&pokey);
    ce_video_init(&roms);
    memset(earom, 0xff, sizeof(earom));
    ce_reset();
}

void ce_reset(void)
{
    memset(ce_ram_bytes, 0, sizeof(ce_ram_bytes));
    memset(&input, 0, sizeof(input));
    memset(tb_pos, 0, sizeof(tb_pos)); memset(tb_sign, 0, sizeof(tb_sign)); tb_quarter = 0;
    earom_ctl = 0; earom_addr = 0; earom_data_in = earom_data_out = 0;
    outlatch = 0; irq_line = 0; scanline = 0; cycle_in_frame = 0;
    pokey_reset(&pokey);
    ce_video_reset();
    m6502_desc_t desc; memset(&desc, 0, sizeof(desc));
    pins = m6502_init(&cpu, &desc);
    earom_control_w(0);
}

void ce_set_dips(uint8_t a, uint8_t b) { dsw1 = a; dsw2 = b; }
ce_input_t *ce_input(void) { return &input; }

/* Advance the counters by one quarter of the frame's motion, so that each of the game's four
 * reads per frame sees a step small enough for the 4-bit counter to represent. `quarter` is
 * 0..3 within the frame, and the split is stateless: step = motion*(q+1)/4 - motion*q/4. */
static void trackball_step(int quarter)
{
    const int8_t want[2] = { input.track_x, input.track_y };
    for (int i = 0; i < 2; i++) {
        if (!want[i]) continue;
        int step = (want[i] * (quarter + 1)) / 4 - (want[i] * quarter) / 4;
        if (!step) continue;
        tb_pos[i] = (uint8_t)(tb_pos[i] + step);
        tb_sign[i] = step < 0 ? 0x80 : 0x00;
    }
}

#ifdef CE_DEBUG
uint32_t ce_dbg_pc_hist[0x4000];
#endif
static inline void run_cycles(uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (irq_line) pins |= M6502_IRQ; else pins &= ~M6502_IRQ;
        pins = m6502_tick(&cpu, pins);
#ifdef CE_DEBUG
        if (pins & M6502_SYNC) ce_dbg_pc_hist[M6502_GET_ADDR(pins) & 0x3fff]++;
#endif
        uint16_t a = M6502_GET_ADDR(pins);
        if (pins & M6502_RW) { M6502_SET_DATA(pins, bus_read(a)); }
        else bus_write(a, M6502_GET_DATA(pins));
    }
    cycle_in_frame += n;
}

void ce_run_frame(void)
{
    cycle_in_frame = 0;
    tb_quarter = 0;
    uint32_t acc = 0;
    for (scanline = 0; scanline < CE_LINES; scanline++) {
        /* IRQ from 16V/32V: asserted at 48, 112, 176, 240, cleared at 16, 80, 144, 208 (and by the ack) */
        if ((scanline & 15) == 0 && (scanline & 16)) {
            if ((scanline - 1) & 32) {
                irq_line = 1; irq_count++;
                trackball_step(tb_quarter++);       /* the game reads the counters once per IRQ */
            } else irq_line = 0;
        }
        acc += CE_CYCLES_PER_FRAME;
        uint32_t n = acc / CE_LINES; acc -= n * CE_LINES;
        /* Main loop idle: 2015 LSR $8A / BCC 2015 waits for the IRQ handler to set bit 0 of $8A.
         * Parked there with no IRQ pending, nothing can happen until the next IRQ line: skip. */
#ifndef CE_NOSKIP
        /* Main loop idle: 2015 is LSR $8A / BCC 2015, waiting for the IRQ handler to set a bit in
         * $8A. The LSR rewrites $8A every pass, so this is only a true idle when $8A is zero:
         * then the shift changes nothing and nothing can happen until the next IRQ. */
        if ((pins & M6502_SYNC) && M6502_GET_ADDR(pins) == 0x2015 && ce_ram_bytes[0x8a] == 0 && !irq_line) {
#else
        if (0) {
#endif
            cycle_in_frame += n; idle_cycles += n;
            continue;
        }
        run_cycles(n);
    }
    random_clock += CE_CYCLES_PER_FRAME;
    frame_count++;
}

void ce_render(uint8_t *fb) { ce_video_render(fb); }
void ce_render_audio(int16_t *buf, int samples, int rate)
{
    memset(buf, 0, samples * sizeof(int16_t));
    pokey_render(&pokey, buf, samples, rate);
    for (int i = 0; i < samples; i++) {              /* one POKEY where Star Wars mixes four: bring it up */
        int32_t v = buf[i] * 3;
        buf[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
    }
}

uint16_t ce_pc(void) { return (uint16_t)m6502_pc(&cpu); }
uint32_t ce_frame_count(void) { return frame_count; }
uint32_t ce_irq_count(void) { return irq_count; }
uint32_t ce_idle_cycles(void) { uint32_t v = idle_cycles; idle_cycles = 0; return v; }
const uint8_t *ce_earom(void) { return earom; }
const uint8_t *ce_ram(void) { return ce_ram_bytes; }
