/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * gyruss_video.c - a 32x32 character map with 8x16 sprites over it.
 *
 * Two bit planes for the characters and four for the sprites, and the colour goes through two
 * lookup PROMs before it reaches the palette: the tile or sprite's four-bit colour code and its
 * pen index together address a 256-byte table, and what comes out is one of sixteen colours.
 * The characters use the upper sixteen, the sprites the lower sixteen.
 *
 * The one thing here that is not obvious from the layouts: the character map is drawn twice.
 * Bit 4 of a tile's colour byte puts it in front of the sprites rather than behind them, which
 * is how Gyruss keeps the score readable through the middle of a wave.
 *
 * Written from MAME's gyruss.cpp.
 */
#include "gyruss_internal.h"
#include <string.h>

/* expanded once at init: [tile][row][col] -> 2-bit pen */
static uint8_t chr_pen[512][8][8];
static uint8_t chr_blank[512];
/* sprites are read from ROM on the fly: 256 of them at 64 bytes each is too much to expand */

static const uint8_t *prom_palette;   /* 32 bytes */
static const uint8_t *prom_sprite;    /* 256 bytes */
static const uint8_t *prom_char;      /* 256 bytes */

void gy_video_init(void)
{
    prom_palette = gy_roms.proms;
    prom_sprite  = gy_roms.proms + 0x20;
    prom_char    = gy_roms.proms + 0x120;

    /*
     * charlayout: two planes at bit offsets 4 and 0 of the same byte, eight pixels taken four
     * from one byte and four from the byte eight along, rows one byte apart, sixteen bytes a
     * character.
     */
    for (int c = 0; c < 512; c++) {
        const uint8_t *g = gy_roms.tiles + c * 16;
        int any = 0;
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                int byte = (x < 4) ? y : y + 8;
                int shift = 3 - (x & 3);
                int p0 = (g[byte] >> (shift + 4)) & 1;   /* plane at bit offset 4 */
                int p1 = (g[byte] >> shift) & 1;         /* plane at bit offset 0 */
                uint8_t v = (uint8_t)((p0 << 1) | p1);
                chr_pen[c][y][x] = v;
                if (v) any = 1;
            }
        }
        chr_blank[c] = (uint8_t)!any;
    }
}

void gy_palette(uint16_t out[GY_PALETTE_SIZE])
{
    /* the resistor ladders: 1k / 470 / 220 on red and green, 470 / 220 on blue */
    static const int wr[3] = { 74, 156, 255 };   /* the three red and green weights, scaled */
    static const int wb[2] = { 96, 255 };        /* and the two blue ones */
    uint16_t base[32];
    for (int i = 0; i < 32; i++) {
        uint8_t d = prom_palette[i];
        int r = ((d >> 0) & 1) * wr[0] + ((d >> 1) & 1) * wr[1] + ((d >> 2) & 1) * wr[2];
        int g = ((d >> 3) & 1) * wr[0] + ((d >> 4) & 1) * wr[1] + ((d >> 5) & 1) * wr[2];
        int b = ((d >> 6) & 1) * wb[0] + ((d >> 7) & 1) * wb[1];
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        base[i] = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
    }
    /* the frame buffer carries a lookup-table result directly: 0-15 sprites, 16-31 characters */
    for (int i = 0; i < GY_PALETTE_SIZE; i++) out[i] = base[i & 0x1f];
}

/* draw one 8x8 character cell */
static void draw_char(uint8_t *fb, int sx, int sy, int code, int color, int flipx, int flipy,
                      int transparent)
{
    code &= 0x1ff;
    if (transparent && chr_blank[code]) return;
    const uint8_t *lut = prom_char + ((color & 0x0f) << 4);
    for (int y = 0; y < 8; y++) {
        int py = sy + y;
        if (py < 0 || py >= GY_FB_H) continue;
        int ry = flipy ? 7 - y : y;
        uint8_t *dst = fb + py * GY_FB_W;
        for (int x = 0; x < 8; x++) {
            int px = sx + x;
            if (px < 0 || px >= GY_FB_W) continue;
            uint8_t v = chr_pen[code][ry][flipx ? 7 - x : x];
            if (transparent && !v) continue;
            dst[px] = (uint8_t)((lut[v] & 0x0f) | 0x10);   /* characters take the upper sixteen */
        }
    }
}

