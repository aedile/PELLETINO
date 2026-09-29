/*
 * scores.h - where Galaxian keeps its high scores.
 * Addresses from MAME's hiscore.dat.
 */
#pragma once
#include "hiscore.h"
#include "galaxian.h"

static const hiscore_range_t score_ranges[] = {
    { 0x40a8, 0x03, 0x00, 0x00 },       /* the best score */
};

static const hiscore_t game_scores = {
    .rom = "galaxian", .ranges = score_ranges, .nranges = 1, .whole = false, .mem = gx_mem,
    /* the best score, for the menu: three bytes of BCD, low byte first */
    .format = HISCORE_BCD, .top_addr = 0x40a8, .top_len = 3, .top_msb_first = false,
};
