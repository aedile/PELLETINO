/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro */
/*
 * SHORYUKEN - Capcom CPS-1, Street Fighter II, on the Waveshare ESP32-C6-LCD-1.69.
 *
 * A tuning bench. It boots the real ROM to its attract mode, and once a second it says
 * over serial where the last second went. Every trade-off is a knob in knobs.h.
 *
 * One task does everything, in order: run the machine for a frame, draw it (or not), top
 * up the sound, and sleep only if there is time left over. Nothing is drawn on another
 * task because there is no frame to hand to one: the picture goes from the machine's
 * state to the panel a strip at a time.
 */
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "display.h"
#include "cps1.h"
#include "cps1_mem.h"
#include "knobs.h"
#include "render.h"
#include "input.h"
#include "audio_hal.h"
#include "medalboot.h"
#include "medal_input.h"

static const char *TAG = "SHORYUKEN";

static uint64_t t_audio;
static const esp_partition_t *rom_part;

static void heap_report(const char *what)
{
    ESP_LOGI(TAG, "heap at %s: free %lu, largest block %lu, lowest ever %lu", what,
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned long)esp_get_minimum_free_heap_size());
}

/* a list of what was asked for and what there was, for when it does not fit */
static void *claim(const char *what, size_t bytes)
{
    size_t before = esp_get_free_heap_size(), largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    void *p = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "alloc %-14s %7u bytes: %s (free before %u, largest block %u)", what, (unsigned)bytes,
             p ? "ok" : "FAILED", (unsigned)before, (unsigned)largest);
    if (!p) {
        ESP_LOGE(TAG, "%s does not fit: short by %d bytes of contiguous heap", what, (int)bytes - (int)largest);
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return p;
}

#if SOUND != SOUND_OFF
static bool render_for_nothing;      /* a bench with the sound muted still pays for the synthesis */
static void pump_audio(void)
{
    int64_t t0 = esp_timer_get_time();
    if (render_for_nothing) {
        static int16_t sink[256];
        static int64_t last;
        if (!last) last = t0;
        int n = (int)((t0 - last) * SOUND_RATE / 1000000);
        if (n > SOUND_RATE / 20) n = SOUND_RATE / 20;
        last += (int64_t)n * 1000000 / SOUND_RATE;
        while (n > 0) { int k = n > 256 ? 256 : n; cps1_render_audio(sink, k); n -= k; }
    } else {
        audio_update();
    }
    t_audio += esp_timer_get_time() - t0;
}
#else
#define pump_audio NULL
#endif

extern "C" void app_main(void)
{
    /*
     * FIRST LINE, before anything that can fail. This points the boot partition back at the
     * PELLETINO launcher, so a panic, a watchdog bite or a brownout lands in the menu instead
     * of boot-looping a broken game.
     */
    medalboot_game_startup();

    ESP_LOGI(TAG, "SHORYUKEN starting: vmode=%s skip=%d auto=%d layers=%d rowscroll=%d sound=%s rate=%d ymq=%d core=%s idleskip=%d tilecache=%d bench=%d",
             VIDEO_MODE_NAME, FRAME_SKIP, FRAME_SKIP_AUTO, LAYERS, ROWSCROLL, SOUND_NAME, SOUND_RATE, YM_QUALITY,
             CPU_CORE_NAME, IDLE_SKIP, TILE_CACHE_KB, BENCH_SECONDS);
    heap_report("start");
    display_init();
    display_set_backlight(DISPLAY_BRIGHTNESS_ACTIVE);
    heap_report("display up");

    /* the ROMs: one data partition, mapped, read in place */
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "media");
    const void *blob = nullptr;
    esp_partition_mmap_handle_t map;
    static cps1_blob_t head;
    const char *bad = "no data partition labelled media";
    if (part) {
        /*
         * The chip can have 8 MB of flash mapped at once, and this program's own code is part
         * of that. So the ROMs are mapped without the samples, which are read as they are
         * played, and the header is read rather than mapped.
         */
        if (esp_partition_read(part, CPS1_BLOB_HEADER_OFF, &head, sizeof(head)) != ESP_OK) bad = "the data partition would not read";
        else bad = cps1_blob_check(&head);
        if (!bad) {
            esp_err_t err = esp_partition_mmap(part, 0, CPS1_BLOB_MAP_BYTES, ESP_PARTITION_MMAP_DATA, &blob, &map);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "mapping %u bytes: %s", (unsigned)CPS1_BLOB_MAP_BYTES, esp_err_to_name(err));
                bad = "the data partition would not map";
            }
        }
    }
    if (bad) {
        ESP_LOGE(TAG, "%s", bad);
        display_toast("NO ROMS", 60000);
        display_fill(0x0000);
        medal_input_config_t cfg = {};
        cfg.exit_hold_us = MEDALBOOT_EXIT_HOLD_MS * 1000;
        cfg.on_exit = medalboot_exit_to_menu;
        medal_input_init(&cfg);
        for (;;) { medal_input_state_t st; medal_input_poll(&st); vTaskDelay(pdMS_TO_TICKS(50)); }
    }
    rom_part = part;
    cps1_roms_t roms;
    cps1_blob_roms(&head, blob, &roms);
    roms.oki_read = [](uint32_t offset, uint8_t *dst, uint32_t len) {
        if (esp_partition_read(rom_part, CPS1_BLOB_MAP_BYTES + offset, dst, len) != ESP_OK) memset(dst, 0x80, len);
    };
    ESP_LOGI(TAG, "ROM set %.16s: %u KB mapped at %p", roms.cfg->set_name, (unsigned)(CPS1_BLOB_MAP_BYTES / 1024), blob);

    /* the machine's own memory is real RAM and is not negotiable */
    uint16_t *gfxram = (uint16_t *)claim("graphics RAM", CPS1_GFXRAM_BYTES);    /* the big one first */
    uint16_t *ram = (uint16_t *)claim("work RAM", CPS1_RAM_BYTES);
    cps1_init(&roms, ram, gfxram);
    cps1_set_clock([]() -> int64_t { return esp_timer_get_time(); });
    ESP_LOGI(TAG, "idle loop at %06lX", (unsigned long)cps1_idle_loop());
    heap_report("machine up");
    render_init();
    input_init();
