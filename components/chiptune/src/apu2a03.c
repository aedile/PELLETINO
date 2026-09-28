/*
 * apu2a03.c - see apu2a03.h.
 *
 * Every channel is a down-counter clocked from the CPU clock that steps a small
 * sequencer when it runs out. Rendering walks each output sample's worth of CPU
 * cycles a run at a time and averages the level over it, which band-limits the
 * square waves well enough that high notes do not alias into a whine - the same
 * approach the AY core here takes.
 */
#include "apu2a03.h"
#include <string.h>

#define CPU_HZ        1789773u
#define FRAME_CYCLES  7457                  /* a quarter frame, about 240 Hz */

static const uint8_t len_tab[32] = {
    10, 254, 20,  2, 40,  4, 80,  6, 160,  8, 60, 10, 14, 12, 26, 14,
    12,  16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};
static const uint8_t  duty_tab[4] = { 0x02, 0x06, 0x1e, 0xf9 };       /* bit n = step n */
static const uint8_t  tri_tab[32] = {
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
     0,  1,  2,  3,  4,  5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};
static const uint16_t noise_tab[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};
static const uint16_t dmc_tab[16] = {
    428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54
};

/* ---- envelopes, sweeps, counters ------------------------------------------ */

static void env_clock(uint8_t *start, uint8_t *div, uint8_t *decay, uint8_t period, uint8_t loop)
{
    if (*start) { *start = 0; *decay = 15; *div = period; return; }
    if (*div) { (*div)--; return; }
    *div = period;
    if (*decay) (*decay)--;
    else if (loop) *decay = 15;
}

static int sweep_target(const apu_pulse_t *p, int ch)
{
    int change = p->period >> p->sweep_shift;
    /* the first pulse channel negates by one's complement, the second by two's */
    return p->sweep_neg ? p->period - change - (ch == 0) : p->period + change;
}

static void sweep_clock(apu_pulse_t *p, int ch)
{
    int target = sweep_target(p, ch);
    if (!p->sweep_div && p->sweep_en && p->sweep_shift && p->period >= 8 && target <= 0x7ff)
        p->period = (uint16_t)(target < 0 ? 0 : target);
    if (!p->sweep_div || p->sweep_reload) { p->sweep_div = p->sweep_period; p->sweep_reload = 0; }
    else p->sweep_div--;
}

static void quarter_frame(apu_t *a)
{
    for (int i = 0; i < 2; i++) {
        apu_pulse_t *p = &a->pulse[i];
        env_clock(&p->env_start, &p->env_div, &p->env_decay, p->vol, p->halt);
    }
    env_clock(&a->noise.env_start, &a->noise.env_div, &a->noise.env_decay, a->noise.vol, a->noise.halt);

    apu_tri_t *t = &a->tri;
    if (t->reload) t->linear = t->reload_val;
    else if (t->linear) t->linear--;
    if (!t->control) t->reload = 0;
}

static void half_frame(apu_t *a)
{
    for (int i = 0; i < 2; i++) {
        apu_pulse_t *p = &a->pulse[i];
        if (!p->halt && p->length) p->length--;
        sweep_clock(p, i);
    }
    if (!a->tri.control && a->tri.length) a->tri.length--;
    if (!a->noise.halt && a->noise.length) a->noise.length--;
}

static void frame_clock(apu_t *a)
{
    /* bit 0 = quarter frame, bit 1 = half frame */
    static const uint8_t four[4] = { 1, 3, 1, 3 };
    static const uint8_t five[5] = { 1, 3, 1, 0, 3 };
    uint8_t what = a->frame_mode ? five[a->frame_step] : four[a->frame_step];
    if (what & 1) quarter_frame(a);
    if (what & 2) half_frame(a);
    if (++a->frame_step >= (a->frame_mode ? 5 : 4)) a->frame_step = 0;
}

/* ---- the delta channel ---------------------------------------------------- */

static void dmc_restart(apu_dmc_t *d)
{
    d->addr = (uint16_t)(0xc000 | (d->addr_reg << 6));
    d->remaining = (uint16_t)((d->len_reg << 4) + 1);
}

static void dmc_fill(apu_t *a)
{
    apu_dmc_t *d = &a->dmc;
    if (d->buffer_full || !d->remaining) return;
    d->buffer = a->read ? a->read(d->addr) : 0;
    d->buffer_full = 1;
    d->addr = (uint16_t)(d->addr == 0xffff ? 0x8000 : d->addr + 1);
    if (--d->remaining == 0 && d->loop) dmc_restart(d);
}

