/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro */
#ifndef RENDER_H
#define RENDER_H
#include <stdint.h>

void render_init(void);
/* Draw the frame the machine is showing now. between() is called after every strip, which
 * is where the audio is kept topped up. Adds what it spent to the two counters. */
void render_frame(void (*between)(void), uint64_t *video_us, uint64_t *strips_us);
extern uint32_t render_strips_sent, render_strips_kept;   /* running counts */

#endif
