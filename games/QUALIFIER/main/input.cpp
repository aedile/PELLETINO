/*
 * input.cpp - medal controls for Pole Position (held sideways, like a wheel)
 *
 * The battery rail, the buttons and the tilt zero live in components/medal_input, which every
 * medal shares. Pole Position takes two things into its own hands:
 *
 *   The car drives itself forward. In Pole Position you hold the throttle down for the whole
 *   race anyway, so making it automatic frees the BOOT button and lets every gesture match the
 *   rest of the medals - which is the whole point, since holding BOOT for the throttle was the
 *   one thing that made this game's controls different from all the others.
 *
 *   tilt (rotate it like a wheel) -> steering
 *   accelerator                   -> automatic (held down for you)
 *   BOOT tap                      -> shift gear (low <-> high)
 *   BOOT + PWR                     -> sound off and on   (standard)
 *   BOOT hold 10 s                -> back to the menu    (standard)
 *   PWR short press               -> coin, then start    (standard)
 *   PWR hold 1 s                  -> power off            (standard)
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
#define FULL_LOCK_DEG 30.0f       /* tilt for full wheel deflection */
#define FULL_LOCK_COUNTS 12.0f    /* wheel counts at full deflection: 8 counts is already a hard swerve */
#define DEADBAND_DEG 1.5f
#define STEER_SIGN (-1.0f)
#define PEDAL_FROM_US 8000000     /* hold the pedal up this long after boot - see input_update */

static int64_t coin_until, both_down_since;
static bool both_armed, both_fired;
uint32_t input_dbg_presses[2];           /* BOOT, PWR press edges since boot (diagnostics) */
uint8_t input_dbg_levels;                /* raw levels: bit0 BOOT, bit1 PWR (1 = released) */
int16_t input_dbg_accel[3]; float input_dbg_angle; uint8_t input_dbg_steer; uint8_t input_dbg_neutral;

static void read_accel_dbg(int16_t *x, int16_t *y, int16_t *z)
{
    qmi8658_read_accel(x, y, z);
    input_dbg_accel[0] = *x; input_dbg_accel[1] = *y; input_dbg_accel[2] = *z;
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
    cfg.read_accel = read_accel_dbg;
    cfg.mute_hold_us = 3000000;       /* BOOT is free now, so mute is the standard 3 s hold */
    cfg.on_mute = on_mute;
    cfg.exit_hold_us = MEDALBOOT_EXIT_HOLD_MS * 1000;   /* hold to leave for the menu */
    cfg.on_exit = medalboot_exit_to_menu;
    medal_input_init(&cfg);
}

void input_update(pp_input_t *in)
{
    medal_input_state_t st;
    medal_input_poll(&st);
    int64_t now = esp_timer_get_time();

    if (st.boot && st.boot_held_us == 0) input_dbg_presses[0]++;
    if (st.pwr && st.pwr_held_us == 0) input_dbg_presses[1]++;
    input_dbg_levels = (uint8_t)((st.boot ? 0 : 1) | (st.pwr ? 0 : 2));

    /*
     * Automatic throttle - you hold it the whole race anyway. But not from power-on: the game
     * samples the pedal as it comes out of its self-test and treats whatever it sees as
     * "released", so a pedal already down at that moment reads as zero for ever after and the
     * car never moves. Keep it up until the attract mode is on screen, then hold it down for
     * good; a second game after GAME OVER is fine with it down throughout (checked on the host).
     */
    in->accel = (now > PEDAL_FROM_US) ? 0x90 : 0;
    in->brake = 0;

    /* a short tap of BOOT shifts gear; longer holds are mute (3 s) and exit (10 s), which
     * medal_input handles, so only a genuine tap counts here */
    if (st.boot_released && st.boot_release_held_us < 400000) {
        in->gear = !in->gear;
        ESP_LOGI(TAG, "gear %s", in->gear ? "high" : "low");
    }

    in->coin1 = st.coin ? 1 : 0;     /* PWR short press; Pole Position free-play starts on the coin */

    if (st.tilt_valid) {
        float d = st.lr;
        if (d > -DEADBAND_DEG && d < DEADBAND_DEG) d = 0;
        float v = 128.0f + STEER_SIGN * d * (FULL_LOCK_COUNTS / FULL_LOCK_DEG);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        in->steer = (uint8_t)v;
    } else {
        in->steer = 128;
    }
    input_dbg_angle = st.lr;
    input_dbg_steer = in->steer;
    input_dbg_neutral = medal_input_have_neutral();
}
