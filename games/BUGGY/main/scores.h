/*
 * scores.h - where Moon Patrol keeps its high scores.
 * Addresses from MAME's hiscore.dat.
 */
#pragma once
#include "hiscore.h"
#include "mpatrol.h"

static const hiscore_range_t score_ranges[] = {
    { 0xe008, 0x2c, 0x00, 0x00 },       /* the table */
};

static const hiscore_t game_scores = {
    .rom = "mpatrolw", .ranges = score_ranges, .nranges = 1, .whole = false, .mem = mp_mem,
};
