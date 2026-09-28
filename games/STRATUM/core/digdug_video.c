/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * digdug_video.c - three layers: the dirt, the text over it, and the sprites on top.
 *
 * The background is not a tilemap the CPU writes. It is a 4 KB ROM that says which tile goes
 * at each position - four screens' worth, selected by two bits of the video latch - and the
 * tile's own top nibble picks its colour. That is the ground you dig through, and the reason
 * the CPU only ever has to change the text layer as a tunnel opens up.
 *
 * Colour goes through two lookup PROMs into a 32-entry palette, so the frame buffer holds
 * palette indices 0-31 directly: 0-15 for the background and the text, 16-31 for sprites.
 */
#include "digdug_internal.h"
#include <string.h>

/* expanded once at init, so the inner loops are a single load */
static uint8_t chr_px[256 * 8 * 8];     /* 1bpp text characters */
static uint8_t chr_blank[256];          /* a character with no set pixels at all */
static uint8_t bgt_px[256 * 8 * 8];     /* 2bpp background tiles */
static uint8_t spr_px[256 * 16 * 16];   /* 2bpp 16x16 sprites */

void dd_video_init(void)
{
    /* text: one bit per pixel, the row reversed */
    for (int t = 0; t < 256; t++) {
        uint8_t any = 0;
        for (int y = 0; y < 8; y++) {
            any |= dd_roms.chr[t * 8 + y];
            for (int x = 0; x < 8; x++)
                chr_px[(t << 6) | (y << 3) | x] = (uint8_t)((dd_roms.chr[t * 8 + y] >> x) & 1);
        }
        chr_blank[t] = any ? 0 : 1;
    }

    /* background tiles: two bits taken from bit 0 and bit 4 of the same byte, and the two
     * halves of each row swapped */
    for (int t = 0; t < 256; t++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                int xo = (x < 4) ? (64 + x) : (x - 4);
                int off = t * 128 + y * 8 + xo;
                uint8_t b0 = (uint8_t)((dd_roms.bgt[off >> 3] >> (7 - (off & 7))) & 1);
                int o1 = off + 4;
                uint8_t b1 = (uint8_t)((dd_roms.bgt[o1 >> 3] >> (7 - (o1 & 7))) & 1);
                bgt_px[(t << 6) | (y << 3) | x] = (uint8_t)((b0 << 1) | b1);
            }

    /* sprites: a 16x16 cell built from four 8x8 quadrants */
    static const int sx_off[16] = { 0, 1, 2, 3, 64, 65, 66, 67,
                                    128, 129, 130, 131, 192, 193, 194, 195 };
    for (int t = 0; t < 256; t++)
        for (int y = 0; y < 16; y++) {
            int yo = (y < 8) ? (y * 8) : (256 + (y - 8) * 8);
            for (int x = 0; x < 16; x++) {
                int off = t * 512 + yo + sx_off[x];
                uint8_t b0 = (uint8_t)((dd_roms.spr[off >> 3] >> (7 - (off & 7))) & 1);
                int o1 = off + 4;
                uint8_t b1 = (uint8_t)((dd_roms.spr[o1 >> 3] >> (7 - (o1 & 7))) & 1);
                spr_px[(t << 8) | (y << 4) | x] = (uint8_t)((b0 << 1) | b1);
            }
        }
}

void dd_palette(uint16_t out[DD_PALETTE_SIZE])
{
    /* 1k/470/220 ohm ladders on red and green, 470/220 on blue */
    for (int i = 0; i < 32; i++) {
        uint8_t d = dd_roms.pal[i];
        int r = 33 * ((d >> 0) & 1) + 71 * ((d >> 1) & 1) + 151 * ((d >> 2) & 1);
        int g = 33 * ((d >> 3) & 1) + 71 * ((d >> 4) & 1) + 151 * ((d >> 5) & 1);
        int b =                        71 * ((d >> 6) & 1) + 151 * ((d >> 7) & 1);
        out[i] = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
    }
}

/* the 36x28 visible layer is stored in a 32x32 block; this is the hardware's mapping */
static inline int tilemap_scan(int col, int row)
{
    row += 2; col -= 2;
    if (col & 0x20) return row + ((col & 0x1f) << 5);
    return col + (row << 5);
}

