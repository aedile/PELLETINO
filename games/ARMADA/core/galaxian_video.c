/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * galaxian_video.c - tilemap, sprites, shells and stars, drawn straight into the rotated frame.
 *
 * The cabinet's monitor is turned 90 degrees, so a raw scanline runs down the screen as we see
 * it. We render one display row at a time; a display row is a fixed raw x, so it sits entirely
 * inside one tilemap column, and the column's scroll and colour are fetched once per row.
 * (This is Frogger's renderer with Frogger's own quirks - the nibble swaps and the colour
 * permutation - taken back out, plus the two things Galaxian has that Frogger does not: the
 * shells and the starfield.)
 */
#include "galaxian_internal.h"
#include <string.h>

void gx_palette(uint16_t out[GX_PALETTE_SIZE])
{
    /* resistor ladders: 1k, 470 and 220 ohms on red and green, 470 and 220 on blue */
    for (int i = 0; i < 32; i++) {
        uint8_t d = gx_roms.prom[i];
        int r = 33 * ((d >> 0) & 1) + 71 * ((d >> 1) & 1) + 151 * ((d >> 2) & 1);
        int g = 33 * ((d >> 3) & 1) + 71 * ((d >> 4) & 1) + 151 * ((d >> 5) & 1);
        int b =                        71 * ((d >> 6) & 1) + 151 * ((d >> 7) & 1);
        out[i] = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
    }
    out[GX_PEN_WHITE]  = 0xFFFF;
    out[GX_PEN_YELLOW] = 0xFFE0;
    /* stars: two bits each of red, green and blue through a small ladder */
    static const uint8_t level[4] = { 0x00, 0x47, 0x97, 0xde };
    for (int i = 0; i < 64; i++) {
        int r = level[i & 3], g = level[(i >> 2) & 3], b = level[(i >> 4) & 3];
        out[GX_PEN_STARS + i] = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
    }
}

/*
 * The character generator is two bit planes that have to be picked apart a bit at a time. Since
 * a display row is a fixed raw x, every pixel in it comes from the same bit position of the
 * plane bytes - so the whole ROM is expanded once, at init, into ready-made pixel values
 * indexed by tile, bit and row.
 */
static uint8_t gfx_col[256 * 8 * 8];      /* [tile][bit][row] -> 2-bit pixel */

/*
 * The starfield. A 17-bit shift register runs for its whole period; a star appears wherever
 * nine particular bits line up, which is about one position in five hundred, and its colour
 * is the next six bits. The period is 131071 and each raw line starts 512 positions after the
 * one before, so the pattern does not repeat down the screen. There are only a few hundred
 * stars in the whole sequence, so they are kept as a sorted list and each line looks up the
 * ones that fall in its window rather than walking every position.
 */
#define STAR_PERIOD ((1 << 17) - 1)
#define STAR_MAX    512
static uint32_t star_off[STAR_MAX];
static uint8_t  star_col[STAR_MAX];
static int      nstars;

void gx_video_init(void)
{
    const uint8_t *gfx = gx_roms.gfx;
    for (int tile = 0; tile < 256; tile++)
        for (int bit = 0; bit < 8; bit++)
            for (int row = 0; row < 8; row++) {
                int o = tile * 8 + row;
                gfx_col[(tile << 6) | (bit << 3) | row] =
                    (uint8_t)((((gfx[o] >> bit) & 1) << 1) | ((gfx[0x800 + o] >> bit) & 1));
            }

    uint32_t sr = 0;
    nstars = 0;
    for (uint32_t i = 0; i < STAR_PERIOD; i++) {
        if ((sr & 0x1fe01) == 0x1fe00 && nstars < STAR_MAX) {
            star_off[nstars] = i;
            star_col[nstars] = (uint8_t)((~sr & 0x1f8) >> 3);
            nstars++;
        }
        /* fed from the XOR of bit 12 and the inverse of bit 0 */
        sr = (sr >> 1) | ((((sr >> 12) ^ ~sr) & 1) << 16);
    }
}

/* one 16x16 sprite pixel; planes are the two halves of the 4 KB graphics ROM */
static inline uint8_t sprite_pixel(const uint8_t *gfx, int code, int px, int py)
{
    int off = (py < 8 ? py * 8 : 128 + (py - 8) * 8) + (px < 8 ? px : 64 + (px - 8));
    int byte = code * 32 + (off >> 3);
    int bit = 7 - (off & 7);
    return (uint8_t)((((gfx[byte] >> bit) & 1) << 1) | ((gfx[0x800 + byte] >> bit) & 1));
}

/* first star at or after `off` in the sorted list */
static int star_from(uint32_t off)
{
    int lo = 0, hi = nstars;
    while (lo < hi) { int mid = (lo + hi) >> 1; if (star_off[mid] < off) lo = mid + 1; else hi = mid; }
    return lo;
}

