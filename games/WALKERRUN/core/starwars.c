/* SPDX-License-Identifier: BSD-3-Clause
 * Ported to plain C for PELLETINO by Jesse Castro, 2026, from MAME (src/mame/atari/starwars.cpp and starwars_m.cpp),
 * copyright Steve Baines, Frank Palazzolo and the MAME team. Redistribution and use are permitted under
 * the BSD-3-Clause license; the notice, conditions and disclaimer in this game's
 * THIRD_PARTY_NOTICES.md apply to this file. */
/*
 * starwars.c - Atari Star Wars main board emulation (see starwars.h)
 *
 * Matrix processor, divider and memory map ported from MAME's
 * starwars.cpp / starwars_m.cpp (BSD-3-Clause, Steve Baines, Frank Palazzolo).
 */
#include "starwars.h"
#include "slapstic.h"
#include "sound.h"
#include <string.h>

/* The CPU core is compiled into this translation unit so its bus accessors inline. */
static inline __attribute__((always_inline)) unsigned char cpu_read(unsigned addr);
static inline __attribute__((always_inline)) void cpu_write(unsigned addr, unsigned char d);
static uint8_t io_read(uint16_t a);
static void io_write(uint16_t a, uint8_t d);
static uint8_t irq_line;
static int snd_sync_break;   /* a sound command was just latched: end the chunk so the sound CPU can take it */
#define E6809_READ8(a)     cpu_read(a)
#define E6809_WRITE8(a, d) cpu_write(a, d)
#define E6809_IRQ_LINE     irq_line
static inline int at_wait_loop(void);
#define E6809_BREAK_CHECK() (at_wait_loop() || snd_sync_break)
#ifdef SW_DEBUG
static void dbg_pre_step(void);
#define E6809_PRE_STEP()   dbg_pre_step()
#endif
#include "e6809.c"

/* ---- memory ---- */
static uint8_t ram_vec[0x3000];     /* 0x0000-0x2FFF vector RAM */
static uint8_t ram_main[0x0800];    /* 0x4800-0x4FFF */
static uint8_t mathram[0x1000];     /* 0x5000-0x5FFF, shared with matrix processor */
static uint8_t nvram[0x100];        /* 0x4500-0x45FF X2212 (4-bit) */
static sw_roms_t roms;
static uint8_t bank_sel;

/* 256-byte page tables: non-NULL = direct memory, NULL = I/O or unmapped (slow path).
 * ESB: the slapstic window 0x8000-0x9FFF stays NULL so every access there goes through the
 * chip (about 2% of fetches); everything else is as fast as Star Wars. */
static const uint8_t *rpage[256];
static uint8_t *wpage[256];
static int is_esb;                  /* rom_slapstic present: ESB memory map */

static void map_bank(void)
{
    const uint8_t *b = roms.rom_bank + (bank_sel ? 0x2000 : 0);
    for (int pg = 0x60; pg < 0x80; pg++) rpage[pg] = b + ((pg - 0x60) << 8);
    if (is_esb) {                   /* ESB: the same latch bit pages 0xA000-0xFFFF too */
        const uint8_t *m = bank_sel ? (roms.rom_main_page1 ? roms.rom_main_page1 : roms.rom_main + 0x6000) : roms.rom_main;
        for (int pg = 0xa0; pg < 0x100; pg++) rpage[pg] = m + ((pg - 0xa0) << 8);
        if (bank_sel && roms.rom_main_page1_c)
            for (int pg = 0xc0; pg < 0xe0; pg++) rpage[pg] = roms.rom_main_page1_c + ((pg - 0xc0) << 8);
    }
}

