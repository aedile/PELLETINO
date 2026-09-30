/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * cps1_mem.h - the 68000's address space, inline.
 *
 *   000000-0FFFFF  program ROM (1 MB of the 4 MB the board decodes)
 *   800000-8001FF  inputs, CPS-A and CPS-B registers, sound latches
 *   900000-92FFFF  graphics RAM
 *   FF0000-FFFFFF  work RAM
 *
 * Everything is kept as 16-bit words in the host's byte order, the ROM included (the
 * converter swaps it), so a word access is one load and a byte access is a load and a shift.
 * Both 68000 cores include this, so they see the same machine.
 */
#ifndef CPS1_MEM_H
#define CPS1_MEM_H

#include <stdint.h>
#include "knobs_m68k.h"

#define CPS1_PROG_BYTES 0x100000
#define CPS1_GFXRAM_BYTES 0x30000
#define CPS1_RAM_BYTES 0x10000

extern const uint16_t *cps1_prog;
#if PROG_CACHE_KB
/* the program by 4 KB page: each entry points at the page in flash or at a copy in RAM */
extern const uint16_t *cps1_prog_page[CPS1_PROG_BYTES >> 12];
#define CPS1_PROG_WORD(a) (cps1_prog_page[(a) >> 12][((a) & 0xffe) >> 1])
#else
#define CPS1_PROG_WORD(a) (cps1_prog[(a) >> 1])
#endif
extern uint16_t *cps1_ram;
extern uint16_t *cps1_gfxram;

unsigned cps1_io_r16(unsigned a);
void cps1_io_w16(unsigned a, unsigned v, unsigned mask);

static inline unsigned cps1_r16(unsigned a)
{
    a &= 0xfffffe;
    if (a >= 0xff0000) return cps1_ram[(a & 0xffff) >> 1];
    if (a < CPS1_PROG_BYTES) return CPS1_PROG_WORD(a);
    if (a - 0x900000 < CPS1_GFXRAM_BYTES) return cps1_gfxram[(a - 0x900000) >> 1];
    return cps1_io_r16(a);
}
static inline unsigned cps1_r8(unsigned a)
{
    unsigned w = cps1_r16(a);
    return (a & 1) ? (w & 0xff) : (w >> 8);
}
static inline unsigned cps1_r32(unsigned a) { unsigned h = cps1_r16(a); return (h << 16) | cps1_r16(a + 2); }

/* instructions come from the ROM; anything else takes the long way round */
static inline unsigned cps1_op16(unsigned a)
{
    a &= 0xfffffe;
    if (a < CPS1_PROG_BYTES) return CPS1_PROG_WORD(a);
    return cps1_r16(a);
}
static inline unsigned cps1_op32(unsigned a) { unsigned h = cps1_op16(a); return (h << 16) | cps1_op16(a + 2); }

/*
 * For the idle detector (cps1_idle.c): the first few words that a pass of the scheduler
 * changes, and what each held before. A write that stores what is already there is not a
 * change. More changes than there is room for, or any write to the hardware, and the
 * count is left above the limit, which says "this pass did something".
 */
#define CPS1_CHANGES 12
typedef struct { uint16_t *at; uint16_t was; } cps1_change_t;
extern cps1_change_t cps1_changes[CPS1_CHANGES];
extern unsigned cps1_nchanges;

static inline void cps1_changing(uint16_t *p)
{
    unsigned n = cps1_nchanges;
    if (n >= CPS1_CHANGES) { cps1_nchanges = CPS1_CHANGES + 1; return; }
    for (unsigned i = 0; i < n; i++) if (cps1_changes[i].at == p) return;
    cps1_changes[n].at = p; cps1_changes[n].was = *p;
    cps1_nchanges = n + 1;
}

/* For the video: how many times each 16 KB of graphics RAM has been changed. A tile map is
 * 16 KB and begins on a 16 KB boundary, so one count says whether a map has changed. */
extern uint32_t cps1_gfx_generation[CPS1_GFXRAM_BYTES / 0x4000];

static inline void cps1_w16(unsigned a, unsigned v)
{
    uint16_t *p;
    a &= 0xfffffe;
    if (a >= 0xff0000) {
        p = &cps1_ram[(a & 0xffff) >> 1];
        if (*p != (uint16_t)v) { cps1_changing(p); *p = (uint16_t)v; }
    } else if (a - 0x900000 < CPS1_GFXRAM_BYTES) {
        p = &cps1_gfxram[(a - 0x900000) >> 1];
        if (*p != (uint16_t)v) { cps1_changing(p); *p = (uint16_t)v; cps1_gfx_generation[(a - 0x900000) >> 14]++; }
    } else {
        cps1_io_w16(a, v & 0xffff, 0xffff);
    }
}
static inline void cps1_w8(unsigned a, unsigned v)
{
    unsigned sh = (a & 1) ? 0 : 8;
    uint16_t *p;
    v &= 0xff;
    a &= 0xffffff;
    if (a >= 0xff0000) {
        p = &cps1_ram[(a & 0xffff) >> 1];
        uint16_t w = (uint16_t)((*p & ~(0xffu << sh)) | (v << sh));
        if (*p != w) { cps1_changing(p); *p = w; }
    } else if (a - 0x900000 < CPS1_GFXRAM_BYTES) {
        p = &cps1_gfxram[(a - 0x900000) >> 1];
        uint16_t w = (uint16_t)((*p & ~(0xffu << sh)) | (v << sh));
        if (*p != w) { cps1_changing(p); *p = w; cps1_gfx_generation[(a - 0x900000) >> 14]++; }
    } else {
        cps1_io_w16(a & ~1u, v << sh, 0xffu << sh);
    }
}
static inline void cps1_w32(unsigned a, unsigned v) { cps1_w16(a, v >> 16); cps1_w16(a + 2, v & 0xffff); }

/* Musashi's names for the same things */
#define m68k_read_memory_8(A)       cps1_r8(A)
#define m68k_read_memory_16(A)      cps1_r16(A)
#define m68k_read_memory_32(A)      cps1_r32(A)
#define m68k_read_immediate_16(A)   cps1_op16(A)
#define m68k_read_immediate_32(A)   cps1_op32(A)
#define m68k_read_pcrelative_8(A)   cps1_r8(A)
#define m68k_read_pcrelative_16(A)  cps1_r16(A)
#define m68k_read_pcrelative_32(A)  cps1_r32(A)
#define m68k_write_memory_8(A, V)   cps1_w8(A, V)
#define m68k_write_memory_16(A, V)  cps1_w16(A, V)
#define m68k_write_memory_32(A, V)  cps1_w32(A, V)

#endif
