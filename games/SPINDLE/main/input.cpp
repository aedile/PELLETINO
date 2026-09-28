/*
 * input.cpp - medal controls for Tempest (held upright, like Pac-Man)
 *
 * The buttons, the power rail, the coin-then-start sequence, the mute gesture and the tilt zero
 * all live in components/medal_input, which every medal shares. What is left here is Tempest's
 * own, and it is the closest fit of any of these games: the cabinet control *is* a spinner, and
 * the medal's twist is a spinner.
 *
 * The one difference is that a real knob has no centre. Tempest's is a free-running four-bit
 * counter, so the game only ever sees how far it moved since the last look. A twist of the
 * medal is an absolute angle instead, so what gets handed over is the change in that angle -
 * turn and hold, and the claw stops where you left it, which is what a spinner does.
 *
 *   twist left / right  -> the claw around the rim of the tube
 *   BOOT button         -> fire; a short second press is the superzapper; hold 3 s for sound
 *   PWR short press     -> coin, then start half a second later; long press (1 s) -> power off
 */
#include "input.h"
#include "medal_input.h"
#include "display.h"
#include "medalboot.h"
#include "qmi8658.h"
#include "audio_hal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>

static const char *TAG = "INPUT";

#define DEG_PER_COUNT  2.5f     /* twist this far to move the claw one position (lower = more sensitive) */
#define MAX_COUNTS     6        /* per frame; a whole lap of the web is 72 */
#define SPIN_SIGN (-1.0f)       /* flip if the claw goes the wrong way */
#define ZAP_WINDOW_US 400000    /* a second press this soon after the first is the superzapper */

static float last_angle;
static bool have_angle;
static float owed;              /* fractional counts not yet handed over */
static int64_t last_press, last_log;
static bool zap_pending, fire_was_down;

static void on_mute(void)
{
    bool muted = !audio_get_mute();
    audio_set_mute(muted);
    medalboot_set_muted(muted);                 /* holds in the menu and every other game */
    display_toast(muted ? "SOUND OFF" : "SOUND ON", 1500);
    ESP_LOGI(TAG, "sound %s", muted ? "off" : "on");
}

/* a fresh zero means the knob is wherever you are holding it, and owes nothing */
static void on_recentre(void) { have_angle = false; owed = 0.0f; }

void input_init(void)
{
    medal_input_config_t cfg = {};
    cfg.init_i2c = true;
    cfg.imu_init = qmi8658_init;
    cfg.read_accel = qmi8658_read_accel;
    cfg.imu_period_us = 5000;           /* a spinner wants a faster hand than a joystick does */
    cfg.mute_hold_us = 3000000;
    cfg.on_mute = on_mute;
    cfg.on_recentre = on_recentre;
    cfg.exit_hold_us = MEDALBOOT_EXIT_HOLD_MS * 1000;   /* hold to leave for the menu */
    cfg.on_exit = medalboot_exit_to_menu;
    medal_input_init(&cfg);
}

void input_update(tp_input_t *in)
{
    medal_input_state_t st;
    medal_input_poll(&st);
    int64_t now = esp_timer_get_time();

    in->spin = 0;
    if (st.tilt_valid) {
        float a = st.lr * SPIN_SIGN;
        if (!have_angle) { last_angle = a; have_angle = true; }
        owed += medal_wrap_deg(a - last_angle) / DEG_PER_COUNT;
        last_angle = a;
        int counts = (int)owed;                  /* truncate toward zero; the rest carries over */
        owed -= counts;
        if (counts >  MAX_COUNTS) counts =  MAX_COUNTS;
        if (counts < -MAX_COUNTS) counts = -MAX_COUNTS;
        in->spin = (int8_t)counts;
    }

    /*
     * Tempest has two buttons and the medal has one, so a quick double press is the
     * superzapper - which is right for it, since you get two a level and never want it by
     * accident in the middle of firing.
     */
    if (st.boot && !fire_was_down) {
        if (now - last_press < ZAP_WINDOW_US) zap_pending = true;
        last_press = now;
    }
    fire_was_down = st.boot;
    in->fire = st.boot ? 1 : 0;
    in->zap = zap_pending ? 1 : 0;
    if (zap_pending && !st.boot) zap_pending = false;

    in->coin1  = st.coin ? 1 : 0;
    in->start1 = st.start ? 1 : 0;

    if (now - last_log >= 3000000) {
        last_log = now;
        ESP_LOGI(TAG, "tilt %d  twist %+6.1f -> spin %+d fire %d zap %d",
                 st.tilt_valid, (double)st.lr, in->spin, in->fire, in->zap);
    }
}
