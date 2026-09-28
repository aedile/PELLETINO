/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * rallyx_video.c - two tilemaps, eight sprites and the radar dots.
 *
 * All of it comes out of the one 4 KB block at 0x8000, which is unusually crowded:
 *   0x000-0x3FF  radar strip tile codes, and in the first 64 bytes the sprites and dots
 *   0x400-0x7FF  playfield tile codes
 *   0x800-0xBFF  radar strip attributes, dot Y positions
 *   0xC00-0xFFF  playfield attributes
 * Sprites live at 0x14-0x1F and the radar dots at 0x20-0x3F, overlapping the top of the radar
 * tile codes - the game simply does not draw radar tiles there.
 *
 * Colour goes through two PROMs: the pixel and the tile's colour index a 256-byte lookup that
 * yields one of 16 entries, and those index a 32-entry palette. The upper 16 palette entries
 * are the shadowed versions, which is how the smoke screen darkens what is under it.
 */
#include "rallyx_internal.h"
#include <string.h>

/* expanded once at init: [tile][row][col] -> 2-bit pixel, for characters and for sprites */
static uint8_t chr_px[256 * 8 * 8];
static uint8_t spr_px[64 * 16 * 16];

void rx_video_init(void)
{
    const uint8_t *g = rx_roms.gfx;
    /* characters: 2 bits per pixel taken from bit 0 and bit 4 of the same byte, four pixels
     * to a byte, and the two halves of each row swapped */
    for (int t = 0; t < 256; t++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                int xo = (x < 4) ? (64 + x) : (x - 4);
                int off = t * 128 + y * 8 + xo;
                uint8_t b0 = (uint8_t)((g[off >> 3] >> (7 - (off & 7))) & 1);
                int off1 = off + 4;
                uint8_t b1 = (uint8_t)((g[off1 >> 3] >> (7 - (off1 & 7))) & 1);
                chr_px[(t << 6) | (y << 3) | x] = (uint8_t)((b0 << 1) | b1);
            }
    /* sprites: the same packing over a 16x16 cell built from four 8x8 quadrants */
    static const int sx_off[16] = { 64, 65, 66, 67, 128, 129, 130, 131,
                                    192, 193, 194, 195, 0, 1, 2, 3 };
    for (int t = 0; t < 64; t++)
        for (int y = 0; y < 16; y++) {
            int yo = (y < 8) ? (y * 8) : (256 + (y - 8) * 8);
            for (int x = 0; x < 16; x++) {
                int off = t * 512 + yo + sx_off[x];
                uint8_t b0 = (uint8_t)((g[off >> 3] >> (7 - (off & 7))) & 1);
                int off1 = off + 4;
                uint8_t b1 = (uint8_t)((g[off1 >> 3] >> (7 - (off1 & 7))) & 1);
                spr_px[(t << 8) | (y << 4) | x] = (uint8_t)((b0 << 1) | b1);
            }
        }
}

void rx_palette(uint16_t out[RX_PALETTE_SIZE])
{
    /* 1k/470/220 ohm ladders on red and green, 470/220 on blue */
    for (int i = 0; i < 32; i++) {
        uint8_t d = rx_roms.pal[i];
        int r = 33 * ((d >> 0) & 1) + 71 * ((d >> 1) & 1) + 151 * ((d >> 2) & 1);
        int g = 33 * ((d >> 3) & 1) + 71 * ((d >> 4) & 1) + 151 * ((d >> 5) & 1);
        int b =                        71 * ((d >> 6) & 1) + 151 * ((d >> 7) & 1);
        out[i] = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
    }
}

/* one tile, drawn opaque; cat records which pixels came from a foreground tile */
static void draw_tile(uint8_t *fb, uint8_t *cat, int sx, int sy, int code, int color,
                      int flipx, int flipy, int category, int clip_lo, int clip_hi)
{
    const uint8_t *px = &chr_px[code << 6];
    const uint8_t *lut = &rx_roms.lut[(color & 0x3f) * 4];
    for (int y = 0; y < 8; y++) {
        int py = sy + y;
        if (py < 0 || py >= RX_FB_H) continue;
        int ry = flipy ? 7 - y : y;
        uint8_t *drow = fb + py * RX_FB_W;
        uint8_t *crow = cat + py * RX_FB_W;
        for (int x = 0; x < 8; x++) {
            int pxx = sx + x;
            if (pxx < clip_lo || pxx > clip_hi || pxx < 0 || pxx >= RX_FB_W) continue;
            int rx = flipx ? 7 - x : x;
            drow[pxx] = (uint8_t)(lut[px[(ry << 3) | rx]] & 0x0f);
            crow[pxx] = (uint8_t)category;
        }
    }
}

