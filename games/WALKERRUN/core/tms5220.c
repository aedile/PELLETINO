/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * tms5220.c - TMS5220 LPC speech synthesis (see tms5220.h)
 */
#include "tms5220.h"
#include <string.h>

/* ---- coefficient ROM (TMS5220, from MAME tms5110r.hxx) ---- */
static const int16_t energytable[16] = { 0, 1, 2, 3, 4, 6, 8, 11, 16, 23, 33, 47, 63, 85, 114, 0 };
static const int16_t pitchtable[64] = {
    0,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,
    30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  44,  46,  48,
    50,  52,  53,  56,  58,  60,  62,  65,  68,  70,  72,  76,  78,  80,  84,  86,
    91,  94,  98, 101, 105, 109, 114, 118, 122, 127, 132, 137, 142, 148, 153, 159 };
static const int16_t k1[32] = { -501, -498, -497, -495, -493, -491, -488, -482, -478, -474, -469, -464, -459, -452, -445, -437,
                                -412, -380, -339, -288, -227, -158,  -81,   -1,   80,  157,  226,  287,  337,  379,  411,  436 };
static const int16_t k2[32] = { -328, -303, -274, -244, -211, -175, -138,  -99,  -59,  -18,   24,   64,  105,  143,  180,  215,
                                 248,  278,  306,  331,  354,  374,  392,  408,  422,  435,  445,  455,  463,  470,  476,  506 };
static const int16_t k3[16] = { -441, -387, -333, -279, -225, -171, -117,  -63,   -9,   45,   98,  152,  206,  260,  314,  368 };
static const int16_t k4[16] = { -328, -273, -217, -161, -106,  -50,    5,   61,  116,  172,  228,  283,  339,  394,  450,  506 };
static const int16_t k5[16] = { -328, -282, -235, -189, -142,  -96,  -50,   -3,   43,   90,  136,  182,  229,  275,  322,  368 };
static const int16_t k6[16] = { -256, -212, -168, -123,  -79,  -35,   10,   54,   98,  143,  187,  232,  276,  320,  365,  409 };
static const int16_t k7[16] = { -308, -260, -212, -164, -117,  -69,  -21,   27,   75,  122,  170,  218,  266,  314,  361,  409 };
static const int16_t k8[8]  = { -256, -161,  -66,   29,  124,  219,  314,  409 };
static const int16_t k9[8]  = { -256, -176,  -96,  -15,   65,  146,  226,  307 };
static const int16_t k10[8] = { -205, -132,  -59,   14,   87,  160,  234,  307 };
static const int16_t *ktable[10] = { k1, k2, k3, k4, k5, k6, k7, k8, k9, k10 };
static const uint8_t kbits[10] = { 5, 5, 4, 4, 4, 4, 4, 3, 3, 3 };
static const uint8_t chirptable[52] = {
    0x00, 0x03, 0x0f, 0x28, 0x4c, 0x6c, 0x71, 0x50, 0x25, 0x26, 0x4c, 0x44, 0x1a, 0x32, 0x3b, 0x13,
    0x37, 0x1a, 0x25, 0x1f, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00 };
static const uint8_t interp_coeff[8] = { 0, 3, 3, 3, 2, 2, 1, 1 };

static inline int talk_status(const tms5220_t *t) { return t->SPEN || t->TALKD; }

void tms5220_init(tms5220_t *t) { tms5220_reset(t); }

void tms5220_reset(tms5220_t *t)
{
    memset(t, 0, sizeof(*t));
    t->wsq = t->rsq = 1;
    t->buffer_low = t->buffer_empty = 1;
    t->inhibit = 1;
    t->OLDE = t->OLDP = 1;
    t->RNG = 0x1FFF;
}

static void update_fifo_status(tms5220_t *t)
{
    t->buffer_low = (t->fifo_count <= 8);
    if (t->fifo_count == 0) {
        t->buffer_empty = 1;
        if (t->DDIS) t->TALK = t->SPEN = 0;    /* buffer empty ends speech (patent gate 232b) */
    } else {
        t->buffer_empty = 0;
    }
    if (t->previous_talk_status && !talk_status(t)) t->DDIS = 0;
    t->previous_talk_status = talk_status(t);
}

