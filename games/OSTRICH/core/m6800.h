/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * m6800.h - a small Motorola 6800 core, one instruction a step, for the Williams sound board's
 * 6808. Include it once in a translation unit that first defines
 *
 *   M6800_READ(a)      read a byte from the sixteen-bit address a
 *   M6800_WRITE(a, v)  write one
 *
 * The 6801's additions (the D register ops, ABX, MUL, PSHX/PULX, BRN) are decoded too; they
 * cost nothing and the 6800 treats those opcodes as undefined anyway. CPX leaves C alone, as
 * the 6800 does. Cycle counts are the 6800's.
 */
#ifndef M6800_CORE_H
#define M6800_CORE_H
#include <stdint.h>

typedef struct {
    uint8_t a, b, cc;
    uint16_t x, sp, pc;
    uint8_t wai;                  /* WAI executed: registers stacked, waiting for an interrupt */
} m6800_t;

#define M6800_C 0x01
#define M6800_V 0x02
#define M6800_Z 0x04
#define M6800_N 0x08
#define M6800_I 0x10
#define M6800_H 0x20

static const uint8_t m6800_cycles[256] = {
/*        0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
/* 0 */   2, 2, 2, 2, 3, 3, 2, 2, 4, 4, 2, 2, 2, 2, 2, 2,
/* 1 */   2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* 2 */   4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
/* 3 */   4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 3,10, 4,10, 9,12,
/* 4 */   2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* 5 */   2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
/* 6 */   7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 7,
/* 7 */   6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 3, 6,
/* 8 */   2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 3, 8, 3, 2,
/* 9 */   3, 3, 3, 5, 3, 3, 3, 4, 3, 3, 3, 3, 4, 5, 4, 5,
/* A */   5, 5, 5, 6, 5, 5, 5, 6, 5, 5, 5, 5, 6, 8, 6, 7,
/* B */   4, 4, 4, 6, 4, 4, 4, 5, 4, 4, 4, 4, 5, 9, 5, 6,
/* C */   2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 3, 2,
/* D */   3, 3, 3, 5, 3, 3, 3, 4, 3, 3, 3, 3, 4, 4, 4, 5,
/* E */   5, 5, 5, 6, 5, 5, 5, 6, 5, 5, 5, 5, 5, 5, 6, 7,
/* F */   4, 4, 4, 6, 4, 4, 4, 5, 4, 4, 4, 4, 5, 5, 5, 6,
};

static inline uint8_t m6800_rd(uint16_t a) { return (uint8_t)M6800_READ(a); }
static inline void m6800_wr(uint16_t a, uint8_t v) { M6800_WRITE(a, v); }
static inline uint16_t m6800_rd16(uint16_t a) { return (uint16_t)((m6800_rd(a) << 8) | m6800_rd((uint16_t)(a + 1))); }

static inline void m6800_push(m6800_t *c, uint8_t v) { m6800_wr(c->sp, v); c->sp--; }
static inline uint8_t m6800_pull(m6800_t *c) { c->sp++; return m6800_rd(c->sp); }

static inline void m6800_push_all(m6800_t *c)
{
    m6800_push(c, (uint8_t)c->pc); m6800_push(c, (uint8_t)(c->pc >> 8));
    m6800_push(c, (uint8_t)c->x);  m6800_push(c, (uint8_t)(c->x >> 8));
    m6800_push(c, c->a); m6800_push(c, c->b); m6800_push(c, c->cc);
}

static void m6800_reset(m6800_t *c)
{
    c->a = c->b = 0; c->x = 0; c->sp = 0;
    c->cc = 0xc0 | M6800_I;
    c->pc = m6800_rd16(0xfffe);
    c->wai = 0;
}

/* take an interrupt through `vector`; returns the cycles it cost */
static int m6800_interrupt(m6800_t *c, uint16_t vector)
{
    if (!c->wai) m6800_push_all(c);
    c->wai = 0;
    c->cc |= M6800_I;
    c->pc = m6800_rd16(vector);
    return 12;
}
static inline int m6800_irq(m6800_t *c) { return (c->cc & M6800_I) ? 0 : m6800_interrupt(c, 0xfff8); }
static inline int m6800_nmi(m6800_t *c) { return m6800_interrupt(c, 0xfffc); }

