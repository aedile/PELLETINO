/*
 * menu.cpp - the wheel.
 *
 * Five places on a curve down the right of the panel: the chosen game's logo
 * large in the middle, its neighbours smaller and dimmer above and below, the
 * pair beyond them smaller still and mostly off the edge. A step turns the
 * wheel one place, quickly at first and settling. Behind it is a screenshot of
 * the chosen game, dimmed, and the whole panel is scanlined on its way out.
 *
 * All of the artwork is optional and none of it ships (tools/fetch_art.py). A
 * game without a logo is its title in text; one without a screenshot sits over
 * the attract screen's stars and grid.
 *
 * Drawn from scratch every frame into the frame buffer in components/fest.
 */
#include "menu.h"
#include "games.h"
#include "fest.h"
#include "mqart.h"
#include "input.h"
#include "battery.h"
#include "sound.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

static const char *TAG = "menu";

#define HEADER_H   18
#define FOOTER_H   20
#define CENTRE_Y  140
#define STEP      256           /* one place on the wheel, in the units s_turn counts */
#define MIDDLE    (2 * STEP)    /* the place in the middle */

/* Where a logo rests: centre, shift right of the panel's centre line, and the
 * box it is fitted to. tools/pack_art.py scales to the same three boxes. */
static const struct { int16_t cy, dx, bw, bh; } PLACE[5] = {
    {  -8, 58,  96, 40 },
    {  62, 26, 140, 58 },
    { 140,  0, 216, 96 },
    { 218, 26, 140, 58 },
    { 288, 58,  96, 40 },
};

static int         s_sel;
static int         s_turn;       /* how far the wheel still has to travel; 0 at rest */
static int         s_frame;
static int         s_colours_of; /* whose screenshot colours are loaded, -1 for nobody's */
static menu_mode_t s_mode;
static const char *s_msg1, *s_msg2;

void menu_release(void)
{
    fest_crt = false;
    fest_free();
}

void menu_init(void)
{
    if (!fest_init()) ESP_LOGE(TAG, "out of memory for the frame buffer");
    fest_crt = true;
    s_colours_of = -1;
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
    if (i >= 0) { s_sel = i; s_turn = 0; }
}

static int wrap(int i)
{
    int n = mqart_count();
    return n > 0 ? ((i % n) + n) % n : 0;
}

void menu_nav(int delta)
{
    if (mqart_count() <= 0) return;
    s_sel = wrap(s_sel + delta);
    /* taps faster than the wheel turns pile up, but only so far: past two
     * places it would be drawing logos that are nowhere near the panel */
    s_turn += delta * STEP;
    if (s_turn >  2 * STEP) s_turn =  2 * STEP;
    if (s_turn < -2 * STEP) s_turn = -2 * STEP;
}

void menu_show_message(const char *l1, const char *l2)
{
    s_msg1 = l1; s_msg2 = l2;
    s_mode = MENU_MESSAGE;
}

/* ------------------------------------------------------------------------ */

static void text_centred(int cx, int y, const char *s, uint8_t colour, int scale)
{
    fest_text_scaled(cx - 4 * scale * (int)strlen(s), y, s, colour, scale);
}

static void backdrop(int index)
{
    const mqart_entry_t *e = mqart_get(index);
    const uint8_t *rgb = mqart_snap_colours(e);
    if (!rgb || e->snap.w != FB_W || e->snap.h != FB_H) {
        fest_clear(UI_BLACK);
        fest_stars(s_frame);
        fest_grid(s_frame, 150);
        return;
    }
    if (s_colours_of != index) {
        for (int k = 0; k < MQART_SNAP_COLOURS; k++, rgb += 3)
            fest_colour((uint8_t)(MQART_SNAP_BASE + k), rgb[0], rgb[1], rgb[2]);
        s_colours_of = index;
    }
    for (int y = 0; y < FB_H; y++)
        if (!mqart_row(&e->snap, y, fest_fb + (size_t)y * FB_W))
            memset(fest_fb + (size_t)y * FB_W, UI_BLACK, FB_W);
}