static void map_init(void)
{
    memset(rpage, 0, sizeof(rpage));
    memset(wpage, 0, sizeof(wpage));
    for (int pg = 0x00; pg < 0x30; pg++) { rpage[pg] = ram_vec + (pg << 8);  wpage[pg] = ram_vec + (pg << 8); }
    for (int pg = 0x30; pg < 0x40; pg++) rpage[pg] = roms.rom_vector + ((pg - 0x30) << 8);
    for (int pg = 0x48; pg < 0x50; pg++) { rpage[pg] = ram_main + ((pg - 0x48) << 8); wpage[pg] = ram_main + ((pg - 0x48) << 8); }
    for (int pg = 0x50; pg < 0x60; pg++) { rpage[pg] = mathram + ((pg - 0x50) << 8);  wpage[pg] = mathram + ((pg - 0x50) << 8); }
    if (!is_esb) for (int pg = 0x80; pg < 0x100; pg++) rpage[pg] = roms.rom_main + ((pg - 0x80) << 8);
    if (is_esb) slapstic_reset();
    map_bank();
}

/* ---- state ---- */
static sw_input_t input;
static uint8_t dsw0 = 0x98, dsw1 = 0x02;
static uint8_t adc_channel;
static uint64_t total_cycles;
static uint64_t next_irq_at;
static uint32_t irq_count;
static uint32_t frame_count;
static uint64_t avg_done_at;       /* CPU cycle when VG_HALT becomes visible */
static uint64_t math_done_at;      /* CPU cycle when MATH_RUN clears */
static uint32_t prng_state = 1;
static int sound_enabled;
static uint32_t soundrst_count;
uint32_t sw_soundrst_count(void) { return soundrst_count; }
static avg_t avg;
#ifdef SW_DEBUG
uint32_t sw_dbg_in0_reads, sw_dbg_in1_reads, sw_dbg_snd_writes, sw_dbg_snd_flag_reads, sw_dbg_snd_reads, sw_dbg_math_runs, sw_dbg_div_ops, sw_dbg_adc_reads;
uint8_t sw_dbg_last_snd_cmd;
uint32_t sw_dbg_irq_taken;          /* IRQ vector fetches */
int sw_dbg_trace_arm;                /* set to 1 to trace the next IRQ handler */
uint16_t sw_dbg_trace[2000]; int sw_dbg_trace_n; static int trace_active;
#define SW_DBG_RING 16384
uint16_t sw_dbg_ring[SW_DBG_RING]; unsigned sw_dbg_ring_i; int sw_dbg_ring_frozen; unsigned sw_dbg_halt_pc = 0x10000;
uint32_t sw_dbg_pc_hist[0x10000];    /* instructions started per PC */
uint32_t sw_dbg_bank_hist[2][8];     /* samples per 8 KB region, by ROM bank page */
uint32_t sw_dbg_avg_frames, sw_dbg_avg_mismatch, sw_dbg_soundrst;
int sw_dbg_log_flags;
#include <stdio.h>
uint32_t sw_dbg_w483d, sw_dbg_r483d; uint16_t sw_dbg_w483d_pc; uint8_t sw_dbg_w483d_val;
#define DBG(x) x
#else
#define DBG(x)
#endif
static sw_frame_cb_t frame_cb;
static void *frame_user;
static uint64_t (*time_src)(void);
static sw_stats_t stats;
#define TNOW() (time_src ? time_src() : 0)

/* ---- matrix processor ---- */
#define M_NOP       0x00
#define M_LAC       0x01
#define M_READ_ACC  0x02
#define M_HALT      0x04
#define M_INC_BIC   0x08
#define M_CLEAR_ACC 0x10
#define M_LDC       0x20
#define M_LDB       0x40
#define M_LDA       0x80

static uint8_t PROM_STR[1024], PROM_MAS[1024], PROM_AM[1024];
static int MPA, BIC;
static int16_t mA, mB, mC;
static int32_t mACC;
static uint16_t dvd_shift, quotient_shift, divisor, dividend;

static void mproc_init(void)
{
    const uint8_t *src = roms.prom_mathbox;
    for (int cnt = 0; cnt < 1024; cnt++) {
        int val  = (src[0x0c00 + cnt]      ) & 0x000f;
        val     |= (src[0x0800 + cnt] <<  4) & 0x00f0;
        val     |= (src[0x0400 + cnt] <<  8) & 0x0f00;
        val     |= (src[0x0000 + cnt] << 12) & 0xf000;
        PROM_STR[cnt] = (val >> 8) & 0xff;
        PROM_MAS[cnt] =  val       & 0x7f;
        PROM_AM[cnt]  = (val >> 7) & 0x01;
    }
}

