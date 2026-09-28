/*
 * QUALIFIER - Namco Pole Position (1982) on the Waveshare ESP32-C6-LCD-1.69 Fiesta medal
 */
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_cpu.h"
#ifdef PP_VIDEO_PROFILE
extern "C" { extern uint32_t pp_video_prof[5]; extern uint32_t (*pp_video_clock)(void); }
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include "display.h"
#include "polepos.h"
#include "polepos_roms.h"
#include "render.h"
#include "input.h"
#include "audio_hal.h"
#include "medalboot.h"

static const char *TAG = "QUAL";
#define DEBUG_LOG 1
static const int64_t FRAME_US = (int64_t)PP_CYCLES_PER_FRAME * 1000000 / PP_CPU_CLOCK;

extern "C" void app_main(void)
{
    /*
     * FIRST LINE, before anything that can fail. This points the boot partition back at the
     * MINIMAME launcher, so a panic, a watchdog bite or a brownout lands in the menu instead
     * of boot-looping a broken game.
     */
    medalboot_game_startup();

#if !DEBUG_LOG
    esp_log_level_set("*", ESP_LOG_NONE);
#endif
    ESP_LOGI(TAG, "QUALIFIER starting, free heap %lu", (unsigned long)esp_get_free_heap_size());
    display_init();
    display_set_backlight(DISPLAY_BRIGHTNESS_ACTIVE);

    auto to_ram = [](const uint8_t *src, size_t n) {
        uint8_t *dst = (uint8_t *)malloc(n);
        if (!dst) { ESP_LOGE(TAG, "ROM RAM copy failed (%u bytes)", (unsigned)n); abort(); }
        memcpy(dst, src, n); return (const uint8_t *)dst;
    };
    pp_roms_t roms = {
        to_ram(pp_rom_z80, sizeof(pp_rom_z80)), pp_rom_sub1, pp_rom_sub2,   /* the core keeps its own word copies of the sub ROMs */
        /* graphics the renderer touches per pixel go to RAM; the 128 KB big-sprite set stays in flash */
        to_ram(pp_chars, sizeof(pp_chars)), to_ram(pp_tiles, sizeof(pp_tiles)), to_ram(pp_sprites, sizeof(pp_sprites)), pp_bigsprites,
        to_ram(pp_road, sizeof(pp_road)), to_ram(pp_scalelut, sizeof(pp_scalelut)), to_ram(pp_proms, sizeof(pp_proms)), pp_wave, pp_engine, pp_voice };
    pp_init(&roms);
    pp_set_dips(0xff, 0x6b);
    /* cheap per-slice time source: CPU cycle counter, folded into a 64-bit microsecond count */
    pp_set_time_source([]() -> uint64_t {
        static uint32_t last; static uint64_t acc_cycles;
        uint32_t now = esp_cpu_get_cycle_count();
        acc_cycles += (uint32_t)(now - last); last = now;
        return acc_cycles / (CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    });
#ifdef PP_VIDEO_PROFILE
    pp_video_clock = []() -> uint32_t { return esp_cpu_get_cycle_count(); };
#endif
    render_init();
    input_init();
    audio_init();
    if (medalboot_muted()) audio_set_mute(true);   /* muted elsewhere: stay muted */
    /* far enough in to be sure this image works: stop the launcher counting attempts */
    medalboot_game_running();
    ESP_LOGI(TAG, "ready, free heap %lu", (unsigned long)esp_get_free_heap_size());

    int64_t last_us = esp_timer_get_time(), last_report = last_us, owed_us = 0;
    uint64_t t_emu = 0, t_video = 0, t_audio = 0;
    uint32_t frames = 0, skipped = 0;
    for (;;) {
        int64_t now = esp_timer_get_time();
        owed_us += now - last_us;
        last_us = now;
        if (owed_us > 3 * FRAME_US) owed_us = 3 * FRAME_US;
        input_update(pp_input());
#ifdef PP_AUTOPLAY
        {   /* self-driving for on-device profiling: coin, floor it, shift up, weave */
            static int64_t t_start = now; double t = (now - t_start) / 1e6; pp_input_t *in = pp_input();
            in->coin1 = t >= 6 && t < 6.3; if (t >= 8) in->accel = 0x90; if (t >= 12) in->gear = 1;
            in->steer = t < 14 ? 0 : (t < 20 ? 0x30 : (t < 26 ? 0x10 : 0x40));
        }
#endif
        while (owed_us >= FRAME_US) {
            int64_t t0 = esp_timer_get_time();
            pp_run_frame();
            int64_t t1 = esp_timer_get_time();
            t_emu += t1 - t0;
            frames++;
            owed_us -= FRAME_US;
            if (owed_us < FRAME_US) {
                uint8_t *fb = render_acquire();
                if (fb) { pp_render(fb); t_video += esp_timer_get_time() - t1; render_submit(fb); }
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
            pp_stats_t *st = pp_stats();
            ESP_LOGI(TAG, "5s: frames %lu drawn %lu skipped %lu dropped %lu; ms/s: emu %llu (z80 %llu sub1 %llu sub2 %llu) video %llu present %lu audio %llu; heap %lu; pc %04X %04X %04X latch %02X; presses boot %lu pwr %lu levels %u; accel %d %d %d angle %.1f neutral %u steer %u",
                     (unsigned long)frames, (unsigned long)render_frames_drawn(), (unsigned long)skipped, (unsigned long)render_frames_dropped(),
                     (unsigned long long)(t_emu / 5000), (unsigned long long)(st->cpu_us[0] / 5000), (unsigned long long)(st->cpu_us[1] / 5000), (unsigned long long)(st->cpu_us[2] / 5000),
                     (unsigned long long)(t_video / 5000), (unsigned long)(render_present_us() / 5000), (unsigned long long)(t_audio / 5000),
                     (unsigned long)esp_get_free_heap_size(), pp_pc(0), pp_pc(1), pp_pc(2), pp_latch(), (unsigned long)input_dbg_presses[0], (unsigned long)input_dbg_presses[1], input_dbg_levels,
                     input_dbg_accel[0], input_dbg_accel[1], input_dbg_accel[2], (double)input_dbg_angle, input_dbg_neutral, input_dbg_steer);
#ifdef PP_VIDEO_PROFILE
            ESP_LOGI(TAG, "video ms/s: clear %lu bg %lu road %lu sprites %lu text %lu",
                     (unsigned long)(pp_video_prof[0] / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ / 5000), (unsigned long)(pp_video_prof[1] / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ / 5000),
                     (unsigned long)(pp_video_prof[2] / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ / 5000), (unsigned long)(pp_video_prof[3] / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ / 5000),
                     (unsigned long)(pp_video_prof[4] / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ / 5000));
            memset(pp_video_prof, 0, sizeof(pp_video_prof));
#endif
            frames = skipped = 0; t_emu = t_video = t_audio = 0; last_report = now; memset(st, 0, sizeof(*st));
        }
    }
}
