/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * missile.c - Atari Missile Command board: memory map, MADSEL video writes, IRQ timing.
 * The 6502 is the instruction-stepped core in m6502fast.h.
 *
 * Timing and memory map follow MAME's missile.cpp.
 */
#include "missile.h"
#include "pokey.h"
#include <string.h>

static mc_roms_t roms;
static uint8_t ram[0x4000];              /* 0x0000-0x3FFF: work RAM and the bitmap, one and the same */
static uint8_t palette_ram[8];
static uint8_t dip_r10 = 0x81, dip_r8 = 0x73;   /* factory settings: English, 6 cities, bonus at 10000 */
static mc_input_t input;
static pokey_t pokey;
static uint32_t frame_count, irq_count;
static int irq_state, irq_pin, ctrld, flipscreen;
static int scanline;
static uint32_t total_cycles;
static int cpu_idle;                     /* the main loop is spinning on the frame tick */
static uint32_t idle_cycles;
static int32_t cycle_debt;               /* instructions overshoot the line budget; carry it forward */
static int extra_cycle;                  /* the three-bit video path costs the CPU one more cycle */
static int madsel_active;                /* this instruction's data access goes to video RAM */

/* trackball: two 4-bit counters the game reads directly */
static uint8_t tb_pos[2];
static int tb_quarter;

/* the third bit of each pixel is scattered around video RAM; this is the schematic's mapping */
static inline uint16_t bit3_addr(uint16_t pixaddr)
{
    return (uint16_t)(((pixaddr & 0x0800) >> 1) | ((~pixaddr & 0x0800) >> 2) |
                      ((pixaddr & 0x07f8) >> 2) | ((pixaddr & 0x1000) >> 12));
}

static void vram_mad_w(uint16_t offset, uint8_t data)
{
    static const uint8_t data_lookup[4] = { 0x00, 0x0f, 0xf0, 0xff };
    uint16_t a = (uint16_t)(offset >> 2);
    uint8_t d = data_lookup[data >> 6];
    uint8_t mask = roms.writeprom[(offset & 7) | 0x10];
    ram[a & 0x3fff] = (uint8_t)((ram[a & 0x3fff] & mask) | (d & ~mask));
    if ((offset & 0xe000) == 0xe000) {   /* the MUSHROOM case: also write the third bit */
        a = bit3_addr(offset);
        d = (uint8_t)-((data >> 5) & 1);
        mask = roms.writeprom[(offset & 7) | 0x18];
        ram[a & 0x3fff] = (uint8_t)((ram[a & 0x3fff] & mask) | (d & ~mask));
        extra_cycle = 1;
    }
}

static uint8_t vram_mad_r(uint16_t offset)
{
    uint8_t result = 0xff;
    uint16_t a = (uint16_t)(offset >> 2);
    uint8_t mask = (uint8_t)(0x11 << (offset & 3));
    uint8_t d = ram[a & 0x3fff] & mask;
    if ((d & 0xf0) == 0) result &= (uint8_t)~0x80;
    if ((d & 0x0f) == 0) result &= (uint8_t)~0x40;
    if ((offset & 0xe000) == 0xe000) {
        a = bit3_addr(offset);
        mask = (uint8_t)(1 << (offset & 7));
        if ((ram[a & 0x3fff] & mask) == 0) result &= (uint8_t)~0x20;
        extra_cycle = 1;
    }
    return result;
}

