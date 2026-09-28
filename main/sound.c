/*
 * sound.c - muting, in the launcher.
 *
 * Both buttons together, the same in attract mode, in the menu and in every
 * game. It is one setting - saved, and shared with every game - so a medal muted
 * anywhere stays muted everywhere until someone turns it back on.
 *
 * Muting powers the codec down, so a muted medal is not spending battery on a
 * speaker nobody can hear.
 */
#include "sound.h"
#include "input.h"
#include "medalboot.h"
#include "audio_hal.h"
#include "display.h"
#include "esp_log.h"

static const char *TAG = "sound";

void sound_init(void)
{
    if (medalboot_muted()) audio_set_mute(true);
    ESP_LOGI(TAG, "sound %s", audio_get_mute() ? "off (saved)" : "on");
}

bool sound_muted(void) { return audio_get_mute(); }

bool sound_poll(void)
{
    if (!input_take_mute()) return false;
    bool muted = !audio_get_mute();
    audio_set_mute(muted);
    medalboot_set_muted(muted);
    display_toast(muted ? "SOUND OFF" : "SOUND ON", 1500);
    ESP_LOGI(TAG, "sound %s", muted ? "off" : "on");
    return true;
}
