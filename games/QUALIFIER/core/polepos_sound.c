/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * polepos_sound.c - 8-voice Namco WSG, engine sample generator, 52XX speech samples,
 * 54XX noise (high-level). Follows MAME's namco.cpp (polepos_wsg), polepos_a.cpp and
 * the pre-MCU 52XX model.
 */
#include "polepos_internal.h"
#include <string.h>
#include <math.h>

static const pp_roms_t *R;
static int sound_on;

/* ---- WSG ---- */
static uint8_t regs[0x40];
static uint32_t vcount[8];
#define WSG_RATE 192000
#define WSG_FRAC 17

void pp_wsg_write(int reg, uint8_t d) { regs[reg & 0x3f] = d; }
uint8_t pp_wsg_read(int reg) { return regs[reg & 0x3f]; }
void pp_sound_enable(int on) { sound_on = on; }

static void wsg_render(int16_t *buf, int samples, int rate)
{
    if (!sound_on) return;
    uint32_t step_scale = (uint32_t)(((uint64_t)WSG_RATE << 8) / (uint32_t)rate);   /* 24.8 */
    for (int ch = 0; ch < 8; ch++) {
        uint32_t freq = regs[ch * 4 + 0] | ((uint32_t)regs[ch * 4 + 1] << 8);
        int wave = regs[ch * 4 + 0x23] & 7;
        int fl = regs[ch * 4 + 3] >> 4, fr = regs[ch * 4 + 3] & 0x0f;
        int rl = regs[ch * 4 + 0x23] >> 4, rr = regs[ch * 4 + 2] >> 4;
        if (regs[ch * 4 + 0x23] & 8) fl = fr = rl = rr = 0;
        int vol = fl + fr + (rl + rr) / 2;
        if (!vol || !freq) continue;
        const uint8_t *w = R->wave + wave * 32;
        uint32_t step = (uint32_t)(((uint64_t)freq * step_scale) >> 8);
        uint32_t c = vcount[ch];
        for (int i = 0; i < samples; i++) {
            int s = ((int)(w[(c >> WSG_FRAC) & 0x1f] & 0x0f) - 8) * vol;   /* +-8 * up to 60 */
            int32_t v = buf[i] + s * 12;
            if (v > 32767) v = 32767; if (v < -32768) v = -32768;
            buf[i] = (int16_t)v;
            c += step;
        }
        vcount[ch] = c;
    }
}

/* ---- biquad (MAME filter2_context), fixed point: coefficients Q28, signal Q20 ----
 * The ESP32-C6 has no FPU, so per-sample work is integer only; the setup math (once per
 * sample-rate change) may use doubles. */
#define BQ_COEF 28
#define SIG_Q   20
typedef struct { int32_t a1, a2, b0, b1, b2; int32_t x1, x2, y1, y2; } biquad_t;
static void biquad_setup(biquad_t *f, int type, double fc, double d, double gain, int rate)
{
    double two_over_T = 2.0 * rate, two_over_T2 = two_over_T * two_over_T;
    double w = rate * 2.0 * tan(M_PI * fc / rate), w2 = w * w;
    double den = two_over_T2 + d * w * two_over_T + w2;
    double a1 = 2.0 * (-two_over_T2 + w2) / den;
    double a2 = (two_over_T2 - d * w * two_over_T + w2) / den;
    double b0, b1, b2;
    if (type == 0) { b0 = b2 = w2 / den; b1 = 2.0 * b0; }                       /* lowpass */
    else if (type == 2) { b0 = d * w * two_over_T / den; b1 = 0; b2 = -b0; }    /* bandpass */
    else { b0 = b2 = two_over_T2 / den; b1 = -2.0 * b0; }                        /* highpass */
    b0 *= gain; b1 *= gain; b2 *= gain;
    const double k = (double)(1 << BQ_COEF);
    f->a1 = (int32_t)(a1 * k); f->a2 = (int32_t)(a2 * k);
    f->b0 = (int32_t)(b0 * k); f->b1 = (int32_t)(b1 * k); f->b2 = (int32_t)(b2 * k);
    f->x1 = f->x2 = f->y1 = f->y2 = 0;
}
static inline int32_t biquad_step(biquad_t *f, int32_t x)
{
    int64_t acc = -(int64_t)f->a1 * f->y1 - (int64_t)f->a2 * f->y2 + (int64_t)f->b0 * x + (int64_t)f->b1 * f->x1 + (int64_t)f->b2 * f->x2;
    int32_t y = (int32_t)(acc >> BQ_COEF);
    f->x2 = f->x1; f->x1 = x; f->y2 = f->y1; f->y1 = y;
    return y;
}
static void opamp_bandpass(biquad_t *f, double r1, double r2, double r3, double c1, double c2, int rate)
{
    double gain, r_in;
    if (r2 == 0) { gain = 1; r_in = r1; } else { gain = r2 / (r1 + r2); r_in = 1.0 / (1.0 / r1 + 1.0 / r2); }
    double fc = 1.0 / (2 * M_PI * sqrt(r_in * r3 * c1 * c2));
    double d = (c1 + c2) / sqrt(r3 / r_in * c1 * c2);
    gain *= -r3 / r_in * c2 / (c1 + c2);
    biquad_setup(f, 2, fc, d, gain, rate);
}

