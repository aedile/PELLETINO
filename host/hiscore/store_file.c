/*
 * store_file.c - the score keeper's flash, as a file, for the games' host harnesses.
 * Set PELLETINO_SCORES to a path and scores are kept there between runs; leave it
 * unset and nothing is kept.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "medalboot.h"

bool medalboot_load_blob(const char *rom, void *buf, size_t len)
{
    const char *path = getenv("PELLETINO_SCORES");
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    bool ok = (size_t)ftell(f) == len;
    rewind(f);
    ok = ok && fread(buf, 1, len, f) == len;
    fclose(f);
    if (ok) printf("scores: %s restored %u bytes\n", rom, (unsigned)len);
    return ok;
}

void medalboot_save_blob(const char *rom, const void *buf, size_t len)
{
    const char *path = getenv("PELLETINO_SCORES");
    FILE *f = path ? fopen(path, "wb") : NULL;
    if (!f) return;
    fwrite(buf, 1, len, f);
    fclose(f);
    printf("scores: %s saved %u bytes:", rom, (unsigned)len);
    for (size_t i = 0; i < len && i < 48; i++) printf(" %02x", ((const uint8_t *)buf)[i]);
    printf("\n");
}

void medalboot_set_highscore(const char *rom, uint32_t score) { printf("scores: %s best is %u\n", rom, (unsigned)score); }

/*
 * For looking at what a game keeps, and for putting a score there without playing for it:
 *   PELLETINO_POKE="20:60b8=00,60b9=45"   at 20 seconds, write these bytes (hex)
 * and at the end of the run every range is printed.
 */
#include "hiscore.h"
void host_scores_frame(const hiscore_t *g, double now)
{
    /* PELLETINO_WATCH=4030 prints that byte whenever it changes */
    static int last = -1;
    const char *w = getenv("PELLETINO_WATCH");
    if (w) {
        uint8_t *m = g->mem((uint16_t)strtol(w, NULL, 16));
        if (m && *m != last) { last = *m; printf("scores: %.2fs %s = %02x\n", now, w, *m); }
    }
    static int done;
    const char *p = getenv("PELLETINO_POKE");
    if (done || !p || now < atof(p)) return;
    done = 1;
    p = strchr(p, ':');
    while (p && *++p) {
        unsigned a, v;
        if (sscanf(p, "%x=%x", &a, &v) != 2) break;
        uint8_t *m = g->mem((uint16_t)a);
        if (m) *m = (uint8_t)v;
        p = strchr(p, ',');
    }
    printf("scores: poked at %.1fs\n", now);
}

/* PELLETINO_FIND=12000: every place in memory that could be that number, however it is written */
static void find(const hiscore_t *g, unsigned long n)
{
    char dec[16]; int nd = snprintf(dec, sizeof dec, "%lu", n);
    for (unsigned a = 0; a < 0x10000; a++) {
        uint8_t b[8]; int have = 0;
        for (; have < 8; have++) { uint8_t *m = g->mem((uint16_t)(a + have)); if (!m) break; b[have] = *m; }
        if (have < 2) continue;
        for (unsigned long div = 1; div <= 100; div *= 10) {
            if (n % div) continue;
            unsigned long v = n / div;
            /* BCD, either end first, two to four bytes */
            for (int len = 2; len <= 4 && len <= have; len++) {
                unsigned long lo = 0, hi = 0; int ok = 1;
                for (int i = 0; i < len; i++) {
                    if ((b[i] >> 4) > 9 || (b[i] & 15) > 9) ok = 0;
                    hi = hi * 100 + (b[i] >> 4) * 10 + (b[i] & 15);
                    int k = len - 1 - i;
                    lo = lo * 100 + (b[k] >> 4) * 10 + (b[k] & 15);
                }
                if (ok && v && hi == v) printf("scores: found %lu at %04x: BCD, %d bytes, high byte first, x%lu\n", n, a, len, div);
                if (ok && v && lo == v && lo != hi) printf("scores: found %lu at %04x: BCD, %d bytes, low byte first, x%lu\n", n, a, len, div);
            }
            /* binary, two or three bytes */
            for (int len = 2; len <= 3 && len <= have; len++) {
                unsigned long le = 0, be = 0;
                for (int i = 0; i < len; i++) { be = (be << 8) | b[i]; le |= (unsigned long)b[i] << (8 * i); }
                if (v > 255 && le == v) printf("scores: found %lu at %04x: binary, %d bytes, low byte first, x%lu\n", n, a, len, div);
                if (v > 255 && be == v) printf("scores: found %lu at %04x: binary, %d bytes, high byte first, x%lu\n", n, a, len, div);
            }
        }
        /* one digit a byte, in the low nibble, either way round */
        if (nd <= have && nd >= 4) {
            int fwd = 1, rev = 1;
            for (int i = 0; i < nd; i++) {
                if ((b[i] & 15) != (unsigned)(dec[i] - '0')) fwd = 0;
                if ((b[i] & 15) != (unsigned)(dec[nd - 1 - i] - '0')) rev = 0;
                if ((b[i] & 0xf0) != (b[0] & 0xf0)) fwd = rev = 0;
            }
            if (fwd) printf("scores: found %lu at %04x: a digit a byte (%02x is 0), first digit first\n", n, a, b[0] & 0xf0);
            if (rev) printf("scores: found %lu at %04x: a digit a byte (%02x is 0), units first\n", n, a, b[0] & 0xf0);
        }
    }
}

void host_scores_dump(const hiscore_t *g)
{
    const char *f = getenv("PELLETINO_FIND");
    if (f) find(g, strtoul(f, NULL, 10));
    for (int r = 0; r < g->nranges; r++) {
        printf("scores: %04x:", g->ranges[r].addr);
        for (int i = 0; i < g->ranges[r].len && i < 256; i++) {
            uint8_t *m = g->mem((uint16_t)(g->ranges[r].addr + i));
            if (m) printf(" %02x", *m); else printf(" --");
        }
        printf("\n");
    }
    printf("scores: best reads %u\n", (unsigned)hiscore_top());
}
