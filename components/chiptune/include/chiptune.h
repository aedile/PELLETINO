/*
 * chiptune.h - the launcher's music.
 *
 * The repository ships no music. Drop a file in music/ and it is embedded at
 * build time; with none, the screen that wanted it runs silent and the build
 * still works. Same arrangement as the ROMs and the artwork.
 *
 *   music/splash.nsf   or  music/splash.mid     the opening
 *   music/credits.nsf  or  music/credits.mid    the credits roll
 *
 * An NSF plays on an emulated NES sound chip and is the better-sounding of the
 * two; a MIDI file plays on three square-wave channels of an AY-3-8910. An NSF
 * usually holds many tunes - put the number you want in music/splash.track.
 *
 * chiptune provides audio_render(), the hook audio_hal calls to fill its DMA
 * queue; call audio_update() regularly to keep it fed.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { CHIP_TUNE_SPLASH, CHIP_TUNE_CREDITS } chip_tune_t;

bool chip_available(chip_tune_t which);  /* was a tune supplied for this slot? */
bool chip_load(chip_tune_t which);      /* false when none was supplied, or it would not load */
bool chip_load_memory(const uint8_t *data, size_t len, int track);   /* track 0 = file's default */
bool chip_has_music(void);
void chip_play(void);                   /* start, or restart, from the top */
void chip_stop(void);                   /* silence */
void chip_free(void);                   /* give the memory back */
bool chip_playing(void);

/* Sound effects. They are made in code, so there is nothing to supply, and
 * they are mixed over the music - or over silence, if there is none. */
typedef enum {
    CHIP_SFX_SHING,     /* a blade drawn: the flash in the opening */
    CHIP_SFX_CLICK,     /* one step of the wheel */
    CHIP_SFX_COIN,      /* a game chosen */
    CHIP_SFX_LOW,       /* the battery is running out */
} chip_sfx_t;
void chip_sfx(chip_sfx_t which);
void chip_tone(int hz);                 /* a held tone at this pitch until told otherwise; 0 stops it */

#ifdef __cplusplus
}
#endif
