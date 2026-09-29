/*
 * scores.h - where Missile Command keeps its high scores.
 * Addresses from MAME's hiscore.dat.
 */
#pragma once
#include "hiscore.h"
#include "missile.h"

static const hiscore_range_t score_ranges[] = {
    { 0x002c, 0x30, 0x47, 0x00 },       /* the table */
};

static const hiscore_t game_scores = {
    .rom = "missile", .ranges = score_ranges, .nranges = 1, .whole = false, .mem = mc_mem,
    /* the best score, for the menu: the last of the eight entries is the best */
    .format = HISCORE_BCD, .top_addr = 0x0059, .top_len = 3, .top_msb_first = false,
};
