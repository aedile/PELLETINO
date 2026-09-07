/*
 * medal_input.cpp - buttons, power, and the tilt zero, shared by every medal.
 * See medal_input.h for what this is and why the pose has to be checked.
 */
#include "medal_input.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char *TAG = "MEDAL_IN";

#define PIN_BTN_BOOT GPIO_NUM_9
#define PIN_BTN_PWR  GPIO_NUM_18
#define PIN_BAT_EN   GPIO_NUM_15
#define PIN_I2C_SDA  GPIO_NUM_8
#define PIN_I2C_SCL  GPIO_NUM_7

static medal_input_config_t cfg;
static bool imu_ok;

static bool have_neutral;
static float neutral_lr, neutral_ud;

static int64_t imu_last_us, pwr_down_since, boot_down_since, coin_seq_start;
static bool pwr_was_down, boot_was_down;
static int  coin_seq;                 /* 0 idle, 1 coin held, 2 gap, 3 start held */
static bool hold_armed, hold_fired;
static int64_t hold_since;

/* the last trusted reading, held so a momentary bad pose does not jerk the controls */
static float held_lr, held_ud;
static bool  held_valid;

float medal_wrap_deg(float d)
{
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

/*
 * One reading, and whether it can be trusted. Returns false when gravity is not in the panel's
 * plane - the medal is lying flat, or nearly - because the in-plane angle is then noise and any
 * zero taken from it is a centre the player was never holding.
 */
static bool read_angles(float *lr, float *ud)
{
    if (!imu_ok || !cfg.read_accel) return false;
    int16_t ax, ay, az;
    cfg.read_accel(&ax, &ay, &az);
    float in_plane = sqrtf((float)ax * ax + (float)ay * ay);
    *lr = atan2f((float)ay, (float)ax) * 57.2958f;
    *ud = atan2f((float)az, in_plane) * 57.2958f;
    return in_plane > 1.2f * fabsf((float)az);      /* held up, not lying down */
}

bool medal_input_recentre(void)
{
    float lr, ud;
    if (!read_angles(&lr, &ud)) return false;       /* try again next time */
    neutral_lr = lr; neutral_ud = ud;
    have_neutral = true;
    held_lr = held_ud = 0.0f; held_valid = true;
    if (cfg.on_recentre) cfg.on_recentre();
    return true;
}

void medal_input_insert_coin(void)
{
    if (coin_seq != 0) return;
    coin_seq = 1;
    coin_seq_start = esp_timer_get_time();
    medal_input_recentre();
}

bool medal_input_have_neutral(void) { return have_neutral; }
bool medal_input_imu_ok(void) { return imu_ok; }

void medal_input_power_off(void)
{
    ESP_LOGI(TAG, "power off");
    gpio_set_level(PIN_BAT_EN, 0);
    vTaskDelay(pdMS_TO_TICKS(1000));
}

void medal_input_init(const medal_input_config_t *c)
{
    cfg = *c;
    if (!cfg.imu_period_us)     cfg.imu_period_us = 16000;
    if (!cfg.power_off_hold_us) cfg.power_off_hold_us = 1000000;
    if (!cfg.coin_us)           cfg.coin_us = 100000;
    if (!cfg.gap_us)            cfg.gap_us = 400000;
    if (!cfg.start_us)          cfg.start_us = 100000;

    have_neutral = false; held_valid = false;
    coin_seq = 0; pwr_was_down = boot_was_down = false;
    hold_armed = hold_fired = false;
    imu_last_us = pwr_down_since = boot_down_since = coin_seq_start = hold_since = 0;

    gpio_config_t bat = {};
    bat.pin_bit_mask = 1ULL << PIN_BAT_EN;
    bat.mode = GPIO_MODE_OUTPUT;
    gpio_config(&bat);
    gpio_set_level(PIN_BAT_EN, 1);                  /* hold the battery rail up */

    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << PIN_BTN_BOOT) | (1ULL << PIN_BTN_PWR);
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io);

    imu_ok = false;
    if (cfg.init_i2c) {
        i2c_config_t i2c = {};
        i2c.mode = I2C_MODE_MASTER;
        i2c.sda_io_num = PIN_I2C_SDA;
        i2c.scl_io_num = PIN_I2C_SCL;
        i2c.sda_pullup_en = GPIO_PULLUP_ENABLE;
        i2c.scl_pullup_en = GPIO_PULLUP_ENABLE;
        i2c.master.clk_speed = 100000;
        i2c_param_config(I2C_NUM_0, &i2c);
        esp_err_t err = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
            ESP_LOGW(TAG, "I2C init failed: %s", esp_err_to_name(err));
    }

    if (cfg.imu_init) imu_ok = cfg.imu_init();
    else              imu_ok = cfg.read_accel != NULL;

    ESP_LOGI(TAG, "input ready (IMU %s); the tilt zero is taken the first time the medal is "
                  "held up, and again on each coin and start", imu_ok ? "ok" : "missing");
}

