/*
 * scores.h - where Rally-X keeps its high scores.
 * Addresses from MAME's hiscore.dat.
 */
#pragma once
#include "hiscore.h"
#include "rallyx.h"

static const hiscore_range_t score_ranges[] = {
    { 0x8060, 0x08, 0x00, 0x02 },       /* the best score, as drawn */
};

static const hiscore_t game_scores = {
    .rom = "rallyx", .ranges = score_ranges, .nranges = 1, .whole = false, .mem = rx_mem,
};
