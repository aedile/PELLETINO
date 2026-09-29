/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * tempest.c - Atari Tempest: memory map, the two POKEYs' pot-wired inputs, the math box,
 * the EAROM, and the interleave between the 6502 and the vector generator.
 *
 * Timing and memory map follow MAME's tempest.cpp.
 */
#include "tempest.h"
#include "pokey.h"
#include "mathbox.h"
#include <string.h>

static tp_roms_t roms;
static uint8_t ram[0x800];            /* 0x0000-0x07FF */
static uint8_t vram[0x1000];          /* 0x2000-0x2FFF, the vector list the AVG walks */
static uint8_t colorram[16];          /* 0x0800-0x080F, write only */
static uint8_t dsw1 = 0x00, dsw2 = 0x00;
static tp_input_t input;
static pokey_t pokey1, pokey2;
static mathbox_t mathbox;
static avg_t avg;

static uint32_t total_cycles, frame_count;
static int irq_state;
static int32_t irq_acc;               /* cycles until the next periodic IRQ */
static int player_select;             /* the FLIP bit: which spinner and buttons are live */
static int frame_points;              /* vectors in the last completed frame */
static int list_complete;             /* that frame is finished; start a new list next time */

/* the spinner is a four-bit up/down counter the game reads and differences */
static uint8_t knob;

/* ---- EAROM (ER2055): 64 bytes of high score storage ---- */
#define EAROM_CK  0x01
#define EAROM_C1  0x02
#define EAROM_C2  0x04
#define EAROM_CS1 0x08
#define EAROM_CS2 0x10
static uint8_t earom[64], earom_addr, earom_data, earom_ctrl;

static void earom_update_state(void)
{
    switch (earom_ctrl & (EAROM_C1 | EAROM_C2)) {
        case 0:
            /* write. The part cannot set bits, only clear them, so this is an AND - which is
             * what makes a write without a preceding erase come out wrong, exactly as it does
             * on the board. */
            earom[earom_addr & 0x3f] &= earom_data;
            break;
        case EAROM_C2:
            earom[earom_addr & 0x3f] = 0xff;         /* erase */
            break;
        default: break;
    }
}

static void earom_set_control(uint8_t d)
{
    uint8_t old = earom_ctrl;
    uint8_t s = (uint8_t)(old & EAROM_CK);
    /* CK = bit 0, C1 = /bit 2, C2 = bit 1, CS1 = bit 3, /CS2 tied low so CS2 is always on */
    if (!((d >> 2) & 1)) s |= EAROM_C1;
    if ((d >> 1) & 1)    s |= EAROM_C2;
    if ((d >> 3) & 1)    s |= EAROM_CS1;
    s |= EAROM_CS2;
    earom_ctrl = s;
    if ((earom_ctrl & (EAROM_CS1 | EAROM_CS2)) != (EAROM_CS1 | EAROM_CS2) || earom_ctrl == old) {
        /* not selected, or nothing changed */
    } else {
        earom_update_state();
    }
    /* the clock line rides along in the same byte */
    uint8_t before = earom_ctrl;
    if (d & 1) earom_ctrl |= EAROM_CK; else earom_ctrl &= (uint8_t)~EAROM_CK;
    if ((earom_ctrl & (EAROM_CS1 | EAROM_CS2)) == (EAROM_CS1 | EAROM_CS2) &&
        earom_ctrl != before && !(d & 1)) {
        if ((earom_ctrl & EAROM_C1) == EAROM_C1) earom_data = earom[earom_addr & 0x3f];  /* read */
        earom_update_state();
    }
}

/* ---- the two input ports, as the POKEYs' pot lines see them ---- */
static uint8_t read_in1(void)
{
    /* bits 0-3 the spinner, bit 4 the cabinet switch (1 = upright) */
    return (uint8_t)((knob & 0x0f) | 0x10);
}

