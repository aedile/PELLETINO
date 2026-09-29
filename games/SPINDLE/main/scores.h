/*
 * scores.h - where Tempest keeps its high scores.
 * Addresses from MAME's hiscore.dat.
 */
#pragma once
#include "hiscore.h"
#include "tempest.h"

static const hiscore_range_t score_ranges[] = {
    { 0x001d, 0x01, 0x03, 0x03 },
    { 0x0605, 0x11a, 0x14, 0x00 },       /* the table and its initials */
};

/* Eight scores of three BCD bytes, low byte first, from 0x0706. Whichever is highest is
 * the best, without having to know which end of the table the game sorts to. */
static uint32_t tempest_top(void)
{
    uint32_t best = 0;
    for (int e = 0; e < 8; e++) {
        uint32_t v = 0;
        for (int i = 2; i >= 0; i--) {
            uint8_t *p = tp_mem((uint16_t)(0x0706 + e * 3 + i));
            if (!p || (*p >> 4) > 9 || (*p & 15) > 9) { v = 0; break; }
            v = v * 100 + (*p >> 4) * 10 + (*p & 15);
        }
        if (v > best) best = v;
    }
    return best;
}

static const hiscore_t game_scores = {
    .rom = "tempest", .ranges = score_ranges, .nranges = 2, .whole = false, .mem = tp_mem,
    .top = tempest_top,
};