static void run_mproc(void)
{
    int M_STOP = 100000;
    int mptime = 0;
    uint64_t t0 = TNOW();

    while (M_STOP > 0) {
        mptime += 5;
        int IP15_8 = PROM_STR[MPA];
        int IP7    = PROM_AM[MPA];
        int IP6_0  = PROM_MAS[MPA];
        int MA;
        if (IP7 == 0)
            MA = (IP6_0 & 3) | ((BIC & 0x01ff) << 2);
        else
            MA = IP6_0;
        int MA_byte = MA << 1;
        int RAMWORD = (mathram[MA_byte + 1] & 0xff) | ((mathram[MA_byte] & 0xff) << 8);

        if (IP15_8 & M_CLEAR_ACC) mACC = 0;
        if (IP15_8 & M_LAC)       mACC = (int32_t)((uint32_t)RAMWORD << 16);
        if (IP15_8 & M_READ_ACC) {
            mathram[MA_byte + 1] = (mACC >> 16) & 0xff;
            mathram[MA_byte    ] = (mACC >> 24) & 0xff;
        }
        if (IP15_8 & M_HALT)    M_STOP = 0;
        if (IP15_8 & M_INC_BIC) BIC = (BIC + 1) & 0x1ff;
        if (IP15_8 & M_LDC) {
            mC = (int16_t)RAMWORD;
            /* ACC += ((A - B) << 1) * C << 1, with 32-bit wraparound (no UB shifts) */
            mACC = (int32_t)((uint32_t)mACC + 4u * (uint32_t)((int64_t)(mA - mB) * (int64_t)mC));
            mA = (mA & 0x8000) ? (int16_t)0xffff : 0;
            mB = (mB & 0x8000) ? (int16_t)0xffff : 0;
            mptime += 33;
        }
        if (IP15_8 & M_LDB) mB = (int16_t)RAMWORD;
        if (IP15_8 & M_LDA) mA = (int16_t)RAMWORD;

        MPA = (MPA & 0x0300) | ((MPA + 1) & 0x00ff);
        M_STOP--;
    }
    /* MATH_RUN stays set for mptime master clocks (8 master clocks per CPU cycle) */
    math_done_at = total_cycles + (uint64_t)mptime / 8 + 1;
    stats.math_us += TNOW() - t0;
    stats.math_steps += 100000 - M_STOP;
}

static void math_w(uint16_t offset, uint8_t data)
{
    switch (offset) {
        case 0: MPA = data << 2; DBG(sw_dbg_math_runs++;) run_mproc(); break;
        case 1: BIC = (BIC & 0x00ff) | ((data & 0x01) << 8); break;
        case 2: BIC = (BIC & 0x0100) | data; break;
        case 4:
            divisor = (divisor & 0x00ff) | (data << 8);
            dvd_shift = dividend;
            quotient_shift = 0;
            break;
        case 5:
            DBG(sw_dbg_div_ops++;)
            divisor = (divisor & 0xff00) | data;
            for (int i = 1; i < 16; i++) {
                quotient_shift <<= 1;
                if (((int32_t)dvd_shift + (divisor ^ 0xffff) + 1) & 0x10000) {
                    quotient_shift |= 1;
                    dvd_shift = (uint16_t)((dvd_shift + (divisor ^ 0xffff) + 1) << 1);
                } else {
                    dvd_shift <<= 1;
                }
            }
            break;
        case 6: dividend = (dividend & 0x00ff) | (data << 8); break;
        case 7: dividend = (dividend & 0xff00) | data; break;
        default: break;
    }
}

/* ---- PRNG: 23-bit LFSR, taps 4 and 22, inverted feedback; CPU sees bits 8..15 ---- */
static uint8_t prng_read(void)
{
    /* the hardware clocks at 3 MHz; advancing a few bits per read is plenty */
    for (int i = 0; i < 8; i++) {
        uint32_t fb = (((prng_state >> 4) ^ (prng_state >> 22)) & 1) ^ 1;
        prng_state = ((prng_state << 1) | fb) & 0x7fffff;
    }
    return (prng_state >> 8) & 0xff;
}

