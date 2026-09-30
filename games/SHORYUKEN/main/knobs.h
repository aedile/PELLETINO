/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro */
/*
 * knobs.h - every trade-off in SHORYUKEN, in one place.
 *
 * Each knob is a compile-time switch. Override one without editing this file:
 *
 *     idf.py -B build_docker -DVIDEO_MODE=CROP -DFRAME_SKIP=1 -DSOUND=FM build
 *     make -C host KNOBS="-DKNOB_VIDEO_MODE=CROP -DFRAME_SKIP=1"
 *
 * knobs.cmake turns each -D<KNOB>=<value> into a #define in a header in the build directory;
 * the knobs that take a word rather than a number arrive as KNOB_<name> and are pasted onto
 * a prefix here. A value given to idf.py is kept in the build directory's CMake cache, so a
 * sweep passes every knob on every build (tools/sweep.py does).
 *
 * "Costs" and "buys" are what was measured on the device; the numbers are in the README.
 */
#ifndef KNOBS_H
#define KNOBS_H

/* what was given to idf.py, if anything was (knobs.cmake writes it) */
#if defined(__has_include)
#if __has_include("knobs_build.h")
#include "knobs_build.h"
#endif
#endif

#define KNOB_CAT_(a, b) a##b
#define KNOB_CAT(a, b)  KNOB_CAT_(a, b)
#define KNOB_STR_(a)    #a
#define KNOB_STR(a)     KNOB_STR_(a)

/*
 * VIDEO_MODE: how 384x224 meets a 240x280 portrait panel.
 *   SCALE  the whole playfield at 240x140, nearest neighbour. Draws 5 of every 8 pixels
 *          across and down, so it is the cheap one, and the picture is complete.
 *   CROP   the middle 240 columns at 1:1, 240x224. Every row is drawn, so it costs about
 *          1.6 times SCALE in video and strips, and the fighters' far edges are cut off.
 */
#define VIDEO_MODE_SCALE 0
#define VIDEO_MODE_CROP  1
#ifndef KNOB_VIDEO_MODE
#define KNOB_VIDEO_MODE SCALE
#endif
#define VIDEO_MODE      KNOB_CAT(VIDEO_MODE_, KNOB_VIDEO_MODE)
#define VIDEO_MODE_NAME KNOB_STR(KNOB_VIDEO_MODE)

/*
 * FRAME_SKIP: draw one frame in N+1. The 68000 runs every frame regardless. Costs
 * smoothness; buys back N/(N+1) of the video and strips time.
 */
#ifndef FRAME_SKIP
#define FRAME_SKIP 3
#endif

/*
 * FRAME_SKIP_AUTO: on top of FRAME_SKIP, drop the next drawn frame whenever the emulation
 * is more than a frame behind the clock. Costs nothing when it is keeping up.
 */
#ifndef FRAME_SKIP_AUTO
#define FRAME_SKIP_AUTO 1
#endif

/*
 * LAYERS: which layers are drawn, for profiling. Bit 0 scroll1 (8x8 text), bit 1 scroll2
 * (16x16), bit 2 scroll3 (32x32), bit 3 sprites. Turning one off buys its share of the
 * video time and costs that layer.
 */
#define LAYER_SCROLL1 1
#define LAYER_SCROLL2 2
#define LAYER_SCROLL3 4
#define LAYER_SPRITES 8
#ifndef LAYERS
#define LAYERS 15
#endif

/*
 * ROWSCROLL: honour scroll2's per-line scroll (the floor in a fight). Off, every line of
 * scroll2 shares one scroll value: the floor goes flat and the layer draws in tile-row
 * batches rather than line by line.
 */
#ifndef ROWSCROLL
#define ROWSCROLL 1
#endif

/*
 * SOUND:
 *   OFF       the Z80 is never run. Nothing is heard and nothing is spent.
 *   FM        Z80 and YM2151: the music.
 *   FM_ADPCM  adds the OKI MSM6295: voices and hits.
 */
#define SOUND_OFF      0
#define SOUND_FM       1
#define SOUND_FM_ADPCM 2
#ifndef KNOB_SOUND
#define KNOB_SOUND OFF
#endif
#define SOUND      KNOB_CAT(SOUND_, KNOB_SOUND)
#define SOUND_NAME KNOB_STR(KNOB_SOUND)

/*
 * SOUND_RATE: 11025, 22050 or 44100. The rate the YM2151 is synthesised at and the rate
 * I2S runs at. The FM cost is proportional to it.
 */
#ifndef SOUND_RATE
#define SOUND_RATE 22050
#endif

/*
 * YM_QUALITY: 1 is the whole chip, with the operators computed at the output rate and the
 * envelopes and timers at the chip's own. 0 is cheaper and sounds it: no LFO (vibrato and
 * tremolo are lost), no noise, and the operators are computed at half the output rate with
 * a straight line drawn between. Both skip channels that cannot be heard, exactly.
 */
#ifndef YM_QUALITY
#define YM_QUALITY 1
#endif