static int read_bits(tms5220_t *t, int count)
{
    int val = 0;
    while (count--) {
        val = (val << 1) | ((t->fifo[t->fifo_head] >> t->fifo_bits_taken) & 1);
        if (++t->fifo_bits_taken >= 8) {
            if (t->fifo_count > 0) t->fifo_count--;
            t->fifo[t->fifo_head] = 0;
            t->fifo_head = (t->fifo_head + 1) % TMS_FIFO_SIZE;
            t->fifo_bits_taken = 0;
            update_fifo_status(t);
        }
    }
    return val;
}

static void command(tms5220_t *t, uint8_t d)
{
    t->commands++;
    switch (d & 0x70) {
        case 0x60:                          /* Speak External */
            memset(t->fifo, 0, sizeof(t->fifo));
            t->fifo_head = t->fifo_tail = t->fifo_count = t->fifo_bits_taken = 0;
            t->DDIS = 1;
            t->zpar = t->uv_zpar = 1;
            t->OLDE = t->OLDP = 1;
            break;
        case 0x70: {                        /* Reset */
            uint32_t c = t->commands, b = t->bytes_in, f = t->frames, s = t->talk_starts;
            tms5220_reset(t);
            t->commands = c; t->bytes_in = b; t->frames = f; t->talk_starts = s;
            break;
        }
        default:                            /* Speak / Load Address / Read: no VSM on this board */
            break;
    }
    update_fifo_status(t);
}

static void data_write(tms5220_t *t, uint8_t d)
{
    int old_buffer_low = t->buffer_low;
    if (!t->DDIS) { command(t, d); return; }
    if (t->fifo_count >= TMS_FIFO_SIZE) return;
    t->fifo[t->fifo_tail] = d;
    t->fifo_tail = (t->fifo_tail + 1) % TMS_FIFO_SIZE;
    t->fifo_count++;
    t->bytes_in++;
    update_fifo_status(t);
    if (!t->SPEN && old_buffer_low && !t->buffer_low) {
        /* nine bytes in: start talking from a silent, zeroed frame */
        t->zpar = t->uv_zpar = 1;
        t->OLDE = t->OLDP = 1;
        t->SPEN = 1;
        t->talk_starts++;
        t->new_energy_idx = 0;
        t->new_pitch_idx = 0;
        for (int i = 0; i < 4; i++) t->new_k_idx[i] = 0;
        for (int i = 4; i < 7; i++) t->new_k_idx[i] = 0xF;
        for (int i = 7; i < 10; i++) t->new_k_idx[i] = 0x7;
    }
}

void tms5220_data_write(tms5220_t *t, uint8_t d) { t->data_in = d; }

void tms5220_wsq(tms5220_t *t, int level)
{
    if (t->wsq && !level) data_write(t, t->data_in);   /* latched on the falling edge of /WS */
    t->wsq = level;
}

void tms5220_rsq(tms5220_t *t, int level) { t->rsq = level; }

uint8_t tms5220_status(tms5220_t *t)
{
    return (uint8_t)((talk_status(t) << 7) | (t->buffer_low << 6) | (t->buffer_empty << 5));
}

int tms5220_readyq(const tms5220_t *t)
{
    return (t->fifo_count >= TMS_FIFO_SIZE && t->DDIS) ? 1 : 0;
}


/* ---- synthesis ---- */
static void parse_frame(tms5220_t *t)
{
    t->uv_zpar = t->zpar = 0;
    t->IP = 0;
    t->frames++;
    update_fifo_status(t);
    if (t->DDIS && t->buffer_empty) return;
    t->new_energy_idx = read_bits(t, 4);
    update_fifo_status(t);
    if (t->DDIS && t->buffer_empty) return;
    if (t->new_energy_idx == 0 || t->new_energy_idx == 15) return;
    int rep = read_bits(t, 1);
    t->new_pitch_idx = read_bits(t, 6);
    t->uv_zpar = (t->new_pitch_idx == 0);
    update_fifo_status(t);
    if (t->DDIS && t->buffer_empty) return;
    if (rep) return;
    for (int i = 0; i < 4; i++) {
        t->new_k_idx[i] = read_bits(t, kbits[i]);
        update_fifo_status(t);
        if (t->DDIS && t->buffer_empty) return;
    }
    if (t->new_pitch_idx == 0) return;
    for (int i = 4; i < 10; i++) {
        t->new_k_idx[i] = read_bits(t, kbits[i]);
        update_fifo_status(t);
        if (t->DDIS && t->buffer_empty) return;
    }
}

