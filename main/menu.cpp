/*
 * menu.cpp - the browser.
 *
 * Drawn in horizontal bands (see gfx.h): clear a band, draw whatever intersects
 * it, push it over SPI, move down. Two buffers are the entire RAM cost - one band
 * and one scratch big enough for the marquee rows that land in a band.
 */
#include "menu.h"
#include "games.h"
#include "gfx.h"
#include "mqart.h"
#include "input.h"
#include "battery.h"
#include "display.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "menu";

/* Layout, 240 x 280 portrait */
#define BOX_X       16
#define BOX_Y       40
#define HEADER_Y    12
#define TITLE_Y     158
#define STATUS_Y    194
#define DOTS_Y      222
#define HOLD_Y      208     /* progress bar while the button is held */
#define HOLD_W      140
#define HINT_Y      252

/*
 * How many rows are composed before anything is pushed. Ideally the whole screen: drawing a
 * strip, pushing it, waiting for the SPI to drain and only then starting the next one leaves
 * the panel showing new content above the seam and old below it while that walks down, which
 * is exactly the "bands" you see on every scroll step. Composing the lot and pushing once
 * makes the change arrive in a single sweep.
 *
 * A full screen is 134 KB of DMA-capable RAM. The launcher runs no emulator so it is there,
 * but if it ever is not this falls back to strips and still works - just visibly.
 */
static int       s_band_h;
static uint16_t *s_band;                  /* GFX_W * s_band_h */
static uint16_t *s_rows;                  /* MQART_BOX_W * min(s_band_h, MQART_BOX_H) */
static int s_sel;
static menu_mode_t s_mode;
static const char *s_msg1, *s_msg2;

static uint16_t C_BG, C_TITLE, C_DIM, C_OK, C_ABSENT, C_DOT, C_DOT_OFF, C_HEAD, C_BAR, C_BAR_BG;

/* battery meter, top right of the header */
#define BATT_X 202
#define BATT_Y   7
#define BATT_W  26
#define BATT_H  12

static void draw_battery(gfx_band_t *b)
{
    int pct = battery_percent();
    uint16_t c = pct <= BATT_CRIT_PCT ? C_ABSENT : pct <= BATT_LOW_PCT ? C_BAR : C_OK;
    gfx_fill_rect(b, BATT_X, BATT_Y, BATT_W, 1, C_HEAD);
    gfx_fill_rect(b, BATT_X, BATT_Y + BATT_H - 1, BATT_W, 1, C_HEAD);
    gfx_fill_rect(b, BATT_X, BATT_Y, 1, BATT_H, C_HEAD);
    gfx_fill_rect(b, BATT_X + BATT_W - 1, BATT_Y, 1, BATT_H, C_HEAD);
    gfx_fill_rect(b, BATT_X + BATT_W, BATT_Y + 4, 2, BATT_H - 8, C_HEAD);   /* the nub */
    int fill = (BATT_W - 4) * pct / 100;
    if (fill > 0) gfx_fill_rect(b, BATT_X + 2, BATT_Y + 2, fill, BATT_H - 4, c);
    char s[16];
    snprintf(s, sizeof s, "%d", pct);
    gfx_text(b, BATT_X - 8 * (int)strlen(s) - 6, BATT_Y + 1, s, 1, C_DIM);
}


/* Quarter brightness, in the big-endian order the band uses. */
static inline uint16_t dim_be(uint16_t be)
{
    uint16_t c = (uint16_t)((be >> 8) | (be << 8));
    uint16_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    c = (uint16_t)(((r >> 2) << 11) | ((g >> 2) << 5) | (b >> 2));
    return (uint16_t)((c >> 8) | (c << 8));
}