static uint8_t read_in2(void)
{
    uint8_t v = 0x03;                       /* difficulty: Medium1 */
    v |= 0x04;                              /* rating: the fixed 1,3,5,7,9 table */
    if (input.fire) v |= 0x08;              /* the buttons are active high through the pots */
    if (input.zap)  v |= 0x10;
    if (!input.start1) v |= 0x20;           /* start is active low */
    if (!input.start2) v |= 0x40;
    v |= 0x80;
    return v;
}

/* Each pot line reads 0 when its bit is set and 228 when it is clear, so ALLPOT hands the
 * game back a whole port at once. */
static void refresh_pots(void)
{
    uint8_t in1 = read_in1(), in2 = read_in2();
    for (int i = 0; i < 8; i++) {
        pokey_set_pot(&pokey1, i, (in1 & (1 << i)) ? 0 : 228);
        pokey_set_pot(&pokey2, i, (in2 & (1 << i)) ? 0 : 228);
    }
}

static uint8_t read_in0(void)
{
    uint8_t v = 0xff;
    if (input.coin1) v &= (uint8_t)~0x04;
    /* bit 6 is VG_HALT, active high; bit 7 is a 3 kHz square wave off the CPU clock */
    if (avg_halted(&avg)) v |= 0x40; else v &= (uint8_t)~0x40;
    if (total_cycles & 0x100) v |= 0x80; else v &= (uint8_t)~0x80;
    return v;
}

/* ---- the bus ---- */
static uint8_t bus_read(uint16_t a)
{
    if (a < 0x0800) return ram[a];
    if (a >= 0x9000) return roms.rom[a];
    if (a >= 0x3000 && a < 0x4000) return roms.vectorrom[a - 0x3000];
    if (a >= 0x2000 && a < 0x3000) return vram[a - 0x2000];
    switch (a) {
        case 0x0c00: return read_in0();
        case 0x0d00: return dsw1;
        case 0x0e00: return dsw2;
        case 0x6040: return mathbox_status_r(&mathbox);
        case 0x6050: return earom_data;
        case 0x6060: return mathbox_lo_r(&mathbox);
        case 0x6070: return mathbox_hi_r(&mathbox);
        default: break;
    }
    if (a >= 0x60c0 && a <= 0x60cf) return pokey_read(&pokey1, a & 0x0f, total_cycles);
    if (a >= 0x60d0 && a <= 0x60df) return pokey_read(&pokey2, a & 0x0f, total_cycles);
    return 0xff;
}

static void bus_write(uint16_t a, uint8_t d)
{
    if (a < 0x0800) { ram[a] = d; return; }
    if (a >= 0x0800 && a <= 0x080f) { colorram[a & 0x0f] = d; return; }
    if (a >= 0x2000 && a < 0x3000) { vram[a - 0x2000] = d; return; }
    if (a >= 0x6000 && a <= 0x603f) { earom_addr = (uint8_t)(a & 0x3f); earom_data = d; return; }
    if (a >= 0x6080 && a <= 0x609f) { mathbox_go(&mathbox, (uint8_t)(a & 0x1f), d); return; }
    if (a >= 0x60c0 && a <= 0x60cf) { pokey_write(&pokey1, a & 0x0f, d); return; }
    if (a >= 0x60d0 && a <= 0x60df) { pokey_write(&pokey2, a & 0x0f, d); return; }
    switch (a) {
        case 0x4000: return;                       /* coin counters and the video flip flags */
        case 0x4800: avg_go(&avg); return;         /* VGGO */
        case 0x5000: irq_state = 0; return;        /* watchdog clear also drops the IRQ line */
        case 0x5800: avg_reset(&avg); return;      /* VGRST */
        case 0x6040: earom_set_control(d); return;
        case 0x60e0: player_select = d & 0x04; return;   /* the two start LEDs, and FLIP */
        default: return;
    }
}

#define M6502F_READ(a)     bus_read(a)
#define M6502F_WRITE(a, v) bus_write((a), (v))
#include "m6502fast.h"
static m6502f_t cpu;

