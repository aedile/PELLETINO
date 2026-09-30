/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * cps1_sound.c - the CPS-1 sound section: a Z80 at 3.579545 MHz, a YM2151 on the same
 * clock, an OKI MSM6295 on 1 MHz, and two latches the 68000 writes.
 *
 *   0000-7FFF  ROM
 *   8000-BFFF  ROM, one of two banks (F004)
 *   D000-D7FF  RAM
 *   F000-F001  YM2151          F002  MSM6295
 *   F004       bank            F006  MSM6295 pin 7
 *   F008       sound command   F00A  fade timer
 *
 * The Z80 has no interrupt of its own: the YM2151's timers interrupt it, and that is the
 * tempo of everything it plays. So the Z80 is run from one timer overflow to the next, and
 * the timers are advanced by exactly the cycles it used.
 *
 * Between interrupts the sound program does nothing, and it does it busily: it compares a
 * count the interrupt handler keeps against the count it has dealt with, and goes round
 * again if they are equal, 85,000 times a second. With IDLE_SKIP, when the Z80 is at the
 * top of that loop and the two counts are equal, whole turns of the loop are taken off the
 * clock without being executed; the part of a turn left over is executed as usual, so an
 * interrupt finds the Z80 at the instruction and the cycle it would have. The loop is
 * found by its shape. The Z80 core is not changed for this; it is simply not called.
 *
 * The Z80 is Marat Fayzullin's. With SOUND=OFF none of this runs.
 */
#include "cps1_internal.h"
#include "knobs.h"
#include <string.h>

#if SOUND == SOUND_OFF

void cps1_sound_init(const cps1_roms_t *roms, int rate) { (void)roms; (void)rate; }
void cps1_render_audio(int16_t *out, int samples) { memset(out, 0, (size_t)samples * sizeof(int16_t)); }

#else

#include "Z80.h"
#include "ym2151.h"
#include "okim6295.h"

#define OKI_CLOCK 1000000            /* 16 MHz / 4 / 4 */

static int32_t z80_done;             /* Z80 cycles run so far this frame */

#ifdef CPS1_PROFILE
/* host only: every write to the YM2151 and when, for comparing against a reference */
void cps1_ym_trace(int port, int v, int64_t clock);
static int64_t trace_clock;
#define YM_TRACE(p, v) cps1_ym_trace(p, v, trace_clock + z80_done)
#else
#define YM_TRACE(p, v)
#endif

static Z80 cpu;
static const uint8_t *rom, *bank;
static uint8_t ram[0x800];
static int32_t z80_debt;

#ifdef CPS1_PROFILE
uint32_t cps1_z80_reads[0x10000];
uint64_t cps1_z80_cycles, cps1_z80_skipped;
#endif

byte RdZ80(register word a)
{
#ifdef CPS1_PROFILE
    cps1_z80_reads[a]++;
#endif
    if (a < 0x8000) return rom[a];
    if (a < 0xc000) return bank[a - 0x8000];
    if (a >= 0xd000 && a < 0xd800) return ram[a - 0xd000];
    switch (a) {
    case 0xf001: return ym2151_status();
#if SOUND == SOUND_FM_ADPCM
    case 0xf002: return okim6295_read();
#else
    case 0xf002: return 0xf0;                    /* nothing is ever playing */
#endif
    case 0xf008: return cps1.latch[0];
    case 0xf00a: return cps1.latch[1];
    }
    return 0xff;
}

void WrZ80(register word a, register byte v)
{
    if (a >= 0xd000 && a < 0xd800) { ram[a - 0xd000] = v; return; }
    switch (a) {
    case 0xf000: ym2151_write(0, v); YM_TRACE(0, v); break;
    case 0xf001:
        ym2151_write(1, v); YM_TRACE(1, v);
        /* the write may have taken the interrupt away, or (enabling a timer whose flag is
         * up) brought it: the core looks at this when interrupts are next enabled */
        cpu.IRequest = ym2151_irq() ? INT_IRQ : INT_NONE;
        break;
#if SOUND == SOUND_FM_ADPCM
    case 0xf002: okim6295_write(v); break;
    case 0xf006: okim6295_set_pin7(v & 1); break;
#endif
    case 0xf004: bank = rom + 0x8000 + (v & 1) * 0x4000; break;
    }
}

/* LD HL,nn / LD A,(nn) / CP (HL) / JR Z,back to the LD HL: 10 + 13 + 7 + 12 cycles a turn */
#define IDLE_TURN 42
static int idle_pc = -1;
static const uint8_t *idle_a, *idle_b;

static void find_idle_loop(void)
{
    idle_pc = -1;
#if IDLE_SKIP
    for (int a = 0; a < 0x7ff0; a++) {
        const uint8_t *c = rom + a;
        if (c[0] != 0x21 || c[3] != 0x3a || c[6] != 0xbe || c[7] != 0x28 || c[8] != 0xf7) continue;
        unsigned hl = c[1] | (c[2] << 8), nn = c[4] | (c[5] << 8);
        if (hl < 0xd000 || hl >= 0xd800 || nn < 0xd000 || nn >= 0xd800) continue;
        idle_pc = a; idle_a = &ram[nn - 0xd000]; idle_b = &ram[hl - 0xd000];
        return;
    }
#endif
}

