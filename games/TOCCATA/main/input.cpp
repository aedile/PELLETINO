/*
 * input.cpp - medal controls for Gyruss (held upright, like Pac-Man)
 *
 * The buttons, the power rail, the coin-then-start sequence, the mute gesture and the tilt zero
 * all live in components/medal_input, which every medal shares. What is left here is Gyruss's
 * own, and it is a happy accident: the ship flies around the rim of a circle, and the cabinet
 * stick only ever moves it left or right around that rim. A twist of the medal is the same
 * gesture, so the ring on the screen turns the way your hand does.
 *
 *   twist left / right  -> around the ring
 *   BOOT button         -> fire
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

#define TURN_DEG       8.0f     /* twist this far to start moving around the ring */
#define RELEASE_DEG    5.0f     /* and back inside this to stop - a little hysteresis */
#define X_SIGN (-1.0f)          /* flip if the ship goes the wrong way */

static int8_t dir;                       /* -1 left, 0 still, +1 right */
static float dbg_roll;
static int64_t last_log;

static void on_mute(void)
{
    medalboot_sound_t s = medalboot_sound_next();      /* loud, quiet, off - and it holds everywhere */
    audio_set_volume(medalboot_sound_volume(s));
    audio_set_mute(s == MEDALBOOT_SOUND_OFF);
    display_toast(medalboot_sound_name(s), 1500);
    ESP_LOGI(TAG, "%s", medalboot_sound_name(s));
}

static void on_recentre(void) { dir = 0; }

void input_init(void)
{
    medal_input_config_t cfg = {};
    cfg.init_i2c = true;
    cfg.imu_init = qmi8658_init;
    cfg.read_accel = qmi8658_read_accel;
    cfg.mute_hold_us = 3000000;
    cfg.on_mute = on_mute;
    cfg.on_recentre = on_recentre;
    cfg.exit_hold_us = MEDALBOOT_EXIT_HOLD_MS * 1000;   /* hold to leave for the menu */
    cfg.on_exit = medalboot_exit_to_menu;
    medal_input_init(&cfg);
}

void input_update(gy_input_t *in)
{
    medal_input_state_t st;
    medal_input_poll(&st);

    if (st.tilt_fresh) {
        float roll = st.lr * X_SIGN;
        dbg_roll = roll;
        /* hysteresis, so a hand that is nearly still does not chatter the ship back and forth */
        float mag = fabsf(roll);
        if (dir == 0) { if (mag > TURN_DEG) dir = (roll > 0) ? +1 : -1; }
        else if (mag < RELEASE_DEG) dir = 0;
        else dir = (roll > 0) ? +1 : -1;
    }

    in->left   = (dir < 0) ? 1 : 0;
    in->right  = (dir > 0) ? 1 : 0;
    in->up = in->down = 0;
    in->fire   = st.boot ? 1 : 0;
    in->coin1  = st.coin ? 1 : 0;
    in->start1 = st.start ? 1 : 0;

    int64_t now = esp_timer_get_time();
    if (now - last_log >= 3000000) {
        last_log = now;
        ESP_LOGI(TAG, "tilt %d  twist %+6.1f -> L%d R%d fire %d",
                 st.tilt_valid, (double)dbg_roll, in->left, in->right, in->fire);
    }
}
