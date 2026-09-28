/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * polepos_video.c - background, road, sprites, text layer; per MAME polepos_v.cpp
 */
#include "polepos_internal.h"
#include <string.h>

static const pp_roms_t *R;
static uint16_t palette[PP_COLORS];
static uint16_t vpos_mod[256];
static uint8_t char_uniform[256], char_value[256];   /* text characters whose 64 pixels are all the same value */
#define P (R->proms)

static inline uint16_t rgb565(int r, int g, int b) { return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)); }
static inline int prom_rgb(uint8_t v) { return 0x0e * (v & 1) + 0x1f * ((v >> 1) & 1) + 0x43 * ((v >> 2) & 1) + 0x8f * ((v >> 3) & 1); }

void pp_video_init(const pp_roms_t *r)
{
    R = r;
    for (int i = 0; i < 128; i++)
        palette[i] = rgb565(prom_rgb(P[0x000 + i]), prom_rgb(P[0x100 + i]), prom_rgb(P[0x200 + i]));
    for (int i = 0; i < 256; i++)
        vpos_mod[i] = (uint16_t)(P[0x500 + i] + (P[0x600 + i] << 4) + (P[0x700 + i] << 8));
    for (int c = 0; c < 256; c++) {
        const uint8_t *src = R->chars + c * 64;
        int uni = 1;
        for (int i = 1; i < 64 && uni; i++) uni = (src[i] == src[0]);
        char_uniform[c] = (uint8_t)uni; char_value[c] = src[0];
    }
}
const uint16_t *pp_palette(void) { return palette; }

/* fb row for a native scanline, or -1 if not visible */
static inline int fb_row(int y) { return (y >= 16 && y < 240) ? y - 16 : -1; }

static void draw_background(uint8_t *fb)
{
    /* 64x16 tiles, column-major, scrolled by hscroll; only native rows 16..127 are visible */
    int xoff = pp_hscroll & 7;
    int col0 = pp_hscroll >> 3;
    for (int trow = 2; trow < 16; trow++) {
        uint8_t *rowbase = fb + (trow * 8 - 16) * PP_FB_W;
        for (int c = 0; c <= 32; c++) {
            int sx = c * 8 - xoff;
            uint16_t w = pp_view16[(((col0 + c) & 63) << 4) + trow];
            int code = (w & 0xff) | ((w & 0x4000) >> 6);
            const uint8_t *pl = P + 0x400 + ((w >> 8) & 0x3f) * 4;
            uint8_t pens[4] = { (uint8_t)(pl[0] & 0x0f), (uint8_t)(pl[1] & 0x0f), (uint8_t)(pl[2] & 0x0f), (uint8_t)(pl[3] & 0x0f) };
            const uint8_t *src = R->tiles + code * 64;
            int x0 = sx < 0 ? -sx : 0, x1 = sx + 8 > PP_FB_W ? PP_FB_W - sx : 8;
            for (int y = 0; y < 8; y++) {
                uint8_t *dst = rowbase + y * PP_FB_W + sx;
                const uint8_t *sr = src + y * 8;
                for (int x = x0; x < x1; x++) dst[x] = pens[sr[x]];
            }
        }
    }
}

static void draw_road(uint8_t *fb)
{
    const uint8_t *road_bits1 = R->road + 0x2000, *road_bits2 = R->road + 0x4000;
    for (int y = 128; y < 256; y++) {
        int fy = fb_row(y);
        if (fy < 0) continue;
        uint8_t scan[256 + 8];
        uint8_t *dest = scan;
        int yoffs = ((vpos_mod[y] + pp_road_vscroll) >> 3) & 0x1ff;
        int roadpal = pp_road16[yoffs] & 15;
        const uint8_t *rp = P + 0x800 + (roadpal << 6);
        uint8_t pen[64];
        for (int i = 0; i < 64; i++) pen[i] = (uint8_t)(0x40 + (rp[i] & 0x0f));
        int xoffs = pp_road16[0x380 + (y & 0x7f)] & 0x3ff;
        int xscroll = xoffs & 7;
        xoffs &= ~7;
        for (int x = 0; x < 256 / 8 + 1; x++, xoffs += 8) {
            if (xoffs & 0x200) {
                for (int i = 0; i < 8; i++) *dest++ = pen[0];
            } else {
                int romoffs = ((y & 0x07f) << 6) + ((xoffs & 0x1f8) >> 3);
                int control = R->road[romoffs];
                int bits1 = road_bits1[romoffs];
                int bits2 = road_bits2[(romoffs & 0xfff) | ((romoffs & 0x1000) >> 1)];
                int roadval = control & 0x3f;
                int carin = control >> 7;
                for (int i = 8; i > 0; i--) {
                    int bits = ((bits1 >> i) & 1) + (((bits2 >> i) & 1) << 1);
                    if (!carin && bits) bits++;
                    *dest++ = pen[roadval & 0x3f];
                    roadval += bits;
                }
            }
        }
        memcpy(fb + fy * PP_FB_W, scan + xscroll, 256);
    }
}

