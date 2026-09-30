/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * cps1_video.c - the CPS-A/CPS-B picture, drawn a strip at a time at the size it is shown.
 *
 * The board has three tile maps of 64x64 tiles (8x8, 16x16 and 32x32 pixels), a list of
 * up to 256 sprites made of 16x16 blocks, and a palette of six pages of 512 colours. The
 * program chooses the order the four layers are stacked in. Tiles in the layer directly
 * under the sprites carry one of four priority groups, and each group has a mask of pens
 * that are drawn over the sprites instead of under them.
 *
 * Nothing here holds a 384x224 picture. The caller describes a view (which screen column
 * each of its columns shows, which screen row each of its rows shows) and asks for bands
 * of it. A tile is drawn by walking the view's columns that fall on it, so a view that
 * shows five columns in eight reads and writes five pixels in eight, and a row the view
 * does not show is never fetched from flash at all.
 *
 * The layers are painted back to front into the strip. For the layer under the sprites
 * the pens that belong on top are painted again after the sprites.
 *
 * Most of what is painted is painted over: in a fight the sky is a whole layer of tiles
 * with a whole layer of aeroplane over it. So before a strip is painted, each tile layer
 * is walked once from the top down, and where a tile that has no transparent pixel (the
 * converter marks them, one bit a tile) will land is noted in a mask of the strip, one bit
 * a pixel. Painting then skips every run of eight pixels that a layer above will cover
 * completely. The sprites are not opaque and cover nothing, but they are skipped under
 * opaque tiles like anything else.
 */
#include "cps1_internal.h"
#include "cps1_mem.h"
#include "knobs.h"
#include <string.h>
#ifdef VIDEO_PROFILE
#include "esp_cpu.h"
#define CPS1_CYCLES() esp_cpu_get_cycle_count()
#endif

#define VIS_X0 64                    /* the visible 384x224 begins here in the 512x262 raster */
#define VIS_Y0 16
#define PEN_NONE 15
#define BACKGROUND_PEN 0xbff

/* palette pages */
#define PAL_SPRITES 0x000
#define PAL_SCROLL1 0x200
#define PAL_SCROLL2 0x400
#define PAL_SCROLL3 0x600

static uint16_t pal_raw[0xc00];      /* as the board latched it: 4 bits each of brightness, R, G, B */
static uint16_t pal565[0xc00];       /* as the panel wants it: RGB565, bytes swapped */
static uint8_t pal_dirty;
static uint32_t pal_generation;      /* counts palette copies */
static uint8_t lut5[16][16], lut6[16][16];
#ifdef CPS1_PROFILE
uint64_t cps1_video_groups, cps1_video_skipped, cps1_video_cells;
#endif
#ifdef VIDEO_PROFILE
/* where the time goes, in CPU cycles: the caller defines CPS1_CYCLES() */
uint32_t cps1_vp[8];                 /* 0 fill, 1 mask, 2 cell rebuilds, 3 scroll1 rows, 4 scroll2 rows, 5 scroll3 rows, 6 sprites, 7 high pass */
#define VP_BEGIN uint32_t vp_t0_ = CPS1_CYCLES()
#define VP_END(i) cps1_vp[i] += CPS1_CYCLES() - vp_t0_
#define VP_MARK(i) do { uint32_t t_ = CPS1_CYCLES(); cps1_vp[i] += t_ - vp_t0_; vp_t0_ = t_; } while (0)
#else
#define VP_BEGIN
#define VP_END(i)
#define VP_MARK(i)
#endif

static uint16_t obj[0x400];          /* the sprite list, taken at the vertical blank */

static int out_w, out_h;
static const uint16_t *col_src, *row_src;
static uint16_t first_col[CPS1_SCREEN_W + 1];   /* first view column at or right of a screen column */
static uint16_t first_row[CPS1_SCREEN_H + 1];
static unsigned layer_mask = 15;
/* the view is one of two shapes worth a loop of its own: five columns in eight (SCALE), or
 * one to one (CROP, and the host's native view); anything else takes the general path */
enum { VIEW_ANY, VIEW_FIVE_OF_EIGHT, VIEW_ONE_TO_ONE };
static int view_kind;
static int view_x0;                  /* one to one: the screen column of view column 0 */

