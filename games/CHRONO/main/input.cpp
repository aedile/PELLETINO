/*
 * input.cpp - medal controls for Time Pilot (held upright, like Pac-Man)
 *
 * The buttons, the power rail, the coin-then-start sequence, the mute gesture and the tilt zero
 * all live in components/medal_input, which every medal shares. What is left here is Time
 * Pilot's own: an eight-way stick. The plane turns toward wherever the stick points, so the
 * two tilt axes are simply the stick's two axes, and both may be on at once for a diagonal.
 *
 *   twist left / right  -> turn toward the left or right of the screen
 *   tip away / toward   -> turn toward the top or bottom
 *   BOOT button         -> fire; hold 5 s to leave for the menu
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

static const char *TAG = "INPUT";

#define ON_DEG    9.0f          /* an axis comes on at this much tilt */
#define OFF_DEG   6.0f          /* and goes off inside this - a little hysteresis */
#define X_SIGN (-1.0f)          /* flip if left/right are reversed */
#define Y_SIGN (-1.0f)          /* flip if up/down are reversed */

static int8_t dir_x, dir_y;
static float dbg_roll, dbg_pitch;
static int64_t last_log;

static void on_mute(void)
{
    medalboot_sound_t s = medalboot_sound_next();      /* loud, quiet, off - and it holds everywhere */
    audio_set_volume(medalboot_sound_volume(s));
    audio_set_mute(s == MEDALBOOT_SOUND_OFF);
    display_toast(medalboot_sound_name(s), 1500);
    ESP_LOGI(TAG, "%s", medalboot_sound_name(s));
}

static void on_recentre(void) { dir_x = dir_y = 0; }

static int8_t axis(float v, int8_t was)
{
    if (was == 0) return (v > ON_DEG) ? 1 : (v < -ON_DEG) ? -1 : 0;
    if (was > 0)  return (v > OFF_DEG) ? 1 : (v < -ON_DEG) ? -1 : 0;
    return (v < -OFF_DEG) ? -1 : (v > ON_DEG) ? 1 : 0;
}

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

void input_update(tp_input_t *in)
{
    medal_input_state_t st;
    medal_input_poll(&st);

    if (st.tilt_fresh) {
        float roll = st.lr * X_SIGN, pitch = st.ud * Y_SIGN;
        dbg_roll = roll; dbg_pitch = pitch;
        dir_x = axis(roll, dir_x);
        dir_y = axis(pitch, dir_y);
    }

    in->coin1  = st.coin ? 1 : 0;
    in->start1 = st.start ? 1 : 0;
    in->left   = (dir_x < 0) ? 1 : 0;
    in->right  = (dir_x > 0) ? 1 : 0;
    in->up     = (dir_y > 0) ? 1 : 0;
    in->down   = (dir_y < 0) ? 1 : 0;
    in->fire   = st.boot ? 1 : 0;

    int64_t now = esp_timer_get_time();
    if (now - last_log >= 3000000) {
        last_log = now;
        ESP_LOGI(TAG, "tilt %d  roll %+6.1f pitch %+6.1f -> U%d D%d L%d R%d F%d",
                 st.tilt_valid, (double)dbg_roll, (double)dbg_pitch, in->up, in->down, in->left, in->right, in->fire);
    }
}