static inline int32_t matrix_multiply(int32_t a, int32_t b)
{
    while (a > 511) a -= 1024;
    while (a < -512) a += 1024;
    while (b > 16383) b -= 32768;
    while (b < -16384) b += 32768;
    return (a * b) >> 9;
}

static int32_t lattice_filter(tms5220_t *t)
{
    int32_t *u = t->u, *x = t->x;
    const int *k = t->current_k;
    u[10] = matrix_multiply(t->previous_energy, t->excitation << 6);
    for (int i = 9; i >= 0; i--) u[i] = u[i + 1] - matrix_multiply(k[i], x[i]);
    for (int i = 9; i >= 1; i--) x[i] = x[i - 1] + matrix_multiply(k[i - 1], u[i - 1]);
    x[0] = u[0];
    t->previous_energy = t->current_energy;
    return u[0];
}

static int16_t clip_analog(int32_t v)
{
    if (v > 2047) v = 2047;
    if (v < -2048) v = -2048;
    v &= ~0xF;
    return (int16_t)((v << 4) | ((v & 0x7F0) >> 3) | ((v & 0x400) >> 10));
}

/* one 8.4 kHz output sample */
static int16_t synth_sample(tms5220_t *t)
{
    int16_t out = 0;
    if (t->TALKD) {
        if (t->IP == 0 && t->PC == 12 && t->subcycle == 1) {
            t->IP = 0;
            parse_frame(t);
            if (t->new_energy_idx == 0x0F) {           /* stop frame */
                t->TALK = t->SPEN = 0;
                update_fifo_status(t);
            }
            int new_unvoiced = (t->new_pitch_idx == 0), new_silence = (t->new_energy_idx == 0);
            if ((!t->OLDP && new_unvoiced) || (t->OLDP && !new_unvoiced) ||
                (t->OLDE && !new_silence) || (t->OLDP && new_silence))
                t->inhibit = 1;
            else
                t->inhibit = 0;
        } else if (t->subcycle == 2) {
            int inhibit_state = (t->inhibit && t->IP != 0);
            int sh = interp_coeff[t->IP];
            switch (t->PC) {
                case 0:
                    if (t->IP == 0) t->pitch_zero = 0;
                    t->current_energy = (t->current_energy + (((energytable[t->new_energy_idx] - t->current_energy) * (1 - inhibit_state)) >> sh)) * (1 - t->zpar);
                    break;
                case 1:
                    t->current_pitch = (t->current_pitch + (((pitchtable[t->new_pitch_idx] - t->current_pitch) * (1 - inhibit_state)) >> sh)) * (1 - t->zpar);
                    break;
                default:
                    if (t->PC >= 2 && t->PC <= 11) {
                        int i = t->PC - 2;
                        int zp = (i < 4) ? t->zpar : t->uv_zpar;
                        t->current_k[i] = (t->current_k[i] + (((ktable[i][t->new_k_idx[i]] - t->current_k[i]) * (1 - inhibit_state)) >> sh)) * (1 - zp);
                    }
                    break;
            }
        }

        /* excitation */
        if (t->OLDP) {
            t->excitation = (t->RNG & 1) ? ~0x3F : 0x40;
        } else {
            t->excitation = (int8_t)chirptable[t->pitch_count >= 51 ? 51 : t->pitch_count];
        }
        for (int i = 0; i < 20; i++) {
            uint32_t bit = ((t->RNG >> 12) ^ (t->RNG >> 3) ^ (t->RNG >> 2) ^ t->RNG) & 1;
            t->RNG = ((t->RNG << 1) | bit) & 0x1FFF;
        }
        int32_t s = lattice_filter(t);
        while (s > 16383) s -= 32768;
        while (s < -16384) s += 32768;
        out = clip_analog(s);

        /* counters */
        t->subcycle++;
        if (t->subcycle == 2 && t->PC == 12) {
            if (t->IP == 7 && t->inhibit) t->pitch_zero = 1;
            if (t->IP == 7) {
                t->OLDE = (t->new_energy_idx == 0);
                t->OLDP = (t->new_pitch_idx == 0);
                t->TALKD = t->TALK;
                update_fifo_status(t);
                if (!t->TALK && t->SPEN) t->TALK = 1;
            }
            t->subcycle = 1;
            t->PC = 0;
            t->IP = (t->IP + 1) & 7;
        } else if (t->subcycle == 3) {
            t->subcycle = 1;
            t->PC++;
        }
        t->pitch_count++;
        if (t->pitch_count >= t->current_pitch || t->pitch_zero) t->pitch_count = 0;
        t->pitch_count &= 0x1FF;
    } else {
        t->subcycle++;
        if (t->subcycle == 2 && t->PC == 12) {
            if (t->IP == 7) {
                t->TALKD = t->TALK;
                update_fifo_status(t);
                if (!t->TALK && t->SPEN) t->TALK = 1;
            }
            t->subcycle = 1;
            t->PC = 0;
            t->IP = (t->IP + 1) & 7;
        } else if (t->subcycle == 3) {
            t->subcycle = 1;
            t->PC++;
        }
        out = 0;
    }
    return out;
}

