/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * z8002.h - Zilog Z8002 CPU core interface (generated core in z8002.c)
 */
#ifndef Z8002_H
#define Z8002_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    union { uint8_t B[32]; uint16_t W[16]; uint32_t L[8]; uint64_t Q[4]; } regs;
    uint32_t op[4];
    uint32_t ppc, pc;
    uint16_t psapseg, psapoff, fcw, refresh, nspseg, nspoff;
    uint8_t irq_req, op_valid;
    uint16_t irq_vec;
    int nmi_state, irq_state[3], busreq_state, busack_state, mi;
    int halt;
    int icount;
} z8k_t;

/* bus, supplied by the machine (addresses are 16-bit, words big-endian) */
uint8_t z8k_rb(uint32_t addr);
uint16_t z8k_rw(uint32_t addr);
void z8k_wb(uint32_t addr, uint8_t data);
void z8k_ww(uint32_t addr, uint16_t data);
uint8_t z8k_in(uint16_t port);
void z8k_out(uint16_t port, uint8_t data);

extern uint32_t z8k_invalid_count;    /* invalid opcodes executed (diagnostic) */
/* read page table (256 entries of native-endian word pointers, every page mapped) for the CPU being run */
extern const uint16_t *const *z8k_rpage;
/* write page table for RAM (NULL = use z8k_ww/z8k_wb); may be shared by all CPUs */
extern uint16_t *const *z8k_wpage;
/* While z8k_run() executes, the CPU state lives in an internal copy; bus callbacks that
 * change the running CPU's interrupt lines must use this instead of the caller's struct. */
z8k_t *z8k_live(void);
void z8k_init_tables(void);           /* once */
void z8k_reset(z8k_t *cpu);
void z8k_set_nvi(z8k_t *cpu, int state);
int z8k_run(z8k_t *cpu, int cycles);  /* returns cycles executed (may overshoot) */

#ifdef __cplusplus
}
#endif
#endif
