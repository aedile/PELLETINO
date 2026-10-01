/* hiscore.c - see hiscore.h */
#include "hiscore.h"
#include "medalboot.h"
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_log.h"
#define SAY(...) ESP_LOGI("hiscore", __VA_ARGS__)
#else
#define SAY(...) ((void)0)
#endif

#define CHECK_EVERY   30        /* frames between looks at the table */
#define SETTLED       10        /* looks it has to stay the same for: about five seconds */

static const hiscore_t *g;
#define TABLE_MAX 2048          /* the largest table any game keeps: Pole Position's whole 2 KB */

static uint8_t *seen, *saved;   /* the table as last looked at, and as last written to flash */
static uint8_t  now[TABLE_MAX]; /* and as it is this frame */
static int      total;
static enum { IDLE, WAITING, WATCHING } state;
static int      frame, still, waited;
static unsigned run;             /* frames since the game started */

#define STARTED 600             /* frames before battery-backed memory is believed: ten seconds */

static uint8_t *at(uint16_t a) { return g->mem ? g->mem(a) : NULL; }

static void copy_out(uint8_t *dst)
{
    for (int r = 0; r < g->nranges; r++)
        for (int i = 0; i < g->ranges[r].len; i++) {
            uint8_t *p = at((uint16_t)(g->ranges[r].addr + i));
            *dst++ = p ? *p : 0;
        }
}

static void copy_in(const uint8_t *src)
{
    for (int r = 0; r < g->nranges; r++)
        for (int i = 0; i < g->ranges[r].len; i++, src++) {
            uint8_t *p = at((uint16_t)(g->ranges[r].addr + i));
            if (p) *p = *src;
        }
}

/* every range has to show the game's own values at both ends, not just the first: a
 * table that reads as zeros is what memory looks like before the game has touched it too */
static bool set_up(void)
{
    for (int i = 0; i < g->nranges; i++) {
        const hiscore_range_t *r = &g->ranges[i];
        uint8_t *a = at(r->addr), *b = at((uint16_t)(r->addr + r->len - 1));
        if (!a || !b || *a != r->first || *b != r->last) return false;
    }
    return true;
}

static void restore(void)
{
    if (medalboot_load_blob(g->rom, seen, (size_t)total)) {
        copy_in(seen);
        memcpy(saved, seen, (size_t)total);
        SAY("%s: %d bytes of scores put back, best %u", g->rom, total, (unsigned)hiscore_top());
    } else {
        if (g->whole) SAY("%s: nothing kept yet", g->rom);     /* its memory means nothing until the game has run */
        else SAY("%s: nothing kept yet, best %u", g->rom, (unsigned)hiscore_top());
        copy_out(saved);                /* nothing kept yet: what the game starts with is the baseline */
    }
    state = WATCHING;
    still = 0;
}

void hiscore_begin(const hiscore_t *game)
{
    state = IDLE;
    free(seen); free(saved); seen = saved = NULL;
    g = game;
    if (!g || !g->rom || !g->mem || g->nranges <= 0) return;

    total = 0;
    for (int r = 0; r < g->nranges; r++) {
        if (g->ranges[r].len == 0) return;
        total += g->ranges[r].len;
    }
    if (total > TABLE_MAX) { SAY("%s: a %d-byte table is more than the %d this keeps", g->rom, total, TABLE_MAX); return; }
    seen  = malloc((size_t)total);
    saved = malloc((size_t)total);
    if (!seen || !saved) { free(seen); free(saved); seen = saved = NULL; return; }

    frame = waited = 0;
    run = 0;
    state = WAITING;
    if (g->whole) restore();
}

uint32_t hiscore_top(void)
{
    if (!g) return 0;
    if (g->top) return g->top();
    if (g->format == HISCORE_NO_NUMBER || !g->top_len) return 0;
    uint32_t v = 0;
    for (int i = 0; i < g->top_len; i++) {
        int k = g->top_msb_first ? i : g->top_len - 1 - i;
        uint8_t *p = at((uint16_t)(g->top_addr + k));
        if (!p) return 0;
        if (g->format == HISCORE_BINARY) {
            v = (v << 8) | *p;
        } else if (g->format == HISCORE_BCD) {
            unsigned hi = *p >> 4, lo = *p & 15;
            if (hi > 9 || lo > 9) return 0;
            v = v * 100 + hi * 10 + lo;
        } else {
            unsigned d = *p == g->digit_blank ? 0 : (unsigned)(*p - g->digit_zero);
            if (d > 9) return 0;
            v = v * 10 + d;
        }
    }
    return v * (g->top_times ? g->top_times : 1);
}

static void save(void)
{
    copy_out(seen);
    if (memcmp(seen, saved, (size_t)total) == 0) return;
    medalboot_save_blob(g->rom, seen, (size_t)total);
    memcpy(saved, seen, (size_t)total);
    uint32_t top = hiscore_top();
    if (top) medalboot_set_highscore(g->rom, top);
}

void hiscore_frame(void)
{
    if (state == IDLE) return;
    run++;

    /* every frame, while waiting: some games only show the values being waited for
     * for a frame at a time */
    if (state == WAITING) {
        if (waited < g->wait_frames) { waited++; return; }
        if (set_up()) restore();
        return;
    }
    if (++frame % CHECK_EVERY) return;
    if (g->whole) return;                       /* saved on the way out, and only then */

    copy_out(now);
    if (memcmp(now, seen, (size_t)total) != 0) {
        memcpy(seen, now, (size_t)total);       /* still moving */
        still = 0;
    } else if (++still == SETTLED && memcmp(now, saved, (size_t)total) != 0) {
        save();
    }
}

void hiscore_flush(void)
{
    if (state != WATCHING) return;              /* the game never got as far as having a table */
    /* Battery-backed memory is put back before the game has run at all, and the game then
     * spends its first seconds checking it and filling in what is missing. Left for the
     * menu in those seconds, what is there is not yet worth keeping. */
    if (g->whole && run < STARTED) return;
    save();
}

/* medal_input calls this before it cuts the power, if it is linked */
void medal_before_power_off(void) { hiscore_flush(); }
