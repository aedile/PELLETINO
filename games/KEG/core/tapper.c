/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * tapper.c - Bally Midway MCR (91490 CPU board) and the SSIO sound board: two Z80s, a Z80 CTC
 * that paces the game with IM2 interrupts, and two AY-3-8910s behind four data latches.
 * The Z80 is Marat Fayzullin's portable core; both CPUs share its global callbacks, so a
 * selector says whose bus a call belongs to.
 */
#include "tapper_internal.h"
#include "ay8910.h"
#include "Z80.h"
#include <string.h>

tap_roms_t tap_roms;
uint8_t tap_vram[0x800];
uint8_t tap_sprram[0x200];
uint8_t tap_palram[0x80];
uint8_t tap_flip;

static uint8_t ram[0x800];           /* 0xE000-0xE7FF (battery backed on the board) */
static uint8_t snd_ram[0x400];       /* 0x8000-0x83FF on the SSIO */
static uint8_t dip3 = 0xc0;          /* demo sounds on, upright, coin meters */
static tap_input_t input;
static uint32_t frame_count;
static int32_t debt[2];
static Z80 cpu[2];
static int cur_cpu;

/* ---- the Z80 CTC ---- */
typedef struct {
    uint8_t ctl, tc, count;
    int want_tc, running, pending;
} ctc_ch_t;
static ctc_ch_t ctc[4];
static uint8_t ctc_vector;
static uint32_t ctc_pre[4];          /* prescaler accumulators */

/* ---- the SSIO ---- */
static uint8_t ssio_data[4], ssio_status;
static uint32_t ssio_ticks;          /* the 14024's count, at 50 kHz */
static uint32_t ssio_tick_acc;
static int ssio_irq;
static ay8910_t ay[2];
static int ssio_mute;

/* ---- input ports, active low ---- */
static uint8_t read_ip0(void)
{
    uint8_t v = 0xff;
    if (input.coin1)  v &= (uint8_t)~0x01;
    if (input.start1) v &= (uint8_t)~0x04;
    if (input.start2) v &= (uint8_t)~0x08;
    return v;
}
static uint8_t read_ip1(void)
{
    uint8_t v = 0xff;
    if (input.right) v &= (uint8_t)~0x01;
    if (input.left)  v &= (uint8_t)~0x02;
    if (input.down)  v &= (uint8_t)~0x04;
    if (input.up)    v &= (uint8_t)~0x08;
    if (input.fire)  v &= (uint8_t)~0x10;
    return v;
}

/* ---- CTC ---- */
static void ctc_write(int ch, uint8_t d)
{
    ctc_ch_t *c = &ctc[ch];
    if (c->want_tc) {                                      /* the time constant that follows a control word */
        c->tc = d;
        c->want_tc = 0;
        c->count = d;
        c->running = !(c->ctl & 0x40) ? !(c->ctl & 0x08) : 1;   /* a timer with no trigger starts at once */
        ctc_pre[ch] = 0;
        return;
    }
    if (d & 0x01) {                                        /* a control word */
        c->ctl = d;
        if (d & 0x02) { c->running = 0; c->pending = 0; }  /* reset */
        if (d & 0x04) c->want_tc = 1;
        else if (!(d & 0x02) && !(d & 0x40) && !(d & 0x08)) c->running = 1;
    } else if (ch == 0) {
        ctc_vector = (uint8_t)(d & 0xf8);                  /* the interrupt vector, on channel 0 only */
    }
}

static uint8_t ctc_read(int ch) { return ctc[ch].count; }

static void ctc_zero(int ch)
{
    ctc_ch_t *c = &ctc[ch];
    c->count = c->tc;
    if (c->ctl & 0x80) c->pending = 1;
    if (ch == 0) ctc_trigger(1);                           /* ZC/TO 0 is wired to TRG 1 */
}

void ctc_trigger(int ch)
{
    ctc_ch_t *c = &ctc[ch];
    if (!(c->ctl & 0x40)) {                                /* timer mode: a trigger starts it */
        if (!c->running && (c->ctl & 0x08) && c->tc) { c->running = 1; c->count = c->tc; ctc_pre[ch] = 0; }
        return;
    }
    if (!c->running) return;
    if (--c->count == 0) ctc_zero(ch);
}

static void ctc_run(int32_t cycles)
{
    for (int ch = 0; ch < 4; ch++) {
        ctc_ch_t *c = &ctc[ch];
        if (!c->running || (c->ctl & 0x40)) continue;      /* counters are clocked by their triggers */
        uint32_t pre = (c->ctl & 0x20) ? 256 : 16;
        ctc_pre[ch] += (uint32_t)cycles;
        while (ctc_pre[ch] >= pre) {
            ctc_pre[ch] -= pre;
            if (--c->count == 0) ctc_zero(ch);
        }
    }
}

