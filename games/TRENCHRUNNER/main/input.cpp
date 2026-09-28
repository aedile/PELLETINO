/*
 * input.cpp - medal controls for Star Wars
 *
 * The buttons, the power rail, the coin, the power-off hold and the tilt zero all live in
 * components/medal_input, which every medal shares. What is left here is Star Wars' own: a
 * flight yoke.
 *
 *   tilt                -> the yoke: twisting yaws, tipping pitches
 *   BOOT button         -> fire (also starts a game in free play)
 *                          held 3 s and released: sound on and off
 *                          held 10 s: back to the MINIMAME menu
 *   PWR short press     -> coin; long press (1 s) -> power off
 *
 * The sound and exit holds are medal_input's own now. They used to be hand-rolled here because
 * a thirteen-second easter egg sat on the same button and there was no room for a third hold;
 * the egg is gone, so this is the same gesture set every other medal has.
 */
#include "input.h"
#include "medal_input.h"
#include "display.h"
#include "medalboot.h"
#include "qmi8658.h"
#include "audio_hal.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "INPUT";

#define FULL_DEFLECTION_DEG 20.0f     /* this much tilt = yoke at its stop */
#define DEADBAND_DEG 1.5f


/* Sign of each axis; flip on hardware if the ship steers the wrong way */
#define YAW_SIGN   (+1.0f)
#define PITCH_SIGN (+1.0f)

static bool human_active;
bool input_human_active(void) { return human_active; }

static uint8_t angle_to_adc(float deg, float sign)
{
    if (deg > -DEADBAND_DEG && deg < DEADBAND_DEG) deg = 0.0f;
    float v = 128.0f + sign * deg * (127.0f / FULL_DEFLECTION_DEG);
    if (v < 0.0f) v = 0.0f;
    if (v > 255.0f) v = 255.0f;
    return (uint8_t)v;
}

static void on_mute(void)
{
    bool muted = !audio_get_mute();
    audio_set_mute(muted);
    medalboot_set_muted(muted);                 /* holds in the menu and every other game */
    display_toast(muted ? "SOUND OFF" : "SOUND ON", 1500);
    ESP_LOGI(TAG, "sound %s", muted ? "off" : "on");
}

void input_init(void)
{
    medal_input_config_t cfg = {};
    cfg.init_i2c = true;
    cfg.imu_init = qmi8658_init;
    cfg.read_accel = qmi8658_read_accel;
    cfg.mute_hold_us = 3000000;
    cfg.on_mute = on_mute;
    cfg.exit_hold_us = MEDALBOOT_EXIT_HOLD_MS * 1000;   /* hold to leave for the menu */
    cfg.on_exit = medalboot_exit_to_menu;
    medal_input_init(&cfg);
}

void input_update(sw_input_t *in)
{
    medal_input_state_t st;
    medal_input_poll(&st);

    in->fire  = st.boot;
    in->coin1 = st.coin ? 1 : 0;
    /* only the trigger takes the controls back from the autopilot. Tilt does not: the medal is
     * worn and carried, and every step would have handed a game to someone who was not
     * playing it. */
    human_active = st.boot;

    if (st.tilt_valid) {
        in->yaw   = angle_to_adc(st.lr, YAW_SIGN);
        in->pitch = angle_to_adc(st.ud, PITCH_SIGN);
    } else {
        in->yaw = 0x80;
        in->pitch = 0x80;
    }
}