void menu_init(void)
{
    if (s_band) return;                             /* called from a few places now */
    s_band_h = GFX_H;
    s_band = (uint16_t *)heap_caps_malloc((size_t)GFX_W * s_band_h * 2, MALLOC_CAP_DMA);
    if (!s_band) {                                  /* no room for the whole screen */
        s_band_h = GFX_BAND_H;
        s_band = (uint16_t *)heap_caps_malloc((size_t)GFX_W * s_band_h * 2, MALLOC_CAP_DMA);
    }
    int rows_h = s_band_h < MQART_BOX_H ? s_band_h : MQART_BOX_H;
    s_rows = (uint16_t *)heap_caps_malloc((size_t)MQART_BOX_W * rows_h * 2, MALLOC_CAP_8BIT);
    if (!s_band || !s_rows) ESP_LOGE(TAG, "out of memory for the frame buffer");
    else ESP_LOGI(TAG, "composing %d rows at a time (%s), %u bytes; free heap %u",
                  s_band_h, s_band_h == GFX_H ? "whole screen, one push" : "strips",
                  (unsigned)((size_t)GFX_W * s_band_h * 2 + (size_t)MQART_BOX_W * rows_h * 2),
                  (unsigned)esp_get_free_heap_size());

    C_BG      = gfx_rgb(  6,   6,   9);
    C_HEAD    = gfx_rgb( 70,  70,  90);
    C_TITLE   = gfx_rgb(255, 255, 255);
    C_DIM     = gfx_rgb(110, 110, 125);
    C_OK      = gfx_rgb( 90, 210, 120);
    C_ABSENT  = gfx_rgb(150,  90,  70);
    C_DOT     = gfx_rgb(235, 235, 245);
    C_DOT_OFF = gfx_rgb( 48,  48,  60);
    C_BAR     = gfx_rgb(255, 200,  60);
    C_BAR_BG  = gfx_rgb( 40,  40,  50);
    s_sel  = 0;
    s_mode = MENU_BROWSE;
}

const char *menu_current_rom(void)
{
    const mqart_entry_t *e = mqart_get(s_sel);
    return e ? e->rom : NULL;
}

const char *menu_current_title(void)
{
    const mqart_entry_t *e = mqart_get(s_sel);
    return e ? e->title : NULL;
}

void menu_set_mode(menu_mode_t m) { s_mode = m; }

void menu_select_rom(const char *rom)
{
    int i = rom ? mqart_find(rom) : -1;
    if (i >= 0) s_sel = i;
}

void menu_nav(int delta)
{
    int n = mqart_count();
    if (n <= 0) return;
    s_sel = (s_sel + delta % n + n) % n;
}

void menu_show_message(const char *l1, const char *l2)
{
    s_msg1 = l1; s_msg2 = l2;
    s_mode = MENU_MESSAGE;
}

/*
 * The longest titles do not fit across the panel at double size - "Missile Command" comes to
 * 238 of the 240 columns and touches both edges - so a title that would run out to the sides
 * is drawn at single size instead. Only one of the fourteen needs it.
 */
static int title_scale(const char *s)
{
    return gfx_text_width(s, 2) <= GFX_W - 16 ? 2 : 1;
}

static void draw_marquee_into(gfx_band_t *b, const mqart_entry_t *e, bool installed)
{
    int mx = BOX_X + (MQART_BOX_W - e->w) / 2;
    int my = BOX_Y + (MQART_BOX_H - e->h) / 2;

    int top = my > b->y0 ? my : b->y0;                       /* first screen row to draw */
    int bot = (my + e->h) < (b->y0 + b->h) ? (my + e->h) : (b->y0 + b->h);
    if (top >= bot) return;

    int nrows = bot - top;
    if (mqart_read_rows(e, top - my, nrows, s_rows) != ESP_OK) return;

    for (int r = 0; r < nrows; r++) {
        uint16_t *src = s_rows + (size_t)r * e->w;
        uint16_t *dst = b->px + (size_t)(top - b->y0 + r) * GFX_W + mx;
        if (installed) {
            memcpy(dst, src, (size_t)e->w * 2);              /* already byte-swapped */
        } else {
            for (int x = 0; x < e->w; x++) dst[x] = dim_be(src[x]);
        }
    }
}