#define RING_SIZE 2048
#define CYCLES_PER_SAMPLE 180      /* 1.512 MHz CPU clock / 8.4 kHz sample rate */

/* Synthesis keeps pace with the emulated CPUs, so the FIFO drains and the
 * status/ready lines change on the same schedule as the real chip regardless
 * of how often the host renders audio. */
void tms5220_tick(tms5220_t *t, unsigned cpu_cycles)
{
    t->cycle_acc += cpu_cycles;
    while (t->cycle_acc >= CYCLES_PER_SAMPLE) {
        t->cycle_acc -= CYCLES_PER_SAMPLE;
        int16_t v = synth_sample(t);
        if (!t->TALKD && v == 0 && t->ring_r == t->ring_w) continue;   /* idle: keep the ring empty */
        unsigned next = (t->ring_w + 1) & (RING_SIZE - 1);
        if (next == t->ring_r) t->ring_r = (t->ring_r + 1) & (RING_SIZE - 1);   /* overrun: drop oldest */
        t->ring[t->ring_w] = v;
        t->ring_w = next;
        t->dbg_written++;
    }
}

/* The mixer runs from the DAC clock and the synthesizer from the emulated CPU, so the ring
 * level jitters by up to one main-loop pass. Give each phrase this much of a head start
 * before draining it, otherwise the reads run ahead of the writes and every gap is a click. */
#ifndef RING_LEAD
#define RING_LEAD 512              /* 61 ms at 8.4 kHz; the ring holds 244 ms */
#endif

void tms5220_render(tms5220_t *t, int16_t *buf, int samples, int sample_rate)
{
    /* drain the 8.4 kHz ring with linear interpolation into the mixer rate */
    uint32_t step = (uint32_t)(((uint64_t)TMS_SAMPLE_RATE << 16) / (uint32_t)sample_rate);
    t->dbg_render_calls++; t->dbg_rate = (uint32_t)sample_rate;
    unsigned fill = (t->ring_w - t->ring_r) & (RING_SIZE - 1);
    if (fill == 0 && t->prev_out == 0 && t->last_sample == 0) {
        t->draining = 0;                            /* nothing queued and already silent */
        return;
    }
    if (!t->draining) {
        if (fill < RING_LEAD && t->TALKD) return;   /* let the phrase build a lead first */
        t->draining = 1;
    }
    for (int i = 0; i < samples; i++) {
        t->resample_acc += step;
        while (t->resample_acc >= 0x10000) {
            t->resample_acc -= 0x10000;
            t->prev_out = t->last_sample;
            if (t->ring_r != t->ring_w) {
                t->last_sample = t->ring[t->ring_r];
                t->ring_r = (t->ring_r + 1) & (RING_SIZE - 1);
                t->dbg_read++;
            } else {
                t->last_sample = 0;                 /* underrun: the synth hasn't produced this yet */
                if (t->TALKD) t->underruns++;
            }
        }
        int32_t frac = t->resample_acc;              /* 0..65535 */
        int32_t v = t->prev_out + (((int32_t)t->last_sample - t->prev_out) * frac >> 16);
        int32_t s = buf[i] + (v >> 1);              /* speech at half scale next to the POKEYs */
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        buf[i] = (int16_t)s;
    }
}