static void draw_stars(uint8_t *fb)
{
    /* the stars blink in four groups; one group is dark at a time, changing every half second */
    int dark = (gx_frame_no / 30) & 3;
    for (int raw_y = 16; raw_y < 240; raw_y++) {
        int dx = GX_FB_W - 1 - (raw_y - 16);
        uint32_t base = ((uint32_t)raw_y * 512 + gx_stars_scroll) % STAR_PERIOD;
        /* the window [base, base+256) may wrap the period */
        for (int pass = 0; pass < 2; pass++) {
            uint32_t b = pass ? 0 : base;
            uint32_t e = pass ? (base + 256 > STAR_PERIOD ? base + 256 - STAR_PERIOD : 0) : base + 256;
            if (e <= b) continue;
            for (int i = star_from(b); i < nstars && star_off[i] < e; i++) {
                if ((star_col[i] & 3) == dark) continue;
                int raw_x = (int)(star_off[i] - b) + (pass ? (int)(STAR_PERIOD - base) : 0);
                if (raw_x < 0 || raw_x > 255) continue;
                fb[raw_x * GX_FB_W + dx] = (uint8_t)(GX_PEN_STARS + (star_col[i] & 0x3f));
            }
        }
    }
}

void gx_render(uint8_t *fb)
{
    const uint8_t *gfx = gx_roms.gfx;

    memset(fb, 0, GX_FB_W * GX_FB_H);
    if (gx_stars_on) draw_stars(fb);

    /* ---- tilemap, one display row (one raw column) at a time; pen 0 lets the stars through ---- */
    for (int dy = 0; dy < GX_FB_H; dy++) {
        int raw_x = dy;
        uint8_t *dst = fb + dy * GX_FB_W;
        int col = raw_x >> 3;
        int px = raw_x & 7;
        uint8_t scroll = gx_oram[col * 2];
        int pen_base = (gx_oram[col * 2 + 1] & 7) * 4;
        int bit = 7 - px;   /* the plane bit this whole display row reads */

        int dx = 0, raw_y = 16 + GX_FB_W - 1;
        while (dx < GX_FB_W) {
            int sy = (raw_y + scroll) & 0xff;
            const uint8_t *g = &gfx_col[(gx_vram[((sy >> 3) << 5) | col] << 6) | (bit << 3)];
            int r = sy & 7;
            int n = r + 1;                               /* pixels left in this tile going down */
            if (dx + n > GX_FB_W) n = GX_FB_W - dx;
            for (int k = 0; k < n; k++, r--) {
                uint8_t pix = g[r];
                if (pix) dst[dx + k] = (uint8_t)(pen_base + pix);
            }
            dx += n; raw_y -= n;
        }
    }

    /* ---- sprites: eight of them, drawn back to front so sprite 0 wins ---- */
    for (int n = 7; n >= 0; n--) {
        const uint8_t *base = &gx_oram[0x40 + n * 4];
        /* the first three sprites match one line earlier than the rest */
        uint8_t sy = (uint8_t)(240 - (base[0] - (n < 3 ? 1 : 0)));
        int code = base[1] & 0x3f;
        int flipx = base[1] & 0x40, flipy = base[1] & 0x80;
        int pen_base = (base[2] & 7) * 4;
        uint8_t sx = (uint8_t)(base[3] + 1);

        for (int iy = 0; iy < 16; iy++) {
            int raw_y = sy + iy;
            if (raw_y < 16 || raw_y > 239) continue;
            int dx = GX_FB_W - 1 - (raw_y - 16);
            for (int ix = 0; ix < 16; ix++) {
                int raw_x = sx + ix;
                /* the line buffer hard-clips the first 16 pixels of every sprite row */
                if (raw_x < 16 || raw_x > 255) continue;
                uint8_t pix = sprite_pixel(gfx, code, flipx ? 15 - ix : ix, flipy ? 15 - iy : iy);
                if (pix) fb[raw_x * GX_FB_W + dx] = (uint8_t)(pen_base + pix);
            }
        }
    }

    /*
     * ---- shells: eight one-pixel shots, four pixels long ----
     * Each begins displaying when the horizontal counter reaches 0xFC and stops at 0x00. The
     * first three match the line before the one their register names; the last one is the
     * player's and is yellow, the rest white.
     */
    for (int n = 0; n < 8; n++) {
        const uint8_t *b = &gx_oram[0x60 + n * 4];
        int raw_y = (n < 3 ? 256 : 255) - b[1];
        if (raw_y < 16 || raw_y > 239) continue;
        int dx = GX_FB_W - 1 - (raw_y - 16);
        uint8_t pen = (n == 7) ? GX_PEN_YELLOW : GX_PEN_WHITE;
        int x0 = 255 - b[3] - 4;
        for (int k = 0; k < 4; k++) {
            int raw_x = x0 + k;
            if (raw_x < 0 || raw_x > 255) continue;
            fb[raw_x * GX_FB_W + dx] = pen;
        }
    }
}
