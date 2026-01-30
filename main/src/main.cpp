/*
 * PELLETINO - Pac-Man Arcade Simulator for ESP32-C6
 *
 * Main entry point
 */

#include <cstdio>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "display.h"
#include "audio_hal.h"
#include "z80_cpu.h"
#include "pacman_hw.h"

// Include converted ROM data
#include "pacman_rom.h"
#include "pacman_tilemap.h"
#include "pacman_spritemap.h"
#include "pacman_cmap.h"
#include "pacman_wavetable.h"

static const char *TAG = "PELLETINO";

// Frame timing
static constexpr uint32_t FRAME_TIME_US = 16667;  // 60 Hz = 16.667ms

// Main emulation state
static bool running = false;

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "PELLETINO starting...");
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());

    // Initialize display
    ESP_LOGI(TAG, "Initializing display...");
    display_init();

    // Initialize audio (ES8311 + I2S)
    ESP_LOGI(TAG, "Initializing audio...");
    audio_init();

    // Initialize Z80 CPU emulator
    ESP_LOGI(TAG, "Initializing Z80 CPU...");
    z80_init();

    // Initialize Pac-Man hardware emulation
    ESP_LOGI(TAG, "Initializing Pac-Man hardware...");
    pacman_hw_init();

    // Load ROM and graphics data
    ESP_LOGI(TAG, "Loading ROM data...");
    pacman_set_rom(pacman_rom);
    pacman_set_tiles(pacman_5e);
    pacman_set_sprites(&pacman_sprites[0][0][0]);
    pacman_set_palette(&pacman_colormap[0][0]);
    pacman_set_wavetable(&pacman_wavetable[0][0]);
    pacman_load_roms();

    ESP_LOGI(TAG, "Free heap after init: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Starting emulation loop...");

    running = true;
    uint64_t frame_start;
    uint64_t frame_count = 0;

    while (running) {
        frame_start = esp_timer_get_time();

        // 1. Run Z80 CPU for one frame worth of cycles (~50,000 @ 3MHz / 60Hz)
        pacman_run_frame();

        // 2. Render display (uses DMA, interleaved with audio)
        pacman_render_screen();

        // 3. Update audio buffer
        audio_update();

        // 4. Poll input
        pacman_poll_input();

        // 5. Trigger VBLANK interrupt if enabled
        pacman_vblank_interrupt();

        // Frame timing - wait for 16.667ms total
        uint64_t elapsed = esp_timer_get_time() - frame_start;
        if (elapsed < FRAME_TIME_US) {
            vTaskDelay(pdMS_TO_TICKS((FRAME_TIME_US - elapsed) / 1000));
        }

        frame_count++;
        if (frame_count % 300 == 0) {  // Every 5 seconds
            ESP_LOGI(TAG, "Frame %llu, elapsed: %llu us", frame_count, elapsed);
        }
    }
}
