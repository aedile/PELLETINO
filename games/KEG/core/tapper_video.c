/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * tapper_video.c - the 91490 CPU board's 16x16 tile map and the 91464 Super Video Gen's 32x32
 * sprites, drawn at half their resolution: the hardware's 512x480 picture is doubled tiles
 * and full-resolution sprites, and 256x240 is what the medal's panel can use of it. Tiles
 * are expanded once into RAM; sprites are read straight from their 128 KB of ROM, every
 * other pixel of every other line.
 */
#include "tapper_internal.h"
#include <string.h>

static uint8_t tile_px[1024 * 32];   /* 4bpp 8x8 tiles, two pixels a byte, even pixel high */

void tap_video_init(void)
{
    /* two ROM halves each hold two bits of every pixel, two pixels a nibble; the second half is
     * the high pair */
    const uint8_t *lo = tap_roms.gfx1, *hi = tap_roms.gfx1 + 0x4000;
    for (int t = 0; t < 1024; t++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                int byte = t * 16 + y * 2 + (x >> 2), sh = 6 - 2 * (x & 3);
                uint8_t v = (uint8_t)((((hi[byte] >> sh) & 3) << 2) | ((lo[byte] >> sh) & 3));
                uint8_t *d = &tile_px[(t << 5) | (y << 2) | (x >> 1)];
                if (x & 1) *d = (uint8_t)((*d & 0xf0) | v); else *d = (uint8_t)((*d & 0x0f) | (v << 4));
            }
}

void tap_palette(uint16_t out[TAP_PALETTE_SIZE])
{
    /* 64 entries of nine bits: three each of green, blue, red from the low bits up */
    for (int i = 0; i < 64; i++) {
        int d = tap_palram[i * 2] | ((tap_palram[i * 2 + 1] & 1) << 8);
        int r = ((d >> 6) & 7) * 255 / 7, g = (d & 7) * 255 / 7, b = ((d >> 3) & 7) * 255 / 7;
        out[i] = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
    }
}

/* one sprite pixel at half resolution: hardware column 2i, line 2j of sprite c */
static inline uint8_t sprite_px(int c, int i, int j)
{
    const uint8_t *q = tap_roms.gfx2 + (i & 3) * 0x8000;
    return (uint8_t)(q[c * 128 + j * 8 + (i >> 2)] >> 4);
}

void tap_render(uint8_t *fb)
{
    static uint8_t claimed[TAP_FB_W * TAP_FB_H / 8];     /* pixels a sprite has claimed, one bit each */
    memset(claimed, 0, sizeof(claimed));

    /* ---- tiles: 32 x 30 of them, a two-byte word each ---- */
    for (int row = 0; row < 30; row++)
        for (int col = 0; col < 32; col++) {
            int data = tap_vram[(row * 32 + col) * 2] | (tap_vram[(row * 32 + col) * 2 + 1] << 8);
            int code = data & 0x3ff;
            int pen_base = ((data >> 12) & 3) * 16;
            int flipx = data & 0x400, flipy = data & 0x800;
            /* the tile's top two bits would pick the sprite palette on some boards; not this one */
            const uint8_t *px = &tile_px[code << 5];
            for (int y = 0; y < 8; y++) {
                uint8_t *dst = fb + (row * 8 + y) * TAP_FB_W + col * 8;
                const uint8_t *src = px + ((flipy ? 7 - y : y) << 2);
                for (int x = 0; x < 8; x++) {
                    int sx = flipx ? 7 - x : x;
                    uint8_t v = (sx & 1) ? (uint8_t)(src[sx >> 1] & 0x0f) : (uint8_t)(src[sx >> 1] >> 4);
                    dst[x] = (uint8_t)(pen_base + v);
                }
            }
        }

    /* ---- sprites: 128 of them, the last entry topmost; a pixel claims its spot for good ---- */
    for (int offs = 0x1fc; offs >= 0; offs -= 4) {
        const uint8_t *s = &tap_sprram[offs];
        int code = (s[2] + 256 * ((s[1] >> 3) & 1)) & 0xff;
        int color = (~s[1] & 3) << 4;
        int hflip = (s[1] & 0x10) ? 15 : 0, vflip = (s[1] & 0x20) ? 15 : 0;
        int sx = (s[3] - 3) & 0xff, sy = (241 - s[0]) & 0xff;
        for (int j = 0; j < 16; j++) {
            int y = (sy + j) & 0xff;
            if (y < 1 || y >= TAP_FB_H) continue;
            uint8_t *dst = fb + y * TAP_FB_W;
            uint8_t *cl = claimed + y * (TAP_FB_W / 8);
            for (int i = 0; i < 16; i++) {
                int x = (sx + i) & 0xff;
                if (cl[x >> 3] & (1 << (x & 7))) continue;
                int pix = color | sprite_px(code, i ^ hflip, j ^ vflip);
                if (pix & 0x0f) {
                    cl[x >> 3] |= (uint8_t)(1 << (x & 7));
                    if (pix & 0x07) dst[x] = (uint8_t)pix;
                }
            }
        }
    }
}
