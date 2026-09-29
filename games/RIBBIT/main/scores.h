/*
 * scores.h - where Frogger keeps its high scores.
 * Addresses from MAME's hiscore.dat.
 */
#pragma once
#include "hiscore.h"
#include "frogger.h"

static const hiscore_range_t score_ranges[] = {
    { 0x83f1, 0x0a, 0x63, 0x01 },       /* the table */
    { 0x83ef, 0x02, 0x63, 0x04 },       /* the best score */
};

static const hiscore_t game_scores = {
    .rom = "frogger", .ranges = score_ranges, .nranges = 2, .whole = false, .mem = fr_mem,
    /* the best score, for the menu: tens of points, low byte first */
    .format = HISCORE_BCD, .top_addr = 0x83ef, .top_len = 2, .top_msb_first = false,
    .top_times = 10,
};
