/*
 * input.cpp - medal controls for Lunar Lander (held upright, like Pac-Man)
 *
 * The buttons, the power rail, the coin-then-start sequence, the mute gesture and the tilt zero
 * all live in components/medal_input, which every medal shares. What is left here is Lunar
 * Lander's own, and it is the one game where the tilt is a genuine analogue control: the
 * cabinet had a spring-loaded throttle lever, and tipping the medal away from you is that
 * lever. Nothing tips, nothing burns; all the way over is full burn.
 *
 *   twist left / right  -> rotate the lander
 *   tip away from you   -> the throttle, from nothing to full burn
 *   BOOT button         -> ABORT (every thruster at once, to save a bad approach); hold 3 s
 *                          for sound off and on, 5 s for the menu. The coin starts the flight
 *                          half a second later, so the mission is the cabinet's default.
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

#define ROT_DEADBAND_DEG    6.0f    /* no rotation inside this much twist */
#define THROTTLE_START_DEG  4.0f    /* the throttle starts to open here */
#define THROTTLE_FULL_DEG  28.0f    /* and is wide open here */
#define ROT_SIGN   (+1.0f)          /* flip if left/right are reversed */
#define THRUST_SIGN (+1.0f)         /* flip if the throttle opens the wrong way */

static int8_t rot_dir;
static uint8_t throttle;
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

/* a fresh zero means the lander stops turning and the throttle closes */
static void on_recentre(void) { rot_dir = 0; throttle = 0; }

void input_init(void)
{
    medal_input_config_t cfg = {};
    cfg.init_i2c = true;
    cfg.imu_init = qmi8658_init;
    cfg.read_accel = qmi8658_read_accel;
    cfg.imu_period_us = 8000;         /* a throttle wants a quicker read than a stick */
    cfg.mute_hold_us = 3000000;
    cfg.on_mute = on_mute;
    cfg.on_recentre = on_recentre;
    cfg.exit_hold_us = MEDALBOOT_EXIT_HOLD_MS * 1000;   /* hold to leave for the menu */
    cfg.on_exit = medalboot_exit_to_menu;
    medal_input_init(&cfg);
}

void input_update(ll_input_t *in)
{
    medal_input_state_t st;
    medal_input_poll(&st);

    if (st.tilt_fresh) {
        float roll = st.lr * ROT_SIGN, pitch = st.ud * THRUST_SIGN;
        dbg_roll = roll; dbg_pitch = pitch;
        rot_dir = (roll > ROT_DEADBAND_DEG) ? +1 : (roll < -ROT_DEADBAND_DEG) ? -1 : 0;
        float t = (pitch - THROTTLE_START_DEG) / (THROTTLE_FULL_DEG - THROTTLE_START_DEG);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        throttle = (uint8_t)(t * 255.0f + 0.5f);
    }

    in->select = 0;
    in->abort_ = st.boot ? 1 : 0;
    in->coin1  = st.coin ? 1 : 0;
    in->start1 = st.start ? 1 : 0;
    in->left   = (rot_dir < 0) ? 1 : 0;
    in->right  = (rot_dir > 0) ? 1 : 0;
    in->thrust = throttle;

    int64_t now = esp_timer_get_time();
    if (now - last_log >= 2000000) {         /* so the controls can be checked over serial */
        last_log = now;
        ESP_LOGI(TAG, "tilt %d  roll %+6.1f pitch %+6.1f -> L%d R%d throttle %3d  abort %d",
                 st.tilt_valid, (double)dbg_roll, (double)dbg_pitch,
                 in->left, in->right, in->thrust, in->abort_);
    }
}
