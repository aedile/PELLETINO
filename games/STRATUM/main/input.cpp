/*
 * input.cpp - medal controls for Dig Dug (held upright, like Pac-Man)
 *
 * The buttons, the power rail, the coin-then-start sequence, the mute gesture and the tilt zero
 * all live in components/medal_input, which every medal shares. What is left here is the part
 * that is this game's own: turning two angles into a four-way stick.
 *
 * Dig Dug is a four-way game, so only the dominant axis is ever reported.
 *
 *   twist left / right  -> left and right
 *   tip away / toward   -> up and down
 *   BOOT button         -> the pump
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

#define MOVE_DEG             10.0f    /* tilt this far to move left or right */
#define VERT_DEG              8.0f    /* and this far up or down - out-of-plane tilt is harder to hold */
#define AXIS_STICK      1.5f    /* how much the other axis must beat the current one to take over */
#define X_SIGN (-1.0f)          /* flip if left/right are reversed */
#define Y_SIGN (-1.0f)          /* flip if forward/back are reversed */

static int8_t dir_x, dir_y;              /* -1 / 0 / +1 */
static int8_t last_axis;                 /* 0 none, 1 horizontal, 2 vertical */
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

/* a fresh zero means a fresh stick: whatever was held before it is not held now */
static void on_recentre(void) { dir_x = dir_y = 0; last_axis = 0; }

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

void input_update(dd_input_t *in)
{
    medal_input_state_t st;
    medal_input_poll(&st);

    if (st.tilt_fresh) {
        float roll = st.lr * X_SIGN, pitch = st.ud * Y_SIGN;
        dbg_roll = roll; dbg_pitch = pitch;
        /*
         * Four-way, and the axis is sticky. Picking whichever tilt is merely larger hands the
         * moment to left/right whenever a little roll comes along with holding the medal tipped
         * away from you - and then nothing happens at all, because that roll is often below its
         * own threshold. So an axis that clears its threshold alone wins outright, and when
         * both clear it the one already in use keeps it until the other beats it by half again
         * as much. The vertical threshold is lower, because tipping the medal out of its own
         * plane is a less natural motion than rolling it.
         */
        int hx = (roll > MOVE_DEG) ? +1 : (roll < -MOVE_DEG) ? -1 : 0;
        int vy = (pitch > VERT_DEG) ? +1 : (pitch < -VERT_DEG) ? -1 : 0;
        int axis;
        if (!hx && !vy)      axis = 0;
        else if (!vy)        axis = 1;
        else if (!hx)        axis = 2;
        else if (last_axis == 1) axis = (fabsf(pitch) > fabsf(roll) * AXIS_STICK) ? 2 : 1;
        else if (last_axis == 2) axis = (fabsf(roll) > fabsf(pitch) * AXIS_STICK) ? 1 : 2;
        else                 axis = (fabsf(roll) > fabsf(pitch)) ? 1 : 2;
        last_axis = (int8_t)axis;
        dir_x = (axis == 1) ? (int8_t)hx : 0;
        dir_y = (axis == 2) ? (int8_t)vy : 0;
    }

    in->coin1  = st.coin ? 1 : 0;
    in->start1 = st.start ? 1 : 0;
    in->left   = (dir_x < 0) ? 1 : 0;
    in->right  = (dir_x > 0) ? 1 : 0;
    in->up     = (dir_y > 0) ? 1 : 0;
    in->down   = (dir_y < 0) ? 1 : 0;
    in->fire   = st.boot ? 1 : 0;                    /* the pump */

    int64_t now = esp_timer_get_time();
    if (now - last_log >= 3000000) {
        last_log = now;
        ESP_LOGI(TAG, "tilt %d  roll %+6.1f pitch %+6.1f -> U%d D%d L%d R%d",
                 st.tilt_valid, (double)dbg_roll, (double)dbg_pitch,
                 in->up, in->down, in->left, in->right);
    }
}
