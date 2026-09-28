/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * mpatrol_sound.c - the M52 sound board: a 6803 whose port 1 is a data bus to two AY-3-8910s
 * and whose port 2 strobes them, an MSM5205 ADPCM chip clocked at 384 kHz whose sample clock
 * interrupts the CPU (NMI) for the next nibble, and the command latch from the main board on
 * the first AY's port A. The 6803's timer is never touched by this program; only its ports and
 * internal RAM are modelled. Moon Patrol's program holds the ADPCM chip in reset throughout
 * (it writes 0x13 to the AY port that controls it, and 0xFF to an address the board's map does
 * not route to the chip), so every sound of the game is the AYs'; the chip is modelled for
 * completeness and is silent here as it is under MAME's map.
 */
#include "mpatrol_internal.h"
#include "ay8910.h"
#include <string.h>

static uint8_t ram[0x80];
static uint8_t p1_ddr, p2_ddr, p1_out, p2_out;
static uint8_t latch = 0;
static int irq_line;
static int32_t cycle_debt;
static ay8910_t ay[2];                 /* 0 = 45M (latch on port A, ADPCM control on port B), 1 = 45L */

/* the MSM5205 */
static int msm_reset = 1, msm_presc = 96, msm_bits4 = 1;
static uint8_t msm_data;
static int32_t msm_signal, msm_step, msm_clock_acc;
static int16_t msm_out;

#define RING 4096
static int16_t ring[RING];
static volatile uint32_t ring_w, ring_r;
static uint32_t sample_acc;
static int16_t last_sample;

static const int msm_step_size[49] = {
    16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552 };