/* the frame, as latched by cps1_frame_begin() */
typedef struct {
    const uint16_t *map;
    int scrollx, scrolly;
    int on;
} layer_t;
static layer_t layers[3];
static uint8_t order[4];             /* 0 sprites, 1-3 the tile maps, back to front */
static uint16_t prio_mask[4];
static const uint16_t *rowscroll;    /* NULL unless scroll2 is scrolling line by line */
static int rowscroll_offs;
static int last_sprite;              /* index of the last sprite in obj[], -1 if none */

/* which pixels of the strip the layers above each layer cover with opaque tiles */
#define MAX_STRIP_ROWS 16
#define MASK_WORDS 12                /* 384 bits: the host's native view is the widest */
typedef uint32_t rowmask_t[MASK_WORDS + 1];    /* the last word says whether any bit is set */
static rowmask_t cover_above[4][MAX_STRIP_ROWS];   /* by position in the layer order */
static const uint8_t *opaque8, *opaque16, *opaque32; /* the converter's bitmaps */

void cps1_video_init(void)
{
    opaque8 = cps1.roms.opaque;                /* a bit for each half of each 8x8 pair: 24 KB */
    opaque16 = opaque8 + 0x600000 / 32 / 8;    /* a bit for each 16x16 tile: 6 KB */
    opaque32 = opaque16 + 0x600000 / 128 / 8;  /* a bit for each 32x32 tile: 1.5 KB */
    for (int b = 0; b < 16; b++) {
        int bright = 0x0f + (b << 1);
        for (int n = 0; n < 16; n++) {
            int v = n * 0x11 * bright / 0x2d;
            lut5[b][n] = (uint8_t)(v >> 3);
            lut6[b][n] = (uint8_t)(v >> 2);
        }
    }
    memset(pal_raw, 0, sizeof(pal_raw));
    memset(obj, 0, sizeof(obj));
    pal_dirty = 1;
    last_sprite = -1;
}

void cps1_set_layers(unsigned mask) { layer_mask = mask; }

void cps1_set_view(int w, int h, const uint16_t *cols, const uint16_t *rows)
{
    out_w = w; out_h = h; col_src = cols; row_src = rows;
    for (int s = 0, o = 0; s <= CPS1_SCREEN_W; s++) {
        while (o < w && cols[o] < s) o++;
        first_col[s] = (uint16_t)o;
    }
    for (int s = 0, o = 0; s <= CPS1_SCREEN_H; s++) {
        while (o < h && rows[o] < s) o++;
        first_row[s] = (uint16_t)o;
    }
    view_kind = VIEW_ANY;
    int five = 1, one = 1;
    for (int x = 0; x < w; x++) {
        if (cols[x] != x * 8 / 5) five = 0;
        if (cols[x] != cols[0] + x) one = 0;
    }
    if (five) view_kind = VIEW_FIVE_OF_EIGHT;
    else if (one) { view_kind = VIEW_ONE_TO_ONE; view_x0 = cols[0]; }
}

static const uint16_t *gfxram_at(unsigned reg, unsigned boundary)
{
    unsigned base = ((unsigned)cps1.a_regs[reg] << 8) & ~(boundary - 1) & 0x3ffff;
    if (base + boundary > CPS1_GFXRAM_BYTES) base = 0;
    return &cps1_gfxram[base >> 1];
}

/* The board copies the palette when the palette base register is written, page by page as
 * the palette control register allows. A page that is switched off keeps what it had; the
 * source still steps past it, unless nothing has been copied yet. */
void cps1_palette_latch(void)
{
    unsigned base = ((unsigned)cps1.a_regs[CPS_A_PALETTE_BASE] << 8) & ~0x3ffu & 0x3ffff;
    unsigned ctrl = cps1.b_regs[cps1.cfg.cpsb_pal_ctrl >> 1];
    unsigned src = base;
    int changed = 0;
    for (int page = 0; page < 6; page++) {
        if (ctrl & (1u << page)) {
            if (src + 0x400 <= CPS1_GFXRAM_BYTES && memcmp(&pal_raw[page * 0x200], &cps1_gfxram[src >> 1], 0x400) != 0) {
                memcpy(&pal_raw[page * 0x200], &cps1_gfxram[src >> 1], 0x400);
                changed = 1;
            }
            src += 0x400;
        } else if (src != base) {
            src += 0x400;
        }
    }
    /* the program does this every frame; only a palette that is different counts */
    if (changed) { pal_dirty = 1; pal_generation++; }
}

