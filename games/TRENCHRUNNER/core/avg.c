/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * avg.c - Atari AVG (Star Wars) - see avg.h
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

void avg_init(avg_t *avg, const uint8_t *prom, const uint8_t *vector_ram, const uint8_t *vector_rom)
{
    memset(avg, 0, sizeof(*avg));
    avg->prom = prom;
    avg->ram = vector_ram;
    avg->rom = vector_rom;
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

static inline __attribute__((always_inline)) void add_point(avg_t *avg, int32_t x, int32_t y, uint8_t color, uint8_t intensity)
{
    if (avg->npoints < AVG_MAX_POINTS) {
        avg_point_t *p = &avg->points[avg->npoints++];
        p->x = x; p->y = y; p->color = color; p->intensity = intensity;
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

static inline __attribute__((always_inline)) int common_strobe2(st_t *a)
{
    if (OP2(a)) {
        if (OP0(a)) {
            a->pc = a->dvy << 1;
            /* (Tempest/Quantum jump-to-zero frame flush not needed for Star Wars) */
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

/* strobe2 (Star Wars variant: 8-bit intensity, 4-bit color) */
static inline __attribute__((always_inline)) int handler_6(st_t *a)
{
    if (!OP2(a) && !a->dvy12) {
        a->intensity = a->dvy & 0xff;
        a->color = (a->dvy >> 8) & 0xf;
    }
    return common_strobe2(a);
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
        add_point(ctx, a->xpos, a->ypos, 0, 0);
    }
    return cycles;
}

/* strobe3 (Star Wars variant: 3-bit intensity DAC scales the 8-bit intensity) */
static inline __attribute__((always_inline)) int handler_7(st_t *a, avg_t *ctx)
{
    const int cycles = common_strobe3(a, ctx);

    if (!OP0(a) && !OP2(a)) {
        /* 12k / 24k / 47k resistive summing network on int_latch bits 3..1 */
        /* weights: 1.0, 0.5, 0.2553; full scale 1.7553 -> scale to 1/256ths */
        static const uint16_t w3 = 146, w2 = 73, w1 = 37;   /* 256 * weight / full_scale */
        uint32_t dac = ((a->int_latch >> 3) & 1) * w3 + ((a->int_latch >> 2) & 1) * w2 + ((a->int_latch >> 1) & 1) * w1;
        uint32_t intensity = (dac * a->intensity + 128) >> 8;
        if (intensity > 255) intensity = 255;
        add_point(ctx, a->xpos, a->ypos, a->color, (uint8_t)intensity);
    }
    return cycles;
}

uint32_t avg_go(avg_t *ctx)
{
    uint32_t total = 0;
    int guard = 200000;
    st_t s = ctx->st;              /* local copy: the compiler can keep it in registers */
    st_t *a = &s;
    const uint8_t *prom = ctx->prom;
    const uint8_t *ram = ctx->ram;
    const uint8_t *rom = ctx->rom;

    /* VGGO */
    a->pc = 0;
    a->sp = 0;
    a->halt = 0;
    ctx->npoints = 0;
    ctx->overflow = 0;
    ctx->steps = 0;

    while (guard-- > 0) {
        int cycles = 0;

        a->state_latch = (a->state_latch & 0x10) | (prom[state_addr(a)] & 0xf);

        if (ST3(a)) {
            uint16_t addr = a->pc;
            a->data = (addr < 0x3000) ? ram[addr] : (addr < 0x4000) ? rom[addr - 0x3000] : 0;
            switch (a->state_latch & 7) {
                case 0: cycles += handler_0(a); break;
                case 1: cycles += handler_1(a); break;
                case 2: cycles += handler_2(a); break;
                case 3: cycles += handler_3(a); break;
                case 4: cycles += handler_4(a); break;
                case 5: cycles += handler_5(a); break;
                case 6: cycles += handler_6(a); break;
                case 7: cycles += handler_7(a, ctx); break;
            }
        }

        int halted_now = (a->halt && !(a->state_latch & 0x10));
        a->state_latch = (a->halt << 4) | (a->state_latch & 0xf);
        cycles += 8;
        total += cycles;

        if (halted_now)
            break;
    }
    ctx->steps = 200000 - guard;
    ctx->st = s;
    return total;
}

/*
 * Direct interpreter, equivalent to stepping the state PROM. The sequence of
 * handler states per opcode was derived from the 136021-105.1l PROM:
 *   VCTR (0): latch1 latch0 latch3 latch2 strobe0 strobe1 strobe3 idle
 *   HALT (1): latch1 latch0 strobe3
 *   SVEC (2): latch1 latch3 strobe0 strobe1 strobe3 idle
 *   STAT (3): latch1 latch0 idle idle idle strobe2 idle
 *   CNTR (4): latch1 latch0 strobe0 strobe3 idle
 *   JSRL (5): latch1 latch0 strobe0 strobe1 strobe2
 *   RTSL (6): latch1 latch0 strobe1 strobe2
 *   JMPL (7): latch1 latch0 strobe2
 * Every state costs 8 cycles plus whatever the handler returns; handler states
 * read the byte at PC first. HALT stops after strobe3 like the sequencer does.
 */
#define RD() (a->data = (a->pc < 0x3000) ? ram[a->pc] : (a->pc < 0x4000) ? rom[a->pc - 0x3000] : 0)
#define STEP(h) do { RD(); cycles += h; cycles += 8; steps++; } while (0)
#define IDLE()  do { cycles += 8; steps++; } while (0)

uint32_t avg_go_fast(avg_t *ctx)
{
    uint32_t cycles = 0, steps = 0;
    int guard = 20000;             /* instructions */
    st_t s = ctx->st;
    st_t *a = &s;
    const uint8_t *ram = ctx->ram;
    const uint8_t *rom = ctx->rom;

    a->pc = 0;
    a->sp = 0;
    a->halt = 0;
    ctx->npoints = 0;
    ctx->overflow = 0;

    /* coming out of halt the sequencer spends one idle state before latch1 */
    if (a->state_latch & 0x10) IDLE();

    while (guard-- > 0) {
        STEP(handler_1(a));
        switch (a->op) {
            case 0:
                STEP(handler_0(a)); STEP(handler_3(a)); STEP(handler_2(a));
                STEP(handler_4(a)); STEP(handler_5(a)); STEP(handler_7(a, ctx)); IDLE();
                break;
            case 1:
                STEP(handler_0(a)); STEP(handler_7(a, ctx));
                break;
            case 2:
                STEP(handler_3(a)); STEP(handler_4(a)); STEP(handler_5(a)); STEP(handler_7(a, ctx)); IDLE();
                break;
            case 3:
                STEP(handler_0(a)); IDLE(); IDLE(); IDLE(); STEP(handler_6(a)); IDLE();
                break;
            case 4:
                STEP(handler_0(a)); STEP(handler_4(a)); STEP(handler_7(a, ctx)); IDLE();
                break;
            case 5:
                STEP(handler_0(a)); STEP(handler_4(a)); STEP(handler_5(a)); STEP(handler_6(a));
                break;
            case 6:
                STEP(handler_0(a)); STEP(handler_5(a)); STEP(handler_6(a));
                break;
            default:
                STEP(handler_0(a)); STEP(handler_6(a));
                break;
        }
        if (a->halt) break;
    }
    /* the sequencer parks in the strobe3 state (0xF) with the halt bit latched */
    a->state_latch = (a->halt << 4) | 0x0f;
    ctx->steps = steps;
    ctx->st = s;
    return cycles;
}
