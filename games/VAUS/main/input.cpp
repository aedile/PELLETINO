/*
 * input.cpp - medal controls for Arkanoid (held upright, like Pac-Man)
 *
 * The buttons, the power rail, the coin-then-start sequence, the mute gesture and the tilt zero
 * all live in components/medal_input, which every medal shares. What is left here is Arkanoid's
 * own: a spinner.
 *
 * Arkanoid's cabinet had one, and the closest thing a medal has is a twist - rotate it in your
 * fingers rather than leaning it. The angle maps straight onto an absolute paddle position, so
 * where you hold the medal is where the Vaus is: no velocity to build up, no drift to fight.
 *
 *   twist left / right  -> the paddle, absolutely, across a 32 degree sweep each way
 *   BOOT button         -> fire, once the laser is fitted
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

#define PADDLE_SWEEP_DEG 32.0f  /* this much twist each way covers the whole playfield */
#define PADDLE_SMOOTH    0.35f  /* how much of each new reading to take; the rest is history */
#define X_SIGN (-1.0f)          /* flip if left/right are reversed */

static int16_t paddle_pos = 128;
static float paddle_f = 128.0f;
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

/* a fresh zero puts the Vaus back in the middle, filter and all */
static void on_recentre(void) { paddle_pos = 128; paddle_f = 128.0f; }

void input_init(void)
{
    medal_input_config_t cfg = {};
    cfg.init_i2c = true;
    cfg.imu_init = qmi8658_init;
    cfg.read_accel = qmi8658_read_accel;
    cfg.imu_period_us = 5000;           /* a paddle wants a faster hand than a joystick does */
    cfg.mute_hold_us = 3000000;
    cfg.on_mute = on_mute;
    cfg.on_recentre = on_recentre;
    cfg.exit_hold_us = MEDALBOOT_EXIT_HOLD_MS * 1000;   /* hold to leave for the menu */
    cfg.on_exit = medalboot_exit_to_menu;
    medal_input_init(&cfg);
}

void input_update(ak_input_t *in)
{
    medal_input_state_t st;
    medal_input_poll(&st);

    if (st.tilt_fresh) {
        float roll = st.lr * X_SIGN;
        dbg_roll = roll;
        /*
         * The accelerometer is noisy at the couple-of-degrees level and a paddle shows every
         * bit of that, so the position is filtered rather than taken raw - otherwise the Vaus
         * jitters in your hand even when you are holding still.
         */
        float f = roll / PADDLE_SWEEP_DEG;
        if (f < -1.0f) f = -1.0f; else if (f > 1.0f) f = 1.0f;
        float target = 128.0f + f * 127.0f;
        paddle_f += (target - paddle_f) * PADDLE_SMOOTH;
        paddle_pos = (int16_t)(paddle_f + 0.5f);
    }

    in->paddle = paddle_pos;
    in->fire   = st.boot ? 1 : 0;
    in->coin1  = st.coin ? 1 : 0;
    in->start1 = st.start ? 1 : 0;

    int64_t now = esp_timer_get_time();
    if (now - last_log >= 3000000) {
        last_log = now;
        ESP_LOGI(TAG, "tilt %d  twist %+6.1f -> paddle %3d fire %d",
                 st.tilt_valid, (double)dbg_roll, (int)in->paddle, in->fire);
    }
}
