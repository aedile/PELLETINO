/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; the chip's behaviour referenced against ymfm (BSD-3-Clause), whose
 * tables of the chip's ROM contents are in ym2151_tables.h under that licence.
 * See THIRD_PARTY_NOTICES.md. */
/*
 * ym2151.c - Yamaha YM2151, synthesised at the rate it is played at.
 *
 * The chip makes a sample every 64 clocks, 55,930 a second. This does not: it makes them at
 * the output rate, by stepping each operator's phase further per sample, so the cost is
 * proportional to SOUND_RATE rather than fixed at the chip's. What that gives up is
 * everything above half the output rate, which folds back instead of being filtered out.
 *
 * What is kept exact is time. The envelope generator is clocked as often as the chip
 * clocks it (every third sample of its own), eight output samples' worth at a go, so notes
 * are as long as they should be at any rate. The timers run on the chip's clock, fed from
 * the Z80's cycle count, and have nothing to do with the sound at all.
 *
 * A channel none of whose carriers can be heard is skipped. With YM_QUALITY 0 there is
 * also no LFO and no noise, and the operators are computed at half the output rate with
 * the samples between filled in by a straight line.
 */
#include "ym2151.h"
#include "ym2151_tables.h"
#include "knobs.h"
#include <string.h>

enum { EG_ATTACK, EG_DECAY, EG_SUSTAIN, EG_RELEASE, EG_OFF };

typedef struct {
    uint32_t phase, step;            /* 32 bits to the cycle, per synthesised sample */
    uint32_t att;                    /* what is added to the sine's attenuation: (envelope + level + tremolo) << 2 */
    uint16_t env;                    /* 0 loud .. 0x3ff silent */
    uint16_t sustain;
    uint16_t tl;                     /* total level << 3 */
    uint8_t state, key;
    uint8_t rate[4];
    uint16_t eg_mask;                /* the envelope moves when the clock has none of these bits set */
    uint8_t eg_level, index;         /* how many bits that is (16: never), and which of the 32 this is */
    uint8_t dt1, mul, ks, ar, d1r, dt2, d2r, sl, rr, am_on;
} op_t;

typedef struct {
    op_t op[4];                      /* in the order they are wired: M1, C1, M2, C2 */
    int32_t fb[2];
    uint8_t alg, fb_level, pan, kc, kf, pms, ams;
    uint8_t dynamic;                 /* the LFO moves its pitch */
} ch_t;

static ch_t chan[8];
static uint8_t reg_addr;
static uint8_t status, irq_enable, timer_run;
static uint16_t timer_a_val;
static uint8_t timer_b_val;
static int32_t timer_a_left, timer_b_left;       /* in clocks */

static uint8_t lfo_rate, lfo_wave, lfo_amd, lfo_pmd, lfo_hold;
static uint32_t lfo_counter, lfo_step;           /* lfo_step: per block of BLOCK samples */
static int32_t lfo_am, lfo_pm;
static uint8_t noise_on, noise_freq;
static uint32_t noise_lfsr = 1, noise_acc, noise_step;
static int32_t noise_out;

static uint32_t eg_counter, eg_acc, eg_per_block;  /* eg_per_block: envelope clocks per BLOCK samples, 16.16 */
static uint32_t rate_ratio;                        /* chip samples per synthesised sample, 12.20 */
static int synth_div;                              /* 1, or 2 when the operators run at half rate */
static int32_t held;                               /* half rate: the last computed sample */
static int have_held;

static int16_t sine[1024];           /* attenuation 4.8, and the sign in bit 15 */
/* the two tables the inner loops use, copied out of flash: a constant is read through the
 * flash cache, and these are read tens of thousands of times a second */
static uint16_t power[256];
static uint32_t eg_increment[64];

#define BLOCK 8                      /* synthesised samples between envelope updates */
static const uint8_t slot_to_op[4] = { 0, 2, 1, 3 };      /* registers go M1 M2 C1 C2 */

