/*
 * medalboot.h - which game the medal boots into, and how to get back out.
 *
 * Shared by the MINIMAME launcher and every game image. Copy this component into
 * a game project (or point at it with EXTRA_COMPONENT_DIRS) so both ends agree
 * on the NVS keys.
 *
 * The medal has a sticky selection. Once you pick a game it boots straight to it
 * every time, skipping the menu, until you deliberately come back.
 *
 *   launcher   hold the button on a game  -> medalboot_set_selected(rom), reboot
 *   game       hold the button for 10 s   -> medalboot_exit_to_menu()
 *   either     hold the button at power-on -> selection cleared, menu shown
 *
 * WHAT A GAME MUST DO
 *
 *   void app_main(void) {
 *       medalboot_game_startup();      // FIRST LINE. points boot back at the menu
 *       ...init...
 *       medalboot_game_running();      // once stable (a few seconds in)
 *       for (;;) {
 *           if (held_for_ms >= MEDALBOOT_EXIT_HOLD_MS) medalboot_exit_to_menu();
 *       }
 *   }
 *
 * medalboot_game_startup() is what makes a crash survivable: it points the boot
 * partition back at the launcher before anything risky runs, so a panic, a
 * watchdog bite or a brownout lands in the menu rather than boot-looping.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEDALBOOT_EXIT_HOLD_MS  10000   /* hold this long in a game to leave it */
#define MEDALBOOT_MAX_ATTEMPTS  3       /* give up auto-booting after this many */

/* --- launcher side --- */
bool medalboot_get_selected(char *out, size_t len);  /* false when nothing is picked */
void medalboot_set_selected(const char *rom);
void medalboot_clear_selected(void);
bool medalboot_get_last(char *out, size_t len);      /* where to open the menu */

int  medalboot_attempts(void);
void medalboot_note_attempt(void);

/* --- game side --- */
void medalboot_game_startup(void);   /* first line of app_main */
void medalboot_game_running(void);   /* clears the loop guard once stable */
void medalboot_exit_to_menu(void);   /* clears the selection and reboots */

/* Feed the button level every loop. Returns true once, when it has been held for
 * MEDALBOOT_EXIT_HOLD_MS - the game should then call medalboot_exit_to_menu().
 * Keeps the exit gesture identical across every medal. */
bool medalboot_exit_hold(bool button_down);

/* Which ROM set to run - a single image can serve several (PELLETINO carries both
 * Pac-Man and Ms. Pac-Man). Returns false if the game was not started by the menu. */
bool medalboot_rom(char *out, size_t len);

#ifdef __cplusplus
}
#endif
