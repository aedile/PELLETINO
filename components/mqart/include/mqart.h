/*
 * mqart.h - the wheel's artwork, stored in the "mqart" flash partition.
 *
 * The blob is built by tools/pack_art.py, which documents the layout. Every
 * picture is one byte per pixel - an index into the launcher's palette (see
 * fest.h) - and run-length coded a row at a time, so any row can be had without
 * decoding the ones above it. That is what lets a logo be drawn at any size.
 *
 * The partition is memory-mapped: rows are decoded straight out of flash into
 * the frame buffer, and nothing is copied on the way.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MQART_MAX          24   /* entries we are willing to hold in RAM */
#define MQART_MAX_W       240   /* no picture is wider or taller than the panel */
#define MQART_MAX_H       280
#define MQART_LOGOS         3   /* selected, its neighbours, the far pair */
#define MQART_CLEAR       255   /* the index that means "nothing here" */
#define MQART_SNAP_COLOURS 56   /* a snap brings its own colours... */
#define MQART_SNAP_BASE   199   /* ...for these palette entries */

typedef struct {
    uint16_t w, h;              /* w == 0: there is no such picture */
    uint32_t off, len;          /* byte range within the partition */
} mqart_img_t;

typedef struct {
    char        rom[13];        /* MAME ROM name; what the launcher records as the selection */
    char        boot[13];       /* partition label to chain-boot; '@...' is built into the launcher */
    char        title[25];      /* display name, e.g. "Ms. Pac-Man" */
    char        by[25];         /* who made it and when, e.g. "Namco 1981" */
    mqart_img_t logo[MQART_LOGOS];
    mqart_img_t snap;
} mqart_entry_t;

esp_err_t             mqart_init(void);
int                   mqart_count(void);
const mqart_entry_t  *mqart_get(int i);
int                   mqart_find(const char *rom);   /* index, or -1 */

/* The partition label to chain-boot for a given selection ROM. Normally the ROM
 * itself; for a shared slot (Pac-Man riding Ms. Pac-Man's image) it is the slot
 * owner's label. Returns rom unchanged if it is not in the blob. */
const char           *mqart_boot_label(const char *rom);

/* Decode row y of a picture into dst, im->w bytes. False, and dst untouched
 * beyond what was decoded, if the row is not there or does not decode cleanly. */
bool                  mqart_row(const mqart_img_t *im, int y, uint8_t *dst);

/* A snap's colours: MQART_SNAP_COLOURS x (r, g, b), or NULL if it has none. */
const uint8_t        *mqart_snap_colours(const mqart_entry_t *e);

#ifdef __cplusplus
}
#endif
