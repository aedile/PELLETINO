/*
 * scores.h - where Space Invaders keeps its high scores.
 * Addresses from MAME's hiscore.dat.
 */
#pragma once
#include "hiscore.h"
#include "invaders.h"

static const hiscore_range_t score_ranges[] = {
    { 0x20f4, 0x02, 0x00, 0x00 },       /* the best score */
};

static const hiscore_t game_scores = {
    .rom = "invaders", .ranges = score_ranges, .nranges = 1, .whole = false, .mem = si_mem,
    /* the best score, for the menu: two bytes of BCD, low byte first */
    .format = HISCORE_BCD, .top_addr = 0x20f4, .top_len = 2, .top_msb_first = false,
};