/* One game, `at` places down the wheel (in STEPs; MIDDLE is the chosen one). */
static void draw_game(const mqart_entry_t *e, int at)
{
    if (!e || at < 0 || at > 4 * STEP) return;
    int i = at / STEP, f = at % STEP;
    if (i == 4) { i = 3; f = STEP; }
    int cy = PLACE[i].cy + (PLACE[i + 1].cy - PLACE[i].cy) * f / STEP;
    int cx = FB_W / 2 + PLACE[i].dx + (PLACE[i + 1].dx - PLACE[i].dx) * f / STEP;
    int bw = PLACE[i].bw + (PLACE[i + 1].bw - PLACE[i].bw) * f / STEP;
    int bh = PLACE[i].bh + (PLACE[i + 1].bh - PLACE[i].bh) * f / STEP;

    /* the copy that was made for the nearest resting place */
    int far = (abs(at - MIDDLE) + STEP / 2) / STEP;
    if (far > MQART_LOGOS - 1) far = MQART_LOGOS - 1;
    const mqart_img_t *im = &e->logo[far];

    if (!im->w) {
        static const uint8_t shade[MQART_LOGOS] = { UI_WHITE, UI_GREY, CUBE(1,1,1) };
        char name[sizeof e->title];
        size_t n = 0;
        for (; e->title[n]; n++) name[n] = (char)toupper((unsigned char)e->title[n]);
        name[n] = 0;
        int scale = (far == 0 && 16 * (int)n <= PLACE[2].bw) ? 2 : 1;
        text_centred(cx, cy - 4 * scale, name, shade[far], scale);
        return;
    }

    int w, h;                                   /* fitted to the box, shape kept */
    if ((int)im->w * bh <= (int)im->h * bw) { h = bh; w = im->w * bh / im->h; }
    else                                    { w = bw; h = im->h * bw / im->w; }
    if (f == 0 || (abs(w - im->w) <= 1 && abs(h - im->h) <= 1)) { w = im->w; h = im->h; }   /* at rest: as stored */
    if (w < 1 || h < 1) return;

    static uint8_t row[MQART_MAX_W];
    int have = -1;
    int x0 = cx - w / 2, y0 = cy - h / 2;
    for (int dy = 0; dy < h; dy++) {
        int y = y0 + dy;
        if (y < HEADER_H || y >= FB_H - FOOTER_H) continue;
        int sy = dy * im->h / h;
        if (sy != have) {
            have = mqart_row(im, sy, row) ? sy : -1;
            if (have < 0) continue;
        }
        uint8_t *dst = fest_fb + (size_t)y * FB_W;
        for (int dx = 0; dx < w; dx++) {
            int x = x0 + dx;
            if (x < 0 || x >= FB_W) continue;
            uint8_t c = row[dx * im->w / w];
            if (c != MQART_CLEAR) dst[x] = c;
        }
    }
}

static void wheel(void)
{
    /* far ones first, so the chosen game is drawn over its neighbours */
    int lo = -4, hi = 4;
    while (lo <= hi) {
        int at_lo = MIDDLE + lo * STEP + s_turn, at_hi = MIDDLE + hi * STEP + s_turn;
        if (abs(at_lo - MIDDLE) >= abs(at_hi - MIDDLE)) { draw_game(mqart_get(wrap(s_sel + lo)), at_lo); lo++; }
        else                                            { draw_game(mqart_get(wrap(s_sel + hi)), at_hi); hi--; }
    }
}

/* a pointer either side of the chosen game, nudging inward and back */
static void pointers(void)
{
    static const int8_t nudge[8] = { 0, 1, 2, 3, 3, 2, 1, 0 };
    int in = nudge[(s_frame / 3) % 8];
    for (int i = 0; i < 6; i++) {
        fest_fill(3 + in + i,          CENTRE_Y - 6 + i, 1, 12 - 2 * i, UI_YELLOW);
        fest_fill(FB_W - 4 - in - i,   CENTRE_Y - 6 + i, 1, 12 - 2 * i, UI_YELLOW);
    }
}