/* ---- normal bus, with MAME's mirrors ---- */
static uint8_t bus_read(uint16_t addr)
{
    uint16_t a = addr & 0x7fff;              /* A15 is not decoded at all */
    if (a < 0x4000) return ram[a];
    if (a >= 0x5000) return roms.rom[a - 0x5000];
    if (a < 0x4800) {                        /* POKEY at 0x4000-0x400F, mirrored to 0x47FF */
        int reg = a & 0x0f;
        if (reg == 0x08) return dip_r8;      /* ALLPOT is wired to the R8 DIP bank */
        return pokey_read(&pokey, reg, total_cycles);
    }
    switch (a & 0xff00) {                    /* everything else mirrors across its page */
        case 0x4800:
            if (ctrld) return (uint8_t)(((tb_pos[1] << 4) & 0xf0) | (tb_pos[0] & 0x0f));
            else {                           /* IN0: cocktail buttons, start, coins, all active low */
                uint8_t v = 0xff;
                if (input.start2) v &= (uint8_t)~0x08;
                if (input.start1) v &= (uint8_t)~0x10;
                if (input.coin1)  v &= (uint8_t)~0x20;
                return v;
            }
        case 0x4900: {                       /* IN1: fire buttons, tilt, service, vblank */
            uint8_t v = 0xff;
            if (input.fire3) v &= (uint8_t)~0x01;
            if (input.fire2) v &= (uint8_t)~0x02;
            if (input.fire1) v &= (uint8_t)~0x04;
            v &= (uint8_t)~0x18;             /* two unused inputs, active high, so they read 0 */
            /* vblank is active high over the first 24 lines - the IRQ handler only counts a
             * frame tick when it sees this set, which is what makes the whole game run */
            if (scanline < 24) v |= 0x80; else v &= (uint8_t)~0x80;
            return v;
        }
        case 0x4a00: return dip_r10;
        default: return 0xff;                /* unmapped reads float high */
    }
}

static void bus_write(uint16_t addr, uint8_t data)
{
    uint16_t a = addr & 0x7fff;
    if (a < 0x4000) { ram[a] = data; return; }
    if (a >= 0x5000) return;
    if (a < 0x4800) { pokey_write(&pokey, a & 0x0f, data); return; }
    switch (a & 0xff00) {
        case 0x4800:                                          /* output latch */
            ctrld = data & 1;                                 /* bit 0 picks trackball vs buttons */
            flipscreen = (~data & 0x40) != 0;
            return;
        case 0x4b00: palette_ram[a & 7] = data; return;        /* eight colour registers */
        case 0x4c00: return;                                   /* watchdog */
        case 0x4d00: irq_state = 0; return;                    /* IRQ acknowledge */
        default: return;
    }
}

/* MADSEL: the data access of a (zp,X) instruction is diverted into video RAM, unless the IRQ
 * pin was asserted when the opcode was fetched. Everything else uses the normal decode. */
#define M6502F_READ(a)        bus_read(a)
#define M6502F_WRITE(a, v)    bus_write(a, v)
#define M6502F_IZX_READ(a)    (madsel_active ? vram_mad_r(a) : bus_read(a))
#define M6502F_IZX_WRITE(a, v) do { if (madsel_active) vram_mad_w((a), (v)); else bus_write((a), (v)); } while (0)
#include "m6502fast.h"

static m6502f_t cpu;

/* ---- public ---- */
void mc_reset(void)
{
    memset(ram, 0, sizeof(ram));
    memset(palette_ram, 0, sizeof(palette_ram));
    memset(&input, 0, sizeof(input));
    memset(tb_pos, 0, sizeof(tb_pos));
    irq_state = 0; irq_pin = 0; ctrld = 0; flipscreen = 0;
    scanline = 0; total_cycles = 0;
    extra_cycle = 0; tb_quarter = 0; cpu_idle = 0; idle_cycles = 0;
    cycle_debt = 0; madsel_active = 0;
    pokey_reset(&pokey);
    m6502f_reset(&cpu);
}

void mc_init(const mc_roms_t *r)
{
    roms = *r;
    pokey_init(&pokey);
    mc_reset();
}

void mc_set_dips(uint8_t r10, uint8_t r8) { dip_r10 = r10; dip_r8 = r8; }
mc_input_t *mc_input(void) { return &input; }

/* the game samples the counters several times a frame, so hand the motion over in pieces */
static void trackball_step(int quarter)
{
    const int8_t want[2] = { input.track_x, input.track_y };
    for (int i = 0; i < 2; i++) {
        if (!want[i]) continue;
        int step = (want[i] * (quarter + 1)) / 4 - (want[i] * quarter) / 4;
        tb_pos[i] = (uint8_t)(tb_pos[i] + step);
    }
}

