/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * z80_fastop.h - force-included when compiling Z80.c (see the CMake and Makefile): opcode and
 * operand fetches from the program ROM and the work RAM skip the bus function. The Z80 core's
 * own FAST_RDOP hook is what makes this possible without touching its files.
 */
#ifndef Z80_FASTOP_H
#define Z80_FASTOP_H
#define FAST_RDOP
extern const unsigned char *mp_z80_rom;          /* the 16 KB program */
extern unsigned char mp_z80_ram[0x800];          /* 0xE000-0xE7FF */
unsigned char RdZ80(unsigned short A);
static inline unsigned char OpZ80(unsigned short A)
{
    if (A < 0x4000) return mp_z80_rom[A];
    if ((A & 0xf800) == 0xe000) return mp_z80_ram[A & 0x7ff];
    return RdZ80(A);
}
#endif