/*
 * CPU_CORE:
 *   MUSASHI  Karl Stenerud's 68000, with its tables moved out of RAM (core/m68k/CHANGES.md).
 *   OWN      a compact 68000 of this project's own, to be written if Musashi could not hold
 *            10 MHz. It can: in the first attract fight it uses about 290 ms of every
 *            second. So OWN has not been written, and asking for it stops the build.
 */
#define CPU_CORE_MUSASHI 0
#define CPU_CORE_OWN     1
#ifndef KNOB_CPU_CORE
#define KNOB_CPU_CORE MUSASHI
#endif
#define CPU_CORE      KNOB_CAT(CPU_CORE_, KNOB_CPU_CORE)
#define CPU_CORE_NAME KNOB_STR(KNOB_CPU_CORE)
#if CPU_CORE == CPU_CORE_OWN
#error "CPU_CORE=OWN: not written, because Musashi holds real time (see the README)"
#endif

/*
 * IDLE_SKIP: Street Fighter II's task scheduler goes round sixteen task slots until the
 * vertical blank makes something ready, and one of its tasks does nothing but look at an
 * empty queue each time round. Three quarters of the instructions the 68000 executes in
 * attract mode are that. With this on, when a whole pass of the scheduler has changed
 * nothing in memory and has ended with every register as the pass before left it, the
 * machine is in a cycle that only an interrupt can break, and the 68000 is moved straight
 * to the end of its time slice. The game cannot tell: the host harness gets the same RAM
 * after every frame and the same pictures, with this on or off. Off, every instruction
 * is executed. The loop is found by its shape in the ROM; a set without it is not skipped.
 */
#ifndef IDLE_SKIP
#define IDLE_SKIP 1
#endif

/*
 * PROG_CACHE_KB: 0, or a multiple of 4 up to 64. RAM given to copies of the 4 KB pages of
 * the 68000's program that it spends its time in. The program is 1 MB in flash, behind the
 * same 32 KB cache as the graphics, and every frame that is drawn pushes it out of that
 * cache. The pages are chosen as it runs, by counting where jumps land, and chosen again
 * every couple of seconds; in attract mode 8 pages hold 87% of what is executed and 16
 * hold 96%. Costs the RAM and one more load for each read of the program.
 */
#include "knobs_m68k.h"      /* PROG_CACHE_KB and OPCODE_TABLE_RAM are defined there */

/*
 * HOT_HANDLERS: 1 puts the 68000 instruction handlers that attract mode actually uses
 * (150 of Musashi's 1700, which are 99% of what is executed) in RAM, where running them
 * does not go through the flash cache. 0 leaves all of them in flash. The list is
 * components/emu/linker_hot.lf, made by tools/hot.py from a profile taken on the host.
 */
#ifndef HOT_HANDLERS
#define HOT_HANDLERS 1
#endif

/*
 * OPCODE_TABLE_RAM: 1 keeps the table that says which handler an opcode uses in RAM
 * (21 KB); 0 keeps it in flash.
 */

/*
 * OCCLUSION: 1 finds, before a strip is painted, which pixels the opaque tiles of each
 * layer will cover, and does not paint what is under them (the converter marks the tiles
 * that have no transparent pixel). 0 paints every layer in full, back to front. Measured
 * on the board it costs more than it saves (see the README), so 0 is the default.
 */
#ifndef OCCLUSION
#define OCCLUSION 0
#endif

/*
 * STRIP_REUSE: 1 keeps, for each 16-row strip, a signature of everything its picture
 * depends on (scroll registers, the maps, the palette, the sprites that touch it), and
 * neither draws nor sends a strip whose signature has not changed since it was last sent.
 * The panel keeps what it was given. In a fight about one strip in six is spared; on the
 * title screen nearly all of them. 0 draws and sends every strip of every drawn frame.
 */
#ifndef STRIP_REUSE
#define STRIP_REUSE 1
#endif

/*
 * TILE_CACHE_KB: 0, 8, 16 or 32 was to be RAM given to a cache of tile rows copied out of
 * flash. Its best possible case was measured instead of building it: drawing every tile out
 * of RAM rather than flash (EXPERIMENT=EXPERIMENT_RAM_TILES, which draws the wrong
 * picture, fast) saves only what the README says it saves, and with the sound on there is
 * not 8 KB of heap to give it. So only 0 is built.
 */
#ifndef TILE_CACHE_KB
#define TILE_CACHE_KB 0
#endif
#if TILE_CACHE_KB != 0
#error "TILE_CACHE_KB: not built; the most it could buy was measured and is in the README"
#endif

/* STATS: the once-a-second line over serial. It is the product; turn it off only to see
 * what printing it costs. */
#ifndef STATS
#define STATS 1
#endif

/*
 * BENCH_SECONDS: 0 runs until told to stop. Anything else is a bench: run the machine as
 * fast as it will go, undrawn, to BENCH_FROM_FRAME (the start of the first attract fight,
 * which takes 10 to 20 seconds rather than 57), then run it properly for this many
 * seconds with the stats line printing, then print one "bench" line with the same fields
 * averaged over the whole measurement, print "bench done", and leave for the menu.
 */
#ifndef BENCH_SECONDS
#define BENCH_SECONDS 0
#endif
#ifndef BENCH_FROM_FRAME
#define BENCH_FROM_FRAME 3400
#endif

#endif
