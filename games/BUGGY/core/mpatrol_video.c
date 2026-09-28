/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * mpatrol_video.c - the M52 picture: the sprite palette's colour 0 behind everything, then the
 * distant mountains and either the hills or the city (each a 256x64 painting of 2-bit pixels
 * on a solid ground of its own colour 3), then the 32x32 character map (its top seven rows
 * opaque, the rest see-through at pen 0, its bottom quarter scrolling), then the sprites.
 *
 * The visible window is the 240x252 of MAME's screen: characters land 8 pixels left of it and
 * 6 above, sprites 7 left and 6 above, backgrounds 12 left and 6 above.
 *
 * Every layer's colours come from PROMs of eight-bit values, so however many entries they have
 * between them there are few distinct colours: the frame holds an index into a table of the
 * distinct ones, built once.
 */
#include "mpatrol_internal.h"
#include <string.h>

static uint16_t pal[MP_PALETTE_SIZE];
static int n_colours;
static uint8_t spr_col[32];                  /* sprite palette entry -> frame colour */
static uint8_t spr_clut[256];                /* (set, pen) -> sprite palette entry; 0 = see-through */
static uint8_t bg_col[3][4];
static uint8_t tx_col[512];                  /* (set, pen) -> frame colour */
static uint32_t plane_lut[2][256];           /* a plane byte -> its eight pixels' bit, one byte each, as two words */
static uint32_t plane_lut_hi[2][256];
static uint32_t bg_lut[256];                 /* a painting byte -> its four pens, a byte each */
static uint8_t tx_blank[512];                /* 1: every pixel of the character is pen 0 */
static uint8_t tx_px[512 * 64];              /* the characters as a pen a pixel */
#ifdef MP_DEBUG
uint32_t mp_dbg_tiles_skipped, mp_dbg_tiles_drawn, mp_dbg_bg_rows;
#endif

static uint16_t rgb(int r, int g, int b) { return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3)); }

static uint8_t colour_id(uint16_t c)
{
    for (int i = 0; i < n_colours; i++) if (pal[i] == c) return (uint8_t)i;
    if (n_colours < MP_PALETTE_SIZE) { pal[n_colours] = c; return (uint8_t)n_colours++; }
    return (uint8_t)(MP_PALETTE_SIZE - 1);
}