/* ---- flag helpers ---- */
#define CC_SET(m) (c->cc |= (m))
#define CC_CLR(m) (c->cc &= (uint8_t)~(m))
#define NZ8(r)   do { CC_CLR(M6800_N | M6800_Z); if ((r) & 0x80) CC_SET(M6800_N); if (!((r) & 0xff)) CC_SET(M6800_Z); } while (0)
#define NZ16(r)  do { CC_CLR(M6800_N | M6800_Z); if ((r) & 0x8000) CC_SET(M6800_N); if (!((r) & 0xffff)) CC_SET(M6800_Z); } while (0)

static inline uint8_t alu_add(m6800_t *c, uint8_t a, uint8_t m, int carry)
{
    unsigned r = (unsigned)a + m + (unsigned)carry;
    CC_CLR(M6800_H | M6800_N | M6800_Z | M6800_V | M6800_C);
    if ((a ^ m ^ r) & 0x10) CC_SET(M6800_H);
    if ((a ^ r) & (m ^ r) & 0x80) CC_SET(M6800_V);
    if (r & 0x100) CC_SET(M6800_C);
    NZ8(r);
    return (uint8_t)r;
}
static inline uint8_t alu_sub(m6800_t *c, uint8_t a, uint8_t m, int carry)
{
    unsigned r = (unsigned)a - m - (unsigned)carry;
    CC_CLR(M6800_N | M6800_Z | M6800_V | M6800_C);
    if ((a ^ m) & (a ^ r) & 0x80) CC_SET(M6800_V);
    if (r & 0x100) CC_SET(M6800_C);
    NZ8(r);
    return (uint8_t)r;
}
static inline uint16_t alu_sub16(m6800_t *c, uint16_t a, uint16_t m, int set_c)
{
    unsigned r = (unsigned)a - m;
    CC_CLR(M6800_N | M6800_Z | M6800_V);
    if (set_c) { CC_CLR(M6800_C); if (r & 0x10000) CC_SET(M6800_C); }
    if ((a ^ m) & (a ^ r) & 0x8000) CC_SET(M6800_V);
    NZ16(r);
    return (uint16_t)r;
}
static inline uint16_t alu_add16(m6800_t *c, uint16_t a, uint16_t m)
{
    unsigned r = (unsigned)a + m;
    CC_CLR(M6800_N | M6800_Z | M6800_V | M6800_C);
    if ((a ^ r) & (m ^ r) & 0x8000) CC_SET(M6800_V);
    if (r & 0x10000) CC_SET(M6800_C);
    NZ16(r);
    return (uint16_t)r;
}
static inline uint8_t alu_logic(m6800_t *c, uint8_t r) { CC_CLR(M6800_V); NZ8(r); return r; }

/* the read-modify-write group, rows 4 to 7: returns the new value (CLR/TST/etc.) */
static inline uint8_t alu_unary(m6800_t *c, int col, uint8_t m)
{
    unsigned r;
    switch (col) {
        case 0x0: /* NEG */ r = (unsigned)(0 - m) & 0xff; CC_CLR(M6800_N | M6800_Z | M6800_V | M6800_C);
                  if (m == 0x80) CC_SET(M6800_V); if (m) CC_SET(M6800_C); NZ8(r); return (uint8_t)r;
        case 0x3: /* COM */ r = (uint8_t)~m; CC_CLR(M6800_V); CC_SET(M6800_C); NZ8(r); return (uint8_t)r;
        case 0x4: /* LSR */ r = m >> 1; CC_CLR(M6800_N | M6800_Z | M6800_V | M6800_C);
                  if (m & 1) CC_SET(M6800_C | M6800_V); if (!r) CC_SET(M6800_Z); return (uint8_t)r;
        case 0x6: /* ROR */ r = (m >> 1) | ((c->cc & M6800_C) ? 0x80 : 0); CC_CLR(M6800_N | M6800_Z | M6800_V | M6800_C);
                  if (m & 1) CC_SET(M6800_C); NZ8(r); if (((c->cc >> 3) ^ c->cc) & 1) CC_SET(M6800_V); return (uint8_t)r;
        case 0x7: /* ASR */ r = (m >> 1) | (m & 0x80); CC_CLR(M6800_N | M6800_Z | M6800_V | M6800_C);
                  if (m & 1) CC_SET(M6800_C); NZ8(r); if (((c->cc >> 3) ^ c->cc) & 1) CC_SET(M6800_V); return (uint8_t)r;
        case 0x8: /* ASL */ r = (m << 1) & 0xff; CC_CLR(M6800_N | M6800_Z | M6800_V | M6800_C);
                  if (m & 0x80) CC_SET(M6800_C); NZ8(r); if (((c->cc >> 3) ^ c->cc) & 1) CC_SET(M6800_V); return (uint8_t)r;
        case 0x9: /* ROL */ r = ((m << 1) | (c->cc & M6800_C)) & 0xff; CC_CLR(M6800_N | M6800_Z | M6800_V | M6800_C);
                  if (m & 0x80) CC_SET(M6800_C); NZ8(r); if (((c->cc >> 3) ^ c->cc) & 1) CC_SET(M6800_V); return (uint8_t)r;
        case 0xa: /* DEC */ r = (m - 1) & 0xff; CC_CLR(M6800_V); if (m == 0x80) CC_SET(M6800_V); NZ8(r); return (uint8_t)r;
        case 0xc: /* INC */ r = (m + 1) & 0xff; CC_CLR(M6800_V); if (m == 0x7f) CC_SET(M6800_V); NZ8(r); return (uint8_t)r;
        case 0xd: /* TST */ CC_CLR(M6800_V | M6800_C); NZ8(m); return m;
        case 0xf: /* CLR */ CC_CLR(M6800_N | M6800_V | M6800_C); CC_SET(M6800_Z); return 0;
        default:  return m;
    }
}

