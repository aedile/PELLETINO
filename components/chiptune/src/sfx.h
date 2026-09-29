/* sfx.h - sound effects, mixed over the music. See sfx.c. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

void sfx_mix(int16_t *buf, int samples, int rate);   /* add whatever is sounding to buf */
bool sfx_active(void);