/* ---- CPU bus ---- */
static uint8_t io_read(uint16_t a)
{
    switch (a & 0xffe0) {
        case 0x4300: {
            DBG(sw_dbg_in0_reads++;)
            uint8_t v = 0xdf;               /* bit5 unused reads 0 */
            if (input.coin2)   v &= ~0x01;
            if (input.coin1)   v &= ~0x02;
            if (input.service) v &= ~0x10;
            if (input.button4) v &= ~0x40;
            if (input.fire)    v &= ~0x80;
            return v;
        }
        case 0x4320: {
            DBG(sw_dbg_in1_reads++;)
            uint8_t v = 0x34;               /* service2, button3, button2 idle high */
            if (input.button3) v &= ~0x10;
            if (input.button2) v &= ~0x20;
            if (total_cycles >= avg_done_at)  v |= 0x40;   /* VG_HALT */
            if (total_cycles < math_done_at)  v |= 0x80;   /* MATH_RUN */
            return v;
        }
        case 0x4340: return dsw0;
        case 0x4360: return dsw1;
        case 0x4380:
            DBG(sw_dbg_adc_reads++;)
            switch (adc_channel) {
                case 0: return input.pitch;
                case 1: return input.yaw;
                default: return 0;
            }
        case 0x4400:
            if (a == 0x4400) { DBG(sw_dbg_snd_reads++;) return sound_enabled ? snd_reply_read() : 0; }
            if (a == 0x4401) { DBG(sw_dbg_snd_flag_reads++;) return sound_enabled ? snd_ready_flags() : 0x00; }
            return 0xff;
        case 0x4500: case 0x4520: case 0x4540: case 0x4560:
        case 0x4580: case 0x45a0: case 0x45c0: case 0x45e0:
            return nvram[a & 0xff] & 0x0f;
        case 0x4700:
            if (a == 0x4700) return (quotient_shift & 0xff00) >> 8;
            if (a == 0x4701) return quotient_shift & 0x00ff;
            if (a == 0x4703) return prng_read();
            return 0xff;
        default:
            return 0xff;
    }
}