static void dmc_clock(apu_t *a)
{
    apu_dmc_t *d = &a->dmc;
    if (!d->silence) {
        if (d->shift & 1) { if (d->level <= 125) d->level += 2; }
        else              { if (d->level >= 2)   d->level -= 2; }
    }
    d->shift >>= 1;
    if (d->bits) d->bits--;
    if (!d->bits) {
        d->bits = 8;
        if (d->buffer_full) { d->silence = 0; d->shift = d->buffer; d->buffer_full = 0; dmc_fill(a); }
        else d->silence = 1;
    }
}

/* ---- registers ------------------------------------------------------------ */

void apu_reset(apu_t *a, apu_read_fn read)
{
    memset(a, 0, sizeof *a);
    a->read = read;
    a->noise.lfsr = 1;
    a->dmc.bits = 8;
    a->dmc.silence = 1;
    a->frame_timer = FRAME_CYCLES;
}

void apu_write(apu_t *a, uint16_t addr, uint8_t v)
{
    if (addr >= 0x4000 && addr <= 0x4007) {
        apu_pulse_t *p = &a->pulse[(addr >> 2) & 1];
        switch (addr & 3) {
        case 0: p->duty = v >> 6; p->halt = (v >> 5) & 1; p->constant = (v >> 4) & 1; p->vol = v & 15; break;
        case 1: p->sweep_en = v >> 7; p->sweep_period = (v >> 4) & 7; p->sweep_neg = (v >> 3) & 1;
                p->sweep_shift = v & 7; p->sweep_reload = 1; break;
        case 2: p->period = (uint16_t)((p->period & 0x700) | v); break;
        case 3: p->period = (uint16_t)((p->period & 0x0ff) | ((v & 7) << 8));
                if (p->enabled) p->length = len_tab[v >> 3];
                p->seq = 0; p->env_start = 1; break;
        }
        return;
    }
    switch (addr) {
    case 0x4008: a->tri.control = v >> 7; a->tri.reload_val = v & 0x7f; break;
    case 0x400a: a->tri.period = (uint16_t)((a->tri.period & 0x700) | v); break;
    case 0x400b: a->tri.period = (uint16_t)((a->tri.period & 0x0ff) | ((v & 7) << 8));
                 if (a->tri.enabled) a->tri.length = len_tab[v >> 3];
                 a->tri.reload = 1; break;
    case 0x400c: a->noise.halt = (v >> 5) & 1; a->noise.constant = (v >> 4) & 1; a->noise.vol = v & 15; break;
    case 0x400e: a->noise.mode = v >> 7; a->noise.period_idx = v & 15; break;
    case 0x400f: if (a->noise.enabled) a->noise.length = len_tab[v >> 3];
                 a->noise.env_start = 1; break;
    case 0x4010: a->dmc.loop = (v >> 6) & 1; a->dmc.rate_idx = v & 15; break;
    case 0x4011: a->dmc.level = v & 0x7f; break;
    case 0x4012: a->dmc.addr_reg = v; break;
    case 0x4013: a->dmc.len_reg = v; break;
    case 0x4015:
        for (int i = 0; i < 2; i++) {
            a->pulse[i].enabled = (v >> i) & 1;
            if (!a->pulse[i].enabled) a->pulse[i].length = 0;
        }
        a->tri.enabled = (v >> 2) & 1;   if (!a->tri.enabled)   a->tri.length = 0;
        a->noise.enabled = (v >> 3) & 1; if (!a->noise.enabled) a->noise.length = 0;
        if (!(v & 0x10)) a->dmc.remaining = 0;
        else if (!a->dmc.remaining) { dmc_restart(&a->dmc); dmc_fill(a); }
        break;
    case 0x4017:
        a->frame_mode = v >> 7;
        a->frame_step = 0;
        a->frame_timer = FRAME_CYCLES;
        if (a->frame_mode) { quarter_frame(a); half_frame(a); }
        break;
    default: break;
    }
}

uint8_t apu_status(const apu_t *a)
{
    return (uint8_t)((a->pulse[0].length ? 1 : 0) | (a->pulse[1].length ? 2 : 0) |
                     (a->tri.length ? 4 : 0) | (a->noise.length ? 8 : 0) |
                     (a->dmc.remaining ? 16 : 0));
}

