/*
 * midi_ay.c - plays a Standard MIDI File on an AY-3-8910.
 *
 * File parsing and timing are TinyMidiLoader's (tml.h, zlib, Bernhard Schelling):
 * it hands back a flat list of events stamped in absolute milliseconds, so there
 * is no tick arithmetic, tempo tracking or running status here. What is left is
 * mapping a general MIDI stream onto three square-wave channels.
 */
#include "player.h"
#include "ay8910.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>

#define TML_NO_STDIO
#define TML_IMPLEMENTATION
#include "tml.h"

static const char *TAG = "midi";

#define AY_CLOCK  1789773u
#define VOICES    3
#define DRUM_CH   9                    /* MIDI channel 10, zero-based */

static ay8910_t     ay;
static tml_message *song, *cur;
static uint64_t     samples_played;
static uint8_t      v_note[VOICES], v_vol[VOICES];

static void wr(uint8_t reg, uint8_t val) { ay_address_w(&ay, reg); ay_data_w(&ay, val); }

/* period = clock / (16 * freq); table is C7..B7 (MIDI 96..107), shifted per octave */
static const uint16_t oct[12] = { 53, 50, 48, 45, 42, 40, 38, 36, 34, 32, 30, 28 };
static uint16_t period_for(uint8_t n)
{
    if (n < 24 || n > 107) return 0;
    return (uint16_t)(oct[n % 12] << (8 - n / 12));
}

static void voice_set(int c, uint8_t note, uint8_t vol)
{
    uint16_t p = period_for(note);
    wr((uint8_t)(c * 2), (uint8_t)(p & 0xff));
    wr((uint8_t)(c * 2 + 1), (uint8_t)(p >> 8));
    wr((uint8_t)(8 + c), (uint8_t)(vol & 0x0f));    /* bit 4 would hand it to the envelope */
    v_note[c] = vol ? note : 0;
    v_vol[c]  = vol;
}

static void all_off(void) { for (int i = 0; i < VOICES; i++) voice_set(i, 0, 0); }

static void note_on(uint8_t note, uint8_t vel)
{
    if (!period_for(note)) return;
    /* retrigger in place: two voices on one pitch is how notes get stranded,
     * because a note-off only ever frees one of them */
    for (int i = 0; i < VOICES; i++)
        if (v_note[i] == note) { voice_set(i, note, (uint8_t)(4 + vel * 11 / 127)); return; }

    int c = -1;
    for (int i = 0; i < VOICES; i++) if (!v_note[i]) { c = i; break; }
    if (c < 0) {
        /* All busy. Keep the outer voices - melody on top, bass at the bottom -
         * and only displace the middle one, and only for something above it. */
        int low = 0, high = 0;
        for (int i = 1; i < VOICES; i++) {
            if (v_note[i] < v_note[low])  low  = i;
            if (v_note[i] > v_note[high]) high = i;
        }
        int mid = (low == high) ? 0 : 3 - low - high;
        if (note <= v_note[mid]) return;
        c = mid;
    }
    voice_set(c, note, (uint8_t)(4 + vel * 11 / 127));
}

static void note_off(uint8_t note)
{
    for (int i = 0; i < VOICES; i++) if (v_note[i] == note) voice_set(i, 0, 0);
}

static void midi_rewind(void)
{
    cur = song;
    samples_played = 0;
    all_off();
}

static bool midi_load(const uint8_t *d, size_t len, int track)
{
    (void)track;
    /* tml_load_memory dereferences its own failed allocations, so check for room
     * first. The event list runs to roughly eight times the file on a dense score. */
    size_t need = len * 10, have = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    if (have < need) {
        ESP_LOGW(TAG, "only %u bytes free, need about %u for this file", (unsigned)have, (unsigned)need);
        return false;
    }
    song = tml_load_memory(d, (int)len);
    if (!song) return false;

    int chans = 0, progs = 0, notes = 0;
    unsigned int first = 0, dur = 0;
    tml_get_info(song, &chans, &progs, &notes, &first, &dur);
    ESP_LOGI(TAG, "%d notes, %u ms", notes, dur);

    ay_init(&ay, AY_CLOCK, NULL, NULL);
    ay_reset(&ay);
    wr(7, 0x38);                       /* tones A/B/C on, noise off */
    midi_rewind();
    return true;
}

static void midi_render(int16_t *buf, int samples, int rate)
{
    while (samples > 0) {
        unsigned int now_ms = (unsigned int)(samples_played * 1000ull / (unsigned)rate);
        while (cur && cur->time <= now_ms) {
            if (cur->channel != DRUM_CH) {
                if (cur->type == TML_NOTE_ON && cur->velocity > 0)
                    note_on((uint8_t)cur->key, (uint8_t)cur->velocity);
                else if (cur->type == TML_NOTE_OFF || (cur->type == TML_NOTE_ON && cur->velocity == 0))
                    note_off((uint8_t)cur->key);
            }
            cur = cur->next;
        }
        if (!cur) { midi_rewind(); continue; }          /* loop back to the top */

        uint64_t due = (uint64_t)cur->time * (unsigned)rate / 1000ull;
        int n = (due > samples_played) ? (int)(due - samples_played) : 1;
        if (n > samples) n = samples;

        /* ay_render ACCUMULATES into the buffer so several chips can be summed,
         * and it returns without touching it at all when every channel is
         * silent. Both mean the caller owns clearing it - miss that and a silent
         * passage replays whatever was in the DMA buffer last, forever. */
        memset(buf, 0, (size_t)n * sizeof(int16_t));
        ay_render(&ay, buf, n, rate);
        buf += n; samples -= n; samples_played += (unsigned)n;
    }
}

static void midi_unload(void)
{
    if (song) { tml_free(song); song = NULL; cur = NULL; }
}

const player_t midi_player = { "MIDI", midi_load, midi_rewind, midi_render, all_off, midi_unload };
