/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * timeplt.c - Konami Time Pilot board: a Z80 for the game, a second Z80 and two AY-3-8910s
 * for the sound (the Pooyan sound board), and an LS259 of control latches.
 * The Z80 is Marat Fayzullin's portable core (see THIRD_PARTY_NOTICES.md).
 */
#include "timeplt_internal.h"
#include "ay8910.h"
#include "Z80.h"
#include <string.h>

tp_roms_t tp_roms;
uint8_t tp_vram[0x400], tp_cram[0x400];
uint8_t tp_spr0[0x100], tp_spr1[0x100];
uint8_t tp_flip, tp_video_enable;

static uint8_t ram[0x800];           /* 0xA800-0xAFFF */
static uint8_t snd_ram[0x400];       /* 0x3000-0x33FF on the sound board */
static uint8_t dsw0 = 0xff;          /* 1 coin 1 play on both mechs */
static uint8_t dsw1 = 0x4b;          /* 3 lives, upright, bonus at 10000 and 50000, difficulty 4, demo sounds */
static tp_input_t input;
static uint32_t frame_count;
static int nmi_enable, snd_irq_pending, snd_trigger;
static uint8_t sound_latch;
static int scanline;
static int32_t debt[2];
static ay8910_t ay[2];
static uint64_t snd_total_cycles;
static Z80 cpu[2];
static int cur_cpu;

/* ---- input ports, active low ---- */
static uint8_t read_in0(void)
{
    uint8_t v = 0xff;
    if (input.coin1)  v &= (uint8_t)~0x01;
    if (input.start1) v &= (uint8_t)~0x08;
    if (input.start2) v &= (uint8_t)~0x10;
    return v;
}
static uint8_t read_in1(void)
{
    uint8_t v = 0xff;
    if (input.left)  v &= (uint8_t)~0x01;
    if (input.right) v &= (uint8_t)~0x02;
    if (input.up)    v &= (uint8_t)~0x04;
    if (input.down)  v &= (uint8_t)~0x08;
    if (input.fire)  v &= (uint8_t)~0x10;
    return v;
}

/* ---- main CPU bus ---- */
static uint8_t main_read(uint16_t a)
{
    if (a < 0x6000) return tp_roms.rom[a];
    if (a < 0xa000) return 0xff;
    if (a < 0xa400) return tp_cram[a & 0x3ff];
    if (a < 0xa800) return tp_vram[a & 0x3ff];
    if (a < 0xb000) return ram[a & 0x7ff];
    if (a < 0xc000) return (a & 0x400) ? tp_spr1[a & 0xff] : tp_spr0[a & 0xff];
    switch (a & 0xf360) {
        case 0xc000: return (uint8_t)scanline;
        case 0xc200: return dsw1;
        case 0xc300: return read_in0();
        case 0xc320: return read_in1();
        case 0xc340: return 0xff;                          /* the cocktail player */
        case 0xc360: return dsw0;
        default: return 0xff;
    }
}

static void main_write(uint16_t a, uint8_t d)
{
    if (a < 0xa000) return;
    if (a < 0xa400) { tp_cram[a & 0x3ff] = d; return; }
    if (a < 0xa800) { tp_vram[a & 0x3ff] = d; return; }
    if (a < 0xb000) { ram[a & 0x7ff] = d; return; }
    if (a < 0xc000) { if (a & 0x400) tp_spr1[a & 0xff] = d; else tp_spr0[a & 0xff] = d; return; }
    if ((a & 0xf300) == 0xc000) { sound_latch = d; return; }
    if ((a & 0xf300) == 0xc200) return;                    /* watchdog */
    if ((a & 0xf3f0) == 0xc300) {                          /* the LS259: one bit per pair of addresses */
        int bit = (a >> 1) & 7, on = d & 1;
        switch (bit) {
            case 0: nmi_enable = on; break;
            case 1: tp_flip = !on; break;
            case 2:                                        /* a rising edge interrupts the sound CPU */
                if (on && !snd_trigger) snd_irq_pending = 1;
                snd_trigger = on;
                break;
            case 4: tp_video_enable = on; break;
            default: break;                                /* mute, coin counters, pay-out */
        }
    }
}

/* ---- sound CPU bus ---- */
static uint8_t snd_read(uint16_t a)
{
    switch (a >> 12) {
        case 0: return tp_roms.snd[a & 0xfff];
        case 1: case 2: return 0xff;                       /* the rest of the ROM space is empty */
        case 3: return snd_ram[a & 0x3ff];
        case 4: return ay_data_r(&ay[0]);
        case 6: return ay_data_r(&ay[1]);
        default: return 0xff;
    }
}
static void snd_write(uint16_t a, uint8_t d)
{
    switch (a >> 12) {
        case 3: snd_ram[a & 0x3ff] = d; break;
        case 4: ay_data_w(&ay[0], d); break;
        case 5: ay_address_w(&ay[0], d); break;
        case 6: ay_data_w(&ay[1], d); break;
        case 7: ay_address_w(&ay[1], d); break;
        default: break;                                    /* 0x8000 up: the output filters */
    }
}

