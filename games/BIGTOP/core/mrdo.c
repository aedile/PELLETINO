/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * mrdo.c - Universal Mr. Do! board: one Z80, two SN76489s, and a protection PAL answered
 * the way MAME answers it. The Z80 is Marat Fayzullin's portable core.
 */
#include "mrdo_internal.h"
#include "sn76489.h"
#include "Z80.h"
#include <string.h>

md_roms_t md_roms;
uint8_t md_bgram[0x800];             /* 0x8000-0x87FF: attributes, then codes at +0x400 */
uint8_t md_fgram[0x800];             /* 0x8800-0x8FFF */
uint8_t md_sprram[0x100];            /* 0x9000-0x90FF */
uint8_t md_flip, md_scrollx, md_scrolly;

static uint8_t ram[0x1000];          /* 0xE000-0xEFFF */
static uint8_t dsw1 = 0xdf;          /* easy, 3 lives, upright */
static uint8_t dsw2 = 0xff;          /* 1 coin 1 play on both mechs */
static md_input_t input;
static uint32_t frame_count;
static int irq_pending;
static int32_t debt;
static Z80 cpu;
static sn76489_t sn[2];
static int snd_rate = 20050;

/* ---- input ports, active low ---- */
static uint8_t read_p1(void)
{
    uint8_t v = 0xff;
    if (input.left)   v &= (uint8_t)~0x01;
    if (input.down)   v &= (uint8_t)~0x02;
    if (input.right)  v &= (uint8_t)~0x04;
    if (input.up)     v &= (uint8_t)~0x08;
    if (input.fire)   v &= (uint8_t)~0x10;
    if (input.start1) v &= (uint8_t)~0x20;
    if (input.start2) v &= (uint8_t)~0x40;
    return v;
}
static uint8_t read_p2(void)
{
    uint8_t v = 0xff;
    if (input.coin1) v &= (uint8_t)~0x40;
    return v;
}

/* ---- bus ---- */
byte RdZ80(register word a)
{
    if (a < 0x8000) return md_roms.rom[a];
    if (a < 0x8800) return md_bgram[a & 0x7ff];
    if (a < 0x9000) return md_fgram[a & 0x7ff];
    if (a >= 0xe000 && a < 0xf000) return ram[a & 0xfff];
    switch (a) {
        case 0x9803: return md_roms.rom[cpu.HL.W & 0x7fff];   /* the PAL: MAME's answer */
        case 0xa000: return read_p1();
        case 0xa001: return read_p2();
        case 0xa002: return dsw1;
        case 0xa003: return dsw2;
        default: return 0xff;
    }
}

void WrZ80(register word a, register byte d)
{
    if (a < 0x8000) return;
    if (a < 0x8800) { md_bgram[a & 0x7ff] = d; return; }
    if (a < 0x9000) { md_fgram[a & 0x7ff] = d; return; }
    if (a < 0x9100) { md_sprram[a & 0xff] = d; return; }
    if (a >= 0xe000 && a < 0xf000) { ram[a & 0xfff] = d; return; }
    if (a >= 0xf800) { md_scrolly = d; return; }
    if (a >= 0xf000) { md_scrollx = d; return; }
    switch (a) {
        case 0x9800: md_flip = d & 1; break;           /* bits 1-3 are playfield priority, unused */
        case 0x9801: sn76489_write(&sn[0], d); break;
        case 0x9802: sn76489_write(&sn[1], d); break;
        default: break;
    }
}

byte InZ80(register word p) { (void)p; return 0xff; }
void OutZ80(register word p, register byte v) { (void)p; (void)v; }
void PatchZ80(register Z80 *R) { (void)R; }
word LoopZ80(register Z80 *R) { (void)R; return INT_QUIT; }

/* ---- public ---- */
void md_reset(void)
{
    memset(ram, 0, sizeof(ram));
    memset(md_bgram, 0, sizeof(md_bgram));
    memset(md_fgram, 0, sizeof(md_fgram));
    memset(md_sprram, 0, sizeof(md_sprram));
    memset(&input, 0, sizeof(input));
    md_flip = md_scrollx = md_scrolly = 0;
    irq_pending = 0; debt = 0;
    sn76489_init(&sn[0], MD_CPU_CLOCK, snd_rate);
    sn76489_init(&sn[1], MD_CPU_CLOCK, snd_rate);
    ResetZ80(&cpu);
}

void md_init(const md_roms_t *r)
{
    md_roms = *r;
    md_video_init();
    memset(&cpu, 0, sizeof(cpu));
    cpu.IPeriod = 1000000;
    md_reset();
}

void md_set_dips(uint8_t d1, uint8_t d2) { dsw1 = d1; dsw2 = d2; }
md_input_t *md_input(void) { return &input; }

static void run_cpu(int32_t cycles)
{
    cycles -= debt;
    debt = 0;
    if (cycles <= 0) { debt = -cycles; return; }
    cpu.IPeriod = cycles;
    cpu.ICount = cycles;
    RunZ80(&cpu);
    int32_t overshoot = cycles - cpu.ICount;
    if (overshoot > 0) debt = overshoot;
}

void md_run_frame(void)
{
    /* the vblank interrupt is held on the line until the CPU takes it */
    enum { SLICES = 8 };
    for (int s = 0; s < SLICES; s++) {
        if (irq_pending && (cpu.IFF & IFF_1)) { IntZ80(&cpu, INT_IRQ); irq_pending = 0; }
        run_cpu(MD_CYCLES_PER_FRAME / SLICES);
    }
    irq_pending = 1;
    frame_count++;
}

void md_render_audio(int16_t *buf, int samples, int rate)
{
    if (rate != snd_rate) {
        snd_rate = rate;
        sn76489_init(&sn[0], MD_CPU_CLOCK, rate);
        sn76489_init(&sn[1], MD_CPU_CLOCK, rate);
    }
    static int32_t mix[1024];
    while (samples > 0) {
        int n = samples > 1024 ? 1024 : samples;
        memset(mix, 0, (size_t)n * sizeof(int32_t));
        sn76489_render(&sn[0], mix, n);
        sn76489_render(&sn[1], mix, n);
        for (int i = 0; i < n; i++) {
            int32_t v = mix[i];
            buf[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
        }
        buf += n; samples -= n;
    }
}

uint16_t md_pc(void) { return cpu.PC.W; }
uint32_t md_frame_count(void) { return frame_count; }

/* the byte at a CPU address, where that is memory - for keeping the high scores */
uint8_t *md_mem(uint16_t a)
{
    if (a >= 0xe000 && a < 0xf000) return &ram[a & 0xfff];
    return 0;
}
