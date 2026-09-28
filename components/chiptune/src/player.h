/* player.h - what chiptune.c needs from a format. Two implementations: nsf.c and midi_ay.c. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    const char *name;
    bool (*load)(const uint8_t *data, size_t len, int track);   /* track 0 = the file's default */
    void (*rewind)(void);
    void (*render)(int16_t *buf, int samples, int rate);        /* must fill the whole buffer */
    void (*silence)(void);
    void (*unload)(void);
} player_t;

extern const player_t nsf_player, midi_player;
