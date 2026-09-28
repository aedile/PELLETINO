/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * dkong_sound.c - the 8035 sound board.
 *
 * The music and most of the effects are the MCU's own work: it writes eight-bit samples to a
 * DAC on port 1, fetching sample data a page at a time through its BUS port, with the page
 * number in the low bits of port 2. So what comes out here is the real tunes, played by the
 * real program, rather than a reconstruction.
 *
 * A handful of percussive effects are not the MCU at all - they are discrete circuits gated by
 * a latch the main CPU writes at 0x7D00-0x7D07. Those are synthesised, because a netlist is
 * not something a microcontroller is going to run.
 */
#include "dkong_internal.h"
#include <string.h>

/* ---- the MCU ---- */
static uint8_t latch_3d;          /* the sound command from the main CPU */
static uint8_t latch_6h;          /* the effect lines, and the MCU's two test pins */
static uint8_t p2_latch;          /* the MCU's port 2, as read back */
static uint8_t dac;               /* the current DAC level */
static int snd_irq;

static uint8_t bus_read(uint16_t offset);
static uint8_t p2_read(void);
static void p1_write(uint8_t v);
static void p2_write(uint8_t v);

#define MCS48_ROM(a)      dk_roms.snd[(a) & 0xfff]
#define MCS48_BUS_IN(a)   bus_read((uint16_t)(a))
#define MCS48_BUS_OUT(v)  ((void)(v))          /* the speech output, never fitted on this board */
#define MCS48_P1_OUT(v)   p1_write(v)
#define MCS48_P2_IN()     p2_read()
#define MCS48_P2_OUT(v)   p2_write(v)
#define MCS48_T0()        (!((latch_6h >> 5) & 1))
#define MCS48_T1()        (!((latch_6h >> 4) & 1))
#include "mcs48.h"

static mcs48_t mcu;

static uint8_t bus_read(uint16_t offset)
{
    uint8_t page = (uint8_t)(p2_latch & 0x47);
    if (page & 0x40) return (uint8_t)(latch_3d & 0x0f);        /* the command, low nibble */
    return dk_roms.samp[((page & 7) * 256 + (offset & 0xff)) & 0x7ff];
}

/* ---- the DAC, resampled into a ring the mixer drains ---- */
#define RING 4096
static uint8_t ring[RING];
static uint32_t ring_w, ring_r;
static uint32_t samp_acc;          /* 16.16 fraction of an output sample */
static uint32_t samp_step;         /* output samples per MCU machine cycle, 16.16 */

/*
 * Port 2 is not simply what the MCU last wrote. Bit 5 is not the MCU's output at all: it is
 * wired back from bit 3 of the sound-signal latch the main CPU writes, and the whole port is
 * inverted on the way in. Returning the MCU's own latch instead sends a JB5 at the top of its
 * main loop down the wrong branch every single time, which is how it ended up droning.
 */
static uint8_t p2_read(void)
{
    uint8_t v = (uint8_t)((p2_latch & ~0x20) | (((latch_6h >> 3) & 1) << 5));
    return (uint8_t)(v ^ 0x20);
}

static void p1_write(uint8_t v) { dac = v; }
static void p2_write(uint8_t v) { p2_latch = v; }

/* ---- the discrete effects ---- */
static float walk_env, jump_env, boom_env, boom_lp, dac_dc;
static float jump_ph;
static uint32_t noise = 1;
static uint8_t prev_sig;

void dk_sound_latch_w(uint8_t data) { latch_3d = (uint8_t)((data & 0x0f) ^ 0x0f); }

void dk_sound_sig_w(int bit, int state)
{
    uint8_t before = latch_6h;
    if (state) latch_6h |= (uint8_t)(1 << bit); else latch_6h &= (uint8_t)~(1 << bit);
    uint8_t rising = (uint8_t)(latch_6h & ~before);
    if (rising & 0x01) walk_env = 1.0f;                        /* the footstep thump */
    if (rising & 0x02) { jump_env = 1.0f; jump_ph = 0.0f; }    /* the jump */
    if (rising & 0x04) boom_env = 1.0f;                        /* the stomp, and Kong landing */
    prev_sig = latch_6h;
}

