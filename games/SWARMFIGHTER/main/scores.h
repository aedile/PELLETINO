/*
 * scores.h - where Galaga keeps its high scores.
 * Addresses from MAME's hiscore.dat.
 */
#pragma once
#include "hiscore.h"
#include "galaga.h"

static const hiscore_range_t score_ranges[] = {
    { 0x8a20, 0x2d, 0x00, 0x18 },       /* the table */
    { 0x83ed, 0x06, 0x00, 0x24 },       /* the best score, as drawn */
};

static const hiscore_t game_scores = {
    .rom = "galaga", .ranges = score_ranges, .nranges = 2, .whole = false, .mem = ga_mem,
    /* the best score, for the menu: six characters on the screen, units first */
    .format = HISCORE_DIGITS, .top_addr = 0x83ed, .top_len = 6, .top_msb_first = false,
    .digit_zero = 0x00, .digit_blank = 0x24,
};