/* ---- public ---- */
void tp_reset(void)
{
    memset(ram, 0, sizeof(ram));
    memset(vram, 0, sizeof(vram));
    memset(colorram, 0, sizeof(colorram));
    memset(&input, 0, sizeof(input));
    irq_state = 0; irq_acc = 0; player_select = 0; knob = 0;
    total_cycles = 0; frame_points = 0; list_complete = 0;
    earom_addr = earom_data = earom_ctrl = 0;
    pokey_reset(&pokey1); pokey_reset(&pokey2);
    mathbox_reset(&mathbox);
    avg_reset(&avg);
    refresh_pots();
    m6502f_reset(&cpu);
}

void tp_init(const tp_roms_t *r)
{
    roms = *r;
    memset(earom, 0xff, sizeof(earom));
    pokey_init(&pokey1); pokey_init(&pokey2);
    avg_init(&avg, roms.avgprom, vram, roms.vectorrom, colorram);
    tp_reset();
}

void tp_set_dips(uint8_t a, uint8_t b) { dsw1 = a; dsw2 = b; }
tp_input_t *tp_input(void) { return &input; }

static void run_cpu(int32_t cycles)
{
    while (cycles > 0) {
        if (irq_state && !(cpu.p & M6502F_I)) {
            int cy = m6502f_irq(&cpu);
            total_cycles += (uint32_t)cy; cycles -= cy;
            continue;
        }
        int cy = m6502f_step(&cpu);
        total_cycles += (uint32_t)cy; cycles -= cy;
    }
}

void tp_run_frame(void)
{
    /*
     * The spinner is a four-bit counter that free-runs as the knob turns; the game reads it
     * and takes the difference. Handing over the whole frame's movement at once is the same
     * thing to the game as turning the knob that far in a sixtieth of a second.
     */
    knob = (uint8_t)((knob + input.spin) & 0x0f);

    /* the vectors of the last frame have been drawn by now, so the list can start over */
    if (list_complete) { avg.npoints = 0; list_complete = 0; }

    enum { SLICES = 16 };
    const int32_t per_slice = TP_CYCLES_PER_FRAME / SLICES;
    const int32_t irq_period = TP_CPU_CLOCK / TP_IRQ_HZ;
    int got_frame = 0;

    for (int s = 0; s < SLICES; s++) {
        refresh_pots();
        for (int i = 0; i < 16; i++) pokey_pot_scanline(&pokey1), pokey_pot_scanline(&pokey2);

        irq_acc -= per_slice;
        if (irq_acc <= 0) { irq_acc += irq_period; irq_state = 1; }
        run_cpu(per_slice);

        /*
         * The vector generator runs at the master clock alongside the CPU, not after it. Give
         * it the same slice of wall time and stop early if it wraps to address zero, because
         * that is the end of a frame's worth of vectors and the CPU is about to rewrite the
         * list underneath it.
         */
        if (!got_frame) {
            int done = 0;
            avg_run_frame(&avg, (uint32_t)(per_slice * 8), &done);
            if (done) { frame_points = avg.npoints; got_frame = 1; list_complete = 1; }
        }
    }
    if (!got_frame && avg.npoints > 0) frame_points = avg.npoints;
    frame_count++;
}

const avg_point_t *tp_points(int *count)
{
    if (count) *count = frame_points;
    return avg.points;
}

void tp_render_audio(int16_t *buf, int samples, int rate)
{
    memset(buf, 0, (size_t)samples * sizeof(int16_t));
    pokey_render(&pokey1, buf, samples, rate);
    pokey_render(&pokey2, buf, samples, rate);
    for (int i = 0; i < samples; i++) {
        int32_t v = buf[i] * 6;
        buf[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
    }
}

uint16_t tp_pc(void) { return cpu.pc; }
uint32_t tp_frame_count(void) { return frame_count; }
int tp_vector_count(void) { return frame_points; }

/* the byte at a CPU address, where that is memory - for keeping the high scores */
uint8_t *tp_mem(uint16_t a)
{
    if (a < 0x0800) return &ram[a];
    return 0;
}
