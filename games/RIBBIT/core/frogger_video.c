/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * frogger_video.c - Galaxian-style tilemap and sprites, drawn straight into the rotated frame.
 *
 * The cabinet's monitor is turned 90 degrees, so a raw scanline runs down the screen as we see
 * it. That is worth leaning into: we render one display row at a time, and because a display
 * row is a fixed raw x, it sits entirely inside one tilemap column - so the column's scroll and
 * colour are fetched once per row and the writes run straight down the frame buffer.
 *
 * That per-column vertical scroll is what makes the game: rotated, it becomes the sideways
 * drift of the traffic lanes and the logs.
 */
#include "frogger_internal.h"
#include <string.h>

/* Frogger permutes the three colour bits on its way to the PROM */
static inline uint8_t frogger_color(uint8_t c)
{
    return (uint8_t)(((c >> 1) & 0x03) | ((c << 2) & 0x04));
}

void fr_palette(uint16_t out[FR_PALETTE_SIZE])
{
    /* resistor ladders: 1k, 470 and 220 ohms on red and green, 470 and 220 on blue */
    for (int i = 0; i < 32; i++) {
        uint8_t d = fr_roms.prom[i];
        int r = 33 * ((d >> 0) & 1) + 71 * ((d >> 1) & 1) + 151 * ((d >> 2) & 1);
        int g = 33 * ((d >> 3) & 1) + 71 * ((d >> 4) & 1) + 151 * ((d >> 5) & 1);
        int b =                        71 * ((d >> 6) & 1) + 151 * ((d >> 7) & 1);
        out[i] = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
    }
    /* the river: a flat blue field behind half the picture */
    out[FR_PEN_RIVER] = (uint16_t)(((0 & 0xf8) << 8) | ((0 & 0xfc) << 3) | (0x47 >> 3));
}

/*
 * The character generator is two bit planes that have to be picked apart a bit at a time. Since
 * a display row is a fixed raw x, every pixel in it comes from the same bit position of the
 * plane bytes - so the whole ROM can be expanded once, at init, into ready-made pixel values
 * indexed by tile, bit and row. That turns the inner loop into a single load and is most of the
 * difference between drawing 42 frames a second and drawing all of them.
 */
static uint8_t gfx_col[256 * 8 * 8];      /* [tile][bit][row] -> 2-bit pixel */

void fr_video_init(void)
{
    const uint8_t *gfx = fr_roms.gfx;
    for (int tile = 0; tile < 256; tile++)
        for (int bit = 0; bit < 8; bit++)
            for (int row = 0; row < 8; row++) {
                int o = tile * 8 + row;
                gfx_col[(tile << 6) | (bit << 3) | row] =
                    (uint8_t)((((gfx[o] >> bit) & 1) << 1) | ((gfx[0x800 + o] >> bit) & 1));
            }
}

/* one 16x16 sprite pixel; planes are the two halves of the 4 KB graphics ROM */
static inline uint8_t sprite_pixel(const uint8_t *gfx, int code, int px, int py)
{
    int off = (py < 8 ? py * 8 : 128 + (py - 8) * 8) + (px < 8 ? px : 64 + (px - 8));
    int byte = code * 32 + (off >> 3);
    int bit = 7 - (off & 7);
    /* plane 0 is the high bit of the pixel, as MAME's gfx_layout orders them */
    return (uint8_t)((((gfx[byte] >> bit) & 1) << 1) | ((gfx[0x800 + byte] >> bit) & 1));
}

void fr_render(uint8_t *fb)
{
    const uint8_t *gfx = fr_roms.gfx;

    /* ---- background and tilemap, one display row (one raw column) at a time ---- */
    for (int dy = 0; dy < FR_FB_H; dy++) {
        int raw_x = dy;
        uint8_t *dst = fb + dy * FR_FB_W;
        /* the river covers the half of the screen the frog swims in */
        memset(dst, raw_x < 128 ? FR_PEN_RIVER : 0, FR_FB_W);

        int col = raw_x >> 3;
        int px = raw_x & 7;
        /* Frogger swaps the nibbles of the scroll value on its way into the adder */
        uint8_t sc = fr_oram[col * 2];
        uint8_t scroll = (uint8_t)((sc >> 4) | (sc << 4));
        uint8_t color = frogger_color(fr_oram[col * 2 + 1] & 7);
        int pen_base = color * 4;
        int bit = 7 - px;   /* the plane bit this whole display row reads */

        /* Eight consecutive pixels down the row come from one tile, so the map lookup is
         * hoisted out and only the two plane bytes are fetched per pixel. Doing this per pixel
         * instead cost about three times as much, which was the whole frame budget. */
        int dx = 0, raw_y = 16 + FR_FB_W - 1;
        while (dx < FR_FB_W) {
            int sy = (raw_y + scroll) & 0xff;
            const uint8_t *g = &gfx_col[(fr_vram[((sy >> 3) << 5) | col] << 6) | (bit << 3)];
            int r = sy & 7;
            int n = r + 1;                               /* pixels left in this tile going down */
            if (dx + n > FR_FB_W) n = FR_FB_W - dx;
            for (int k = 0; k < n; k++, r--) {
                uint8_t pix = g[r];
                if (pix) dst[dx + k] = (uint8_t)(pen_base + pix);   /* pen 0 lets the river through */
            }
            dx += n; raw_y -= n;
        }
    }

    /* ---- sprites: eight of them, drawn back to front so sprite 0 wins ---- */
    for (int n = 7; n >= 0; n--) {
        const uint8_t *base = &fr_oram[0x40 + n * 4];
        uint8_t b0 = (uint8_t)((base[0] >> 4) | (base[0] << 4));   /* nibbles swapped, as above */
        /* the first three sprites match one line earlier than the rest */
        uint8_t sy = (uint8_t)(240 - (b0 - (n < 3 ? 1 : 0)));
        int code = base[1] & 0x3f;
        int flipx = base[1] & 0x40, flipy = base[1] & 0x80;
        uint8_t color = frogger_color(base[2] & 7);
        uint8_t sx = (uint8_t)(base[3] + 1);
        int pen_base = color * 4;

        for (int iy = 0; iy < 16; iy++) {
            int raw_y = sy + iy;
            if (raw_y < 16 || raw_y > 239) continue;
            int dx = FR_FB_W - 1 - (raw_y - 16);
            for (int ix = 0; ix < 16; ix++) {
                int raw_x = sx + ix;
                /* the line buffer hard-clips the first 16 pixels of every sprite row */
                if (raw_x < 16 || raw_x > 255) continue;
                uint8_t pix = sprite_pixel(gfx, code, flipx ? 15 - ix : ix, flipy ? 15 - iy : iy);
                if (pix) fb[raw_x * FR_FB_W + dx] = (uint8_t)(pen_base + pix);
            }
        }
    }
}
