/*
 * scores.h - where Donkey Kong keeps its high scores.
 * Addresses from MAME's hiscore.dat.
 */
#pragma once
#include "hiscore.h"
#include "dkong.h"

static const hiscore_range_t score_ranges[] = {
    { 0x6100, 0xaa, 0x94, 0x76 },       /* the table, as it is drawn */
    { 0x60b8, 0x03, 0x50, 0x00 },       /* the best score */
    { 0x7641, 0x01, 0x00, 0x00 },       /* and its digits on the screen */
    { 0x7621, 0x01, 0x00, 0x00 },
    { 0x7601, 0x01, 0x07, 0x07 },
    { 0x75e1, 0x01, 0x06, 0x06 },
    { 0x75c1, 0x01, 0x05, 0x05 },
    { 0x75a1, 0x01, 0x00, 0x00 },
};

static const hiscore_t game_scores = {
    .rom = "dkong", .ranges = score_ranges, .nranges = 8, .whole = false, .mem = dk_mem,
    /* the best score, for the menu: three bytes of BCD, low byte first */
    .format = HISCORE_BCD, .top_addr = 0x60b8, .top_len = 3, .top_msb_first = false,
};
