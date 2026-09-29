/*
 * scores.h - where Time Pilot keeps its high scores.
 * Addresses from MAME's hiscore.dat.
 */
#pragma once
#include "hiscore.h"
#include "timeplt.h"

static const hiscore_range_t score_ranges[] = {
    { 0xab08, 0x28, 0x00, 0xf1 },       /* the table */
    { 0xa98b, 0x03, 0x00, 0x01 },       /* the best score */
};

static const hiscore_t game_scores = {
    .rom = "timeplt", .ranges = score_ranges, .nranges = 2, .whole = false, .mem = tp_mem,
    /* the best score, for the menu: three bytes of BCD, low byte first */
    .format = HISCORE_BCD, .top_addr = 0xa98b, .top_len = 3, .top_msb_first = false,
};
