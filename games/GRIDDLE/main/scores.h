/*
 * scores.h - where Burger Time keeps its high scores.
 * Addresses from MAME's hiscore.dat.
 */
#pragma once
#include "hiscore.h"
#include "btime.h"

static const hiscore_range_t score_ranges[] = {
    { 0x0033, 0x27, 0x00, 0xff },       /* the table */
};

static const hiscore_t game_scores = {
    .rom = "btime", .ranges = score_ranges, .nranges = 1, .whole = false, .mem = bt_mem,
    /* the best score, for the menu: the first entry in the table, low byte first */
    .format = HISCORE_BCD, .top_addr = 0x0033, .top_len = 3, .top_msb_first = false,
};
