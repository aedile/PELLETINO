/*
 * scores.h - where Root Beer Tapper keeps its high scores.
 * The board kept all of its RAM on a battery. The table is sixty bytes of it: ten entries
 * of three initials and three bytes of BCD, best first. Found by playing a game and looking
 * at what changed. The game clears it while it starts up, so it is put back ten seconds in.
 */
#pragma once
#include "hiscore.h"
#include "tapper.h"

static const hiscore_range_t score_ranges[] = {
    { 0xe014, 0x3c, 0x00, 0x00 },       /* the table */
};

static const hiscore_t game_scores = {
    .rom = "rbtapper", .ranges = score_ranges, .nranges = 1, .whole = false, .wait_frames = 600, .mem = tap_mem,
    /* the best score, for the menu: the first entry, after its initials */
    .format = HISCORE_BCD, .top_addr = 0xe017, .top_len = 3, .top_msb_first = true,
};
