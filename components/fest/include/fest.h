/*
 * fest.h - palette framebuffer and the attract-mode scene drawn into it.
 *
 * Everything the launcher shows moves - the attract sequence, the credits, the
 * wheel - so it all draws into one real framebuffer: 8 bits per pixel,
 * 240x280 = 67 KB, half what RGB565 would cost, and cheap to fill and sprite into.
 *
 * Colours are indices into a 6x6x5 RGB cube (0..179) plus a few named entries
 * above it. 199..254 are free for whoever is drawing; the wheel loads the
 * colours of the screenshot behind it there. Palette entries are stored
 * byte-swapped, ready for the panel.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FB_W  240
#define FB_H  280

/* 6 reds x 6 greens x 5 blues = 180 entries, index 0..179 */
#define CUBE(r, g, b) ((uint8_t)((r) * 30 + (g) * 5 + (b)))

#define UI_BLACK  192
#define UI_WHITE  193
#define UI_GREY   194
#define UI_YELLOW 195
#define UI_GREEN  196
#define UI_RED    197
#define UI_BLUE   198

extern uint8_t *fest_fb;          /* FB_W * FB_H, NULL until fest_init() */
extern uint16_t fest_pal[256];
extern bool     fest_crt;         /* present every other row a quarter darker, whatever is on it */
extern uint8_t  fest_white;       /* present everything this far toward white: 0 as drawn, 255 blank white */

bool fest_init(void);             /* false if the framebuffer will not fit */
void fest_free(void);

void fest_colour(uint8_t index, uint8_t r, uint8_t g, uint8_t b);   /* set a palette entry */
void fest_clear(uint8_t colour);
void fest_px(int x, int y, uint8_t colour);
void fest_fill(int x, int y, int w, int h, uint8_t colour);
void fest_frame(int x, int y, int w, int h, uint8_t colour);
void fest_text(int x, int y, const char *s, uint8_t colour);
void fest_text_center(int y, const char *s, uint8_t colour);
void fest_text_scaled(int x, int y, const char *s, uint8_t colour, int scale);
void fest_present(void);          /* palette -> RGB565, pushed in strips */

/* attract-mode scene, all procedural - no artwork, no borrowed characters */
extern const uint8_t arcade_colours[6];
void fest_stars(int frame);                 /* twinkling starfield */
void fest_grid(int frame, int horizon);     /* scrolling perspective grid */
void fest_scanlines(void);                  /* CRT line dimming, applied last */
void fest_cabinet(int frame, int cx, int base_y);   /* a lit upright cabinet */

#ifdef __cplusplus
}
#endif