byte InZ80(register word p) { (void)p; return 0xff; }
void OutZ80(register word p, register byte v) { (void)p; (void)v; }
void PatchZ80(register Z80 *r) { (void)r; }
word LoopZ80(register Z80 *r) { (void)r; return INT_NONE; }

void cps1_sound_init(const cps1_roms_t *roms, int rate)
{
    rom = roms->z80;
    ym2151_init(CPS1_Z80_CLOCK, rate);
#if SOUND == SOUND_FM_ADPCM
    okim6295_init(roms->oki, roms->oki_read, 0x40000, OKI_CLOCK, rate);
#endif
    cps1_sound_reset();
}

void cps1_sound_reset(void)
{
    if (!rom) rom = cps1.roms.z80;
    bank = rom + 0x8000;
    find_idle_loop();
    memset(ram, 0, sizeof(ram));
    memset(&cpu, 0, sizeof(cpu));
    ResetZ80(&cpu);
    cpu.IRequest = INT_NONE;
    cpu.IAutoReset = 0;
    z80_done = 0; z80_debt = 0;
    ym2151_reset();
#if SOUND == SOUND_FM_ADPCM
    okim6295_reset();
#endif
}

void cps1_sound_run_to(int32_t m68k_cycles)
{
    /* 3.579545 MHz against 10 MHz */
    int32_t target = (int32_t)(((int64_t)m68k_cycles * CPS1_Z80_CLOCK) / CPS1_M68K_CLOCK);
    while (z80_done < target) {
        /* the line is a level. If the Z80 has interrupts off it is taken when they come
         * back on, part way through a slice: the core does that itself, given IRequest. */
        cpu.IRequest = ym2151_irq() ? INT_IRQ : INT_NONE;
        if (cpu.IRequest != INT_NONE && (cpu.IFF & IFF_1) && !(cpu.IFF & IFF_EI)) IntZ80(&cpu, INT_IRQ);
        int32_t n = target - z80_done, t = ym2151_clocks_to_overflow();
        if (n > t) n = t;
        int32_t want = n - z80_debt;
        z80_debt = 0;
        while (want > 0) {
#if IDLE_SKIP
            if (want > IDLE_TURN && (unsigned)(cpu.PC.W - idle_pc) < 9 && !(cpu.IFF & (IFF_EI | IFF_HALT))) {
                /* in the loop: walk to the top of it, and if there is nothing to wait for
                 * but an interrupt, take the whole turns that are left off the clock */
                while (want > IDLE_TURN && cpu.PC.W != idle_pc && (unsigned)(cpu.PC.W - idle_pc) < 9)
                    want = ExecZ80(&cpu, 1) + want - 1;
                if (want > IDLE_TURN && cpu.PC.W == idle_pc && *idle_a == *idle_b && cpu.IRequest == INT_NONE) {
                    int32_t turns = (want - 1) / IDLE_TURN;
                    want -= turns * IDLE_TURN;
#ifdef CPS1_PROFILE
                    cps1_z80_skipped += (uint64_t)(turns * IDLE_TURN);
#endif
                }
            }
            /* otherwise it is working: look again every few hundred cycles */
            int32_t go = want > 384 ? 384 : want;
#else
            int32_t go = want;
#endif
            int32_t left = ExecZ80(&cpu, go);
#ifdef CPS1_PROFILE
            cps1_z80_cycles += (uint64_t)(go - left);
#endif
            want -= go - left;
        }
        z80_debt = -want;                        /* it finishes the instruction it is in */
        ym2151_advance(n);
        z80_done += n;
    }
}

void cps1_sound_end_frame(int32_t m68k_cycles)
{
    int32_t frame = (int32_t)(((int64_t)m68k_cycles * CPS1_Z80_CLOCK) / CPS1_M68K_CLOCK);
    z80_done -= frame;
#ifdef CPS1_PROFILE
    trace_clock += frame;
#endif
}

void cps1_render_audio(int16_t *out, int samples)
{
    int32_t mix[128];
    while (samples > 0) {
        int n = samples > 128 ? 128 : samples;
        memset(mix, 0, sizeof(int32_t) * (size_t)n);
        ym2151_render(mix, n);
        /* the board mixes the YM2151 at 0.35 a side and the MSM6295 at 0.30 */
        for (int i = 0; i < n; i++) mix[i] = (mix[i] * 45) >> 7;
#if SOUND == SOUND_FM_ADPCM
        int32_t pcm[128];
        memset(pcm, 0, sizeof(int32_t) * (size_t)n);
        okim6295_render(pcm, n);
        for (int i = 0; i < n; i++) mix[i] += (pcm[i] * 77) >> 8;
#endif
        for (int i = 0; i < n; i++) {
            int32_t v = mix[i];
            out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        out += n; samples -= n;
    }
}

#endif
