/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * invaders.c - Space Invaders board: an 8080, a shift register, and a frame of bits.
 * The CPU is Marat Fayzullin's portable Z80 core (see THIRD_PARTY_NOTICES.md).
 */
#include "invaders.h"
#include "Z80.h"
#include <string.h>

static const uint8_t *rom;
static uint8_t ram[0x2000];          /* 0x2000-0x3FFF: 1 KB of work RAM, then the frame at 0x2400 */
static uint8_t dip_in2;              /* 3 lives, bonus at 1500, coin info shown */
static si_input_t input;
static uint32_t frame_count;
static Z80 cpu;
static int32_t debt;                 /* cycles a slice overran by, repaid from the next one */
static uint16_t shift_reg;
static uint8_t shift_amt;

/* ---- the bus: ROM below 0x2000, RAM above it, and the RAM again at 0x4000 ---- */
byte RdZ80(register word a) { return a < 0x2000 ? rom[a] : ram[(a - 0x2000) & 0x1fff]; }
void WrZ80(register word a, register byte d) { if (a >= 0x2000) ram[(a - 0x2000) & 0x1fff] = d; }

/* ---- I/O ports; every input is active high on this board ---- */
static uint8_t read_in1(void)
{
    uint8_t v = 0x08;                                /* bit 3 is tied high */
    if (input.coin1)  v |= 0x01;
    if (input.start2) v |= 0x02;
    if (input.start1) v |= 0x04;
    if (input.fire)   v |= 0x10;
    if (input.left)   v |= 0x20;
    if (input.right)  v |= 0x40;
    return v;
}

byte InZ80(register word p)
{
    switch (p & 0xff) {
        case 0:  return (uint8_t)(0x0e | (read_in1() & 0x70));
        case 1:  return read_in1();
        /* DIPs, tilt (never), and the player 2 stick - the same stick, for a cocktail */
        case 2:  return (uint8_t)((dip_in2 & 0x8b) | (read_in1() & 0x70));
        case 3:  return (uint8_t)((shift_reg >> (8 - shift_amt)) & 0xff);
        default: return 0xff;
    }
}

void OutZ80(register word p, register byte v)
{
    switch (p & 0xff) {
        case 2: shift_amt = v & 7; break;
        case 3: si_sound_write(3, v); break;
        case 4: shift_reg = (uint16_t)((v << 8) | (shift_reg >> 8)); break;
        case 5: si_sound_write(5, v); break;
        default: break;                              /* 6 is the watchdog */
    }
}

void PatchZ80(register Z80 *R) { (void)R; }
/* RunZ80 calls this every IPeriod cycles; quitting there is how we get a bounded slice */
word LoopZ80(register Z80 *R) { (void)R; return INT_QUIT; }

/* ---- public ---- */
void si_reset(void)
{
    memset(ram, 0, sizeof(ram));
    memset(&input, 0, sizeof(input));
    shift_reg = 0; shift_amt = 0; debt = 0;
    si_sound_reset();
    ResetZ80(&cpu);
}

void si_init(const si_roms_t *r)
{
    rom = r->rom;
    memset(&cpu, 0, sizeof(cpu));
    cpu.IPeriod = 1000000;
    si_reset();
}

void si_set_dips(uint8_t in2) { dip_in2 = in2; }
si_input_t *si_input(void) { return &input; }

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

void si_run_frame(void)
{
    /*
     * 262 lines a frame. The video circuit raises RST 1 when the beam reaches line 96, about
     * halfway down, and RST 2 at line 224 as it enters vblank. The program leans on both: the
     * mid-screen one draws whatever is in the top half while the beam is safely in the bottom.
     */
    enum { LINES = 262, MID = 96, VBL = 224 };
    const int32_t c_mid = (int32_t)((int64_t)SI_CYCLES_PER_FRAME * MID / LINES);
    const int32_t c_vbl = (int32_t)((int64_t)SI_CYCLES_PER_FRAME * VBL / LINES);
    run_cpu(c_mid);
    IntZ80(&cpu, INT_RST08);                         /* RST 1 */
    run_cpu(c_vbl - c_mid);
    IntZ80(&cpu, INT_RST10);                         /* RST 2 */
    run_cpu(SI_CYCLES_PER_FRAME - c_vbl);
    frame_count++;
}

/*
 * The frame is 256 bits wide and 224 rows deep, one byte per eight pixels, starting at
 * 0x2400. The monitor is turned a quarter turn, so a row of memory is a column of screen:
 * the first byte of the first row is the bottom-left corner and each row of 32 bytes climbs
 * that column from the bottom. Screen (sx, sy) is therefore bit (255 - sy) of row sx.
 *
 * The colour gel, in the frame's own coordinates (x runs up the screen):
 *   x 192..223  red    - the UFO's lane
 *   x  16.. 71  green  - shields, player, ground
 *   x   0.. 15  green only over y 16..133 - the spare-ships row at the bottom left
 * Everything else the gel left clear.
 */
void si_render(uint8_t *fb)
{
    const uint8_t *vram = ram + 0x400;
    for (int sy = 0; sy < SI_FB_H; sy++) {
        int x = 255 - sy;
        const uint8_t *src = vram + (x >> 3);
        uint8_t mask = (uint8_t)(1u << (x & 7));
        uint8_t base = (x >= 192 && x < 224) ? 3 : (x >= 16 && x < 72) ? 2 : 1;
        uint8_t *dst = fb + sy * SI_FB_W;
        if (x < 16) {
            for (int y = 0; y < SI_FB_W; y++, src += 32)
                dst[y] = (src[0] & mask) ? ((y >= 16 && y < 134) ? 2 : 1) : 0;
        } else {
            for (int y = 0; y < SI_FB_W; y++, src += 32)
                dst[y] = (src[0] & mask) ? base : 0;
        }
    }
}

void si_palette(uint16_t out[SI_PALETTE_SIZE])
{
    out[0] = 0x0000;                                 /* black */
    out[1] = 0xFFFF;                                 /* white */
    out[2] = 0x27E4;                                 /* green  0x20,0xFF,0x20 */
    out[3] = 0xF904;                                 /* red    0xFF,0x20,0x20 */
}

uint16_t si_pc(void) { return cpu.PC.W; }
uint32_t si_frame_count(void) { return frame_count; }
const uint8_t *si_ram(void) { return ram; }