/* ---- engine ---- */
static uint8_t eng_msb, eng_lsb; static int eng_enable;
static uint32_t eng_pos;
static biquad_t eng_f[3]; static int eng_rate;
static const double volume_table[8] = {
    (1.0/(1.0/1000+1.0/250)*3 + 2200) / 10000, (1.0/(1.0/1000+1.0/250)*2 + 1000 + 2200) / 10000,
    (1.0/(1.0/1000+1.0/250)*2 + 2200 + 2200) / 10000, (1.0/(1.0/1000+1.0/250) + 2200 + 1000 + 2200) / 10000,
    (4700 + 1.0/(1.0/1000+1.0/250)*2 + 2200) / 10000, (4700 + 1.0/(1.0/1000+1.0/250) + 1000 + 2200) / 10000,
    (4700 + 2200 + 1.0/(1.0/1000+1.0/250) + 2200) / 10000, (4700 + 2200 + 1000 + 2200) / 10000 };
/* per-filter output weights: r_filt_total / 2 / r_filt_out[k] * 20000, with r_filt_total = 4700 || 7500 || 10000 */
static const int32_t eng_out_w[3] = { 4752, 2978, 2234 };
static int32_t eng_xtab[8][256];      /* (3.4/255 * sample - 2) * volume[slot], Q20 */
void pp_engine_lsb(uint8_t d) { eng_lsb = d & 62; eng_enable = d & 1; }
void pp_engine_msb(uint8_t d) { eng_msb = d & 63; }
static void engine_setup(int rate)
{
    eng_rate = rate;
    opamp_bandpass(&eng_f[0], 220e3, 33e3, 390e3, 0.01e-6, 0.01e-6, rate);
    opamp_bandpass(&eng_f[1], 150e3, 22e3, 330e3, 0.0047e-6, 0.0047e-6, rate);
    biquad_setup(&eng_f[2], 1, 950, 1.0 / 0.707, 1, rate);
    for (int slot = 0; slot < 8; slot++)
        for (int v = 0; v < 256; v++)
            eng_xtab[slot][v] = (int32_t)((3.4 / 255 * v - 2) * volume_table[slot] * (1 << SIG_Q));
}
static void engine_render(int16_t *buf, int samples, int rate)
{
    if (rate != eng_rate) engine_setup(rate);
    if (!eng_enable) return;
    uint32_t clock = (uint32_t)(((uint64_t)(3072000 / 16) * ((eng_msb + 1) * 64 + eng_lsb + 1)) / (64 * 64));
    uint32_t step = (uint32_t)(((uint64_t)clock << 12) / (uint32_t)rate);
    int slot = (eng_msb >> 3) & 7;
    const int32_t *xtab = eng_xtab[slot];
    const uint8_t *base = R->engine + slot * 0x800;
    const int32_t ymax = (int32_t)(1.5 * (1 << SIG_Q)), ymin = -2 * (1 << SIG_Q);
    for (int i = 0; i < samples; i++) {
        int32_t x = xtab[base[(eng_pos >> 12) & 0x7ff]];
        int64_t out = 0;
        for (int k = 0; k < 3; k++) {
            int32_t y = biquad_step(&eng_f[k], x);
            if (y > ymax) y = ymax; if (y < ymin) y = ymin;
            out += (int64_t)y * eng_out_w[k];
        }
        int32_t v = buf[i] + (int32_t)(out >> SIG_Q);
        if (v > 32767) v = 32767; if (v < -32768) v = -32768;
        buf[i] = (int16_t)v;
        eng_pos += step;
    }
}

