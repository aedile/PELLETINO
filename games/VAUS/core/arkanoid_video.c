/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * arkanoid_video.c - a 32x32 tilemap with sixteen 8x16 sprites over it.
 *
 * Three bit planes, one 32 KB ROM each, and three colour PROMs - red, green and blue, four
 * bits apiece - giving 512 colours addressed as 64 palettes of eight. Both the tile bank and
 * the palette bank come from the same control latch the CPU writes at 0xD008.
 */
#include "arkanoid_internal.h"
#include <string.h>

/*
 * There are 4096 characters at three bits a pixel, and expanding them all would be a quarter
 * of a megabyte - more RAM than this board has. So the planes are read straight from the ROM,
 * three bytes per tile row, and the eight pixels shifted out of them. The graphics stay in
 * flash and the reads go through the instruction cache, which is cheap enough at this size.
 */
void ak_video_init(void) { }

/*
 * The PROMs hold 512 colours but a frame buffer pixel is a byte, so the palette bank cannot
 * live in the pixel: an index of 480 would come back as 224 and the whole scene would be
 * painted out of the wrong half of the PROM - which is what turned the story screen's black
 * sky white. The bank is a single latch bit that applies to the entire frame, so it belongs
 * in the palette rather than in the pixel. Pixels carry the colour's low five bits and the
 * pen; ak_palette() hands back the 256 entries of whichever half was current when the frame
 * was drawn.
 */
static uint8_t render_palbank;

void ak_palette(uint16_t out[AK_PALETTE_SIZE])
{
    int base = 256 * render_palbank;
    for (int j = 0; j < AK_PALETTE_SIZE; j++) {
        int i = base + j;
        int r = ak_roms.prom_r[i] & 0x0f;
        int g = ak_roms.prom_g[i] & 0x0f;
        int b = ak_roms.prom_b[i] & 0x0f;
        r = r * 255 / 15; g = g * 255 / 15; b = b * 255 / 15;
        out[j] = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
    }
}

static void draw_char(uint8_t *fb, int sx, int sy, int code, int color,
                      int flipx, int flipy, int transparent)
{
    const uint8_t *g = ak_roms.gfx + ((code & 0xfff) * 8);
    int pen_base = (color & 0x1f) * 8;
    for (int y = 0; y < 8; y++) {
        int py = sy + y;
        if (py < 0 || py >= AK_FB_H) continue;
        int ry = flipy ? 7 - y : y;
        uint8_t p0 = g[0x10000 + ry];      /* the most significant plane */
        uint8_t p1 = g[0x08000 + ry];
        uint8_t p2 = g[0x00000 + ry];
        uint8_t *dst = fb + py * AK_FB_W;
        for (int x = 0; x < 8; x++) {
            int pxx = sx + x;
            if (pxx < 0 || pxx >= AK_FB_W) continue;
            int b = 7 - (flipx ? 7 - x : x);
            uint8_t v = (uint8_t)((((p0 >> b) & 1) << 2) | (((p1 >> b) & 1) << 1) | ((p2 >> b) & 1));
            if (transparent && !v) continue;
            dst[pxx] = (uint8_t)(pen_base + v);
        }
    }
}

void ak_video_render(uint8_t *fb)
{
    render_palbank = ak_palbank;
    memset(fb, 0, AK_FB_W * AK_FB_H);

    /* ---- the tilemap: 32x32 tiles, two bytes each ---- */
    for (int row = 0; row < 32; row++) {
        for (int col = 0; col < 32; col++) {
            int idx = (row * 32 + col) * 2;
            int code = ak_vram[idx + 1] + ((ak_vram[idx] & 0x07) << 8) + 2048 * ak_gfxbank;
            int color = (ak_vram[idx] & 0xf8) >> 3;
            draw_char(fb, col * 8, row * 8 - 16, code, color, 0, 0, 0);
        }
    }

    /* ---- sprites: sixteen of them, each two characters tall ---- */
    for (int offs = 0; offs < 0x40; offs += 4) {
        int sx = ak_sprram[offs];
        int sy = 248 - ak_sprram[offs + 1];
        if (ak_flipx) sx = 248 - sx;
        if (ak_flipy) sy = 248 - sy;
        int code = ak_sprram[offs + 3] + ((ak_sprram[offs + 2] & 0x03) << 8) + 1024 * ak_gfxbank;
        int color = (ak_sprram[offs + 2] & 0xf8) >> 3;
        int top = sy + (ak_flipy ? 8 : -8);
        draw_char(fb, sx, top - 16,  2 * code,     color, ak_flipx, ak_flipy, 1);
        draw_char(fb, sx, sy - 16,   2 * code + 1, color, ak_flipx, ak_flipy, 1);
    }
}