static void header(void)
{
    fest_fill(0, 0, FB_W, HEADER_H, UI_BLACK);
    if (sound_muted()) fest_text(8, 5, "MUTED", UI_RED);
    else               fest_text(8, 5, "PELLETINO", CUBE(3,3,4));

    int pct = battery_percent();
    uint8_t c = pct <= BATT_CRIT_PCT ? UI_RED : pct <= BATT_LOW_PCT ? UI_YELLOW : UI_GREEN;
    const int x = 204, y = 4, w = 26, h = 11;
    fest_frame(x, y, w, h, CUBE(3,3,4));
    fest_fill(x + w, y + 3, 2, h - 6, CUBE(3,3,4));          /* the nub */
    int fill = (w - 4) * pct / 100;
    if (fill > 0) fest_fill(x + 2, y + 2, fill, h - 4, c);
    char s[16];
    snprintf(s, sizeof s, "%d", pct);
    fest_text(x - 8 * (int)strlen(s) - 5, 5, s, UI_GREY);
}

static void footer(const mqart_entry_t *e, bool installed)
{
    int top = FB_H - FOOTER_H;
    fest_fill(0, top, FB_W, FOOTER_H, UI_BLACK);
    if (s_mode == MENU_LAUNCHING) {
        text_centred(FB_W / 2, top + 7, "LAUNCHING", UI_GREEN, 1);
        return;
    }
    const char *act = installed ? "HOLD TO PLAY" : "NOT INSTALLED";
    int act_x = FB_W - 8 - 8 * (int)strlen(act);
    fest_text(act_x, top + 7, act, installed ? UI_YELLOW : UI_RED);

    char by[sizeof e->by];                      /* whatever of it fits beside that */
    int room = (act_x - 8 - 8) / 8;
    snprintf(by, sizeof by, "%.*s", room > 0 ? room : 0, e->by);
    fest_text(8, top + 7, by, UI_WHITE);

    /* Fill a bar while the button is held so the hold has a visible length -
     * otherwise nobody knows how long "a few seconds" is. */
    int held = input_hold_ms();
    if (installed && held > 0) {
        int w = held * FB_W / INPUT_SELECT_HOLD_MS;
        fest_fill(0, top, FB_W, 3, CUBE(1,1,1));
        fest_fill(0, top, w > FB_W ? FB_W : w, 3, UI_YELLOW);
    }
}

void menu_render(void)
{
    if (!fest_fb) return;
    s_frame++;

    if (s_mode == MENU_MESSAGE) {
        fest_clear(UI_BLACK);
        const char *l1 = s_msg1 ? s_msg1 : "";
        text_centred(FB_W / 2, 120, l1, UI_WHITE, 16 * (int)strlen(l1) <= FB_W - 16 ? 2 : 1);
        if (s_msg2) text_centred(FB_W / 2, 156, s_msg2, UI_GREY, 1);
        fest_present();
        return;
    }

    const mqart_entry_t *e = mqart_get(s_sel);
    if (!e) return;
    bool installed = e->boot[0] == '@' || game_installed(e->boot);   /* '@' = built into the launcher */

    /* the screenshot changes over half way through the step */
    int behind = s_sel;
    if (s_turn >=  STEP / 2) behind = wrap(s_sel - 1);
    if (s_turn <= -STEP / 2) behind = wrap(s_sel + 1);
    backdrop(behind);
    wheel();
    if (s_turn == 0 && s_mode == MENU_BROWSE) pointers();
    header();
    footer(e, installed || s_mode == MENU_LAUNCHING);
    fest_present();

    /* five eighths of what is left, each frame: away fast, arriving gently */
    s_turn = s_turn * 5 / 8;
    if (abs(s_turn) < 6) s_turn = 0;
}
