/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * sound.c - Star Wars sound board (see sound.h)
 */
#include "sound.h"
#include "pokey.h"
#include "tms5220.h"
#include <string.h>

/* ---- second 6809 instance: the core is compiled into this translation unit under new names ---- */
static inline __attribute__((always_inline)) unsigned char snd_read(unsigned addr);
static inline __attribute__((always_inline)) void snd_write(unsigned addr, unsigned char d);
#define E6809_READ8(a)     snd_read(a)
#define E6809_WRITE8(a, d) snd_write(a, d)
#define e6809_read8   snd_e6809_read8
#define e6809_write8  snd_e6809_write8
#define e6809_reset   snd_e6809_reset
#define e6809_sstep   snd_e6809_sstep
#define e6809_get_pc  snd_e6809_get_pc
#include "e6809.c"

/* ---- memory ---- */
static const uint8_t *rom;          /* 16KB */
static uint8_t ram[0x800];          /* 0x2000-0x27FF */
static uint8_t riot_ram[0x80];      /* 0x1000-0x107F */

/* ---- latches ---- */
static uint8_t cmd_val, cmd_pending;      /* main -> sound */
static uint8_t reply_val, reply_pending;  /* sound -> main */

/* ---- MOS 6532 RIOT ---- */
static uint8_t riot_pa_out, riot_ddra, riot_pb_out, riot_ddrb;
static uint32_t riot_timer_count;    /* counts down at the prescaled rate */
static uint32_t riot_prescale;       /* 1, 8, 64, 1024 */
static uint32_t riot_prescale_left;
static uint8_t riot_timer_irq_en, riot_timer_flag;
static uint8_t riot_pa7_irq_en, riot_pa7_pos_edge, riot_pa7_flag, riot_pa7_last;
static uint8_t riot_timer_expired;   /* after expiry the timer runs at /1 and wraps */

/* ---- stats ---- */
static uint64_t total_cycles;
static uint32_t irq_count, pokey_writes, commands;
#ifdef SW_DEBUG
#include <stdio.h>
uint32_t snd_dbg_pc_hist[0x10000];
int snd_dbg_log_pokey, snd_dbg_log_tms;
#endif

static pokey_t pokey[4];
static tms5220_t tms;
#ifdef SW_DEBUG
void snd_dbg_dump(void)
{
    for (int i = 0; i < 4; i++)
        printf("   POKEY%d audf %02X %02X %02X %02X audc %02X %02X %02X %02X ctl %02X skctl %02X\n", i,
               pokey[i].audf[0], pokey[i].audf[1], pokey[i].audf[2], pokey[i].audf[3],
               pokey[i].audc[0], pokey[i].audc[1], pokey[i].audc[2], pokey[i].audc[3], pokey[i].audctl, pokey[i].skctl);
    printf("   TMS: cmds %u bytes %u frames %u talk_starts %u talking %d fifo %d  RIOT: timer_irq_en %d pa7_irq_en %d prescale %u\n",
           tms.commands, tms.bytes_in, tms.frames, tms.talk_starts, tms.TALKD, tms.fifo_count, riot_timer_irq_en, riot_pa7_irq_en, riot_prescale);
}
#endif

/* PA input bits: 0 = /WS (out), 1 = /RS (out), 2 = TMS /READY (in), 3 nop (out),
 * 4 = not self-test (in, 1), 5 = TMS VDD (out), 6 = reply pending (in), 7 = command pending (in) */
static uint8_t riot_pa_in(void)
{
    uint8_t v = 0;
    if (tms5220_readyq(&tms)) v |= 0x04;
    v |= 0x10;
    if (reply_pending) v |= 0x40;
    if (cmd_pending)   v |= 0x80;
    return v;
}

static void riot_pa7_update(void)
{
    uint8_t now = (riot_pa_in() >> 7) & 1;
    if (now != riot_pa7_last) {
        if ((riot_pa7_pos_edge && now) || (!riot_pa7_pos_edge && !now)) {
            riot_pa7_flag = 1;
#ifdef SW_DEBUG
            if (snd_dbg_log_tms) printf("  SND: PA7 edge -> flag (irq_en %d) at %.3fs pc %04X\n", riot_pa7_irq_en, (double)total_cycles / 1512000.0, snd_e6809_get_pc() & 0xffff);
#endif
        }
        riot_pa7_last = now;
    }
}