/* ---- main CPU bus ---- */
static uint8_t main_read(uint16_t a)
{
    if (a < 0xe000) return tap_roms.rom[a];
    if (a < 0xe800) return ram[a & 0x7ff];
    if (a < 0xf000) return tap_sprram[a & 0x1ff];
    if (a < 0xf800) return tap_vram[a & 0x7ff];
    return tap_palram[a & 0x7f];
}
static void main_write(uint16_t a, uint8_t d)
{
    if (a < 0xe000) return;
    if (a < 0xe800) { ram[a & 0x7ff] = d; return; }
    if (a < 0xf000) { tap_sprram[a & 0x1ff] = d; return; }
    if (a < 0xf800) { tap_vram[a & 0x7ff] = d; return; }
    tap_palram[a & 0x7f] = d;
}
static uint8_t main_in(uint8_t p)
{
    if ((p & 0xe7) <= 0x04) {                              /* the SSIO's input ports, mirrored */
        switch (p & 0x07) {
            case 0: return read_ip0();
            case 1: return read_ip1();
            case 2: return 0xff;                           /* the cocktail player */
            case 3: return dip3;
            case 4: return 0xff;
        }
    }
    if ((p & 0xe7) == 0x07) return ssio_status;
    if (p >= 0xf0 && p <= 0xf3) return ctc_read(p & 3);
    return 0xff;
}
static void main_out(uint8_t p, uint8_t d)
{
    if (p <= 0x07) { if (p < 4) tap_flip = (d >> 6) & 1; return; }   /* the control port: LEDs, meters, flip */
    if (p >= 0x1c && p <= 0x1f) { ssio_data[p & 3] = d; return; }
    if (p >= 0xf0 && p <= 0xf3) { ctc_write(p & 3, d); return; }
}

/* ---- SSIO bus ---- */
static uint8_t snd_read(uint16_t a)
{
    switch (a >> 12) {
        case 0: case 1: case 2: case 3: return tap_roms.snd[a];
        case 8: return snd_ram[a & 0x3ff];
        case 9: return ssio_data[a & 3];
        case 0xa: return (a & 3) == 1 ? ay_data_r(&ay[0]) : 0xff;
        case 0xb: return (a & 3) == 1 ? ay_data_r(&ay[1]) : 0xff;
        case 0xe: ssio_ticks = 0; ssio_irq = 0; return 0xff;   /* interrupt acknowledge */
        case 0xf: return 0xff;                             /* the board's own DIPs */
        default: return 0xff;
    }
}
static void snd_write(uint16_t a, uint8_t d)
{
    switch (a >> 12) {
        case 8: snd_ram[a & 0x3ff] = d; break;
        case 0xa: if ((a & 3) == 0) ay_address_w(&ay[0], d); else if ((a & 3) == 2) ay_data_w(&ay[0], d); break;
        case 0xb: if ((a & 3) == 0) ay_address_w(&ay[1], d); else if ((a & 3) == 2) ay_data_w(&ay[1], d); break;
        case 0xc: ssio_status = d; break;
        default: break;                                    /* the LED */
    }
}

/* ---- Z80 callbacks ---- */
byte RdZ80(register word a) { return cur_cpu ? snd_read(a) : main_read(a); }
void WrZ80(register word a, register byte d) { if (cur_cpu) snd_write(a, d); else main_write(a, d); }
byte InZ80(register word p) { return cur_cpu ? 0xff : main_in((uint8_t)p); }
void OutZ80(register word p, register byte v) { if (!cur_cpu) main_out((uint8_t)p, v); }
void PatchZ80(register Z80 *R) { (void)R; }
word LoopZ80(register Z80 *R) { (void)R; return INT_QUIT; }

/* the AYs' ports drive the volume modulators; only the mute matters to us */
static void ay_port_write(void *ctx, int port, uint8_t v)
{
    if (ctx == &ay[1] && port == 1) ssio_mute = v & 0x80;
}

/* ---- public ---- */
void tap_reset(void)
{
    memset(ram, 0xff, sizeof(ram));
    memset(snd_ram, 0, sizeof(snd_ram));
    memset(tap_vram, 0, sizeof(tap_vram));
    memset(tap_sprram, 0, sizeof(tap_sprram));
    memset(tap_palram, 0, sizeof(tap_palram));
    memset(&input, 0, sizeof(input));
    memset(ctc, 0, sizeof(ctc)); memset(ctc_pre, 0, sizeof(ctc_pre)); ctc_vector = 0;
    memset(ssio_data, 0, sizeof(ssio_data)); ssio_status = 0; ssio_ticks = 0; ssio_tick_acc = 0; ssio_irq = 0; ssio_mute = 0;
    tap_flip = 0;
    debt[0] = debt[1] = 0;
    ay_reset(&ay[0]); ay_reset(&ay[1]);
    for (int i = 0; i < 2; i++) {
        cur_cpu = i;
        ResetZ80(&cpu[i]);
        /* a real Z80 comes out of reset with the index and stack registers all ones, and the
         * boot code leans on that: it compares IX against BC to decide whether a table copy
         * is wanted, and with IX at zero it copies a large slab of ROM over the stack */
        cpu[i].IX.W = cpu[i].IY.W = 0xffff;
        cpu[i].SP.W = 0xffff;
        cpu[i].AF.W = 0xffff;
    }
    cur_cpu = 0;
}

