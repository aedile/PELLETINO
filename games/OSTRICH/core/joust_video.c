/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * joust_video.c - the 4-bit bitmap. The byte at offset y + 256 * (x / 2) holds pixels x and
 * x + 1 of line y, even pixel in the high nibble. The palette is sixteen bytes of RGB 3-3-2,
 * weighted by the resistor ladder on the board.
 */
#include "joust_internal.h"
#include <string.h>

void jo_palette(uint16_t out[JO_PALETTE_SIZE])
{
    /* 1200, 560 and 330 ohm for red and green, 560 and 330 for blue */
    static const uint8_t w3[8] = { 0, 37, 81, 118, 137, 174, 218, 255 };
    static const uint8_t w2[4] = { 0, 95, 160, 255 };
    for (int i = 0; i < 16; i++) {
        uint8_t d = jo_palram[i];
        int r = w3[d & 7], g = w3[(d >> 3) & 7], b = w2[(d >> 6) & 3];
        out[i] = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
    }
}

/* the visible columns, each a run of 240 bytes in the video RAM: a straight copy per column */
void jo_snapshot(uint8_t *snap)
{
    for (int c = 0; c < JO_SNAP_COLS; c++)
        memcpy(snap + c * JO_SCREEN_H, jo_vram + ((JO_SCREEN_X0 / 2 + c) << 8) + JO_SCREEN_Y0, JO_SCREEN_H);
}

void jo_render(uint8_t *fb)
{
    for (int y = 0; y < JO_FB_H; y++) {
        const uint8_t *line = jo_vram + JO_SCREEN_Y0 + y;
        uint8_t *dst = fb + y * JO_FB_W;
        for (int x = JO_SCREEN_X0; x < JO_SCREEN_X0 + JO_SCREEN_W; x += 2) {
            uint8_t v = line[(x >> 1) << 8];
            *dst++ = (uint8_t)(v >> 4);
            *dst++ = (uint8_t)(v & 0x0f);
        }
    }
}