void dd_video_render(uint8_t *fb)
{
    const int bg_select = dd_videolatch & 0x03;
    const int bg_color_bank = dd_videolatch & 0x30;
    const int tx_color_mode = (dd_videolatch >> 2) & 1;
    const int bg_disable = (dd_videolatch >> 3) & 1;

    for (int row = 0; row < 28; row++) {
        for (int col = 0; col < 36; col++) {
            int idx = tilemap_scan(col, row) & 0x3ff;
            int sx = col * 8, sy = row * 8;

            /* the dirt */
            int bcode = dd_roms.play[idx | (bg_select << 10)];
            int bcolor = (bg_disable ? 0x0f : (bcode >> 4)) | bg_color_bank;
            const uint8_t *bp = &bgt_px[bcode << 6];
            const uint8_t *blut = &dd_roms.lut_bg[(bcolor & 0x3f) * 4];

            /* the text over it */
            uint8_t code = dd_videoram[idx];
            int ccolor = tx_color_mode ? (code & 0x0f)
                                       : (((code >> 4) & 0x0e) | ((code >> 3) & 0x02));
            const uint8_t *cp = &chr_px[(code & 0x7f) << 6];

            /* Most of the screen is dirt with nothing written over it, so a character cell
             * that has no set pixels at all skips the overlay test entirely. That is the
             * difference between drawing about 35 frames a second and drawing most of them. */
            if (chr_blank[code & 0x7f]) {
                for (int y = 0; y < 8; y++) {
                    uint8_t *dst = fb + (sy + y) * DD_FB_W + sx;
                    const uint8_t *row = bp + (y << 3);
                    for (int x = 0; x < 8; x++) dst[x] = (uint8_t)(blut[row[x]] & 0x0f);
                }
            } else {
                uint8_t cv = (uint8_t)(ccolor & 0x0f);
                for (int y = 0; y < 8; y++) {
                    uint8_t *dst = fb + (sy + y) * DD_FB_W + sx;
                    const uint8_t *brow = bp + (y << 3);
                    const uint8_t *crow = cp + (y << 3);
                    for (int x = 0; x < 8; x++)
                        dst[x] = crow[x] ? cv : (uint8_t)(blut[brow[x]] & 0x0f);
                }
            }
        }
    }

    /* ---- sprites ---- */
    const uint8_t *sram = dd_objram + 0x380;
    const uint8_t *pram = dd_posram + 0x380;
    const uint8_t *fram = dd_flpram + 0x380;
    static const int gfx_offs[2][2] = { { 0, 1 }, { 2, 3 } };

    for (int offs = 0; offs < 0x80; offs += 2) {
        int sprite = sram[offs];
        int color = sram[offs + 1] & 0x3f;
        int sx = pram[offs + 1] - 40 + 1;
        int sy = 256 - pram[offs] + 1;        /* sprites are buffered, so delayed by a line */
        int flipx = fram[offs] & 0x01;
        int flipy = (fram[offs] & 0x02) >> 1;
        int size = (sprite & 0x80) >> 7;
        if (size) sprite = (sprite & 0xc0) | ((sprite & ~0xc0) << 2);
        sy -= 16 * size;
        sy = (sy & 0xff) - 32;                /* fix the wraparound */
        const uint8_t *lut = &dd_roms.lut_spr[color * 4];

        for (int cy = 0; cy <= size; cy++) {
            for (int cx = 0; cx <= size; cx++) {
                int cell = (sprite + gfx_offs[cy ^ (size * flipy)][cx ^ (size * flipx)]) & 0xff;
                const uint8_t *sp = &spr_px[cell << 8];
                /* the cell is drawn twice, 256 apart, because the position wraps */
                for (int rep = 0; rep < 2; rep++) {
                    int bx = ((sx + 16 * cx) & 0xff) + rep * 0x100;
                    int by = sy + 16 * cy;
                    for (int y = 0; y < 16; y++) {
                        int py = by + y;
                        if (py < 0 || py >= DD_FB_H) continue;
                        int ry = flipy ? 15 - y : y;
                        uint8_t *dst = fb + py * DD_FB_W;
                        for (int x = 0; x < 16; x++) {
                            int px = bx + x;
                            /* the hardware masks the outer columns */
                            if (px < 2 * 8 || px > 34 * 8 - 1 || px >= DD_FB_W) continue;
                            int rx = flipx ? 15 - x : x;
                            uint8_t v = (uint8_t)((lut[sp[(ry << 4) | rx]] & 0x0f) | 0x10);
                            if (v != 0x1f) dst[px] = v;   /* 0x1F is the transparent colour */
                        }
                    }
                }
            }
        }
    }
}