void cps1_objram_latch(void)
{
    memcpy(obj, gfxram_at(CPS_A_OBJ_BASE, 0x800), sizeof(obj));
}

static void palette_convert(void)
{
    for (int i = 0; i < 0xc00; i++) {
        unsigned p = pal_raw[i], b = p >> 12;
        unsigned c = ((unsigned)lut5[b][(p >> 8) & 15] << 11) | ((unsigned)lut6[b][(p >> 4) & 15] << 5) | lut5[b][p & 15];
        pal565[i] = (uint16_t)((c >> 8) | (c << 8));
    }
    pal_dirty = 0;
}

void cps1_frame_begin(void)
{
    unsigned lc = cps1.b_regs[cps1.cfg.cpsb_layer_ctrl >> 1];
    unsigned vc = cps1.a_regs[CPS_A_VIDEOCONTROL];

    layers[0].map = gfxram_at(CPS_A_SCROLL1_BASE, 0x4000);
    layers[1].map = gfxram_at(CPS_A_SCROLL2_BASE, 0x4000);
    layers[2].map = gfxram_at(CPS_A_SCROLL3_BASE, 0x4000);
    for (int i = 0; i < 3; i++) {
        layers[i].scrollx = cps1.a_regs[CPS_A_SCROLL1_X + 2 * i];
        layers[i].scrolly = cps1.a_regs[CPS_A_SCROLL1_Y + 2 * i];
    }
    layers[0].on = (lc & cps1.cfg.cpsb_layer_mask[0]) != 0;
    layers[1].on = (lc & cps1.cfg.cpsb_layer_mask[1]) != 0 && (vc & 0x04);
    layers[2].on = (lc & cps1.cfg.cpsb_layer_mask[2]) != 0 && (vc & 0x08);
    for (int i = 0; i < 4; i++) {
        order[i] = (uint8_t)((lc >> (6 + 2 * i)) & 3);
        prio_mask[i] = cps1.b_regs[cps1.cfg.cpsb_prio[i] >> 1] & 0x7fff;
    }
#if ROWSCROLL
    rowscroll = (vc & 0x01) ? gfxram_at(CPS_A_OTHER_BASE, 0x800) : NULL;
    rowscroll_offs = cps1.a_regs[CPS_A_ROWSCROLL_OFFS];
#else
    rowscroll = NULL;
#endif

    /* the list ends at the first entry whose attribute word begins FF */
    last_sprite = 0x400 / 4 - 1;
    for (int i = 0; i < 0x400 / 4; i++)
        if ((obj[i * 4 + 3] & 0xff00) == 0xff00) { last_sprite = i - 1; break; }

    if (pal_dirty) palette_convert();
}

/*
 * One row of one tile. tx is the screen column of the tile's left edge (it may be off
 * either side), words is how many groups of eight pixels wide the tile is, and row points
 * at that many 32-bit words, leftmost pixel in the low nibble of the first. pens has a
 * bit set for every pen that is to be drawn.
 */
/* are the view columns o .. oe-1 all covered? (oe - o is at most eight) */
static inline int covered(const uint32_t *cover, int o, int oe)
{
    if (o >= oe) return 1;
    unsigned w = (unsigned)o >> 5, lo = (unsigned)o & 31, n = (unsigned)(oe - o);
    uint32_t m = (n >= 32 ? 0xffffffffu : ((1u << n) - 1)) << lo;
    if ((cover[w] & m) != m) return 0;
    if (lo + n > 32) {
        uint32_t m2 = (1u << (lo + n - 32)) - 1;
        return (cover[w + 1] & m2) == m2;
    }
    return 1;
}

