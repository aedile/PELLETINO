/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro */
/*
 * cps1_view.h - the two ways 384x224 is put on a 240-wide panel, as the lookup tables
 * cps1_set_view() takes. Shared by the device and the host harness so that both draw the
 * same picture.
 */
#ifndef CPS1_VIEW_H
#define CPS1_VIEW_H

#include "cps1.h"

#define VIEW_W 240
#define VIEW_SCALE_H 140             /* 384x224 at 5/8 */
#define VIEW_CROP_H 224
#define VIEW_CROP_X0 ((CPS1_SCREEN_W - VIEW_W) / 2)

/* nearest neighbour: view column x shows screen column x * 8 / 5, and rows likewise */
static inline void view_scale(uint16_t *cols, uint16_t *rows)
{
    for (int x = 0; x < VIEW_W; x++) cols[x] = (uint16_t)(x * CPS1_SCREEN_W / VIEW_W);
    for (int y = 0; y < VIEW_SCALE_H; y++) rows[y] = (uint16_t)(y * CPS1_SCREEN_H / VIEW_SCALE_H);
}

/* the middle 240 columns, one to one */
static inline void view_crop(uint16_t *cols, uint16_t *rows)
{
    for (int x = 0; x < VIEW_W; x++) cols[x] = (uint16_t)(VIEW_CROP_X0 + x);
    for (int y = 0; y < VIEW_CROP_H; y++) rows[y] = (uint16_t)y;
}

#endif
