/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * polepos.c - machine core (see polepos.h)
 */
#include "polepos_internal.h"
#include <stdlib.h>
#include "Z80.h"
#include "z8002.h"
#include <string.h>

static pp_roms_t R;
static uint16_t rom_sub_w[2][0x4000];        /* sub ROMs as native-endian words */
static const uint16_t *rpage_sub[2][256];    /* Z8002 read page tables (words) */
static Z80 z80;
static z8k_t sub[2];
static int cur_sub;
uint16_t pp_sprite16[0x800], pp_road16[0x400], pp_alpha16[0x400], pp_view16[0x800];
uint16_t pp_hscroll, pp_road_vscroll;
uint8_t pp_chacl;
static uint8_t nvram[0x800];
static uint8_t sound_ram[0x3c0];
static uint8_t latch;                 /* 0xA000-0xA007 */
static uint8_t dswa = 0xff, dswb = 0x6b;
static pp_input_t input;
static uint32_t frame_count;
static int scanline;
static uint8_t z80_irq_pending;
static uint8_t sub_irq_mask;
static uint8_t adc_done, adc_value;
static uint32_t idle_cycles[3];
static uint8_t n06_ctrl;
static uint64_t (*time_src)(void);
static pp_stats_t stats;
#define TNOW() (time_src ? time_src() : 0)
#ifdef PP_DEBUG
#include <stdio.h>
uint32_t pp_dbg_frame;
uint32_t pp_dbg_sub_hist[2][0x10000];
uint32_t pp_dbg_irq, pp_dbg_nmi, pp_dbg_nvi, pp_dbg_n51r, pp_dbg_n53r, pp_dbg_ctrlw, pp_dbg_adc_w, pp_dbg_adc_r, pp_dbg_pc_hist[0x10000];
static void dbg_log_read(const char *what, uint8_t v) { static int n; if (pp_dbg_frame >= 322 && pp_dbg_frame < 328 && (n06_ctrl & 0x60) != 0x60 && n++ < 60) printf("  06XX %s -> %02X (z80 pc %04X)\n", what, v, z80.PC.W); }
static int trace_n; static int cur_line_dbg;
#define TRACE(...) do { if (trace_n++ < 200) { printf("  [f%u l%d] ", pp_dbg_frame, cur_line_dbg); printf(__VA_ARGS__); } } while (0)
#define DBG(x) x
#else
#define DBG(x)
#endif

/* 06XX */
static int32_t n06_nmi_countdown = -1;
#define N06_NMI_PERIOD 614

/* 51XX (switch mode only in Pole Position) */
static struct { int mode, in_count, credits, coins[2], coins_per_cred[2], creds_per_coin[2], coincred_mode, remap_joy, lastcoins, lastbuttons; } n51;
/* 53XX */
static int n53_count;

static uint8_t in0(void)
{
    uint8_t v = 0xff;
    if (input.gear) v &= ~0x02;
    if (!(latch & 0x40)) v &= ~0x04;      /* auto start: active while SB0 is low */
    if (input.coin1) v &= ~0x10;
    if (input.coin2) v &= ~0x20;
    if (input.service) v &= ~0x40;
    if (input.test) v &= ~0x80;
    return v;
}

/* ---- 51XX ---- */
static void n51_write(uint8_t d)
{
    d &= 7;
    if (n51.coincred_mode) {
        switch (n51.coincred_mode--) {
            case 4: n51.coins_per_cred[0] = d; break;
            case 3: n51.creds_per_coin[0] = d; break;
            case 2: n51.coins_per_cred[1] = d; break;
            case 1: n51.creds_per_coin[1] = d; break;
        }
        return;
    }
    switch (d) {
        case 1: n51.coincred_mode = 4; n51.credits = 0; break;
        case 2: n51.mode = 1; n51.in_count = 0; break;
        case 3: n51.remap_joy = 0; break;
        case 4: n51.remap_joy = 1; break;
        case 5: n51.mode = 0; n51.in_count = 0; break;
        default: break;
    }
}
/* Port wiring (pre-MCU MAME driver): p0 = IN0 low nibble, p1 = IN0 high nibble,
 * p2 = DSWB low nibble, p3 = DSWB high nibble. Pole Position sets coinage, then
 * credit mode, and reads back the credit count in BCD. */
