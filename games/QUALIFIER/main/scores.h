/*
 * scores.h - where Pole Position keeps its high scores.
 * The board kept this memory powered, so it is put back before the game starts.
 */
#pragma once
#include "hiscore.h"
#include "polepos.h"

static const hiscore_range_t score_ranges[] = {
    { 0x3000, 0x800, 0x00, 0x00 },       /* all of it: the board kept this RAM on a battery */
};

static const hiscore_t game_scores = {
    .rom = "polepos", .ranges = score_ranges, .nranges = 1, .whole = true, .mem = pp_mem,
    /* the best score, for the menu: the first entry, a sixteen-bit count of tens of points */
    .format = HISCORE_BINARY, .top_addr = 0x3000, .top_len = 2, .top_msb_first = false,
    .top_times = 10,
};