#ifdef SOUND_PROFILE
#include "esp_cpu.h"
uint32_t ym_prof[4];                 /* cycles: lfo, channels, envelope; and channels actually computed */
#define YT() esp_cpu_get_cycle_count()
#else
#define YT() 0
#endif

/* ---- what the registers mean ---- */

static uint32_t phase_step(unsigned kc, unsigned kf, int delta)
{
    int block = (kc >> 4) & 7;
    unsigned note = kc & 15;
    int f = (int)((note - (note >> 2)) * 64 + kf) + delta;   /* the 16 notes of an octave are 12 with gaps */
    while (f < 0) { f += 768; block--; }
    while (f >= 768) { f -= 768; block++; }
    if (block < 0) return ym_phase_step[0] >> 7;
    if (block > 7) return ym_phase_step[767];
    return ym_phase_step[f] >> (7 - block);
}

static void op_frequency(ch_t *c, op_t *o, int pm)
{
    static const int16_t dt2_delta[4] = { 0, 384, 500, 608 };      /* 600, 781 and 950 cents, in 64ths of a semitone */
    unsigned keycode = c->kc >> 2;
    int delta = dt2_delta[o->dt2];
    if (c->pms && pm) delta += c->pms < 6 ? pm >> (6 - c->pms) : pm * (1 << (c->pms - 5));
    int32_t s = (int32_t)phase_step(c->kc, c->kf, delta);
    int d = ym_detune[keycode][o->dt1 & 3];
    s += (o->dt1 & 4) ? -d : d;
    if (s < 0) s = 0;
    s = o->mul ? s * o->mul : s >> 1;
    /* the chip's phase is 20 bits to the cycle; ours is 32, and steps rate_ratio times as far */
    o->step = (uint32_t)(((uint64_t)(uint32_t)s * rate_ratio) >> 8);
}

/* An envelope at rate r moves on one clock in 2^(11 - r/4), and on every clock from rate 44
 * up. Which clocks those are is a matter of which low bits of the counter are clear. */
/* eg_turn[k]: the operators whose envelope moves when the clock's low k bits are zero */
static uint32_t eg_turn[12];

static inline void op_eg_mask(op_t *o)
{
    unsigned level;
    /* off, or silent and not attacking: nothing will move it until the next key-on */
    if (o->state == EG_OFF || (o->env >= 0x3ff && o->state != EG_ATTACK)) level = 16;
    else {
        unsigned shift = o->rate[o->state] >> 2;
        level = shift < 11 ? 11 - shift : 0;
    }
    if (level == o->eg_level) return;
    if (o->eg_level < 12) eg_turn[o->eg_level] &= ~(1u << o->index);
    if (level < 12) eg_turn[level] |= 1u << o->index;
    o->eg_level = (uint8_t)level;
    o->eg_mask = level < 16 ? (uint16_t)((1u << level) - 1) : 0xffff;
}

/* attack ends when the envelope reaches the top, decay when it reaches the sustain level */
static inline void op_eg_state(op_t *o)
{
    if (o->state == EG_ATTACK && o->env == 0) o->state = EG_DECAY;
    if (o->state == EG_DECAY && o->env >= o->sustain) o->state = EG_SUSTAIN;
    op_eg_mask(o);
}

static unsigned eff_rate(unsigned raw, unsigned ksr) { unsigned r = raw ? raw + ksr : 0; return r > 63 ? 63 : r; }

static void op_rates(ch_t *c, op_t *o)
{
    unsigned ksr = (c->kc >> 2) >> (3 - o->ks);
    o->rate[EG_ATTACK] = (uint8_t)eff_rate(o->ar * 2, ksr);
    o->rate[EG_DECAY] = (uint8_t)eff_rate(o->d1r * 2, ksr);
    o->rate[EG_SUSTAIN] = (uint8_t)eff_rate(o->d2r * 2, ksr);
    o->rate[EG_RELEASE] = (uint8_t)eff_rate(o->rr * 4 + 2, ksr);
    o->sustain = (uint16_t)((o->sl == 15 ? 31 : o->sl) << 5);
    op_eg_mask(o);
}

