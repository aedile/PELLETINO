/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * m6502fast.h - a compact instruction-stepped NMOS 6502.
 *
 * The cycle-stepped core we started with (Andre Weissflog's chips m6502) is the honest way to
 * model a 6502, but it carries its whole pin state in a uint64_t. On a 32-bit RISC-V every pin
 * poke costs two instructions, and it works out at roughly 200 CPU clocks per emulated 6502
 * cycle - about four times what an ESP32-C6 can afford for a 1.25 MHz CPU. This core runs one
 * whole instruction per call instead and reports how many cycles it took.
 *
 * The one thing Missile Command needs from a cycle-accurate core is MADSEL: the data access
 * five cycles after an opcode fetch whose low five bits are 1 goes to video RAM instead of
 * normal address decoding. Those opcodes are exactly the eight indexed-indirect (zp,X) ones,
 * all six cycles long, and their data access is always their sixth cycle - so at instruction
 * granularity MADSEL is simply "the data access of a (zp,X) instruction", which this core
 * routes through separate hooks.
 *
 * Define before including:
 *   M6502F_READ(a)        read a byte: opcode, operand, pointer and stack cycles
 *   M6502F_WRITE(a,v)     write a byte
 *   M6502F_FETCHOP(a)     read an opcode (optional; defaults to M6502F_READ)
 *   M6502F_IZX_READ(a)    the data read of a (zp,X) instruction   (defaults to M6502F_READ)
 *   M6502F_IZX_WRITE(a,v) the data write of a (zp,X) instruction  (defaults to M6502F_WRITE)
 */
#ifndef M6502FAST_H
#define M6502FAST_H
#include <stdint.h>

#define M6502F_C 0x01
#define M6502F_Z 0x02
#define M6502F_I 0x04
#define M6502F_D 0x08
#define M6502F_B 0x10
#define M6502F_U 0x20
#define M6502F_V 0x40
#define M6502F_N 0x80

typedef struct {
    uint16_t pc;
    uint8_t a, x, y, s, p;
} m6502f_t;

#ifndef M6502F_IZX_READ
#define M6502F_IZX_READ(a) M6502F_READ(a)
#endif
#ifndef M6502F_IZX_WRITE
#define M6502F_IZX_WRITE(a, v) M6502F_WRITE(a, v)
#endif

#ifndef M6502F_FETCHOP
#define M6502F_FETCHOP(a) M6502F_READ(a)   /* an opcode fetch, for hardware that treats those differently */
#endif
#define MRD(a)    M6502F_READ((uint16_t)(a))
#define MWR(a, v) M6502F_WRITE((uint16_t)(a), (uint8_t)(v))
#define MFETCH()  MRD(c->pc++)

#define SETNZ(val) do { uint8_t t_ = (uint8_t)(val); \
    c->p = (uint8_t)((c->p & ~(M6502F_N | M6502F_Z)) | (t_ & M6502F_N) | (t_ ? 0 : M6502F_Z)); } while (0)

/* Decimal mode follows Bruce Clark's description of the NMOS part: N and V are taken from the
 * value before the high-nibble fixup, and Z always comes from the plain binary sum. */
static inline void m6502f_adc(m6502f_t *c, uint8_t v)
{
    uint8_t cin = (uint8_t)(c->p & M6502F_C);
    uint16_t bin = (uint16_t)(c->a + v + cin);
    c->p &= (uint8_t)~(M6502F_N | M6502F_V | M6502F_Z | M6502F_C);
    if (!(uint8_t)bin) c->p |= M6502F_Z;
    if (c->p & M6502F_D) {
        uint16_t al = (uint16_t)((c->a & 0x0f) + (v & 0x0f) + cin);
        if (al >= 0x0a) al = (uint16_t)(((al + 6) & 0x0f) + 0x10);
        int32_t r = (int32_t)((c->a & 0xf0) + (v & 0xf0) + al);
        if (r & 0x80) c->p |= M6502F_N;
        if (((c->a ^ (uint8_t)r) & ~(c->a ^ v)) & 0x80) c->p |= M6502F_V;
        if (r >= 0xa0) r += 0x60;
        if (r >= 0x100) c->p |= M6502F_C;
        c->a = (uint8_t)r;
    } else {
        if (bin & 0x100) c->p |= M6502F_C;
        if (bin & 0x80) c->p |= M6502F_N;
        if (((c->a ^ bin) & (v ^ bin)) & 0x80) c->p |= M6502F_V;
        c->a = (uint8_t)bin;
    }
}