static uint8_t n51_p0(void) { return in0() & 0x0f; }
static uint8_t n51_p1(void) { return in0() >> 4; }
static uint8_t n51_p2(void) { return dswb & 0x0f; }
static uint8_t n51_p3(void) { return dswb >> 4; }
static const uint8_t joy_map[16] = { 0xf, 0xe, 0xd, 0x5, 0xc, 0x9, 0x7, 0x6, 0xb, 0x3, 0xa, 0x4, 0x1, 0x2, 0x0, 0x8 };

static uint8_t n51_read(void)
{
    if (n51.mode == 0) {                           /* switch mode */
        switch ((n51.in_count++) % 3) {
            default:
            case 0: return (uint8_t)(n51_p0() | (n51_p1() << 4));
            case 1: return (uint8_t)(n51_p2() | (n51_p3() << 4));
            case 2: return 0;
        }
    }
    switch ((n51.in_count++) % 3) {                /* credit mode */
        default:
        case 0: {
            int in = ~(n51_p0() | (n51_p1() << 4)) & 0xff;
            int toggle = in ^ n51.lastcoins;
            n51.lastcoins = in;
            if (n51.coins_per_cred[0] > 0) {
                if (n51.credits < 99) {
                    if (toggle & in & 0x10) {
                        if (++n51.coins[0] >= n51.coins_per_cred[0]) { n51.credits += n51.creds_per_coin[0]; n51.coins[0] -= n51.coins_per_cred[0]; }
                    }
                    if (toggle & in & 0x20) {
                        if (++n51.coins[1] >= n51.coins_per_cred[1]) { n51.credits += n51.creds_per_coin[1]; n51.coins[1] -= n51.coins_per_cred[1]; }
                    }
                    if (toggle & in & 0x40) n51.credits++;
                }
            } else {
                n51.credits = 100;                 /* free play */
            }
            if (n51.mode == 1) {
                if (toggle & in & 0x04) { if (n51.credits >= 1) { n51.credits--; n51.mode = 2; } }
                else if (toggle & in & 0x08) { if (n51.credits >= 2) { n51.credits -= 2; n51.mode = 2; } }
            }
            if (~n51_p1() & 0x08) return 0xbb;      /* test switch */
            return (uint8_t)((n51.credits / 10) * 16 + n51.credits % 10);
        }
        case 1: {
            int joy = n51_p2() & 0x0f;
            int in = ~n51_p0() & 0x0f;
            int toggle = in ^ n51.lastbuttons;
            n51.lastbuttons = (n51.lastbuttons & 2) | (in & 1);
            if (n51.remap_joy) joy = joy_map[joy];
            joy |= ((toggle & in & 0x01) ^ 1) << 4;
            joy |= ((in & 0x01) ^ 1) << 5;
            return (uint8_t)joy;
        }
        case 2: {
            int joy = n51_p3() & 0x0f;
            int in = ~n51_p0() & 0x0f;
            int toggle = in ^ n51.lastbuttons;
            n51.lastbuttons = (n51.lastbuttons & 1) | (in & 2);
            if (n51.remap_joy) joy = joy_map[joy];
            joy |= ((toggle & in & 0x02) ^ 2) << 3;
            joy |= ((in & 0x02) ^ 2) << 4;
            return (uint8_t)joy;
        }
    }
}

/* ---- 53XX: steering wheel position and DSWA (pre-MCU model: ports in1_l, in1_h, dipA_l, dipA_h) ---- */
/* The wheel is a quadrature encoder that the MCU counts; each Z80 read sees the count move by at
 * most one step (MAME's model hands over one count per read). The tilt control can jump the
 * requested position by tens of counts in one frame, and the game's steering code does not
 * cope with that, so the reported position walks toward the requested one a count per read. */
