/*
 * HADOUKEN - a video clip on the Waveshare ESP32-C6-LCD-1.69 Fiesta medal.
 *
 * Not an emulator. Street Fighter II is a 10 MHz 68000 driving five megabytes of graphics
 * through hardware this board cannot approach, so this plays the attract mode instead: an
 * MJPEG + MP3 clip out of the "media" partition, letterboxed 4:3 on the portrait panel, on a
 * loop. The player is the one that used to be Star Wars' easter egg. Anyone building their
 * own medal can pack whatever clip they like with tools/pack_media.py.
 *
 *   BOOT + PWR       -> sound off and on
 *   BOOT hold 5 s  -> back to the MINIMAME menu
 *   PWR hold 1 s    -> power off
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"
#include "audio_hal.h"
#include "medal_input.h"
#include "qmi8658.h"
#include "medalboot.h"
#include "egg.h"

static const char *TAG = "HADOUKEN";

static void on_mute(void)
{
    medalboot_sound_t s = medalboot_sound_next();      /* loud, quiet, off - and it holds everywhere */
    audio_set_volume(medalboot_sound_volume(s));
    audio_set_mute(s == MEDALBOOT_SOUND_OFF);
    display_toast(medalboot_sound_name(s), 1500);
    ESP_LOGI(TAG, "%s", medalboot_sound_name(s));
}

/* runs inside the player's loop: the shared gestures, and nothing else ends the clip */
static bool poll(void)
{
    medal_input_state_t st;
    medal_input_poll(&st);
    return false;
}

extern "C" void app_main(void)
{
    medalboot_game_startup();            /* FIRST LINE: a crash lands in the menu, not here */

    display_init();
    display_set_backlight(DISPLAY_BRIGHTNESS_ACTIVE);
    audio_init();
    audio_set_volume(medalboot_sound_volume(medalboot_sound()));   /* as it was left, here or anywhere */
    if (medalboot_muted()) audio_set_mute(true);

    medal_input_config_t cfg = {};
    cfg.init_i2c = true;
    cfg.mute_hold_us = 3000000;
    cfg.on_mute = on_mute;
    cfg.exit_hold_us = MEDALBOOT_EXIT_HOLD_MS * 1000;
    cfg.on_exit = medalboot_exit_to_menu;
    cfg.imu_init = qmi8658_init;         /* nothing here is tilt-driven, but being moved is what */
    cfg.read_accel = qmi8658_read_accel; /* keeps the backlight up on a medal that is being worn */
    medal_input_init(&cfg);
    egg_set_poll(poll);

    medalboot_game_running();

    int n = egg_clip_count();
    ESP_LOGI(TAG, "%d clip(s) in the media partition", n);
    if (n <= 0) {
        ESP_LOGE(TAG, "no media - pack a clip with tools/pack_media.py and flash it");
        for (;;) { poll(); vTaskDelay(pdMS_TO_TICKS(50)); }
    }
    for (int i = 0;; i = (i + 1) % n) {
        egg_play(i);
        vTaskDelay(pdMS_TO_TICKS(400));  /* a beat of black between loops */
    }
}
