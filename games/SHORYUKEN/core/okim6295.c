/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * okim6295.c - OKI MSM6295.
 *
 * The ROM begins with a table of eight-byte entries, one a phrase: three bytes of start
 * address and three of end. A phrase is started by two writes - the phrase number with the
 * top bit set, then which voices in the high nibble and how much quieter in the low - and
 * stopped by one write with the top bit clear and the voices in bits 3 to 6. Reading gives
 * which voices are playing.
 *
 * The samples are OKI's ADPCM: four bits each, high nibble first, a step size that follows
 * a table of 49 sizes each a tenth larger than the last, and a 12-bit result.
 *
 * The ROM may not be in memory. On the device there is not the address space to map it, so
 * each voice keeps a window of 64 bytes of it and refills the window as it plays through.
 *
 * A voice is played at the chip's rate and heard at the output's: each output sample is
 * whatever the voice's last decoded sample was. At 7,576 samples a second going to 11,025
 * or more there is nothing to be gained by doing better.
 */
#include "okim6295.h"
#include <string.h>

#define WINDOW 64

typedef struct {
    uint32_t pos, end;               /* in nibbles */
    int32_t signal, step;
    int32_t out;                     /* the sample being held, with the volume applied */
    uint8_t playing, volume;
    uint32_t win_at;                 /* the byte offset the window begins at */
    uint8_t win[WINDOW];
} voice_t;

static voice_t voice[4];
static const uint8_t *rom;
static void (*rom_read)(uint32_t offset, uint8_t *dst, uint32_t len);
static uint32_t rom_mask;
static int chip_clock, out_rate;
static uint32_t acc, acc_step;       /* 16.16 chip samples per output sample */
static int pending = -1;

static int16_t step_size[49];
static const int8_t step_move[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
/* each step is 3 dB: 32 is full */
static const uint8_t volumes[16] = { 32, 22, 16, 11, 8, 6, 4, 3, 2, 0, 0, 0, 0, 0, 0, 0 };

static void set_rate(int high)
{
    int rate = chip_clock / (high ? 132 : 165);
    acc_step = (uint32_t)(((uint64_t)rate << 16) / (uint32_t)out_rate);
}

void okim6295_init(const uint8_t *r, void (*read)(uint32_t, uint8_t *, uint32_t), uint32_t size, int clock, int rate)
{
    rom = r; rom_read = read; rom_mask = size - 1; chip_clock = clock; out_rate = rate;
    double s = 16.0;
    for (int i = 0; i < 49; i++) { step_size[i] = (int16_t)s; s *= 1.1; }
    set_rate(1);
    okim6295_reset();
}

void okim6295_reset(void)
{
    memset(voice, 0, sizeof(voice));
    pending = -1; acc = 0;
}

void okim6295_set_pin7(int high) { set_rate(high); }

uint8_t okim6295_read(void)
{
    uint8_t v = 0xf0;
    for (int i = 0; i < 4; i++) if (voice[i].playing) v |= (uint8_t)(1 << i);
    return v;
}

void okim6295_write(uint8_t v)
{
    if (pending >= 0) {
        uint8_t e[8];
        if (rom) memcpy(e, &rom[((uint32_t)pending * 8) & rom_mask], 8);
        else rom_read(((uint32_t)pending * 8) & rom_mask, e, 8);
        uint32_t start = (((uint32_t)e[0] << 16) | ((uint32_t)e[1] << 8) | e[2]) & 0x3ffff;
        uint32_t stop = (((uint32_t)e[3] << 16) | ((uint32_t)e[4] << 8) | e[5]) & 0x3ffff;
        for (int i = 0; i < 4; i++) {
            if (!((v >> (4 + i)) & 1)) continue;
            voice_t *vc = &voice[i];
            if (vc->playing || start >= stop) continue;      /* a voice that is busy is left alone */
            vc->pos = start * 2; vc->end = (stop + 1) * 2;
            vc->signal = 0; vc->step = 0; vc->out = 0;
            vc->volume = volumes[v & 15];
            vc->win_at = 0xffffffff;
            vc->playing = 1;
        }
        pending = -1;
    } else if (v & 0x80) {
        pending = v & 0x7f;
    } else {
        for (int i = 0; i < 4; i++) if ((v >> (3 + i)) & 1) voice[i].playing = 0;
    }
}

static inline void decode(voice_t *vc)
{
    uint32_t at = (vc->pos >> 1) & rom_mask;
    unsigned b;
    if (rom) b = rom[at];
    else {
        if (at - vc->win_at >= WINDOW) {
            vc->win_at = at;
            rom_read(at, vc->win, at + WINDOW <= rom_mask + 1 ? WINDOW : rom_mask + 1 - at);
        }
        b = vc->win[at - vc->win_at];
    }
    unsigned n = (vc->pos & 1) ? (b & 15) : (b >> 4);
    int s = step_size[vc->step];
    int d = s >> 3;
    if (n & 1) d += s >> 2;
    if (n & 2) d += s >> 1;
    if (n & 4) d += s;
    vc->signal += (n & 8) ? -d : d;
    if (vc->signal > 2047) vc->signal = 2047; else if (vc->signal < -2048) vc->signal = -2048;
    vc->step += step_move[n & 7];
    if (vc->step < 0) vc->step = 0; else if (vc->step > 48) vc->step = 48;
    vc->out = (vc->signal * vc->volume) >> 1;               /* 12 bits to 16 */
    if (++vc->pos >= vc->end) { vc->playing = 0; vc->out = 0; }
}

void okim6295_render(int32_t *mix, int samples)
{
    if (!(voice[0].playing | voice[1].playing | voice[2].playing | voice[3].playing)) return;
    for (int k = 0; k < samples; k++) {
        acc += acc_step;
        for (unsigned n = acc >> 16; n; n--)
            for (int i = 0; i < 4; i++) if (voice[i].playing) decode(&voice[i]);
        acc &= 0xffff;
        mix[k] += voice[0].out + voice[1].out + voice[2].out + voice[3].out;
    }
}
