/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro */
/*
 * cps1_idle.c - not executing the part of the program that does nothing.
 *
 * The game is a set of sixteen tasks and a scheduler. The scheduler goes round the task
 * slots running whatever is ready, and when it has been all the way round it starts again.
 * Most of a frame there is nothing ready but one task that looks at an empty queue and
 * yields, so the 68000 goes round and round: about fifty times a frame, which is three
 * instructions in four.
 *
 * What is done about it rests on one fact. If the scheduler reaches the end of a pass with
 * the registers exactly as they were at the end of the pass before, and nothing in memory
 * was changed in between, then the machine is in precisely the state it was in one pass
 * ago, at the same instruction. It is deterministic, so it will do the same pass again,
 * and again, until something from outside changes something: an interrupt. Nothing else
 * can. So the rest of the time slice can be given up without executing it.
 *
 * "Nothing in memory was changed" means that memory at the end of the pass is what it was
 * at the start, not that nothing was written. Every pass pushes and pops return addresses,
 * and marks the polling task as running and then as ready again. So the memory functions
 * (cps1_mem.h) keep a short list of the words a pass has changed and what they held, and
 * at the end of the pass each is looked at: if all are back as they were, nothing changed.
 * A pass that changes more than the list holds, or writes to the hardware, did something.
 *
 * The rest of the slice is not simply thrown away, though, because where in the loop the
 * interrupt lands decides how long the program takes to get to work after it, and work
 * that moves by a few hundred cycles occasionally falls on the other side of the end of a
 * frame. So the passes are timed. Once a pass is known to repeat, as many whole passes as
 * fit in what is left of the slice are taken off the clock at once, and the last part of
 * a pass is executed as usual. The interrupt then finds the program at the instruction,
 * and the cycle, where it would have found it with nothing skipped at all.
 *
 * Nothing is tested on every instruction. The end of a pass is a BRA, and the handler for
 * that kind of instruction is wrapped to look at the program counter.
 */
#include "cps1_internal.h"
#include "knobs.h"

#if IDLE_SKIP && CPU_CORE == CPU_CORE_MUSASHI

#include "m68kcpu.h"

static unsigned pass_end_pc = 0xffffffff;      /* as the PC reads inside the handler: past the opcode */
static void (*real_bra)(void);
static unsigned last_regs[18];
static int last_left;                          /* cycles left in the slice at the end of the last pass */
static unsigned last_slice;

static uint32_t found_at = 0xffffffff;
uint32_t cps1_idle_loop(void) { return found_at; }

static void wrapped_bra(void)
{
    if (REG_PC == pass_end_pc) {
        unsigned same = cps1_nchanges <= CPS1_CHANGES;
        if (same) for (unsigned i = 0; i < cps1_nchanges; i++) same &= *cps1_changes[i].at == cps1_changes[i].was;
        for (int i = 0; i < 16; i++) { same &= last_regs[i] == REG_DA[i]; last_regs[i] = REG_DA[i]; }
        unsigned sr = m68ki_get_sr(), usp = REG_USP;
        same &= last_regs[16] == sr && last_regs[17] == usp;
        last_regs[16] = sr; last_regs[17] = usp;
        cps1_nchanges = 0;

        int left = GET_CYCLES(), pass = last_left - left;
        if (same && last_slice == cps1_slice && pass > 0 && left > pass) {
            /* this pass was the last one over again, and the next will be: skip the whole ones */
            left -= ((left - 1) / pass) * pass;
            SET_CYCLES(left);
        }
        last_left = left;
        last_slice = cps1_slice;
    }
    real_bra();
}

/* The loop, by its shape: mask interrupts, test the flag, look at a slot's state, dispatch
 * if it is 4 or more, unmask, next slot, sixteen times, and round again. */
static unsigned find_loop(const uint16_t *prog)
{
    static const uint16_t shape[] = { 0x46fc, 0x2600, 0x4a2d, 0, 0x6600, 0x1228, 0x0000, 0x0c01, 0x0004,
                                      0x6400, 0x46fc, 0x2000, 0x41e8, 0x0020, 0x51c8, 0xff00, 0x6000 };
    static const uint16_t care[]  = { 0xffff, 0xffff, 0xffff, 0, 0xff00, 0xffff, 0xffff, 0xffff, 0xffff,
                                      0xff00, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xff00, 0xff00 };
    const unsigned n = sizeof(shape) / sizeof(shape[0]);
    for (unsigned a = 0; a + n < 0x8000; a++) {
        unsigned i = 0;
        while (i < n && (prog[a + i] & care[i]) == shape[i]) i++;
        if (i == n) return (a + n - 1) * 2;          /* the BRA that closes it */
    }
    return 0xffffffff;
}

void cps1_idle_init(const uint16_t *prog)
{
    found_at = find_loop(prog);
    if (found_at == 0xffffffff) return;
    unsigned bra = m68ki_op_index[prog[found_at >> 1]];
    if (m68ki_handlers[bra] != wrapped_bra) { real_bra = m68ki_handlers[bra]; m68ki_handlers[bra] = wrapped_bra; }
    pass_end_pc = found_at + 2;
}

#elif IDLE_SKIP

/* the other 68000 does this for itself (m68kown.c) */

#else

uint32_t cps1_idle_loop(void) { return 0xffffffff; }
void cps1_idle_init(const uint16_t *prog) { (void)prog; }

#endif