static inline int m6800_cond(const m6800_t *c, int op)
{
    uint8_t cc = c->cc;
    int n = (cc >> 3) & 1, z = (cc >> 2) & 1, v = (cc >> 1) & 1, cy = cc & 1;
    switch (op & 0x0f) {
        case 0x0: return 1;               /* BRA */
        case 0x1: return 0;               /* BRN */
        case 0x2: return !(cy | z);       /* BHI */
        case 0x3: return cy | z;          /* BLS */
        case 0x4: return !cy;             /* BCC */
        case 0x5: return cy;              /* BCS */
        case 0x6: return !z;              /* BNE */
        case 0x7: return z;               /* BEQ */
        case 0x8: return !v;              /* BVC */
        case 0x9: return v;               /* BVS */
        case 0xa: return !n;              /* BPL */
        case 0xb: return n;               /* BMI */
        case 0xc: return !(n ^ v);        /* BGE */
        case 0xd: return n ^ v;           /* BLT */
        case 0xe: return !(z | (n ^ v));  /* BGT */
        default:  return z | (n ^ v);     /* BLE */
    }
}

/* one instruction; returns its cycles (1 while parked in WAI) */
static int m6800_step(m6800_t *c)
{
    if (c->wai) return 1;
    uint8_t op = m6800_rd(c->pc++);
    int cyc = m6800_cycles[op];

    if (op >= 0x80) {
        /* the accumulator/memory group: A in rows 8-B, B in rows C-F; imm/dir/idx/ext by row */
        int col = op & 0x0f, mode = (op >> 4) & 3;
        uint8_t *acc = (op & 0x40) ? &c->b : &c->a;
        uint16_t ea = 0;
        int is16 = (col == 0x3 || col == 0xc || col == 0xe || col == 0xf) || (op & 0x40 && (col == 0xc || col == 0xd));
        int is_store = (col == 0x7 || col == 0xd || col == 0xf || (op & 0x40 && col == 0xd));
        if (col == 0xd && mode == 0) {                      /* 8D BSR / CD undefined */
            int8_t off = (int8_t)m6800_rd(c->pc++);
            m6800_push(c, (uint8_t)c->pc); m6800_push(c, (uint8_t)(c->pc >> 8));
            c->pc = (uint16_t)(c->pc + off);
            return cyc;
        }
        switch (mode) {
            case 0: ea = c->pc; c->pc = (uint16_t)(c->pc + (is16 ? 2 : 1)); break;
            case 1: ea = m6800_rd(c->pc++); break;
            case 2: ea = (uint16_t)(c->x + m6800_rd(c->pc++)); break;
            default: ea = m6800_rd16(c->pc); c->pc = (uint16_t)(c->pc + 2); break;
        }
        (void)is_store;
        switch (col) {
            case 0x0: *acc = alu_sub(c, *acc, m6800_rd(ea), 0); break;                          /* SUB */
            case 0x1: alu_sub(c, *acc, m6800_rd(ea), 0); break;                                 /* CMP */
            case 0x2: *acc = alu_sub(c, *acc, m6800_rd(ea), c->cc & M6800_C); break;            /* SBC */
            case 0x3: {                                                                          /* SUBD / ADDD (6801) */
                uint16_t d = (uint16_t)((c->a << 8) | c->b), m = m6800_rd16(ea);
                d = (op & 0x40) ? alu_add16(c, d, m) : alu_sub16(c, d, m, 1);
                c->a = (uint8_t)(d >> 8); c->b = (uint8_t)d; break;
            }
            case 0x4: *acc = alu_logic(c, (uint8_t)(*acc & m6800_rd(ea))); break;               /* AND */
            case 0x5: alu_logic(c, (uint8_t)(*acc & m6800_rd(ea))); break;                      /* BIT */
            case 0x6: *acc = alu_logic(c, m6800_rd(ea)); break;                                 /* LDA */
            case 0x7: alu_logic(c, *acc); m6800_wr(ea, *acc); break;                            /* STA */
            case 0x8: *acc = alu_logic(c, (uint8_t)(*acc ^ m6800_rd(ea))); break;               /* EOR */
            case 0x9: *acc = alu_add(c, *acc, m6800_rd(ea), c->cc & M6800_C); break;            /* ADC */
            case 0xa: *acc = alu_logic(c, (uint8_t)(*acc | m6800_rd(ea))); break;               /* ORA */
            case 0xb: *acc = alu_add(c, *acc, m6800_rd(ea), 0); break;                          /* ADD */
            case 0xc:
                if (op & 0x40) { uint16_t d = m6800_rd16(ea); CC_CLR(M6800_V); NZ16(d); c->a = (uint8_t)(d >> 8); c->b = (uint8_t)d; }   /* LDD */
                else alu_sub16(c, c->x, m6800_rd16(ea), 0);                                     /* CPX */
                break;
            case 0xd:
                if (op & 0x40) { uint16_t d = (uint16_t)((c->a << 8) | c->b); CC_CLR(M6800_V); NZ16(d); m6800_wr(ea, c->a); m6800_wr((uint16_t)(ea + 1), c->b); }   /* STD */
                else { m6800_push(c, (uint8_t)c->pc); m6800_push(c, (uint8_t)(c->pc >> 8)); c->pc = ea; }   /* JSR */
                break;
            case 0xe: {                                                                          /* LDS / LDX */
                uint16_t d = m6800_rd16(ea); CC_CLR(M6800_V); NZ16(d);
                if (op & 0x40) c->x = d; else c->sp = d; break;
            }
            default: {                                                                           /* STS / STX */
                uint16_t d = (op & 0x40) ? c->x : c->sp; CC_CLR(M6800_V); NZ16(d);
                m6800_wr(ea, (uint8_t)(d >> 8)); m6800_wr((uint16_t)(ea + 1), (uint8_t)d); break;
            }
        }
        return cyc;
    }

    if (op >= 0x40) {
        /* read-modify-write on A, B, indexed, extended */
        int col = op & 0x0f, mode = (op >> 4) & 3;
        if (col == 0xe) {                                       /* JMP */
            if (mode == 2) c->pc = (uint16_t)(c->x + m6800_rd(c->pc));
            else if (mode == 3) c->pc = m6800_rd16(c->pc);
            return cyc;
        }
        if (mode == 0) c->a = alu_unary(c, col, c->a);
        else if (mode == 1) c->b = alu_unary(c, col, c->b);
        else {
            uint16_t ea = (mode == 2) ? (uint16_t)(c->x + m6800_rd(c->pc++)) : m6800_rd16(c->pc);
            if (mode == 3) c->pc = (uint16_t)(c->pc + 2);
            uint8_t v = alu_unary(c, col, m6800_rd(ea));
            if (col != 0xd) m6800_wr(ea, v);
        }
        return cyc;
    }

    if (op >= 0x20 && op < 0x30) {                              /* branches */
        int8_t off = (int8_t)m6800_rd(c->pc++);
        if (m6800_cond(c, op)) c->pc = (uint16_t)(c->pc + off);
        return cyc;
    }

    switch (op) {
        case 0x01: break;                                                                        /* NOP */
        case 0x04: { uint16_t d = (uint16_t)((c->a << 8) | c->b); CC_CLR(M6800_N | M6800_Z | M6800_V | M6800_C);   /* LSRD */
                     if (d & 1) CC_SET(M6800_C | M6800_V); d >>= 1; if (!d) CC_SET(M6800_Z); c->a = (uint8_t)(d >> 8); c->b = (uint8_t)d; break; }
        case 0x05: { unsigned d = (unsigned)((c->a << 8) | c->b) << 1; CC_CLR(M6800_N | M6800_Z | M6800_V | M6800_C);   /* ASLD */
                     if (d & 0x10000) CC_SET(M6800_C); NZ16(d); if (((c->cc >> 3) ^ c->cc) & 1) CC_SET(M6800_V);
                     c->a = (uint8_t)(d >> 8); c->b = (uint8_t)d; break; }
        case 0x06: c->cc = (uint8_t)(c->a | 0xc0); break;                                        /* TAP */
        case 0x07: c->a = (uint8_t)(c->cc | 0xc0); break;                                        /* TPA */
        case 0x08: c->x++; CC_CLR(M6800_Z); if (!c->x) CC_SET(M6800_Z); break;                    /* INX */
        case 0x09: c->x--; CC_CLR(M6800_Z); if (!c->x) CC_SET(M6800_Z); break;                    /* DEX */
        case 0x0a: CC_CLR(M6800_V); break;                                                       /* CLV */
        case 0x0b: CC_SET(M6800_V); break;                                                       /* SEV */
        case 0x0c: CC_CLR(M6800_C); break;                                                       /* CLC */
        case 0x0d: CC_SET(M6800_C); break;                                                       /* SEC */
        case 0x0e: CC_CLR(M6800_I); break;                                                       /* CLI */
        case 0x0f: CC_SET(M6800_I); break;                                                       /* SEI */
        case 0x10: c->a = alu_sub(c, c->a, c->b, 0); break;                                      /* SBA */
        case 0x11: alu_sub(c, c->a, c->b, 0); break;                                             /* CBA */
        case 0x16: c->b = alu_logic(c, c->a); break;                                             /* TAB */
        case 0x17: c->a = alu_logic(c, c->b); break;                                             /* TBA */
        case 0x19: {                                                                             /* DAA */
            unsigned a = c->a, msn = a & 0xf0, lsn = a & 0x0f, cf = 0;
            if (lsn > 0x09 || (c->cc & M6800_H)) cf |= 0x06;
            if (msn > 0x80 && lsn > 0x09) cf |= 0x60;
            if (msn > 0x90 || (c->cc & M6800_C)) cf |= 0x60;
            unsigned r = a + cf;
            CC_CLR(M6800_N | M6800_Z | M6800_V);
            if (cf & 0x60) CC_SET(M6800_C);
            NZ8(r); c->a = (uint8_t)r; break;
        }
        case 0x1b: c->a = alu_add(c, c->a, c->b, 0); break;                                      /* ABA */
        case 0x30: c->x = (uint16_t)(c->sp + 1); break;                                          /* TSX */
        case 0x31: c->sp++; break;                                                               /* INS */
        case 0x32: c->a = m6800_pull(c); break;                                                  /* PULA */
        case 0x33: c->b = m6800_pull(c); break;                                                  /* PULB */
        case 0x34: c->sp--; break;                                                               /* DES */
        case 0x35: c->sp = (uint16_t)(c->x - 1); break;                                          /* TXS */
        case 0x36: m6800_push(c, c->a); break;                                                   /* PSHA */
        case 0x37: m6800_push(c, c->b); break;                                                   /* PSHB */
        case 0x38: { uint8_t h = m6800_pull(c), l = m6800_pull(c); c->x = (uint16_t)((h << 8) | l); break; }   /* PULX */
        case 0x39: { uint8_t h = m6800_pull(c), l = m6800_pull(c); c->pc = (uint16_t)((h << 8) | l); break; }  /* RTS */
        case 0x3a: c->x = (uint16_t)(c->x + c->b); break;                                        /* ABX */
        case 0x3b: {                                                                             /* RTI */
            c->cc = (uint8_t)(m6800_pull(c) | 0xc0); c->b = m6800_pull(c); c->a = m6800_pull(c);
            uint8_t xh = m6800_pull(c), xl = m6800_pull(c); c->x = (uint16_t)((xh << 8) | xl);
            uint8_t ph = m6800_pull(c), pl = m6800_pull(c); c->pc = (uint16_t)((ph << 8) | pl); break;
        }
        case 0x3c: m6800_push(c, (uint8_t)c->x); m6800_push(c, (uint8_t)(c->x >> 8)); break;    /* PSHX */
        case 0x3d: { unsigned r = (unsigned)c->a * c->b; c->a = (uint8_t)(r >> 8); c->b = (uint8_t)r;   /* MUL */
                     CC_CLR(M6800_C); if (c->b & 0x80) CC_SET(M6800_C); break; }
        case 0x3e: m6800_push_all(c); c->wai = 1; break;                                         /* WAI */
        case 0x3f: m6800_push_all(c); CC_SET(M6800_I); c->pc = m6800_rd16(0xfffa); break;         /* SWI */
        default: break;                                                                          /* undefined: NOP */
    }
    return cyc;
}

#undef CC_SET
#undef CC_CLR
#undef NZ8
#undef NZ16
#endif
