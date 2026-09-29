/*
 * sound.c - the sound setting, in the launcher.
 *
 * Both buttons together steps it: loud, quiet, off, and round again. The same
 * in attract mode, in the menu and in every game, and it is one setting - saved,
 * and shared with every game - so whatever it is left at anywhere is what it is
 * everywhere until someone changes it.
 *
 * Off powers the codec down, so it is not spending battery on a speaker nobody
 * can hear.
 */
#include "sound.h"
#include "input.h"
#include "medalboot.h"
#include "audio_hal.h"
#include "display.h"
#include "esp_log.h"

static const char *TAG = "sound";
static medalboot_sound_t level;

static void apply(medalboot_sound_t s)
{
    level = s;
    audio_set_volume(medalboot_sound_volume(s));        /* before waking the codec, which sets it */
    bool off = s == MEDALBOOT_SOUND_OFF;
    if (off != audio_get_mute()) audio_set_mute(off);
}

void sound_init(void)
{
    apply(medalboot_sound());
    ESP_LOGI(TAG, "%s (saved)", medalboot_sound_name(level));
}

bool sound_muted(void) { return level == MEDALBOOT_SOUND_OFF; }
bool sound_quiet(void) { return level == MEDALBOOT_SOUND_QUIET; }

bool sound_poll(void)
{
    if (!input_take_mute()) return false;
    apply(medalboot_sound_next());
    display_toast(medalboot_sound_name(level), 1500);
    ESP_LOGI(TAG, "%s", medalboot_sound_name(level));
    return true;
}
