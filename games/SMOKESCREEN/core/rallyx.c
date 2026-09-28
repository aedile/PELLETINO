/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * rallyx.c - Namco Rally-X board: Z80, memory map, the LS259 main latch and interrupt timing.
 * The Z80 is Marat Fayzullin's portable core (see THIRD_PARTY_NOTICES.md).
 */
#include "rallyx_internal.h"
#include "Z80.h"
#include <string.h>

rx_roms_t rx_roms;
uint8_t rx_vram[0x1000];
uint8_t rx_radarattr[0x10];
uint8_t rx_scrollx, rx_scrolly, rx_flip;

static uint8_t ram[0x800];               /* 0x9800-0x9FFF */
static uint8_t dsw = 0xcb;               /* factory: 1 coin 1 credit, normal, bonus at 20k */
static rx_input_t input;
static uint32_t frame_count;
static int irq_mask, sound_on;
static uint8_t irq_vector = 0xff;
static int32_t debt;
static int irq_pending;
static Z80 cpu;
uint8_t rx_dbg_vector; int rx_dbg_mask, rx_dbg_im;

/* ---- input ports, all active low ---- */
static uint8_t read_p1(void)
{
    uint8_t v = 0xff;
    if (input.smoke) v &= (uint8_t)~0x02;
    if (input.left)  v &= (uint8_t)~0x04;
    if (input.right) v &= (uint8_t)~0x08;
    if (input.down)  v &= (uint8_t)~0x10;
    if (input.up)    v &= (uint8_t)~0x20;
    if (input.start1) v &= (uint8_t)~0x40;
    if (input.coin1)  v &= (uint8_t)~0x80;
    return v;
}
static uint8_t read_p2(void)
{
    uint8_t v = 0xff;
    if (input.start2) v &= (uint8_t)~0x40;
    return v;
}

/* ---- bus ---- */
byte RdZ80(register word a)
{
    if (a < 0x4000) return rx_roms.rom[a];
    if (a >= 0x8000 && a < 0x9000) return rx_vram[a & 0xfff];
    if (a >= 0x9800 && a < 0xa000) return ram[a & 0x7ff];
    switch (a & 0xff80) {
        case 0xa000: return read_p1();
        case 0xa080: return read_p2();
        case 0xa100: return dsw;
        default: return 0x00;      /* MAME leaves this map's unmapped reads low */
    }
}

void WrZ80(register word a, register byte d)
{
    if (a >= 0x8000 && a < 0x9000) { rx_vram[a & 0xfff] = d; return; }
    if (a >= 0x9800 && a < 0xa000) { ram[a & 0x7ff] = d; return; }
    if (a < 0xa000) return;
    if (a < 0xa010) { rx_radarattr[a & 0x0f] = d; return; }
    if (a >= 0xa100 && a < 0xa120) { rx_wsg_write(a & 0x1f, d); return; }
    switch (a) {
        case 0xa080: return;                       /* watchdog */
        case 0xa130: rx_scrollx = d; return;
        case 0xa140: rx_scrolly = d; return;
        default: break;
    }
    if (a >= 0xa180 && a < 0xa188) {               /* LS259 main latch, one bit per address */
        int bit = d & 1;
        switch (a & 7) {
            case 0: rx_bang(bit); break;           /* the discrete explosion */
            case 1: irq_mask = bit; break;
            case 2: sound_on = bit; break;
            case 3: rx_flip = bit; break;
            default: break;                        /* lamps, coin lockout, coin counter */
        }
    }
}

/* the game supplies its own interrupt vector through port 0 */
byte InZ80(register word p) { (void)p; return 0xff; }
void OutZ80(register word p, register byte v) { if ((p & 0xff) == 0) irq_vector = v; }
void PatchZ80(register Z80 *R) { (void)R; }
word LoopZ80(register Z80 *R) { (void)R; return INT_QUIT; }

/* ---- public ---- */
void rx_reset(void)
{
    memset(ram, 0, sizeof(ram));
    memset(rx_vram, 0, sizeof(rx_vram));
    memset(rx_radarattr, 0, sizeof(rx_radarattr));
    memset(&input, 0, sizeof(input));
    rx_scrollx = rx_scrolly = rx_flip = 0;
    irq_mask = sound_on = 0; irq_vector = 0xff; debt = 0; irq_pending = 0;
    rx_wsg_reset();
    ResetZ80(&cpu);
}

void rx_init(const rx_roms_t *r)
{
    rx_roms = *r;
    memset(&cpu, 0, sizeof(cpu));
    cpu.IPeriod = 1000000;
    rx_wsg_init(rx_roms.wave);
    rx_video_init();
    rx_reset();
}

void rx_set_dips(uint8_t d) { dsw = d; }
rx_input_t *rx_input(void) { return &input; }

/* run the CPU for `cycles`, carrying any overshoot into the next slice */
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

void rx_run_frame(void)
{
    /*
     * The vblank interrupt is a level on a wire: it stays asserted until the CPU acknowledges
     * it. Raising it once at the end of a frame and calling the CPU immediately would throw it
     * away whenever the game happened to have interrupts disabled at that instant - and the
     * game does disable them, at which point it loses a frame tick, runs off into memory it
     * never meant to execute, and ends up somewhere like an RST vector. So it is held pending
     * and delivered at the first slice boundary where the CPU will take it.
     */
    enum { SLICES = 8 };
    for (int s = 0; s < SLICES; s++) {
        if (irq_pending && (cpu.IFF & IFF_1)) {
            IntZ80(&cpu, irq_vector);
            irq_pending = 0;
        }
        run_cpu(RX_CYCLES_PER_FRAME / SLICES);
    }
    if (irq_mask) irq_pending = 1;         /* vblank */
    frame_count++;
}

void rx_render_audio(int16_t *buf, int samples, int rate)
{
    memset(buf, 0, (size_t)samples * sizeof(int16_t));
    if (!sound_on) return;
    rx_wsg_render(buf, samples, rate);
    rx_bang_render(buf, samples, rate);
}

uint16_t rx_pc(void) { return cpu.PC.W; }
uint32_t rx_frame_count(void) { return frame_count; }
const uint8_t *rx_ram(void) { return ram; }