void rx_render(uint8_t *fb)
{
    static uint8_t cat[RX_FB_W * RX_FB_H];
    memset(fb, 0, RX_FB_W * RX_FB_H);
    memset(cat, 0, sizeof(cat));

    /* ---- playfield: 32x32 tiles, scrolling, clipped to the left 28 columns ---- */
    for (int row = 0; row < 32; row++) {
        for (int col = 0; col < 32; col++) {
            int idx = row * 32 + col;
            int code = rx_vram[0x400 + idx];
            uint8_t attr = rx_vram[0xc00 + idx];
            /* the scroll registers count backwards, and the playfield sits three pixels over */
            int sx = ((col * 8 - rx_scrollx + 3) & 0xff);
            int sy = ((row * 8 - rx_scrolly) & 0xff) - 16;
            /* a tile can wrap; draw it at both candidate positions and let the clip decide */
            for (int rep = 0; rep < 2; rep++) {
                int dx = sx + rep * 256;
                if (dx > 27 * 8) continue;
                draw_tile(fb, cat, dx, sy, code, attr & 0x3f,
                          !(attr & 0x40), (attr & 0x80) != 0, (attr >> 5) & 1, 0, 28 * 8 - 1);
            }
        }
    }

    /* ---- radar strip: 8x32 tiles repeated, clipped to the right quarter ---- */
    for (int row = 0; row < 32; row++) {
        for (int col = 0; col < 8; col++) {
            int idx = col + row * 32;
            int code = rx_vram[idx];
            uint8_t attr = rx_vram[0x800 + idx];
            int sy = row * 8 - 16;
            for (int rep = 3; rep <= 4; rep++) {          /* the two repeats that touch x >= 224 */
                int dx = col * 8 + rep * 64;
                draw_tile(fb, cat, dx, sy, code, attr & 0x3f,
                          !(attr & 0x40), (attr & 0x80) != 0, (attr >> 5) & 1, 28 * 8, RX_FB_W - 1);
            }
        }
    }

    /* ---- radar dots and smoke, solid pass (under the sprites) ---- */
    for (int pass = 0; pass < 2; pass++) {
        for (int offs = 0x14; offs < 0x20; offs++) {
            int dot = ((rx_radarattr[offs & 0x0f] & 0x0e) >> 1) ^ 0x07;
            /* the X positions sit at videoram+0x20 and the Y positions 0x800 further on */
            int x = rx_vram[0x20 + offs] + ((~rx_radarattr[offs & 0x0f] & 0x01) << 8);
            int y = 253 - rx_vram[0x820 + offs];
            for (int iy = 0; iy < 4; iy++) {
                int py = y + iy - 16;
                if (py < 0 || py >= RX_FB_H) continue;
                for (int ix = 0; ix < 4; ix++) {
                    int px = x + ix;
                    if (px < 0 || px >= RX_FB_W) continue;
                    uint8_t pix = (uint8_t)(rx_roms.dots[dot * 16 + iy * 4 + ix] & 3);
                    if (pix == 3) continue;                /* transparent in both passes */
                    if (pass == 0) fb[py * RX_FB_W + px] = (uint8_t)(0x10 + pix);
                    else {                                  /* shadow: darken what is beneath */
                        uint8_t *p = &fb[py * RX_FB_W + px];
                        if (*p < 16) *p = (uint8_t)(*p + 16);
                    }
                }
            }
        }
        if (pass == 0) {
            /* ---- sprites, between the two dot passes ---- */
            for (int offs = 0x1e; offs >= 0x14; offs -= 2) {
                int sx = rx_vram[offs + 1] + ((rx_vram[0x800 + offs + 1] & 0x80) << 1);
                int sy = 241 - rx_vram[0x800 + offs] - 16;
                int color = rx_vram[0x800 + offs + 1] & 0x3f;
                int flipx = rx_vram[offs] & 1, flipy = rx_vram[offs] & 2;
                int code = (rx_vram[offs] & 0xfc) >> 2;
                const uint8_t *px = &spr_px[code << 8];
                const uint8_t *lut = &rx_roms.lut[color * 4];
                for (int iy = 0; iy < 16; iy++) {
                    int py = sy + iy;
                    if (py < 0 || py >= RX_FB_H) continue;
                    int ry = flipy ? 15 - iy : iy;
                    for (int ix = 0; ix < 16; ix++) {
                        int pxx = sx + ix;
                        if (pxx < 0 || pxx >= RX_FB_W) continue;
                        if (cat[py * RX_FB_W + pxx]) continue;   /* foreground tiles win */
                        int rxx = flipx ? 15 - ix : ix;
                        uint8_t v = lut[px[(ry << 4) | rxx]] & 0x0f;
                        if (v) fb[py * RX_FB_W + pxx] = v;
                    }
                }
            }
        }
    }
}
