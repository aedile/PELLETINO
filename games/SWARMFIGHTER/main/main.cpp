/*
 * SWARMFIGHTER - Namco Galaga (1981) on the Waveshare ESP32-C6-LCD-1.69 Fiesta medal
 */
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include "display.h"
#include "galaga.h"
#include "galaga_roms.h"
#include "render.h"
#include "input.h"
#include "audio_hal.h"
#include "medalboot.h"
#include "scores.h"

static const char *TAG = "SWARM";
#define DEBUG_LOG 1
static const int64_t FRAME_US = (int64_t)GA_CYCLES_PER_FRAME * 1000000 / GA_CPU_CLOCK;   /* 16500 */

extern "C" void app_main(void)
{
    /* Before anything else: if we were chain-booted from the menu, make sure the
     * next reset goes back to it rather than here. */
    /*
     * FIRST LINE, before anything that can fail: point the boot partition back at the MINIMAME
     * launcher, so a panic or a brownout lands in the menu instead of boot-looping.
     */
    medalboot_game_startup();

#if !DEBUG_LOG
    esp_log_level_set("*", ESP_LOG_NONE);
#endif
    ESP_LOGI(TAG, "SWARMFIGHTER starting, free heap %lu", (unsigned long)esp_get_free_heap_size());
    display_init();
    display_set_backlight(DISPLAY_BRIGHTNESS_ACTIVE);

    auto to_ram = [](const uint8_t *src, size_t n) {
        uint8_t *dst = (uint8_t *)malloc(n);
        if (!dst) { ESP_LOGE(TAG, "ROM RAM copy failed"); abort(); }
        memcpy(dst, src, n); return (const uint8_t *)dst;
    };
    ga_roms_t roms = {
        to_ram(ga_rom_cpu1, sizeof(ga_rom_cpu1)), to_ram(ga_rom_cpu2, sizeof(ga_rom_cpu2)), to_ram(ga_rom_cpu3, sizeof(ga_rom_cpu3)),
        to_ram(ga_tiles, sizeof(ga_tiles)), to_ram(ga_sprites, sizeof(ga_sprites)),
        ga_prom_palette, ga_prom_charlut, ga_prom_spritelut, ga_prom_wave };
    ga_init(&roms);
    ga_set_dips(0xf7, 0x97);      /* easy, demo sounds, upright; 1C/1C, 20K/70K, 3 fighters */
    render_init();
    input_init();
    audio_init();
    audio_set_volume(medalboot_sound_volume(medalboot_sound()));   /* as it was left, here or anywhere */
    if (medalboot_muted()) audio_set_mute(true);
    medalboot_game_running();   /* far enough in to be sure this image works */
    hiscore_begin(&game_scores);
    ESP_LOGI(TAG, "ready, free heap %lu", (unsigned long)esp_get_free_heap_size());

    int64_t last_us = esp_timer_get_time(), last_report = last_us, owed_us = 0;
    uint64_t t_emu = 0, t_render = 0, t_audio = 0;
    uint32_t frames = 0, skipped = 0;
    for (;;) {
        int64_t now = esp_timer_get_time();
        owed_us += now - last_us;
        last_us = now;
        if (owed_us > 3 * FRAME_US) owed_us = 3 * FRAME_US;
        input_update(ga_input());
        while (owed_us >= FRAME_US) {
            int64_t t0 = esp_timer_get_time();
            ga_run_frame();
            hiscore_frame();
            int64_t t1 = esp_timer_get_time();
            t_emu += t1 - t0;
            frames++;
            owed_us -= FRAME_US;
            if (owed_us < FRAME_US) {              /* draw only the last frame of a catch-up burst */
                uint8_t *fb = render_acquire();
                if (fb) { ga_render(fb); render_submit(fb); t_render += esp_timer_get_time() - t1; }
                else skipped++;
            } else {
                skipped++;
            }
        }
        int64_t ta = esp_timer_get_time();
        audio_update();
        t_audio += esp_timer_get_time() - ta;
        vTaskDelay(1);
        if (now - last_report >= 5000000) {
            ESP_LOGI(TAG, "5s: frames %lu drawn %lu skipped %lu dropped %lu; ms/s: emu %llu render %llu present %llu audio %llu; halt%% c2 %lu c3 %lu; heap %lu; pc %04X %04X %04X",
                     (unsigned long)frames, (unsigned long)render_frames_drawn(), (unsigned long)skipped, (unsigned long)render_frames_dropped(),
                     (unsigned long long)(t_emu / 5000), (unsigned long long)(t_render / 5000), (unsigned long long)(render_busy_us() / 5000), (unsigned long long)(t_audio / 5000),
                     (unsigned long)(ga_halt_cycles(1) / (5 * GA_CPU_CLOCK / 100)), (unsigned long)(ga_halt_cycles(2) / (5 * GA_CPU_CLOCK / 100)),
                     (unsigned long)esp_get_free_heap_size(), ga_pc(0), ga_pc(1), ga_pc(2));
            frames = skipped = 0; t_emu = t_render = t_audio = 0; last_report = now;
        }
    }
}
