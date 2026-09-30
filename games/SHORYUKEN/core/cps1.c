/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * cps1.c - the CPS-1 A-board as Street Fighter II uses it: the 68000's I/O, the vertical
 * blank interrupt, and the pacing of the two CPUs against each other.
 *
 * A frame is 262 lines of 512 pixel clocks at 8 MHz, which is 167,680 cycles of the 10 MHz
 * 68000. The blank begins at line 240 and raises IPL1 (level 2); the board acknowledges with
 * VPA, which takes the line away again.
 *
 * The sound CPU is only brought up to date when it matters: when the 68000 writes a sound
 * latch, and at the end of the frame. That is exact where exactness is audible (the order
 * and timing of commands) and costs nothing in between.
 */
#include "cps1_internal.h"
#include "cps1_mem.h"
#include "knobs.h"
#include <string.h>

#if CPU_CORE == CPU_CORE_OWN
#include "m68kown.h"
#define cpu_init()        m68kown_init()
#define cpu_reset()       m68kown_reset()
#define cpu_run(n)        m68kown_run(n)
#define cpu_irq(l)        m68kown_set_irq(l)
#define cpu_pc()          m68kown_pc()
#define cpu_cycles_run()  m68kown_cycles_run()
#else
#include "m68k.h"
#define cpu_init()        do { m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000); } while (0)
#define cpu_reset()       m68k_pulse_reset()
#define cpu_run(n)        m68k_execute(n)
#define cpu_irq(l)        m68k_set_irq(l)
#define cpu_pc()          m68k_get_reg(NULL, M68K_REG_PC)
#define cpu_cycles_run()  m68k_cycles_run()
#endif

#define CYCLES_PER_FRAME 167680
#ifndef VBLANK_NUDGE
#define VBLANK_NUDGE 0               /* host experiments: move the interrupt by a few cycles */
#endif
#define CYCLES_TO_VBLANK (CYCLES_PER_FRAME * 240 / 262 + VBLANK_NUDGE)

const uint16_t *cps1_prog;
uint16_t *cps1_ram;
uint16_t *cps1_gfxram;
cps1_state_t cps1;
cps1_change_t cps1_changes[CPS1_CHANGES];
unsigned cps1_nchanges;
unsigned cps1_slice;

static cps1_input_t input;
static uint32_t frame_count;
static int32_t cycle_debt;               /* what the last slice ran past its end */
static int32_t frame_base;               /* cycles of this frame that ended before the current slice */
static int in_slice;

static int64_t (*clock_us)(void);
static uint64_t z80_us;

const char *cps1_blob_check(const cps1_blob_t *b)
{
    if (b->magic != CPS1_BLOB_MAGIC) return "no ROM image here (run tools/convert_roms.py and flash roms.bin)";
    if (b->version != CPS1_BLOB_VERSION) return "the ROM image is from a different version of convert_roms.py";
    if (b->prog_size != CPS1_PROG_BYTES || b->gfx_size != 0x600000 || b->z80_size != 0x10000 || b->oki_size != 0x40000
        || b->oki_off != CPS1_BLOB_MAP_BYTES || b->oki_off + b->oki_size != CPS1_BLOB_HEADER_OFF)
        return "the ROM image is not laid out as this build expects";
    return NULL;
}

void cps1_blob_roms(const cps1_blob_t *b, const void *base, cps1_roms_t *roms)
{
    const uint8_t *p = (const uint8_t *)base;
    roms->prog = (const uint16_t *)(p + b->prog_off);
    roms->gfx = p + b->gfx_off;
    roms->z80 = p + b->z80_off;
    roms->oki = NULL;
    roms->oki_read = NULL;
    roms->cfg = b;
}

/* how far into the frame the 68000 is, in its own cycles */
static int32_t frame_cycles_now(void)
{
    return frame_base + (in_slice ? (int32_t)cpu_cycles_run() : 0);
}

