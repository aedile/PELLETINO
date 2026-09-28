/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * mrdo_video.c - two 32x32 character maps, one scrolling behind the other, and sixteen-colour
 * sprites over both. The picture is 240 wide by 192 tall in the hardware's own terms; the
 * cabinet turns the monitor a quarter turn anticlockwise (MAME's ROT270), and the renderer
 * does that.
 */
#include "mrdo_internal.h"
#include <string.h>

static uint8_t bg_px[512 * 64];      /* 2bpp characters, expanded once: [tile][y][x] */
static uint8_t fg_px[512 * 64];
static uint8_t spr_px[128 * 256];    /* 2bpp 16x16 sprites */
static uint8_t spr_lut[64];          /* sprite colour * 4 + pen -> palette index */
static uint8_t bg_blank[512], fg_blank[512];   /* characters with no set pixel at all */

static void expand_chars(uint8_t *dst, uint8_t *blank, const uint8_t *rom)
{
    /* two planes in the two halves of the 8 KB: the first half is the low bit */
    for (int t = 0; t < 512; t++)
        for (int y = 0; y < 8; y++) {
            uint8_t lo = rom[t * 8 + y], hi = rom[0x1000 + t * 8 + y];
            if (y == 0) blank[t] = 1;
            if (lo | hi) blank[t] = 0;
            for (int x = 0; x < 8; x++) {
                int b = 7 - x;
                dst[(t << 6) | (y << 3) | x] = (uint8_t)((((hi >> b) & 1) << 1) | ((lo >> b) & 1));
            }
        }
}

void md_video_init(void)
{
    expand_chars(fg_px, fg_blank, md_roms.fg);
    expand_chars(bg_px, bg_blank, md_roms.bg);
    /* sprites: 64 bytes each, four bytes a row; a byte holds four pixels with the high bit in
     * its low nibble and the low bit in its high nibble */
    for (int t = 0; t < 128; t++)
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) {
                uint8_t b = md_roms.spr[t * 64 + y * 4 + (x >> 2)];
                int bit = 3 - (x & 3);
                spr_px[(t << 8) | (y << 4) | x] = (uint8_t)((((b >> bit) & 1) << 1) | ((b >> (bit + 4)) & 1));
            }
    /* the sprite colour lookup: the low nibble of the PROM for colours 0-7, the high for 8-15 */
    for (int i = 0; i < 64; i++) {
        uint8_t e = md_roms.prom[0x40 + (i & 0x1f)];
        e = (i & 0x20) ? (uint8_t)(e >> 4) : (uint8_t)(e & 0x0f);
        spr_lut[i] = (uint8_t)(e + ((e & 0x0c) << 3));
    }
}

void md_palette(uint16_t out[MD_PALETTE_SIZE])
{
    /* two PROMs feed a four-resistor ladder per colour through diodes, with a pulldown */
    static int weight[16];
    static int have_weight;
    if (!have_weight) {
        const float R1 = 150, R2 = 120, R3 = 100, R4 = 75, pull = 220, drop = 0.7f;
        float pot[16];
        for (int i = 15; i >= 0; i--) {
            float par = 0;
            if (i & 1) par += 1.0f / R1;
            if (i & 2) par += 1.0f / R2;
            if (i & 4) par += 1.0f / R3;
            if (i & 8) par += 1.0f / R4;
            pot[i] = par ? pull / (pull + 1.0f / par) - drop : 0;
        }
        for (int i = 0; i < 16; i++) {
            int w = (int)(255.0f * pot[i] / pot[15]);
            weight[i] = w < 0 ? 0 : w;
        }
        have_weight = 1;
    }
    for (int i = 0; i < 256; i++) {
        int a1 = ((i >> 3) & 0x1c) + (i & 0x03) + 0x20;     /* the low-bits PROM */
        int a2 = ((i >> 0) & 0x1c) + (i & 0x03);            /* the high-bits PROM */
        uint8_t lo = md_roms.prom[a1], hi = md_roms.prom[a2];
        int r = weight[((lo >> 0) & 3) | (((hi >> 0) & 3) << 2)];
        int g = weight[((lo >> 2) & 3) | (((hi >> 2) & 3) << 2)];
        int b = weight[((lo >> 4) & 3) | (((hi >> 4) & 3) << 2)];
        out[i] = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
    }
}

