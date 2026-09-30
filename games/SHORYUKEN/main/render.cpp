/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro */
/*
 * render.cpp - the CPS-1 picture onto the 240x280 portrait panel.
 *
 * The picture is 384x224 and the panel is 240 wide, so it is either shrunk to 240x140
 * (VIDEO_MODE=SCALE) or the middle 240 columns are shown as they are (CROP). Either way it
 * sits in the middle of the panel with black above and below, and either way it is drawn
 * sixteen rows at a time, at the size it is shown, straight into the strip that goes to the
 * panel. There is no frame buffer: 384x224 would be 86 KB even at a byte a pixel, and the
 * machine's own RAM has already taken 256 KB of the 390 KB there is.
 *
 * The panel driver's DMA buffer holds 7168 bytes, which is less than a strip (7680), so a
 * strip goes out as two transfers of eight rows. The driver copies each into one of its
 * two buffers and sends it while the next is being drawn.
 */
#include "render.h"
#include "cps1.h"
#include "cps1_view.h"
#include "knobs.h"
#include "display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "RENDER";

#define STRIP_ROWS 16
#define HALF_ROWS 8

#if VIDEO_MODE == VIDEO_MODE_CROP
#define VIEW_H VIEW_CROP_H
#else
#define VIEW_H VIEW_SCALE_H
#endif
#define VIEW_Y ((DISPLAY_HEIGHT - VIEW_H) / 2)

static uint16_t *strip;
static uint16_t cols[VIEW_W], rows[VIEW_H];
static uint64_t strip_sig[(VIEW_H + STRIP_ROWS - 1) / STRIP_ROWS];
static bool toast_was_up;
uint32_t render_strips_sent, render_strips_kept;

void render_init(void)
{
    strip = (uint16_t *)heap_caps_malloc(VIEW_W * STRIP_ROWS * sizeof(uint16_t), MALLOC_CAP_8BIT);
    if (!strip) { ESP_LOGE(TAG, "strip allocation failed"); abort(); }
#if VIDEO_MODE == VIDEO_MODE_CROP
    view_crop(cols, rows);
#else
    view_scale(cols, rows);
#endif
    cps1_set_view(VIEW_W, VIEW_H, cols, rows);
    display_fill(0x0000);                       /* the bars are drawn once and left alone */
    ESP_LOGI(TAG, "%s: %dx%d at row %d, %d-row strips", VIDEO_MODE_NAME, VIEW_W, VIEW_H, VIEW_Y, STRIP_ROWS);
}

void render_frame(void (*between)(void), uint64_t *video_us, uint64_t *strips_us)
{
    int64_t t0 = esp_timer_get_time();
    cps1_frame_begin();
    /* the toast is drawn into strips as they go past, so while one is up, and once after it
     * has gone, every strip goes */
    bool toast_up = display_toast_active();
    bool everything = !STRIP_REUSE || toast_up || toast_was_up;
    toast_was_up = toast_up;
    int64_t t1 = esp_timer_get_time();
    *video_us += t1 - t0;
    t0 = t1;

    for (int y = 0, i = 0; y < VIEW_H; y += STRIP_ROWS, i++) {
        int n = (y + STRIP_ROWS <= VIEW_H) ? STRIP_ROWS : (VIEW_H - y);
#if STRIP_REUSE
        uint64_t sig = cps1_strip_signature(y, n);
        if (!everything && sig == strip_sig[i]) { render_strips_kept++; continue; }
        strip_sig[i] = sig;
#endif
        cps1_render(strip, y, n);
        t1 = esp_timer_get_time();
        *video_us += t1 - t0;
        display_set_window(0, VIEW_Y + y, VIEW_W, n);
        for (int r = 0; r < n; r += HALF_ROWS) {
            int h = (r + HALF_ROWS <= n) ? HALF_ROWS : (n - r);
            display_write_preswapped(strip + r * VIEW_W, h * VIEW_W);
        }
        render_strips_sent++;
        t0 = esp_timer_get_time();
        *strips_us += t0 - t1;
        if (between) { between(); t0 = esp_timer_get_time(); }
    }
    display_wait_done();
    *strips_us += esp_timer_get_time() - t0;
}
