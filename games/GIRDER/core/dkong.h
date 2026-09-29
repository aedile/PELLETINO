/*
 * dkong.h - Nintendo Donkey Kong (1981) board emulation
 *
 * A Z80 at 3.072 MHz for the game and an 8035 microcontroller for the sound. The 8035 is not
 * a coprocessor in the usual sense: it plays the music and most of the effects by writing
 * samples to an eight-bit DAC on its port 1, fetching the sample data a page at a time through
 * its BUS port. Emulating it is what gets the real tunes rather than an imitation of them.
 *
 * An 8257 DMA controller copies the sprite list into the sprite RAM once a frame, triggered by
 * a write to 0x7D85.
 *
 * Timing, memory map and video follow MAME's nintendo/dkong.cpp and dkong_v.cpp.
 */
#ifndef DKONG_H
#define DKONG_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define DK_MASTER_CLOCK  61440000
#define DK_CPU_CLOCK     (DK_MASTER_CLOCK / 5 / 4)      /* 3.072 MHz */
#define DK_SND_CLOCK     6000000                        /* the 8035 */
#define DK_CYCLES_PER_FRAME 50688                       /* 384 x 264 at 6.144 MHz = 60.606 Hz */

/* the native frame, before the cabinet turns it upright - the renderer does the rotation */
#define DK_FB_W 256
#define DK_FB_H 224
#define DK_PALETTE_SIZE 256

typedef struct {
    const uint8_t *rom;        /* 16 KB Z80 program */
    const uint8_t *snd;        /* 4 KB 8035 program */
    const uint8_t *samp;       /* 2 KB of sample pages */
    const uint8_t *chr;        /* 4 KB characters, two bit planes */
    const uint8_t *spr;        /* 8 KB sprites */
    const uint8_t *pal_lo;     /* palette PROM, low nibble, inverted */
    const uint8_t *pal_hi;     /* palette PROM, high nibble, inverted */
    const uint8_t *colcode;    /* character colour codes, per column */
} dk_roms_t;

typedef struct {
    uint8_t up, down, left, right, jump;      /* player 1 */
    uint8_t start1, start2, coin1;
} dk_input_t;

void dk_init(const dk_roms_t *roms);
void dk_reset(void);
void dk_set_dips(uint8_t dsw);
dk_input_t *dk_input(void);

void dk_run_frame(void);
void dk_video_init(void);
void dk_render(uint8_t *fb);                     /* DK_FB_W * DK_FB_H palette indices */
void dk_palette(uint16_t out[DK_PALETTE_SIZE]);  /* RGB565 */
void dk_render_audio(int16_t *buf, int samples, int rate);

uint16_t dk_pc(void);
uint16_t dk_snd_pc(void);
uint32_t dk_frame_count(void);
uint8_t *dk_mem(uint16_t addr);      /* a byte of RAM by its CPU address, or NULL */

#ifdef __cplusplus
}
#endif
#endif
