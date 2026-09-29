/*
 * sfx.c - the launcher's sound effects, made rather than recorded.
 *
 * An effect is a handful of ringing partials, each a two-pole resonator with a
 * decay of its own, under a burst of noise. That covers a struck or drawn piece
 * of metal (many high partials, close pairs beating against each other, long
 * decays) and a ratchet click (one low partial, gone in a few milliseconds).
 *
 * It may also have a few notes, played one after another on a square wave: a
 * coin going in, a warning. And there is one held tone, which sounds for as
 * long as it is asked for and goes wherever it is told - the hold to pick a
 * game, rising as it fills.
 *
 * Effects are added to whatever the music rendered, and the sum is clipped.
 * Everything here runs from audio_render(), on the task that calls
 * audio_update(), so starting one is just a note left for the next render.
 */
#include "chiptune.h"
#include "sfx.h"
#include <math.h>
#include <string.h>

#define MAX_PARTIALS 6

typedef struct { float hz; int level; float seconds; } partial_def_t;   /* seconds: time to fall to 1/e */
#define MAX_NOTES 4
typedef struct { uint16_t hz, ms; } note_def_t;                         /* hz 0 is a rest */
typedef struct {
    partial_def_t partial[MAX_PARTIALS];
    int   noise_level;
    float noise_seconds;
    note_def_t note[MAX_NOTES];
    int   note_level;
    float note_seconds;                     /* each note falls away at this rate from its start */
} sfx_def_t;

static const sfx_def_t defs[] = {
    [CHIP_SFX_SHING] = {
        { { 2637, 3000, 0.42f }, { 2671, 3000, 0.42f },       /* a pair 34 Hz apart: the shimmer */
          { 4186, 2200, 0.34f }, { 5274, 1700, 0.27f },
          { 6645, 1300, 0.20f }, { 8372,  900, 0.13f } },
        4200, 0.045f },
    [CHIP_SFX_CLICK] = {
        { { 1850, 5200, 0.006f }, { 3300, 2200, 0.003f } },
        3800, 0.0015f },
    [CHIP_SFX_COIN] = {                     /* two notes a fourth apart, the second left to ring */
        .note = { { 988, 75 }, { 1319, 480 } }, .note_level = 5200, .note_seconds = 0.16f },
    [CHIP_SFX_LOW] = {                      /* and two falling ones: something is running out */
        .note = { { 659, 150 }, { 0, 60 }, { 440, 320 } }, .note_level = 5200, .note_seconds = 0.30f },
};

/* y[n] = k*y[n-1] - y[n-2] rings at a fixed pitch for ever; env is what ends it */
typedef struct { int32_t y1, y2, k; uint32_t env, decay; } ring_t;   /* k Q14, env Q24, decay Q16 */

static ring_t   ring[MAX_PARTIALS];
static int      rings;
static uint32_t noise_env, noise_decay;       /* Q24, Q16 */
static int      noise_level, noise_last;
static uint32_t seed = 0x2545F491u;
static int      wanted = -1;                  /* an effect asked for and not yet started */

static const note_def_t *notes;               /* the notes still to play, or NULL */
static int      note_at, note_left, note_level;
static uint32_t note_phase, note_step, note_env, note_decay;

#define TONE_LEVEL 2600
static int      tone_hz;                      /* what is asked for; 0 is silence */
static int      tone_now;                     /* its level, eased toward that so it never clicks */
static uint32_t tone_phase;

void chip_sfx(chip_sfx_t which)
{
    if ((unsigned)which < sizeof defs / sizeof defs[0]) wanted = (int)which;
}

void chip_tone(int hz) { tone_hz = hz > 0 ? hz : 0; }

static uint32_t decay_for(float seconds, int rate)
{
    return (uint32_t)(65536.0f * expf(-1.0f / (seconds * (float)rate)));
}