/* ---- 52XX speech samples ---- */
static int n52_start, n52_end, n52_length, n52_pos; static uint32_t n52_cycle, n52_step; static int n52_rate;   /* cycle/step: 16.16 */
static biquad_t n52_hp, n52_lp;
void pp_n52_write(uint8_t d)
{
    d &= 0x0f;
    if (!d) return;
    n52_start = R->voice[d - 1] + (R->voice[d - 1 + 0x10] << 8);
    n52_end = R->voice[d] + (R->voice[d + 0x10] << 8);
    if (n52_end >= 0x6000) n52_end = 0x6000;
    n52_length = (n52_end - n52_start) * 2;
    n52_pos = 0; n52_cycle = 0;
}
static void n52_render(int16_t *buf, int samples, int rate)
{
    if (rate != n52_rate) {
        n52_rate = rate; n52_step = (uint32_t)(((uint64_t)(1536000 / 384) << 16) / (uint32_t)rate);
        biquad_setup(&n52_hp, 1, 100, 1.0 / 0.3, 1, rate);
        biquad_setup(&n52_lp, 0, 1200, 1.0 / 0.8, 0.5, rate);
    }
    if (n52_start >= n52_end) return;
    for (int i = 0; i < samples; i++) {
        n52_cycle += n52_step;
        n52_pos += n52_cycle >> 16; n52_cycle &= 0xffff;
        if (n52_pos > n52_length) { n52_start = n52_end = n52_length = n52_pos = 0; return; }
        int rom_pos = n52_start + (n52_pos >> 1);
        int s4 = (((n52_pos & 1) ? R->voice[rom_pos] >> 4 : R->voice[rom_pos]) & 0x0f) - 8;
        int32_t y = biquad_step(&n52_lp, biquad_step(&n52_hp, s4 << SIG_Q));
        int32_t v = buf[i] + (int32_t)(((int64_t)y * 0x0fff) >> SIG_Q);
        if (v > 32767) v = 32767; if (v < -32768) v = -32768;
        buf[i] = (int16_t)v;
    }
}

/* ---- 54XX noise (high level), Q15 envelopes ---- */
typedef struct { int active; int32_t env, decay, lp, alpha, gain; } noise_t;   /* env/decay/alpha Q15, gain Q8, lp = sample scale */
static noise_t nz[3]; static int n54_param_left; static int n54_pending[3]; static uint32_t rng = 0x12345678;
void pp_n54_write(uint8_t d)
{
    if (n54_param_left) { n54_param_left--; return; }
    switch (d >> 4) {
        case 1: n54_pending[0] = 1; break;
        case 2: n54_pending[1] = 1; break;
        case 3: case 4: n54_param_left = 4; break;
        case 5: n54_pending[2] = 1; break;
        case 6: n54_param_left = 5; break;
        case 7: nz[2].gain = ((d & 0x0f) * 256) / 15; break;
        default: break;
    }
}
static void trigger(int w, int decay_ms, int cutoff_hz, int32_t gain_q8, int rate)
{
    nz[w].active = 1; nz[w].env = 32767;
    nz[w].decay = 32768 - (int32_t)((32768LL * 1000) / ((int64_t)decay_ms * rate));
    nz[w].alpha = (int32_t)((32768LL * rate) / ((int64_t)(6.2832 * cutoff_hz) + rate));
    nz[w].gain = gain_q8;
}
static void n54_render(int16_t *buf, int samples, int rate)
{
    if (n54_pending[0]) { trigger(0, 800, 700, 230, rate); n54_pending[0] = 0; }
    if (n54_pending[1]) { trigger(1, 300, 2500, 154, rate); n54_pending[1] = 0; }
    if (n54_pending[2]) { trigger(2, 1500, 1200, nz[2].gain > 0 ? nz[2].gain : 128, rate); n54_pending[2] = 0; }
    if (!nz[0].active && !nz[1].active && !nz[2].active) return;
    for (int i = 0; i < samples; i++) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        int32_t white = (int32_t)(rng & 0xffff) - 32768, v = 0;
        for (int k = 0; k < 3; k++) {
            noise_t *n = &nz[k];
            if (!n->active) continue;
            n->lp += (n->alpha * (white - n->lp)) >> 15;
            v += (((n->lp * n->env) >> 15) * n->gain) >> 8;
            n->env = (n->env * n->decay) >> 15;
            if (n->env < 66) n->active = 0;
        }
        int32_t s = buf[i] + ((v * 12000) >> 15);
        if (s > 32767) s = 32767; if (s < -32768) s = -32768;
        buf[i] = (int16_t)s;
    }
}

void pp_sound_init(const pp_roms_t *r) { R = r; pp_sound_reset(); }
void pp_sound_reset(void)
{
    memset(regs, 0, sizeof(regs)); memset(vcount, 0, sizeof(vcount)); sound_on = 0;
    eng_msb = eng_lsb = 0; eng_enable = 0; eng_pos = 0;
    n52_start = n52_end = n52_length = n52_pos = 0;
    memset(nz, 0, sizeof(nz)); n54_param_left = 0;
}
void pp_sound_render(int16_t *buf, int samples, int rate)
{
    memset(buf, 0, samples * sizeof(int16_t));
    wsg_render(buf, samples, rate);
    engine_render(buf, samples, rate);
    n52_render(buf, samples, rate);
    n54_render(buf, samples, rate);
}