void mp_video_init(void)
{
    /* 1000, 470, 220 ohm ladders for red and green, 470, 220 for blue */
    static const uint8_t w3[8] = { 0, 33, 71, 104, 151, 184, 222, 255 };
    static const uint8_t w2[4] = { 0, 81, 174, 255 };
    const uint8_t *tx_pal = mp_roms.proms, *bg_pal = mp_roms.proms + 0x200, *sp_pal = mp_roms.proms + 0x220, *clut = mp_roms.proms + 0x240;
    n_colours = 0;
    /* the sprites' own palette first, so that its entry 0 - the colour behind everything - is index 0 */
    for (int i = 0; i < 32; i++) {
        uint8_t d = sp_pal[i];
        spr_col[i] = colour_id(rgb(w2[(d >> 6) & 3], w3[(d >> 3) & 7], w3[d & 7]));
    }
    for (int i = 0; i < 256; i++) spr_clut[i] = (uint8_t)(clut[i] & 0x1f);
    /* the backgrounds: 32 PROM entries, of which each painting picks four */
    static const uint8_t bg_pick[3][4] = { { 0, 4, 8, 12 }, { 0, 1, 2, 3 }, { 0, 17, 18, 19 } };
    for (int i = 0; i < 3; i++)
        for (int p = 0; p < 4; p++) {
            uint8_t d = bg_pal[bg_pick[i][p]];
            bg_col[i][p] = colour_id(rgb(w3[d & 7], w3[(d >> 3) & 7], w2[(d >> 6) & 3]));
        }
    /* the characters: 128 sets of four, straight from the PROM */
    for (int i = 0; i < 512; i++) {
        uint8_t d = tx_pal[i];
        tx_col[i] = colour_id(rgb(w3[d & 7], w3[(d >> 3) & 7], w2[(d >> 6) & 3]));
    }
    /* a byte of a plane spread to a byte a pixel, leftmost pixel first (bit 7 first); plane 1 is worth 2 */
    for (int b = 0; b < 256; b++) {
        uint32_t lo = 0, hi = 0;
        for (int k = 0; k < 4; k++) { lo |= (uint32_t)((b >> (7 - k)) & 1) << (8 * k); hi |= (uint32_t)((b >> (3 - k)) & 1) << (8 * k); }
        plane_lut[0][b] = lo; plane_lut_hi[0][b] = hi;
        plane_lut[1][b] = lo << 1; plane_lut_hi[1][b] = hi << 1;
        /* a painting byte: pixel k has bit 1 at bit 3-k and bit 0 at bit 7-k */
        bg_lut[b] = (hi << 1) | lo;
    }
    for (int c = 0; c < 512; c++) {
        int blank = 1;
        for (int y = 0; y < 8; y++) {
            uint8_t b0 = mp_roms.tx[c * 8 + y], b1 = mp_roms.tx[0x1000 + c * 8 + y];
            if (b0 | b1) blank = 0;
            for (int x = 0; x < 8; x++) tx_px[c * 64 + y * 8 + x] = (uint8_t)((((b1 >> (7 - x)) & 1) << 1) | ((b0 >> (7 - x)) & 1));
        }
        tx_blank[c] = (uint8_t)blank;
    }
}

void mp_palette(uint16_t out[MP_PALETTE_SIZE]) { memcpy(out, pal, sizeof(pal)); }

/* a 256x64 painting at (x0, y0) of the window, wrapping sideways, on its ground of pen 3 */
static void draw_background(uint8_t *fb, int img, int x0, int y0, int vy_end)
{
    const uint8_t *rom = mp_roms.bg + img * 0x1000;
    const uint8_t *cols = bg_col[img];
    for (int vy = 0; vy < vy_end; vy++) {
        int iy = vy - y0;
        if (iy < 0 || iy >= 256) continue;
        uint8_t *dst = fb + vy * MP_FB_W;
        if (iy >= 64) { memset(dst, cols[3], MP_FB_W); continue; }
        const uint8_t *row = rom + (iy >> 5) * 0x800 + (iy & 31) * 64;
        int ix = (-x0) & 0xff, vx = 0;
        while (vx < MP_FB_W) {
            uint32_t p = bg_lut[row[ix >> 2]];
            for (int k = ix & 3; k < 4 && vx < MP_FB_W; k++, vx++) {
                int pen = (p >> (8 * k)) & 3;
                if (pen) dst[vx] = cols[pen];
            }
            ix = (ix + 4) & 0xfc;
        }
    }
}

