/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * btime_video.c - a 32x32 map of 8x8 characters in three bit planes, eight 16x16 sprites, and
 * an optional background of 16x16 tiles laid out by a PROM and scrolled along the hardware's
 * x axis. The palette is sixteen registers the program writes.
 *
 * The frame is kept in hardware coordinates, 240x240 of a 256x256 field (8..247 each way);
 * the renderer turns it for the cabinet's sideways monitor.
 */
#include "btime_internal.h"
#include <string.h>

static uint8_t chr_px[1024 * 64];    /* 3bpp characters: 8 KB a plane is 1024 of them */
static uint8_t spr_px[256 * 256];    /* 3bpp 16x16 sprites, from the same ROMs */
static uint8_t bg_px[64 * 256];      /* 3bpp 16x16 background tiles */

static inline uint8_t plane_bits(const uint8_t *rom, int plane_size, int byte, int bit)
{
    return (uint8_t)((((rom[2 * plane_size + byte] >> bit) & 1) << 2) |
                     (((rom[plane_size + byte] >> bit) & 1) << 1) |
                     ((rom[byte] >> bit) & 1));
}

void bt_video_init(void)
{
    /* characters: 8 bytes a plane, planes a third of the ROM apart, the last third highest */
    for (int t = 0; t < 1024; t++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                chr_px[(t << 6) | (y << 3) | x] = plane_bits(bt_roms.gfx1, 0x2000, t * 8 + y, 7 - x);
    /* 16x16 tiles: 32 bytes a plane; the left eight pixels come from the second sixteen bytes */
    for (int t = 0; t < 256; t++)
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                spr_px[(t << 8) | (y << 4) | x] = plane_bits(bt_roms.gfx1, 0x2000, t * 32 + y + (x < 8 ? 16 : 0), 7 - (x & 7));
    for (int t = 0; t < 64; t++)
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                bg_px[(t << 8) | (y << 4) | x] = plane_bits(bt_roms.gfx2, 0x800, t * 32 + y + (x < 8 ? 16 : 0), 7 - (x & 7));
}

void bt_palette(uint16_t out[BT_PALETTE_SIZE])
{
    /* BGR 2:3:3 in each register, inverted */
    for (int i = 0; i < 16; i++) {
        uint8_t d = (uint8_t)~bt_palram[i];
        int b = ((d >> 5) & 7) * 255 / 7;
        int g = ((d >> 2) & 7) * 255 / 7;
        int r = (d & 3) * 255 / 3;
        out[i] = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
    }
}

/* hardware (x, y) of the 256 field into the 240x240 frame */
#define PUT(x, y, v) do { int px_ = (x) - 8, py_ = (y) - 8; \
    if (px_ >= 0 && px_ < BT_FB_W && py_ >= 0 && py_ < BT_FB_H) fb[py_ * BT_FB_W + px_] = (v); } while (0)

static void draw_tile16(uint8_t *fb, const uint8_t *px, int x0, int y0, int flipx, int flipy, int pen_base, int opaque)
{
    for (int iy = 0; iy < 16; iy++) {
        const uint8_t *row = px + ((flipy ? 15 - iy : iy) << 4);
        for (int ix = 0; ix < 16; ix++) {
            uint8_t v = row[flipx ? 15 - ix : ix];
            if (v || opaque) PUT(x0 + ix, y0 + iy, (uint8_t)(pen_base + v));
        }
    }
}

static void draw_chars(uint8_t *fb, int transparent)
{
    for (int offs = 0; offs < 0x400; offs++) {
        int x = 31 - (offs / 32), y = offs % 32;
        int code = bt_vram[offs] + 256 * (bt_cram[offs] & 3);
        const uint8_t *px = &chr_px[code << 6];
        for (int iy = 0; iy < 8; iy++) {
            const uint8_t *row = px + (iy << 3);
            for (int ix = 0; ix < 8; ix++) {
                uint8_t v = row[ix];
                if (v || !transparent) PUT(8 * x + ix, 8 * y + iy, v);
            }
        }
    }
}

static void draw_background(uint8_t *fb)
{
    /* four pages of the map PROM in turn, scrolled along x, with one extra page for the wrap */
    int start = 1;
    uint8_t tmap[4];
    for (int i = 0; i < 4; i++) { tmap[i] = (uint8_t)(start | (bt_scroll[0] & 0x04)); start = (start + 1) & 3; }
    int scroll = -(bt_scroll[1] | ((bt_scroll[0] & 0x03) << 8));
    for (int i = 0; i < 5; i++, scroll += 256) {
        if (scroll > 256) break;
        if (scroll < -256) continue;
        const uint8_t *page = &bt_roms.bgmap[tmap[i & 3] * 0x100];
        for (int offs = 0; offs < 0x100; offs++) {
            int x = 240 - (16 * (offs / 16) + scroll) - 1;
            int y = 16 * (offs % 16);
            draw_tile16(fb, &bg_px[(page[offs] & 0x3f) << 8], x, y, 0, 0, 8, 1);
        }
    }
}

void bt_render(uint8_t *fb)
{
    memset(fb, 0, BT_FB_W * BT_FB_H);
    if (bt_scroll[0] & 0x10) { draw_background(fb); draw_chars(fb, 1); }
    else draw_chars(fb, 0);

    /* eight sprites, their words spread down the first column of the video RAM */
    for (int i = 0; i < 8; i++) {
        const uint8_t *s = &bt_vram[i * 0x80];
        if (!(s[0] & 0x01)) continue;
        int x = 240 - s[0x60], y = 240 - s[0x40] - 1;
        int flipx = s[0] & 0x04, flipy = s[0] & 0x02;
        const uint8_t *px = &spr_px[s[0x20] << 8];
        draw_tile16(fb, px, x, y, flipx, flipy, 0, 0);
        draw_tile16(fb, px, x, y + 256, flipx, flipy, 0, 0);     /* and again for the wrap */
    }
}
