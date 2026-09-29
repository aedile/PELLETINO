/*
 * STRATUM - Namco Dig Dug (1982) on the Waveshare ESP32-C6-LCD-1.69 Fiesta medal
 */
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include "display.h"
#include "digdug.h"
#include "digdug_roms.h"
#include "render.h"
#include "input.h"
#include "audio_hal.h"
#include "medalboot.h"
#include "scores.h"

static const char *TAG = "STRATUM";
/* 18.432 MHz over 384 x 264 pixels = 60.606 Hz */
static const int64_t FRAME_US = 16500;   /* 60.606 Hz */

extern "C" void app_main(void)
{
    /*
     * FIRST LINE, before anything that can fail. This points the boot partition back at the
     * MINIMAME launcher, so a panic, a watchdog bite or a brownout lands in the menu instead
     * of boot-looping a broken game.
     */
    medalboot_game_startup();

    ESP_LOGI(TAG, "STRATUM starting, free heap %lu", (unsigned long)esp_get_free_heap_size());
    display_init();
    display_set_backlight(DISPLAY_BRIGHTNESS_ACTIVE);

    auto to_ram = [](const uint8_t *src, size_t n) {
        uint8_t *dst = (uint8_t *)malloc(n);
        if (!dst) { ESP_LOGE(TAG, "ROM RAM copy failed"); abort(); }
        memcpy(dst, src, n); return (const uint8_t *)dst;
    };
    dd_roms_t roms = { to_ram(dd_cpu1, sizeof(dd_cpu1)), to_ram(dd_cpu2, sizeof(dd_cpu2)),
                       to_ram(dd_cpu3, sizeof(dd_cpu3)), to_ram(dd_chr, sizeof(dd_chr)),
                       to_ram(dd_spr, sizeof(dd_spr)),   to_ram(dd_bgt, sizeof(dd_bgt)),
                       to_ram(dd_play, sizeof(dd_play)), to_ram(dd_pal, sizeof(dd_pal)),
                       to_ram(dd_lut_spr, sizeof(dd_lut_spr)), to_ram(dd_lut_bg, sizeof(dd_lut_bg)),
                       to_ram(dd_wave, sizeof(dd_wave)) };
    dd_init(&roms);
    dd_set_dips(0x99, 0x24);   /* factory: 1 coin 1 credit, 3 lives, bonus at 20K and 60K */
    render_init();
    input_init();
    audio_init();
    audio_set_volume(medalboot_sound_volume(medalboot_sound()));   /* as it was left, here or anywhere */
    if (medalboot_muted()) audio_set_mute(true);
    /* far enough in to be sure this image works: stop the launcher counting attempts */
    medalboot_game_running();
    hiscore_begin(&game_scores);
    ESP_LOGI(TAG, "ready, free heap %lu", (unsigned long)esp_get_free_heap_size());

    int64_t ldd_us = esp_timer_get_time(), ldd_report = ldd_us, owed_us = 0;
    uint64_t t_emu = 0, t_submit = 0, t_audio = 0;
    uint32_t frames = 0, skipped = 0;
    for (;;) {
        int64_t now = esp_timer_get_time();
        owed_us += now - ldd_us;
        ldd_us = now;
        if (owed_us > 3 * FRAME_US) owed_us = 3 * FRAME_US;
        input_update(dd_input());
        while (owed_us >= FRAME_US) {
            int64_t t0 = esp_timer_get_time();
            dd_run_frame();
            hiscore_frame();
            int64_t t1 = esp_timer_get_time();
            t_emu += t1 - t0;
            frames++;
            owed_us -= FRAME_US;
            if (owed_us < FRAME_US) {              /* draw only the last frame of a catch-up burst */
                uint8_t *fb = render_acquire();
                if (fb) { dd_render(fb); render_submit(fb); t_submit += esp_timer_get_time() - t1; }
                else skipped++;
            } else {
                skipped++;
            }
        }
        int64_t ta = esp_timer_get_time();
        audio_update();
        t_audio += esp_timer_get_time() - ta;
        vTaskDelay(1);
        if (now - ldd_report >= 5000000) {
            ESP_LOGI(TAG, "5s: frames %lu drawn %lu skipped %lu dropped %lu; ms/s: emu %llu submit %llu render %llu audio %llu; heap %lu; pc %04X",
                     (unsigned long)frames, (unsigned long)render_frames_drawn(), (unsigned long)skipped, (unsigned long)render_frames_dropped(),
                     (unsigned long long)(t_emu / 5000), (unsigned long long)(t_submit / 5000), (unsigned long long)(render_busy_us() / 5000), (unsigned long long)(t_audio / 5000),
                     (unsigned long)esp_get_free_heap_size(), dd_pc(0));
            frames = skipped = 0; t_emu = t_submit = t_audio = 0; ldd_report = now;
        }
    }
}
