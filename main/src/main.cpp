/*
 * PELLETINO - Pac-Man Arcade Simulator for ESP32-C6
 *
 * Main entry point
 */

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "audio_hal.h"
#include "display.h"
#include "game_state.h"
#include "pacman_hw.h"
#include "pacman_input.h"
#include "qmi8658.h"
#include "z80_cpu.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#ifdef CONFIG_BT_ENABLED
#include "esp_bt.h"
#include "esp_bt_main.h"
#endif

// Forward declare video player
extern "C" int play_fiesta_video(void);

// Game selection comes from the build: idf.py -DGAME=pacman (default) or -DGAME=mspacman
// (see main/CMakeLists.txt). ROM headers come from tools/convert_roms.py.
#if !defined(GAME_PACMAN) && !defined(GAME_MSPACMAN)
#define GAME_PACMAN
#endif

// Include converted ROM data based on game selection
#ifdef GAME_MSPACMAN
#include "mspacman_cmap.h"
#include "mspacman_rom.h"
#include "mspacman_spritemap.h"
#include "mspacman_tilemap.h"
#include "mspacman_wavetable.h"
#define GAME_NAME "Ms. Pac-Man"
#define GAME_ROM mspacman_rom
#define GAME_TILES mspacman_5e
#define GAME_SPRITES mspacman_sprites
#define GAME_COLORMAP mspacman_colormap
#define GAME_WAVETABLE mspacman_wavetable
#else
#include "pacman_cmap.h"
#include "pacman_rom.h"
#include "pacman_spritemap.h"
#include "pacman_tilemap.h"
#include "pacman_wavetable.h"
#define GAME_NAME "Pac-Man"
#define GAME_ROM pacman_rom
#define GAME_TILES pacman_5e
#define GAME_SPRITES pacman_sprites
#define GAME_COLORMAP pacman_colormap
#define GAME_WAVETABLE pacman_wavetable
#endif

// Debug logging flag - set to 1 for serial output, 0 for silent (battery saving)
#define PELLETINO_DEBUG 0

static const char *TAG = "PELLETINO";

// Frame timing
static constexpr uint32_t FRAME_TIME_US = 16667; // 60 Hz = 16.667ms

// Main emulation state
static bool running = false;

// High score saving disabled due to NVS flash power brownouts on battery

extern "C" void app_main(void) {
#if !PELLETINO_DEBUG
  esp_log_level_set("*", ESP_LOG_NONE);
#endif

  // Initialize NVS (required for WiFi/BT)
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Initialize and stop WiFi (to force PHY power and clock gating off)
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&cfg) == ESP_OK) {
      esp_wifi_stop();
      esp_wifi_deinit();
  }

  // Initialize and stop BT (if enabled in sdkconfig)
#ifdef CONFIG_BT_ENABLED
  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  if (esp_bt_controller_init(&bt_cfg) == ESP_OK) {
      esp_bt_controller_disable();
      esp_bt_controller_deinit();
  }
#endif

  ESP_LOGI(TAG, "PELLETINO starting - %s", GAME_NAME);
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
  pacman_set_rom(GAME_ROM, sizeof(GAME_ROM));
#ifdef GAME_MSPACMAN
  pacman_set_mspacman_aux(mspacman_rom_plain);
#endif
  pacman_set_tiles(GAME_TILES);
  pacman_set_sprites(&GAME_SPRITES[0][0][0]);
  pacman_set_palette(&GAME_COLORMAP[0][0]);
  pacman_set_wavetable(&GAME_WAVETABLE[0][0]);
  pacman_load_roms();

  // Load high score moved to attract mode start to allow Z80 to initialize default first

  ESP_LOGI(TAG, "Free heap after init: %lu bytes", esp_get_free_heap_size());

  running = true;
  uint64_t frame_start;
  uint64_t frame_count = 0;
  bool game_over_video_played = false;

  // Battery optimization: Audio silence detection
  uint32_t silence_frames = 0;
  const uint32_t SILENCE_THRESHOLD = 120; // 2 seconds @ 60fps
  bool audio_powered = true;

  // Battery optimization: Adaptive backlight dimming
  uint32_t idle_frames = 0;
  const uint32_t IDLE_DIM_THRESHOLD = 1800; // 30 seconds @ 60fps
  uint8_t current_brightness = DISPLAY_BRIGHTNESS_ACTIVE;

  // Battery optimization: CPU frequency scaling
  bool cpu_low_power = false;

  // Emulation runs in real time even when rendering can't keep 60 fps:
  // each loop iteration runs as many 1/60 s emulated frames as wall-clock
  // time owes (the display transfer alone takes ~15 ms), so game speed and
  // music tempo stay correct and only the on-screen frame rate drops.
  int64_t emu_accum_us = 0;
  int64_t emu_last_us = esp_timer_get_time();
  bool emu_realtime = false;  // only during play; attract mode stays at 1 frame per loop
