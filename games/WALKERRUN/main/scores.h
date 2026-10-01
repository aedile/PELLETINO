/*
 * scores.h - where The Empire Strikes Back keeps its high scores.
 * The same X2212 non-volatile RAM as Star Wars, at the same address, so it is put back
 * before the game starts. How this game lays its table out in there has not been read, so
 * the scores are kept and the menu shows no figure for them.
 */
#pragma once
#include "hiscore.h"
#include "starwars.h"

static const hiscore_range_t score_ranges[] = {
    { 0x4500, 0x100, 0x00, 0x00 },       /* all of it */
};

static const hiscore_t game_scores = {
#ifdef SW_GAME_ESB
    .rom = "esb",
#else
    .rom = "starwars",
#endif
    .ranges = score_ranges, .nranges = 1, .whole = true, .mem = sw_mem,
};