static void io_write(uint16_t a, uint8_t d)
{
    switch (a & 0xffe0) {
        case 0x4400:
            if (a == 0x4400) {
                DBG(if (sw_dbg_log_flags && total_cycles > 26700000 && total_cycles < 26900000) printf("  CMD %02X at cyc %llu main pc %04X pending %d\n", d, (unsigned long long)total_cycles, e6809_get_pc() & 0xffff, sound_enabled ? (snd_ready_flags() >> 7) : 0);)
                if (sound_enabled) { snd_command_write(d); snd_sync_break = 1; } DBG(sw_dbg_snd_writes++; sw_dbg_last_snd_cmd = d;)
            }
            break;
        case 0x4500: case 0x4520: case 0x4540: case 0x4560:
        case 0x4580: case 0x45a0: case 0x45c0: case 0x45e0:
            nvram[a & 0xff] = d & 0x0f;
            break;
        case 0x4600: {
            uint64_t t0 = TNOW();
#ifdef SW_DEBUG
            static avg_t avg_ref;
            avg_ref = avg;
            uint32_t ref_cycles = avg_go(&avg_ref);
#endif
            uint32_t vg_cycles = avg_go_fast(&avg);
#ifdef SW_DEBUG
            sw_dbg_avg_frames++;
            {
                int d_cyc = ref_cycles != vg_cycles, d_n = avg_ref.npoints != avg.npoints;
                int d_pts = memcmp(avg_ref.points, avg.points, avg.npoints * sizeof(avg_point_t)) != 0;
                int d_st = memcmp(&avg_ref.st, &avg.st, sizeof(avg.st)) != 0;
                if (d_cyc || d_n || d_pts || d_st) {
                    sw_dbg_avg_mismatch++;
                    if (sw_dbg_avg_mismatch <= 3)
                        printf("AVG mismatch: cycles %u vs %u, npoints %d vs %d, points %s, state %s (latch %02X/%02X data %02X/%02X op %d/%d)\n",
                               ref_cycles, vg_cycles, avg_ref.npoints, avg.npoints, d_pts ? "DIFF" : "same", d_st ? "DIFF" : "same",
                               avg_ref.st.state_latch, avg.st.state_latch, avg_ref.st.data, avg.st.data, avg_ref.st.op, avg.st.op);
                }
            }
#endif
            uint64_t t1 = TNOW();
            avg_done_at = total_cycles + vg_cycles / 8 + 1;
            frame_count++;
            if (frame_cb) frame_cb(&avg, frame_user);
            stats.avg_us += t1 - t0;
            stats.frame_cb_us += TNOW() - t1;
            stats.avg_steps += avg.steps;
            break;
        }
        case 0x4620:
            avg_reset(&avg);
            avg_done_at = 0;
            break;
        case 0x4640: break;                       /* watchdog */
        case 0x4660: irq_line = 0; break;         /* IRQ acknowledge */
        case 0x4680: {                            /* LS259 output latch, mirror 0x18 */
            int bit = a & 7;
            int val = d >> 7;
            if (bit == 4) { bank_sel = val; map_bank(); }   /* ROM bank */
            /* 0,1 coin counters; 2,3,6 LEDs; 5 PRNG reset; 7 NVRAM recall */
            break;
        }
        case 0x46a0: break;                       /* NVRAM store (we keep it in RAM) */
        case 0x46c0:
            if (a <= 0x46c3) adc_channel = a & 3;
            break;
        case 0x46e0:                              /* sound CPU reset */
            DBG(sw_dbg_soundrst++; printf("  SOUNDRST at %.2fs (main pc %04X, flags %02X, sound pc %04X)\n", (double)total_cycles / SW_CPU_CLOCK, e6809_get_pc() & 0xffff, sound_enabled ? snd_ready_flags() : 0, sound_enabled ? snd_pc() : 0); if (sound_enabled) snd_dbg_dump();)
            if (sound_enabled) snd_cpu_reset();
            soundrst_count++;
            break;
        case 0x4700:
            if (a <= 0x4707) math_w(a & 7, d);
            break;
        default: break;
    }
}

static inline __attribute__((always_inline)) unsigned char cpu_read(unsigned addr)
{
    uint16_t a = (uint16_t)addr;
#ifdef SW_DEBUG
    if (a == 0xfff8) { sw_dbg_irq_taken++; if (sw_dbg_trace_arm) { sw_dbg_trace_arm = 0; trace_active = 1; sw_dbg_trace_n = 0; } }
    if (a == 0x483d) sw_dbg_r483d++;
#endif
    const uint8_t *p = rpage[a >> 8];
    if (p) return p[a & 0xff];
    if (is_esb && (a & 0xe000) == 0x8000) {          /* slapstic window: the byte comes from the bank in force, then the chip sees the access */
        uint8_t v = roms.rom_slapstic[((unsigned)slapstic_current_bank << 13) | (a & 0x1fff)];
        slapstic_tweak(a & 0x1fff);
        return v;
    }
    if (a >= 0x4300 && a < 0x4800) return io_read(a);
    return 0xff;
}

static inline __attribute__((always_inline)) void cpu_write(unsigned addr, unsigned char d)
{
    uint16_t a = (uint16_t)addr;
#ifdef SW_DEBUG
    if (a == 0x483d) { sw_dbg_w483d++; sw_dbg_w483d_pc = (uint16_t)e6809_get_pc(); sw_dbg_w483d_val = d; }
#endif
    uint8_t *p = wpage[a >> 8];
    if (p) { p[a & 0xff] = d; return; }
    if (is_esb && (a & 0xe000) == 0x8000) { slapstic_tweak(a & 0x1fff); return; }
    if (a >= 0x4300 && a < 0x4800) io_write(a, d);
}

