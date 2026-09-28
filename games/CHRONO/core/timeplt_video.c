/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * timeplt_video.c - a 32x32 character map with 24 sprites over it, and a few characters that
 * sit over the sprites (the ones the program flags), drawn straight into the rotated frame.
 *
 * The cabinet's monitor is turned a quarter turn (MAME's ROT90): the hardware's top edge is
 * the player's right, as for Frogger. A frame pixel (fx, fy) is hardware (x = fy, y = 239 - fx)
 * of the 256x256 field, of which lines 16-239 are shown.
 */
#include "timeplt_internal.h"
#include <string.h>

static uint8_t chr_px[512 * 64];     /* 2bpp characters, expanded once */
static uint8_t spr_px[256 * 256];    /* 2bpp 16x16 sprites */
static uint8_t chr_lut[32 * 4];      /* colour * 4 + pen -> palette entry */
static uint8_t spr_lut[64 * 4];

/* a pixel in one of the ROMs: four pixels a byte, the high bit of a pixel in the low nibble */
static inline uint8_t rom_px(const uint8_t *rom, int byte, int p)
{
    uint8_t b = rom[byte];
    return (uint8_t)((((b >> (3 - p)) & 1) << 1) | ((b >> (7 - p)) & 1));
}

void tp_video_init(void)
{
    /* characters: 16 bytes each, the left half's rows then the right half's */
    for (int t = 0; t < 512; t++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                chr_px[(t << 6) | (y << 3) | x] = rom_px(tp_roms.chr, t * 16 + y + 8 * (x >> 2), x & 3);
    /* sprites: 64 bytes each, four columns of four pixels, the bottom half 32 bytes on */
    for (int t = 0; t < 256; t++)
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                spr_px[(t << 8) | (y << 4) | x] = rom_px(tp_roms.spr, t * 64 + (y & 7) + 32 * (y >> 3) + 8 * (x >> 2), x & 3);
    /* the lookup PROMs: sprites use the first sixteen palette entries, characters the other sixteen */
    for (int i = 0; i < 256; i++) spr_lut[i] = (uint8_t)(tp_roms.prom[0x40 + i] & 0x0f);
    for (int i = 0; i < 128; i++) chr_lut[i] = (uint8_t)((tp_roms.prom[0x140 + i] & 0x0f) + 0x10);
}

void tp_palette(uint16_t out[TP_PALETTE_SIZE])
{
    static const int w[5] = { 0x19, 0x24, 0x35, 0x40, 0x4d };
    for (int i = 0; i < 32; i++) {
        uint8_t a = tp_roms.prom[i], b = tp_roms.prom[0x20 + i];
        int r = w[0] * ((b >> 1) & 1) + w[1] * ((b >> 2) & 1) + w[2] * ((b >> 3) & 1) + w[3] * ((b >> 4) & 1) + w[4] * ((b >> 5) & 1);
        int g = w[0] * ((b >> 6) & 1) + w[1] * ((b >> 7) & 1) + w[2] * ((a >> 0) & 1) + w[3] * ((a >> 1) & 1) + w[4] * ((a >> 2) & 1);
        int bl = w[0] * ((a >> 3) & 1) + w[1] * ((a >> 4) & 1) + w[2] * ((a >> 5) & 1) + w[3] * ((a >> 6) & 1) + w[4] * ((a >> 7) & 1);
        out[i] = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (bl >> 3));
    }
}

/* the frame is display-oriented: hardware (x, y) lands at frame column 239 - y, row x */
#define PUT(x, yv, v) fb[(x) * TP_FB_W + (TP_FB_W - 1 - (yv))] = (v)

static void draw_chars(uint8_t *fb, int category)
{
    for (int row = 0; row < 32; row++) {
        int y0 = row * 8 - 16;
        if (y0 + 8 <= 0 || y0 >= TP_FB_W) continue;
        for (int col = 0; col < 32; col++) {
            uint8_t attr = tp_cram[row * 32 + col];
            if (((attr >> 4) & 1) != category) continue;
            int code = tp_vram[row * 32 + col] + 8 * (attr & 0x20);
            int flipx = attr & 0x40, flipy = attr & 0x80;
            const uint8_t *px = &chr_px[code << 6];
            const uint8_t *lut = &chr_lut[(attr & 0x1f) * 4];
            for (int y = 0; y < 8; y++) {
                int yv = y0 + y;
                if (yv < 0 || yv >= TP_FB_W) continue;
                const uint8_t *prow = px + ((flipy ? 7 - y : y) << 3);
                for (int x = 0; x < 8; x++) {
                    uint8_t v = prow[flipx ? 7 - x : x];
                    PUT(col * 8 + x, yv, lut[v]);
                }
            }
        }
    }
}

void tp_render(uint8_t *fb)
{
    if (!tp_video_enable) { memset(fb, 0, TP_FB_W * TP_FB_H); return; }

    draw_chars(fb, 0);

    /* sprites: 24 of them, the later entries first so the earlier ones win; x from one bank,
     * y and code from the other, flip x is inverted on this board */
    for (int offs = 0x3e; offs >= 0x10; offs -= 2) {
        int sx = tp_spr0[offs];
        int sy = 241 - tp_spr1[offs + 1];
        int code = tp_spr0[offs + 1];
        int flipx = !(tp_spr1[offs] & 0x40), flipy = tp_spr1[offs] & 0x80;
        const uint8_t *px = &spr_px[code << 8];
        const uint8_t *lut = &spr_lut[(tp_spr1[offs] & 0x3f) * 4];
        for (int iy = 0; iy < 16; iy++) {
            int yv = sy + iy - 16;
            if (yv < 0 || yv >= TP_FB_W) continue;
            const uint8_t *prow = px + ((flipy ? 15 - iy : iy) << 4);
            for (int ix = 0; ix < 16; ix++) {
                int x = sx + ix;
                if (x < 0 || x > 255) continue;
                uint8_t v = prow[flipx ? 15 - ix : ix];
                if (v) PUT(x, yv, lut[v]);
            }
        }
    }

    draw_chars(fb, 1);
}