/*
 * spritelayout: four planes, two in each half of the 32 KB region, at bit offsets 4 and 0.
 * Eight pixels come four from one byte and four from the byte eight along; the sixteen rows
 * are two blocks of eight, the second starting 32 bytes on. Sixty-four bytes a sprite.
 */
static void draw_sprite(uint8_t *fb, int sx, int sy, int code, int color, int bank,
                        int flipx, int flipy)
{
    const uint8_t *g = gy_roms.sprites + (bank * 0x10) + ((code & 0xff) * 64);
    const uint8_t *lut = prom_sprite + ((color & 0x0f) << 4);
    for (int y = 0; y < 16; y++) {
        int py = sy + y;
        if (py < 0 || py >= GY_FB_H) continue;
        int ry = flipy ? 15 - y : y;
        int rowbase = (ry < 8) ? ry : (ry - 8) + 32;
        uint8_t *dst = fb + py * GY_FB_W;
        for (int x = 0; x < 8; x++) {
            int px = sx + x;
            if (px < 0 || px >= GY_FB_W) continue;
            int rx = flipx ? 7 - x : x;
            int byte = rowbase + ((rx < 4) ? 0 : 8);
            int shift = 3 - (rx & 3);
            int p0 = (g[0x4000 + byte] >> (shift + 4)) & 1;
            int p1 = (g[0x4000 + byte] >> shift) & 1;
            int p2 = (g[byte] >> (shift + 4)) & 1;
            int p3 = (g[byte] >> shift) & 1;
            uint8_t v = (uint8_t)((p0 << 3) | (p1 << 2) | (p2 << 1) | p3);
            if (!v) continue;                       /* pen 0 is transparent */
            dst[px] = (uint8_t)(lut[v] & 0x0f);     /* sprites take the lower sixteen */
        }
    }
}

/* the character map, either the cells that go behind the sprites or the ones that go in front */
static void draw_tilemap(uint8_t *fb, int front)
{
    for (int row = 0; row < 32; row++) {
        for (int col = 0; col < 32; col++) {
            int idx = row * 32 + col;
            uint8_t attr = gy_colorram[idx];
            /* bit 4 clear puts the cell in front of the sprites */
            if (front != !(attr & 0x10)) continue;
            int code = ((attr & 0x20) << 3) | gy_videoram[idx];
            int color = attr & 0x0f;
            int fx = (attr >> 6) & 1, fy = (attr >> 7) & 1;
            int sx = col * 8, sy = row * 8 - 16;
            if (gy_flipscreen) { sx = 248 - sx; sy = 208 - sy; fx = !fx; fy = !fy; }
            draw_char(fb, sx, sy, code, color, fx, fy, front);
        }
    }
}

void gy_video_render(uint8_t *fb)
{
    memset(fb, 0, GY_FB_W * GY_FB_H);

    draw_tilemap(fb, 0);                    /* the opaque layer, behind everything */

    /* the sprite list runs backwards, so the first entry ends up on top */
    for (int offs = 0xbc; offs >= 0; offs -= 4) {
        int x = gy_spriteram[offs];
        int y = 241 - gy_spriteram[offs + 3];
        int bank = gy_spriteram[offs + 1] & 0x01;
        int code = ((gy_spriteram[offs + 2] & 0x20) << 2) | (gy_spriteram[offs + 1] >> 1);
        int color = gy_spriteram[offs + 2] & 0x0f;
        int flipx = !(gy_spriteram[offs + 2] & 0x40);
        int flipy = (gy_spriteram[offs + 2] & 0x80) != 0;
        draw_sprite(fb, x, y - 16, code, color, bank, flipx, flipy);
    }

    draw_tilemap(fb, 1);                    /* and the cells that sit in front of them */
}