/* ---- public API ---- */
void sw_init(const sw_roms_t *r)
{
    roms = *r;
    is_esb = roms.rom_slapstic != NULL;
    map_init();
    memset(&input, 0, sizeof(input));
    input.pitch = 0x80;
    input.yaw = 0x80;
    memset(nvram, 0, sizeof(nvram));
    mproc_init();
    avg_init(&avg, roms.prom_avg, ram_vec, roms.rom_vector);
    sw_reset();
}

void sw_reset(void)
{
    memset(ram_vec, 0, sizeof(ram_vec));
    memset(ram_main, 0, sizeof(ram_main));
    memset(mathram, 0, sizeof(mathram));
    bank_sel = 0;
    map_bank();
    if (is_esb) slapstic_reset();
    adc_channel = 0;
    total_cycles = 0;
    next_irq_at = SW_CPU_CYCLES_PER_IRQ;
    irq_line = 0;
    irq_count = 0;
    frame_count = 0;
    avg_done_at = 0;
    math_done_at = 0;
    MPA = BIC = 0;
    mA = mB = mC = 0; mACC = 0;
    avg_reset(&avg);
    e6809_reset();
}

#ifdef SW_DEBUG
static void dbg_pre_step(void)
{
    sw_dbg_pc_hist[e6809_get_pc() & 0xffff]++;
    sw_dbg_bank_hist[bank_sel & 1][(e6809_get_pc() & 0xffff) >> 13]++;
    if (trace_active) { if (sw_dbg_trace_n < 2000) sw_dbg_trace[sw_dbg_trace_n++] = (uint16_t)e6809_get_pc(); else trace_active = 0; }
    if (!sw_dbg_ring_frozen) {              /* rolling PC history, frozen at the first sight of sw_dbg_halt_pc */
        unsigned rpc = e6809_get_pc() & 0xffff;
        sw_dbg_ring[sw_dbg_ring_i++ & (SW_DBG_RING - 1)] = (uint16_t)rpc;
        if (rpc == sw_dbg_halt_pc) sw_dbg_ring_frozen = 1;
    }
}
#endif

/* Main-program wait loops (bank 0 of 136021.214, always mapped while they run):
 *   0x6004-0x6011  LSR $483D / BCC   waits for the 20 Hz tick the IRQ handler raises
 *   0x6032-0x6035  LDA $483F / BMI   waits for the IRQ handler to start the vector list
 * Both only advance from the IRQ handler, so with no interrupt pending nothing
 * observable happens until the next one: skip straight to it. */
/* ESB (136031): 0xA331-0xA334 LDA <$46 / BMI in page 0 of 0xA000-0xFFFF waits for the IRQ
 * handler to start the vector list; 0x6287-0x628B TST $4320 / BMI in page 1 of 0x6000-0x7FFF
 * waits for the matrix processor. */
static inline int in_wait_loop(unsigned pc)
{
#ifdef ESB_NO_WAIT
    if (is_esb) return 0;
#endif
    if (is_esb) return pc >= 0xa331 && pc <= 0xa334;
    return (pc >= 0x6004 && pc <= 0x6012) || (pc >= 0x6032 && pc <= 0x6035);
}
/* 0xCDBD-0xCDC1: TST $4320 / BMI  waits for the matrix processor (MATH_RUN clear) */
static inline int in_math_wait_loop(unsigned pc)
{
#ifdef ESB_NO_MATH
    if (is_esb) return 0;
#endif
    if (is_esb) {                    /* TST $4320 / BMI loops: 6287 and D98E on page 1, B33B on page 0 */
        if (bank_sel) return (pc >= 0x6287 && pc <= 0x628b) || (pc >= 0xd98e && pc <= 0xd992);
        return pc >= 0xb33b && pc <= 0xb33f;
    }
    return pc >= 0xcdbd && pc <= 0xcdc1;
}
static uint32_t main_idle_skipped;
static inline int at_wait_loop(void)
{
    unsigned pc = reg_pc & 0xffff;
    return in_wait_loop(pc) || in_math_wait_loop(pc);
}
uint32_t sw_idle_skipped(void) { uint32_t v = main_idle_skipped; main_idle_skipped = 0; return v; }