/*
 * The tilemaps are 256x256 and the screen shows columns 8-247 and rows 32-223 of them. The
 * hardware scans x from the right: tilemap column 8 is the frame's last pixel column, so a
 * frame pixel (fx, fy) is tilemap (247 - fx, fy + 32). The background map is scrolled first.
 */
static void draw_map(uint8_t *fb, const uint8_t *vram, const uint8_t *px, const uint8_t *blank, int sx, int sy)
{
    for (int row = 0; row < 32; row++)
        for (int col = 0; col < 32; col++) {
            uint8_t attr = vram[row * 32 + col];
            int code = vram[0x400 + row * 32 + col] + ((attr & 0x80) << 1);
            int pen_base = (attr & 0x3f) * 4;
            int opaque = attr & 0x40;
            if (blank[code] && !opaque) continue;      /* nothing to draw */
            const uint8_t *t = &px[code << 6];
            for (int y = 0; y < 8; y++) {
                int fy = ((row * 8 + y - sy) & 0xff) - 32;
                if (fy < 0 || fy >= MD_FB_H) continue;
                uint8_t *dst = fb + fy * MD_FB_W;
                const uint8_t *trow = t + (y << 3);
                for (int x = 0; x < 8; x++) {
                    int fx = 247 - ((col * 8 + x - sx) & 0xff);
                    if (fx < 0 || fx >= MD_FB_W) continue;
                    uint8_t v = trow[x];
                    if (v || opaque) dst[fx] = (uint8_t)(pen_base + v);
                }
            }
        }
}

int md_dbg_layers = 7;               /* host diagnostics: 1 bg, 2 fg, 4 sprites */
void md_render(uint8_t *fb)
{
    memset(fb, 0, MD_FB_W * MD_FB_H);
    if (md_dbg_layers & 1) draw_map(fb, md_bgram, bg_px, bg_blank, md_scrollx, md_scrolly);
    if (md_dbg_layers & 2) draw_map(fb, md_fgram, fg_px, fg_blank, 0, 0);
    if (!(md_dbg_layers & 4)) return;

    /* sprites, last entry first so the first one ends up on top */
    for (int offs = 0xfc; offs >= 0; offs -= 4) {
        const uint8_t *s = &md_sprram[offs];
        if (!s[1]) continue;
        int code = s[0] & 0x7f;
        int color = s[2] & 0x0f;
        int flipx = s[2] & 0x10, flipy = s[2] & 0x20;
        int x = 247 - s[3] - 15, y = 256 - s[1] - 32;    /* x runs from the right, as for the maps */
        flipx = !flipx;
        const uint8_t *px = &spr_px[code << 8];
        for (int iy = 0; iy < 16; iy++) {
            int fy = y + iy;
            if (fy < 0 || fy >= MD_FB_H) continue;
            int ry = flipy ? 15 - iy : iy;
            uint8_t *dst = fb + fy * MD_FB_W;
            for (int ix = 0; ix < 16; ix++) {
                int fx = x + ix;
                if (fx < 0 || fx >= MD_FB_W) continue;
                int rx = flipx ? 15 - ix : ix;
                uint8_t v = px[(ry << 4) | rx];
                if (v) dst[fx] = spr_lut[color * 4 + v];
            }
        }
    }

    if (md_flip) {                                   /* the cocktail second player: turn it all round */
        for (int y = 0; y < MD_FB_H / 2; y++)
            for (int x = 0; x < MD_FB_W; x++) {
                uint8_t *a = &fb[y * MD_FB_W + x], *b = &fb[(MD_FB_H - 1 - y) * MD_FB_W + (MD_FB_W - 1 - x)];
                uint8_t t = *a; *a = *b; *b = t;
            }
    }
}
