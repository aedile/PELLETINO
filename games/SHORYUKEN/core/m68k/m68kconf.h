/* SPDX-License-Identifier: MIT */
/*
 * m68kconf.h - Musashi's configuration, as SHORYUKEN sets it.
 *
 * This file is the one Musashi means its user to write. The options and what they do are
 * Musashi's (Copyright 1998-2001 Karl Stenerud, MIT licence, see readme.txt); the choices are
 * this project's: a plain 68000, nothing optional, and memory reached through the inline
 * functions in cps1_mem.h rather than through calls.
 */
#ifndef M68KCONF__HEADER
#define M68KCONF__HEADER

#define M68K_OPT_OFF             0
#define M68K_OPT_ON              1
#define M68K_OPT_SPECIFY_HANDLER 2

#define M68K_COMPILE_FOR_MAME       M68K_OPT_OFF

#define M68K_EMULATE_010            M68K_OPT_OFF
#define M68K_EMULATE_EC020          M68K_OPT_OFF
#define M68K_EMULATE_020            M68K_OPT_OFF
#define M68K_EMULATE_030            M68K_OPT_OFF
#define M68K_EMULATE_040            M68K_OPT_OFF

/* immediate and PC-relative reads go straight to the program ROM (see cps1_mem.h) */
#define M68K_SEPARATE_READS         M68K_OPT_ON
#define M68K_SIMULATE_PD_WRITES     M68K_OPT_OFF

/* the board acknowledges with VPA and drops both interrupt lines as it does */
#define M68K_EMULATE_INT_ACK        M68K_OPT_SPECIFY_HANDLER
#define M68K_INT_ACK_CALLBACK(A)    cps1_int_ack(A)

#define M68K_EMULATE_BKPT_ACK       M68K_OPT_OFF
#define M68K_EMULATE_TRACE          M68K_OPT_OFF
#define M68K_EMULATE_RESET          M68K_OPT_OFF
#define M68K_CMPILD_HAS_CALLBACK    M68K_OPT_OFF
#define M68K_RTE_HAS_CALLBACK       M68K_OPT_OFF
#define M68K_TAS_HAS_CALLBACK       M68K_OPT_OFF
#define M68K_ILLG_HAS_CALLBACK      M68K_OPT_OFF
#define M68K_TRAP_HAS_CALLBACK      M68K_OPT_OFF
#define M68K_EMULATE_FC             M68K_OPT_OFF
/* where jumps land is counted, for choosing which pages of the program to keep in RAM */
#include "knobs_m68k.h"
#if PROG_CACHE_KB
#define M68K_MONITOR_PC             M68K_OPT_SPECIFY_HANDLER
#define M68K_SET_PC_CALLBACK(A)     (cps1_page_hits[((A) >> 12) & 0xff]++)
extern unsigned short cps1_page_hits[256];
#else
#define M68K_MONITOR_PC             M68K_OPT_OFF
#endif

/* the opcode table: in RAM unless told otherwise */
#if !OPCODE_TABLE_RAM
#define M68K_GROUP_ATTR const
#define M68K_ROW_ATTR const
#endif
/* a host build with -DCPS1_PROFILE counts and traces instructions through this */
#ifdef CPS1_PROFILE
#define M68K_INSTRUCTION_HOOK       M68K_OPT_SPECIFY_HANDLER
#define M68K_INSTRUCTION_CALLBACK(pc) cps1_profile_hook(pc)
void cps1_profile_hook(unsigned int pc);
#else
#define M68K_INSTRUCTION_HOOK       M68K_OPT_OFF
#endif
#define M68K_EMULATE_PREFETCH       M68K_OPT_OFF
#define M68K_EMULATE_ADDRESS_ERROR  M68K_OPT_OFF
#define M68K_LOG_ENABLE             M68K_OPT_OFF
#define M68K_LOG_1010_1111          M68K_OPT_OFF
#define M68K_LOG_TRAP               M68K_OPT_OFF
#define M68K_EMULATE_PMMU           M68K_OPT_OFF

/* a 32-bit RISC-V: 64-bit arithmetic is a library call */
#define M68K_USE_64_BIT             M68K_OPT_OFF

int cps1_int_ack(int level);

#endif /* M68KCONF__HEADER */
