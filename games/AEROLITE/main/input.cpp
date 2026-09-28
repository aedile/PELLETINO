/*
 * input.cpp - medal controls for Asteroids (held upright, like Pac-Man)
 *
 * The buttons, the power rail, the coin-then-start sequence, the mute gesture and the tilt zero
 * all live in components/medal_input, which every medal shares. What is left here is Asteroids'
 * own: a ship that rotates, thrusts, fires and jumps.
 *
 *   twist left / right  -> rotate
 *   tip away from you   -> thrust
 *   BOOT button         -> fire; hold 0.7 s for hyperspace, 3 s for sound off and on
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

#define ROT_DEADBAND_DEG    5.0f    /* no rotation inside this much tilt */
#define THRUST_DEG         12.0f    /* tip this far away from you to thrust */
#define ROT_SIGN   (+1.0f)          /* flip if left/right are reversed */
#define THRUST_SIGN (+1.0f)         /* flip if thrust triggers the wrong way */
#define HOLD_HYPER_US 700000        /* hold the button this long for hyperspace */

static int8_t rot_dir;
static bool thrusting;
static float dbg_roll, dbg_pitch;
static int64_t last_log;

static void on_mute(void)
{
    bool muted = !audio_get_mute();
    audio_set_mute(muted);
    medalboot_set_muted(muted);                 /* holds in the menu and every other game */
    display_toast(muted ? "SOUND OFF" : "SOUND ON", 1500);
    ESP_LOGI(TAG, "sound %s", muted ? "off" : "on");
}

/* a fresh zero means the ship stops turning and coasting */
static void on_recentre(void) { rot_dir = 0; thrusting = false; }

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

void input_update(ast_input_t *in)
{
    medal_input_state_t st;
    medal_input_poll(&st);

    if (st.tilt_fresh) {
        float roll = st.lr * ROT_SIGN, pitch = st.ud * THRUST_SIGN;
        dbg_roll = roll; dbg_pitch = pitch;
        rot_dir = (roll > ROT_DEADBAND_DEG) ? +1 : (roll < -ROT_DEADBAND_DEG) ? -1 : 0;
        thrusting = pitch > THRUST_DEG;
    }

    /*
     * Fire is a level, not a pulse. The main loop runs many times per emulated frame, so a
     * press raised and cleared on one pass could land entirely inside iterations that emulated
     * nothing - which is what made shooting feel unreliable. The cabinet wires it as a level,
     * so this does too. Holding past the threshold is hyperspace, and drops fire while it is
     * held so you do not shoot on the way out.
     */
    bool hyper_held = st.boot && st.boot_held_us >= HOLD_HYPER_US;
    in->fire       = (st.boot && !hyper_held) ? 1 : 0;
    in->hyperspace = hyper_held ? 1 : 0;
    in->coin1      = st.coin ? 1 : 0;
    in->start1     = st.start ? 1 : 0;
    in->left       = (rot_dir < 0) ? 1 : 0;
    in->right      = (rot_dir > 0) ? 1 : 0;
    in->thrust     = thrusting ? 1 : 0;

    int64_t now = esp_timer_get_time();
    if (now - last_log >= 2000000) {         /* so the controls can be checked over serial */
        last_log = now;
        ESP_LOGI(TAG, "tilt %d  roll %+6.1f pitch %+6.1f -> L%d R%d T%d  fire %d hyper %d",
                 st.tilt_valid, (double)dbg_roll, (double)dbg_pitch,
                 in->left, in->right, in->thrust, in->fire, in->hyperspace);
    }
}
