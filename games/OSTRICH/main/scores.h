/*
 * scores.h - where Joust keeps its high scores.
 * The board kept them in a CMOS RAM on a battery, four bits to a byte, along with its
 * settings and book-keeping; all of it is put back before the game starts. It holds two
 * tables: the champions, which the cabinet never forgot, and the day's best, which it did.
 */
#pragma once
#include "hiscore.h"
#include "joust.h"

static const hiscore_range_t score_ranges[] = {
    { 0xcc00, 0x400, 0x00, 0x00 },      /* all of it */
};

/* a score is seven digits, a nibble each, first digit first */
static uint32_t joust_score(uint16_t addr)
{
    uint32_t v = 0;
    for (int i = 0; i < 7; i++) {
        uint8_t *p = jo_mem((uint16_t)(addr + i));
        unsigned d = p ? (*p & 15) : 10;
        if (d > 9) return 0;
        v = v * 10 + d;
    }
    return v;
}

/* the best on the machine is whichever table's first entry is higher */
static uint32_t joust_top(void)
{
    uint32_t champion = joust_score(0xcd6d), today = joust_score(0xcfab);
    return champion > today ? champion : today;
}

static const hiscore_t game_scores = {
    .rom = "joust", .ranges = score_ranges, .nranges = 1, .whole = true, .mem = jo_mem,
    .top = joust_top,
};
