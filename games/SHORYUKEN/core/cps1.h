/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * cps1.h - Capcom CPS-1 running Street Fighter II: The World Warrior (MAME set "sf2").
 *
 * A 68000 at 10 MHz, a Z80 at 3.58 MHz with a YM2151 and an OKI MSM6295, three scrolling
 * tile layers and a sprite layer, 384x224 at 59.64 Hz. The picture is never assembled at
 * that size: cps1_render() draws any band of rows of a smaller view of it, straight into
 * the caller's strip.
 */
#ifndef CPS1_H
#define CPS1_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CPS1_SCREEN_W 384
#define CPS1_SCREEN_H 224
#define CPS1_M68K_CLOCK 10000000
#define CPS1_Z80_CLOCK 3579545
#define CPS1_FRAME_US 16768          /* 8 MHz / 512 / 262 = 59.637 Hz */

/* ---- the ROM blob, as tools/convert_roms.py writes it ---- */
#define CPS1_BLOB_MAGIC 0x42324653   /* "SF2B" */
#define CPS1_BLOB_VERSION 2
#define CPS1_BLOB_HEADER_OFF 0x750000 /* the header follows the ROMs */
#define CPS1_BLOB_HEADER_SIZE 0x1000
#define CPS1_BLOB_MAP_BYTES 0x710000  /* everything but the samples and the header */
typedef struct {
    uint32_t magic, version;
    uint32_t prog_off, prog_size;    /* 68000 program, 16-bit words, little-endian */
    uint32_t gfx_off, gfx_size;      /* graphics, 4 bits a pixel, eight pixels to a little-endian word */
    uint32_t z80_off, z80_size;      /* sound program: 32K fixed, then the banks */
    uint32_t oki_off, oki_size;      /* ADPCM samples */
    /* the CPS-B chip on this set's C-board: where its registers are and what it answers */
    uint8_t cpsb_id_reg, cpsb_layer_ctrl, cpsb_pal_ctrl, cpsb_in2_reg;
    uint16_t cpsb_id_value;
    uint8_t cpsb_prio[4];
    uint8_t cpsb_layer_mask[3];      /* scroll1, scroll2, scroll3 enable bits in the layer control */
    uint8_t pad[3];
    char set_name[16];
} cps1_blob_t;

typedef struct {
    const uint16_t *prog;
    const uint8_t *gfx;
    const uint8_t *z80;
    const uint8_t *oki;              /* NULL if the samples are not in memory, and then: */
    void (*oki_read)(uint32_t offset, uint8_t *dst, uint32_t len);
    const cps1_blob_t *cfg;          /* must outlive the machine */
} cps1_roms_t;

/* NULL if the header is one of ours, or what is wrong with it */
const char *cps1_blob_check(const cps1_blob_t *header);
/* base is where offset 0 of the image is in memory. Sets everything but oki and oki_read. */
void cps1_blob_roms(const cps1_blob_t *header, const void *base, cps1_roms_t *roms);

typedef struct {
    uint8_t coin1, coin2, start1, start2, service;
    uint8_t p1_right, p1_left, p1_down, p1_up, p1_b1, p1_b2, p1_b3, p1_b4, p1_b5, p1_b6;
} cps1_input_t;

/* ram is 64 KB and gfxram 192 KB, both the caller's, both real RAM */
void cps1_init(const cps1_roms_t *roms, uint16_t *ram, uint16_t *gfxram);
void cps1_reset(void);
cps1_input_t *cps1_input(void);

/* one video frame of the machine: the 68000 and, if there is sound, the Z80 alongside it */
void cps1_run_frame(void);
uint32_t cps1_frame_count(void);
uint32_t cps1_pc(void);
uint32_t cps1_idle_loop(void);        /* where the idle loop was found, or 0xffffffff */
/* time spent in the sound CPU and its timers during cps1_run_frame(), if a clock was given */
void cps1_set_clock(int64_t (*now_us)(void));
uint64_t cps1_z80_us(void);          /* reads and clears */

/* ---- video ---- */
/*
 * A view is a picture out_w by out_h whose column x shows screen column col_src[x] and
 * whose row y shows screen row row_src[y]. Both tables must not decrease. The tables are
 * kept by pointer.
 */
void cps1_set_view(int out_w, int out_h, const uint16_t *col_src, const uint16_t *row_src);
/* latch the frame: registers, the sprite list, the palette. Once per drawn frame. */
void cps1_frame_begin(void);
/* rows y0 .. y0+rows-1 of the view, as RGB565 with the bytes already swapped for the panel */
void cps1_render(uint16_t *dst, int y0, int rows);
void cps1_set_layers(unsigned mask);  /* LAYER_* bits; the host harness uses it, the device uses the knob */

/* ---- sound ---- */
void cps1_sound_init(const cps1_roms_t *roms, int rate);
void cps1_render_audio(int16_t *out, int samples);

#ifdef __cplusplus
}
#endif
#endif
