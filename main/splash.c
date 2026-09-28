/*
 * splash.c - the cold open.
 *
 * PELLETINO races past, then three hard cuts - fireworks over the tower, a
 * pinata, a cabinet mid-game - and finally the fiesta itself: papel picado,
 * confetti and a dancing stage, with the AY-3-8910 playing throughout.
 *
 * Everything on screen is drawn from primitives in components/fest. The splash
 * deliberately shares no artwork with the menu: the marquees are the menu's job,
 * and a splash that previewed them would just look like a slower menu.
 *
 * Adapted from NESTOR's splash, which drove its music through the NES core's APU.
 * That core is nofrendo and GPL-2.0, so this one uses the AY the arcade medals
 * already emulate and the launcher stays 0BSD.
 */
#include "splash.h"
#include "fest.h"
#include "chiptune.h"
#include "audio_hal.h"
#include "input.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define FPS      30
#define CUT_LEN  44
#define P1_END   50                        /* title race */
#define P2_END   (P1_END + CUT_LEN * 3)    /* three cuts */
#define P3_END   (P2_END + 180)            /* the fiesta */
#define CX       (FB_W / 2)

/* horizontal streaks, so a cut reads as motion rather than a slide */
static void speed_lines(int t, int dir)
{
    for (int i = 0; i < 14; i++) {
        uint32_t h = (uint32_t)(i + 1) * 2246822519u;
        int y = (int)((h >> 9) % FB_H);
        int len = 30 + (int)((h >> 3) % 70);
        int x = (int)((int)((h >> 17) % FB_W) + dir * t * 22) % FB_W;
        if (x < 0) x += FB_W;
        fest_fill(x, y, len, 1, UI_GREY);
    }
}

/* one cut: the set piece punches in from the side, a word rides a letterbox band */
static void cut(int t, int which, uint8_t colour, const char *word)
{
    int slide = (CUT_LEN - t) * (CUT_LEN - t) / 4;
    int dir = (which & 1) ? -1 : 1;
    int ox = dir * slide;

    fest_clear(which == 0 ? UI_BLACK : UI_BLACK);
    if (which != 0) speed_lines(t, -dir);   /* the skyline is busy enough already */

    switch (which) {
    case 0:
        /* the night scene is drawn at fixed coordinates, so it lands as a hard
         * cut rather than sliding in - which suits it better anyway */
        fest_stars(t * 2);
        fest_skyline();
        fest_fireworks(t * 2);
        fest_beacon(t * 2);
        break;
    case 1:
        fest_pinata(t * 2, CX + ox, 40);
        break;
    default:
        fest_cabinet(t, CX + ox, 190);
        break;
    }

    int band = 236;          /* below the skyline's ground line */
    fest_fill(0, band, FB_W, 34, colour);
    fest_text_scaled(CX - 12 * (int)strlen(word), band + 5, word, UI_BLACK, 3);
}

static void fiesta(int t)
{
    fest_clear(CUBE(0,0,1));
    fest_fireworks(t);
    fest_papel_picado(t);
    fest_confetti(t);
    fest_dancers(t, 240);
    bool pop = (t / 6) & 1;
    /* one word. 10 chars at scale 2 is 160 px, so it centres with a real margin;
     * scale 3 would be exactly 240 and touch both edges. */
    fest_text_scaled(CX - 8 * 10, 100, "PELLETINO", pop ? UI_YELLOW : UI_WHITE, 2);
    fest_text_center(152, "HOLD THE BUTTON TO PLAY", UI_GREY);
}

void splash_run(void)
{
    if (!fest_init()) return;
    chip_init();
    chip_play();

    int64_t start = esp_timer_get_time();
    for (int t = 0; t < P3_END; t++) {
        audio_update();
        if (input_button_down()) break;

        if (t < P1_END) {
            fest_clear(UI_BLACK);
            speed_lines(t, -1);
            fest_text_scaled(FB_W + 80 - t * 12, 120, "PELLETINO", UI_YELLOW, 3);
        } else if (t < P2_END) {
            int k = (t - P1_END) / CUT_LEN;
            static const char *const words[3] = { "PICK", "ONE", "PLAY" };
            cut((t - P1_END) % CUT_LEN, k, fiesta_colours[k * 2], words[k]);
        } else {
            fiesta(t - P2_END);
        }
        fest_present();

        /* hold the frame rate without drifting, and keep the DMA queue fed */
        int64_t due = start + (int64_t)(t + 1) * (1000000 / FPS);
        while (esp_timer_get_time() < due) { audio_update(); vTaskDelay(1); }
    }

    chip_stop();
    chip_free();
    audio_update();
    fest_free();
}