/* set bits o .. oe-1 (a tile is at most 32 wide, so two words at most) */
static inline void mark(uint32_t *cover, int o, int oe)
{
    if (o >= oe) return;
    unsigned w = (unsigned)o >> 5, lo = (unsigned)o & 31, n = (unsigned)(oe - o);
    cover[w] |= (n >= 32 ? 0xffffffffu : ((1u << n) - 1)) << lo;
    if (lo + n > 32) cover[w + 1] |= (1u << (lo + n - 32)) - 1;
    cover[MASK_WORDS] = 1;
}

static inline void draw_row(uint16_t *dst, int tx, int words, const uint32_t *row, int flipx,
                            const uint16_t *pal, unsigned pens, const uint32_t *cover)
{
    (void)cover;
    for (int g = 0; g < words; g++) {
        int gx = tx + g * 8;
        int a = gx < 0 ? 0 : gx, b = gx + 8 > CPS1_SCREEN_W ? CPS1_SCREEN_W : gx + 8;
        if (a >= b) continue;
        int o = first_col[a], oe = first_col[b];
#if OCCLUSION
#ifdef CPS1_PROFILE
        cps1_video_groups++;
        if (cover && covered(cover, o, oe)) { cps1_video_skipped++; continue; }
#else
        if (cover && covered(cover, o, oe)) continue;
#endif
#endif
        uint32_t w = row[flipx ? words - 1 - g : g];
        if (w == 0xffffffffu) continue;
        if (pens == 0x7fff && a == gx && b == gx + 8) {
            /* the whole group is on the screen and every pen but 15 is wanted */
            if (flipx) w = ((w >> 28) & 0xf) | ((w >> 20) & 0xf0) | ((w >> 12) & 0xf00) | ((w >> 4) & 0xf000)
                         | ((w << 4) & 0xf0000) | ((w << 12) & 0xf00000) | ((w << 20) & 0xf000000) | (w << 28);
            uint16_t *d = dst + o;
#define PIX(i, k) do { unsigned n_ = (w >> (4 * (k))) & 15; if (n_ != 15) d[i] = pal[n_]; } while (0)
            if (view_kind == VIEW_ONE_TO_ONE) {
                PIX(0, 0); PIX(1, 1); PIX(2, 2); PIX(3, 3); PIX(4, 4); PIX(5, 5); PIX(6, 6); PIX(7, 7);
                continue;
            }
            if (view_kind == VIEW_FIVE_OF_EIGHT) {
                /* view column x shows screen column x * 8 / 5: of eight screen columns from gx,
                 * the five shown depend on gx modulo 8 */
                switch (gx & 7) {
                case 0: PIX(0, 0); PIX(1, 1); PIX(2, 3); PIX(3, 4); PIX(4, 6); break;
                case 1: PIX(0, 0); PIX(1, 2); PIX(2, 3); PIX(3, 5); PIX(4, 7); break;
                case 2: PIX(0, 1); PIX(1, 2); PIX(2, 4); PIX(3, 6); PIX(4, 7); break;
                case 3: PIX(0, 0); PIX(1, 1); PIX(2, 3); PIX(3, 5); PIX(4, 6); break;
                case 4: PIX(0, 0); PIX(1, 2); PIX(2, 4); PIX(3, 5); PIX(4, 7); break;
                case 5: PIX(0, 1); PIX(1, 3); PIX(2, 4); PIX(3, 6); PIX(4, 7); break;
                case 6: PIX(0, 0); PIX(1, 2); PIX(2, 3); PIX(3, 5); PIX(4, 6); break;
                default: PIX(0, 1); PIX(1, 2); PIX(2, 4); PIX(3, 5); PIX(4, 7); break;
                }
                continue;
            }
#undef PIX
        }
        if (flipx) {
            for (; o < oe; o++) {
                unsigned n = (w >> ((7 - (col_src[o] - gx)) * 4)) & 15;
                if ((pens >> n) & 1) dst[o] = pal[n];
            }
        } else {
            for (; o < oe; o++) {
                unsigned n = (w >> ((col_src[o] - gx) * 4)) & 15;
                if ((pens >> n) & 1) dst[o] = pal[n];
            }
        }
    }
}

/* The STF29 PAL: which graphics ROMs answer for a tile code, by layer. Codes it does not
 * answer for read as all ones, which is a transparent tile. Returns the byte offset of
 * the tile, or -1. */
