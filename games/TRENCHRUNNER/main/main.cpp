/*
 * TRENCHRUNNER - Atari Star Wars (1983) on the Waveshare ESP32-C6-LCD-1.69 medal
 */
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

#include "display.h"
#include "starwars.h"
#include "autoplay.h"
#include "starwars_roms.h"
#include "render.h"
#include "marquee.h"
#include "input.h"
#include "audio_hal.h"
#include "sound.h"
#include "medalboot.h"

static const char *TAG = "TRENCH";

#define DEBUG_LOG 1

static uint32_t frames_emulated, frames_skipped;
static volatile bool emu_behind;       /* set while the emulator owes more than a frame of time */

/* The game issued VGGO with a complete vector list: hand it to the render task */
static void on_frame(const avg_t *avg, void *user)
{
    ap_frame(avg);                       /* the autopilot looks at every frame, cheaply */
    (void)user;
    static bool skip_toggle;
    frames_emulated++;
    if (emu_behind) {
        /* when short on CPU, draw every other frame rather than slow the game down */
        skip_toggle = !skip_toggle;
        if (skip_toggle) { frames_skipped++; return; }
    }
    render_submit(avg->points, avg->npoints);
}

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
    ESP_LOGI(TAG, "TRENCHRUNNER starting, free heap %lu", (unsigned long)esp_get_free_heap_size());

    display_init();
    display_set_backlight(DISPLAY_BRIGHTNESS_ACTIVE);
    render_init();
    /* Marquee text in the black bars above and below the picture (portrait layout only).
     * Off by default. marquee_set(bar, text, color, star_color, scale, scroll px/s): asterisks
     * are drawn in star_color (MARQUEE_NONE = same as the text); scroll 0 = static, centred.
     * Colors: MARQUEE_RED/GREEN/BLUE/CYAN/MAGENTA/YELLOW/WHITE; scale 2 = 20-pixel letters. */
#ifdef MARQUEE_DEMO
    marquee_set(MARQUEE_TOP, "FIESTA 2027 * VIVA FIESTA * SAN ANTONIO *", MARQUEE_RED, MARQUEE_GREEN, 2, 40);
    marquee_set(MARQUEE_BOTTOM, "MAY THE FORCE BE WITH YOU * RED FIVE STANDING BY *", MARQUEE_BLUE, MARQUEE_GREEN, 2, 30);
