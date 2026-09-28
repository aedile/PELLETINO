#include "fest.h"
#include "font8x8.h"
#include "display.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>

uint8_t *fest_fb;
uint16_t fest_pal[256];
static uint16_t pal_dim[256];                     /* the same colours on a scanline */
bool fest_crt;

#define STRIP 20                                  /* rows converted per SPI push */
static uint16_t *strip;                           /* FB_W * STRIP, DMA-capable */

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));       /* pre-swapped for the panel */
}

void fest_colour(uint8_t i, uint8_t r, uint8_t g, uint8_t b)
{
    fest_pal[i] = rgb(r, g, b);
    pal_dim[i]  = rgb((uint8_t)(r * 3 / 4), (uint8_t)(g * 3 / 4), (uint8_t)(b * 3 / 4));
}

bool fest_init(void)
{
    if (fest_fb) return true;
    fest_fb = calloc(FB_W * FB_H, 1);
    strip = heap_caps_malloc(FB_W * STRIP * 2, MALLOC_CAP_DMA);
    if (!fest_fb || !strip) { fest_free(); return false; }

    for (int i = 0; i < 180; i++)                 /* the 6x6x5 cube */
        fest_colour((uint8_t)i, (i / 30) * 51, ((i / 5) % 6) * 51, (i % 5) * 63);
    fest_colour(UI_BLACK, 0, 0, 0);
    fest_colour(UI_WHITE, 255, 255, 255);
    fest_colour(UI_GREY, 128, 128, 128);
    fest_colour(UI_YELLOW, 255, 220, 0);
    fest_colour(UI_GREEN, 40, 220, 40);
    fest_colour(UI_RED, 240, 40, 40);
    fest_colour(UI_BLUE, 40, 80, 220);
    return true;
}

void fest_free(void)
{
    free(fest_fb); fest_fb = NULL;
    heap_caps_free(strip); strip = NULL;
}

void fest_clear(uint8_t colour) { if (fest_fb) memset(fest_fb, colour, FB_W * FB_H); }

void fest_px(int x, int y, uint8_t colour)
{
    if (fest_fb && x >= 0 && x < FB_W && y >= 0 && y < FB_H) fest_fb[y * FB_W + x] = colour;
}

void fest_fill(int x, int y, int w, int h, uint8_t colour)
{
    if (!fest_fb) return;
    int x0 = x < 0 ? 0 : x, x1 = x + w > FB_W ? FB_W : x + w;
    if (x1 <= x0) return;
    for (int r = y < 0 ? 0 : y; r < y + h && r < FB_H; r++)
        memset(fest_fb + r * FB_W + x0, colour, (size_t)(x1 - x0));
}

void fest_frame(int x, int y, int w, int h, uint8_t colour)
{
    fest_fill(x, y, w, 1, colour); fest_fill(x, y + h - 1, w, 1, colour);
    fest_fill(x, y, 1, h, colour); fest_fill(x + w - 1, y, 1, h, colour);
}

void fest_text_scaled(int x, int y, const char *s, uint8_t colour, int scale)
{
    if (!fest_fb || scale < 1) return;
    for (; *s; s++, x += 8 * scale) {
        if (*s < 32 || *s > 126) continue;
        const uint8_t *g = font8x8[*s - 32];
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++) {
                if (!(g[r] & (0x80 >> c))) continue;
                fest_fill(x + c * scale, y + r * scale, scale, scale, colour);
            }
    }
}

void fest_text(int x, int y, const char *s, uint8_t colour) { fest_text_scaled(x, y, s, colour, 1); }

void fest_text_center(int y, const char *s, uint8_t colour)
{
    fest_text_scaled((FB_W - 8 * (int)strlen(s)) / 2, y, s, colour, 1);
}

void fest_present(void)
{
    if (!fest_fb) return;
    for (int y = 0; y < FB_H; y += STRIP) {
        int h = FB_H - y < STRIP ? FB_H - y : STRIP;
        const uint8_t *src = fest_fb + (size_t)y * FB_W;
        uint16_t *dst = strip;
        for (int r = 0; r < h; r++) {
            const uint16_t *pal = (fest_crt && ((y + r) & 1)) ? pal_dim : fest_pal;
            for (int x = 0; x < FB_W; x++) *dst++ = pal[*src++];
        }
        display_set_window(0, (uint16_t)y, FB_W, (uint16_t)h);
        display_write_preswapped(strip, (uint32_t)FB_W * h);
        display_wait_done();
    }
}
