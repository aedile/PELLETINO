#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MENU_BROWSE = 0,     /* the wheel, with the hold-to-pick progress bar */
    MENU_LAUNCHING,      /* the picked game while it boots */
    MENU_MESSAGE,        /* two lines of text, nothing else */
} menu_mode_t;

void        menu_init(void);
void        menu_release(void);  /* hand the frame buffer back; menu_init() takes it again */
void        menu_set_mode(menu_mode_t m);
void        menu_nav(int delta); /* turn the wheel; it takes a few frames to get there */
void        menu_select_rom(const char *rom);   /* open the wheel on this game, no animation */
void        menu_render(void);   /* one frame; call it thirty times a second */
const char *menu_current_rom(void);
const char *menu_current_title(void);
void        menu_show_message(const char *line1, const char *line2);

#ifdef __cplusplus
}
#endif
