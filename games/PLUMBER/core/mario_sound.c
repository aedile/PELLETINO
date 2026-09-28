/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * mario_sound.c - the 8039 sound board.
 *
 * The tunes and most effects are the MCU's own work: it writes eight-bit samples to a DAC on
 * its BUS port, fetching sample data a page at a time from the second half of its own ROM with
 * the page number in the low bits of port 2, and reads the tune the main CPU asked for through
 * the same BUS port when port 2 bit 7 is up. The main CPU talks to it through a latch at
 * 0x7F00-0x7F07: bit 0 is the MCU's interrupt (a death), bits 1 and 2 its two test pins (a
 * coin taken, ice), bits 3-6 are read back on port 1 (crab, turtle, fly, coin), and bit 7 is
 * the skid, which is a discrete circuit. The footsteps at 0x7C00/0x7C80 are discrete too.
 * Those last are synthesised.
 */
#include "mario_internal.h"
#include <string.h>

/* ---- the MCU ---- */
static uint8_t tune_latch;        /* the sound command from the main CPU */
static uint8_t sig_latch;         /* 0x7F00-0x7F07 */
static uint8_t p2_latch;
static uint8_t dac;

static uint8_t bus_read(uint16_t offset);

#define MCS48_ROM(a)      mb_roms.snd[(a) & 0xfff]
#define MCS48_BUS_IN(a)   bus_read((uint16_t)(a))
#define MCS48_BUS_OUT(v)  (dac = (v))
#define MCS48_P1_OUT(v)   ((void)(v))
#define MCS48_P1_IN()     ((uint8_t)((sig_latch >> 3) & 0x0f))
#define MCS48_P2_IN()     ((uint8_t)(p2_latch & 0xef))   /* bit 4 is grounded */
#define MCS48_P2_OUT(v)   (p2_latch = (v))
#define MCS48_T0()        ((sig_latch >> 1) & 1)
#define MCS48_T1()        ((sig_latch >> 2) & 1)
#include "mcs48.h"

static mcs48_t mcu;

static uint8_t bus_read(uint16_t offset)
{
    if (p2_latch & 0x80) return tune_latch;
    return mb_roms.snd[((p2_latch & 0x0f) << 8) | (offset & 0xff)];
}

/* ---- the DAC, resampled into a ring the mixer drains ---- */
#define RING 4096
static uint8_t ring[RING];
static uint32_t ring_w, ring_r;
static uint32_t samp_acc;          /* 16.16 fraction of an output sample */
static uint32_t samp_step;         /* output samples per MCU machine cycle, 16.16 */

/* ---- the discrete effects ---- */
static int32_t run_env[2], skid_env, dac_dc, run_lp;
static uint32_t run_ph[2], skid_ph;
static uint32_t noise = 1;

void mb_sound_latch_w(uint8_t data) { tune_latch = data; }

void mb_sound_sig_w(int bit, int state)
{
    uint8_t before = sig_latch;
    if (state) sig_latch |= (uint8_t)(1 << bit); else sig_latch &= (uint8_t)~(1 << bit);
    if (bit == 0) mcu.irq_line = (uint8_t)state;
    if ((sig_latch & ~before) & 0x80) { skid_env = 32767; skid_ph = 0; }
}

void mb_sound_run_w(int who, uint8_t data)
{
    (void)data;
    run_env[who & 1] = 32767;                          /* a footstep */
}

void mb_sound_init(void)
{
    /* a machine cycle is 15 oscillator periods, and we want 20050 output samples a second */
    samp_step = (uint32_t)(((uint64_t)20050 << 16) / (MB_SND_CLOCK / 15));
    mb_sound_reset();
}

void mb_sound_reset(void)
{
    mcs48_reset(&mcu);
    tune_latch = 0; sig_latch = 0; p2_latch = 0xff; dac = 0x80;   /* the MCU's ports come up all ones */
    ring_w = ring_r = 0; samp_acc = 0;
    run_env[0] = run_env[1] = skid_env = 0; run_lp = 0; dac_dc = 128 << 8;
    memset(ring, 0x80, sizeof(ring));
}

/* advance the MCU alongside the Z80 and drop DAC samples into the ring as it goes */
void mb_sound_run(int32_t z80_cycles)
{
    static uint32_t cyc_acc;
    cyc_acc += (uint32_t)z80_cycles * (MB_SND_CLOCK / 15);
    uint32_t machine_cycles = cyc_acc / MB_CPU_CLOCK;
    cyc_acc -= machine_cycles * MB_CPU_CLOCK;
    if (machine_cycles > 8000) machine_cycles = 8000;          /* never let a stall become a freeze */

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

uint16_t mb_sound_pc(void) { return mcu.pc; }

void mb_render_audio(int16_t *buf, int samples, int rate)
{
    (void)rate;
    for (int i = 0; i < samples; i++) {
        /* the DAC is unipolar and the board's coupling capacitor throws the offset away:
         * a one-pole high pass does the same here */
        static int v = 0x80;                               /* hold the last value when the ring runs dry */
        if (ring_r != ring_w) { v = ring[ring_r]; ring_r = (ring_r + 1) % RING; }
        int32_t raw = v << 8;                              /* 0..65280 */
        dac_dc += (raw - dac_dc) >> 8;
        int32_t out = ((raw - dac_dc) * 5) >> 3;

        noise = (noise >> 1) | ((((noise >> 0) ^ (noise >> 3)) & 1) << 16);
        int32_t n = (noise & 1) ? 32767 : -32767;
        for (int w = 0; w < 2; w++) {
            if (run_env[w] > 60) {                         /* a footstep: a short low click */
                run_ph[w] += (uint32_t)(((uint64_t)180 << 32) / 20050);
                run_lp += (((run_ph[w] & 0x80000000u) ? n / 2 : -n / 2) - run_lp) >> 2;
                out += (run_lp * run_env[w]) >> 17;
                run_env[w] -= run_env[w] >> 6;
            }
        }
        if (skid_env > 60) {                               /* the skid: a buzz sliding down */
            skid_ph += (uint32_t)(((uint64_t)(200 + ((skid_env * 600) >> 15)) << 32) / 20050);
            out += (((skid_ph & 0x80000000u) ? 32767 : -32767) * skid_env) >> 18;
            skid_env -= skid_env >> 9;
        }

        if (out > 32767) out = 32767; else if (out < -32768) out = -32768;
        buf[i] = (int16_t)out;
    }
}
