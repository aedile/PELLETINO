/*
 * chiptune.c - the launcher's music: picks a player by the file's first bytes
 * and hands audio_hal its samples.
 *
 * There are two tunes, the splash's and the credits', and each may be an NSF
 * (played on an emulated NES sound chip) or a Standard MIDI File (played on an
 * AY-3-8910). Neither ships with the project; see music/README.md.
 */
#include "chiptune.h"
#include "player.h"
#include "sfx.h"
#include "audio_hal.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "chiptune";

#ifdef HAVE_TUNE_SPLASH
extern const uint8_t splash_start[] asm("_binary_tune_splash_bin_start");
extern const uint8_t splash_end[]   asm("_binary_tune_splash_bin_end");
#endif
#ifdef HAVE_TUNE_CREDITS
extern const uint8_t credits_start[] asm("_binary_tune_credits_bin_start");
extern const uint8_t credits_end[]   asm("_binary_tune_credits_bin_end");
#endif
#ifndef TUNE_SPLASH_TRACK
#define TUNE_SPLASH_TRACK 0
#endif
#ifndef TUNE_CREDITS_TRACK
#define TUNE_CREDITS_TRACK 0
#endif

static const player_t *cur;
static bool playing;

bool chip_load_memory(const uint8_t *d, size_t len, int track)
{
    chip_free();
    const player_t *p = NULL;
    if (len > 5 && !memcmp(d, "NESM\x1a", 5)) p = &nsf_player;
    else if (len > 4 && !memcmp(d, "MThd", 4)) p = &midi_player;
    if (!p) { ESP_LOGW(TAG, "not an NSF or a MIDI file"); return false; }
    if (!p->load(d, len, track)) { ESP_LOGW(TAG, "%s file would not load", p->name); return false; }
    cur = p;
    return true;
}

bool chip_available(chip_tune_t which)
{
#ifdef HAVE_TUNE_SPLASH
    if (which == CHIP_TUNE_SPLASH) return true;
#endif
#ifdef HAVE_TUNE_CREDITS
    if (which == CHIP_TUNE_CREDITS) return true;
#endif
    (void)which;
    return false;
}

bool chip_load(chip_tune_t which)
{
    switch (which) {
#ifdef HAVE_TUNE_SPLASH
    case CHIP_TUNE_SPLASH:
        return chip_load_memory(splash_start, (size_t)(splash_end - splash_start), TUNE_SPLASH_TRACK);
#endif
#ifdef HAVE_TUNE_CREDITS
    case CHIP_TUNE_CREDITS:
        return chip_load_memory(credits_start, (size_t)(credits_end - credits_start), TUNE_CREDITS_TRACK);
#endif
    default:
        break;
    }
    chip_free();
    ESP_LOGI(TAG, "no music supplied for this screen; it will run silent");
    return false;
}

bool chip_has_music(void) { return cur != NULL; }
bool chip_playing(void)   { return playing; }

void chip_play(void)
{
    if (!cur) return;
    cur->rewind();
    playing = true;
}

void chip_stop(void)
{
    playing = false;
    if (cur) cur->silence();
}

void chip_free(void)
{
    chip_stop();
    if (cur) { cur->unload(); cur = NULL; }
}

/* audio_hal calls this to top up its DMA queue */
void audio_render(int16_t *buf, int samples, int rate)
{
    if (!playing || !cur) memset(buf, 0, (size_t)samples * sizeof(int16_t));
    else cur->render(buf, samples, rate);
    sfx_mix(buf, samples, rate);
}