#endif
    input_init();
    audio_init();
    if (medalboot_muted()) audio_set_mute(true);   /* muted elsewhere: stay muted */

    /* far enough in to be sure this image works: stop the launcher counting attempts */
    medalboot_game_running();

    /* Copy the ROMs the CPU and AVG fetch from into RAM: reads through the flash
     * cache are far slower than SRAM and this is the emulator's hottest path. */
    auto to_ram = [](const uint8_t *src, size_t n) {
        uint8_t *dst = (uint8_t *)malloc(n);
        if (!dst) { ESP_LOGE(TAG, "ROM RAM copy failed"); abort(); }
        memcpy(dst, src, n);
        return (const uint8_t *)dst;
    };
    sw_roms_t roms = {
        .rom_main = to_ram(sw_rom_main, sizeof(sw_rom_main)),
        .rom_bank = to_ram(sw_rom_bank, sizeof(sw_rom_bank)),
        .rom_vector = to_ram(sw_rom_vector, sizeof(sw_rom_vector)),
        .prom_mathbox = sw_prom_mathbox,
        .prom_avg = to_ram(sw_prom_avg, sizeof(sw_prom_avg)),
    };
    sw_init(&roms);
    sw_attach_sound(sw_rom_sound);      /* the sound CPU is mostly idle: flash is fine for its ROM */
    sw_set_dips(0x90, 0x00);     /* 6 shields, easy, 1 bonus shield, demo sounds; free play */
    sw_set_frame_callback(on_frame, nullptr);
    /*
     * The attract mode is eighty-five seconds of text. Left alone for about one run of it,
     * the medal starts a game and flies it, aiming from the vector list it already draws. A
     * person touching anything takes over at once.
     */
    ap_config_t ac = {};
    ac.idle_us = 15u * 1000000u;         /* attract runs for fifteen seconds before it flies itself */
    ap_init(&ac);
    sw_set_time_source([]() -> uint64_t { return (uint64_t)esp_timer_get_time(); });
    ESP_LOGI(TAG, "emulation ready, free heap %lu", (unsigned long)esp_get_free_heap_size());

    int64_t last_us = esp_timer_get_time();
    int64_t last_report = last_us;
    uint64_t t_emu = 0, t_audio = 0;
    const int64_t MAX_CATCHUP_US = 60000;   /* never owe more than ~2.5 vector frames */

    for (;;) {
        int64_t now = esp_timer_get_time();
        int64_t elapsed = now - last_us;
        if (elapsed > MAX_CATCHUP_US) elapsed = MAX_CATCHUP_US;
        last_us = now;

        input_update(sw_input());
        ap_update(sw_input(), (uint64_t)now, input_human_active());
        static bool fire_prev; static unsigned long fire_presses;
        if (sw_input()->fire && !fire_prev) fire_presses++;
        fire_prev = sw_input()->fire;

        /* run the 6809 for the wall-clock time that passed (1.512 MHz) */
        uint32_t cycles = (uint32_t)(elapsed * SW_CPU_CLOCK / 1000000);
        if (cycles > 0) sw_run(cycles);
        int64_t t1 = esp_timer_get_time();
        t_emu += t1 - now;

        audio_update();                 /* tops the I2S DMA queue up from the POKEY mixer */
        int64_t t2 = esp_timer_get_time();
        t_audio += t2 - t1;

        /* load = CPU time this loop spent per unit of emulated time (smoothed).
         * Above ~0.9 we cannot keep real time with every frame drawn, so skip frames. */
        static int load_pct = 0;
        int inst = (int)((t2 - now) * 100 / (elapsed > 0 ? elapsed : 1));
        load_pct += (inst - load_pct) / 8;
        emu_behind = load_pct > 90;

        vTaskDelay(1);   /* ~1 ms: lets the render task and idle task run */

        if (now - last_report >= 5000000) {
            sw_stats_t *st = sw_stats();
            uint32_t drawn = render_frames_drawn(), dropped = render_frames_dropped();
            ESP_LOGI(TAG, "%llums: emulated %lu, drawn %lu, dropped %lu, skipped %lu; ms/s: emu-total %llu (6809 %llu, avg %llu, math %llu, submit %llu) render %llu audio %llu; idle-skip main %lu%% snd %lu%%; sndrst %lu; underruns %lu speech-gaps %lu; heap %lu; pc %04X; ap %d presses %lu",
                     (unsigned long long)((now - last_report) / 1000),
                     (unsigned long)frames_emulated, (unsigned long)drawn, (unsigned long)dropped, (unsigned long)frames_skipped,
                     (unsigned long long)(t_emu / 5000),
                     (unsigned long long)((t_emu - st->avg_us - st->math_us - st->frame_cb_us) / 5000),
                     (unsigned long long)(st->avg_us / 5000), (unsigned long long)(st->math_us / 5000),
                     (unsigned long long)(st->frame_cb_us / 5000),
                     (unsigned long long)(render_busy_us() / 5000), (unsigned long long)(t_audio / 5000),
                     (unsigned long)(sw_idle_skipped() / (5 * SW_CPU_CLOCK / 100)),
                     (unsigned long)(snd_idle_skipped() / (5 * SW_CPU_CLOCK / 100)),
                     (unsigned long)sw_soundrst_count(), (unsigned long)audio_get_underrun_count(), (unsigned long)snd_speech_underruns(),
                     (unsigned long)esp_get_free_heap_size(), sw_pc(), (int)ap_state(), fire_presses);
            fire_presses = 0;
            memset(st, 0, sizeof(*st));
            frames_emulated = 0; frames_skipped = 0;
            t_emu = 0; t_audio = 0;
            last_report = now;
        }
    }
}
