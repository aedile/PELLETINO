/*
 * battery.c - rail control, fuel gauge and over-discharge cutoff.
 *
 * Gauge ported from NESTOR's medal.c. The cutoff is new: NESTOR reports a
 * percentage but never acts on it, so a medal left in a drawer keeps drawing
 * until the cell is damaged. A single lithium cell should not be taken below
 * about 3.0 V, and this board carries no protection circuit of its own.
 */
#include "battery.h"
#include "chiptune.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "battery";

#define PIN_BAT_EN     GPIO_NUM_15
#define BAT_ADC_CH     ADC_CHANNEL_0     /* GPIO0, behind the board's divider */
#define DIVIDER        3                 /* ponytail: Waveshare's own example uses 3 for this
                                          * board; tune here if the reading reads off */
#define EMPTY_MV       3300              /* 0% on the gauge */
#define FULL_MV        4200              /* 100% */
#define CUTOFF_MV      3250              /* below this we stop, before the cell is hurt */
#define CUTOFF_STRIKES 3                 /* consecutive low reads before acting */
#define SAMPLE_US      5000000

static void sample(void);

static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t cali;
static int last_mv, pct = -1, strikes;

void battery_init(void)
{
    gpio_config_t bat = {};
    bat.pin_bit_mask = 1ULL << PIN_BAT_EN;
    bat.mode = GPIO_MODE_OUTPUT;
    gpio_config(&bat);
    gpio_set_level(PIN_BAT_EN, 1);        /* hold the rail up before anything else */

    adc_oneshot_unit_init_cfg_t u = {};
    u.unit_id = ADC_UNIT_1;
    if (adc_oneshot_new_unit(&u, &adc) != ESP_OK) {
        ESP_LOGW(TAG, "no ADC: running without a gauge");
        adc = NULL;
        return;
    }
    adc_oneshot_chan_cfg_t c = {};
    c.atten = ADC_ATTEN_DB_12;
    c.bitwidth = ADC_BITWIDTH_DEFAULT;
    adc_oneshot_config_channel(adc, BAT_ADC_CH, &c);

    adc_cali_curve_fitting_config_t cc = {};
    cc.unit_id = ADC_UNIT_1;
    cc.chan = BAT_ADC_CH;
    cc.atten = ADC_ATTEN_DB_12;
    cc.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&cc, &cali) != ESP_OK) cali = NULL;

    sample();
    ESP_LOGI(TAG, "%d mV, %d%% (cutoff %d mV, divider %d, cal %s)",
             last_mv, pct, CUTOFF_MV, DIVIDER, cali ? "curve" : "linear");
}

bool battery_present(void) { return adc != NULL; }

static void sample(void)
{
    int raw = 0, mv = 0;
    if (!adc || adc_oneshot_read(adc, BAT_ADC_CH, &raw) != ESP_OK) return;
    if (!cali || adc_cali_raw_to_voltage(cali, raw, &mv) != ESP_OK) mv = raw * 3300 / 4095;
    last_mv = mv * DIVIDER;

    int p = (last_mv - EMPTY_MV) * 100 / (FULL_MV - EMPTY_MV);
    pct = p < 0 ? 0 : p > 100 ? 100 : p;
}

int battery_percent(void)
{
    static int64_t last;
    int64_t now = esp_timer_get_time();
    if (adc && (pct < 0 || now - last > SAMPLE_US)) { last = now; sample(); }
    return pct < 0 ? 100 : pct;      /* no gauge: don't show a scary empty meter */
}

int battery_mv(void) { battery_percent(); return last_mv; }

void battery_tick(void)
{
    if (!adc) return;
    int before = pct;
    battery_percent();
    if (pct == before && last_mv == 0) return;

    /* Say so before it runs out: once as it gets low, once more when there are
     * minutes left. Three readings in a row, because one can be a sag. */
    static int warned, run, seen_mv;
    int level = pct < 0 ? 0 : pct <= BATT_CRIT_PCT ? 2 : pct <= BATT_LOW_PCT ? 1 : 0;
    if (last_mv != seen_mv) {
        seen_mv = last_mv;
        run = level ? run + 1 : 0;
        if (pct > BATT_LOW_PCT + 5) warned = 0;         /* it has been charged since */
    }
    if (level > warned && run >= 3) {
        warned = level;
        display_toast(level == 2 ? "BATTERY DYING" : "BATTERY LOW", 3000);
        chip_sfx(CHIP_SFX_LOW);
        ESP_LOGW(TAG, "%d%%: warned", pct);
    }

    /* A big SPI push or a loud passage sags the rail for a few milliseconds. Only
     * a run of low readings, seconds apart, means the pack is actually flat. */
    if (last_mv && last_mv < CUTOFF_MV) {
        if (++strikes >= CUTOFF_STRIKES) {
            ESP_LOGW(TAG, "%d mV: cutting power to protect the cell", last_mv);
            battery_power_off();
        }
    } else {
        strikes = 0;
    }
}

void battery_power_off(void)
{
    ESP_LOGI(TAG, "power off");
    display_set_backlight(0);
    gpio_set_level(PIN_BAT_EN, 0);
    /* On USB the rail stays up regardless: sit dark rather than spin. */
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