void mp_render(uint8_t *fb)
{
    /* ---- the paintings; each one's ground is solid from 64 rows below its top, so whatever
     * sits under that need not be drawn at all ---- */
    int near = (mp_bgctl & 0x20) ? -1 : !(mp_bgctl & 0x02) ? 1 : !(mp_bgctl & 0x04) ? 2 : -1;
    int far = (mp_bgctl & 0x20) || (mp_bgctl & 0x10) ? -1 : 0;
    int near_ground = near >= 0 ? mp_bgy[0] - 6 + 64 : MP_FB_H;
    int far_ground = far >= 0 ? mp_bgy[1] - 6 + 64 : MP_FB_H;
    int clear_to = near_ground < far_ground ? near_ground : far_ground;
    if (clear_to < 0) clear_to = 0;
    if (clear_to > MP_FB_H) clear_to = MP_FB_H;
    memset(fb, spr_col[0], MP_FB_W * clear_to);
    if (far >= 0) draw_background(fb, far, mp_bgx[1] - 12, mp_bgy[1] - 6, near_ground < MP_FB_H ? (near_ground < 0 ? 0 : near_ground) : MP_FB_H);
    if (near >= 0) draw_background(fb, near, mp_bgx[0] - 12, mp_bgy[0] - 6, MP_FB_H);
    if (near < 0 && far < 0) memset(fb, spr_col[0], MP_FB_W * MP_FB_H);

    /* ---- the characters: window (vx, vy) shows map pixel (vx + 8 [- scroll], vy + 6) ---- */
    for (int vy = 0; vy < MP_FB_H; vy++) {
        int ty = (vy + 6) & 0xff;
        int trow = ty >> 3, py = ty & 7;
        int shift = (trow >= 24) ? 8 - mp_scroll : 8;
        int opaque = trow <= 6;
        uint8_t *dst = fb + vy * MP_FB_W;
        const uint8_t *vrow = mp_vram + trow * 32, *crow = mp_cram + trow * 32;
        for (int vx = 0; vx < MP_FB_W; ) {
            int tx = (vx + shift) & 0xff;
            int tcol = tx >> 3, px = tx & 7;
            int n = 8 - px;
            if (vx + n > MP_FB_W) n = MP_FB_W - vx;
            int code = vrow[tcol] | ((crow[tcol] & 0x80) << 1);
            if (!opaque && tx_blank[code]) {
#ifdef MP_DEBUG
                mp_dbg_tiles_skipped++;
#endif
                vx += n; continue; }
#ifdef MP_DEBUG
            mp_dbg_tiles_drawn++;
#endif
            const uint8_t *cols = tx_col + (crow[tcol] & 0x7f) * 4;
            const uint8_t *t = tx_px + code * 64 + py * 8 + px;
            uint8_t *d = dst + vx;
            if (opaque) { for (int k = 0; k < n; k++) d[k] = cols[t[k]]; }
            else { for (int k = 0; k < n; k++) { int pen = t[k]; if (pen) d[k] = cols[pen]; } }
            vx += n;
        }
    }

    /* ---- the sprites: four banks in turn, each from its last entry to its first ---- */
    for (int bank = 0; bank < 0x100; bank += 0x40)
        for (int offs = bank + 0x3c; offs >= bank; offs -= 4) {
            const uint8_t *s = &mp_sprram[offs];
            int sy = 235 - s[0], sx = s[3] - 7;
            int flipx = s[1] & 0x40, flipy = s[1] & 0x80;
            const uint8_t *lut = spr_clut + (s[1] & 0x1f) * 8;
            const uint8_t *g1 = mp_roms.sp + (s[2] & 0x7f) * 32, *g0 = g1 + 0x1000;
            for (int j = 0; j < 16; j++) {
                int vy = sy + j;
                if (vy < 0 || vy >= MP_FB_H) continue;
                int gy = flipy ? 15 - j : j;
                uint8_t *dst = fb + vy * MP_FB_W;
                /* the row's sixteen pens, left half then right, four to a word */
                uint32_t w[4] = {
                    plane_lut[0][g0[gy]] | plane_lut[1][g1[gy]], plane_lut_hi[0][g0[gy]] | plane_lut_hi[1][g1[gy]],
                    plane_lut[0][g0[gy + 16]] | plane_lut[1][g1[gy + 16]], plane_lut_hi[0][g0[gy + 16]] | plane_lut_hi[1][g1[gy + 16]] };
                for (int i = 0; i < 16; i++) {
                    int vx = sx + i;
                    if (vx < 0 || vx >= MP_FB_W) continue;
                    int gx = flipx ? 15 - i : i;
                    int pen = (int)((w[gx >> 2] >> (8 * (gx & 3))) & 3);
                    uint8_t c = lut[pen];
                    if (c) dst[vx] = spr_col[c];
                }
            }
        }
}