static inline void op_level(const ch_t *c, op_t *o)
{
    unsigned a = o->env + o->tl;
    if (o->am_on && c->ams) a += (unsigned)lfo_am << (c->ams - 1);
    if (a > 0x3ff) a = 0x3ff;
    o->att = a << 2;
}

static void chan_refresh(ch_t *c)
{
    c->dynamic = YM_QUALITY && c->pms && lfo_pmd;
    for (int i = 0; i < 4; i++) { op_frequency(c, &c->op[i], c->dynamic ? lfo_pm : 0); op_rates(c, &c->op[i]); op_level(c, &c->op[i]); }
}

static void key(ch_t *c, op_t *o, int on)
{
    if (on == o->key) return;
    o->key = (uint8_t)on;
    if (on) {
        o->state = EG_ATTACK;
        o->phase = 0;
        if (o->rate[EG_ATTACK] >= 62) o->env = 0;
    } else if (o->state != EG_OFF) {
        o->state = EG_RELEASE;
    }
    op_eg_mask(o);
    op_level(c, o);
}

#if YM_QUALITY
static void lfo_set_rate(void);
#else
#define lfo_set_rate()
#endif

static void timers_load(unsigned v)
{
    if ((v & 1) && !(timer_run & 1)) timer_a_left = 64 * (1024 - timer_a_val);
    if ((v & 2) && !(timer_run & 2)) timer_b_left = 1024 * (256 - timer_b_val);
    timer_run = v & 3;
    irq_enable = (v >> 2) & 3;
    if (v & 0x10) status &= ~1;
    if (v & 0x20) status &= ~2;
}

static void write_reg(unsigned r, unsigned v)
{
    if (r < 0x20) {
        switch (r) {
        case 0x01: lfo_hold = (v >> 1) & 1; if (lfo_hold) lfo_counter = 0; break;
        case 0x08: {
            ch_t *c = &chan[v & 7];
            for (int i = 0; i < 4; i++) key(c, &c->op[i], (v >> (3 + i)) & 1);
            break;
        }
        case 0x0f: noise_on = (v >> 7) & 1; noise_freq = v & 0x1f; break;
        case 0x10: timer_a_val = (uint16_t)((timer_a_val & 3) | (v << 2)); break;
        case 0x11: timer_a_val = (uint16_t)((timer_a_val & 0x3fc) | (v & 3)); break;
        case 0x12: timer_b_val = (uint8_t)v; break;
        case 0x14: timers_load(v); break;
        case 0x18: lfo_rate = (uint8_t)v; lfo_set_rate(); break;
        case 0x19:
            if (v & 0x80) lfo_pmd = v & 0x7f; else lfo_amd = v & 0x7f;
            for (int i = 0; i < 8; i++) chan_refresh(&chan[i]);
            break;
        case 0x1b: lfo_wave = v & 3; break;
        }
        return;
    }
    ch_t *c = &chan[r & 7];
    if (r < 0x40) {
        switch (r & 0x38) {
        case 0x20: c->pan = (v >> 6) & 3; c->fb_level = (v >> 3) & 7; c->alg = v & 7; break;
        case 0x28: c->kc = v & 0x7f; chan_refresh(c); break;
        case 0x30: c->kf = (uint8_t)(v >> 2); chan_refresh(c); break;
        case 0x38: c->pms = (v >> 4) & 7; c->ams = v & 3; chan_refresh(c); break;
        }
        return;
    }
    op_t *o = &c->op[slot_to_op[(r >> 3) & 3]];
    switch (r & 0xe0) {
    case 0x40: o->dt1 = (v >> 4) & 7; o->mul = v & 15; op_frequency(c, o, c->dynamic ? lfo_pm : 0); break;
    case 0x60: o->tl = (uint16_t)((v & 0x7f) << 3); op_level(c, o); break;
    case 0x80: o->ks = (uint8_t)(v >> 6); o->ar = v & 0x1f; op_rates(c, o); break;
    case 0xa0: o->am_on = (uint8_t)(v >> 7); o->d1r = v & 0x1f; op_rates(c, o); op_level(c, o); break;
    case 0xc0: o->dt2 = (uint8_t)(v >> 6); o->d2r = v & 0x1f; op_frequency(c, o, c->dynamic ? lfo_pm : 0); op_rates(c, o); break;
    case 0xe0: o->sl = (uint8_t)(v >> 4); o->rr = v & 15; op_rates(c, o); break;
    }
}

