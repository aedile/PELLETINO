/*
 * scene.c - the attract-mode look: starfield, a scrolling perspective grid,
 * CRT scanlines and a cabinet. Everything is drawn from primitives; no artwork
 * ships with this project and no character from any game is reproduced here.
 *
 */
#include "fest.h"
#include <stdbool.h>
#include <stddef.h>

#define L  0
#define R  FB_W
#define CX (FB_W / 2)

/* the colours an arcade cabinet actually used: saturated, few, high contrast */
const uint8_t arcade_colours[6] = {
    CUBE(5,0,1), CUBE(0,4,5), CUBE(5,4,0), CUBE(0,5,1), CUBE(4,0,5), CUBE(5,2,0)
};

/* ~100 stars in three brightness tiers, each twinkling on its own phase */
void fest_stars(int frame)
{
    static const uint8_t tier[3] = { CUBE(1,1,2), CUBE(3,3,4), UI_WHITE };
    for (int i = 0; i < 100; i++) {
        uint32_t h = (uint32_t)i * 2654435761u;
        int x = L + (int)((h >> 8) % (uint32_t)(R - L)), y = (int)((h >> 20) % 150);
        int t = (int)((h >> 4) % 10);
        int phase = (frame + (int)(h & 63)) >> 3;
        int tw = (phase * 5 + i) % 7;
        if (t < 6) { if (tw > 1) fest_px(x, y, tier[0]); }
        else if (t < 9) { fest_px(x, y, tw > 3 ? tier[1] : tier[0]); }
        else {
            fest_px(x, y, tier[2]);
            if (tw > 4) {                      /* the brightest flare into a sparkle */
                fest_px(x - 1, y, tier[1]); fest_px(x + 1, y, tier[1]);
                fest_px(x, y - 1, tier[1]); fest_px(x, y + 1, tier[1]);
            }
        }
    }
}

void fest_cabinet(int frame, int cx, int base_y)
{
    uint8_t shell = CUBE(1,1,2), trim = CUBE(4,0,1);
    int w = 74, h = 116, x = cx - w / 2, y = base_y - h;
    fest_fill(x, y, w, h, shell);                       /* body */
    fest_fill(x + 3, y + 4, w - 6, 16, trim);           /* header, lit */
    for (int i = 0; i < 5; i++)
        fest_fill(x + 8 + i * 12, y + 8, 8, 8, ((frame >> 2) + i) & 1 ? CUBE(5,5,2) : CUBE(5,3,0));
    fest_fill(x + 6, y + 26, w - 12, 44, UI_BLACK);     /* screen bezel */
    /* something playing on it: a drifting starfield with a blob dodging about */
    for (int i = 0; i < 18; i++) {
        uint32_t hh = (uint32_t)(i + 3) * 2246822519u;
        int sx = x + 8 + (int)(((hh >> 7) % (uint32_t)(w - 16) + frame) % (uint32_t)(w - 16));
        int sy = y + 28 + (int)((hh >> 15) % 40);
        fest_px(sx, sy, CUBE(2,2,4));
    }
    int bx = x + w / 2 + ((frame >> 1) % 24) - 12;
    fest_fill(bx, y + 58, 6, 5, CUBE(1,5,1));
    fest_fill(x + 6, y + 74, w - 12, 12, CUBE(2,2,3));  /* control panel */
    fest_fill(x + 16, y + 77, 5, 5, CUBE(5,0,0));       /* stick */
    fest_fill(x + 34, y + 78, 4, 4, CUBE(5,5,0));
    fest_fill(x + 44, y + 78, 4, 4, CUBE(0,3,5));
    fest_fill(x, base_y - 26, w, 26, CUBE(1,1,1));      /* the plinth */
}

/* ---------------------------------------------------------------------------
 * The perspective grid. Horizontal rules crowd toward the horizon and scroll
 * outward; verticals fan from a vanishing point at the centre of it. Integer
 * maths throughout: a rule at depth z lands at horizon + BELOW/z, so z doubling
 * halves the distance below the horizon, which is what perspective does.
 * ------------------------------------------------------------------------- */
void fest_grid(int frame, int horizon)
{
    if (horizon < 1 || horizon >= FB_H) return;
    int below = FB_H - horizon;
    uint8_t near = CUBE(4,0,5), far = CUBE(1,0,2);

    /* horizontal rules, scrolling toward the viewer */
    for (int k = 1; k <= 18; k++) {
        int z16 = k * 16 + (frame % 16);              /* 1/16 of a step per frame */
        int y = horizon + (below * 16) / z16;
        if (y >= FB_H || y <= horizon) continue;
        fest_fill(0, y, FB_W, 1, (y - horizon) > below / 3 ? near : far);
    }

    /* verticals fanning out from the vanishing point */
    for (int i = -7; i <= 7; i++) {
        int x_bottom = FB_W / 2 + i * 42;
        for (int y = horizon; y < FB_H; y++) {
            int x = FB_W / 2 + (x_bottom - FB_W / 2) * (y - horizon) / below;
            fest_px(x, y, (y - horizon) > below / 3 ? near : far);
        }
    }
}

/* A CRT drew every other line dimmer. One pass over the frame, skipping black,
 * dropping each channel by a third in cube space. */
void fest_scanlines(void)
{
    if (!fest_fb) return;
    for (int y = 1; y < FB_H; y += 2) {
        uint8_t *row = fest_fb + (size_t)y * FB_W;
        for (int x = 0; x < FB_W; x++) {
            uint8_t v = row[x];
            if (!v || v >= 180) continue;             /* black, or a named UI colour */
            row[x] = CUBE((v / 30) * 2 / 3, ((v / 5) % 6) * 2 / 3, (v % 5) * 2 / 3);
        }
    }
}