static void sound_catch_up(void)
{
#if SOUND != SOUND_OFF
    int64_t t0 = clock_us ? clock_us() : 0;
    cps1_sound_run_to(frame_cycles_now());
    if (clock_us) z80_us += (uint64_t)(clock_us() - t0);
#endif
}

/* ---- 800000-8001FF ---- */

static unsigned port_in0(void)
{
    unsigned v = 0xff;
    if (input.coin1) v &= ~0x01u;
    if (input.coin2) v &= ~0x02u;
    if (input.service) v &= ~0x04u;
    if (input.start1) v &= ~0x10u;
    if (input.start2) v &= ~0x20u;
    return v;
}
static unsigned port_in1(void)
{
    unsigned v = 0xffff;
    if (input.p1_right) v &= ~0x0001u;
    if (input.p1_left) v &= ~0x0002u;
    if (input.p1_down) v &= ~0x0004u;
    if (input.p1_up) v &= ~0x0008u;
    if (input.p1_b1) v &= ~0x0010u;
    if (input.p1_b2) v &= ~0x0020u;
    if (input.p1_b3) v &= ~0x0040u;
    return v;
}
static unsigned port_in2(void)
{
    unsigned v = 0xff;
    if (input.p1_b4) v &= ~0x01u;
    if (input.p1_b5) v &= ~0x02u;
    if (input.p1_b6) v &= ~0x04u;
    return v;
}

unsigned cps1_io_r16(unsigned a)
{
    if (a >= 0x800000 && a < 0x800008) return port_in1();
    if (a >= 0x800018 && a < 0x800020) {
        unsigned n = (a >> 1) & 3;
        unsigned v = n == 0 ? port_in0() : cps1.dsw[n - 1];
        return (v << 8) | 0xff;
    }
    if (a >= 0x800140 && a < 0x800180) {
        unsigned r = a & 0x3e;
        if (r == cps1.cfg.cpsb_id_reg) return cps1.cfg.cpsb_id_value;
        if (r == cps1.cfg.cpsb_in2_reg) return port_in2();
        return 0xffff;
    }
    return 0xffff;
}

void cps1_io_w16(unsigned a, unsigned v, unsigned mask)
{
    cps1_nchanges = CPS1_CHANGES + 1;
    if (a >= 0x800100 && a < 0x800140) {
        unsigned r = (a & 0x3e) >> 1;
        cps1.a_regs[r] = (uint16_t)((cps1.a_regs[r] & ~mask) | (v & mask));
        /* writing the palette base is what makes the board copy the palette out of graphics RAM */
        if (r == CPS_A_PALETTE_BASE) cps1_palette_latch();
        return;
    }
    if (a >= 0x800140 && a < 0x800180) {
        unsigned r = (a & 0x3e) >> 1;
        cps1.b_regs[r] = (uint16_t)((cps1.b_regs[r] & ~mask) | (v & mask));
        return;
    }
    if (a >= 0x800180 && a < 0x800188) {           /* sound command */
        sound_catch_up();
        cps1.latch[0] = (uint8_t)((mask & 0xff) ? v : v >> 8);
        return;
    }
    if (a >= 0x800188 && a < 0x800190) {           /* sound fade timer */
        if (mask & 0xff) { sound_catch_up(); cps1.latch[1] = (uint8_t)v; }
        return;
    }
    /* 800030: coin counters and lockouts. Nothing is wired to them here. */
}

int cps1_int_ack(int level)
{
    (void)level;
    cpu_irq(0);
    return 0xffffffff;       /* M68K_INT_ACK_AUTOVECTOR */
}

#ifdef CPS1_PROFILE
/* host only: how many instructions ran, and where; and, between two frames given in the
 * environment (TRACE_FROM, TRACE_TO), every instruction outside the idle loop */
