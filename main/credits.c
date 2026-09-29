/*
 * credits.c - everyone this medal owes something to, scrolling past.
 *
 * It is a menu entry like any game, but it lives in the launcher: games.toml
 * marks it `builtin`, so it has a marquee and no flash slot.
 *
 * A line starting '#' is a heading, '~' is set dimmer, anything else is plain,
 * and a tab splits a line into two columns set against the left and right
 * margins. The font is 8 pixels wide and the panel 240, with rounded corners, so
 * the roll keeps a 16 pixel margin each side: 26 characters to a line, 13 to a
 * heading.
 *
 * The music section is not written here, because the music is not ours and is
 * not shipped: it is whatever is in music/credits.txt, which sits beside the
 * files it describes. See music/README.md.
 */
#include "credits.h"
#include "menu.h"
#include "input.h"
#include "battery.h"
#include "sound.h"
#include "fest.h"
#include "chiptune.h"
#include "audio_hal.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define FPS        30
#define LINE_H     12
#define HEAD_H     30
#define MAX_EXTRA  24
#define MARGIN     16
#define COLS       ((FB_W - 2 * MARGIN) / 8)      /* 26 */

static const char *const before_music[] = {
    "#PELLETINO",
    "a pocket arcade",
    "",
    "",
    "#MADE BY",
    "Jesse Castro",
    "",
    "",
    "#THE GAMES",
    "~and the people behind them",
    "",
    "Pac-Man\tNamco 1980",
    "Ms. Pac-Man\tMidway 1982",
    "Galaxian\tNamco 1979",
    "Galaga\tNamco 1981",
    "Dig Dug\tNamco 1982",
    "Rally-X\tNamco 1980",
    "Pole Position\tNamco 1982",
    "Donkey Kong\tNintendo 1981",
    "Mario Bros.\tNintendo 1983",
    "Space Invaders\tTaito 1978",
    "Arkanoid\tTaito 1986",
    "Lunar Lander\tAtari 1979",
    "Asteroids\tAtari 1979",
    "Missile Command\tAtari 1980",
    "Centipede\tAtari 1981",
    "Tempest\tAtari 1981",
    "Star Wars\tAtari 1983",
    "Frogger\tKonami 1981",
    "Time Pilot\tKonami 1982",
    "Gyruss\tKonami 1983",
    "Joust\tWilliams 1982",
    "Moon Patrol\tIrem 1982",
    "Mr. Do!\tUniversal 1982",
    "Burger Time\tData East 1982",
    "Tapper\tBally Midway 1984",
    "Street Fighter II\tCapcom 1991",
    "",
    "~Every game belongs to its",
    "~maker. No ROMs ship with",
    "~this project.",
    "",
    "",
    "#EMULATION",
    "",
    "Z80 core",
    "~Marat Fayzullin",
    "",
    "6809 core",
    "~vecx - V. Manohararajah",
    "",
    "6502 core (Centipede)",
    "~chips - Andre Weissflog",
    "",
    "Hardware reference",
    "~the MAME team",
    "~Nicola Salmoria,",
    "~Aaron Giles and every",
    "~contributor since",
    "",
    "",
    "#SOFTWARE",
    "",
    "MIDI parsing",
    "~TinyMidiLoader",
    "~Bernhard Schelling",
    "",
    "MP3 decoding",
    "~Helix - RealNetworks",
    "",
    "Font",
    "~font8x8 - Daniel Hepper",
    "",
    "ESP-IDF",
    "~Espressif Systems",
    "",
    "Inspiration",
    "~Galagino - Till Harbaum",
    "",
    "",
    "#MUSIC",
    "",
};
static const char *const no_music[] = {
    "~none supplied with",
    "~this build",
};
static const char *const after_music[] = {
    "",
    "",
    "#THANK YOU",
    "for playing",
    "",
    "~github.com/aedile",
    "~/PELLETINO",
};

#define COUNT(a) ((int)(sizeof(a) / sizeof *(a)))

#ifdef HAVE_MUSIC_CREDITS
extern const char music_txt_start[] asm("_binary_credits_txt_start");
extern const char music_txt_end[]   asm("_binary_credits_txt_end");
static char        extra_buf[1024];
#endif
static const char *extra[MAX_EXTRA];
static int         n_extra;