static inline void m6502f_sbc(m6502f_t *c, uint8_t v)
{
    uint8_t borrow = (uint8_t)((c->p & M6502F_C) ^ M6502F_C);
    uint16_t bin = (uint16_t)(c->a - v - borrow);
    c->p &= (uint8_t)~(M6502F_N | M6502F_V | M6502F_Z | M6502F_C);
    if (!(uint8_t)bin) c->p |= M6502F_Z;
    if (bin & 0x80) c->p |= M6502F_N;
    if (((c->a ^ v) & (c->a ^ (uint8_t)bin)) & 0x80) c->p |= M6502F_V;
    if (!(bin & 0x100)) c->p |= M6502F_C;
    if (c->p & M6502F_D) {
        int16_t al = (int16_t)((c->a & 0x0f) - (v & 0x0f) - borrow);
        if (al < 0) al = (int16_t)(((al - 6) & 0x0f) - 0x10);
        int32_t r = (int32_t)((c->a & 0xf0) - (v & 0xf0) + al);
        if (r < 0) r -= 0x60;
        c->a = (uint8_t)r;
    } else {
        c->a = (uint8_t)bin;
    }
}

static inline void m6502f_cmp(m6502f_t *c, uint8_t r, uint8_t v)
{
    uint8_t t = (uint8_t)(r - v);
    c->p = (uint8_t)((c->p & ~(M6502F_N | M6502F_Z | M6502F_C)) | (t & M6502F_N) |
                     (t ? 0 : M6502F_Z) | (r >= v ? M6502F_C : 0));
}

/* push PC and P, then vector; shared by BRK, IRQ and NMI */
static inline int m6502f_interrupt(m6502f_t *c, uint16_t vec, int brk)
{
    MWR(0x0100 + c->s--, c->pc >> 8);
    MWR(0x0100 + c->s--, c->pc & 0xff);
    MWR(0x0100 + c->s--, (uint8_t)(c->p | M6502F_U | (brk ? M6502F_B : 0)));
    c->p |= M6502F_I;
    c->pc = (uint16_t)(MRD(vec) | (MRD(vec + 1) << 8));
    return 7;
}
static inline int m6502f_irq(m6502f_t *c) { return m6502f_interrupt(c, 0xfffe, 0); }
static inline int m6502f_nmi(m6502f_t *c) { return m6502f_interrupt(c, 0xfffa, 0); }

static inline void m6502f_reset(m6502f_t *c)
{
    c->a = c->x = c->y = 0;
    c->s = 0xfd;
    c->p = M6502F_U | M6502F_I;
    c->pc = (uint16_t)(MRD(0xfffc) | (MRD(0xfffd) << 8));
}

#ifdef M6502F_COUNT_UNDOC
uint32_t m6502f_undoc[256];
#endif

