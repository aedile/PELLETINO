/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * m6805.h - Motorola MC6805 interpreter, as used by the MC68705P5 on Arkanoid's board.
 *
 * The MCU reads the spinner and the buttons and hands them to the Z80 through a two-byte
 * handshake, with a little protection folded in. It is a small part - accumulator, index
 * register, six-bit stack pointer, five condition codes - but its instruction set is wide,
 * because almost every operation exists in six addressing modes and there are sixteen
 * bit-test-and-branch opcodes on top.
 *
 * Define before including:
 *   M6805_READ(a)      read a byte           M6805_WRITE(a,v)   write a byte
 * The address space is 13 bits. The caller is responsible for the I/O registers at 0x00-0x0F
 * and for the ROM; this core only fetches and executes.
 */
#ifndef M6805_H
#define M6805_H
#include <stdint.h>

#define CC_C 0x01
#define CC_Z 0x02
#define CC_N 0x04
#define CC_I 0x08
#define CC_H 0x10

typedef struct {
    uint8_t a, x, cc;
    uint8_t sp;                 /* only the low six bits count; the stack lives at 0x60-0x7F */
    uint16_t pc;
    uint8_t irq_line, stopped;
} m6805_t;

#define RD(a)   M6805_READ((uint16_t)((a) & 0x1fff))
#define WR(a,v) M6805_WRITE((uint16_t)((a) & 0x1fff), (uint8_t)(v))

static inline void m6805_push(m6805_t *c, uint8_t v)
{
    WR(0x60 | (c->sp & 0x1f), v);
    c->sp = (uint8_t)((c->sp - 1) & 0x1f);
}
static inline uint8_t m6805_pop(m6805_t *c)
{
    c->sp = (uint8_t)((c->sp + 1) & 0x1f);
    return RD(0x60 | (c->sp & 0x1f));
}

static void m6805_reset(m6805_t *c)
{
    c->a = c->x = 0;
    c->cc = CC_I;
    c->sp = 0x1f;
    c->irq_line = 0; c->stopped = 0;
    c->pc = (uint16_t)((RD(0x07fe) << 8) | RD(0x07ff));
}

#define SETNZ(v) do { uint8_t t_ = (uint8_t)(v); \
    c->cc = (uint8_t)((c->cc & ~(CC_N | CC_Z)) | ((t_ & 0x80) ? CC_N : 0) | (t_ ? 0 : CC_Z)); } while (0)

static inline uint8_t m6805_sub(m6805_t *c, uint8_t a, uint8_t b, uint8_t borrow)
{
    uint16_t r = (uint16_t)(a - b - borrow);
    c->cc &= (uint8_t)~(CC_C | CC_N | CC_Z);
    if (r & 0x100) c->cc |= CC_C;
    if (r & 0x80) c->cc |= CC_N;
    if (!(uint8_t)r) c->cc |= CC_Z;
    return (uint8_t)r;
}
static inline uint8_t m6805_add(m6805_t *c, uint8_t a, uint8_t b, uint8_t carry)
{
    uint16_t r = (uint16_t)(a + b + carry);
    c->cc &= (uint8_t)~(CC_C | CC_N | CC_Z | CC_H);
    if (r & 0x100) c->cc |= CC_C;
    if (((a & 0x0f) + (b & 0x0f) + carry) > 0x0f) c->cc |= CC_H;
    if (r & 0x80) c->cc |= CC_N;
    if (!(uint8_t)r) c->cc |= CC_Z;
    return (uint8_t)r;
}