/* ---- rendering ------------------------------------------------------------ */

static int pulse_level(const apu_pulse_t *p, int ch)
{
    if (!p->length || p->period < 8) return 0;
    if (!p->sweep_neg && sweep_target(p, ch) > 0x7ff) return 0;
    if (!((duty_tab[p->duty] >> p->seq) & 1)) return 0;
    return p->constant ? p->vol : p->env_decay;
}

/* each run_*() returns the sum of (level x cycles) across n CPU cycles */
static int32_t run_pulse(apu_pulse_t *p, int ch, int n)
{
    if (!p->length || p->period < 8) return 0;          /* silent: its phase does not matter */
    int32_t acc = 0;
    int step = 2 * (p->period + 1), level = pulse_level(p, ch);
    while (n > 0) {
        if (p->timer <= 0) p->timer = step;
        int run = n < p->timer ? n : p->timer;
        acc += level * run;
        p->timer -= run; n -= run;
        if (!p->timer) { p->seq = (p->seq + 1) & 7; level = pulse_level(p, ch); }
    }
    return acc;
}

static int32_t run_tri(apu_tri_t *t, int n)
{
    /* stopped, or so high it is ultrasonic: hold the level, which the DC filter removes */
    if (!t->length || !t->linear || t->period < 2) return tri_tab[t->seq] * n;
    int32_t acc = 0;
    int step = t->period + 1;
    while (n > 0) {
        if (t->timer <= 0) t->timer = step;
        int run = n < t->timer ? n : t->timer;
        acc += tri_tab[t->seq] * run;
        t->timer -= run; n -= run;
        if (!t->timer) t->seq = (t->seq + 1) & 31;
    }
    return acc;
}

static int32_t run_noise(apu_noise_t *z, int n)
{
    if (!z->length) return 0;
    int32_t acc = 0;
    int step = noise_tab[z->period_idx], vol = z->constant ? z->vol : z->env_decay;
    while (n > 0) {
        if (z->timer <= 0) z->timer = step;
        int run = n < z->timer ? n : z->timer;
        if (!(z->lfsr & 1)) acc += vol * run;
        z->timer -= run; n -= run;
        if (!z->timer) {
            uint16_t fb = (uint16_t)((z->lfsr ^ (z->lfsr >> (z->mode ? 6 : 1))) & 1);
            z->lfsr = (uint16_t)((z->lfsr >> 1) | (fb << 14));
        }
    }
    return acc;
}

static int32_t run_dmc(apu_t *a, int n)
{
    apu_dmc_t *d = &a->dmc;
    int32_t acc = 0;
    int step = dmc_tab[d->rate_idx];
    while (n > 0) {
        if (d->timer <= 0) d->timer = step;
        int run = n < d->timer ? n : d->timer;
        acc += d->level * run;
        d->timer -= run; n -= run;
        if (!d->timer) dmc_clock(a);
    }
    return acc;
}

void apu_render(apu_t *a, int16_t *buf, int samples, int rate)
{
    uint32_t step = (uint32_t)(((uint64_t)CPU_HZ << 16) / (uint32_t)rate);
    for (int i = 0; i < samples; i++) {
        a->acc += step;
        int n = (int)(a->acc >> 16);
        a->acc &= 0xffff;
        if (n < 1) n = 1;

        a->frame_timer -= n;
        while (a->frame_timer <= 0) { a->frame_timer += FRAME_CYCLES; frame_clock(a); }

        int32_t p = run_pulse(&a->pulse[0], 0, n) + run_pulse(&a->pulse[1], 1, n);
        int32_t t = run_tri(&a->tri, n);
        int32_t z = run_noise(&a->noise, n);
        int32_t d = run_dmc(a, n);

        /* the chip's mixer, linearised: weights in units of 1e-5 of full scale */
        int32_t out = (p * 752 + t * 851 + z * 494 + d * 335) / n;
        out = out * 21 / 64;

        /* the output only swings upward from zero; take the DC off as the console's
         * coupling capacitor did */
        a->dc += (out - a->dc) >> 8;
        out -= a->dc;
        buf[i] = (int16_t)(out > 32767 ? 32767 : out < -32768 ? -32768 : out);
    }
}