static void start(const sfx_def_t *d, int rate)
{
    rings = 0;
    for (int i = 0; i < MAX_PARTIALS; i++) {
        const partial_def_t *p = &d->partial[i];
        if (p->level <= 0 || p->hz * 2.2f > (float)rate) continue;      /* nothing near the top of the band */
        float w = 6.2831853f * p->hz / (float)rate;
        ring_t *r = &ring[rings++];
        r->k  = (int32_t)(2.0f * cosf(w) * 16384.0f);
        r->y1 = 0;
        r->y2 = (int32_t)(-(float)p->level * sinf(w));
        r->env = 1u << 24;
        r->decay = decay_for(p->seconds, rate);
    }
    noise_level = d->noise_level;
    noise_env   = d->noise_level ? 1u << 24 : 0;
    noise_decay = decay_for(d->noise_seconds, rate);

    notes = d->note_level ? d->note : NULL;
    note_at = -1; note_left = 0;
    note_level = d->note_level;
    note_decay = d->note_level ? decay_for(d->note_seconds, rate) : 0;
}

/* on to the next note, if there is one */
static void next_note(int rate)
{
    if (++note_at >= MAX_NOTES || !notes[note_at].ms) { notes = NULL; return; }
    note_left = (int)((long)notes[note_at].ms * rate / 1000);
    note_step = (uint32_t)(((uint64_t)notes[note_at].hz << 32) / (uint32_t)rate);
    note_env  = 1u << 24;
    note_phase = 0;
}

bool sfx_active(void) { return wanted >= 0 || rings > 0 || noise_env > 0 || notes || tone_hz || tone_now; }

void sfx_mix(int16_t *buf, int samples, int rate)
{
    if (wanted >= 0) { start(&defs[wanted], rate); wanted = -1; }
    if (!rings && !noise_env && !notes && !tone_hz && !tone_now) return;

    uint32_t tone_step = (uint32_t)(((uint64_t)tone_hz << 32) / (uint32_t)rate);

    for (int i = 0; i < samples; i++) {
        int32_t out = 0;
        if (notes) {
            if (note_left <= 0) next_note(rate);
            if (notes) {
                note_left--;
                if (note_step) {
                    int32_t level = (int32_t)(((int64_t)note_level * (note_env >> 8)) >> 16);
                    out += (note_phase & 0x80000000u) ? -level : level;
                    note_phase += note_step;
                    note_env = (uint32_t)(((uint64_t)note_env * note_decay) >> 16);
                }
            }
        }
        if (tone_hz || tone_now) {
            if (tone_hz && tone_now < TONE_LEVEL) tone_now += 8;
            if (!tone_hz) tone_now = tone_now > 8 ? tone_now - 8 : 0;
            /* a triangle: softer than the square, so it sits under the music */
            uint32_t ph = tone_phase >> 16;                         /* 0..65535 */
            int32_t tri = (int32_t)(ph < 32768 ? ph : 65535 - ph) - 16384;   /* -16384..16383 */
            out += tri * tone_now / 16384;
            tone_phase += tone_step;
        }
        for (int r = 0; r < rings; r++) {
            ring_t *g = &ring[r];
            int32_t y = (int32_t)(((int64_t)g->k * g->y1) >> 14) - g->y2;
            g->y2 = g->y1; g->y1 = y;
            out += (int32_t)(((int64_t)y * (g->env >> 8)) >> 16);
            g->env = (uint32_t)(((uint64_t)g->env * g->decay) >> 16);
        }
        if (noise_env) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            int n = (int)(seed & 0xFFFF) - 32768;
            int hiss = (n - noise_last) / 2;                /* first difference: keeps the top, drops the rumble */
            noise_last = n;
            out += (int32_t)(((int64_t)hiss * noise_level >> 15) * (int32_t)(noise_env >> 8) >> 16);
            noise_env = (uint32_t)(((uint64_t)noise_env * noise_decay) >> 16);
            if (noise_env < 1u << 10) noise_env = 0;
        }
        int32_t v = buf[i] + out;
        buf[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }

    /* drop what has died away, so a finished effect costs nothing */
    int kept = 0;
    for (int r = 0; r < rings; r++)
        if (ring[r].env >= 1u << 12) ring[kept++] = ring[r];
    rings = kept;
}