/*
 * The hold bar is the only thing that animates, and it is five pixels tall. Repainting the
 * whole screen for it - seven bands, and the marquee re-read from flash for three of them -
 * is what made the panel visibly wipe in strips twenty times a second while the button was
 * held. Redrawing just the rows that changed is one band and about two milliseconds.
 */
void menu_render(void) { menu_render_range(0, GFX_H); }

/* microseconds spent in the last full repaint, for the performance log */
static uint32_t s_last_render_us;
uint32_t menu_last_render_us(void) { return s_last_render_us; }
int      menu_band_rows(void) { return s_band_h; }

void menu_render_range(int ry0, int ry1)
{
    if (!s_band) return;
    int64_t t0 = esp_timer_get_time();
    const mqart_entry_t *e = mqart_get(s_sel);
    bool installed = e && game_installed(e->boot);
    int  n = mqart_count();

    char pos[32];
    snprintf(pos, sizeof pos, "%d/%d", s_sel + 1, n > 0 ? n : 0);

    for (int y0 = 0; y0 < GFX_H; y0 += s_band_h) {
        gfx_band_t band;
        band.px = s_band;
        band.y0 = y0;
        band.h  = (GFX_H - y0) < s_band_h ? (GFX_H - y0) : s_band_h;
        if (y0 + band.h <= ry0 || y0 >= ry1) continue;      /* nothing here changed */
        gfx_clear(&band, C_BG);

        if (s_mode == MENU_MESSAGE) {
            gfx_text_center(&band, GFX_W / 2, 120, s_msg1 ? s_msg1 : "", 2, C_TITLE);
            if (s_msg2) gfx_text_center(&band, GFX_W / 2, 156, s_msg2, 1, C_DIM);
        } else if (e) {
            gfx_text_center(&band, GFX_W / 2, HEADER_Y, "PELLETINO", 1, C_HEAD);
            draw_battery(&band);
            draw_marquee_into(&band, e, installed || s_mode == MENU_LAUNCHING);
            int ts = title_scale(e->title);
            /* keep the baseline where it is when the title drops a size */
            gfx_text_center(&band, GFX_W / 2, TITLE_Y + (2 - ts) * GFX_GLYPH_H / 2, e->title, ts,
                            (installed || s_mode == MENU_LAUNCHING) ? C_TITLE : C_DIM);

            if (s_mode == MENU_LAUNCHING) {
                gfx_text_center(&band, GFX_W / 2, STATUS_Y, "LAUNCHING", 1, C_OK);
            } else {
                gfx_text_center(&band, GFX_W / 2, STATUS_Y,
                                installed ? "HOLD TO PLAY" : "NOT INSTALLED",
                                1, installed ? C_OK : C_ABSENT);

                /* Fill a bar while the button is held so the hold has a visible
                 * length - otherwise nobody knows how long "a few seconds" is. */
                int held = input_hold_ms();
                if (installed && held > 0) {
                    int w = held * HOLD_W / INPUT_SELECT_HOLD_MS;
                    if (w > HOLD_W) w = HOLD_W;
                    gfx_fill_rect(&band, (GFX_W - HOLD_W) / 2, HOLD_Y, HOLD_W, 5, C_BAR_BG);
                    if (w > 0) gfx_fill_rect(&band, (GFX_W - HOLD_W) / 2, HOLD_Y, w, 5, C_BAR);
                }

                int span  = n * 10 - 4;
                int start = (GFX_W - span) / 2;
                for (int i = 0; i < n; i++) {
                    int cx = start + i * 10;
                    if (i == s_sel) gfx_fill_rect(&band, cx - 1, DOTS_Y - 1, 6, 6, C_DOT);
                    else            gfx_fill_rect(&band, cx,     DOTS_Y,     4, 4, C_DOT_OFF);
                }
                gfx_text_center(&band, GFX_W / 2, HINT_Y, pos, 1, C_HEAD);
            }
        }

        display_set_window(0, (uint16_t)y0, GFX_W, (uint16_t)band.h);
        display_write_preswapped(band.px, (uint32_t)GFX_W * band.h);
        display_wait_done();
    }
    s_last_render_us = (uint32_t)(esp_timer_get_time() - t0);
}
