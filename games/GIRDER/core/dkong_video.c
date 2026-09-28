/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * dkong_video.c - a 32x32 character map with sprites over it.
 *
 * The character colour does not come from the map at all: it comes from a PROM indexed by the
 * tile's column and by which band of four rows it is in, so the girders, the ladders and the
 * score line get their colours from where they sit on the screen rather than from anything the
 * CPU writes.
 */
#include "dkong_internal.h"
#include <string.h>

static uint8_t chr_px[256 * 8 * 8];      /* 2bpp characters, expanded once */
static uint8_t spr_px[128 * 16 * 16];    /* 2bpp 16x16 sprites */

void dk_video_init(void)
{
    /* characters: two bit planes, the second half of the ROM holding the high bit */
    for (int t = 0; t < 256; t++)
        for (int y = 0; y < 8; y++) {
            uint8_t p0 = dk_roms.chr[0x800 + t * 8 + y];
            uint8_t p1 = dk_roms.chr[0x000 + t * 8 + y];
            for (int x = 0; x < 8; x++) {
                int b = 7 - x;
                chr_px[(t << 6) | (y << 3) | x] =
                    (uint8_t)((((p1 >> b) & 1) << 1) | ((p0 >> b) & 1));
            }
        }
    /* sprites: 16x16 built from two 8-wide halves, the planes in separate quarters of the ROM */
    for (int t = 0; t < 128; t++)
        for (int y = 0; y < 16; y++) {
            for (int half = 0; half < 2; half++) {
                int off = t * 16 + y + half * 0x800;
                uint8_t p0 = dk_roms.spr[0x1000 + off];
                uint8_t p1 = dk_roms.spr[0x0000 + off];
                for (int x = 0; x < 8; x++) {
                    int b = 7 - x;
                    spr_px[(t << 8) | (y << 4) | (half * 8 + x)] =
                        (uint8_t)((((p1 >> b) & 1) << 1) | ((p0 >> b) & 1));
                }
            }
        }
}

void dk_palette(uint16_t out[DK_PALETTE_SIZE])
{
    /* both PROMs drive inverters, so the weights are subtracted from full brightness */
    for (int i = 0; i < 256; i++) {
        uint8_t lo = dk_roms.pal_lo[i], hi = dk_roms.pal_hi[i];
        int r = 255 - (33 * ((hi >> 1) & 1) + 71 * ((hi >> 2) & 1) + 151 * ((hi >> 3) & 1));
        int g = 255 - (33 * ((lo >> 2) & 1) + 71 * ((lo >> 3) & 1) + 151 * ((hi >> 0) & 1));
        int b = 255 - (             71 * ((lo >> 0) & 1) + 151 * ((lo >> 1) & 1));
        /* the background is tri-stated to real black wherever the low two bits are clear */
        if ((i & 0x03) == 0x00) { r = g = b = 0; }
        out[i] = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
    }
}

void dk_render(uint8_t *fb)
{
    memset(fb, 0, DK_FB_W * DK_FB_H);

    /* ---- the character map: 32 columns of 32 rows, of which 28 are visible ---- */
    for (int row = 0; row < 32; row++) {
        int sy = row * 8 - 16;                   /* the visible window starts 16 lines down */
        for (int col = 0; col < 32; col++) {
            int idx = row * 32 + col;
            int code = dk_vram[idx] + 256 * dk_gfxbank;
            /* the colour comes from the PROM, by column and by band of four rows */
            int color = (dk_roms.colcode[(idx % 32) + 32 * (idx / 32 / 4)] & 0x0f)
                        + 0x10 * dk_palettebank;
            const uint8_t *px = &chr_px[(code & 0xff) << 6];
            int pen_base = (color & 0x3f) * 4;
            for (int y = 0; y < 8; y++) {
                int py = sy + y;
                if (py < 0 || py >= DK_FB_H) continue;
                uint8_t *dst = fb + py * DK_FB_W + col * 8;
                const uint8_t *srow = px + (y << 3);
                for (int x = 0; x < 8; x++) dst[x] = (uint8_t)(pen_base + srow[x]);
            }
        }
    }

    /* ---- sprites ---- */
    for (int offs = 0; offs < 0x400; offs += 4) {
        uint8_t sy_raw = dk_sprram[offs];
        if (!sy_raw) continue;
        int code = (dk_sprram[offs + 1] & 0x7f) + ((dk_sprram[offs + 2] & 0x40) << 1);
        int color = (dk_sprram[offs + 2] & 0x0f) + 16 * dk_palettebank;
        int flipx = dk_sprram[offs + 2] & 0x80;
        int flipy = dk_sprram[offs + 1] & 0x80;
        int x = (dk_sprram[offs + 3] - 8) & 0xff;
        /* the buffer matches a sprite against the scanline counter; the top line works out
         * as 247 - y once that comparison is unwound */
        int y = ((247 - sy_raw) & 0xff) - 16;
        const uint8_t *px = &spr_px[(code & 0x7f) << 8];
        int pen_base = (color & 0x3f) * 4;

        for (int rep = -1; rep <= 0; rep++) {           /* sprites wrap rather than clip */
            int bx = x + rep * 256;
            for (int iy = 0; iy < 16; iy++) {
                int py = y + iy;
                if (py < 0 || py >= DK_FB_H) continue;
                int ry = flipy ? 15 - iy : iy;
                uint8_t *dst = fb + py * DK_FB_W;
                for (int ix = 0; ix < 16; ix++) {
                    int pxx = bx + ix;
                    if (pxx < 0 || pxx >= DK_FB_W) continue;
                    int rx = flipx ? 15 - ix : ix;
                    uint8_t v = px[(ry << 4) | rx];
                    if (v) dst[pxx] = (uint8_t)(pen_base + v);
                }
            }
        }
    }
}
