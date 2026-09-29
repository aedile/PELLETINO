/*
 * scores.h - where Arkanoid keeps its high scores.
 * Addresses from MAME's hiscore.dat.
 */
#pragma once
#include "hiscore.h"
#include "arkanoid.h"

static const hiscore_range_t score_ranges[] = {
    { 0xef79, 0x23, 0x00, 0x52 },       /* the table */
    { 0xc4df, 0x03, 0x00, 0x00 },       /* the best score */
};

static const hiscore_t game_scores = {
    .rom = "arkanoidu", .ranges = score_ranges, .nranges = 2, .whole = false, .mem = ak_mem,
    /* the best score, for the menu: tens of points, high byte first */
    .format = HISCORE_BCD, .top_addr = 0xc4df, .top_len = 3, .top_msb_first = true,
    .top_times = 10,
};
