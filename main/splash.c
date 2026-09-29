/*
 * splash.c - the attract sequence.
 *
 * The wordmark crosses a starfield and leaves; the screen flashes white, with
 * the sound of a blade being drawn; and the title screen is there as the flash
 * clears - a perspective grid, a cabinet, the wordmark - with scanlines over the
 * whole thing so it reads like a CRT rather than an LCD. Everything is drawn from primitives in components/fest: no
 * artwork ships, and no character from any game is reproduced.
 *
 * This draws and nothing else: the frame buffer and the music belong to the
 * caller, so one tune can play across this and the credits roll after it.
 */
#include "splash.h"
#include "fest.h"
#include "chiptune.h"
#include "audio_hal.h"
#include "input.h"
#include "battery.h"
#include "sound.h"
#include "mqart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

#define FPS      30
#define WORD_W   (9 * 16)       /* PELLETINO at scale 2 */
#define FLY_STEP  6             /* pixels a frame */
#define P1_END   ((FB_W + 40 + WORD_W) / FLY_STEP + 6)   /* the wordmark crosses and is gone, then a beat of empty sky */
#define FLASH_FULL 3            /* frames of nothing but white */
#define FLASH_FADE 14           /* frames for the title screen to come up out of it */
#define P3_END   360            /* the title screen holds until here */
#define CX       (FB_W / 2)
#define HORIZON  150

/* motion streaks, only while the wordmark is travelling */
static void speed_lines(int t)
{
    for (int i = 0; i < 12; i++) {
        uint32_t h = (uint32_t)(i + 1) * 2246822519u;
        int y = (int)((h >> 9) % FB_H);
        int len = 30 + (int)((h >> 3) % 70);
        int x = (int)((int)((h >> 17) % FB_W) - t * 26) % FB_W;
        if (x < 0) x += FB_W;
        fest_fill(x, y, len, 1, CUBE(1,1,2));
    }
}

static void wordmark(int x, int y, bool bright)
{
    fest_text_scaled(x, y, "PELLETINO", bright ? UI_WHITE : CUBE(4,4,5), 2);
}

/* how many of the carousel's entries are games, rather than part of the launcher */
static int game_count(void)
{
    int n = 0;
    for (int i = 0; i < mqart_count(); i++) {
        const mqart_entry_t *e = mqart_get(i);
        if (e && e->boot[0] != '@') n++;
    }
    return n;
}

bool splash_scene(void)
{
    char games[24];
    snprintf(games, sizeof games, "%d GAMES", game_count());

    int64_t start = esp_timer_get_time();
    for (int t = 0; t < P3_END; t++) {
        audio_update();
        input_poll();                       /* the power button still has to work */
        battery_tick();
        sound_poll();                       /* both buttons: sound off/on */
        if (input_take_wake()) { fest_white = 0; return true; }

        fest_clear(UI_BLACK);
        fest_stars(t);

        if (t < P1_END) {
            speed_lines(t);
            wordmark(FB_W + 40 - t * FLY_STEP, 120, true);
        } else {
            int k = t - P1_END;
            if (k == 0) chip_sfx(CHIP_SFX_SHING);
            fest_white = k < FLASH_FULL ? 255
                       : k < FLASH_FULL + FLASH_FADE ? (uint8_t)(255 * (FLASH_FULL + FLASH_FADE - k) / FLASH_FADE)
                       : 0;
            fest_grid(t, HORIZON);
            fest_cabinet(t, CX, HORIZON + 46);
            wordmark(CX - WORD_W / 2, 44, true);
            fest_text_center(70, games, arcade_colours[2]);
            if (k > FLASH_FULL + FLASH_FADE && ((k / 12) & 1)) fest_text_center(250, "PRESS THE BUTTON", UI_WHITE);
        }

        fest_scanlines();
        fest_present();

        int64_t due = start + (int64_t)(t + 1) * (1000000 / FPS);
        while (esp_timer_get_time() < due) { audio_update(); vTaskDelay(1); }
    }
    return false;
}
