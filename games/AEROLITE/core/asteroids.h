/*
 * asteroids.h - Atari Asteroids (1979) board emulation
 *
 * One 6502 at 1.512 MHz and a Digital Vector Generator. There is no frame buffer and no
 * graphics ROM at all: the CPU builds a display list in vector RAM and the DVG walks it,
 * steering the beam. What comes out of this core is therefore a list of line segments
 * rather than a raster.
 *
 * Sound is entirely discrete analogue on the real board - a 555 thump VCO, a noise source,
 * and a handful of one-shots. Rather than run a netlist on a microcontroller, the same
 * latches drive a small synthesiser that aims to sound like the cabinet.
 *
 * Timing and memory map follow MAME's asteroid.cpp.
 */
#ifndef ASTEROIDS_H
#define ASTEROIDS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define AST_MASTER_CLOCK  12096000
#define AST_CPU_CLOCK     (AST_MASTER_CLOCK / 8)          /* 1.512 MHz */
/* The periodic NMI runs at 12.096 MHz / 4096 / 12 = 246.09 Hz, and the display is refreshed
 * on every fourth one, so a frame is 61.523 Hz and exactly 24576 CPU cycles. */
#define AST_NMI_CYCLES    6144
#define AST_NMIS_PER_FRAME 4
#define AST_CYCLES_PER_FRAME (AST_NMI_CYCLES * AST_NMIS_PER_FRAME)

/* The DVG's position counters are 12 bits. Segments come out in those raw units, y increasing
 * upwards. The game draws inside a window centred on 512: the full 1024 across, but only
 * 96..927 vertically, which is the 832 rows the cabinet's 4:3 monitor showed. Mapping that
 * window (rather than a full square) onto a 4:3 picture is what gets the framing right. */
#define AST_X_MIN 0
#define AST_X_MAX 1024
#define AST_Y_MIN 96
#define AST_Y_MAX 928
#define AST_MAX_LINES 640

typedef struct {
    int16_t x0, y0, x1, y1;
    uint8_t bright;              /* 1..15; segments with intensity 0 are not emitted */
} ast_line_t;

typedef struct {
    const uint8_t *rom;          /* 6 KB program, CPU 0x6800-0x7FFF */
    const uint8_t *vecrom;       /* 2 KB vector ROM, CPU 0x5000-0x57FF */
} ast_roms_t;

typedef struct {
    uint8_t left, right, thrust, fire, hyperspace;
    uint8_t start1, start2, coin1;
} ast_input_t;

void ast_init(const ast_roms_t *roms);
void ast_reset(void);
void ast_set_dips(uint8_t dsw1);           /* see the DIP table in asteroids.c */
ast_input_t *ast_input(void);

void ast_run_frame(void);
/* the most recently completed display list */
const ast_line_t *ast_lines(int *count);
void ast_render_audio(int16_t *buf, int samples, int rate);

/* diagnostics */
uint16_t ast_pc(void);
uint32_t ast_frame_count(void);
uint8_t *ast_mem(uint16_t addr);      /* a byte of RAM by its CPU address, or NULL */
uint32_t ast_idle_cycles(void);
const uint8_t *ast_ram(void);

#ifdef __cplusplus
}
#endif
#endif