static void run_cycles(int32_t n)
{
    int32_t budget = n - cycle_debt;
    while (budget > 0) {
        irq_pin = irq_state;                    /* the pin is latched at each opcode fetch */
        if (irq_pin && !(cpu.p & M6502F_I)) {
            int cy = m6502f_irq(&cpu);
            total_cycles += cy; budget -= cy;
            continue;
        }
        /* The main loop waits for the next frame tick with LSR $9F / BCC $500F. That is a true
         * idle only while $9F is zero - LSR rewrites the byte, so any other value would fall out
         * of the loop on a later pass - and only until the next IRQ, which is the thing that sets
         * the tick. Nothing else can change $9F meanwhile, so those cycles are ours to skip. */
        if (cpu.pc == 0x500f && ram[0x9f] == 0 && !irq_state) {
            cpu_idle = 1;
            total_cycles += (uint32_t)budget; idle_cycles += (uint32_t)budget;
            cycle_debt = 0;
            return;
        }
        madsel_active = !irq_pin;
        extra_cycle = 0;
        int cy = m6502f_step(&cpu) + extra_cycle;
        total_cycles += (uint32_t)cy; budget -= cy;
    }
    cycle_debt = -budget;
}

void mc_run_frame(void)
{
    tb_quarter = 0;
    for (scanline = 0; scanline < MC_LINES; scanline++) {
        /* IRQ is clocked by /32V: asserted at 0, 64, 128, 192 and cleared at 32, 96, 160, 224 */
        if ((scanline & 31) == 0) {
            if (scanline & 32) irq_state = 0;
            else { irq_state = 1; irq_count++; cpu_idle = 0; }   /* the tick the CPU was waiting for */
        }
        if ((scanline & 63) == 0) trackball_step(tb_quarter++);
        /* the CPU runs at half speed over the bottom 32 lines, while the video circuit
         * has the RAM to itself */
        int32_t per_line = (scanline >= 224) ? 40 : 80;
        if (cpu_idle) { total_cycles += (uint32_t)per_line; idle_cycles += (uint32_t)per_line; continue; }
        run_cycles(per_line);
    }
    frame_count++;
}

void mc_palette(uint16_t out[MC_PALETTE_SIZE])
{
    for (int i = 0; i < 8; i++) {
        uint8_t d = palette_ram[i];
        int r = (~d >> 3) & 1, g = (~d >> 2) & 1, b = (~d >> 1) & 1;
        out[i] = (uint16_t)((r ? 0xf800 : 0) | (g ? 0x07e0 : 0) | (b ? 0x001f : 0));
    }
}

void mc_render(uint8_t *fb)
{
    /* the visible window starts 25 lines down; MAME's cliprect is y = 25..255 */
    for (int r = 0; r < MC_FB_H; r++) {
        int y = r + 25;
        int effy = flipscreen ? ((256 + 24 - y) & 0xff) : y;
        const uint8_t *src = &ram[(effy * 64) & 0x3fff];
        const uint8_t *src3 = (effy >= 224) ? &ram[bit3_addr((uint16_t)(effy << 8)) & 0x3fff] : 0;
        uint8_t *dst = fb + r * MC_FB_W;
        for (int x = 0; x < MC_FB_W; x++) {
            uint8_t pix = (uint8_t)(src[x / 4] >> (x & 3));
            pix = (uint8_t)(((pix >> 2) & 4) | ((pix << 1) & 2));
            if (src3) pix |= (uint8_t)((src3[(x / 8) * 2] >> (x & 7)) & 1);
            dst[x] = pix;
        }
    }
}

void mc_render_audio(int16_t *buf, int samples, int rate)
{
    memset(buf, 0, samples * sizeof(int16_t));
    pokey_render(&pokey, buf, samples, rate);
    for (int i = 0; i < samples; i++) {          /* one POKEY where Star Wars mixes four */
        int32_t v = buf[i] * 24;
        buf[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
    }
}

uint16_t mc_pc(void) { return cpu.pc; }
uint32_t mc_frame_count(void) { return frame_count; }
uint32_t mc_irq_count(void) { return irq_count; }
uint32_t mc_idle_cycles(void) { uint32_t v = idle_cycles; idle_cycles = 0; return v; }
const uint8_t *mc_ram(void) { return ram; }

/* the byte at a CPU address, where that is memory - for keeping the high scores */
uint8_t *mc_mem(uint16_t a)
{
    if (a < 0x4000) return &ram[a];
    return 0;
}
