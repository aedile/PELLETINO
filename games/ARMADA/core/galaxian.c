/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * galaxian.c - Galaxian board: one Z80, a handful of latches, and the sound board's inputs.
 * The Z80 is Marat Fayzullin's portable core (see THIRD_PARTY_NOTICES.md).
 */
#include "galaxian.h"
#include "galaxian_internal.h"
#include "Z80.h"
#include <string.h>

gx_roms_t gx_roms;
uint8_t gx_vram[0x400];              /* 0x5000-0x53FF: the 32x32 character map */
uint8_t gx_oram[0x100];              /* 0x5800-0x58FF: column scroll and colours, sprites, shells */
uint8_t gx_flip_x, gx_flip_y, gx_stars_on;
uint32_t gx_stars_scroll;
uint32_t gx_frame_no;

static uint8_t ram[0x400];           /* 0x4000-0x43FF work RAM, mirrored to 0x4FFF */
static uint8_t dip_in1 = 0x00;       /* 1 coin 1 play */
static uint8_t dip_in2 = 0x04;       /* 3 lives, bonus at 7000 */
static gx_input_t input;
static int nmi_enable;
static int32_t debt;                 /* cycles a slice overran by, repaid from the next one */
static Z80 cpu;

/* ---- input ports, all active high ---- */
static uint8_t read_in0(void)
{
    uint8_t v = 0;                                   /* bit 7 clear: upright cabinet */
    if (input.coin1) v |= 0x01;
    if (input.left)  v |= 0x04;
    if (input.right) v |= 0x08;
    if (input.fire)  v |= 0x10;
    return v;
}
static uint8_t read_in1(void)
{
    uint8_t v = (uint8_t)(dip_in1 & 0xc0);
    if (input.start1) v |= 0x01;
    if (input.start2) v |= 0x02;
    return v;
}
static uint8_t read_in2(void) { return (uint8_t)(dip_in2 & 0x07); }

/* ---- the bus ---- */
byte RdZ80(register word a)
{
    if (a < 0x2800) return gx_roms.rom[a];
    if (a < 0x4000) return 0xff;                       /* the rest of the ROM space is empty */
    if (a < 0x5000) return ram[a & 0x3ff];
    if (a < 0x5800) return gx_vram[a & 0x3ff];
    if (a < 0x6000) return gx_oram[a & 0xff];
    if (a < 0x6800) return read_in0();
    if (a < 0x7000) return read_in1();
    if (a < 0x7800) return read_in2();
    return 0xff;                                       /* watchdog reset on read */
}

void WrZ80(register word a, register byte d)
{
    if (a < 0x4000) return;
    if (a < 0x5000) { ram[a & 0x3ff] = d; return; }
    if (a < 0x5800) { gx_vram[a & 0x3ff] = d; return; }
    if (a < 0x6000) { gx_oram[a & 0xff] = d; return; }
    if (a < 0x6800) {                                  /* lamps, coin lockout, and the LFO bits */
        int r = a & 7;
        if (r >= 4) gx_sound_write(0x10 + (r - 4), d & 1);
        return;
    }
    if (a < 0x7000) { gx_sound_write(a & 7, d & 1); return; }
    if (a < 0x7800) {
        switch (a & 7) {
            case 1: nmi_enable = d & 1; break;
            case 4: gx_stars_on = d & 1; break;
            case 6: gx_flip_x = d & 1; break;
            case 7: gx_flip_y = d & 1; break;
            default: break;
        }
        return;
    }
    gx_sound_write(0x20, d);                           /* pitch */
}

byte InZ80(register word p) { (void)p; return 0xff; }
void OutZ80(register word p, register byte v) { (void)p; (void)v; }
void PatchZ80(register Z80 *R) { (void)R; }
/* RunZ80 calls this every IPeriod cycles; quitting there is how we get a bounded slice */
word LoopZ80(register Z80 *R) { (void)R; return INT_QUIT; }

/* ---- public ---- */
void gx_reset(void)
{
    memset(ram, 0, sizeof(ram));
    memset(gx_vram, 0, sizeof(gx_vram));
    memset(gx_oram, 0, sizeof(gx_oram));
    memset(&input, 0, sizeof(input));
    nmi_enable = 0; debt = 0;
    gx_flip_x = gx_flip_y = gx_stars_on = 0;
    gx_stars_scroll = 0;
    gx_sound_reset();
    ResetZ80(&cpu);
}

void gx_init(const gx_roms_t *r)
{
    gx_roms = *r;
    gx_video_init();
    memset(&cpu, 0, sizeof(cpu));
    cpu.IPeriod = 1000000;
    gx_reset();
}

void gx_set_dips(uint8_t in1, uint8_t in2) { dip_in1 = in1; dip_in2 = in2; }
gx_input_t *gx_input(void) { return &input; }

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

void gx_run_frame(void)
{
    run_cpu(GX_CYCLES_PER_FRAME);
    /* vblank drives an NMI, gated by the enable latch at 0x7001 */
    if (nmi_enable) IntZ80(&cpu, INT_NMI);
    /* the starfield drifts a line a frame */
    gx_stars_scroll++;
    gx_frame_no++;
}

uint16_t gx_pc(void) { return cpu.PC.W; }
uint32_t gx_frame_count(void) { return gx_frame_no; }
const uint8_t *gx_ram(void) { return ram; }