void sw_attach_sound(const uint8_t *rom_sound, unsigned size)
{
    snd_init(rom_sound, size);
    sound_enabled = 1;
}

uint32_t sw_run(uint32_t cycles)
{
    uint32_t run = 0;
    static int32_t snd_balance;   /* main-CPU cycles the sound CPU still has to run (may go negative) */
    while (run < cycles) {
        if (total_cycles >= next_irq_at) {
            irq_line = 1;
            irq_count++;
            next_irq_at += SW_CPU_CYCLES_PER_IRQ;
        }
        /* run up to the next interrupt edge, in chunks that keep the sound CPU in step */
        uint32_t chunk = cycles - run;
        uint64_t to_irq = next_irq_at - total_cycles;
        if (to_irq < chunk) chunk = (uint32_t)to_irq;
        /* The sound CPU normally runs in 256-cycle steps behind the main CPU. While a sound
         * command is waiting in the latch, step in 32s instead: the real board answers a
         * command within a few cycles, and the main program's "was my last command taken"
         * check is sensitive to that. */
        uint32_t lim = (sound_enabled && (snd_ready_flags() & 0x80)) ? 32 : 256;
        if (chunk > lim) chunk = lim;
        if (chunk == 0) chunk = 1;
        unsigned c;
        unsigned pc = e6809_get_pc() & 0xffff;
        uint32_t skip = 0;
#ifdef SW_NOSKIP
        if (0) {
#else
        if (!irq_line) {
#endif
            if (!bank_sel && in_wait_loop(pc)) {
                skip = chunk;                                   /* bounded by the next IRQ above */
            } else if (in_math_wait_loop(pc) && math_done_at > total_cycles) {
                uint64_t to_math = math_done_at - total_cycles;
                skip = (to_math < chunk) ? (uint32_t)to_math : chunk;
            }
        }
        if (skip > 24) {
            c = skip - 16;               /* leave a few cycles so the loop observes the event normally */
            main_idle_skipped += c;
            DBG(if (sw_dbg_log_flags && total_cycles > 26700000 && total_cycles < 26900000) printf("  SKIP %u at cyc %llu pc %04X\n", c, (unsigned long long)total_cycles, pc);)
        } else {
            c = e6809_run(chunk);
        }
        total_cycles += c;
        run += c;
        if (sound_enabled) {
            snd_balance += (int32_t)c;
            /* a freshly latched command: run the sound CPU now, whatever the balance, so it is
             * taken within the ~200 cycles the main program allows before it gives up */
            if (snd_balance >= (int32_t)lim || snd_sync_break) { if (snd_balance > 0) snd_balance -= (int32_t)snd_run((uint32_t)snd_balance); }
            snd_sync_break = 0;
        }
    }
    if (sound_enabled && snd_balance > 0) snd_balance -= (int32_t)snd_run((uint32_t)snd_balance);
    return run;
}

sw_input_t *sw_input(void) { return &input; }
void sw_set_dips(uint8_t d0, uint8_t d1) { dsw0 = d0; dsw1 = d1; }
void sw_set_frame_callback(sw_frame_cb_t cb, void *user) { frame_cb = cb; frame_user = user; }
void sw_set_time_source(uint64_t (*now_us)(void)) { time_src = now_us; }
sw_stats_t *sw_stats(void) { return &stats; }
uint64_t sw_total_cycles(void) { return total_cycles; }
uint32_t sw_irq_count(void) { return irq_count; }
uint32_t sw_frame_count(void) { return frame_count; }
uint8_t sw_nvram_read(int idx) { return nvram[idx & 0xff]; }
const uint8_t *sw_ram(void) { return ram_vec; }
uint16_t sw_pc(void) { return (uint16_t)e6809_get_pc(); }
#ifdef SW_DEBUG
uint8_t sw_dbg_peek(uint16_t a) { return cpu_read(a); }
#endif