/* ---- the interface ---- */

void ym2151_init(int clock, int rate)
{
    for (int i = 0; i < 1024; i++) {
        unsigned q = (i & 0x100) ? ~(unsigned)i : (unsigned)i;
        sine[i] = (int16_t)(ym_sin_quarter[q & 0xff] | ((i & 0x200) ? 0x8000 : 0));
    }
    memcpy(power, ym_power, sizeof(power));
    memcpy(eg_increment, ym_eg_increment, sizeof(eg_increment));
    synth_div = YM_QUALITY ? 1 : 2;
    double native = clock / 64.0, synth = (double)rate / synth_div;
    rate_ratio = (uint32_t)(native / synth * (1 << 20) + 0.5);
    eg_per_block = (uint32_t)(native / 3.0 / synth * BLOCK * 65536.0 + 0.5);
    ym2151_reset();
}

void ym2151_reset(void)
{
    memset(chan, 0, sizeof(chan));
    memset(eg_turn, 0, sizeof(eg_turn));
    for (int i = 0; i < 8; i++) {
        chan[i].pan = 3;
        for (int k = 0; k < 4; k++) {
            op_t *o = &chan[i].op[k];
            o->env = 0x3ff; o->state = EG_OFF; o->eg_mask = 0xffff; o->eg_level = 16; o->index = (uint8_t)(i * 4 + k);
        }
        chan_refresh(&chan[i]);
    }
    reg_addr = 0; status = 0; irq_enable = 0; timer_run = 0;
    timer_a_val = 0; timer_b_val = 0; timer_a_left = timer_b_left = 0;
    lfo_rate = lfo_wave = lfo_amd = lfo_pmd = lfo_hold = 0;
    lfo_counter = 0; lfo_am = lfo_pm = 0;
    noise_on = 0; noise_lfsr = 1; noise_acc = 0; noise_out = 0;
    eg_counter = 0; eg_acc = 0; have_held = 0; held = 0;
    lfo_set_rate();
}

void ym2151_write(int port, uint8_t v)
{
    if (port & 1) write_reg(reg_addr, v);
    else reg_addr = v;
}

uint8_t ym2151_status(void) { return status; }
int ym2151_irq(void) { return (status & irq_enable) != 0; }

void ym2151_advance(int clocks)
{
    if (timer_run & 1) {
        timer_a_left -= clocks;
        while (timer_a_left <= 0) { timer_a_left += 64 * (1024 - timer_a_val); status |= 1; }
    }
    if (timer_run & 2) {
        timer_b_left -= clocks;
        while (timer_b_left <= 0) { timer_b_left += 1024 * (256 - timer_b_val); status |= 2; }
    }
}

int ym2151_clocks_to_overflow(void)
{
    int n = 1 << 30;
    if ((timer_run & 1) && timer_a_left < n) n = timer_a_left;
    if ((timer_run & 2) && timer_b_left < n) n = timer_b_left;
    return n < 1 ? 1 : n;
}

/* ---- the envelope generator ---- */