static inline int32_t sprite_offset(unsigned code)
{
    return code < 0x9000 ? (int32_t)(code * 128) : -1;
}
static inline int32_t tile_offset(int which, unsigned code)
{
    switch (which) {
    case 0:  return (code >= 0x4000 && code < 0x5000) ? (int32_t)((0x10000 + (code & 0x7fff)) * 64) : -1;
    case 1:  return (code >= 0x2800 && code < 0x4000) ? (int32_t)((0x8000 + (code & 0x3fff)) * 128) : -1;
    default: code &= 0x3fff;
             return (code >= 0x400 && code < 0x800) ? (int32_t)((0x2000 + (code & 0xfff)) * 512) : -1;
    }
}

typedef struct {
    const uint8_t *data;             /* the tile's first row */
    const uint16_t *pal;
    int16_t tx;
    uint8_t flip;                    /* bit 0 across, bit 1 down */
    uint8_t opaque;                  /* no transparent pixel anywhere in it */
    uint16_t pens;
} cell_t;

static inline int tile_opaque(int which, int32_t off, int odd_column)
{
    unsigned i;
    const uint8_t *m;
    switch (which) {
    case 0:  i = (unsigned)(off / 64) * 2 + (unsigned)odd_column; m = opaque8; break;
    case 1:  i = (unsigned)(off / 128); m = opaque16; break;
    default: i = (unsigned)(off / 512); m = opaque32; break;
    }
    return (m[i >> 3] >> (i & 7)) & 1;
}

#define MAX_CELLS (CPS1_SCREEN_W / 8 + 1)

/*
 * A tile map, rows y0 .. y0+rows-1 of the view. high = 0 draws the layer; high = 1 draws
 * only the pens its priority masks put above the sprites. cover is what the layers above
 * will cover, and is not drawn. With dst NULL nothing is drawn: instead what this layer's
 * opaque tiles cover is added to cover, for the layers below.
 */
static void draw_tilemap(int which, uint16_t *dst, int y0, int rows, int high, rowmask_t *cover)
{
    static const uint16_t pal_base[3] = { PAL_SCROLL1, PAL_SCROLL2, PAL_SCROLL3 };
    const layer_t *l = &layers[which];
    const int shift = 3 + which, size = 8 << which, words = 1 << which;
    const int stride = which == 2 ? 16 : 8;
    const int across = CPS1_SCREEN_W / size + 1;
    cell_t cells[MAX_CELLS];
    int ncells = 0, have_row = -1, have_x = -1;

    for (int r = 0; r < rows; r++, dst = dst ? dst + out_w : dst) {
        int sy = row_src[y0 + r] + VIS_Y0;
        int ty = (sy + l->scrolly) & (64 * size - 1);
        int trow = ty >> shift, line = ty & (size - 1);
        int x = VIS_X0 + l->scrollx;
        if (which == 1 && rowscroll) x += rowscroll[(sy + rowscroll_offs) & 0x3ff];
        x &= 64 * size - 1;

        VP_BEGIN;
        if (trow != have_row || x != have_x) {
            int col = x >> shift, tx = -(x & (size - 1));
            have_row = trow; have_x = x; ncells = 0;
            for (int i = 0; i < across; i++, tx += size) {
                int c = (col + i) & 63;
                unsigned idx;
                if (which == 0)      idx = (trow & 0x1f) + (c << 5) + ((trow & 0x20) << 6);
                else if (which == 1) idx = (trow & 0x0f) + (c << 4) + ((trow & 0x30) << 6);
                else                 idx = (trow & 0x07) + (c << 3) + ((trow & 0x38) << 6);
                unsigned attr = l->map[2 * idx + 1];
                unsigned pens = high ? prio_mask[(attr >> 7) & 3] : 0x7fff;
                if (!pens) continue;
                int32_t off = tile_offset(which, l->map[2 * idx]);
                if (off < 0) continue;
                cell_t *cell = &cells[ncells++];
                cell->opaque = (uint8_t)(!high && tile_opaque(which, off, c & 1));
                if (which == 0 && (c & 1)) off += 4;      /* odd columns use the right half of the pair */
#ifdef EXPERIMENT_RAM_TILES
                { static uint8_t fake[2048]; cell->data = fake + (off & 1023); }
#else
                cell->data = cps1.roms.gfx + off;
#endif
                cell->pal = &pal565[pal_base[which] + ((attr & 0x1f) << 4)];
                cell->tx = (int16_t)tx;
                cell->flip = (uint8_t)((attr >> 5) & 3);
                cell->pens = (uint16_t)pens;
            }
        }
        VP_MARK(2);
        if (!dst) {
            /* only saying what this row of the layer covers */
            for (int i = 0; i < ncells; i++) {
                const cell_t *cell = &cells[i];
                if (!cell->opaque) continue;
                int a = cell->tx < 0 ? 0 : cell->tx, b = cell->tx + size > CPS1_SCREEN_W ? CPS1_SCREEN_W : cell->tx + size;
                if (a < b) mark(cover[r], first_col[a], first_col[b]);
            }
            VP_END(1);
            continue;
        }
#ifdef CPS1_PROFILE
        cps1_video_cells += (uint64_t)ncells;
#endif
        const uint32_t *cv = cover[r][MASK_WORDS] ? cover[r] : NULL;    /* nothing over this row: no tests */
        for (int i = 0; i < ncells; i++) {
            const cell_t *cell = &cells[i];
            int ln = (cell->flip & 2) ? size - 1 - line : line;
            draw_row(dst, cell->tx, words, (const uint32_t *)(cell->data + ln * stride), cell->flip & 1, cell->pal, cell->pens, cv);
        }
        VP_END(high ? 7 : 3 + which);
    }
}

