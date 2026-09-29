/*
 * scores.h - where Centipede keeps its high scores.
 * Addresses from MAME's hiscore.dat.
 */
#pragma once
#include "hiscore.h"
#include "centiped.h"

static const hiscore_range_t score_ranges[] = {
    { 0x000b, 0x0f, 0x10, 0x01 },       /* the scores */
    { 0x0023, 0x0f, 0x04, 0x12 },       /* and the initials */
};

static const hiscore_t game_scores = {
    .rom = "centiped", .ranges = score_ranges, .nranges = 2, .whole = false, .mem = ce_mem,
    /* the best score, for the menu: the first entry in the table, low byte first */
    .format = HISCORE_BCD, .top_addr = 0x000b, .top_len = 3, .top_msb_first = false,
};