/* run one instruction; returns the cycles it took */
static int m6805_step(m6805_t *c)
{
    if (c->irq_line && !(c->cc & CC_I)) {           /* the external interrupt */
        c->stopped = 0;
        m6805_push(c, (uint8_t)(c->pc & 0xff));
        m6805_push(c, (uint8_t)(c->pc >> 8));
        m6805_push(c, c->x);
        m6805_push(c, c->a);
        m6805_push(c, c->cc);
        c->cc |= CC_I;
        c->pc = (uint16_t)((RD(0x07fa) << 8) | RD(0x07fb));
        return 11;
    }
    if (c->stopped) return 1;

    uint8_t op = RD(c->pc++);
    uint8_t v;
    uint16_t ea = 0;
    int cyc = 3;

    #define FETCH()   RD(c->pc++)
    #define EA_DIR()  ea = FETCH()
    #define EA_EXT()  do { uint8_t h_ = FETCH(); ea = (uint16_t)((h_ << 8) | FETCH()); } while (0)
    #define EA_IX0()  ea = c->x
    #define EA_IX1()  ea = (uint16_t)(c->x + FETCH())
    #define EA_IX2()  do { uint8_t h_ = FETCH(); ea = (uint16_t)(((h_ << 8) | FETCH()) + c->x); } while (0)
    #define BRA(cond) do { int8_t r_ = (int8_t)FETCH(); if (cond) c->pc = (uint16_t)(c->pc + r_); cyc = 3; } while (0)

    /* the four op groups that are decided by the high nibble share their operations */
    switch (op & 0xf0) {
    case 0x00: {                                    /* BRSET n / BRCLR n, direct, relative */
        int bit = (op >> 1) & 7;
        uint8_t addr = FETCH();
        int8_t rel = (int8_t)FETCH();
        uint8_t val = RD(addr);
        int set = (val >> bit) & 1;
        c->cc = (uint8_t)((c->cc & ~CC_C) | (set ? CC_C : 0));
        if ((op & 1) ? !set : set) c->pc = (uint16_t)(c->pc + rel);
        return 5;
    }
    case 0x10: {                                    /* BSET n / BCLR n, direct */
        int bit = (op >> 1) & 7;
        uint8_t addr = FETCH();
        uint8_t val = RD(addr);
        if (op & 1) val &= (uint8_t)~(1 << bit); else val |= (uint8_t)(1 << bit);
        WR(addr, val);
        return 5;
    }
    case 0x20:                                      /* the conditional branches */
        switch (op & 0x0f) {
            case 0x0: BRA(1); break;                                  /* BRA */
            case 0x1: BRA(0); break;                                  /* BRN */
            case 0x2: BRA(!(c->cc & (CC_C | CC_Z))); break;           /* BHI */
            case 0x3: BRA(c->cc & (CC_C | CC_Z)); break;              /* BLS */
            case 0x4: BRA(!(c->cc & CC_C)); break;                    /* BCC */
            case 0x5: BRA(c->cc & CC_C); break;                       /* BCS */
            case 0x6: BRA(!(c->cc & CC_Z)); break;                    /* BNE */
            case 0x7: BRA(c->cc & CC_Z); break;                       /* BEQ */
            case 0x8: BRA(!(c->cc & CC_H)); break;                    /* BHCC */
            case 0x9: BRA(c->cc & CC_H); break;                       /* BHCS */
            case 0xa: BRA(!(c->cc & CC_N)); break;                    /* BPL */
            case 0xb: BRA(c->cc & CC_N); break;                       /* BMI */
            case 0xc: BRA(!(c->cc & CC_I)); break;                    /* BMC */
            case 0xd: BRA(c->cc & CC_I); break;                       /* BMS */
            default:  BRA(0); break;                                  /* BIL/BIH: no IRQ pin here */
        }
        return cyc;
    case 0x30: case 0x60: case 0x70: {              /* read-modify-write on memory */
        if ((op & 0xf0) == 0x30) { EA_DIR(); cyc = 5; }
        else if ((op & 0xf0) == 0x60) { EA_IX1(); cyc = 6; }
        else { EA_IX0(); cyc = 5; }
        goto rmw;
    }
    case 0x40: case 0x50: {                         /* the same operations on A or X */
        ea = 0xffff; cyc = 3;
        goto rmw;
    }
    default: break;
    }

    if (0) {
rmw:    {
        int is_a = ((op & 0xf0) == 0x40), is_x = ((op & 0xf0) == 0x50);
        v = is_a ? c->a : (is_x ? c->x : RD(ea));
        switch (op & 0x0f) {
            case 0x0: {                             /* NEG */
                uint8_t r = (uint8_t)(0 - v);
                c->cc &= (uint8_t)~(CC_C | CC_N | CC_Z);
                if (v) c->cc |= CC_C;
                if (r & 0x80) c->cc |= CC_N;
                if (!r) c->cc |= CC_Z;
                v = r; break;
            }
            case 0x3: v = (uint8_t)~v; SETNZ(v); c->cc |= CC_C; break;          /* COM */
            case 0x4: c->cc = (uint8_t)((c->cc & ~CC_C) | (v & 1));             /* LSR */
                      v = (uint8_t)(v >> 1); SETNZ(v); c->cc &= (uint8_t)~CC_N; break;
            case 0x6: { uint8_t cy = (uint8_t)((c->cc & CC_C) ? 0x80 : 0);      /* ROR */
                        c->cc = (uint8_t)((c->cc & ~CC_C) | (v & 1));
                        v = (uint8_t)((v >> 1) | cy); SETNZ(v); break; }
            case 0x7: c->cc = (uint8_t)((c->cc & ~CC_C) | (v & 1));             /* ASR */
                      v = (uint8_t)((v >> 1) | (v & 0x80)); SETNZ(v); break;
            case 0x8: c->cc = (uint8_t)((c->cc & ~CC_C) | ((v >> 7) & 1));      /* LSL/ASL */
                      v = (uint8_t)(v << 1); SETNZ(v); break;
            case 0x9: { uint8_t cy = (uint8_t)((c->cc & CC_C) ? 1 : 0);         /* ROL */
                        c->cc = (uint8_t)((c->cc & ~CC_C) | ((v >> 7) & 1));
                        v = (uint8_t)((v << 1) | cy); SETNZ(v); break; }
            case 0xa: v = (uint8_t)(v - 1); SETNZ(v); break;                    /* DEC */
            case 0xc: v = (uint8_t)(v + 1); SETNZ(v); break;                    /* INC */
            case 0xd: SETNZ(v); break;                                          /* TST */
            case 0xf: v = 0; SETNZ(v); break;                                   /* CLR */
            default: break;
        }
        if ((op & 0x0f) != 0x0d) {                  /* TST does not write back */
            if (is_a) c->a = v; else if (is_x) c->x = v; else WR(ea, v);
        }
        return cyc;
    }
    }

    /* inherent, and the six addressing modes of the arithmetic group */
    switch (op) {
    case 0x80:                                      /* RTI */
        c->cc = m6805_pop(c); c->a = m6805_pop(c); c->x = m6805_pop(c);
        c->pc = (uint16_t)(m6805_pop(c) << 8); c->pc |= m6805_pop(c);
        return 9;
    case 0x81:                                      /* RTS */
        c->pc = (uint16_t)(m6805_pop(c) << 8); c->pc |= m6805_pop(c);
        return 6;
    case 0x83:                                      /* SWI */
        m6805_push(c, (uint8_t)(c->pc & 0xff)); m6805_push(c, (uint8_t)(c->pc >> 8));
        m6805_push(c, c->x); m6805_push(c, c->a); m6805_push(c, c->cc);
        c->cc |= CC_I;
        c->pc = (uint16_t)((RD(0x07fc) << 8) | RD(0x07fd));
        return 11;
    case 0x8e: case 0x8f: c->stopped = 1; return 2;                 /* STOP / WAIT */
    case 0x97: c->x = c->a; return 2;                               /* TAX */
    case 0x98: c->cc &= (uint8_t)~CC_C; return 2;
    case 0x99: c->cc |= CC_C; return 2;
    case 0x9a: c->cc &= (uint8_t)~CC_I; return 2;
    case 0x9b: c->cc |= CC_I; return 2;
    case 0x9c: c->sp = 0x1f; return 2;                              /* RSP */
    case 0x9d: return 2;                                            /* NOP */
    case 0x9f: c->a = c->x; return 2;                               /* TXA */
    default: break;
    }

    {
        int mode = op >> 4;                         /* A immediate, B direct, C ext, D/E/F indexed */
        int fn = op & 0x0f;
        uint8_t imm = 0;
        switch (mode) {
            case 0xa: ea = c->pc++; cyc = 2; break;
            case 0xb: EA_DIR(); cyc = 3; break;
            case 0xc: EA_EXT(); cyc = 4; break;
            case 0xd: EA_IX2(); cyc = 5; break;
            case 0xe: EA_IX1(); cyc = 4; break;
            case 0xf: EA_IX0(); cyc = 3; break;
            default: return 2;
        }
        if (fn == 0x0c) {                           /* JMP */
            c->pc = ea; return cyc;
        }
        if (fn == 0x0d) {                           /* JSR, and BSR in immediate mode */
            if (mode == 0xa) { int8_t r = (int8_t)RD(ea); ea = (uint16_t)(c->pc + r); }
            m6805_push(c, (uint8_t)(c->pc & 0xff));
            m6805_push(c, (uint8_t)(c->pc >> 8));
            c->pc = ea; return cyc + 3;
        }
        if (fn == 0x07 || fn == 0x0f) {             /* STA / STX write instead of read */
            v = (fn == 0x07) ? c->a : c->x;
            SETNZ(v); WR(ea, v); return cyc + 1;
        }
        imm = RD(ea);
        switch (fn) {
            case 0x0: c->a = m6805_sub(c, c->a, imm, 0); break;             /* SUB */
            case 0x1: m6805_sub(c, c->a, imm, 0); break;                    /* CMP */
            case 0x2: c->a = m6805_sub(c, c->a, imm, (uint8_t)((c->cc & CC_C) ? 1 : 0)); break;
            case 0x3: m6805_sub(c, c->x, imm, 0); break;                    /* CPX */
            case 0x4: c->a &= imm; SETNZ(c->a); break;                      /* AND */
            case 0x5: SETNZ((uint8_t)(c->a & imm)); break;                  /* BIT */
            case 0x6: c->a = imm; SETNZ(c->a); break;                       /* LDA */
            case 0x8: c->a ^= imm; SETNZ(c->a); break;                      /* EOR */
            case 0x9: c->a = m6805_add(c, c->a, imm, (uint8_t)((c->cc & CC_C) ? 1 : 0)); break;
            case 0xa: c->a |= imm; SETNZ(c->a); break;                      /* ORA */
            case 0xb: c->a = m6805_add(c, c->a, imm, 0); break;             /* ADD */
            case 0xe: c->x = imm; SETNZ(c->x); break;                       /* LDX */
            default: break;
        }
        return cyc;
    }
    #undef FETCH
    #undef EA_DIR
    #undef EA_EXT
    #undef EA_IX0
    #undef EA_IX1
    #undef EA_IX2
    #undef BRA
}

#undef RD
#undef WR
#endif
