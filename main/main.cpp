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
#include <string.h>
#include "menu.h"
#include "input.h"
#include "games.h"
#include "mqart.h"
#include "medalboot.h"
#include "splash.h"
#include "chiptune.h"
#include "fest.h"
#include "credits.h"
#include "howto.h"
#include "sound.h"
#include "battery.h"
#include "audio_hal.h"
#include "display.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "pelletino";

#define BOOT_ESCAPE_MS  600      /* hold the button this long at power-on for the menu */
#define MENU_FPS         30
#define MENU_IDLE_MS  45000      /* left alone in the menu this long, it goes back to attract mode */

/* True only if the button stayed down for the whole window - a knock will not do. */
static bool button_held_at_boot(void)
{
    for (int t = 0; t < BOOT_ESCAPE_MS; t += 20) {
        if (!input_button_down()) return false;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return true;
}

/* One frame of the menu, and then whatever is left of a thirtieth of a second. */
static void menu_frame(void)
{
    int64_t due = esp_timer_get_time() + 1000000 / MENU_FPS;
    audio_update();
    menu_render();
    while (esp_timer_get_time() < due) { audio_update(); vTaskDelay(1); }
}

/*
 * `chosen` is somebody picking the game just now, as against the launcher starting
 * the one picked last time: they get the send-off - the coin, the logo coming out
 * of the screen, the flash. A medal that is only switching on gets its game.
 */
static bool launch(const char *rom, bool chosen)
{
    menu_init();                        /* may not have been needed until now */
    /* A shared slot (Pac-Man riding Ms. Pac-Man's image) records its own ROM as the
     * selection - so the image knows which variant to run - but chain-boots the slot
     * owner's partition. For every other game the boot label is the ROM itself. */
    menu_select_rom(rom);
    if (chosen) {
        menu_begin_launch();
        while (!menu_launch_done()) menu_frame();
        for (int i = 0; i < 8; i++) menu_frame();       /* white, while the coin rings out */
    } else {
        menu_set_mode(MENU_LAUNCHING);
        menu_render();
    }
    chip_stop();
    audio_update();
    medalboot_note_attempt();
    if (game_launch(mqart_boot_label(rom))) return true;    /* does not return */

    /* It could not be booted - a slot that was never flashed, most likely. Undo what
     * was just recorded, or the medal would try the same thing at every power-on,
     * and give the menu back with its music. */
    ESP_LOGW(TAG, "%s would not boot", rom);
    medalboot_clear_selected();
    menu_set_mode(MENU_BROWSE);
    if (chip_has_music()) chip_play();
    display_toast("NOT INSTALLED", 2000);
    return false;
}

/*
 * The games, shown off: the wheel turning by itself, a game at a time, each with
 * its screenshot behind it. The menu draws it; this only turns the wheel.
 */
#define SHOWCASE_FRAMES 50              /* how long each game gets */

static bool showcase_scene(void)
{
    char was[24] = "";
    const char *rom = menu_current_rom();
    if (rom) strlcpy(was, rom, sizeof was);

    menu_init();                        /* the wheel draws with the panel's scanlines, not its own */
    menu_select_rom(mqart_get(0)->rom);
    menu_set_mode(MENU_SHOWCASE);

    bool pressed = false;
    for (int i = 0; i < mqart_count() && !pressed; i++) {
        if (i) menu_nav(+1);
        if (menu_current_is_builtin()) continue;        /* Credits is not a game */
        for (int t = 0; t < SHOWCASE_FRAMES && !pressed; t++) {
            input_poll();
            battery_tick();
            sound_poll();
            pressed = input_take_wake();
            menu_frame();
        }
    }
    menu_set_mode(MENU_BROWSE);
    menu_select_rom(was);
    fest_crt = false;
    return pressed;
}

/*
 * Attract mode: the title, how to work it, the games, the credits, and round
 * again, for as long as nobody touches it. The music is not its business - one
 * tune is started before the first of these and plays on through the menu and
 * back.
 *
 * A button ends it - either one, pressed and let go. Both buttons together is
 * the sound, and does not end it.
 */
static void attract(void)
{
    if (!fest_init()) { menu_init(); return; }  /* no room to draw: straight to the menu */
    fest_crt = false;                           /* these scenes draw their own scanlines */

    for (int pass = 1; ; pass++) {
        ESP_LOGI(TAG, "attract: title (pass %d)", pass);
        if (splash_scene()) break;
        ESP_LOGI(TAG, "attract: instructions");
        if (howto_scene()) break;
        ESP_LOGI(TAG, "attract: the games");
        if (showcase_scene()) break;
        ESP_LOGI(TAG, "attract: credits");
        if (credits_scene()) break;
    }
    ESP_LOGI(TAG, "attract: button pressed, on to the menu");

    input_take_hold();                          /* that press was "wake up", not "pick this" */
    input_take_nav();
    menu_init();
}

#ifdef PELLETINO_SELFTEST
/*
 * A launcher that tests itself, for when nobody can press the buttons: it starts
 * muted and skips attract mode, turns the wheel every other second, turns the
 * sound on after six, and reports once a second. After twelve it lets go, so
 * the menu should give up and return to attract mode. Nothing is saved.
 * Build it with PELLETINO_SELFTEST set in the environment idf.py runs in.
 */
static uint32_t selftest_frames, selftest_render_us;

static bool selftest_tick(void)             /* true while it is "pressing buttons" */
{
    static int64_t began;
    static int last = -1;
    int64_t now = esp_timer_get_time();
    if (!began) began = now;
    int s = (int)((now - began) / 1000000);
    if (s == last || s > 12) return s <= 12;
    last = s;

    uint32_t played; int peak;
    audio_get_stats(&played, &peak);
    ESP_LOGI(TAG, "selftest %2ds: %s, %u frames, %u ms each, played %u bytes, peak %d, heap %u (low %u)",
             s, audio_get_mute() ? "muted" : "sound on", (unsigned)selftest_frames,
             selftest_frames ? (unsigned)(selftest_render_us / selftest_frames / 1000) : 0,
             (unsigned)played, peak, (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());
    selftest_frames = selftest_render_us = 0;

    if (s == 6) { audio_set_mute(false); }
    if (s == 10) menu_begin_launch();
    if (s == 12) menu_set_mode(MENU_BROWSE);
    if (s & 1) menu_nav(s % 8 == 7 ? -1 : +1);
    return true;
}
#endif

#ifdef PELLETINO_TOUR
/*
 * A launcher for checking every game on a medal without touching it: each time it
 * starts it boots the next game on the wheel, and says which. Reset the board and it
 * moves on. Build it with PELLETINO_TOUR set in the environment idf.py runs in.
 */
static void tour(void)
{
    uint8_t next = 0;
    nvs_handle_t h;
    if (nvs_open("tour", NVS_READWRITE, &h) == ESP_OK) {
        nvs_get_u8(h, "next", &next);
        nvs_set_u8(h, "next", (uint8_t)(next + 1));
        nvs_commit(h);
        nvs_close(h);
    }
    int games = 0;
    for (int i = 0; i < mqart_count(); i++) {
        const mqart_entry_t *e = mqart_get(i);
        if (e->boot[0] != '@' && game_installed(e->boot)) games++;
    }
    if (!games) { ESP_LOGE(TAG, "tour: no games installed"); return; }
    int want = next % games;
#ifdef PELLETINO_TOUR_ONLY
    /* or always the one game named when it was built */
    ESP_LOGI(TAG, "tour: only %s", PELLETINO_TOUR_ONLY);
    medalboot_set_selected(PELLETINO_TOUR_ONLY);
    launch(PELLETINO_TOUR_ONLY, false);
#endif
    for (int i = 0; i < mqart_count(); i++) {
        const mqart_entry_t *e = mqart_get(i);
        if (e->boot[0] == '@' || !game_installed(e->boot)) continue;
        if (want-- == 0) {
            ESP_LOGI(TAG, "tour: %d of %d: %s (%s)", next % games + 1, games, e->title, e->rom);
            medalboot_set_selected(e->rom);
            launch(e->rom, false);
            ESP_LOGE(TAG, "tour: %s would not boot", e->rom);
        }
    }
}
#endif

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
#ifdef PELLETINO_TOUR
    tour();                     /* does not return unless there is nothing to boot */
#endif

    /*
     * How we got here decides whether to auto-boot. A power-on or brownout with a game selected
     * means boot straight into it - that is the sticky selection working. A software restart
     * can only be a game that asked to leave via medalboot_exit_to_menu(): show the menu, and
     * clear the selection here too, in case the game image could not (an older image without
     * NVS initialised failed at that step silently and the restart landed straight back in the
     * game - the exit gesture appeared to relaunch instead).
     */
    char sel[24];
    (void)sel;
    if (esp_reset_reason() == ESP_RST_SW) {
        ESP_LOGI(TAG, "software restart - a game asked for the menu");
        medalboot_clear_selected();
        while (input_button_down()) vTaskDelay(pdMS_TO_TICKS(20));   /* the exit hold is probably still down */
    } else if (button_held_at_boot()) {
        ESP_LOGI(TAG, "button held at boot - clearing the selection");
        medalboot_clear_selected();
        /* Wait for release so the same press does not immediately pick a game. */
        while (input_button_down()) vTaskDelay(pdMS_TO_TICKS(20));
#ifndef PELLETINO_SELFTEST                     /* the self-test is of the launcher: stay in it */
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
        } else if (launch(sel, false)) {   /* does not return */
        } else {
            menu_select_rom(sel);          /* it would not boot: the menu, open on it */
        }
#endif
    }

    /* Only on the way to the menu - with a game selected it boots straight into its game
     * above, and nobody wants twelve seconds of titles in front of every launch. */
    audio_init();
    sound_init();

    if (chip_load(CHIP_TUNE_SPLASH)) chip_play();
#ifdef PELLETINO_SELFTEST
    audio_set_mute(true);
    menu_init();
#else
    attract();
#endif

    /* Open the wheel on whatever was played last. */
    char last[24];
    if (medalboot_get_last(last, sizeof last)) menu_select_rom(last);
    menu_set_mode(MENU_BROWSE);

    ESP_LOGI(TAG, "%d entries on the wheel", mqart_count());

    int64_t touched = esp_timer_get_time();
    while (true) {
        audio_update();                         /* the music carries on in the menu */
        input_poll();
        battery_tick();

        bool busy = sound_poll();
        if (input_take_wake()) busy = true;
        if (input_hold_ms() > 0) busy = true;

        nav_t nav = input_take_nav();
        if (nav == NAV_NEXT) { menu_nav(+1); busy = true; }
        else if (nav == NAV_PREV) { menu_nav(-1); busy = true; }

        if (input_take_hold()) {
            const char *rom = menu_current_rom();
            const char *label = rom ? mqart_boot_label(rom) : NULL;
            if (label && label[0] == '@') {
                /* Built into the launcher. It is not a selection - nobody wants
                 * to boot to the credits - so nothing is recorded. */
                if (!strcmp(label, "@credits")) credits_run();
                menu_set_mode(MENU_BROWSE);
            } else if (rom && game_installed(mqart_boot_label(rom))) {
                medalboot_set_selected(rom);   /* sticky from now on, unless it will not boot */
                launch(rom, true);             /* does not return, unless it will not boot */
            }
            busy = true;
        }

#ifdef PELLETINO_SELFTEST
        if (selftest_tick()) busy = true;
#endif
        int64_t now = esp_timer_get_time();
        if (busy) touched = now;
        if (now - touched >= (int64_t)MENU_IDLE_MS * 1000) {
            ESP_LOGI(TAG, "nobody here - back to attract mode");
            attract();                          /* comes back when a button is pressed */
            touched = esp_timer_get_time();
            continue;
        }

        menu_frame();
#ifdef PELLETINO_SELFTEST
        selftest_frames++;
        selftest_render_us += (uint32_t)(esp_timer_get_time() - now);
#endif
    }
}
