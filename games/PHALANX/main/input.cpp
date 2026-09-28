/*
 * input.cpp - medal controls for Space Invaders (held upright, like Pac-Man)
 *
 * The buttons, the power rail, the coin-then-start sequence, the mute gesture and the tilt zero
 * all live in components/medal_input, which every medal shares. What is left here is Space
 * Invaders' own, and there is very little of it: the cannon only moves left and right.
 *
 *   twist left / right  -> the cannon
 *   BOOT button         -> fire; hold 10 s to leave for the menu
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

#define MOVE_DEG   8.0f          /* twist this far to move */
#define RELEASE_DEG 5.0f         /* and back inside this to stop - hysteresis so it does not chatter */
#define X_SIGN (-1.0f)           /* flip if left/right are reversed */

static int8_t dir_x;             /* -1 / 0 / +1 */
static float dbg_roll;
static int64_t last_log;

static void on_mute(void)
{
    bool muted = !audio_get_mute();
    audio_set_mute(muted);
    medalboot_set_muted(muted);                 /* holds in the menu and every other game */
    display_toast(muted ? "SOUND OFF" : "SOUND ON", 1500);
    ESP_LOGI(TAG, "sound %s", muted ? "off" : "on");
}

/* a fresh zero means a fresh stick: whatever was held before it is not held now */
static void on_recentre(void) { dir_x = 0; }

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

void input_update(si_input_t *in)
{
    medal_input_state_t st;
    medal_input_poll(&st);

    if (st.tilt_fresh) {
        float roll = st.lr * X_SIGN;
        dbg_roll = roll;
        if (dir_x == 0) {
            if (roll > MOVE_DEG) dir_x = +1; else if (roll < -MOVE_DEG) dir_x = -1;
        } else if (fabsf(roll) < RELEASE_DEG || (roll > 0) != (dir_x > 0)) {
            dir_x = 0;
            if (roll > MOVE_DEG) dir_x = +1; else if (roll < -MOVE_DEG) dir_x = -1;
        }
    }

    in->coin1  = st.coin ? 1 : 0;
    in->start1 = st.start ? 1 : 0;
    in->left   = (dir_x < 0) ? 1 : 0;
    in->right  = (dir_x > 0) ? 1 : 0;
    in->fire   = st.boot ? 1 : 0;

    int64_t now = esp_timer_get_time();
    if (now - last_log >= 3000000) {
        last_log = now;
        ESP_LOGI(TAG, "tilt %d  roll %+6.1f -> L%d R%d F%d",
                 st.tilt_valid, (double)dbg_roll, in->left, in->right, in->fire);
    }
}