static void draw_block(uint16_t *dst, int y0, int rows, int sx, int sy, const uint8_t *data,
                       const uint16_t *pal, int flipx, int flipy, rowmask_t *cover)
{
    int vx = sx - VIS_X0, vy = sy - VIS_Y0;
    if (vx <= -16 || vx >= CPS1_SCREEN_W || vy <= -16 || vy >= CPS1_SCREEN_H) return;
    int a = vy < 0 ? 0 : vy, b = vy + 16 > CPS1_SCREEN_H ? CPS1_SCREEN_H : vy + 16;
    int o = first_row[a], oe = first_row[b];
    if (o < y0) o = y0;
    if (oe > y0 + rows) oe = y0 + rows;
    for (; o < oe; o++) {
        int line = row_src[o] - vy;
        if (flipy) line = 15 - line;
        draw_row(dst + (o - y0) * out_w, vx, 2, (const uint32_t *)(data + line * 8), flipx, pal, 0x7fff, cover[o - y0][MASK_WORDS] ? cover[o - y0] : NULL);
    }
}

/* Sprites. The first in the list is on top, so they are painted last to first. */
static void draw_sprites(uint16_t *dst, int y0, int rows, rowmask_t *cover)
{
    /* the band of the raster this strip shows */
    int band0 = row_src[y0] + VIS_Y0, band1 = row_src[y0 + rows - 1] + VIS_Y0;

    for (int i = last_sprite; i >= 0; i--) {
        const uint16_t *s = &obj[i * 4];
        unsigned attr = s[3];
        int x = s[0], y = s[1] & 0x1ff;
        int nx = ((attr >> 8) & 15) + 1, ny = ((attr >> 12) & 15) + 1;
        /* a sprite that does not wrap round the bottom of the raster is either on this
         * band or it is not */
        if (y + ny * 16 <= 512 && (y > band1 || y + ny * 16 <= band0)) continue;
        unsigned code = s[2];
        if (sprite_offset(code) < 0) continue;
        const uint16_t *pal = &pal565[PAL_SPRITES + ((attr & 0x1f) << 4)];
        int flipx = (attr >> 5) & 1, flipy = (attr >> 6) & 1;
        for (int by = 0; by < ny; by++) {
            int sy = (y + by * 16) & 0x1ff;
            if (sy > band1 || sy + 16 <= band0) continue;
            int cy = flipy ? ny - 1 - by : by;
            for (int bx = 0; bx < nx; bx++) {
                int sx = (x + bx * 16) & 0x1ff;
                int cx = flipx ? nx - 1 - bx : bx;
                unsigned c = (code & ~0xfu) + ((code + cx) & 0xf) + 0x10 * cy;
                int32_t off = sprite_offset(c);
                if (off < 0) continue;
#ifdef EXPERIMENT_RAM_TILES
                { static uint8_t fake[2048]; draw_block(dst, y0, rows, sx, sy, fake + (off & 1023), pal, flipx, flipy, cover); }
#else
                draw_block(dst, y0, rows, sx, sy, cps1.roms.gfx + off, pal, flipx, flipy, cover);
#endif
            }
        }
    }
}

