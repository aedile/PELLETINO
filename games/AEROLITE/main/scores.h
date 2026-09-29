/*
 * scores.h - where Asteroids keeps its high scores.
 * Addresses from MAME's hiscore.dat.
 */
#pragma once
#include "hiscore.h"
#include "asteroids.h"

static const hiscore_range_t score_ranges[] = {
    { 0x001d, 0x35, 0x00, 0x00 },       /* the table and its initials */
    { 0x4030, 0x01, 0x01, 0x01 },
};

static const hiscore_t game_scores = {
    .rom = "asteroid", .ranges = score_ranges, .nranges = 2, .whole = false, .mem = ast_mem,
    /* the best score, for the menu: tens of points, low byte first */
    .format = HISCORE_BCD, .top_addr = 0x001d, .top_len = 2, .top_msb_first = false,
    .top_times = 10,
};
