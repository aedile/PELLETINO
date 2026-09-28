/*
 * display_toast.cpp - a short message drawn over whatever is on the panel.
 *
 * Every image on this medal - the launcher and each game - pushes its pixels
 * through display_write() or display_write_preswapped(), whatever its renderer
 * looks like and whichever task it runs on. So the toast is drawn here, into
 * the rows as they go past, and no renderer has to know about it.
 *
 * It draws into the caller's buffer. Renderers rebuild their row buffers every
 * frame, so the toast disappears by itself when it expires; an image that pushes
 * straight from a frame buffer it keeps would have to redraw those rows.
 */
#include "display.h"
#include "toast_font.h"
#include "esp_timer.h"
#include <string.h>

#define TOAST_SCALE   2
#define TOAST_PAD_X   12
#define TOAST_H       32
#define TOAST_Y       ((DISPLAY_HEIGHT - TOAST_H) / 2)
#define TOAST_MAX     13                       /* characters that fit the panel */

static char             text[TOAST_MAX + 1];
static volatile int64_t until_us;
static uint16_t         win_x, win_y, win_w;
static uint32_t         win_pos;               /* pixels already written into this window */

void display_toast(const char *s, uint32_t ms)
{
    until_us = 0;                              /* nothing reads the text while it changes */
    strncpy(text, s ? s : "", TOAST_MAX);
    text[TOAST_MAX] = 0;
    until_us = esp_timer_get_time() + (int64_t)ms * 1000;
}

bool display_toast_active(void)
{
    if (until_us && esp_timer_get_time() > until_us) until_us = 0;
    return until_us != 0;
}

void display_toast_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    (void)h;
    win_x = x; win_y = y; win_w = w; win_pos = 0;
}

static inline uint16_t colour(uint8_t r, uint8_t g, uint8_t b, bool swapped)
{
    uint16_t c = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
    return swapped ? (uint16_t)((c >> 8) | (c << 8)) : c;
}

void display_toast_overlay(uint16_t *data, uint32_t len, bool swapped)
{
    uint32_t pos = win_pos;
    win_pos += len;
    if (!win_w || !display_toast_active()) return;

    int chars = (int)strlen(text);
    int box_w = chars * 8 * TOAST_SCALE + 2 * TOAST_PAD_X;
    int box_x = (DISPLAY_WIDTH - box_w) / 2;
    int text_x = box_x + TOAST_PAD_X, text_y = TOAST_Y + (TOAST_H - 8 * TOAST_SCALE) / 2;

    uint16_t ink = colour(255, 255, 255, swapped), paper = colour(12, 12, 40, swapped),
             edge = colour(255, 200, 60, swapped);

    int first = win_y + (int)(pos / win_w), last = win_y + (int)((pos + len - 1) / win_w);
    if (last < TOAST_Y || first >= TOAST_Y + TOAST_H) return;

    for (int y = (first > TOAST_Y ? first : TOAST_Y); y <= last && y < TOAST_Y + TOAST_H; y++) {
        for (int x = box_x; x < box_x + box_w; x++) {
            if (x < win_x || x >= win_x + win_w) continue;
            int64_t i = (int64_t)(y - win_y) * win_w + (x - win_x) - (int64_t)pos;
            if (i < 0 || i >= (int64_t)len) continue;

            uint16_t c = paper;
            if (y < TOAST_Y + 2 || y >= TOAST_Y + TOAST_H - 2 || x < box_x + 2 || x >= box_x + box_w - 2) {
                c = edge;
            } else {
                int gx = (x - text_x) / TOAST_SCALE, gy = (y - text_y) / TOAST_SCALE;
                if (x >= text_x && y >= text_y && gy < 8 && gx < chars * 8) {
                    char ch = text[gx / 8];
                    if (ch >= 32 && ch <= 126 && (toast_font[ch - 32][gy] & (0x80 >> (gx % 8)))) c = ink;
                }
            }
            data[i] = c;
        }
    }
}
