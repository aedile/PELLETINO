/*
 * pacman_input.cpp - medal controls for Pac-Man and Ms. Pac-Man
 *
 * The battery rail, the buttons, the coin-then-start sequence, the mute gesture and the tilt
 * zero all live in components/medal_input, which every medal shares. What is left here is the
 * part that is Pac-Man's own: turning two angles into a four-way stick, and the two port reads
 * the emulator asks for.
 *
 *   twist left / right  -> left and right
 *   tip away / toward   -> up and down
 *   BOOT short press    -> coin, then start half a second later (as it always has been here)
 *   BOOT hold 3 s       -> sound off and on
 *   PWR short press     -> coin and start, the same gesture as every other medal
 *   PWR hold 1 s        -> power off
 *
 * The tilt used to come from qmi8658_get_tilt() against a calibration taken at boot, which
 * assumed the medal was lying flat. That is a centre nobody was ever holding: it makes you tilt
 * away from flat rather than away from however you are actually holding the thing. Now the zero
 * is the same one every other medal uses - captured the first time the medal is held up, and
 * again on each coin and start.
 */

#include "pacman_input.h"
#include "medal_input.h"
#include "qmi8658.h"
#include "audio_hal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>

static const char *TAG = "PACMAN_INPUT";

#define MOVE_DEG       10.0f    /* twist this far to turn left or right */
#define VERT_DEG        8.0f    /* and tip this far for up and down - out-of-plane is harder to hold */
#define AXIS_STICK      1.5f    /* how much the other axis must beat the current one to take over */
#define X_SIGN (-1.0f)          /* flip if left/right are reversed */
#define Y_SIGN (-1.0f)          /* flip if up/down are reversed */
#define HOLD_MUTE_US 3000000

static uint8_t current_buttons;
static bool coin_active, start_active;
static int8_t dir_x, dir_y;              /* -1 / 0 / +1 */
static int8_t last_axis;                 /* 0 none, 1 horizontal, 2 vertical */
static bool mute_fired;
static int64_t last_log;

static void on_mute(void)
{
    mute_fired = true;
    bool m = !audio_get_mute();
    audio_set_mute(m);
    ESP_LOGI(TAG, "sound %s", m ? "off" : "on");
}

/* a fresh zero means a fresh stick: whatever was held before it is not held now */
static void on_recentre(void) { dir_x = dir_y = 0; last_axis = 0; }

void pacman_input_init(void)
{
    medal_input_config_t cfg = {};
    cfg.init_i2c = false;              /* the display driver already brings up I2C0 here */
    cfg.imu_init = qmi8658_init;
    cfg.read_accel = qmi8658_read_accel;
    cfg.mute_hold_us = HOLD_MUTE_US;
    cfg.on_mute = on_mute;
    cfg.on_recentre = on_recentre;
    medal_input_init(&cfg);
    ESP_LOGI(TAG, "input ready (BOOT or PWR = coin and start, BOOT held 3 s = sound, "
                  "PWR held 1 s = power off)");
}

void pacman_input_update(void)
{
    medal_input_state_t st;
    medal_input_poll(&st);

    /* BOOT has always been the coin on this medal, and PWR is the coin on every other one, so
     * both work. A release that only happened because the mute gesture fired is not a coin. */
    if (st.boot_released) {
        if (!mute_fired && st.boot_release_held_us < HOLD_MUTE_US) medal_input_insert_coin();
        mute_fired = false;
    }
    coin_active = st.coin;
    start_active = st.start;

    if (st.tilt_fresh) {
        float roll = st.lr * X_SIGN, pitch = st.ud * Y_SIGN;
        /*
         * Four-way, and the axis is sticky. Picking whichever tilt is merely larger hands the
         * moment to left/right whenever a little roll comes along with holding the medal tipped
         * away from you - and then nothing happens at all, because that roll is often below its
         * own threshold. So an axis that clears its threshold alone wins outright, and when
         * both clear it the one already in use keeps it until the other beats it by half again
         * as much. Pac-Man's maze is four-way, so only one ever wins.
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

    current_buttons = 0;
    if (dir_y > 0) current_buttons |= BTN_UP;
    if (dir_y < 0) current_buttons |= BTN_DOWN;
    if (dir_x < 0) current_buttons |= BTN_LEFT;
    if (dir_x > 0) current_buttons |= BTN_RIGHT;
    if (coin_active) current_buttons |= BTN_COIN;

    int64_t now = esp_timer_get_time();
    if (now - last_log >= 3000000) {
        last_log = now;
        ESP_LOGI(TAG, "tilt %d  twist %+6.1f tip %+6.1f -> U%d D%d L%d R%d",
                 st.tilt_valid, (double)st.lr, (double)st.ud,
                 (current_buttons & BTN_UP) ? 1 : 0, (current_buttons & BTN_DOWN) ? 1 : 0,
                 (current_buttons & BTN_LEFT) ? 1 : 0, (current_buttons & BTN_RIGHT) ? 1 : 0);
    }
}

uint8_t pacman_read_in0(void)
{
    /*
     * IN0 port layout (directly from Galagino):
     *   bit 0: UP   bit 1: LEFT   bit 2: RIGHT   bit 3: DOWN   bit 5: COIN
     * Returns active-low (0 = pressed).
     */
    uint8_t retval = 0xFF;
    if (current_buttons & BTN_UP)    retval &= (uint8_t)~0x01;
    if (current_buttons & BTN_LEFT)  retval &= (uint8_t)~0x02;
    if (current_buttons & BTN_RIGHT) retval &= (uint8_t)~0x04;
    if (current_buttons & BTN_DOWN)  retval &= (uint8_t)~0x08;
    if (coin_active)                 retval &= (uint8_t)~0x20;
    return retval;
}

uint8_t pacman_read_in1(void)
{
    /* IN1 bit 5 is 1P START, active low. */
    uint8_t retval = 0xFF;
    if (start_active) retval &= (uint8_t)~0x20;
    return retval;
}
