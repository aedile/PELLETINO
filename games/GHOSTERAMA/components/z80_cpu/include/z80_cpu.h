/*
 * z80_cpu.h - Z80 CPU Emulator Wrapper
 *
 * Wraps Marat Fayzullin's Z80 emulator for ESP-IDF use
 */

#ifndef Z80_CPU_H
#define Z80_CPU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the Z80 CPU emulator
 */
void z80_init(void);

/**
 * Reset the Z80 CPU
 */
void z80_reset(void);

/**
 * Execute a specified number of CPU cycles
 * @param cycles Number of T-states to execute
 * @return Actual cycles executed
 */
int z80_execute(int cycles);

/**
 * Trigger an interrupt
 * @param vector Interrupt vector (for mode 2)
 */
void z80_interrupt(uint8_t vector);

/**
 * Trigger a non-maskable interrupt
 */
void z80_nmi(void);

#ifdef __cplusplus
}
#endif

#endif // Z80_CPU_H
