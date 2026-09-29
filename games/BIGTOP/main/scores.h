/*
 * scores.h - where Mr. Do! keeps its high scores.
 * Addresses from MAME's hiscore.dat.
 */
#pragma once
#include "hiscore.h"
#include "mrdo.h"

static const hiscore_range_t score_ranges[] = {
    { 0xe017, 0x64, 0x01, 0x00 },       /* the table */
};

static const hiscore_t game_scores = {
    .rom = "mrdo", .ranges = score_ranges, .nranges = 1, .whole = false, .mem = md_mem,
    /* the best score, for the menu: the first entry in the table */
    .format = HISCORE_BCD, .top_addr = 0xe017, .top_len = 3, .top_msb_first = true,
};