/* split music/credits.txt into lines, clipped to what the panel can show */
static void load_extra(void)
{
    n_extra = 0;
#ifdef HAVE_MUSIC_CREDITS
    size_t len = (size_t)(music_txt_end - music_txt_start);
    if (len >= sizeof extra_buf) len = sizeof extra_buf - 1;
    memcpy(extra_buf, music_txt_start, len);
    extra_buf[len] = 0;
    char *p = extra_buf;
    while (*p && n_extra < MAX_EXTRA) {
        char *eol = strchr(p, '\n');
        if (eol) *eol = 0;
        size_t n = strlen(p);
        if (n && p[n - 1] == '\r') p[--n] = 0;
        if (n > COLS) p[COLS] = 0;
        extra[n_extra++] = p;
        if (!eol) break;
        p = eol + 1;
    }
    while (n_extra && !*extra[n_extra - 1]) n_extra--;      /* trailing blank lines */
#endif
}

static int total_lines(void)
{
    return COUNT(before_music) + (n_extra ? n_extra : COUNT(no_music)) + COUNT(after_music);
}

static const char *line_at(int i)
{
    if (i < COUNT(before_music)) return before_music[i];
    i -= COUNT(before_music);
    int nm = n_extra ? n_extra : COUNT(no_music);
    if (i < nm) return n_extra ? extra[i] : no_music[i];
    return after_music[i - nm];
}

static int height_of(const char *s) { return s[0] == '#' ? HEAD_H : LINE_H; }

static void draw_line(const char *s, int y)
{
    if (!*s) return;
    if (s[0] == '#') {
        int w = 16 * (int)strlen(s + 1);
        fest_text_scaled((FB_W - w) / 2, y + 4, s + 1, arcade_colours[2], 2);
    } else if (s[0] == '~') {
        fest_text_center(y, s + 1, CUBE(3,3,4));
    } else if (strchr(s, '\t')) {
        /* two columns: the game against the left margin, its maker against the right */
        char left[COLS + 1];
        const char *tab = strchr(s, '\t'), *right = tab + 1;
        size_t n = (size_t)(tab - s);
        if (n > COLS) n = COLS;
        memcpy(left, s, n); left[n] = 0;
        fest_text(MARGIN, y, left, UI_WHITE);
        fest_text(FB_W - MARGIN - 8 * (int)strlen(right), y, right, CUBE(3,3,4));
    } else {
        fest_text_center(y, s, UI_WHITE);
    }
}

static void wait_release(void)
{
    while (input_button_down()) { audio_update(); vTaskDelay(pdMS_TO_TICKS(10)); }
}

bool credits_scene(void)
{
    load_extra();
    int n = total_lines(), height = 0;
    for (int i = 0; i < n; i++) height += height_of(line_at(i));

    int64_t start = esp_timer_get_time();
    for (int t = 0; ; t++) {
        audio_update();
        input_poll();                       /* the power button still has to work */
        battery_tick();
        sound_poll();                       /* both buttons: sound off/on */
        if (input_take_wake()) return true;

        int top = FB_H - t;                 /* one pixel a frame, rising from the bottom */
        if (top + height < 0) return false; /* the last line has left the screen */

        fest_clear(UI_BLACK);
        fest_stars(t);
        int y = top;
        for (int i = 0; i < n; i++) {
            const char *s = line_at(i);
            int h = height_of(s);
            if (y > -h && y < FB_H) draw_line(s, y);
            y += h;
        }
        fest_scanlines();
        fest_present();

        int64_t due = start + (int64_t)(t + 1) * (1000000 / FPS);
        while (esp_timer_get_time() < due) { audio_update(); vTaskDelay(1); }
    }
}

/* Chosen from the carousel. It has a tune of its own if one was supplied, and
 * gives the menu its music back afterwards either way. */
void credits_run(void)
{
    if (!fest_init()) return;
    fest_crt = false;                       /* the roll draws its own scanlines */

    bool own_tune = chip_available(CHIP_TUNE_CREDITS);
    if (own_tune && chip_load(CHIP_TUNE_CREDITS)) chip_play();

    wait_release();                         /* the hold that chose this is still down */
    input_poll();
    input_take_wake();                      /* ...and letting go of it is not "leave" */
    input_take_nav();
    credits_scene();

    if (own_tune && chip_load(CHIP_TUNE_SPLASH)) chip_play();
    wait_release();                         /* or the same press starts a new hold in the menu */
    input_take_hold();
    input_take_nav();
    menu_init();
}