#if SOUND != SOUND_OFF
    cps1_sound_init(&roms, SOUND_RATE);
    audio_init();
    audio_set_volume(medalboot_sound_volume(medalboot_sound()));   /* as it was left, here or anywhere */
    if (medalboot_muted()) audio_set_mute(true);
    render_for_nothing = BENCH_SECONDS > 0 && medalboot_muted();
    heap_report("sound up");
#endif
    /* far enough in to be sure this image works: stop the launcher counting attempts */
    medalboot_game_running();
    heap_report("ready");

    const int64_t start_us = esp_timer_get_time();
    int64_t last_us = start_us, report_us = start_us, owed_us = 0;
    uint64_t t_m68k = 0, t_z80 = 0, t_video = 0, t_strips = 0, t_idle = 0, t_input = 0;
    uint32_t emulated = 0, drawn = 0, skipped = 0, frame_no = 0;
    bool overran = false;

    for (;;) {
        int64_t now = esp_timer_get_time();
        owed_us += now - last_us;
        last_us = now;
        if (owed_us > 4 * CPS1_FRAME_US) owed_us = 4 * CPS1_FRAME_US;   /* never owe more than this: slow down instead */

        if (owed_us < CPS1_FRAME_US) {
            /* ahead of the clock: the only place this task sleeps */
            vTaskDelay(1);
            t_idle += esp_timer_get_time() - now;
        } else {
            owed_us -= CPS1_FRAME_US;
            input_update(cps1_input());

            int64_t t0 = esp_timer_get_time();
            t_input += t0 - now;
            cps1_run_frame();
            uint64_t z = cps1_z80_us();
            t_z80 += z;
            t_m68k += (uint64_t)(esp_timer_get_time() - t0) - z;
            emulated++;

            bool draw = (frame_no++ % (FRAME_SKIP + 1)) == 0;
            if (FRAME_SKIP_AUTO && overran) draw = false;
            if (draw) { render_frame(pump_audio, &t_video, &t_strips); drawn++; }
            else skipped++;
#if SOUND != SOUND_OFF
            pump_audio();
#endif
            /* a drawn frame that took longer than a frame buys the next one off */
            overran = draw && (esp_timer_get_time() - now) > CPS1_FRAME_US;
        }

#if STATS
        if (now - report_us >= 1000000) {
            double s = (now - report_us) / 1e6;
            uint64_t sum = t_m68k + t_z80 + t_audio + t_video + t_strips + t_idle + t_input;
            printf("stats fps=%.1f m68k=%.1fms z80=%.1fms ym=%.1fms video=%.1fms strips=%.1fms idle=%.1fms "
                   "skipped=%lu/%lu heap=%lu minheap=%lu vmode=%s skip=%d sound=%s "
                   "emu=%.1f input=%.1fms other=%.1fms rate=%d core=%s under=%lu pc=%06lX\n",
                   drawn / s, t_m68k / 1e3 / s, t_z80 / 1e3 / s, t_audio / 1e3 / s, t_video / 1e3 / s,
                   t_strips / 1e3 / s, t_idle / 1e3 / s,
                   (unsigned long)skipped, (unsigned long)emulated,
                   (unsigned long)esp_get_free_heap_size(), (unsigned long)esp_get_minimum_free_heap_size(),
                   VIDEO_MODE_NAME, FRAME_SKIP, SOUND_NAME,
                   emulated / s, t_input / 1e3 / s, ((now - report_us) - (int64_t)sum) / 1e3 / s, SOUND_RATE, CPU_CORE_NAME,
#if SOUND != SOUND_OFF
                   (unsigned long)audio_get_underrun_count(),
#else
                   0ul,
#endif
                   (unsigned long)cps1_pc());
            t_m68k = t_z80 = t_audio = t_video = t_strips = t_idle = t_input = 0;
            emulated = drawn = skipped = 0;
            report_us = now;
        }
#endif
#if BENCH_SECONDS > 0
        if (now - start_us >= (int64_t)BENCH_SECONDS * 1000000) {
            printf("bench done: %d seconds\n", BENCH_SECONDS);
            char rom[16];
            if (medalboot_rom(rom, sizeof(rom))) medalboot_exit_to_menu();   /* the menu started it: go back */
            display_toast("BENCH DONE", 60000);
            for (;;) { medal_input_state_t st; medal_input_poll(&st); vTaskDelay(pdMS_TO_TICKS(50)); }
        }
#endif
    }
}