void dk_sound_irq_w(int state) { snd_irq = state; mcu.irq_line = (uint8_t)state; }

void dk_sound_init(void)
{
    /* a machine cycle is 15 oscillator periods, and we want 20050 output samples a second */
    samp_step = (uint32_t)(((uint64_t)20050 << 16) / (DK_SND_CLOCK / 15));
    dk_sound_reset();
}

void dk_sound_reset(void)
{
    mcs48_reset(&mcu);
    latch_3d = 0x0f; latch_6h = 0; p2_latch = 0; dac = 0x80; snd_irq = 0;
    ring_w = ring_r = 0; samp_acc = 0;
    walk_env = jump_env = boom_env = boom_lp = jump_ph = 0.0f;
    dac_dc = 1.0f;
    memset(ring, 0x80, sizeof(ring));
}

/* advance the MCU alongside the Z80 and drop DAC samples into the ring as it goes */
void dk_sound_run(int32_t z80_cycles)
{
    /* the MCU's machine cycle is 400 kHz against the Z80's 3.072 MHz */
    static uint32_t cyc_acc;
    cyc_acc += (uint32_t)z80_cycles * (DK_SND_CLOCK / 15);
    uint32_t machine_cycles = cyc_acc / DK_CPU_CLOCK;
    cyc_acc -= machine_cycles * DK_CPU_CLOCK;
    if (machine_cycles > 4000) machine_cycles = 4000;          /* never let a stall become a freeze */

    uint32_t done = 0;
    while (done < machine_cycles) {
        int n = mcs48_step(&mcu);
        done += (uint32_t)n;
        samp_acc += (uint32_t)n * samp_step;
        while (samp_acc >= (1u << 16)) {
            samp_acc -= (1u << 16);
            uint32_t next = (ring_w + 1) % RING;
            if (next != ring_r) { ring[ring_w] = dac; ring_w = next; }
        }
    }
}

uint16_t dk_sound_pc(void) { return mcu.pc; }
/* the main CPU watches port 2 bit 4, inverted, to know when the MCU is busy */
int dk_sound_status(void) { return !((p2_latch >> 4) & 1); }

void dk_render_audio(int16_t *buf, int samples, int rate)
{
    (void)rate;
    for (int i = 0; i < samples; i++) {
        /* The DAC is unipolar - the program swings it inside whatever range it likes and the
         * board's coupling capacitor throws the offset away. So do the same here, with a
         * one-pole high pass, rather than assuming it sits at mid-scale. */
        int v = 0x80;
        if (ring_r != ring_w) { v = ring[ring_r]; ring_r = (ring_r + 1) % RING; }
        float raw = (float)v / 128.0f;
        dac_dc += (raw - dac_dc) * 0.0063f;            /* about a 20 Hz corner */
        float out = (raw - dac_dc) * 1.7f;

        /* the discrete channels, mixed in after it as they are on the board */
        noise = (noise >> 1) | ((((noise >> 0) ^ (noise >> 3)) & 1) << 16);
        float n = (noise & 1) ? 1.0f : -1.0f;
        if (walk_env > 0.002f) {
            boom_lp += (n - boom_lp) * 0.25f;
            out += boom_lp * walk_env * 0.20f;
            walk_env -= walk_env * 60.0f / 20050.0f;
        }
        if (jump_env > 0.002f) {
            jump_ph += (240.0f + 700.0f * jump_env) / 20050.0f;
            if (jump_ph >= 1.0f) jump_ph -= 1.0f;
            out += (jump_ph < 0.5f ? 0.22f : -0.22f) * jump_env;
            jump_env -= jump_env * 9.0f / 20050.0f;
        }
        if (boom_env > 0.002f) {
            boom_lp += (n - boom_lp) * 0.05f;
            out += boom_lp * boom_env * 0.5f;
            boom_env -= boom_env * 5.0f / 20050.0f;
        }

        int32_t s = (int32_t)(out * 12000.0f);
        buf[i] = (int16_t)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
    }
}
