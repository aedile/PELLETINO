/*
 * scores.h - where Gyruss keeps its high scores.
 * Addresses from MAME's hiscore.dat.
 */
#pragma once
#include "hiscore.h"
#include "gyruss.h"

static const hiscore_range_t score_ranges[] = {
    { 0x9488, 0x28, 0x00, 0x83 },       /* the table */
    { 0x940b, 0x03, 0x00, 0x01 },       /* the best score */
};

static const hiscore_t game_scores = {
    .rom = "gyruss", .ranges = score_ranges, .nranges = 2, .whole = false, .mem = gy_mem,
    /* the best score, for the menu: three bytes of BCD, low byte first */
    .format = HISCORE_BCD, .top_addr = 0x940b, .top_len = 3, .top_msb_first = false,
};