static void eg_clock(void)
{
    eg_counter++;
    /* every operator whose turn it is: those whose k is no more than the clock's trailing zeros */
    unsigned t = eg_counter ? (unsigned)__builtin_ctz(eg_counter) : 31;
    uint32_t due = 0;
    for (unsigned k = 0; k <= t && k < 12; k++) due |= eg_turn[k];
    while (due) {
        unsigned i = (unsigned)__builtin_ctz(due);
        due &= due - 1;
        ch_t *c = &chan[i >> 2];
        op_t *o = &c->op[i & 3];
        /* the state is as the last change of level left it, except that a register
         * write may have moved the sustain level under it */
        op_eg_state(o);
        if (eg_counter & o->eg_mask) continue;
        unsigned rate = o->rate[o->state], shift = rate >> 2;
        uint32_t n = eg_counter << shift;
        unsigned inc = (eg_increment[rate] >> (4 * ((n >> (shift <= 11 ? 11 : shift)) & 7))) & 15;
        if (o->state == EG_ATTACK) {
            if (rate < 62) o->env = (uint16_t)((o->env + (((~(unsigned)o->env) * inc) >> 4)) & 0x3ff);
        } else {
            unsigned e = o->env + inc;
            if (e >= 0x3ff) { e = 0x3ff; if (o->state == EG_RELEASE) o->state = EG_OFF; }
            o->env = (uint16_t)e;
        }
        op_eg_state(o);
        op_level(c, o);
    }
}

#if YM_QUALITY
static void lfo_set_rate(void)
{
    /* the rate is a 4.4 floating point step with an implied leading one */
    lfo_step = (uint32_t)(((uint64_t)((0x10u | (lfo_rate & 15)) << (lfo_rate >> 4)) * rate_ratio * BLOCK) >> 20);
}

static void lfo_clock(void)
{
    if (!lfo_hold) lfo_counter += lfo_step;
    if (!(lfo_amd | lfo_pmd)) { lfo_am = 0; lfo_pm = 0; return; }
    unsigned i = (lfo_counter >> 22) & 0xff, am, pm;
    switch (lfo_wave) {
    case 0:  am = i ^ 0xff; pm = i; break;                                  /* sawtooth */
    case 1:  am = (i & 0x80) ? 0 : 0xff; pm = am ^ 0x80; break;             /* square */
    case 2:  am = ((i & 0x80) ? i << 1 : (i ^ 0xff) << 1) & 0xff;           /* triangle */
             pm = ((i & 0x40) ? am : ~am) & 0xff; break;
    default: am = pm = (noise_lfsr >> 17) & 0xff; break;                    /* noise */
    }
    int32_t new_am = (int32_t)((am * lfo_amd) >> 7);
    int32_t new_pm = ((int32_t)(int8_t)pm * lfo_pmd) >> 7;
    int am_moved = new_am != lfo_am, pm_moved = new_pm != lfo_pm;
    lfo_am = new_am; lfo_pm = new_pm;
    if (!(am_moved | pm_moved)) return;
    for (int ci = 0; ci < 8; ci++) {
        ch_t *c = &chan[ci];
        if (!((c->dynamic && pm_moved) || (c->ams && am_moved))) continue;
        for (int k = 0; k < 4; k++) {
            if (c->dynamic && pm_moved) op_frequency(c, &c->op[k], lfo_pm);
            if (c->ams && am_moved && c->op[k].am_on) op_level(c, &c->op[k]);
        }
    }
}
#endif

/* ---- the operators ---- */

/* one sample of one operator: the sine of its phase, pushed along by whatever modulates it,
 * and turned down by its envelope */
static inline int32_t op_value(uint32_t phase, uint32_t att, int32_t mod)
{
    unsigned s = (unsigned)(uint16_t)sine[((phase >> 22) + (uint32_t)mod) & 0x3ff];
    unsigned a = (s & 0x7fff) + att, sh = a >> 8;
    if (sh >= 13) return 0;
    int32_t v = (int32_t)(power[a & 0xff] >> sh);
    return (s & 0x8000) ? -v : v;
}