/*
 * The two things a long press can mean. Holding a button for a few seconds toggles the sound -
 * this is a thing people wear places, and some of those places need to be quiet - and holding
 * it a good deal longer leaves the game for the menu.
 *
 * When both are configured the sound has to wait for the release, because a hold long enough to
 * leave passes through the shorter one on its way. With no exit gesture there is nothing to
 * pass through, and the sound fires the moment it is due, as it always has.
 */
static void hold_gestures(bool boot, int64_t now)
{
    if (!cfg.mute_hold_us && !cfg.exit_hold_us) return;

    if (boot) {
        if (!hold_armed) { hold_armed = true; hold_fired = false; hold_since = now; }
        int64_t held = now - hold_since;
        if (cfg.exit_hold_us && !hold_fired && held >= (int64_t)cfg.exit_hold_us) {
            hold_fired = true;
            if (cfg.on_exit) cfg.on_exit();
        } else if (!cfg.exit_hold_us && cfg.mute_hold_us && !hold_fired &&
                   held >= (int64_t)cfg.mute_hold_us) {
            hold_fired = true;
            if (cfg.on_mute) cfg.on_mute();
        }
        return;
    }

    if (hold_armed && !hold_fired && cfg.exit_hold_us && cfg.mute_hold_us &&
        now - hold_since >= (int64_t)cfg.mute_hold_us && cfg.on_mute)
        cfg.on_mute();
    hold_armed = false;
}

void medal_input_poll(medal_input_state_t *st)
{
    int64_t now = esp_timer_get_time();
    memset(st, 0, sizeof(*st));

    bool boot = gpio_get_level(PIN_BTN_BOOT) == 0;
    bool pwr  = gpio_get_level(PIN_BTN_PWR) == 0;
    st->boot = boot; st->pwr = pwr;

    if (boot && !boot_was_down) boot_down_since = now;
    if (!boot && boot_was_down) { st->boot_released = true; st->boot_release_held_us = now - boot_down_since; }
    st->boot_held_us = boot ? now - boot_down_since : 0;
    boot_was_down = boot;

    hold_gestures(boot, now);

    if (pwr && !pwr_was_down) pwr_down_since = now;
    if (!pwr && pwr_was_down) { st->pwr_released = true; st->pwr_release_held_us = now - pwr_down_since; }
    st->pwr_held_us = pwr ? now - pwr_down_since : 0;
    if (pwr && now - pwr_down_since >= (int64_t)cfg.power_off_hold_us) medal_input_power_off();
    /* a short press is a coin; anything longer was on its way to being a power off */
    if (!cfg.manual_pwr && st->pwr_released && st->pwr_release_held_us < 400000 && coin_seq == 0) {
        coin_seq = 1; coin_seq_start = now;
        medal_input_recentre();               /* a coin re-centres however you are holding it */
    }
    pwr_was_down = pwr;

    int64_t el = now - coin_seq_start;
    switch (coin_seq) {
        case 1: st->coin = true;  if (el > (int64_t)cfg.coin_us) coin_seq = 2; break;
        case 2: if (el > (int64_t)(cfg.coin_us + cfg.gap_us)) {
                    coin_seq = 3;
                    medal_input_recentre();   /* settled into playing posture by now */
                }
                break;
        case 3: st->start = true; if (el > (int64_t)(cfg.coin_us + cfg.gap_us + cfg.start_us)) coin_seq = 0; break;
        default: break;
    }

    /* The IMU is only read on its own schedule, and only ever acted on while the medal is
     * actually being held up. When it is not, the last trusted angles are held rather than
     * letting the controls swing on noise. */
    float lr, ud;
    bool due = now - imu_last_us >= (int64_t)cfg.imu_period_us;
    if (due) imu_last_us = now;
    if (due && read_angles(&lr, &ud) && (have_neutral || medal_input_recentre())) {
        held_lr = medal_wrap_deg(lr - neutral_lr);
        held_ud = medal_wrap_deg(ud - neutral_ud);
        held_valid = true;
        st->tilt_fresh = true;
    }
    st->tilt_valid = held_valid;
    st->lr = held_lr;
    st->ud = held_ud;
}
