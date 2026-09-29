/* SPDX-License-Identifier: BSD-3-Clause
 * Ported to plain C for PELLETINO by Jesse Castro, 2026, from MAME (src/mame/machine/slapstic.cpp, pre-2022),
 * copyright Aaron Giles and the MAME team. Redistribution and use are permitted under
 * the BSD-3-Clause license; the notice, conditions and disclaimer in this game's
 * THIRD_PARTY_NOTICES.md apply to this file. */
/*
 * slapstic.c - Atari slapstic 137412-101 (see slapstic.h). State machine and table from the
 * pre-2022 MAME slapstic.cpp (BSD-3-Clause, copyright Aaron Giles and the MAME team).
 */
#include "slapstic.h"

/* 137412-101 */
static const uint16_t bank_sel[4] = { 0x0080, 0x0090, 0x00a0, 0x00b0 };
#define ALT2_M 0x1fff
#define ALT2_V 0x1dff            /* postbyte fetch of "LDA ,X" at 1DFE */
#define ALT3_M 0x1ffc
#define ALT3_V 0x1b5c            /* the LDA's read: bank = offset & 3 */
#define ALT4_M 0x1fcf
#define ALT4_V 0x0080            /* commit on any bank-select address */
#define BIT1_M 0x1ff0
#define BIT1_V 0x1540
#define BIT2_M 0x1ff3
#define BIT2C0 0x1540
#define BIT2S0 0x1541
#define BIT2C1 0x1542
#define BIT2S1 0x1543
#define BIT3_M 0x1ff8
#define BIT3_V 0x1550
#define BANK_START 3

uint8_t slapstic_state;
uint8_t slapstic_current_bank;
static uint8_t alt_bank, bit_bank, bit_xor;

#define MATCH(o, m, v) (((o) & (m)) == (v))
static inline int is_bank_sel(uint16_t o) { return o == bank_sel[0] || o == bank_sel[1] || o == bank_sel[2] || o == bank_sel[3]; }

void slapstic_reset(void)
{
    slapstic_state = SLAP_DISABLED;
    slapstic_current_bank = BANK_START;
    alt_bank = bit_bank = bit_xor = 0;
}

int slapstic_tweak(uint16_t o)
{
    if (o == 0) { slapstic_state = SLAP_ENABLED; return slapstic_current_bank; }   /* reset is universal */
    switch (slapstic_state) {
    case SLAP_DISABLED:
        break;
    case SLAP_ENABLED:
        if (MATCH(o, BIT1_M, BIT1_V)) slapstic_state = SLAP_BIT1;
        else if (MATCH(o, ALT2_M, ALT2_V)) slapstic_state = SLAP_ALT2;      /* the 1st alt access is never in the window */
        else for (int i = 0; i < 4; i++)
            if (o == bank_sel[i]) { slapstic_state = SLAP_DISABLED; slapstic_current_bank = (uint8_t)i; break; }
        break;
    case SLAP_ALT2:
        if (MATCH(o, ALT3_M, ALT3_V)) { slapstic_state = SLAP_ALT3; alt_bank = o & 3; }
        else slapstic_state = SLAP_ENABLED;
        break;
    case SLAP_ALT3:
        if (MATCH(o, ALT4_M, ALT4_V)) { slapstic_state = SLAP_DISABLED; slapstic_current_bank = alt_bank; }
        break;
    case SLAP_BIT1:
        if (is_bank_sel(o)) { slapstic_state = SLAP_BIT2; bit_bank = slapstic_current_bank; bit_xor = 0; }
        break;
    case SLAP_BIT2:
        if (MATCH(o ^ bit_xor, BIT2_M, BIT2C0)) { bit_bank &= ~1; bit_xor ^= 3; }
        else if (MATCH(o ^ bit_xor, BIT2_M, BIT2S0)) { bit_bank |= 1; bit_xor ^= 3; }
        else if (MATCH(o ^ bit_xor, BIT2_M, BIT2C1)) { bit_bank &= ~2; bit_xor ^= 3; }
        else if (MATCH(o ^ bit_xor, BIT2_M, BIT2S1)) { bit_bank |= 2; bit_xor ^= 3; }
        else if (MATCH(o, BIT3_M, BIT3_V)) slapstic_state = SLAP_BIT3;
        break;
    case SLAP_BIT3:
        if (is_bank_sel(o)) { slapstic_state = SLAP_DISABLED; slapstic_current_bank = bit_bank; }
        break;
    default:
        slapstic_state = SLAP_DISABLED;
        break;
    }
    return slapstic_current_bank;
}