/* which operators are heard, by algorithm, as a mask of M1 C1 M2 C2 */
static const uint8_t carriers[8] = { 8, 8, 8, 8, 10, 14, 14, 15 };

static void chan_render(ch_t *c, int32_t *out, int n, int is_noise)
{
    /*
     * An operator whose attenuation is 0xd00 or more gives 0 whatever its phase: the
     * attenuation plus the sine's, shifted down eight, is thirteen or more, and the power
     * table is shifted right by that. A channel none of whose carriers can give anything is
     * not computed; the phases are moved on to where they would be, and if the first
     * operator (the one with feedback) can be heard, it alone is run, so that its feedback
     * is what it would have been.
     */
#ifndef YM_COMPUTE_ALL
    int heard = 0;
    for (int i = 0; i < 4; i++)
        if ((carriers[c->alg] >> i) & 1) heard |= c->op[i].att < 0xd00;
    if (!heard || !c->pan) {
        for (int i = 1; i < 4; i++) c->op[i].phase += c->op[i].step * (uint32_t)n;
        op_t *m = &c->op[0];
        if (m->att < 0xd00) {
            const int fbs = c->fb_level ? 10 - c->fb_level : 31;
            int32_t f0 = c->fb[0], f1 = c->fb[1];
            uint32_t p = m->phase;
            for (int k = 0; k < n; k++) { int32_t a = op_value(p, m->att, (f0 + f1) >> fbs); p += m->step; f0 = f1; f1 = a; }
            m->phase = p; c->fb[0] = f0; c->fb[1] = f1;
        } else {
            m->phase += m->step * (uint32_t)n;
            c->fb[0] = c->fb[1] = 0;
        }
        return;
    }
#endif
#ifdef SOUND_PROFILE
    ym_prof[3] += (uint32_t)n;
#endif
    /*
     * The four operators are copied into locals for the length of the block, so that the
     * compiler can keep them in registers, and there is a loop for each way of wiring them.
     */
    uint32_t p1 = c->op[0].phase, p2 = c->op[1].phase, p3 = c->op[2].phase, p4 = c->op[3].phase;
    const uint32_t s1 = c->op[0].step, s2 = c->op[1].step, s3 = c->op[2].step, s4 = c->op[3].step;
    const uint32_t a1 = c->op[0].att, a2 = c->op[1].att, a3 = c->op[2].att, a4 = c->op[3].att;
    const int gain = c->pan == 3 ? 2 : 1;        /* both sides go to the one speaker */
    const int fbs = c->fb_level ? 10 - c->fb_level : 31;
    int32_t f0 = c->fb[0], f1 = c->fb[1];

#define OP(n, mod) op_value(p##n, a##n, (mod)); p##n += s##n
#define FIRST      int32_t a = OP(1, (f0 + f1) >> fbs); f0 = f1; f1 = a; a >>= 1
#define EACH       for (int k = 0; k < n; k++)
    int32_t b, d, r;
#if YM_QUALITY
    if (is_noise) {
        /* channel 8's last operator is noise at the level of its envelope */
        const int32_t nz = noise_out * (int32_t)((((a4 >> 2) ^ 0x3ff) & 0x3ff) << 1);
        EACH {
            FIRST;
            switch (c->alg) {
            case 4:  b = OP(2, a); r = b + nz; p3 += s3; break;
            case 5:  b = OP(2, a); d = OP(3, a); r = b + d + nz; break;
            case 6:  b = OP(2, a); d = OP(3, 0); r = b + d + nz; break;
            case 7:  b = OP(2, 0); d = OP(3, 0); r = (a << 1) + b + d + nz; break;
            default: r = nz; p2 += s2; p3 += s3; break;
            }
            p4 += s4;
            out[k] += r * gain;
        }
    } else
#endif
    switch (c->alg) {
    case 0:  EACH { FIRST; b = OP(2, a); d = OP(3, b >> 1); r = OP(4, d >> 1); out[k] += r * gain; } break;
    case 1:  EACH { FIRST; b = OP(2, 0); d = OP(3, a + (b >> 1)); r = OP(4, d >> 1); out[k] += r * gain; } break;
    case 2:  EACH { FIRST; b = OP(2, 0); d = OP(3, b >> 1); r = OP(4, a + (d >> 1)); out[k] += r * gain; } break;
    case 3:  EACH { FIRST; b = OP(2, a); d = OP(3, 0); r = OP(4, (b >> 1) + (d >> 1)); out[k] += r * gain; } break;
    case 4:  EACH { FIRST; b = OP(2, a); d = OP(3, 0); r = OP(4, d >> 1); out[k] += (b + r) * gain; } break;
    case 5:  EACH { FIRST; b = OP(2, a); d = OP(3, a); r = OP(4, a); out[k] += (b + d + r) * gain; } break;
    case 6:  EACH { FIRST; b = OP(2, a); d = OP(3, 0); r = OP(4, 0); out[k] += (b + d + r) * gain; } break;
    default: EACH { FIRST; b = OP(2, 0); d = OP(3, 0); r = OP(4, 0); out[k] += ((a << 1) + b + d + r) * gain; } break;
    }
#undef OP
#undef FIRST
#undef EACH
    c->op[0].phase = p1; c->op[1].phase = p2; c->op[2].phase = p3; c->op[3].phase = p4;
    c->fb[0] = f0; c->fb[1] = f1;
}

