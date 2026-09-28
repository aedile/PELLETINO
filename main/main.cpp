/*
 * main.cpp - PELLETINO launcher.
 *
 * The medal has a sticky selection: once a game is picked it boots straight into
 * it, skipping this menu entirely, until someone deliberately comes back. So most
 * of the time this code runs for a few milliseconds and hands over.
 *
 *   nothing selected            -> browse
 *   selected, installed         -> boot it
 *   button held at power-on     -> forget the selection, browse
 *   selected but never confirms -> after MEDALBOOT_MAX_ATTEMPTS, browse
 */
#include "menu.h"
#include "input.h"
#include "games.h"
#include "mqart.h"
#include "medalboot.h"
#include "splash.h"
#include "battery.h"
#include "audio_hal.h"
#include "display.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "minimame";

#define BOOT_ESCAPE_MS  600      /* hold the button this long at power-on for the menu */
#define HOLD_REDRAW_MS   50      /* repaint rate while the progress bar is filling */

/* True only if the button stayed down for the whole window - a knock will not do. */
static bool button_held_at_boot(void)
{
    for (int t = 0; t < BOOT_ESCAPE_MS; t += 20) {
        if (!input_button_down()) return false;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return true;
}

static void launch(const char *rom)
{
    menu_init();                        /* may not have been needed until now */
    /* A shared slot (Pac-Man riding Ms. Pac-Man's image) records its own ROM as the
     * selection - so the image knows which variant to run - but chain-boots the slot
     * owner's partition. For every other game the boot label is the ROM itself. */
    menu_select_rom(rom);
    menu_set_mode(MENU_LAUNCHING);
    menu_render();
    medalboot_note_attempt();
    game_launch(mqart_boot_label(rom));   /* does not return */
}

extern "C" void app_main(void)
{
    esp_err_t nv = nvs_flash_init();
    if (nv == ESP_ERR_NVS_NO_FREE_PAGES || nv == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    battery_init();          /* holds the rail up: do this before anything else */
    display_init();
    display_set_backlight(DISPLAY_BRIGHTNESS_ACTIVE);

    if (mqart_init() != ESP_OK) {
        ESP_LOGE(TAG, "no marquee data");
        menu_init();
        menu_show_message("NO ARTWORK", "FLASH THE MQART PARTITION");
        menu_render();
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    input_init();

    /*
     * How we got here decides whether to auto-boot. A power-on or brownout with a game selected
     * means boot straight into it - that is the sticky selection working. A software restart
     * can only be a game that asked to leave via medalboot_exit_to_menu(): show the menu, and
     * clear the selection here too, in case the game image could not (an older image without
     * NVS initialised failed at that step silently and the restart landed straight back in the
     * game - the exit gesture appeared to relaunch instead).
     */
    char sel[24];
    if (esp_reset_reason() == ESP_RST_SW) {
        ESP_LOGI(TAG, "software restart - a game asked for the menu");
        medalboot_clear_selected();
        while (input_button_down()) vTaskDelay(pdMS_TO_TICKS(20));   /* the exit hold is probably still down */
    } else if (button_held_at_boot()) {
        ESP_LOGI(TAG, "button held at boot - clearing the selection");
        medalboot_clear_selected();
        /* Wait for release so the same press does not immediately pick a game. */
        while (input_button_down()) vTaskDelay(pdMS_TO_TICKS(20));
    } else if (medalboot_get_selected(sel, sizeof sel)) {
        if (!game_installed(mqart_boot_label(sel))) {
            ESP_LOGW(TAG, "%s is selected but not installed", sel);
            medalboot_clear_selected();
        } else if (medalboot_attempts() >= MEDALBOOT_MAX_ATTEMPTS) {
            ESP_LOGW(TAG, "%s failed to start %d times", sel, medalboot_attempts());
            medalboot_clear_selected();
            menu_select_rom(sel);
            menu_show_message("COULD NOT START", "HOLD TO PICK ANOTHER");
            menu_render();
            vTaskDelay(pdMS_TO_TICKS(2500));
            menu_set_mode(MENU_BROWSE);
        } else {
            launch(sel);         /* does not return */
        }
    }

    /* Only on the way to the menu - a selected medal boots straight into its game
     * above, and nobody wants six seconds of titles in front of every launch. */
    audio_init();

    /* The splash runs before the menu allocates. Its frame buffer is 178 KB and
     * the song parses into a linked list about as large; both at once does not
     * fit in 420 KB, and tml_load_memory does not survive a failed malloc. */
    splash_run();
    menu_init();

    /* Open the carousel on whatever was played last. */
    char last[24];
    if (medalboot_get_last(last, sizeof last)) menu_select_rom(last);
    menu_set_mode(MENU_BROWSE);

    ESP_LOGI(TAG, "%d games in the carousel", mqart_count());

    bool dirty = true;
    int64_t last_hold_draw = 0;
    while (true) {
        input_poll();
        battery_tick();

        nav_t nav = input_take_nav();
        if (nav == NAV_NEXT) { menu_nav(+1); dirty = true; }
        else if (nav == NAV_PREV) { menu_nav(-1); dirty = true; }

        if (input_take_hold()) {
            const char *rom = menu_current_rom();
            if (rom && game_installed(mqart_boot_label(rom))) {
                medalboot_set_selected(rom);   /* sticky from now on */
                launch(rom);                   /* does not return */
            }
            dirty = true;                      /* not installed: repaint */
        }

        /*
         * Animate the progress bar without repainting at full loop rate, and without
         * repainting anything but the rows it occupies - a full repaint here is what made the
         * panel wipe in strips while the button was held.
         */
        int64_t now = esp_timer_get_time();
        bool bar_only = false;
        if (input_hold_ms() > 0 && now - last_hold_draw >= HOLD_REDRAW_MS * 1000) {
            last_hold_draw = now;
            if (!dirty) bar_only = true;
        }
        if (input_hold_ms() == 0 && last_hold_draw) { last_hold_draw = 0; if (!dirty) bar_only = true; }

        if (dirty)          { menu_render(); dirty = false; }
        else if (bar_only)  { menu_render_range(MENU_HOLD_BAR_Y0, MENU_HOLD_BAR_Y1); }

        vTaskDelay(pdMS_TO_TICKS(16));
    }
}