static uint8_t n53_steer_out;
static uint8_t n53_read(void)
{
    switch ((n53_count++) % 8) {
        case 0: {
            DBG(if (getenv("STEER_RAW")) return input.steer;)
            int8_t d = (int8_t)(input.steer - n53_steer_out);
            if (d > 0) n53_steer_out++; else if (d < 0) n53_steer_out--;
            return n53_steer_out;
        }
        case 4: return dswa;
        default: return 0xff;      /* polepos2 hangs if 0 is returned */
    }
}

/* ---- Z80 bus ---- */
static uint8_t z80_io_read(uint16_t a)
{
    switch ((a >> 8) & 3) {
        case 0: {                                   /* READY */
            uint8_t r = 0xff;
            if (scanline >= 128) r ^= 0x02;
            if (adc_done) r ^= 0x08;
            return r;
        }
        default: return 0xff;
    }
}

static void latch_write(int bit, int v)
{
    uint8_t old = latch;
    if (v) latch |= (uint8_t)(1 << bit); else latch &= (uint8_t)~(1 << bit);
    switch (bit) {
        case 0: if (!v) { z80_irq_pending = 0; z80.IRequest = INT_NONE; } break;
        case 1: if (!v) { memset(&n51, 0, sizeof(n51)); n53_count = 0; } break;   /* custom chip reset */
        case 2: pp_sound_enable(v); if (!v) { pp_engine_lsb(0); pp_engine_msb(0); } break;
        case 4: if (v && !(old & 0x10)) z8k_reset(&sub[0]); break;
        case 5: if (v && !(old & 0x20)) z8k_reset(&sub[1]); break;
        case 7: pp_chacl = (uint8_t)v; break;
        default: break;
    }
}

byte RdZ80(register word a)
{
    DBG(if (a == 0x0992) { static int n; if (n++ < 80) printf("  [f%u] step -> %04X\n", pp_dbg_frame, z80.HL.W); })
    if (a < 0x3000) return R.rom_z80[a];
    DBG(if (a >= 0x4048 && a <= 0x404b && z80.PC.W != 0x1046 && z80.PC.W != 0x0ea7) TRACE("z80 rd %04X -> %02X (pc %04X)\n", a, pp_sprite16[a - 0x4000] & 0xff, z80.PC.W);)
    if (a < 0x4000) return nvram[a & 0x7ff];
    if (a < 0x4800) return pp_sprite16[a - 0x4000] & 0xff;
    if (a < 0x4c00) return pp_road16[a - 0x4800] & 0xff;
    if (a < 0x5000) return pp_alpha16[a - 0x4c00] & 0xff;
    if (a < 0x5800) return pp_view16[a - 0x5000] & 0xff;
    if (a >= 0x8000 && a < 0x9000) {
        int i = a & 0x3ff;
        return (i < 0x3c0) ? sound_ram[i] : pp_wsg_read(i - 0x3c0);
    }
    if (a >= 0x9000 && a < 0xa000) {
        if (a & 0x100) return n06_ctrl;
        if (!(n06_ctrl & 0x10)) return 0;
        uint8_t r = 0xff;
        if (n06_ctrl & 0x01) { r &= n51_read(); DBG(pp_dbg_n51r++; dbg_log_read("51xx", r);) }
        if (n06_ctrl & 0x02) { r &= n53_read(); DBG(pp_dbg_n53r++; dbg_log_read("53xx", r);) }
        return r;
    }
    if (a >= 0xa000 && a < 0xb000) return z80_io_read(a);
    return 0xff;
}

