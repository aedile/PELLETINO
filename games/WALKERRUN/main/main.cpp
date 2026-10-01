/*
 * WALKERRUN - Atari The Empire Strikes Back (1985) on the Waveshare ESP32-C6-LCD-1.69
 *
 * The same board as Star Wars with a bigger program and a slapstic chip, and this is the
 * same firmware as TRENCHRUNNER around a core that knows both. Which game it is comes from
 * the ROM header: tools/convert_roms.py defines SW_GAME_ESB or SW_GAME_STARWARS.
 */
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "esp_heap_caps.h"

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
#include "scores.h"

static const char *TAG = "WALKER";

#define DEBUG_LOG 1

static uint32_t frames_emulated, frames_skipped;
static volatile bool emu_behind;       /* set while the emulator owes more than a frame of time */

/* The game issued VGGO with a complete vector list: hand it to the render task */
static void on_frame(const avg_t *avg, void *user)
{
#ifndef SW_GAME_ESB
    ap_frame(avg);                       /* the autopilot looks at every frame, cheaply */
#endif
    hiscore_frame();
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
    ESP_LOGI(TAG, "WALKERRUN starting, free heap %lu", (unsigned long)esp_get_free_heap_size());

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
    audio_set_volume(medalboot_sound_volume(medalboot_sound()));   /* as it was left, here or anywhere */
    if (medalboot_muted()) audio_set_mute(true);

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
    sw_roms_t roms = {};
    roms.prom_mathbox = sw_prom_mathbox;
#ifdef SW_GAME_ESB
    /*
     * Empire's program is 64 KB to Star Wars's 48, in 8 KB pieces, and the chip's RAM is three
     * regions: one big, and two of 10 and 14 KB that nothing else ever uses. What is left of
     * the big one has to hold a 16 KB table for the sound chips as well, so the program does
     * not all fit there.
     *
     * Measured on the device, where a piece sits makes less difference than it should: with
     * every piece in RAM the 6809 cost what it had with a fifth of the program in flash. What
     * Empire costs is the work it does - it skips a tenth of its time as idle where Star
     * Wars skips a fifth. So memory is spent where it is free. The two pieces the game spends
     * least time in go in the two small regions, which takes filling the big one first, as
     * the allocator will not choose them while it has room. The next coldest stays in flash,
     * and so does the slapstic's 32 KB, which is 0.3% of what the CPU executes.
     *
     * Share of the 6809's time by piece, from the host harness (REGIONS=1):
     *   page 0: 0xA000 7.8%, 0xC000 2.2%, 0xE000 42.6%      page 1: 0.0%, 0.5%, 0.8%
     *   bank:   page 0 8.8%, page 1 37.0%
     */
    roms.rom_main = sw_rom_main;
    roms.rom_bank = sw_rom_bank;
    roms.rom_slapstic = sw_rom_slapstic;
    {
        static const int small_regions[2] = { 3, 4 };       /* page 1's 0xA000 and 0xC000 */
        static const int big_region[3]    = { 0, 1, 2 };    /* all of page 0; page 1's 0xE000 stays in flash */
        size_t big = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        void *fill = big > 0x2000 ? malloc(big - 64) : nullptr;
        for (int i = 0; i < 2; i++) {
            uint8_t *p = (uint8_t *)malloc(0x2000);
            if (p) { memcpy(p, sw_rom_main + small_regions[i] * 0x2000, 0x2000); roms.rom_part[small_regions[i]] = p; }
        }
        free(fill);
        for (int i = 0; i < 3; i++) roms.rom_part[big_region[i]] = to_ram(sw_rom_main + big_region[i] * 0x2000, 0x2000);
        for (int i = 0; i < 2; i++) roms.rom_part[6 + i] = to_ram(sw_rom_bank + i * 0x2000, 0x2000);
    }
#else
    roms.rom_main = to_ram(sw_rom_main, sizeof(sw_rom_main));
    roms.rom_bank = to_ram(sw_rom_bank, sizeof(sw_rom_bank));
#endif
    roms.rom_vector = to_ram(sw_rom_vector, sizeof(sw_rom_vector));
    roms.prom_avg = to_ram(sw_prom_avg, sizeof(sw_prom_avg));
    sw_init(&roms);
    sw_attach_sound(sw_rom_sound, sizeof(sw_rom_sound));   /* the sound CPU is mostly idle: flash is fine for its ROM */
#ifdef SW_GAME_ESB
    sw_set_dips(0xfb, 0x00);     /* 4 shields, easy, Jedi letters increment, demo sounds, freeze off; free play */
#else
    sw_set_dips(0x90, 0x00);     /* 6 shields, easy, 1 bonus shield, demo sounds; free play */
#endif
    hiscore_begin(&game_scores);         /* after sw_init(), which clears the chip */
    sw_set_frame_callback(on_frame, nullptr);
#ifndef SW_GAME_ESB
    /*
     * Star Wars only: left alone, it starts a game and flies it, aiming from the vector list
     * it already draws. The autopilot knows that game's targets and its trench and nothing
     * about this one's, so Empire shows its own attract mode.
     */
    ap_config_t ac = {};
    ac.idle_us = 15u * 1000000u;
    ap_init(&ac);
#endif
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
#ifndef SW_GAME_ESB
        ap_update(sw_input(), (uint64_t)now, input_human_active());
#endif
#ifdef SW_AUTOPLAY
        {   /* self-playing, for measuring on the device: start, let the countdown pick a wave, fire and weave */
            static int64_t t_start = now; double t = (now - t_start) / 1e6; sw_input_t *in = sw_input();
            in->fire = (t >= 5 && t < 5.3) || (t >= 20 && fmod(t, 6.0) < 0.3);
            in->yaw = t < 26 ? 0x80 : (fmod(t, 12.0) < 6 ? 0x60 : 0xa0);
            in->pitch = t < 60 ? 0x80 : (fmod(t, 15.0) < 7 ? 0x60 : 0x90);
        }
#endif
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
