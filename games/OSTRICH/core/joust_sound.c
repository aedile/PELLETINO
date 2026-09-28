/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * joust_sound.c - the Williams sound board: a 6808 at 895 kHz, a 6821 PIA whose port A is an
 * MC1408 DAC and whose port B takes the command from the main board, and 4 KB of program that
 * makes every sound by writing samples to the DAC in software. The DAC is sampled at the
 * output rate into a ring the audio side drains, so the program's own timing sets the pitch.
 */
#include "joust_internal.h"
#include <string.h>

static uint8_t ram[0x100];
static uint8_t dac;
static int32_t cycle_debt;
static uint32_t sample_acc;

/* the PIA: port A output, port B input from the command latch, CB1 low when the command is idle */
static uint8_t p_ddr[2], p_out[2], p_cr[2], p_cb1, cmd = 0xff;

#define RING 4096
static int16_t ring[RING];
static volatile uint32_t ring_w, ring_r;
static int16_t last_sample;

static uint8_t pia_read(int reg)
{
    int port = reg >> 1;
    if (reg & 1) return p_cr[port];
    if (!(p_cr[port] & 0x04)) return p_ddr[port];
    p_cr[port] &= 0x3f;
    uint8_t in = port ? cmd : p_out[0];
    return (uint8_t)((in & ~p_ddr[port]) | (p_out[port] & p_ddr[port]));
}
static void pia_write(int reg, uint8_t d)
{
    int port = reg >> 1;
    if (reg & 1) { p_cr[port] = (uint8_t)((p_cr[port] & 0xc0) | (d & 0x3f)); return; }
    if (!(p_cr[port] & 0x04)) { p_ddr[port] = d; return; }
    p_out[port] = d;
    if (port == 0) dac = d;
}
static inline int pia_irq(void)
{
    return ((p_cr[0] & 0x80) && (p_cr[0] & 0x01)) || ((p_cr[1] & 0x80) && (p_cr[1] & 0x01));
}

void jo_sound_cmd(uint8_t d)
{
    cmd = d;
    int cb1 = (d != 0xff);
    if (cb1 != p_cb1) {
        int rising = (p_cr[1] & 0x02) != 0;
        if (cb1 == rising) p_cr[1] |= 0x80;
        p_cb1 = (uint8_t)cb1;
    }
}

static uint8_t bus_read(uint16_t a)
{
    if (a < 0x0100) return ram[a];
    if (a >= 0xf000) return jo_roms.snd[a & 0xfff];
    if ((a & 0x7ffc) == 0x0400) return pia_read(a & 3);
    return 0xff;
}
static void bus_write(uint16_t a, uint8_t d)
{
    if (a < 0x0100) { ram[a] = d; return; }
    if ((a & 0x7ffc) == 0x0400) pia_write(a & 3, d);
}

#define M6800_READ(a)     bus_read((uint16_t)(a))
#define M6800_WRITE(a, v) bus_write((uint16_t)(a), (v))
#include "m6800.h"

static m6800_t cpu;

void jo_sound_init(void) { }

void jo_sound_reset(void)
{
    memset(ram, 0, sizeof(ram));
    memset(p_ddr, 0, sizeof(p_ddr)); memset(p_out, 0, sizeof(p_out)); memset(p_cr, 0, sizeof(p_cr));
    p_cb1 = 0; cmd = 0xff; dac = 0; cycle_debt = 0; sample_acc = 0;
    ring_w = ring_r = 0; last_sample = 0;
    m6800_reset(&cpu);
}

static inline void push_sample(void)
{
    int16_t s = (int16_t)(((int)dac - 128) * 96);
    uint32_t w = ring_w;
    if (w - ring_r >= RING) ring_r = w - RING + 1;       /* nobody is listening: drop the oldest */
    ring[w & (RING - 1)] = s;
    ring_w = w + 1;
}

void jo_sound_run(int32_t n)
{
    int32_t budget = n - cycle_debt;
    while (budget > 0) {
        int cy;
        if (pia_irq() && (!(cpu.cc & M6800_I) || cpu.wai)) cy = m6800_irq(&cpu); else cy = 0;
        if (!cy) cy = m6800_step(&cpu);
#ifdef JO_DEBUG
        { extern uint64_t jo_dbg_snd_steps; jo_dbg_snd_steps++; }
#endif
        budget -= cy;
        sample_acc += (uint32_t)cy * JO_AUDIO_RATE;
        while (sample_acc >= JO_SND_CLOCK) { sample_acc -= JO_SND_CLOCK; push_sample(); }
    }
    cycle_debt = -budget;
}

void jo_render_audio(int16_t *buf, int samples, int rate)
{
    /* the DAC's output is capacitor-coupled: a one-pole DC blocker stands in for that */
    static int32_t hp_x, hp_y;
    (void)rate;                                            /* JO_AUDIO_RATE, by construction */
    for (int i = 0; i < samples; i++) {
        if (ring_r != ring_w) { last_sample = ring[ring_r & (RING - 1)]; ring_r++; }
        int32_t x = last_sample;
        hp_y = x - hp_x + hp_y - (hp_y >> 7);
        hp_x = x;
        buf[i] = (int16_t)(hp_y > 32767 ? 32767 : (hp_y < -32768 ? -32768 : hp_y));
    }
}

uint16_t jo_snd_pc(void) { return cpu.pc; }