void WrZ80(register word a, register byte d)
{
    DBG(if (a >= 0x4048 && a <= 0x404b && z80.PC.W != 0x102b) TRACE("z80 wr %04X <- %02X (pc %04X)\n", a, d, z80.PC.W);)
    DBG(if (a >= 0x4c00 && a < 0x5000 && pp_dbg_frame >= 300 && pp_dbg_frame < 330) { static int n; if (n++ < 12) printf("  [f%u] z80 alpha wr %04X <- %02X (pc %04X)\n", pp_dbg_frame, a, d, z80.PC.W); })
    DBG(if (a >= 0x4c00 && a < 0x5000 && pp_dbg_frame >= 242 && pp_dbg_frame < 300 && z80.PC.W != 0x09ef) { static int m; if (m++ < 40) printf("  [f%u] z80 alpha wr(early) %04X <- %02X (pc %04X)\n", pp_dbg_frame, a, d, z80.PC.W); })
    DBG(if (a >= 0xa000 && a < 0xb000 && ((a >> 8) & 3) == 0) TRACE("z80 latch bit%d <- %d (pc %04X)\n", a & 7, d & 1, z80.PC.W);)
    if (a >= 0x3000 && a < 0x4000) { nvram[a & 0x7ff] = d; return; }
    if (a >= 0x4000 && a < 0x4800) { pp_sprite16[a - 0x4000] = (pp_sprite16[a - 0x4000] & 0xff00) | d; return; }
    if (a >= 0x4800 && a < 0x4c00) { pp_road16[a - 0x4800] = (pp_road16[a - 0x4800] & 0xff00) | d; return; }
    if (a >= 0x4c00 && a < 0x5000) { pp_alpha16[a - 0x4c00] = (pp_alpha16[a - 0x4c00] & 0xff00) | d; return; }
    if (a >= 0x5000 && a < 0x5800) { pp_view16[a - 0x5000] = (pp_view16[a - 0x5000] & 0xff00) | d; return; }
    if (a >= 0x8000 && a < 0x9000) {
        int i = a & 0x3ff;
        if (i < 0x3c0) sound_ram[i] = d; else pp_wsg_write(i - 0x3c0, d);
        return;
    }
    if (a >= 0x9000 && a < 0xa000) {
        if (a & 0x100) {
            DBG({ static int n; if (pp_dbg_frame >= 322 && pp_dbg_frame < 328 && d != 0x10 && d != 0x71 && d != 0x72 && d != 0x88 && n++ < 30) printf("  [f%u] 06XX ctrl <- %02X (z80 pc %04X)\n", pp_dbg_frame, d, z80.PC.W); })
            n06_ctrl = d;
            n06_nmi_countdown = ((d & 0x0f) == 0) ? -1 : N06_NMI_PERIOD;
        } else if (!(n06_ctrl & 0x10)) {
            DBG({ static int n; if (pp_dbg_frame >= 322 && pp_dbg_frame < 328 && n06_ctrl != 0x88 && n++ < 30) printf("  [f%u] 06XX data <- %02X (ctrl %02X) n51 mode %d\n", pp_dbg_frame, d, n06_ctrl, n51.mode); })
            if (n06_ctrl & 0x01) n51_write(d);
            if (n06_ctrl & 0x04) pp_n52_write(d);
            if (n06_ctrl & 0x08) pp_n54_write(d);
        }
        return;
    }
    if (a >= 0xa000 && a < 0xb000) {
        switch ((a >> 8) & 3) {
            case 0: latch_write(a & 7, d & 1); break;
            case 1: break;                                    /* watchdog */
            case 2: pp_engine_lsb(d); break;
            case 3: pp_engine_msb(d); break;
        }
        return;
    }
}

byte InZ80(register word p)
{
    if ((p & 0xff) == 0) { DBG(pp_dbg_adc_r++;) adc_done = 0; return adc_value; }
    return 0xff;
}
void OutZ80(register word p, register byte v)
{
    (void)v;
    if ((p & 0xff) == 0) {                   /* start a conversion of the selected pedal */
        adc_value = (latch & 0x08) ? input.accel : input.brake;
        adc_done = 1;
        DBG(pp_dbg_adc_w++;)
    }
}
void PatchZ80(register Z80 *r) { (void)r; }
word LoopZ80(register Z80 *r) { (void)r; return INT_QUIT; }