/* ---- Z80 callbacks: the core's are global, so cur_cpu picks the map ---- */
byte RdZ80(register word a) { return cur_cpu ? snd_read(a) : main_read(a); }
void WrZ80(register word a, register byte d) { if (cur_cpu) snd_write(a, d); else main_write(a, d); }
byte InZ80(register word p) { (void)p; return 0xff; }
void OutZ80(register word p, register byte v) { (void)p; (void)v; }
void PatchZ80(register Z80 *R) { (void)R; }
word LoopZ80(register Z80 *R) { (void)R; return INT_QUIT; }

/*
 * AY 1's port A is the sound latch. Port B is a timer: the sound clock divided by 512 and
 * then by ten through an LS90, read on the top four bits in this order.
 */
static uint8_t ay_port_read(void *ctx, int port)
{
    (void)ctx;
    static const uint8_t timer[10] = { 0x00, 0x10, 0x20, 0x30, 0x40, 0x90, 0xa0, 0xb0, 0xa0, 0xd0 };
    if (port == 0) return sound_latch;
    return timer[(snd_total_cycles / 512) % 10];
}

/* ---- public ---- */
void tp_reset(void)
{
    memset(ram, 0, sizeof(ram));
    memset(snd_ram, 0, sizeof(snd_ram));
    memset(tp_vram, 0, sizeof(tp_vram));
    memset(tp_cram, 0, sizeof(tp_cram));
    memset(tp_spr0, 0, sizeof(tp_spr0));
    memset(tp_spr1, 0, sizeof(tp_spr1));
    memset(&input, 0, sizeof(input));
    nmi_enable = 0; snd_irq_pending = 0; snd_trigger = 0; sound_latch = 0;
    tp_flip = 0; tp_video_enable = 0; scanline = 0;
    debt[0] = debt[1] = 0;
    snd_total_cycles = 0;
    ay_reset(&ay[0]); ay_reset(&ay[1]);
    for (int i = 0; i < 2; i++) { cur_cpu = i; ResetZ80(&cpu[i]); }
    cur_cpu = 0;
}

void tp_init(const tp_roms_t *r)
{
    tp_roms = *r;
    ay_init(&ay[0], TP_SND_CLOCK, ay_port_read, NULL);
    ay_init(&ay[1], TP_SND_CLOCK, NULL, NULL);
    tp_video_init();
    for (int i = 0; i < 2; i++) { memset(&cpu[i], 0, sizeof(cpu[i])); cpu[i].IPeriod = 1000000; }
    tp_reset();
}

void tp_set_dips(uint8_t d0, uint8_t d1) { dsw0 = d0; dsw1 = d1; }
tp_input_t *tp_input(void) { return &input; }

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

void tp_run_frame(void)
{
    enum { SLICES = 16 };
    for (int s = 0; s < SLICES; s++) {
        scanline = s * 256 / SLICES;                       /* the program reads the beam to double its clouds */
        run_cpu(0, TP_MAIN_CYCLES_PER_FRAME / SLICES);
        if (snd_irq_pending && (cpu[1].IFF & IFF_1)) {
            cur_cpu = 1;
            IntZ80(&cpu[1], INT_IRQ);
            snd_irq_pending = 0;
        }
        run_cpu(1, TP_SND_CYCLES_PER_FRAME / SLICES);
        snd_total_cycles += TP_SND_CYCLES_PER_FRAME / SLICES;
    }
    if (nmi_enable) { cur_cpu = 0; IntZ80(&cpu[0], INT_NMI); }
    frame_count++;
}

void tp_render_audio(int16_t *buf, int samples, int rate)
{
    static int16_t tmp[1024];
    while (samples > 0) {
        int n = samples > 1024 ? 1024 : samples;
        memset(buf, 0, (size_t)n * sizeof(int16_t));        /* the AY mixer adds into its buffer */
        memset(tmp, 0, (size_t)n * sizeof(int16_t));
        ay_render(&ay[0], buf, n, rate);
        ay_render(&ay[1], tmp, n, rate);
        for (int i = 0; i < n; i++) {
            int32_t v = ((int32_t)buf[i] + tmp[i]) * 3;
            buf[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
        }
        buf += n; samples -= n;
    }
}

uint16_t tp_pc(void) { return cpu[0].PC.W; }
uint16_t tp_snd_pc(void) { return cpu[1].PC.W; }
uint32_t tp_frame_count(void) { return frame_count; }

/* the byte at a CPU address, where that is memory - for keeping the high scores */
uint8_t *tp_mem(uint16_t a)
{
    if (a >= 0xa800 && a < 0xb000) return &ram[a & 0x7ff];
    return 0;
}
