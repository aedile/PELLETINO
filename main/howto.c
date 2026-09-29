/*
 * howto.c - how to work it, told by the thing itself.
 *
 * Nothing on the outside says which button does what, or that pressing both
 * changes the sound, so attract mode says it: three short tables - in the menu,
 * in a game, anywhere - that write themselves out a row at a time.
 *
 * The panel's corners are rounded, so the tables sit in the middle and the
 * first and last lines are short.
 */
#include "howto.h"
#include "fest.h"
#include "chiptune.h"
#include "audio_hal.h"
#include "input.h"
#include "battery.h"
#include "sound.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define FPS        30
#define ROW_EVERY  14           /* frames between one row appearing and the next */
#define HOLD_AFTER 150          /* frames it stays up once it is all there */
#define GUTTER     132          /* what to do, right-aligned to here; what it does, from 8 past it */

/* a row with nothing on the right is a heading */
static const struct { const char *what, *does; } rows[] = {
    { "IN THE MENU",     NULL },
    { "TOP BUTTON",      "LAST GAME" },
    { "MIDDLE BUTTON",   "NEXT GAME" },
    { "HOLD MIDDLE",     "PLAY IT" },
    { "",                NULL },
    { "IN A GAME",       NULL },
    { "TILT",            "MOVE, STEER" },
    { "TOP BUTTON",      "COIN, START" },
    { "MIDDLE BUTTON",   "FIRE, JUMP" },
    { "HOLD MIDDLE 5S",  "TO THE MENU" },
    { "",                NULL },
    { "ANYWHERE",        NULL },
    { "BOTH BUTTONS",    "SOUND" },
    { "HOLD TOP",        "POWER OFF" },
};
#define ROWS ((int)(sizeof rows / sizeof rows[0]))
#define TOP      62
#define ROW_H    12

bool howto_scene(void)
{
    int64_t start = esp_timer_get_time();
    int total = ROWS * ROW_EVERY + HOLD_AFTER;
    for (int t = 0; t < total; t++) {
        audio_update();
        input_poll();                       /* the power button still has to work */
        battery_tick();
        sound_poll();                       /* both buttons: the sound */
        if (input_take_wake()) return true;

        fest_clear(UI_BLACK);
        fest_stars(t);
        fest_text_scaled((FB_W - 11 * 16) / 2, 30, "HOW TO PLAY", UI_WHITE, 2);

        int shown = t / ROW_EVERY + 1;
        if (shown > ROWS) shown = ROWS;
        for (int i = 0; i < shown; i++) {
            int y = TOP + i * ROW_H;
            const char *what = rows[i].what, *does = rows[i].does;
            if (!does) {
                fest_text_center(y, what, arcade_colours[1]);
                continue;
            }
            /* the newest row is white for a moment, then settles */
            bool fresh = i == shown - 1 && t < ROWS * ROW_EVERY;
            fest_text(GUTTER - 8 * (int)strlen(what), y, what, fresh ? UI_WHITE : arcade_colours[2]);
            fest_text(GUTTER + 8, y, does, fresh ? UI_WHITE : CUBE(4,4,5));
        }
        /* a tick as each row lands, headings and gaps excepted */
        if (t % ROW_EVERY == 0 && t / ROW_EVERY < ROWS && rows[t / ROW_EVERY].does) chip_sfx(CHIP_SFX_CLICK);

        if ((t / 12) & 1) fest_text_center(244, "PRESS A BUTTON", UI_WHITE);

        fest_scanlines();
        fest_present();

        int64_t due = start + (int64_t)(t + 1) * (1000000 / FPS);
        while (esp_timer_get_time() < due) { audio_update(); vTaskDelay(1); }
    }
    return false;
}