#if PELLETINO_DEBUG
  uint32_t emu_frames_period = 0;
#endif

  while (running) {
    frame_start = esp_timer_get_time();

    // 1. Run the Z80 for one or more 1/60 s frames (~51,200 cycles each),
    //    with a VBLANK interrupt after each.
    int frames_to_run = 1;
    emu_accum_us += frame_start - emu_last_us;
    emu_last_us = frame_start;
    if (emu_realtime) {
      const int64_t frame_us = (int64_t)FRAME_TIME_US;  // signed copy: -FRAME_TIME_US on the
                                                        // uint32 constant would wrap positive
      frames_to_run = (int)(emu_accum_us / frame_us);
      if (frames_to_run < 1) frames_to_run = 1;
      if (frames_to_run > 3) frames_to_run = 3;  // don't spiral after a stall (e.g. video)
      emu_accum_us -= (int64_t)frames_to_run * frame_us;
      if (emu_accum_us > frame_us) emu_accum_us = frame_us;
      if (emu_accum_us < -frame_us) emu_accum_us = -frame_us;
    } else {
      emu_accum_us = 0;
    }
    for (int i = 0; i < frames_to_run; i++) {
      pacman_run_frame();
      pacman_vblank_interrupt();
    }
#if PELLETINO_DEBUG
    emu_frames_period += frames_to_run;
    uint64_t t_z80 = esp_timer_get_time();
#endif

    // 2. Render display (uses DMA, interleaved with audio)
    pacman_render_screen();
#if PELLETINO_DEBUG
    uint64_t t_render = esp_timer_get_time();
#endif

    // 3. Update audio buffer
    audio_update();
#if PELLETINO_DEBUG
    uint64_t t_audio = esp_timer_get_time();
#endif

    // 4. Poll input
    pacman_poll_input();
#if PELLETINO_DEBUG
    uint64_t t_input = esp_timer_get_time();
    static uint64_t acc_z80 = 0, acc_render = 0, acc_audio = 0, acc_input = 0;
    acc_z80 += t_z80 - frame_start;
    acc_render += t_render - t_z80;
    acc_audio += t_audio - t_render;
    acc_input += t_input - t_audio;
    if ((frame_count + 1) % 300 == 0) {
      ESP_LOGI(TAG, "avg us/loop: z80 %llu, render %llu, audio %llu, input %llu; emulated frames in last 300 loops: %lu",
               acc_z80 / 300, acc_render / 300, acc_audio / 300, acc_input / 300, (unsigned long)emu_frames_period);
      acc_z80 = acc_render = acc_audio = acc_input = 0;
      emu_frames_period = 0;
    }
#endif

    // 5. Battery optimization: Detect audio silence and power down amplifier
    // Also respect mute state - keep amplifier off when muted
    extern bool audio_get_mute(void);
    bool is_muted = audio_get_mute();
    bool is_silent = true;
    uint8_t* sound_regs = audio_get_sound_registers();
    for (int ch = 0; ch < 3; ch++) {
      if (sound_regs[ch * 5 + 0x15] & 0x0F) { // Check volume for each channel
        is_silent = false;
        break;
      }
    }

    // When muted, always keep amplifier off
    if (is_muted) {
      if (audio_powered) {
        audio_set_power_state(false);
        audio_powered = false;
      }
      silence_frames = 0; // Reset counter
    } else if (is_silent) {
      silence_frames++;
      if (silence_frames == SILENCE_THRESHOLD && audio_powered) {
        audio_set_power_state(false);
        audio_powered = false;
      }
    } else {
      if (!audio_powered) {
        audio_set_power_state(true);
        audio_powered = true;
      }
      silence_frames = 0;
    }

    // 6. Battery optimization: CPU frequency scaling
    // Check game state to determine if actively playing
    const uint8_t* memory = pacman_get_memory();
    uint8_t game_mode = memory ? memory[PACMAN_ADDR_GAME_STATE - 0x4000] : 0;
    bool is_playing = (game_mode >= 0x02);  // 0x01=attract, 0x02+=active game
    emu_realtime = is_playing;
    
    if (is_playing && cpu_low_power) {
      // Switched off: Dynamic frequency scaling kills DMA transfers
      // esp_pm_config_t pm_config = { .max_freq_mhz = 160, .min_freq_mhz = 160, .light_sleep_enable = false };
      // esp_pm_configure(&pm_config);
      cpu_low_power = false;
      // ESP_LOGI(TAG, "CPU frequency: 160MHz (active gameplay)");
    } else if (!is_playing && !cpu_low_power) {
      // Switched off: Dynamic frequency scaling crashes ESP32-C6 during transitions
      // esp_pm_config_t pm_config = { .max_freq_mhz = 80, .min_freq_mhz = 80, .light_sleep_enable = true };
      // esp_pm_configure(&pm_config);
      cpu_low_power = true;
      // ESP_LOGI(TAG, "CPU frequency: 80MHz (attract mode)");
    }

    // 7. Battery optimization: Adaptive backlight dimming
    // Reset idle counter when actively playing (game mode >= 0x02)
    if (is_playing) {
      idle_frames = 0;
    } else {
      idle_frames++;
    }
    if (idle_frames >= IDLE_DIM_THRESHOLD) {
      if (current_brightness != DISPLAY_BRIGHTNESS_IDLE) {
        display_set_backlight(DISPLAY_BRIGHTNESS_IDLE);
        current_brightness = DISPLAY_BRIGHTNESS_IDLE;
        ESP_LOGI(TAG, "Backlight dimmed to 25%% (idle)");
      }
    } else if (current_brightness != DISPLAY_BRIGHTNESS_ACTIVE) {
      display_set_backlight(DISPLAY_BRIGHTNESS_ACTIVE);
      current_brightness = DISPLAY_BRIGHTNESS_ACTIVE;
      ESP_LOGI(TAG, "Backlight restored to 50%% (active)");
    }

    // 9. Check for attract mode start (after arcade boot or after game over) and play video
    if (check_attract_mode_start(pacman_get_memory())) {
      static bool first_attract_entry = true;

      if (first_attract_entry) {
         first_attract_entry = false;
      }
      
      ESP_LOGI(TAG, "Attract mode starting - playing FIESTA video...");
      /* Temporarily boost CPU for video decode (Runs at constant 160MHz now anyway, no scaling to crash)
      esp_pm_config_t pm_video = {
        .max_freq_mhz = 160,
        .min_freq_mhz = 160,
        .light_sleep_enable = false
      };
      esp_pm_configure(&pm_video);
      */
      play_fiesta_video();
      emu_last_us = esp_timer_get_time();  // don't count video time as owed emulation
      emu_accum_us = 0;
      // Restore low power for attract mode - disabled!
      /* esp_pm_config_t pm_low = {
        .max_freq_mhz = 80,
        .min_freq_mhz = 80,
        .light_sleep_enable = true
      };
      esp_pm_configure(&pm_low);
      */
      // Clear any accumulated credits so attract mode plays demo
      // instead of waiting for START button press
      clear_credits(pacman_get_memory_rw());
      ESP_LOGI(TAG, "Video complete, attract mode will continue");
    }

    // Frame timing - wait for 16.667ms total
    // Frame timing - wait for 16.667ms total (60fps) or 33.333ms (30fps) for attract mode
    uint64_t elapsed = esp_timer_get_time() - frame_start;
    // Target 60fps for gameplay, 30fps for attract mode to save power
    uint32_t target_frame_time = is_playing ? FRAME_TIME_US : (FRAME_TIME_US * 2);

    if (elapsed < target_frame_time) {
      vTaskDelay(pdMS_TO_TICKS((target_frame_time - elapsed) / 1000));
    }

    frame_count++;
    if (frame_count % 300 == 0) { // Every 5 seconds
      ESP_LOGI(TAG, "Frame %llu, elapsed: %llu us, audio underruns: %lu", frame_count, elapsed,
               (unsigned long)audio_get_underrun_count());
    }
  }
}
