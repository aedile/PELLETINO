/*
 * scores.h - where Star Wars keeps its high scores.
 * The board kept them in an X2212 non-volatile RAM, so it is put back before the game starts.
 */
#pragma once
#include "hiscore.h"
#include "starwars.h"

static const hiscore_range_t score_ranges[] = {
    { 0x4500, 0x100, 0x00, 0x00 },       /* all of it */
};

static const hiscore_t game_scores = {
    .rom = "starwars", .ranges = score_ranges, .nranges = 1, .whole = true, .mem = sw_mem,
    /* the best score, for the menu: seven digits, a nibble each, first digit first. The chip
     * holds the top three, eight bytes apart from 0x4509, best first - read off a medal's log
     * after a game, where the factory's 1,285,353, 1,110,936 and 1,024,650 were plain to see. */
    .format = HISCORE_DIGITS, .top_addr = 0x4509, .top_len = 7, .top_msb_first = true,
    .digit_zero = 0x00, .digit_blank = 0xff,
};