static uint8_t riot_read(uint16_t a)
{
    uint8_t r = a & 0x1f;
    if (!(r & 0x04)) {
        switch (r & 3) {
            case 0: {
#ifdef SW_DEBUG
                if (snd_dbg_log_tms) printf("  RIOT PA read %.3fs: in %02X ddra %02X pc %04X\n", (double)total_cycles / 1512000.0, riot_pa_in(), riot_ddra, snd_e6809_get_pc() & 0xffff);
#endif
                return (riot_pa_in() & ~riot_ddra) | (riot_pa_out & riot_ddra);
            }
            case 1: return riot_ddra;
            case 2: {
                uint8_t st = tms5220_status(&tms);
#ifdef SW_DEBUG
                if (snd_dbg_log_tms) printf("  TMS PB read %.3fs: /RS %d status %02X (talkd %d spen %d talk %d fifo %d ddis %d) pc %04X\n", (double)total_cycles / 1512000.0, (riot_pa_out >> 1) & 1, st, tms.TALKD, tms.SPEN, tms.TALK, tms.fifo_count, tms.DDIS, snd_e6809_get_pc() & 0xffff);
#endif
                return (st & ~riot_ddrb) | (riot_pb_out & riot_ddrb);
            }
            case 3: return riot_ddrb;
        }
    }
    if (r & 0x01) {
        /* interrupt flag register: bit7 timer, bit6 PA7 */
        uint8_t v = (riot_timer_flag ? 0x80 : 0) | (riot_pa7_flag ? 0x40 : 0);
        riot_pa7_flag = 0;
        return v;
    }
    /* timer read: A3 sets the timer interrupt enable */
    riot_timer_irq_en = (r & 0x08) ? 1 : 0;
    riot_timer_flag = 0;
    return (uint8_t)riot_timer_count;
}

static void riot_write(uint16_t a, uint8_t d)
{
    uint8_t r = a & 0x1f;
    if (!(r & 0x04)) {
        switch (r & 3) {
            case 0: {
                uint8_t old = riot_pa_out;
                riot_pa_out = d;
#ifdef SW_DEBUG
                if (snd_dbg_log_tms && ((old ^ d) & 0x03) && !(d & 1) == 1) printf("  TMS ctl %.2fs: /WS %d /RS %d data %02X (DDIS %d fifo %d talk %d status %02X)\n", (double)total_cycles / 1512000.0, d & 1, (d >> 1) & 1, riot_pb_out, tms.DDIS, tms.fifo_count, tms.TALKD, tms5220_status(&tms));
#endif
                /* TMS5220 control lines are outputs on PA0 (/WS) and PA1 (/RS) */
                if ((old ^ d) & 0x01) tms5220_wsq(&tms, d & 0x01);
                if ((old ^ d) & 0x02) tms5220_rsq(&tms, (d >> 1) & 0x01);
                break;
            }
            case 1: riot_ddra = d; break;
            case 2: riot_pb_out = d; tms5220_data_write(&tms, d); break;
            case 3: riot_ddrb = d; break;
        }
        return;
    }
    if (r & 0x10) {
        /* timer write: A1A0 = prescaler, A3 = interrupt enable */
        static const uint32_t pre[4] = { 1, 8, 64, 1024 };
        riot_prescale = pre[r & 3];
        riot_prescale_left = riot_prescale;
        riot_timer_count = d;
        riot_timer_irq_en = (r & 0x08) ? 1 : 0;
        riot_timer_flag = 0;
        riot_timer_expired = 0;
#ifdef SW_DEBUG
        if (snd_dbg_log_tms && total_cycles < 3000000) printf("  SND: RIOT timer write %02X reg %02X (prescale %u irq_en %d) at %.3fs\n", d, r, riot_prescale, riot_timer_irq_en, (double)total_cycles / 1512000.0);
#endif
    } else {
        /* edge detect control: A0 = PA7 interrupt enable, A1 = positive edge */
        riot_pa7_irq_en = r & 0x01;
        riot_pa7_pos_edge = (r >> 1) & 0x01;
#ifdef SW_DEBUG
        if (snd_dbg_log_tms) printf("  SND: RIOT edge ctl reg %02X: pa7_irq_en %d pos_edge %d at %.3fs\n", r, riot_pa7_irq_en, riot_pa7_pos_edge, (double)total_cycles / 1512000.0);
#endif
    }
}

