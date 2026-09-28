/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * mario_video.c - a 32x32 character map with a vertical scroll, and 16x16 sprites in three
 * bit planes over it.
 *
 * A character's colour comes from bits of its own code (unlike Donkey Kong, where it came from
 * a PROM by position), and the sprites carry a four-bit colour of their own; both are offset
 * by the palette bank the CPU selects.
 */
#include "mario_internal.h"
#include <string.h>

static uint8_t chr_px[512 * 8 * 8];      /* 2bpp characters, expanded once */
static uint8_t spr_px[256 * 16 * 16];    /* 3bpp 16x16 sprites */

void mb_video_init(void)
{
    /* characters: 3f is the high plane, 3j the low */
    for (int t = 0; t < 512; t++)
        for (int y = 0; y < 8; y++) {
            uint8_t hi = mb_roms.chr[0x0000 + t * 8 + y];
            uint8_t lo = mb_roms.chr[0x1000 + t * 8 + y];
            for (int x = 0; x < 8; x++) {
                int b = 7 - x;
                chr_px[(t << 6) | (y << 3) | x] = (uint8_t)((((hi >> b) & 1) << 1) | ((lo >> b) & 1));
            }
        }
    /* sprites: three planes of 8 KB (7m/7n high, 7p/7s, 7t/7u low), each plane's left halves
     * in its first 4 KB and right halves in its second */
    for (int t = 0; t < 256; t++)
        for (int y = 0; y < 16; y++)
            for (int half = 0; half < 2; half++) {
                int off = t * 16 + y + half * 0x1000;
                uint8_t p2 = mb_roms.spr[0x0000 + off];
                uint8_t p1 = mb_roms.spr[0x2000 + off];
                uint8_t p0 = mb_roms.spr[0x4000 + off];
                for (int x = 0; x < 8; x++) {
                    int b = 7 - x;
                    spr_px[(t << 8) | (y << 4) | (half * 8 + x)] =
                        (uint8_t)((((p2 >> b) & 1) << 2) | (((p1 >> b) & 1) << 1) | ((p0 >> b) & 1));
                }
            }
}

void mb_palette(uint16_t out[MB_PALETTE_SIZE])
{
    /* the PROM drives inverters, so the weights come off full brightness */
    for (int i = 0; i < 256; i++) {
        uint8_t d = mb_roms.prom[i];
        int r = 255 - (33 * ((d >> 5) & 1) + 71 * ((d >> 6) & 1) + 151 * ((d >> 7) & 1));
        int g = 255 - (33 * ((d >> 2) & 1) + 71 * ((d >> 3) & 1) + 151 * ((d >> 4) & 1));
        int b = 255 - (                        71 * ((d >> 0) & 1) + 151 * ((d >> 1) & 1));
        out[i] = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
    }
    out[0] = 0;                                        /* the blanked background */
}

void mb_render(uint8_t *fb)
{
    /* ---- the character map, scrolled vertically; the visible window is lines 16-239 ---- */
    int scroll = (mb_scroll + 17) & 0xff;
    for (int row = 0; row < 32; row++) {
        for (int col = 0; col < 32; col++) {
            int raw = mb_vram[row * 32 + col];
            int code = raw + 256 * mb_gfxbank;
            int color = 8 + (raw >> 5) + 16 * mb_palettebank;
            const uint8_t *px = &chr_px[code << 6];
            int pen_base = color * 4;
            for (int y = 0; y < 8; y++) {
                int py = ((row * 8 + y - scroll) & 0xff) - 16;
                if (py < 0 || py >= MB_FB_H) continue;
                if (mb_flip) py = MB_FB_H - 1 - py;
                uint8_t *dst = fb + py * MB_FB_W;
                const uint8_t *srow = px + (y << 3);
                /* pen 0 of any character is blanked to black by the hardware: index 0 is kept black */
                if (!mb_flip) { for (int x = 0; x < 8; x++) dst[col * 8 + x] = srow[x] ? (uint8_t)(pen_base + srow[x]) : 0; }
                else          { for (int x = 0; x < 8; x++) dst[255 - col * 8 - x] = srow[x] ? (uint8_t)(pen_base + srow[x]) : 0; }
            }
        }
    }

    /* ---- sprites: a sprite is drawn on the lines where its y matches the counter ---- */
    for (int offs = 0; offs < 0x400; offs += 4) {
        uint8_t sy_raw = mb_sprram[offs];
        if (!sy_raw) continue;
        int y = 240 - ((sy_raw + 0xfa) & 0xff) + 1 - 16;
        int x = mb_sprram[offs + 3] - 8;
        int code = mb_sprram[offs + 2];
        int color = (mb_sprram[offs + 1] & 0x0f) + 16 * mb_palettebank;
        int flipx = mb_sprram[offs + 1] & 0x80;
        int flipy = mb_sprram[offs + 1] & 0x40;
        if (mb_flip) { y = MB_FB_H - 16 - y; x = MB_FB_W - 16 - x; flipx = !flipx; flipy = !flipy; }
        const uint8_t *px = &spr_px[code << 8];
        int pen_base = color * 8;

        for (int rep = -1; rep <= 0; rep++) {           /* sprites wrap rather than clip */
            int bx = x + rep * 256;
            for (int iy = 0; iy < 16; iy++) {
                int py = y + iy;
                if (py < 0 || py >= MB_FB_H) continue;
                int ry = flipy ? 15 - iy : iy;
                uint8_t *dst = fb + py * MB_FB_W;
                for (int ix = 0; ix < 16; ix++) {
                    int pxx = bx + ix;
                    if (pxx < 0 || pxx >= MB_FB_W) continue;
                    int rx = flipx ? 15 - ix : ix;
                    uint8_t v = px[(ry << 4) | rx];
                    if (v) dst[pxx] = (uint8_t)(pen_base + v);
                }
            }
        }
    }
}