static const int msm_index_shift[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

#ifdef MP_DEBUG
uint32_t mp_dbg_msm_active, mp_dbg_msm_nonzero, mp_dbg_msm_nibbles, mp_dbg_presc_writes;
#endif
static void msm_clock(void)
{
#ifdef MP_DEBUG
    if (!msm_reset) { mp_dbg_msm_active++; if (msm_data & 7) mp_dbg_msm_nonzero++; }
#endif
    /* one sample period: take the nibble the program left, as the chip does on its own clock */
    if (msm_reset) { msm_signal = 0; msm_step = 0; msm_out = 0; return; }
    int nib = msm_bits4 ? (msm_data & 0x0f) : ((msm_data & 0x07) << 1);
    int st = msm_step_size[msm_step];
    int diff = st >> 3;
    if (nib & 4) diff += st;
    if (nib & 2) diff += st >> 1;
    if (nib & 1) diff += st >> 2;
    msm_signal += (nib & 8) ? -diff : diff;
    if (msm_signal > 2047) msm_signal = 2047; else if (msm_signal < -2048) msm_signal = -2048;
    msm_step += msm_index_shift[nib & 7];
    if (msm_step < 0) msm_step = 0; else if (msm_step > 48) msm_step = 48;
    msm_out = (int16_t)(msm_signal * 6);
}

/* AY 45M's port B drives the ADPCM chip: bits 2-3 the sample clock divider, bit 4 3-bit mode, bit 0 reset */
static void ay45m_port_write(void *ctx, int port, uint8_t v)
{
    (void)ctx;
    if (port != 1) return;
#ifdef MP_DEBUG
    mp_dbg_presc_writes++;
    { extern uint32_t mp_dbg_pb_hist[256]; mp_dbg_pb_hist[v]++; }
#endif
    static const int presc[4] = { 96, 48, 64, 96 };    /* S1 = bit 2, S2 = bit 3; both set is "external clock", never used here */
    msm_presc = presc[(v >> 2) & 3];
    msm_bits4 = (v & 0x10) != 0;
    msm_reset = v & 1;
}
static uint8_t ay45m_port_read(void *ctx, int port) { (void)ctx; return port == 0 ? latch : 0xff; }

void mp_sound_cmd(uint8_t d)
{
    latch = d;
    if (!(d & 0x80)) irq_line = 1;
}

static uint8_t port1_in(void)
{
    if (p2_out & 0x08) return ay_data_r(&ay[0]);
    if (p2_out & 0x10) return ay_data_r(&ay[1]);
    return 0xff;
}

static void port2_write(uint8_t d)
{
    /* a falling edge on bit 0 strobes port 1 into whichever AY bits 3 and 4 pick; bit 2 says address */
    if ((p2_out & 0x01) && !(d & 0x01)) {
        if (p2_out & 0x04) {
            if (p2_out & 0x08) ay_address_w(&ay[0], p1_out);
            if (p2_out & 0x10) ay_address_w(&ay[1], p1_out);
        } else {
            if (p2_out & 0x08) ay_data_w(&ay[0], p1_out);
            if (p2_out & 0x10) ay_data_w(&ay[1], p1_out);
        }
    }
    p2_out = d;
}

static uint8_t bus_read(uint16_t a)
{
    a &= 0x7fff;
    if (a >= 0x7000) return mp_roms.snd[a & 0xfff];
    if (a >= 0x80 && a < 0x100) return ram[a & 0x7f];
    switch (a) {
        case 0x00: return p1_ddr;
        case 0x01: return p2_ddr;
        case 0x02: return (uint8_t)((port1_in() & ~p1_ddr) | (p1_out & p1_ddr));
        case 0x03: return (uint8_t)(p2_out & p2_ddr);           /* the inputs of port 2 read low */
        default: return 0xff;
    }
}
static void bus_write(uint16_t a, uint8_t d)
{
    a &= 0x7fff;
    if (a >= 0x80 && a < 0x100) { ram[a & 0x7f] = d; return; }
    if (a < 0x20) {
        switch (a) {
            case 0x00: p1_ddr = d; break;
            case 0x01: p2_ddr = d; break;
            case 0x02: p1_out = d; break;
            case 0x03: port2_write(d); break;
            default: break;
        }
        return;
    }
    if (a < 0x1000) {
#ifdef MP_DEBUG
        mp_dbg_msm_nibbles++;
        { extern uint32_t mp_dbg_nib_hist[16], mp_dbg_nib_addr[8]; mp_dbg_nib_hist[d & 15]++; mp_dbg_nib_addr[(a >> 9) & 7]++; }
#endif
        if (a & 1) msm_data = d; return; }      /* the ADPCM nibble */
    if (a < 0x2000) { if (latch & 0x80) irq_line = 0; return; }
}

#define M6800_READ(a)     bus_read((uint16_t)(a))
#define M6800_WRITE(a, v) bus_write((uint16_t)(a), (v))
#include "m6800.h"

static m6800_t cpu;

void mp_sound_init(void)
{
    ay_init(&ay[0], MP_AY_CLOCK, ay45m_port_read, NULL);
    ay_set_port_write(&ay[0], ay45m_port_write, NULL);
    ay_init(&ay[1], MP_AY_CLOCK, NULL, NULL);
}

void mp_sound_reset(void)
{
    memset(ram, 0, sizeof(ram));
    p1_ddr = p2_ddr = p1_out = p2_out = 0;
    latch = 0; irq_line = 1;                                /* MAME asserts it at reset */
    cycle_debt = 0;
    msm_reset = 1; msm_presc = 96; msm_bits4 = 1; msm_data = 0; msm_signal = 0; msm_step = 0; msm_clock_acc = 0; msm_out = 0;
    ring_w = ring_r = 0; sample_acc = 0; last_sample = 0;
    ay_reset(&ay[0]); ay_reset(&ay[1]);
    m6800_reset(&cpu);
}

static inline void push_sample(void)
{
    uint32_t w = ring_w;
    if (w - ring_r >= RING) ring_r = w - RING + 1;
    ring[w & (RING - 1)] = msm_out;
    ring_w = w + 1;
}

void mp_sound_run(int32_t n)
{
    int32_t budget = n - cycle_debt;
    while (budget > 0) {
        int cy = 0;
        if (irq_line && (!(cpu.cc & M6800_I) || cpu.wai)) cy = m6800_irq(&cpu);
        if (!cy) cy = m6800_step(&cpu);
        budget -= cy;
        /* the ADPCM sample clock: 384 kHz / prescaler, in E cycles */
        msm_clock_acc += cy * MP_MSM_CLOCK;
        int32_t period = msm_presc * MP_SND_CLOCK;
        if (msm_clock_acc >= period) {
            msm_clock_acc -= period;
            msm_clock();
            budget -= m6800_nmi(&cpu);
        }
        sample_acc += (uint32_t)cy * MP_AUDIO_RATE;
        while (sample_acc >= MP_SND_CLOCK) { sample_acc -= MP_SND_CLOCK; push_sample(); }
    }
    cycle_debt = -budget;
}

void mp_render_audio(int16_t *buf, int samples, int rate)
{
    static int16_t tmp[1024];
    static int32_t lp;
    while (samples > 0) {
        int n = samples > 1024 ? 1024 : samples;
        memset(buf, 0, (size_t)n * sizeof(int16_t));
        memset(tmp, 0, (size_t)n * sizeof(int16_t));
        ay_render(&ay[0], buf, n, rate);
        ay_render(&ay[1], tmp, n, rate);
        for (int i = 0; i < n; i++) {
            if (ring_r != ring_w) { last_sample = ring[ring_r & (RING - 1)]; ring_r++; }
            lp += (last_sample - lp) >> 2;                  /* the board's low-pass, roughly */
            int32_t v = ((int32_t)buf[i] + tmp[i]) * 3 / 4 + lp;
            buf[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
        }
        buf += n; samples -= n;
    }
}

uint16_t mp_snd_pc(void) { return cpu.pc; }
