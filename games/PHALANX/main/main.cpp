/*
 * PHALANX - Taito/Midway Space Invaders (1978) on the Waveshare ESP32-C6-LCD-1.69 Fiesta medal
 */
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include "display.h"
#include "invaders.h"
#include "invaders_roms.h"
#include "render.h"
#include "input.h"
#include "audio_hal.h"
#include "medalboot.h"

static const char *TAG = "PHALANX";
static const int64_t FRAME_US = 1000000 / SI_FPS;

extern "C" void app_main(void)
{
    /*
     * FIRST LINE, before anything that can fail. This points the boot partition back at the
     * PELLETINO launcher, so a panic, a watchdog bite or a brownout lands in the menu instead
     * of boot-looping a broken game.
     */
    medalboot_game_startup();

    ESP_LOGI(TAG, "PHALANX starting, free heap %lu", (unsigned long)esp_get_free_heap_size());
    display_init();
    display_set_backlight(DISPLAY_BRIGHTNESS_ACTIVE);

    /* the ROM is read on every instruction fetch; a copy in RAM keeps it off the flash cache */
    uint8_t *rom = (uint8_t *)malloc(sizeof(si_rom));
    if (!rom) { ESP_LOGE(TAG, "ROM RAM copy failed"); abort(); }
    memcpy(rom, si_rom, sizeof(si_rom));
    si_roms_t roms = { rom };
    si_init(&roms);
    si_set_dips(0x00);   /* 3 lives, bonus at 1500, coin info shown */
    render_init();
    input_init();
    audio_init();
    if (medalboot_muted()) audio_set_mute(true);   /* muted elsewhere: stay muted */
    /* far enough in to be sure this image works: stop the launcher counting attempts */
    medalboot_game_running();
    ESP_LOGI(TAG, "ready, free heap %lu", (unsigned long)esp_get_free_heap_size());

    int64_t lfr_us = esp_timer_get_time(), lfr_report = lfr_us, owed_us = 0;
    uint64_t t_emu = 0, t_submit = 0, t_audio = 0;
    uint32_t frames = 0, skipped = 0;
    for (;;) {
        int64_t now = esp_timer_get_time();
        owed_us += now - lfr_us;
        lfr_us = now;
        if (owed_us > 3 * FRAME_US) owed_us = 3 * FRAME_US;
        input_update(si_input());
        while (owed_us >= FRAME_US) {
            int64_t t0 = esp_timer_get_time();
            si_run_frame();
            int64_t t1 = esp_timer_get_time();
            t_emu += t1 - t0;
            frames++;
            owed_us -= FRAME_US;
            if (owed_us < FRAME_US) {              /* draw only the last frame of a catch-up burst */
                uint8_t *fb = render_acquire();
                if (fb) { si_render(fb); render_submit(fb); t_submit += esp_timer_get_time() - t1; }
                else skipped++;
            } else {
                skipped++;
            }
        }
        int64_t ta = esp_timer_get_time();
        audio_update();
        t_audio += esp_timer_get_time() - ta;
        vTaskDelay(1);
        if (now - lfr_report >= 5000000) {
            ESP_LOGI(TAG, "5s: frames %lu drawn %lu skipped %lu dropped %lu; ms/s: emu %llu submit %llu render %llu audio %llu; heap %lu; pc %04X",
                     (unsigned long)frames, (unsigned long)render_frames_drawn(), (unsigned long)skipped, (unsigned long)render_frames_dropped(),
                     (unsigned long long)(t_emu / 5000), (unsigned long long)(t_submit / 5000), (unsigned long long)(render_busy_us() / 5000), (unsigned long long)(t_audio / 5000),
                     (unsigned long)esp_get_free_heap_size(), si_pc());
            frames = skipped = 0; t_emu = t_submit = t_audio = 0; lfr_report = now;
        }
    }
}
