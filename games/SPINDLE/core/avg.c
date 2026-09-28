/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * avg.c - Atari AVG (Tempest) - see avg.h
 */
#include "avg.h"
#include <string.h>

typedef struct avg_state st_t;

#define OP0(a) ((a)->op & 1)
#define OP1(a) (((a)->op >> 1) & 1)
#define OP2(a) (((a)->op >> 2) & 1)
#define OP3(a) (((a)->op >> 3) & 1)
#define ST3(a) (((a)->state_latch >> 3) & 1)

#define XCENTER ((AVG_XMAX / 2) << 16)
#define YCENTER ((AVG_YMAX / 2) << 16)
#define XDAC_XOR 0x200
#define YDAC_XOR 0x200

void avg_init(avg_t *avg, const uint8_t *prom, const uint8_t *vector_ram,
              const uint8_t *vector_rom, const uint8_t *colorram)
{
    memset(avg, 0, sizeof(*avg));
    avg->prom = prom;
    avg->ram = vector_ram;
    avg->rom = vector_rom;
    avg->colorram = colorram;
    avg->st.halt = 1;
}


void avg_reset(avg_t *avg)
{
    avg->st.state_latch = 0;
    avg->st.bin_scale = 0;
    avg->st.scale = 0;
    avg->st.color = 0;
    avg->st.halt = 1;
}

static inline __attribute__((always_inline)) void add_point(avg_t *avg, int32_t x, int32_t y,
                                                            uint8_t r, uint8_t g, uint8_t b,
                                                            uint8_t intensity)
{
    if (avg->npoints < AVG_MAX_POINTS) {
        avg_point_t *p = &avg->points[avg->npoints++];
        p->x = x; p->y = y; p->r = r; p->g = g; p->b = b; p->intensity = intensity;
    } else {
        avg->overflow = 1;
    }
}

static inline __attribute__((always_inline)) uint8_t state_addr(const st_t *a)
{
    return (((a->state_latch >> 4) ^ 1) << 7) | (a->op << 4) | (a->state_latch & 0xf);
}

/* latch0 */
static inline __attribute__((always_inline)) int handler_0(st_t *a)
{
    a->dvy = (a->dvy & 0x1f00) | a->data;
    a->pc++;
    return 0;
}

/* latch1 */
static inline __attribute__((always_inline)) int handler_1(st_t *a)
{
    a->dvy12 = (a->data >> 4) & 1;
    a->op = a->data >> 5;
    a->int_latch = 0;
    a->dvy = (a->dvy12 << 12) | ((a->data & 0xf) << 8);
    a->dvx = 0;
    a->pc++;
    return 0;
}

/* latch2 */
static inline __attribute__((always_inline)) int handler_2(st_t *a)
{
    a->dvx = (a->dvx & 0x1f00) | a->data;
    a->pc++;
    return 0;
}

/* latch3 */
static inline __attribute__((always_inline)) int handler_3(st_t *a)
{
    a->int_latch = a->data >> 4;
    a->dvx = ((a->int_latch & 1) << 12) | ((a->data & 0xf) << 8) | (a->dvx & 0xff);
    a->pc++;
    return 0;
}

/* strobe0 */
static inline __attribute__((always_inline)) int handler_4(st_t *a)
{
    if (OP0(a)) {
        a->stack[a->sp & 3] = a->pc;
    } else {
        /* Normalization for roughly constant deflection speed (see MAME). */
        int i = 0;
        while ((((a->dvy ^ (a->dvy << 1)) & 0x1000) == 0)
               && (((a->dvx ^ (a->dvx << 1)) & 0x1000) == 0)
               && (i++ < 16)) {
            a->dvy = (a->dvy & 0x1000) | ((a->dvy << 1) & 0x1fff);
            a->dvx = (a->dvx & 0x1000) | ((a->dvx << 1) & 0x1fff);
            a->timer >>= 1;
            a->timer |= 0x4000 | (OP1(a) << 7);
        }
        if (OP1(a))
            a->timer &= 0xff;
    }
    return 0;
}

static inline __attribute__((always_inline)) int common_strobe1(st_t *a)
{
    if (OP2(a)) {
        if (OP1(a))
            a->sp = (a->sp - 1) & 0xf;
        else
            a->sp = (a->sp + 1) & 0xf;
    }
    return 0;
}

/* strobe1 */
static inline __attribute__((always_inline)) int handler_5(st_t *a)
{
    if (!OP2(a)) {
        for (int i = a->bin_scale; i > 0; i--) {
            a->timer >>= 1;
            a->timer |= 0x4000 | (OP1(a) << 7);
        }
        if (OP1(a))
            a->timer &= 0xff;
    }
    return common_strobe1(a);
}

static inline __attribute__((always_inline)) int common_strobe2(st_t *a, avg_t *ctx)
{
    if (OP2(a)) {
        if (OP0(a)) {
            a->pc = a->dvy << 1;
            /*
             * Tempest keeps the AVG in an endless loop: sooner or later it jumps back to
             * address zero and starts the list again, while the CPU rewrites vector RAM
             * underneath it. There is no halt to wait for, so this jump is what divides the
             * stream into frames. On a real vector monitor there are no frames at all.
             */
            if (a->dvy == 0) ctx->frame_done = 1;
        } else {
            a->pc = a->stack[a->sp & 3];
        }
    } else {
        if (a->dvy12) {
            a->scale = a->dvy & 0xff;
            a->bin_scale = (a->dvy >> 8) & 7;
        }
    }
    return 0;
}

