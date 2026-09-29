/*
 * scores.h - where Dig Dug keeps its high scores.
 * Addresses from MAME's hiscore.dat.
 */
#pragma once
#include "hiscore.h"
#include "digdug.h"

static const hiscore_range_t score_ranges[] = {
    { 0x89a0, 0x25, 0x01, 0x01 },       /* the table */
};

static const hiscore_t game_scores = {
    .rom = "digdug", .ranges = score_ranges, .nranges = 1, .whole = false, .mem = dd_mem,
    /* the best score, for the menu: the first entry in the table */
    .format = HISCORE_BCD, .top_addr = 0x89a0, .top_len = 3, .top_msb_first = true,
};