void cps1_render(uint16_t *dst, int y0, int rows)
{
    const unsigned mask = layer_mask & LAYERS;
    uint16_t bg = pal565[BACKGROUND_PEN];
    VP_BEGIN;
    for (int i = 0; i < rows * out_w; i++) dst[i] = bg;
    VP_END(0);

#if OCCLUSION
    /* from the top down: what each layer will have over it */
    memset(cover_above, 0, sizeof(cover_above));
    for (int k = 3; k > 0; k--) {
        int l = order[k];
        memcpy(cover_above[k - 1], cover_above[k], sizeof(cover_above[k]));
        if (l != 0 && layers[l - 1].on && (mask & (1u << (l - 1))))
            draw_tilemap(l - 1, NULL, y0, rows, 0, cover_above[k - 1]);
    }
#endif

    for (int k = 0; k < 4; k++) {
        int l = order[k];
        if (l == 0) {
            if (mask & LAYER_SPRITES) { VP_BEGIN; draw_sprites(dst, y0, rows, cover_above[k]); VP_END(6); }
            if (k > 0 && order[k - 1] != 0) {
                int under = order[k - 1] - 1;
                if (layers[under].on && (mask & (1u << under))) draw_tilemap(under, dst, y0, rows, 1, cover_above[k]);
            }
        } else if (layers[l - 1].on && (mask & (1u << (l - 1)))) {
            draw_tilemap(l - 1, dst, y0, rows, 0, cover_above[k]);
        }
    }
}

/* ---- what a strip depends on ---- */

static inline uint64_t mix(uint64_t h, uint32_t v) { return (h ^ v) * 0x100000001b3ull; }

uint64_t cps1_strip_signature(int y0, int rows)
{
    uint64_t h = 0xcbf29ce484222325ull;
    const unsigned mask = layer_mask & LAYERS;
    h = mix(h, pal_generation);
    h = mix(h, cps1.b_regs[cps1.cfg.cpsb_layer_ctrl >> 1]);
    h = mix(h, cps1.a_regs[CPS_A_VIDEOCONTROL]);
    for (int i = 0; i < 4; i++) h = mix(h, prio_mask[i]);
    int band0 = row_src[y0] + VIS_Y0, band1 = row_src[y0 + rows - 1] + VIS_Y0;
    for (int i = 0; i < 3; i++) {
        if (!layers[i].on || !(mask & (1u << i))) { h = mix(h, 0xffffffffu); continue; }
        h = mix(h, (uint32_t)(layers[i].map - cps1_gfxram));
        h = mix(h, cps1_gfx_generation[(unsigned)(layers[i].map - cps1_gfxram) >> 13]);
        h = mix(h, (uint32_t)(layers[i].scrollx | (layers[i].scrolly << 16)));
        if (i == 1 && rowscroll)
            for (int sy = band0; sy <= band1; sy++) h = mix(h, rowscroll[(sy + rowscroll_offs) & 0x3ff]);
    }
    if (mask & LAYER_SPRITES) {
        for (int i = last_sprite; i >= 0; i--) {
            const uint16_t *s = &obj[i * 4];
            unsigned attr = s[3];
            int y = s[1] & 0x1ff, ny = ((attr >> 12) & 15) + 1;
            /* the same test draw_sprites() makes; anything it would consider is in the hash */
            if (y + ny * 16 <= 512 && (y > band1 || y + ny * 16 <= band0)) continue;
            h = mix(h, (uint32_t)(s[0] | ((uint32_t)s[1] << 16)));
            h = mix(h, (uint32_t)(s[2] | ((uint32_t)s[3] << 16)));
            h = mix(h, (uint32_t)i);
        }
    }
    return h;
}