static void zoom_sprite(uint8_t *fb, int big, int code, int color, int flipx, int sx, int sy, int sizex, int sizey)
{
    const uint8_t *gfxdata = big ? R->bigsprites + (code & 0x7f) * 1024 : R->sprites + (code & 0x7f) * 256;
    int rowbytes = big ? 32 : 16;
    const uint8_t *lut = P + 0xc00 + (color & 0x3f) * 16;
    uint8_t bank = (color & 0x40) ? 0x50 : 0x10;
    uint8_t pent[16];
    for (int i = 0; i < 16; i++) { uint8_t v = lut[i] & 0x0f; pent[i] = (v == 15) ? 0xff : (uint8_t)(bank + v); }
    int offsxor = flipx ? (big ? 0x1f : 0x0f) : 0;
    int width = big ? 64 : 32;
    if ((sx & 0x3ff) >= 0x100 && (sx & 0x3ff) + width <= 0x400) return;   /* entirely off screen */
    for (int y = 0; y <= sizey; y++) {
        int yy = (sy + y) & 0x1ff;
        if (yy >= 0x10 && yy < 0xf0) {
            int dy = R->scalelut[(y << 6) + sizey] & 0x1f;
            int xx = sx & 0x3ff, siz = 0, offs = 0;
            if (!big) dy >>= 1;
            const uint8_t *src = gfxdata + dy * rowbytes;
            uint8_t *row = fb + (yy - 16) * PP_FB_W;
            for (int x = (big ? 0x40 : 0x20); x > 0; x--) {
                if (xx < 0x100) {
                    uint8_t t = pent[src[(offs >> 1) ^ offsxor]];
                    if (t != 0xff) row[xx] = t;
                }
                offs++;
                siz = siz + 1 + sizex;
                if (siz & 0x40) { siz &= 0x3f; xx = (xx + 1) & 0x3ff; }
            }
        }
    }
}

static void draw_sprites(uint8_t *fb)
{
    const uint16_t *posmem = &pp_sprite16[0x380], *sizmem = &pp_sprite16[0x780];
    for (int i = 0; i < 64; i++, posmem += 2, sizmem += 2) {
        int sx = (posmem[1] & 0x3ff) - 0x40 + 4;
        int sy = 512 - (posmem[0] & 0x1ff) + 1;
        int sizex = (sizmem[1] & 0x3f00) >> 8;
        int sizey = (sizmem[0] & 0x3f00) >> 8;
        int code = sizmem[0] & 0x7f;
        int flipx = (sizmem[0] >> 7) & 1;
        int color = sizmem[1] & 0x3f;
        if (sy >= 128) color |= 0x40;
        zoom_sprite(fb, (sizmem[0] >> 15) & 1, code, color, flipx, sx, sy, sizex, sizey);
    }
}

static void draw_text(uint8_t *fb)
{
    for (int trow = 2; trow < 30; trow++) {            /* native rows 16..239 */
        for (int tcol = 0; tcol < 32; tcol++) {
            int idx = trow * 32 + tcol;
            uint16_t w = pp_alpha16[idx];
            int code = (w & 0xff) | ((w & 0x4000) >> 6);
            int color = (w >> 8) & 0x3f;
            if (!pp_chacl) { code &= 0xff; color = 0; }
            const uint8_t *lut = P + 0x300 + color * 4;
            uint8_t bank = (idx >= 32 * 16) ? 0x60 : 0x20;
            uint8_t pens[4]; int opaque = 0;
            for (int i = 0; i < 4; i++) { uint8_t v = lut[i] & 0x0f; pens[i] = (v == 15) ? 0xff : (uint8_t)(bank + v); opaque |= (v != 15); }
            if (!opaque) continue;
            uint8_t *dst = fb + (trow * 8 - 16) * PP_FB_W + tcol * 8;
            if (code < 256 && char_uniform[code]) {          /* blank or solid: one pen for the whole cell */
                uint8_t t = pens[char_value[code]];
                if (t == 0xff) continue;
                for (int y = 0; y < 8; y++, dst += PP_FB_W) memset(dst, t, 8);
                continue;
            }
            const uint8_t *src = R->chars + code * 64;
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    uint8_t t = pens[src[y * 8 + x]];
                    if (t != 0xff) dst[x] = t;
                }
                dst += PP_FB_W;
            }
        }
    }
}

#ifdef PP_VIDEO_PROFILE
uint32_t pp_video_prof[5];
uint32_t (*pp_video_clock)(void);
#define VP(i) do { uint32_t _n = pp_video_clock(); pp_video_prof[i] += _n - _t; _t = _n; } while (0)
#else
#define VP(i)
#endif
int pp_render_mask = 0x0f;    /* bit0 background, bit1 road, bit2 sprites, bit3 text (diagnostics) */
void pp_video_render(uint8_t *fb)
{
#ifdef PP_VIDEO_PROFILE
    uint32_t _t = pp_video_clock();
#endif
    memset(fb, 0, PP_FB_W * PP_FB_H);
    VP(0);
    if (pp_render_mask & 1) draw_background(fb);
    VP(1);
    if (pp_render_mask & 2) draw_road(fb);
    VP(2);
    if (pp_render_mask & 4) draw_sprites(fb);
    VP(3);
    if (pp_render_mask & 8) draw_text(fb);
    VP(4);
}
