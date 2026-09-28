/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * centiped_video.c - tile layer, sprites and palette RAM; per MAME centiped_v.cpp.
 *
 * Eight bytes of palette RAM (the ones with bit 2 of the address set) hold the colors:
 * 0x1404-0x1407 the four tile colors, 0x140C-0x140F the four sprite colors. Each byte,
 * inverted: bit 0 red, bit 1 green, bit 2 blue, bit 3 "alternate", which dims blue, or
 * green if there is no blue. A sprite's color byte picks one of the four sprite colors
 * for each of its three visible pens; color 0 makes that pen transparent.
 */
#include "centiped_internal.h"
#include <string.h>

static const ce_roms_t *R;
static uint16_t palette[CE_PALETTE_SIZE];

static inline uint16_t rgb565(int r, int g, int b) { return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }

void ce_video_init(const ce_roms_t *r) { R = r; ce_video_reset(); }
void ce_video_reset(void) { memset(palette, 0, sizeof(palette)); }

void ce_video_palette_w(int offset, uint8_t data)
{
    if (!(offset & 4)) return;
    int r = (~data & 1) ? 0xff : 0, g = (~data & 2) ? 0xff : 0, b = (~data & 4) ? 0xff : 0;
    if (~data & 8) { if (b) b = 0xc0; else if (g) g = 0xc0; }
    palette[((offset & 8) ? 4 : 0) + (offset & 3)] = rgb565(r, g, b);
}

void ce_palette(uint16_t out[CE_PALETTE_SIZE]) { memcpy(out, palette, sizeof(palette)); }

void ce_video_render(uint8_t *fb)
{
    const uint8_t *vram = ce_ram_bytes + 0x400;
    /* tiles: 32 columns x 30 rows */
    for (int row = 0; row < 30; row++) {
        for (int col = 0; col < 32; col++) {
            uint8_t d = vram[row * 32 + col];
            const uint8_t *t = R->tiles + ((d & 0x3f) + 0x40) * 64;
            int fx = d & 0x40, fy = d & 0x80;
            uint8_t *dst = fb + (row * 8) * CE_FB_W + col * 8;
            for (int y = 0; y < 8; y++) {
                const uint8_t *src = t + (fy ? 7 - y : y) * 8;
                uint8_t *o = dst + y * CE_FB_W;
                if (fx) for (int x = 0; x < 8; x++) o[x] = src[7 - x];
                else    for (int x = 0; x < 8; x++) o[x] = src[x];
            }
        }
    }
    /* sprites: 16 of them, 8x16, not drawn in the rightmost 8 columns */
    const uint8_t *spr = ce_ram_bytes + 0x7c0;
    for (int i = 0; i < 16; i++) {
        int code = ((spr[i] & 0x3e) >> 1) | ((spr[i] & 0x01) << 6);
        int color = spr[i + 0x30] & 0x3f;
        int fx = spr[i] & 0x40, fy = spr[i] & 0x80;
        int sx = spr[i + 0x20], sy = 240 - spr[i + 0x10];
        uint8_t pen[4] = { 0, (uint8_t)(color & 3), (uint8_t)((color >> 2) & 3), (uint8_t)((color >> 4) & 3) };
        const uint8_t *g = R->sprites + code * 128;
        for (int y = 0; y < 16; y++) {
            int py = sy + y;
            if (py < 0 || py >= CE_FB_H) continue;
            const uint8_t *src = g + (fy ? 15 - y : y) * 8;
            uint8_t *o = fb + py * CE_FB_W;
            for (int x = 0; x < 8; x++) {
                int px = sx + x;
                if (px < 0 || px > CE_FB_W - 1 - 8) continue;
                uint8_t p = src[fx ? 7 - x : x];
                if (p && pen[p]) o[px] = (uint8_t)(4 + pen[p]);
            }
        }
    }
}
