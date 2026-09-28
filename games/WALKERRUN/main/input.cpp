/*
 * input.cpp - medal controls for Star Wars
 *   tilt (QMI8658 IMU) -> flight yoke pitch/yaw
 *   BOOT button (GPIO9)  -> fire (also starts a game in free play)
 *                           with PWR, together: sound on/off
 *                           held 13 s: the easter egg (input_take_gesture)
 *   PWR button (GPIO18)  -> short press: coin, long press (1 s): power off
 *   both together      -> sound off and on (the same everywhere on the medal)
 */
#include "input.h"
#include "qmi8658.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <math.h>

static const char *TAG = "INPUT";

#define PIN_BTN_BOOT    GPIO_NUM_9
#define PIN_BTN_PWR     GPIO_NUM_18
#define PIN_BAT_EN      GPIO_NUM_15   /* must stay HIGH to keep battery power on */

#define PWR_LONG_PRESS_MS 1000
#define IMU_PERIOD_US 16000           /* poll the accelerometer at ~60 Hz, not every loop */
#define FULL_DEFLECTION_DEG 20.0f     /* this much tilt = yoke at its stop */
#define DEADBAND_DEG 1.5f

/* Sign of each axis; flip on hardware if the ship steers the wrong way */
#define YAW_SIGN   (+1.0f)
#define PITCH_SIGN (+1.0f)

static int64_t pwr_down_since;
static bool pwr_was_down;
static bool imu_ok;
static int64_t imu_last_us;
static float g0_yaw_ang, g0_pitch_ang;    /* neutral pose */
static bool have_neutral;
static bool fire_was_down;
static int64_t fire_down_since;
static bool fire_hold_consumed;           /* the 13 s gesture fired; ignore the release */
static int pending_gesture = GESTURE_NONE;
#define HOLD_SOUND_US   3000000
#define HOLD_EGG_US    13000000

/* The medal is held sideways (landscape) and roughly upright, screen facing the
 * player, like a yoke. Gravity then lies mostly along the panel's short axis
 * (accelerometer X). Steering rotates gravity within the X/Y plane, pitching the
 * nose rotates it within the X/Z plane; both are angles we can take with atan2
 * relative to a neutral pose captured when the player first presses fire. */
static void read_angles(float *yaw_ang, float *pitch_ang)
{
    int16_t ax, ay, az;
    qmi8658_read_accel(&ax, &ay, &az);
    /* steering: direction of gravity within the panel plane (any grip that isn't flat)
     * pitch: how far gravity leaves the panel plane, i.e. tipping the top edge toward/away */
    float in_plane = sqrtf((float)ax * ax + (float)ay * ay);
    *yaw_ang = atan2f((float)ay, (float)ax) * 57.2958f;
    *pitch_ang = atan2f((float)az, in_plane) * 57.2958f;
}

static float wrap_deg(float d)
{
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

static uint8_t angle_to_adc(float deg, float sign)
{
    if (deg > -DEADBAND_DEG && deg < DEADBAND_DEG) deg = 0.0f;
    float v = 128.0f + sign * deg * (127.0f / FULL_DEFLECTION_DEG);
    if (v < 0.0f) v = 0.0f;
    if (v > 255.0f) v = 255.0f;
    return (uint8_t)v;
}

static void capture_neutral(void)
{
    if (!imu_ok) return;
    read_angles(&g0_yaw_ang, &g0_pitch_ang);
    have_neutral = true;
    ESP_LOGI(TAG, "neutral pose captured: yaw %.1f pitch %.1f", g0_yaw_ang, g0_pitch_ang);
}

void input_init(void)
{
    gpio_config_t bat = {};
    bat.pin_bit_mask = 1ULL << PIN_BAT_EN;
    bat.mode = GPIO_MODE_OUTPUT;
    gpio_config(&bat);
    gpio_set_level(PIN_BAT_EN, 1);

    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << PIN_BTN_BOOT) | (1ULL << PIN_BTN_PWR);
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io);

    /* I2C bus shared by the IMU (and the audio codec later) */
    i2c_config_t i2c = {};
    i2c.mode = I2C_MODE_MASTER;
    i2c.sda_io_num = GPIO_NUM_8;
    i2c.scl_io_num = GPIO_NUM_7;
    i2c.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c.master.clk_speed = 100000;
    i2c_param_config(I2C_NUM_0, &i2c);
    esp_err_t err = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "I2C init failed: %s", esp_err_to_name(err));
    }

    imu_ok = qmi8658_init();
    if (imu_ok) {
        ESP_LOGI(TAG, "IMU tilt control enabled (neutral pose set on first fire press, or PWR short press)");
    } else {
        ESP_LOGW(TAG, "IMU not available");
    }
}

void input_update(sw_input_t *in)
{
    int64_t now = esp_timer_get_time();
    bool boot = gpio_get_level(PIN_BTN_BOOT) == 0;
    bool pwr = gpio_get_level(PIN_BTN_PWR) == 0;

    in->fire = boot;
    if (boot && !fire_was_down) {
        if (!have_neutral) capture_neutral();
        fire_down_since = now;
        fire_hold_consumed = false;
    }
    if (boot && !fire_hold_consumed && now - fire_down_since >= HOLD_EGG_US) {
        pending_gesture = GESTURE_EGG;
        fire_hold_consumed = true;
    }
    if (!boot && fire_was_down && !fire_hold_consumed) {
        int64_t held = now - fire_down_since;
        if (held >= HOLD_SOUND_US && held < HOLD_EGG_US) pending_gesture = GESTURE_TOGGLE_SOUND;
    }
    fire_was_down = boot;

    /* PWR: short press = coin + recapture neutral pose; long press = power off */
    in->coin1 = 0;
    if (pwr && !pwr_was_down) pwr_down_since = now;
    if (pwr && now - pwr_down_since >= PWR_LONG_PRESS_MS * 1000) {
        ESP_LOGI(TAG, "power off");
        gpio_set_level(PIN_BAT_EN, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (!pwr && pwr_was_down && now - pwr_down_since < 400000) {
        in->coin1 = 1;                /* released after a short press */
        capture_neutral();
    }
    pwr_was_down = pwr;

    if (imu_ok && now - imu_last_us >= IMU_PERIOD_US) {
        imu_last_us = now;
        float yaw_ang, pitch_ang;
        read_angles(&yaw_ang, &pitch_ang);
        if (!have_neutral) {
            in->yaw = 0x80;
            in->pitch = 0x80;
        } else {
            in->yaw = angle_to_adc(wrap_deg(yaw_ang - g0_yaw_ang), YAW_SIGN);
            in->pitch = angle_to_adc(wrap_deg(pitch_ang - g0_pitch_ang), PITCH_SIGN);
        }
    }
}

int input_take_gesture(void)
{
    int g = pending_gesture;
    pending_gesture = GESTURE_NONE;
    return g;
}