static void synth(int32_t *out, int n)
{
    while (n > 0) {
        int b = n > BLOCK ? BLOCK : n;
        uint32_t y0 = YT(), y1;
        (void)y0; (void)y1;
#if YM_QUALITY
        lfo_clock();
        if (noise_on) {
            noise_step = (uint32_t)(((uint64_t)rate_ratio * 2 * BLOCK << 8) / (32 - (noise_freq & 31)));
            noise_acc += noise_step;
            for (unsigned t = noise_acc >> 28; t; t--) noise_lfsr = (noise_lfsr << 1) | (((noise_lfsr >> 17) ^ (noise_lfsr >> 14) ^ 1) & 1);
            noise_acc &= (1u << 28) - 1;
            noise_out = ((noise_lfsr >> 17) & 1) ? -1 : 1;
        }
#endif
#ifdef SOUND_PROFILE
        y1 = YT(); ym_prof[0] += y1 - y0; y0 = y1;
#endif
        for (int ci = 0; ci < 8; ci++) chan_render(&chan[ci], out, b, YM_QUALITY && noise_on && ci == 7);
#ifdef SOUND_PROFILE
        y1 = YT(); ym_prof[1] += y1 - y0; y0 = y1;
#endif
        eg_acc += eg_per_block * (uint32_t)b / BLOCK;
        while (eg_acc >= 0x10000) { eg_acc -= 0x10000; eg_clock(); }
#ifdef SOUND_PROFILE
        y1 = YT(); ym_prof[2] += y1 - y0;
#endif
        out += b; n -= b;
    }
}

void ym2151_render(int32_t *mix, int samples)
{
    if (synth_div == 1) { synth(mix, samples); return; }

    /* half rate: every other sample is computed, and the one between is halfway */
    int32_t tmp[64];
    while (samples > 0) {
        int want = samples > 64 ? 64 : samples, k = 0;
        if (have_held == 2) { mix[k++] += held; have_held = 1; }     /* the second half of a pair left over */
        int pairs = (want - k + 1) / 2;
        memset(tmp, 0, sizeof(int32_t) * (size_t)pairs);
        synth(tmp, pairs);
        for (int p = 0; p < pairs; p++) {
            int32_t next = tmp[p];
            mix[k++] += have_held ? (held + next) / 2 : next;
            held = next; have_held = 1;
            if (k < want) mix[k++] += next; else have_held = 2;
        }
        mix += want; samples -= want;
    }
}