/* CPU cycles until the timer expires (0 if it already has) */
static uint32_t riot_cycles_to_expiry(void)
{
    if (riot_timer_expired) return 0;
    return riot_timer_count * riot_prescale + riot_prescale_left;
}

static void riot_tick(unsigned cycles)
{
    /* the 6532 timer runs at the CPU clock (phi2) */
    while (cycles) {
        if (riot_timer_expired) {
            riot_timer_count = (riot_timer_count - cycles) & 0xff;   /* /1 after expiry, wraps */
            return;
        }
        if (cycles < riot_prescale_left) {
            riot_prescale_left -= cycles;
            return;
        }
        cycles -= riot_prescale_left;
        riot_prescale_left = riot_prescale;
        if (riot_timer_count == 0) {
            riot_timer_flag = 1;
            riot_timer_expired = 1;
            riot_timer_count = 0xff;
        } else {
            riot_timer_count--;
        }
    }
}

static int riot_irq(void)
{
    return (riot_timer_flag && riot_timer_irq_en) || (riot_pa7_flag && riot_pa7_irq_en);
}

/* ---- bus ---- */
static inline __attribute__((always_inline)) unsigned char snd_read(unsigned addr)
{
    uint16_t a = (uint16_t)addr;
    if (a < 0x0800) return 0xff;
    if (a < 0x1000) {                                                /* SIN read */
#ifdef SW_DEBUG
        if (snd_dbg_log_tms) printf("  SND: SIN read %02X at %.3fs (pc %04X)\n", cmd_val, (double)total_cycles / 1512000.0, snd_e6809_get_pc() & 0xffff);
#endif
        cmd_pending = 0; return cmd_val;
    }
    if (a < 0x1080) return riot_ram[a & 0x7f];
    if (a < 0x10a0) return riot_read(a);
    if (a < 0x2000) return 0xff;
    if (a < 0x2800) return ram[a - 0x2000];
    if (a < 0x4000) return 0xff;
    if (a < 0x8000) return rom[a - 0x4000];
    if (a < 0xc000) return 0xff;
    return rom[a - 0xc000];
}

static inline __attribute__((always_inline)) void snd_write(unsigned addr, unsigned char d)
{
    uint16_t a = (uint16_t)addr;
    if (a < 0x0800) {                                                /* SOUT */
#ifdef SW_DEBUG
        if (snd_dbg_log_tms) printf("  SND: SOUT write %02X at %.3fs (pc %04X)\n", d, (double)total_cycles / 1512000.0, snd_e6809_get_pc() & 0xffff);
#endif
        reply_val = d; reply_pending = 1; return;
    }
    if (a < 0x1000) return;
    if (a < 0x1080) { riot_ram[a & 0x7f] = d; return; }
    if (a < 0x10a0) { riot_write(a, d); return; }
    if (a >= 0x1800 && a < 0x1840) {
        unsigned offset = a - 0x1800;
        int chip = (offset >> 3) & ~0x04;
        int control = (offset & 0x20) >> 2;
        int reg = (offset % 8) | control;
        pokey_write(&pokey[chip], reg, d);
        pokey_writes++;
#ifdef SW_DEBUG
        if (snd_dbg_log_pokey && pokey_writes <= 400) printf("  POKEY%d reg %X <= %02X (pc %04X)\n", chip, reg, d, snd_e6809_get_pc());
#endif
        return;
    }
    if (a >= 0x2000 && a < 0x2800) { ram[a - 0x2000] = d; return; }
}

/* ---- public ---- */
void snd_init(const uint8_t *r)
{
    rom = r;
    for (int i = 0; i < 4; i++) pokey_init(&pokey[i]);
    tms5220_init(&tms);
    snd_reset();
}

void snd_cpu_reset(void)
{
    cmd_pending = reply_pending = 0;
    snd_e6809_reset();
}

