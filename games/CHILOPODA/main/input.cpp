/*
 * input.cpp - medal controls for Centipede (held upright, like Pac-Man)
 *
 * The buttons, the power rail, the coin-then-start sequence, the mute gesture and the tilt zero
 * all live in components/medal_input, which every medal shares. What is left here is the part
 * that is this game's own: turning two angles into a trackball.
 *
 * Centipede is a trackball game, so the tilt drives the ball's counters rather than a joystick's
 * switches: the angle sets a speed, which is the closest a tilt sensor gets to something you
 * spin. Fractional counts accumulate, so even a slow lean keeps moving, one count at a time.
 *
 *   twist left / right  -> the ball, left and right
 *   tip away / toward   -> the ball, up and down
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

#define DEADBAND_DEG   3.0f    /* no movement inside this much tilt */
#define FULL_DEG      22.0f    /* this much tilt = full speed */
#define MAX_COUNTS     4.0f    /* trackball counts per frame at full speed; the game caps near here */
#define X_SIGN (+1.0f)         /* flip if left/right are reversed */
#define Y_SIGN (+1.0f)         /* flip if up/down are reversed */

static int16_t acc_x, acc_y;             /* 8.8 fixed point: counts owed but not yet delivered */

static void on_mute(void)
{
    medalboot_sound_t s = medalboot_sound_next();      /* loud, quiet, off - and it holds everywhere */
    audio_set_volume(medalboot_sound_volume(s));
    audio_set_mute(s == MEDALBOOT_SOUND_OFF);
    display_toast(medalboot_sound_name(s), 1500);
    ESP_LOGI(TAG, "%s", medalboot_sound_name(s));
}

/* a fresh zero means the ball starts from a standstill: drop whatever counts were owed */
static void on_recentre(void) { acc_x = acc_y = 0; }

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

/* tilt angle -> trackball counts per frame, in 8.8 fixed point */
static int16_t rate_from_angle(float deg, float sign)
{
    float mag = fabsf(deg);
    if (mag <= DEADBAND_DEG) return 0;
    float f = (mag - DEADBAND_DEG) / (FULL_DEG - DEADBAND_DEG);
    if (f > 1.0f) f = 1.0f;
    float counts = f * MAX_COUNTS * (deg < 0 ? -1.0f : 1.0f) * sign;
    return (int16_t)(counts * 256.0f);
}

void input_update(ce_input_t *in)
{
    medal_input_state_t st;
    medal_input_poll(&st);

    in->coin1  = st.coin ? 1 : 0;
    in->start1 = st.start ? 1 : 0;
    in->fire = st.boot ? 1 : 0;

    if (st.tilt_fresh) {
        acc_x += rate_from_angle(st.lr, X_SIGN);
        acc_y += rate_from_angle(st.ud, Y_SIGN);
        /* hand over whole counts and keep the remainder for next time */
        in->track_x = (int8_t)(acc_x >> 8); acc_x -= (int16_t)(in->track_x << 8);
        in->track_y = (int8_t)(acc_y >> 8); acc_y -= (int16_t)(in->track_y << 8);
    } else {
        in->track_x = 0; in->track_y = 0;
    }
}