/* run one instruction; returns the number of cycles it took */
static int m6502f_step(m6502f_t *c)
{
    uint16_t addr = 0;
    uint8_t v;
    int cyc;
    uint8_t op = M6502F_FETCHOP((uint16_t)(c->pc)); c->pc++;

    #define A_IMM()   addr = c->pc++
    #define A_ZP()    addr = MFETCH()
    #define A_ZPX()   addr = (uint8_t)(MFETCH() + c->x)
    #define A_ZPY()   addr = (uint8_t)(MFETCH() + c->y)
    #define A_ABS()   do { uint8_t lo_ = MFETCH(); addr = (uint16_t)(lo_ | (MFETCH() << 8)); } while (0)
    /* indexing that carries into the high byte costs an extra cycle, but only on reads */
    #define A_ABX(pen) do { uint8_t lo_ = MFETCH(); uint16_t b_ = (uint16_t)(lo_ | (MFETCH() << 8)); \
                            addr = (uint16_t)(b_ + c->x); if ((pen) && ((b_ ^ addr) & 0xff00)) cyc++; } while (0)
    #define A_ABY(pen) do { uint8_t lo_ = MFETCH(); uint16_t b_ = (uint16_t)(lo_ | (MFETCH() << 8)); \
                            addr = (uint16_t)(b_ + c->y); if ((pen) && ((b_ ^ addr) & 0xff00)) cyc++; } while (0)
    #define A_IZX()   do { uint8_t z_ = (uint8_t)(MFETCH() + c->x); \
                           addr = (uint16_t)(MRD(z_) | (MRD((uint8_t)(z_ + 1)) << 8)); } while (0)
    #define A_IZY(pen) do { uint8_t z_ = MFETCH(); \
                            uint16_t b_ = (uint16_t)(MRD(z_) | (MRD((uint8_t)(z_ + 1)) << 8)); \
                            addr = (uint16_t)(b_ + c->y); if ((pen) && ((b_ ^ addr) & 0xff00)) cyc++; } while (0)
    #define BRANCH(cond) do { int8_t d_ = (int8_t)MFETCH(); \
                              if (cond) { uint16_t t_ = (uint16_t)(c->pc + d_); \
                                          cyc += ((c->pc ^ t_) & 0xff00) ? 2 : 1; c->pc = t_; } } while (0)
    #define PUSH(x)   MWR(0x0100 + c->s--, (x))
    #define POP()     MRD(0x0100 + ++c->s)
    /* read-modify-write: the NMOS part writes the unmodified byte back first */
    #define DO_ASL()  do { v = MRD(addr); MWR(addr, v); c->p = (uint8_t)((c->p & ~M6502F_C) | (v >> 7)); \
                           v = (uint8_t)(v << 1); MWR(addr, v); SETNZ(v); } while (0)
    #define DO_LSR()  do { v = MRD(addr); MWR(addr, v); c->p = (uint8_t)((c->p & ~M6502F_C) | (v & 1)); \
                           v = (uint8_t)(v >> 1); MWR(addr, v); SETNZ(v); } while (0)
    #define DO_ROL()  do { v = MRD(addr); MWR(addr, v); uint8_t nc_ = (uint8_t)(v >> 7); \
                           v = (uint8_t)((v << 1) | (c->p & M6502F_C)); \
                           c->p = (uint8_t)((c->p & ~M6502F_C) | nc_); MWR(addr, v); SETNZ(v); } while (0)
    #define DO_ROR()  do { v = MRD(addr); MWR(addr, v); uint8_t nc_ = (uint8_t)(v & 1); \
                           v = (uint8_t)((v >> 1) | ((c->p & M6502F_C) << 7)); \
                           c->p = (uint8_t)((c->p & ~M6502F_C) | nc_); MWR(addr, v); SETNZ(v); } while (0)
    #define DO_INC(d) do { v = MRD(addr); MWR(addr, v); v = (uint8_t)(v + (d)); MWR(addr, v); SETNZ(v); } while (0)
    #define DO_BIT()  do { v = MRD(addr); \
                           c->p = (uint8_t)((c->p & ~(M6502F_N | M6502F_V | M6502F_Z)) | \
                                            (v & (M6502F_N | M6502F_V)) | ((c->a & v) ? 0 : M6502F_Z)); } while (0)

    switch (op) {
    /* ---- ORA ---- */
    case 0x09: cyc = 2; A_IMM();  c->a |= MRD(addr); SETNZ(c->a); break;
    case 0x05: cyc = 3; A_ZP();   c->a |= MRD(addr); SETNZ(c->a); break;
    case 0x15: cyc = 4; A_ZPX();  c->a |= MRD(addr); SETNZ(c->a); break;
    case 0x0d: cyc = 4; A_ABS();  c->a |= MRD(addr); SETNZ(c->a); break;
    case 0x1d: cyc = 4; A_ABX(1); c->a |= MRD(addr); SETNZ(c->a); break;
    case 0x19: cyc = 4; A_ABY(1); c->a |= MRD(addr); SETNZ(c->a); break;
    case 0x01: cyc = 6; A_IZX();  c->a |= M6502F_IZX_READ(addr); SETNZ(c->a); break;
    case 0x11: cyc = 5; A_IZY(1); c->a |= MRD(addr); SETNZ(c->a); break;
    /* ---- AND ---- */
    case 0x29: cyc = 2; A_IMM();  c->a &= MRD(addr); SETNZ(c->a); break;
    case 0x25: cyc = 3; A_ZP();   c->a &= MRD(addr); SETNZ(c->a); break;
    case 0x35: cyc = 4; A_ZPX();  c->a &= MRD(addr); SETNZ(c->a); break;
    case 0x2d: cyc = 4; A_ABS();  c->a &= MRD(addr); SETNZ(c->a); break;
    case 0x3d: cyc = 4; A_ABX(1); c->a &= MRD(addr); SETNZ(c->a); break;
    case 0x39: cyc = 4; A_ABY(1); c->a &= MRD(addr); SETNZ(c->a); break;
    case 0x21: cyc = 6; A_IZX();  c->a &= M6502F_IZX_READ(addr); SETNZ(c->a); break;
    case 0x31: cyc = 5; A_IZY(1); c->a &= MRD(addr); SETNZ(c->a); break;
    /* ---- EOR ---- */
    case 0x49: cyc = 2; A_IMM();  c->a ^= MRD(addr); SETNZ(c->a); break;
    case 0x45: cyc = 3; A_ZP();   c->a ^= MRD(addr); SETNZ(c->a); break;
    case 0x55: cyc = 4; A_ZPX();  c->a ^= MRD(addr); SETNZ(c->a); break;
    case 0x4d: cyc = 4; A_ABS();  c->a ^= MRD(addr); SETNZ(c->a); break;
    case 0x5d: cyc = 4; A_ABX(1); c->a ^= MRD(addr); SETNZ(c->a); break;
    case 0x59: cyc = 4; A_ABY(1); c->a ^= MRD(addr); SETNZ(c->a); break;
    case 0x41: cyc = 6; A_IZX();  c->a ^= M6502F_IZX_READ(addr); SETNZ(c->a); break;
    case 0x51: cyc = 5; A_IZY(1); c->a ^= MRD(addr); SETNZ(c->a); break;
    /* ---- ADC ---- */
    case 0x69: cyc = 2; A_IMM();  m6502f_adc(c, MRD(addr)); break;
    case 0x65: cyc = 3; A_ZP();   m6502f_adc(c, MRD(addr)); break;
    case 0x75: cyc = 4; A_ZPX();  m6502f_adc(c, MRD(addr)); break;
    case 0x6d: cyc = 4; A_ABS();  m6502f_adc(c, MRD(addr)); break;
    case 0x7d: cyc = 4; A_ABX(1); m6502f_adc(c, MRD(addr)); break;
    case 0x79: cyc = 4; A_ABY(1); m6502f_adc(c, MRD(addr)); break;
    case 0x61: cyc = 6; A_IZX();  m6502f_adc(c, M6502F_IZX_READ(addr)); break;
    case 0x71: cyc = 5; A_IZY(1); m6502f_adc(c, MRD(addr)); break;
    /* ---- SBC ---- */
    case 0xe9: cyc = 2; A_IMM();  m6502f_sbc(c, MRD(addr)); break;
    case 0xe5: cyc = 3; A_ZP();   m6502f_sbc(c, MRD(addr)); break;
    case 0xf5: cyc = 4; A_ZPX();  m6502f_sbc(c, MRD(addr)); break;
    case 0xed: cyc = 4; A_ABS();  m6502f_sbc(c, MRD(addr)); break;
    case 0xfd: cyc = 4; A_ABX(1); m6502f_sbc(c, MRD(addr)); break;
    case 0xf9: cyc = 4; A_ABY(1); m6502f_sbc(c, MRD(addr)); break;
    case 0xe1: cyc = 6; A_IZX();  m6502f_sbc(c, M6502F_IZX_READ(addr)); break;
    case 0xf1: cyc = 5; A_IZY(1); m6502f_sbc(c, MRD(addr)); break;
    /* ---- CMP / CPX / CPY ---- */
    case 0xc9: cyc = 2; A_IMM();  m6502f_cmp(c, c->a, MRD(addr)); break;
    case 0xc5: cyc = 3; A_ZP();   m6502f_cmp(c, c->a, MRD(addr)); break;
    case 0xd5: cyc = 4; A_ZPX();  m6502f_cmp(c, c->a, MRD(addr)); break;
    case 0xcd: cyc = 4; A_ABS();  m6502f_cmp(c, c->a, MRD(addr)); break;
    case 0xdd: cyc = 4; A_ABX(1); m6502f_cmp(c, c->a, MRD(addr)); break;
    case 0xd9: cyc = 4; A_ABY(1); m6502f_cmp(c, c->a, MRD(addr)); break;
    case 0xc1: cyc = 6; A_IZX();  m6502f_cmp(c, c->a, M6502F_IZX_READ(addr)); break;
    case 0xd1: cyc = 5; A_IZY(1); m6502f_cmp(c, c->a, MRD(addr)); break;
    case 0xe0: cyc = 2; A_IMM();  m6502f_cmp(c, c->x, MRD(addr)); break;
    case 0xe4: cyc = 3; A_ZP();   m6502f_cmp(c, c->x, MRD(addr)); break;
    case 0xec: cyc = 4; A_ABS();  m6502f_cmp(c, c->x, MRD(addr)); break;
    case 0xc0: cyc = 2; A_IMM();  m6502f_cmp(c, c->y, MRD(addr)); break;
    case 0xc4: cyc = 3; A_ZP();   m6502f_cmp(c, c->y, MRD(addr)); break;
    case 0xcc: cyc = 4; A_ABS();  m6502f_cmp(c, c->y, MRD(addr)); break;
    /* ---- LDA / LDX / LDY ---- */
    case 0xa9: cyc = 2; A_IMM();  c->a = MRD(addr); SETNZ(c->a); break;
    case 0xa5: cyc = 3; A_ZP();   c->a = MRD(addr); SETNZ(c->a); break;
    case 0xb5: cyc = 4; A_ZPX();  c->a = MRD(addr); SETNZ(c->a); break;
    case 0xad: cyc = 4; A_ABS();  c->a = MRD(addr); SETNZ(c->a); break;
    case 0xbd: cyc = 4; A_ABX(1); c->a = MRD(addr); SETNZ(c->a); break;
    case 0xb9: cyc = 4; A_ABY(1); c->a = MRD(addr); SETNZ(c->a); break;
    case 0xa1: cyc = 6; A_IZX();  c->a = M6502F_IZX_READ(addr); SETNZ(c->a); break;
    case 0xb1: cyc = 5; A_IZY(1); c->a = MRD(addr); SETNZ(c->a); break;
    case 0xa2: cyc = 2; A_IMM();  c->x = MRD(addr); SETNZ(c->x); break;
    case 0xa6: cyc = 3; A_ZP();   c->x = MRD(addr); SETNZ(c->x); break;
    case 0xb6: cyc = 4; A_ZPY();  c->x = MRD(addr); SETNZ(c->x); break;
    case 0xae: cyc = 4; A_ABS();  c->x = MRD(addr); SETNZ(c->x); break;
    case 0xbe: cyc = 4; A_ABY(1); c->x = MRD(addr); SETNZ(c->x); break;
    case 0xa0: cyc = 2; A_IMM();  c->y = MRD(addr); SETNZ(c->y); break;
    case 0xa4: cyc = 3; A_ZP();   c->y = MRD(addr); SETNZ(c->y); break;
    case 0xb4: cyc = 4; A_ZPX();  c->y = MRD(addr); SETNZ(c->y); break;
    case 0xac: cyc = 4; A_ABS();  c->y = MRD(addr); SETNZ(c->y); break;
    case 0xbc: cyc = 4; A_ABX(1); c->y = MRD(addr); SETNZ(c->y); break;
    /* ---- STA / STX / STY ---- */
    case 0x85: cyc = 3; A_ZP();   MWR(addr, c->a); break;
    case 0x95: cyc = 4; A_ZPX();  MWR(addr, c->a); break;
    case 0x8d: cyc = 4; A_ABS();  MWR(addr, c->a); break;
    case 0x9d: cyc = 5; A_ABX(0); MWR(addr, c->a); break;
    case 0x99: cyc = 5; A_ABY(0); MWR(addr, c->a); break;
    case 0x81: cyc = 6; A_IZX();  M6502F_IZX_WRITE(addr, c->a); break;
    case 0x91: cyc = 6; A_IZY(0); MWR(addr, c->a); break;
    case 0x86: cyc = 3; A_ZP();   MWR(addr, c->x); break;
    case 0x96: cyc = 4; A_ZPY();  MWR(addr, c->x); break;
    case 0x8e: cyc = 4; A_ABS();  MWR(addr, c->x); break;
    case 0x84: cyc = 3; A_ZP();   MWR(addr, c->y); break;
    case 0x94: cyc = 4; A_ZPX();  MWR(addr, c->y); break;
    case 0x8c: cyc = 4; A_ABS();  MWR(addr, c->y); break;
    /* ---- INC / DEC ---- */
    case 0xe6: cyc = 5; A_ZP();   DO_INC(+1); break;
    case 0xf6: cyc = 6; A_ZPX();  DO_INC(+1); break;
    case 0xee: cyc = 6; A_ABS();  DO_INC(+1); break;
    case 0xfe: cyc = 7; A_ABX(0); DO_INC(+1); break;
    case 0xc6: cyc = 5; A_ZP();   DO_INC(-1); break;
    case 0xd6: cyc = 6; A_ZPX();  DO_INC(-1); break;
    case 0xce: cyc = 6; A_ABS();  DO_INC(-1); break;
    case 0xde: cyc = 7; A_ABX(0); DO_INC(-1); break;
    case 0xe8: cyc = 2; c->x++; SETNZ(c->x); break;
    case 0xc8: cyc = 2; c->y++; SETNZ(c->y); break;
    case 0xca: cyc = 2; c->x--; SETNZ(c->x); break;
    case 0x88: cyc = 2; c->y--; SETNZ(c->y); break;
    /* ---- shifts and rotates ---- */
    case 0x0a: cyc = 2; c->p = (uint8_t)((c->p & ~M6502F_C) | (c->a >> 7));
                        c->a = (uint8_t)(c->a << 1); SETNZ(c->a); break;
    case 0x06: cyc = 5; A_ZP();   DO_ASL(); break;
    case 0x16: cyc = 6; A_ZPX();  DO_ASL(); break;
    case 0x0e: cyc = 6; A_ABS();  DO_ASL(); break;
    case 0x1e: cyc = 7; A_ABX(0); DO_ASL(); break;
    case 0x4a: cyc = 2; c->p = (uint8_t)((c->p & ~M6502F_C) | (c->a & 1));
                        c->a = (uint8_t)(c->a >> 1); SETNZ(c->a); break;
    case 0x46: cyc = 5; A_ZP();   DO_LSR(); break;
    case 0x56: cyc = 6; A_ZPX();  DO_LSR(); break;
    case 0x4e: cyc = 6; A_ABS();  DO_LSR(); break;
    case 0x5e: cyc = 7; A_ABX(0); DO_LSR(); break;
    case 0x2a: cyc = 2; { uint8_t nc = (uint8_t)(c->a >> 7);
                          c->a = (uint8_t)((c->a << 1) | (c->p & M6502F_C));
                          c->p = (uint8_t)((c->p & ~M6502F_C) | nc); SETNZ(c->a); } break;
    case 0x26: cyc = 5; A_ZP();   DO_ROL(); break;
    case 0x36: cyc = 6; A_ZPX();  DO_ROL(); break;
    case 0x2e: cyc = 6; A_ABS();  DO_ROL(); break;
    case 0x3e: cyc = 7; A_ABX(0); DO_ROL(); break;
    case 0x6a: cyc = 2; { uint8_t nc = (uint8_t)(c->a & 1);
                          c->a = (uint8_t)((c->a >> 1) | ((c->p & M6502F_C) << 7));
                          c->p = (uint8_t)((c->p & ~M6502F_C) | nc); SETNZ(c->a); } break;
    case 0x66: cyc = 5; A_ZP();   DO_ROR(); break;
    case 0x76: cyc = 6; A_ZPX();  DO_ROR(); break;
    case 0x6e: cyc = 6; A_ABS();  DO_ROR(); break;
    case 0x7e: cyc = 7; A_ABX(0); DO_ROR(); break;
    /* ---- BIT ---- */
    case 0x24: cyc = 3; A_ZP();   DO_BIT(); break;
    case 0x2c: cyc = 4; A_ABS();  DO_BIT(); break;
    /* ---- branches ---- */
    case 0x10: cyc = 2; BRANCH(!(c->p & M6502F_N)); break;
    case 0x30: cyc = 2; BRANCH( (c->p & M6502F_N)); break;
    case 0x50: cyc = 2; BRANCH(!(c->p & M6502F_V)); break;
    case 0x70: cyc = 2; BRANCH( (c->p & M6502F_V)); break;
    case 0x90: cyc = 2; BRANCH(!(c->p & M6502F_C)); break;
    case 0xb0: cyc = 2; BRANCH( (c->p & M6502F_C)); break;
    case 0xd0: cyc = 2; BRANCH(!(c->p & M6502F_Z)); break;
    case 0xf0: cyc = 2; BRANCH( (c->p & M6502F_Z)); break;
    /* ---- jumps, calls and returns ---- */
    case 0x4c: cyc = 3; A_ABS(); c->pc = addr; break;
    case 0x6c: cyc = 5; A_ABS();                       /* the NMOS indirect-JMP page-wrap bug */
               c->pc = (uint16_t)(MRD(addr) | (MRD((uint16_t)((addr & 0xff00) | ((addr + 1) & 0xff))) << 8));
               break;
    case 0x20: cyc = 6; { uint8_t lo = MFETCH(); uint16_t ret = c->pc;  /* pushes the address of the last operand byte */
                          PUSH(ret >> 8); PUSH(ret & 0xff);
                          c->pc = (uint16_t)(lo | (MRD(ret) << 8)); } break;
    case 0x60: cyc = 6; { uint8_t lo = POP(); uint8_t hi = POP(); c->pc = (uint16_t)((lo | (hi << 8)) + 1); } break;
    case 0x40: cyc = 6; { c->p = (uint8_t)((POP() & ~M6502F_B) | M6502F_U);
                          uint8_t lo = POP(); uint8_t hi = POP(); c->pc = (uint16_t)(lo | (hi << 8)); } break;
    case 0x00: cyc = 7; c->pc++; m6502f_interrupt(c, 0xfffe, 1); break;
    /* ---- stack and transfers ---- */
    case 0x48: cyc = 3; PUSH(c->a); break;
    case 0x68: cyc = 4; c->a = POP(); SETNZ(c->a); break;
    case 0x08: cyc = 3; PUSH(c->p | M6502F_U | M6502F_B); break;
    case 0x28: cyc = 4; c->p = (uint8_t)((POP() & ~M6502F_B) | M6502F_U); break;
    case 0xaa: cyc = 2; c->x = c->a; SETNZ(c->x); break;
    case 0x8a: cyc = 2; c->a = c->x; SETNZ(c->a); break;
    case 0xa8: cyc = 2; c->y = c->a; SETNZ(c->y); break;
    case 0x98: cyc = 2; c->a = c->y; SETNZ(c->a); break;
    case 0xba: cyc = 2; c->x = c->s; SETNZ(c->x); break;
    case 0x9a: cyc = 2; c->s = c->x; break;
    /* ---- flags ---- */
    case 0x18: cyc = 2; c->p &= (uint8_t)~M6502F_C; break;
    case 0x38: cyc = 2; c->p |= M6502F_C; break;
    case 0x58: cyc = 2; c->p &= (uint8_t)~M6502F_I; break;
    case 0x78: cyc = 2; c->p |= M6502F_I; break;
    case 0xb8: cyc = 2; c->p &= (uint8_t)~M6502F_V; break;
    case 0xd8: cyc = 2; c->p &= (uint8_t)~M6502F_D; break;
    case 0xf8: cyc = 2; c->p |= M6502F_D; break;
    case 0xea: cyc = 2; break;
    default:   cyc = 2;                        /* undocumented opcodes behave as NOP here */
#ifdef M6502F_COUNT_UNDOC
               m6502f_undoc[op]++;
#endif
               break;
    }
    (void)v;
    return cyc;
}

#undef MRD
#undef MWR
#undef MFETCH
#endif
