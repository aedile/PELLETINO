/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro */
/*
 * knobs_m68k.h - the two knobs the 68000 itself reads. They are documented in knobs.h.
 *
 * Kept apart so that the 68000 (a megabyte of generated source) is only recompiled when one
 * of these changes, and not whenever any knob does: knobs.cmake writes them to a header
 * of their own.
 */
#ifndef KNOBS_M68K_H
#define KNOBS_M68K_H

#if defined(__has_include)
#if __has_include("knobs_m68k_build.h")
#include "knobs_m68k_build.h"
#endif
#endif

#ifndef PROG_CACHE_KB
#define PROG_CACHE_KB 0
#endif
#ifndef OPCODE_TABLE_RAM
#define OPCODE_TABLE_RAM 0
#endif

#endif
