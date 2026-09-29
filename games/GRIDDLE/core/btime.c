/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * btime.c - Data East Burger Time board: the game's 6502 (in DECO's CPU-7 epoxy block, which
 * scrambles opcodes) and its memory map. The 6502 is the instruction-stepped core in
 * m6502fast.h; the sound board is in btime_sound.c.
 */
#include "btime_internal.h"
#include <string.h>

bt_roms_t bt_roms;
uint8_t bt_vram[0x400], bt_cram[0x400];
uint8_t bt_palram[16];
uint8_t bt_scroll[2];
uint8_t bt_flip;

static uint8_t ram[0x800];
static uint8_t dsw1 = 0x3f;          /* 1 coin 1 play both mechs, "leave off" off, upright */
static uint8_t dsw2 = 0xfb;          /* 3 lives, bonus at 20000, enemies 4 */
static bt_input_t input;
static uint32_t frame_count;
static int vblank, irq_pending, had_written, prev_coin;
static int32_t cycle_debt;

/* ---- input ports, active low (the coin switches are the exception) ---- */
static uint8_t read_p1(void)
{
    uint8_t v = 0xff;
    if (input.right) v &= (uint8_t)~0x01;
    if (input.left)  v &= (uint8_t)~0x02;
    if (input.up)    v &= (uint8_t)~0x04;
    if (input.down)  v &= (uint8_t)~0x08;
    if (input.fire)  v &= (uint8_t)~0x10;
    return v;
}
static uint8_t read_system(void)
{
    uint8_t v = 0x3f;
    if (input.start1) v &= (uint8_t)~0x01;
    if (input.start2) v &= (uint8_t)~0x02;
    if (input.coin1)  v |= 0x40;
    return v;
}

/* the video RAMs have a second port with the coordinates swapped; the sprites live in the
 * first row of that view, which is the first column of the normal one */
static inline uint16_t swapped(uint16_t offset) { return (uint16_t)(32 * (offset % 32) + offset / 32); }

static uint8_t bus_read(uint16_t a)
{
    if (a < 0x0800) return ram[a];
    if (a >= 0xb000) return bt_roms.rom[a - 0xb000];
    if (a >= 0x0c00 && a < 0x0c10) return bt_palram[a & 0x0f];
    if (a >= 0x1000 && a < 0x1400) return bt_vram[a & 0x3ff];
    if (a >= 0x1400 && a < 0x1800) return bt_cram[a & 0x3ff];
    if (a >= 0x1800 && a < 0x1c00) return bt_vram[swapped(a & 0x3ff)];
    if (a >= 0x1c00 && a < 0x2000) return bt_cram[swapped(a & 0x3ff)];
    switch (a) {
        case 0x4000: return read_p1();
        case 0x4001: return 0xff;                          /* the cocktail player */
        case 0x4002: return read_system();
        case 0x4003: return (uint8_t)(dsw1 | (vblank ? 0x80 : 0));   /* bit 7 is the vblank line */
        case 0x4004: return dsw2;
        default: return 0xff;
    }
}

static void bus_write(uint16_t a, uint8_t d)
{
    if (a < 0x0800) { ram[a] = d; return; }
    if (a >= 0x0c00 && a < 0x0c10) { bt_palram[a & 0x0f] = d; return; }
    if (a >= 0x1000 && a < 0x1400) { bt_vram[a & 0x3ff] = d; return; }
    if (a >= 0x1400 && a < 0x1800) { bt_cram[a & 0x3ff] = d; return; }
    if (a >= 0x1800 && a < 0x1c00) { bt_vram[swapped(a & 0x3ff)] = d; return; }
    if (a >= 0x1c00 && a < 0x2000) { bt_cram[swapped(a & 0x3ff)] = d; return; }
    switch (a) {
        case 0x4002: bt_flip = 0; break;                   /* palette select and, in a cocktail, flip */
        case 0x4003: bt_sound_latch_w(d); break;
        case 0x4004: bt_scroll[0] = d; break;
        default: break;
    }
}

/*
 * The CPU-7 block: an opcode fetched right after a write cycle, from an address with bits 2
 * and 8 set, comes out with its bits shuffled, and the program is stored to suit. That is the
 * whole of the encryption; an interrupt's stack pushes count as writes just like a store.
 */
static uint8_t fetch_op(uint16_t a)
{
    uint8_t v = bus_read(a);
    if (had_written) {
        had_written = 0;
        if ((a & 0x104) == 0x104)
            v = (uint8_t)(((v >> 6) & 1) << 7 | ((v >> 5) & 1) << 6 | ((v >> 3) & 1) << 5 | ((v >> 4) & 1) << 4 |
                          ((v >> 2) & 1) << 3 | ((v >> 7) & 1) << 2 | ((v >> 1) & 1) << 1 | (v & 1));
    }
    return v;
}

#define M6502F_READ(a)     bus_read(a)
#define M6502F_WRITE(a, v) (bus_write((a), (v)), had_written = 1)
#define M6502F_FETCHOP(a)  fetch_op(a)
#include "m6502fast.h"

static m6502f_t cpu;

/* ---- public ---- */
void bt_reset(void)
{
    memset(ram, 0, sizeof(ram));
    memset(bt_vram, 0, sizeof(bt_vram));
    memset(bt_cram, 0, sizeof(bt_cram));
    memset(bt_palram, 0, sizeof(bt_palram));
    memset(&input, 0, sizeof(input));
    bt_scroll[0] = bt_scroll[1] = 0; bt_flip = 0;
    vblank = 0; irq_pending = 0; had_written = 0; prev_coin = 0;
    cycle_debt = 0;
    bt_sound_reset();
    m6502f_reset(&cpu);
}

void bt_init(const bt_roms_t *r)
{
    bt_roms = *r;
    bt_video_init();
    bt_sound_init();
    bt_reset();
}

void bt_set_dips(uint8_t d1, uint8_t d2) { dsw1 = (uint8_t)(d1 & 0x7f); dsw2 = d2; }
bt_input_t *bt_input(void) { return &input; }

#ifdef BT_DEBUG
uint32_t bt_dbg_hist[0x10000];
#endif
static void run_cycles(int32_t n)
{
    int32_t budget = n - cycle_debt;
    while (budget > 0) {
#ifdef BT_DEBUG
        bt_dbg_hist[cpu.pc]++;
#endif
        if (irq_pending && !(cpu.p & 0x04)) { budget -= m6502f_irq(&cpu); irq_pending = 0; }
        int cy = m6502f_step(&cpu);
        budget -= cy;
    }
    cycle_debt = -budget;
}

void bt_run_frame(void)
{
    /* no vblank interrupt on this board: the program polls the vblank line, and a coin is
     * what interrupts it */
    enum { SLICES = 17 };                                  /* one per sound-board NMI */
    if (input.coin1 && !prev_coin) irq_pending = 1;
    prev_coin = input.coin1;
    for (int s = 0; s < SLICES; s++) {
        vblank = (s * 272 / SLICES) >= 248;
        run_cycles(BT_CYCLES_PER_FRAME / SLICES);
        bt_sound_run(BT_SND_CYCLES_PER_FRAME / SLICES, 1);
    }
    frame_count++;
}

uint16_t bt_pc(void) { return cpu.pc; }
uint32_t bt_frame_count(void) { return frame_count; }

/* the byte at a CPU address, where that is memory - for keeping the high scores */
uint8_t *bt_mem(uint16_t a)
{
    if (a < 0x0800) return &ram[a];
    return 0;
}