void tap_init(const tap_roms_t *r)
{
    tap_roms = *r;
    ay_init(&ay[0], TAP_SND_CLOCK, NULL, NULL);
    ay_init(&ay[1], TAP_SND_CLOCK, NULL, NULL);
    ay_set_port_write(&ay[0], ay_port_write, &ay[0]);
    ay_set_port_write(&ay[1], ay_port_write, &ay[1]);
    tap_video_init();
    for (int i = 0; i < 2; i++) { memset(&cpu[i], 0, sizeof(cpu[i])); cpu[i].IPeriod = 1000000; }
    tap_reset();
}

void tap_set_dips(uint8_t d3) { dip3 = d3; }
tap_input_t *tap_input(void) { return &input; }

static void run_cpu(int i, int32_t cycles)
{
    cycles -= debt[i];
    debt[i] = 0;
    if (cycles <= 0) { debt[i] = -cycles; return; }
    cur_cpu = i;
    cpu[i].IPeriod = cycles;
    cpu[i].ICount = cycles;
    RunZ80(&cpu[i]);
    int32_t overshoot = cycles - cpu[i].ICount;
    if (overshoot > 0) debt[i] = overshoot;
}

void tap_run_frame(void)
{
    /* one 60 Hz field: the CTC's channel 2 is triggered by every vblank, channel 3 by every
     * other one (the display is interlaced at 30 Hz) */
    enum { SLICES = 16 };
    ctc_trigger(2);
    if (!(frame_count & 1)) ctc_trigger(3);
    for (int s = 0; s < SLICES; s++) {
        /* the CTC's interrupts, lowest channel first, in mode 2 */
        if (cpu[0].IFF & IFF_1)
            for (int ch = 0; ch < 4; ch++)
                if (ctc[ch].pending) {
                    ctc[ch].pending = 0;
                    cur_cpu = 0;
                    IntZ80(&cpu[0], (word)(ctc_vector | (ch << 1)));
                    break;
                }
        run_cpu(0, TAP_MAIN_CYCLES_PER_FRAME / SLICES);
        ctc_run(TAP_MAIN_CYCLES_PER_FRAME / SLICES);

        /* the SSIO's 14024 counts at 50 kHz and holds the interrupt line for 64 counts in
         * every 128 until the program acknowledges it */
        ssio_tick_acc += TAP_SSIO_TICKS_PER_FRAME;
        uint32_t t = ssio_tick_acc / SLICES; ssio_tick_acc -= t * SLICES;
        while (t--) {
            ssio_ticks = (ssio_ticks + 1) & 0x7f;
            if ((ssio_ticks & 0x3f) == 0) ssio_irq = (ssio_ticks & 0x40) ? 1 : 0;
        }
        if (ssio_irq && (cpu[1].IFF & IFF_1)) { cur_cpu = 1; IntZ80(&cpu[1], INT_IRQ); }
        run_cpu(1, TAP_SND_CYCLES_PER_FRAME / SLICES);
    }
    frame_count++;
}

void tap_render_audio(int16_t *buf, int samples, int rate)
{
    static int16_t tmp[1024];
    while (samples > 0) {
        int n = samples > 1024 ? 1024 : samples;
        memset(buf, 0, (size_t)n * sizeof(int16_t));
        memset(tmp, 0, (size_t)n * sizeof(int16_t));
        if (!ssio_mute) {
            ay_render(&ay[0], buf, n, rate);
            ay_render(&ay[1], tmp, n, rate);
        }
        for (int i = 0; i < n; i++) {
            int32_t v = (int32_t)buf[i] + tmp[i];
            buf[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
        }
        buf += n; samples -= n;
    }
}

uint16_t tap_pc(void) { return cpu[0].PC.W; }
uint16_t tap_snd_pc(void) { return cpu[1].PC.W; }
uint32_t tap_frame_count(void) { return frame_count; }

/* the byte at a CPU address, where that is memory - for keeping the high scores */
uint8_t *tap_mem(uint16_t a)
{
    if (a >= 0xe000 && a < 0xe800) return &ram[a & 0x7ff];
    return 0;
}