/* strobe2 (Tempest: bit 11 of the data word picks colour index or 4-bit intensity) */
static inline __attribute__((always_inline)) int handler_6(st_t *a, avg_t *ctx)
{
    if (!OP2(a) && !a->dvy12) {
        if (a->dvy & 0x800) a->color = a->dvy & 0xf;
        else                a->intensity = (a->dvy >> 4) & 0xf;
    }
    return common_strobe2(a, ctx);
}

static inline __attribute__((always_inline)) int common_strobe3(st_t *a, avg_t *ctx)
{
    int cycles = 0;

    a->halt = OP0(a);

    if (!OP0(a) && !OP2(a)) {
        if (OP1(a))
            cycles = 0x100 - (a->timer & 0xff);
        else
            cycles = 0x8000 - a->timer;
        a->timer = 0;

        a->xpos += ((((a->dvx >> 3) ^ XDAC_XOR) - 0x200) * cycles * (a->scale ^ 0xff)) >> 4;
        a->ypos -= ((((a->dvy >> 3) ^ YDAC_XOR) - 0x200) * cycles * (a->scale ^ 0xff)) >> 4;
    }

    if (OP2(a)) {
        cycles = 0x8000 - a->timer;
        a->timer = 0;
        a->xpos = XCENTER;
        a->ypos = YCENTER;
        add_point(ctx, a->xpos, a->ypos, 0, 0, 0, 0);
    }
    return cycles;
}

/*
 * strobe3 (Tempest). The colour is an index into the sixteen bytes of colour RAM the CPU
 * writes at 0x0800, and every bit of it is inverted on the way to the DACs: bit 1 and bit 0
 * make red between them, bit 3 is green, bit 2 is blue. The beam's x and y are swapped
 * because the monitor is mounted rotated.
 */
static inline __attribute__((always_inline)) int handler_7(st_t *a, avg_t *ctx)
{
    const int cycles = common_strobe3(a, ctx);

    if (!OP0(a) && !OP2(a)) {
        uint8_t d = ctx->colorram ? ctx->colorram[a->color & 0xf] : 0x0f;
        uint8_t r = (uint8_t)((((~d >> 1) & 1) * 0xf3) + (((~d >> 0) & 1) * 0x0c));
        uint8_t g = (uint8_t)(((~d >> 3) & 1) * 0xf3);
        uint8_t b = (uint8_t)(((~d >> 2) & 1) * 0xf3);
        /* the 4-bit intensity DAC: a lone bit 1 in the latch means "use the STAT intensity" */
        uint8_t i4 = ((a->int_latch >> 1) == 1) ? a->intensity : (uint8_t)(a->int_latch & 0x0e);
        uint8_t intensity = (uint8_t)((i4 & 0xf) * 0x11);
        add_point(ctx, a->ypos - YCENTER + XCENTER, a->xpos - XCENTER + YCENTER,
                  r, g, b, intensity);
    }
    return cycles;
}

void avg_go(avg_t *ctx)
{
    ctx->st.pc = 0;
    ctx->st.sp = 0;
    ctx->st.halt = 0;
    ctx->npoints = 0;
    ctx->overflow = 0;
    ctx->frame_done = 0;
}

int avg_halted(const avg_t *ctx) { return ctx->st.halt; }

uint32_t avg_run_frame(avg_t *ctx, uint32_t max_cycles, int *frame)
{
    uint32_t total = 0;
    st_t s = ctx->st;              /* local copy: the compiler can keep it in registers */
    st_t *a = &s;
    const uint8_t *prom = ctx->prom;
    const uint8_t *ram = ctx->ram;
    const uint8_t *rom = ctx->rom;

    ctx->frame_done = 0;
    ctx->steps = 0;
    if (frame) *frame = 0;

    while (total < max_cycles) {
        int cycles = 0;

        a->state_latch = (a->state_latch & 0x10) | (prom[state_addr(a)] & 0xf);

        if (ST3(a)) {
            /*
             * The vector memory is sixteen bits wide and the two halves are the other way
             * round from the CPU's view, so the generator fetches with the low address bit
             * flipped. Its program counter is also relative to the base of vector RAM at
             * CPU 0x2000, not an address in the CPU's map. Getting either of these wrong
             * gives a sequencer that runs, consumes cycles and draws nothing recognisable.
             */
            uint16_t addr = (uint16_t)(0x2000 + (a->pc ^ 1));
            a->data = (addr < 0x3000) ? ram[addr - 0x2000]
                    : (addr < 0x4000) ? rom[addr - 0x3000] : 0;
            switch (a->state_latch & 7) {
                case 0: cycles += handler_0(a); break;
                case 1: cycles += handler_1(a); break;
                case 2: cycles += handler_2(a); break;
                case 3: cycles += handler_3(a); break;
                case 4: cycles += handler_4(a); break;
                case 5: cycles += handler_5(a); break;
                case 6: cycles += handler_6(a, ctx); break;
                case 7: cycles += handler_7(a, ctx); break;
            }
        }

        a->state_latch = (a->halt << 4) | (a->state_latch & 0xf);
        cycles += 8;
        total += cycles;
        ctx->steps++;

        /* the wrap to address zero is the end of a frame's worth of vectors */
        if (ctx->frame_done) { if (frame) *frame = 1; break; }

        /* a HALT opcode parks the sequencer; Tempest's list does not use one, but the
         * self-test does, and there is nothing to run until the CPU pulses VGGO again */
        if (a->halt) break;
    }
    ctx->st = s;
    return total;
}
