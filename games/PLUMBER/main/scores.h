/*
 * scores.h - where Mario Bros. keeps its high scores.
 * Addresses from MAME's hiscore.dat.
 */
#pragma once
#include "hiscore.h"
#include "mario.h"

static const hiscore_range_t score_ranges[] = {
    { 0x6b00, 0xaa, 0x97, 0x74 },       /* the table, as it is drawn */
    { 0x6c00, 0x3c, 0x00, 0x00 },
    { 0x6823, 0x03, 0x01, 0x00 },       /* the best score */
};

static const hiscore_t game_scores = {
    .rom = "mario", .ranges = score_ranges, .nranges = 3, .whole = false, .mem = mb_mem,
    /* the best score, for the menu: three bytes of BCD, high byte first */
    .format = HISCORE_BCD, .top_addr = 0x6823, .top_len = 3, .top_msb_first = true,
};