/* ---- Z8002 bus (both CPUs share everything except their ROM and NVI enable) ---- */
static inline uint16_t *sub_ram_word(uint32_t a)
{
    if (a < 0x9000) return &pp_sprite16[(a - 0x8000) >> 1];
    if (a < 0x9800) return &pp_road16[(a - 0x9000) >> 1];
    if (a < 0xa000) return &pp_alpha16[(a - 0x9800) >> 1];
    if (a < 0xb000) return &pp_view16[(a - 0xa000) >> 1];
    return 0;
}
static const uint16_t open_bus_page[0x80] = { [0 ... 0x7f] = 0xffff };
static uint16_t *wpage_sub[0x100];            /* write page table (same for both CPUs; special pages NULL) */
static void sub_pages_init(void)
{
    for (int pg = 0x80; pg < 0xb0; pg++) wpage_sub[pg] = sub_ram_word((uint32_t)pg << 8);
    z8k_wpage = wpage_sub;
    for (int c = 0; c < 2; c++) {
        for (int i = 0; i < 0x4000; i++) {
            const uint8_t *rom = c ? R.rom_sub2 : R.rom_sub1;
            rom_sub_w[c][i] = (uint16_t)((rom[2 * i] << 8) | rom[2 * i + 1]);
        }
        memset(rpage_sub[c], 0, sizeof(rpage_sub[c]));
        for (int pg = 0; pg < 0x80; pg++) rpage_sub[c][pg] = rom_sub_w[c] + (pg << 7);
        for (int pg = 0x80; pg < 0x90; pg++) rpage_sub[c][pg] = pp_sprite16 + ((pg - 0x80) << 7);
        for (int pg = 0x90; pg < 0x98; pg++) rpage_sub[c][pg] = pp_road16 + ((pg - 0x90) << 7);
        for (int pg = 0x98; pg < 0xa0; pg++) rpage_sub[c][pg] = pp_alpha16 + ((pg - 0x98) << 7);
        for (int pg = 0xa0; pg < 0xb0; pg++) rpage_sub[c][pg] = pp_view16 + ((pg - 0xa0) << 7);
        for (int pg = 0xb0; pg < 0x100; pg++) rpage_sub[c][pg] = open_bus_page;
    }
    z8k_rpage = rpage_sub[0];
}
uint16_t z8k_rw(uint32_t a)
{
    a &= 0xfffe;
    DBG(if (0 && a >= 0x8090 && a <= 0x8094 && sub[cur_sub].pc != 0x34cc && sub[cur_sub].pc != 0x29c4 && sub[cur_sub].pc != 0x34c0 && sub[cur_sub].pc != 0x34c6 && sub[cur_sub].pc != 0x34c4 && sub[cur_sub].pc != 0x34ca && sub[cur_sub].pc != 0x29c8) { uint16_t *w = sub_ram_word(a); TRACE("sub%d rd %04X -> %04X (pc %04X)\n", cur_sub + 1, a, w ? *w : 0, sub[cur_sub].pc); })
    if (a < 0x8000) return rom_sub_w[cur_sub][a >> 1];
    uint16_t *w = sub_ram_word(a);
    return w ? *w : 0xffff;
}
uint8_t z8k_rb(uint32_t a)
{
    uint16_t w = z8k_rw(a & 0xfffe);
    return (a & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
}
void z8k_ww(uint32_t a, uint16_t d)
{
    a &= 0xfffe;
    DBG(if (a >= 0x8090 && a <= 0x8096) TRACE("sub%d wr %04X <- %04X (pc %04X)\n", cur_sub + 1, a, d, sub[cur_sub].pc);)
    DBG(if (a >= 0x6000 && a < 0x8000) TRACE("sub%d NVI enable <- %d (pc %04X)\n", cur_sub + 1, d & 1, sub[cur_sub].pc);)
    if (a >= 0x6000 && a < 0x8000) {
        sub_irq_mask = d & 1;
        if (!sub_irq_mask) z8k_set_nvi(z8k_live(), 0);      /* the writing CPU is the one running */
        return;
    }
    if (a >= 0xc000) { if (a & 0x100) pp_road_vscroll = d; else pp_hscroll = d; return; }
    uint16_t *w = sub_ram_word(a);
    if (w) *w = d;
}
void z8k_wb(uint32_t a, uint8_t d)
{
    DBG(if (a >= 0x8090 && a <= 0x8097) TRACE("sub%d wrb %04X <- %02X (pc %04X)\n", cur_sub + 1, a, d, sub[cur_sub].pc);)
    if (a >= 0x6000 && a < 0x8000) { z8k_ww(a, d); return; }
    if (a >= 0xc000) { z8k_ww(a, d); return; }
    uint16_t *w = sub_ram_word(a & 0xfffe);
    if (!w) return;
    if (a & 1) *w = (*w & 0xff00) | d; else *w = (*w & 0x00ff) | (uint16_t)(d << 8);
}
uint8_t z8k_in(uint16_t p) { (void)p; return 0xff; }
void z8k_out(uint16_t p, uint8_t d) { (void)p; (void)d; }

/* ---- public ---- */
void pp_init(const pp_roms_t *r)
{
    R = *r;
    z8k_init_tables();
    sub_pages_init();
    pp_video_init(&R);
    pp_sound_init(&R);
    memset(nvram, 0xff, sizeof(nvram));
    pp_reset();
}

void pp_reset(void)
{
    memset(pp_sprite16, 0, sizeof(pp_sprite16)); memset(pp_road16, 0, sizeof(pp_road16));
    memset(pp_alpha16, 0, sizeof(pp_alpha16)); memset(pp_view16, 0, sizeof(pp_view16));
    memset(sound_ram, 0, sizeof(sound_ram));
    pp_hscroll = pp_road_vscroll = 0; pp_chacl = 0;
    latch = 0; scanline = 0; z80_irq_pending = 0; sub_irq_mask = 0; adc_done = 0;
    n06_ctrl = 0; n06_nmi_countdown = -1;
    memset(&n51, 0, sizeof(n51)); n53_count = 0; n53_steer_out = 128;
    ResetZ80(&z80); z80.IAutoReset = 1; z80.TrapBadOps = 0;
    z8k_reset(&sub[0]); z8k_reset(&sub[1]);
    pp_sound_reset();
}

void pp_set_dips(uint8_t a, uint8_t b) { dswa = a; dswb = b; }
pp_input_t *pp_input(void) { return &input; }

static int32_t z80_debt, sub_debt[2];
static void run_z80(int32_t cycles)
{
    cycles -= z80_debt; z80_debt = 0;
    if (cycles <= 0) { z80_debt = -cycles; return; }
    if (z80_irq_pending && (z80.IFF & IFF_1)) { IntZ80(&z80, INT_IRQ); z80_irq_pending = 0; DBG(pp_dbg_irq++;) }
    DBG(pp_dbg_pc_hist[z80.PC.W]++;)
    DBG({ static uint16_t prev_pc; static int reported; uint16_t pc = z80.PC.W;
          if ((pc == 0x0000 || pc == 0x009b || pc == 0x0010) && reported < 8) { printf("  [f%u l%d] z80 RESTART at %04X, previous slice pc %04X, SP %04X, IFF %02X\n", pp_dbg_frame, cur_line_dbg, pc, prev_pc, z80.SP.W, z80.IFF); reported++; }
          prev_pc = pc; })
    if (z80.IFF & IFF_HALT) { idle_cycles[0] += cycles; return; }
    if (z80.PC.W >= 0x0a5f && z80.PC.W <= 0x0a63 && !z80_irq_pending) { idle_cycles[0] += cycles; return; }   /* waits for the IRQ tick */
    /* Main loop (00B1: call 0278 / jr 00B1) polls the sub-CPU mailbox: it only does work when
     * byte 4010 == 73 and byte 4018 == 0, otherwise it just rewrites 4018 with the value it already
     * has. Nothing in this slice can change those bytes but the other CPUs, which run between slices. */
    if ((z80.PC.W == 0x00b1 || z80.PC.W == 0x00b4) && !z80_irq_pending) {
        uint8_t st = RdZ80(0x4010), busy = RdZ80(0x4018);
        if ((st != 0x73) ? (busy == 0) : (busy != 0)) { idle_cycles[0] += cycles; return; }
    }
    z80.IPeriod = cycles; z80.ICount = cycles;
    RunZ80(&z80);
    int32_t over = cycles - z80.ICount;
    if (over > 0) z80_debt = over;
}
static void run_sub(int i, int32_t cycles)
{
    if (!(latch & (0x10 << i))) return;          /* held in reset */
    cycles -= sub_debt[i]; sub_debt[i] = 0;
    if (cycles <= 0) { sub_debt[i] = -cycles; return; }
    if (sub[i].halt && !(sub[i].irq_req)) { idle_cycles[1 + i] += cycles; return; }
    cur_sub = i;
    z8k_rpage = rpage_sub[i];
#ifdef PP_DEBUG
    pp_dbg_sub_hist[i][sub[i].pc & 0xffff]++;
    static int traced[2];
    if (getenv("TRACESUB") && traced[i] < 260) {
        int ran = 0;
        while (ran < cycles && traced[i] < 260) {
            printf("  [f%u] sub%d pc=%04X op=%04X R0=%04X R1=%04X fcw=%04X\n", pp_dbg_frame, i + 1, sub[i].pc, z8k_rw(sub[i].pc), sub[i].regs.W[0 ^ 3], sub[i].regs.W[1 ^ 3], sub[i].fcw);
            ran += z8k_run(&sub[i], 1);
            traced[i]++;
        }
        return;
    }
#endif
    int ran = z8k_run(&sub[i], cycles);
    if (ran > cycles) sub_debt[i] = ran - cycles;
}

void pp_run_frame(void)
{
    DBG(pp_dbg_frame = frame_count; if (0) printf("  [f%u] pcs z80 %04X sub1 %04X sub2 %04X latch %02X w48=%04X w49=%04X w4A=%04X\n", frame_count, z80.PC.W, (unsigned)sub[0].pc, (unsigned)sub[1].pc, latch, pp_sprite16[0x48], pp_sprite16[0x49], pp_sprite16[0x4a]);)
    for (scanline = 0; scanline < PP_LINES; scanline++) {
        DBG(cur_line_dbg = scanline;)
        if ((scanline == 64 || scanline == 192) && (latch & 1)) z80_irq_pending = 1;
        if (scanline == 240 && sub_irq_mask) { z8k_set_nvi(&sub[0], 1); z8k_set_nvi(&sub[1], 1); DBG(pp_dbg_nvi++;) }
        if (n06_nmi_countdown >= 0) {
            n06_nmi_countdown -= PP_CYCLES_PER_LINE;
            if (n06_nmi_countdown < 0) { n06_nmi_countdown += N06_NMI_PERIOD; IntZ80(&z80, INT_NMI); DBG(pp_dbg_nmi++;) }
        }
        if (scanline % PP_SLICE_LINES) continue;
        uint64_t t0 = TNOW();
        run_z80(PP_CYCLES_PER_LINE * PP_SLICE_LINES);
        uint64_t t1 = TNOW();
        run_sub(0, PP_CYCLES_PER_LINE * PP_SLICE_LINES);
        uint64_t t2 = TNOW();
        run_sub(1, PP_CYCLES_PER_LINE * PP_SLICE_LINES);
        uint64_t t3 = TNOW();
        stats.cpu_us[0] += t1 - t0; stats.cpu_us[1] += t2 - t1; stats.cpu_us[2] += t3 - t2;
    }
    frame_count++;
}

void pp_render(uint8_t *fb) { pp_video_render(fb); }
void pp_render_audio(int16_t *buf, int samples, int rate) { pp_sound_render(buf, samples, rate); }
void pp_set_time_source(uint64_t (*f)(void)) { time_src = f; }
pp_stats_t *pp_stats(void) { return &stats; }
uint16_t pp_pc(int c) { return c == 0 ? z80.PC.W : (uint16_t)sub[c - 1].pc; }
uint32_t pp_frame_count(void) { return frame_count; }
uint32_t pp_idle_cycles(int c) { uint32_t v = idle_cycles[c]; idle_cycles[c] = 0; return v; }
uint8_t pp_latch(void) { return latch; }
const uint8_t *pp_nvram(void) { return nvram; }
void pp_nvram_load(const uint8_t *d) { memcpy(nvram, d, sizeof(nvram)); }

/* the byte at a CPU address, where that is memory - for keeping the high scores */
uint8_t *pp_mem(uint16_t a)
{
    if (a >= 0x3000 && a < 0x3800) return &nvram[a & 0x7ff];
    return 0;
}
