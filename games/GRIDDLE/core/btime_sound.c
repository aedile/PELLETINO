/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * btime_sound.c - the sound board: a 6502 at 500 kHz and two AY-3-8910s. The main CPU's
 * command latch interrupts it; an NMI every sixteen scanlines, gated by an enable it writes,
 * paces its music player. The output filters are not modelled.
 */
#include "btime_internal.h"
#include "ay8910.h"
#include <string.h>

static uint8_t ram[0x400];
static uint8_t latch;
static int latch_pending, nmi_enable;
static int32_t cycle_debt;
static ay8910_t ay[2];

static uint8_t bus_read(uint16_t a)
{
    if (a < 0x2000) return ram[a & 0x3ff];
    if (a >= 0xe000) return bt_roms.snd[a & 0xfff];
    if (a >= 0xa000 && a < 0xc000) { latch_pending = 0; return latch; }
    return 0xff;
}

static void bus_write(uint16_t a, uint8_t d)
{
    if (a < 0x2000) { ram[a & 0x3ff] = d; return; }
    switch (a >> 13) {
        case 1: ay_data_w(&ay[0], d); break;                 /* 0x2000 */
        case 2: ay_address_w(&ay[0], d); break;              /* 0x4000 */
        case 3: ay_data_w(&ay[1], d); break;                 /* 0x6000 */
        case 4: ay_address_w(&ay[1], d); break;              /* 0x8000 */
        case 6: nmi_enable = d & 1; break;                   /* 0xC000 */
        default: break;
    }
}

#define M6502F_READ(a)     bus_read(a)
#define M6502F_WRITE(a, v) bus_write((a), (v))
#include "m6502fast.h"

static m6502f_t cpu;

void bt_sound_latch_w(uint8_t d) { latch = d; latch_pending = 1; }

void bt_sound_init(void)
{
    ay_init(&ay[0], BT_AY_CLOCK, NULL, NULL);
    ay_init(&ay[1], BT_AY_CLOCK, NULL, NULL);
}

void bt_sound_reset(void)
{
    memset(ram, 0, sizeof(ram));
    latch = 0; latch_pending = 0; nmi_enable = 0; cycle_debt = 0;
    ay_reset(&ay[0]); ay_reset(&ay[1]);
    m6502f_reset(&cpu);
}

/* run the sound CPU for a slice; `nmi` says the 8V line rose at the start of it */
void bt_sound_run(int32_t n, int nmi)
{
    int32_t budget = n - cycle_debt;
    if (nmi && nmi_enable) budget -= m6502f_nmi(&cpu);
    while (budget > 0) {
        if (latch_pending && !(cpu.p & 0x04)) budget -= m6502f_irq(&cpu);
        budget -= m6502f_step(&cpu);
    }
    cycle_debt = -budget;
}

void bt_render_audio(int16_t *buf, int samples, int rate)
{
    static int16_t tmp[1024];
    while (samples > 0) {
        int n = samples > 1024 ? 1024 : samples;
        memset(buf, 0, (size_t)n * sizeof(int16_t));
        memset(tmp, 0, (size_t)n * sizeof(int16_t));
        ay_render(&ay[0], buf, n, rate);
        ay_render(&ay[1], tmp, n, rate);
        for (int i = 0; i < n; i++) {
            int32_t v = ((int32_t)buf[i] + tmp[i]) * 3 / 2;
            buf[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
        }
        buf += n; samples -= n;
    }
}

uint16_t bt_snd_pc(void) { return cpu.pc; }