void snd_reset(void)
{
    memset(ram, 0, sizeof(ram));
    memset(riot_ram, 0, sizeof(riot_ram));
    cmd_pending = reply_pending = 0;
    riot_pa_out = riot_ddra = riot_pb_out = riot_ddrb = 0;
    riot_timer_count = 0xff; riot_prescale = 1; riot_prescale_left = 1;
    riot_timer_irq_en = riot_timer_flag = 0;
    riot_pa7_irq_en = riot_pa7_pos_edge = riot_pa7_flag = 0;
    riot_pa7_last = 0;
    riot_timer_expired = 0;
    for (int i = 0; i < 4; i++) pokey_reset(&pokey[i]);
    tms5220_reset(&tms);
    snd_e6809_reset();
}

/* The sound program idles in a tight loop (0x7D4C-0x7D5E) polling a tick flag
 * that its timer interrupt handler sets. While parked there with no interrupt
 * pending, nothing observable happens until the timer expires, so skip ahead. */
#define IDLE_LOOP_LO 0x7d4c
#define IDLE_LOOP_HI 0x7d5f
static uint32_t idle_skipped;

uint32_t snd_run(uint32_t cycles)
{
    uint32_t run = 0;
    while (run < cycles) {
        riot_pa7_update();
        int irq = riot_irq();
        if (irq) irq_count++;
        if (!irq) {
            unsigned pc = snd_e6809_get_pc() & 0xffff;
            if (pc >= IDLE_LOOP_LO && pc <= IDLE_LOOP_HI) {
                uint32_t to_expiry = riot_cycles_to_expiry();
                uint32_t skip = cycles - run;
                if (to_expiry && to_expiry < skip) skip = to_expiry;
                if (skip > 16) {
                    skip -= 8;                     /* leave a little room to execute the loop */
                    riot_tick(skip);
                    tms5220_tick(&tms, skip);
                    total_cycles += skip;
                    run += skip;
                    idle_skipped += skip;
                    continue;
                }
            }
        }
#ifdef SW_DEBUG
        snd_dbg_pc_hist[snd_e6809_get_pc() & 0xffff]++;
#endif
        unsigned c = snd_e6809_sstep(irq, 0);
        riot_tick(c);
        tms5220_tick(&tms, c);
        total_cycles += c;
        run += c;
    }
    return run;
}

void snd_command_write(uint8_t d)
{
#ifdef SW_DEBUG
    if (snd_dbg_log_tms) printf("  SND: command %02X at %.3fs (pending was %d)\n", d, (double)total_cycles / 1512000.0, cmd_pending);
#endif
    cmd_val = d; cmd_pending = 1; commands++;
}
uint8_t snd_reply_read(void) { reply_pending = 0; return reply_val; }
uint8_t snd_ready_flags(void) { return (cmd_pending ? 0x80 : 0) | (reply_pending ? 0x40 : 0); }

int snd_render_mask = 0x1f;   /* bit i: POKEY i; bit 4: speech (diagnostics) */
void snd_render(int16_t *buf, int samples, int sample_rate)
{
    memset(buf, 0, samples * sizeof(int16_t));
    for (int i = 0; i < 4; i++) if (snd_render_mask & (1 << i)) pokey_render(&pokey[i], buf, samples, sample_rate);
    if (snd_render_mask & 0x10) tms5220_render(&tms, buf, samples, sample_rate);
}

uint16_t snd_pc(void) { return (uint16_t)snd_e6809_get_pc(); }
uint32_t snd_irq_count(void) { return irq_count; }
uint32_t snd_pokey_writes(void) { return pokey_writes; }
uint32_t snd_commands(void) { return commands; }
uint64_t snd_total_cycles(void) { return total_cycles; }
uint32_t snd_speech_underruns(void) { return tms.underruns; }
void snd_speech_debug(uint32_t *written, uint32_t *rd, uint32_t *calls, uint32_t *rate, uint32_t *talkd)
{ *written = tms.dbg_written; *rd = tms.dbg_read; *calls = tms.dbg_render_calls; *rate = tms.dbg_rate; *talkd = tms.TALKD; }
uint32_t snd_idle_skipped(void) { uint32_t v = idle_skipped; idle_skipped = 0; return v; }