#include <stdio.h>
#include <stdlib.h>
extern int m68ki_remaining_cycles;
uint64_t cps1_profile_instructions;
uint32_t cps1_profile_pc[CPS1_PROG_BYTES / 2];
static FILE *trace;
static int trace_from = -1, trace_to = -1;
void cps1_profile_hook(unsigned int pc)
{
    cps1_profile_instructions++;
    if (pc < CPS1_PROG_BYTES) cps1_profile_pc[pc >> 1]++;
    if (trace_from < 0) {
        trace_from = getenv("TRACE_FROM") ? atoi(getenv("TRACE_FROM")) : 1 << 30;
        trace_to = getenv("TRACE_TO") ? atoi(getenv("TRACE_TO")) : 0;
        if (getenv("TRACE")) trace = fopen(getenv("TRACE"), "w");
    }
    if (trace && (int)frame_count >= trace_from && (int)frame_count <= trace_to && (!getenv("TRACE_NO_LOOP") || pc < 0xa30 || pc > 0xa5c))
        fprintf(trace, "%u %06X\n", frame_count, pc);
}
#endif

/* ---- the machine ---- */

void cps1_init(const cps1_roms_t *roms, uint16_t *ram, uint16_t *gfxram)
{
    memset(&cps1, 0, sizeof(cps1));
    cps1.roms = *roms;
    cps1.cfg = *roms->cfg;
    cps1_prog = roms->prog;
    cps1_ram = ram;
    cps1_gfxram = gfxram;
    /* SW(A): 1 coin 1 credit. SW(B): difficulty 4 of 8. SW(C): demo sounds on, continue
     * allowed, not free play, not frozen, not flipped, game mode. */
    cps1.dsw[0] = 0xff;
    cps1.dsw[1] = 0xfc;
    cps1.dsw[2] = 0x9f;
    cpu_init();
#if IDLE_SKIP
    cps1_idle_init(roms->prog);
#endif
    cps1_video_init();
    cps1_reset();
}

void cps1_reset(void)
{
    memset(cps1_ram, 0, CPS1_RAM_BYTES);
    memset(cps1_gfxram, 0, CPS1_GFXRAM_BYTES);
    memset(cps1.a_regs, 0, sizeof(cps1.a_regs));
    memset(cps1.b_regs, 0, sizeof(cps1.b_regs));
    cps1.a_regs[CPS_A_OBJ_BASE] = 0x9200;
    cps1.a_regs[CPS_A_SCROLL1_BASE] = 0x9000;
    cps1.a_regs[CPS_A_SCROLL2_BASE] = 0x9040;
    cps1.a_regs[CPS_A_SCROLL3_BASE] = 0x9080;
    cps1.a_regs[CPS_A_OTHER_BASE] = 0x9100;
    cps1.latch[0] = cps1.latch[1] = 0xff;
    memset(&input, 0, sizeof(input));
    frame_count = 0;
    cycle_debt = 0;
    cpu_reset();
#if SOUND != SOUND_OFF
    cps1_sound_reset();
#endif
}

cps1_input_t *cps1_input(void) { return &input; }
uint32_t cps1_frame_count(void) { return frame_count; }
uint32_t cps1_pc(void) { return (uint32_t)cpu_pc(); }

void cps1_set_clock(int64_t (*now_us)(void)) { clock_us = now_us; }
uint64_t cps1_z80_us(void) { uint64_t v = z80_us; z80_us = 0; return v; }

static void run_slice(int32_t until)
{
    int32_t want = until - frame_base - cycle_debt;
    int32_t ran = 0;
    cps1_slice++;
    if (want > 0) {
        in_slice = 1;
        ran = (int32_t)cpu_run(want);
        in_slice = 0;
    }
    cycle_debt = ran - want;                 /* an instruction is not cut in half: it overruns */
    if (cycle_debt < 0) cycle_debt = 0;      /* stopped early (STOP): the time passes anyway */
    frame_base = until;
}

void cps1_run_frame(void)
{
    frame_base = 0;
    run_slice(CYCLES_TO_VBLANK);
    cps1_objram_latch();                     /* the sprite list is taken at the blank */
    cpu_irq(2);
    run_slice(CYCLES_PER_FRAME);
    sound_catch_up();
#if SOUND != SOUND_OFF
    cps1_sound_end_frame(CYCLES_PER_FRAME);
#endif
    frame_count++;
}
