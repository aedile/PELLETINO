/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * galaga.c - machine core (see galaga.h)
 */
#include "galaga.h"
#include "galaga_internal.h"
#include "Z80.h"
#include <string.h>
#include <stdio.h>

/* ---- state ---- */
static ga_roms_t roms;
static Z80 cpu[3];
static int cur_cpu;
static uint8_t vram[0x800];
uint8_t ga_ram1[0x400], ga_ram2[0x400], ga_ram3[0x400];   /* shared with video */
uint8_t ga_videoram[0x800];
uint8_t ga_videolatch;                                    /* 0xA000-0xA007 bits */
static uint8_t dswa = 0xf7, dswb = 0x97;
static ga_input_t input;
static uint32_t frame_count;

/* misc latch 0x6820-0x6827 */
static uint8_t main_irq_en, sub_irq_en, sub2_nmi_en, subs_running;
static uint8_t irq_pending[3];      /* vblank IRQ asserted, not yet taken (CPU3 uses NMI only) */

/* 06XX */
static uint8_t n06_ctrl;
static int32_t n06_nmi_countdown;   /* cycles until next NMI while active */
#define N06_NMI_PERIOD 614          /* 200 us at 3.072 MHz */

/* 51XX high level model (protocol from the old MAME HLE) */
static struct {
    int mode;               /* 0 switch mode, 1 credit mode, 2 game running */
    int in_count;
    int credits;
    int coins[2], coins_per_cred[2], creds_per_coin[2];
    int coincred_mode;
    int remap_joy;
    int lastcoins, lastbuttons;
} n51;

static uint32_t halt_cycles[3];

/* 256-byte page tables per CPU for direct memory (NULL = I/O or unmapped) */
static const uint8_t *rpage[3][256];
static uint8_t *wpage[256];

static void map_init(void)
{
    memset(rpage, 0, sizeof(rpage));
    memset(wpage, 0, sizeof(wpage));
    for (int pg = 0; pg < 0x40; pg++) rpage[0][pg] = roms.rom_cpu1 + (pg << 8);
    for (int pg = 0; pg < 0x10; pg++) { rpage[1][pg] = roms.rom_cpu2 + (pg << 8); rpage[2][pg] = roms.rom_cpu3 + (pg << 8); }
    for (int c = 0; c < 3; c++) {
        for (int pg = 0x80; pg < 0x88; pg++) rpage[c][pg] = ga_videoram + ((pg - 0x80) << 8);
        for (int pg = 0x88; pg < 0x8c; pg++) rpage[c][pg] = ga_ram1 + ((pg - 0x88) << 8);
        for (int pg = 0x90; pg < 0x94; pg++) rpage[c][pg] = ga_ram2 + ((pg - 0x90) << 8);
        for (int pg = 0x98; pg < 0x9c; pg++) rpage[c][pg] = ga_ram3 + ((pg - 0x98) << 8);
    }
    for (int pg = 0x80; pg < 0x88; pg++) wpage[pg] = ga_videoram + ((pg - 0x80) << 8);
    for (int pg = 0x88; pg < 0x8c; pg++) wpage[pg] = ga_ram1 + ((pg - 0x88) << 8);
    for (int pg = 0x90; pg < 0x94; pg++) wpage[pg] = ga_ram2 + ((pg - 0x90) << 8);
    for (int pg = 0x98; pg < 0x9c; pg++) wpage[pg] = ga_ram3 + ((pg - 0x98) << 8);
}

/* Idle loops the sub CPUs sit in between interrupts (verified in the ROMs):
 *   CPU2 05B1: LD SP,$9100 / JP $05B1   (waits for the vblank IRQ)
 *   CPU3 00B9: JR $                      (waits for the 05XX NMI) */
static inline int in_idle_loop(int c, unsigned pc)
{
    if (c == 1) return pc >= 0x05b1 && pc <= 0x05b6;
    if (c == 2) return pc == 0x00b9 || pc == 0x00ba;
    return 0;
}

/* ---- Z80 callbacks (the core has global callbacks; cur_cpu selects the ROM) ---- */
static uint8_t n51_read(void);
static void n51_write(uint8_t d);

byte RdZ80(register word a)
{
    const uint8_t *p = rpage[cur_cpu][a >> 8];
    if (p) return p[a & 0xff];
    if (a >= 0x6800 && a < 0x6808) {
        int off = a & 7;
        return ((dswb >> off) & 1) | (((dswa >> off) & 1) << 1);
    }
    if (a >= 0x7000 && a < 0x7100) {
        if (!(n06_ctrl & 0x10)) return 0;               /* read in write mode */
        uint8_t r = 0xff;
        if (n06_ctrl & 0x01) r &= n51_read();
        return r;
    }
    if (a == 0x7100) return n06_ctrl;
    return 0xff;
}

void WrZ80(register word a, register byte d)
{
    uint8_t *p = wpage[a >> 8];
    if (p) { p[a & 0xff] = d; return; }
    if (a >= 0x6800 && a < 0x6820) { ga_wsg_write(a & 0x1f, d); return; }
    if (a >= 0x6820 && a < 0x6828) {
        int bit = a & 7, v = d & 1;
        switch (bit) {
            case 0: main_irq_en = v; if (!v) { irq_pending[0] = 0; cpu[0].IRequest = INT_NONE; } break;
            case 1: sub_irq_en = v;  if (!v) { irq_pending[1] = 0; cpu[1].IRequest = INT_NONE; } break;
            case 2: sub2_nmi_en = !v; break;
            case 3:
                if (v && !subs_running) { ResetZ80(&cpu[1]); ResetZ80(&cpu[2]); }
                if (!v) { memset(&n51, 0, sizeof(n51)); ga_n54_reset(); }
                subs_running = v;
                break;
            default: break;
        }
        return;
    }
    if (a == 0x6830) return;                             /* watchdog */
    if (a >= 0x7000 && a < 0x7100) {
        if (n06_ctrl & 0x10) return;                     /* write in read mode */
        if (n06_ctrl & 0x01) n51_write(d);
        if (n06_ctrl & 0x08) ga_n54_write(d);
        return;
    }
    if (a == 0x7100) {
        n06_ctrl = d;
        if ((d & 0x0f) == 0) n06_nmi_countdown = -1;
        else n06_nmi_countdown = N06_NMI_PERIOD;
        return;
    }
    if (a >= 0xa000 && a < 0xa008) {
        int bit = a & 7;
        if (d & 1) ga_videolatch |= (1 << bit); else ga_videolatch &= ~(1 << bit);
        return;
    }
}

byte InZ80(register word p) { (void)p; return 0xff; }
void OutZ80(register word p, register byte v) { (void)p; (void)v; }
void PatchZ80(register Z80 *R) { (void)R; }
word LoopZ80(register Z80 *R) { (void)R; return INT_QUIT; }

/* ---- 51XX ---- */
static const uint8_t joy_map[16] = {
    /* LDRU, LDR, LDU, LD, LRU, LR, LU, L, DRU, DR, DU, D, RU, R, U, center */
    0xf, 0xe, 0xd, 0x5, 0xc, 0x9, 0x7, 0x6, 0xb, 0x3, 0xa, 0x4, 0x1, 0x2, 0x0, 0x8 };

/* the chip's four input nibbles, active low like the real switches */
static uint8_t port0(void) { return (uint8_t)(~((input.fire ? 1 : 0) | (input.start1 ? 4 : 0) | (input.start2 ? 8 : 0)) & 0x0f); }
static uint8_t port1(void) { return (uint8_t)(~((input.coin1 ? 1 : 0) | (input.coin2 ? 2 : 0) | (input.service ? 4 : 0)) & 0x0f); }
static uint8_t port2(void) { return (uint8_t)(~((input.right ? 2 : 0) | (input.left ? 8 : 0)) & 0x0f); }
static uint8_t port3(void) { return 0x0f; }

static void n51_write(uint8_t d)
{
    d &= 0x07;
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

static uint8_t n51_read(void)
{
    if (n51.mode == 0) {
        switch ((n51.in_count++) % 3) {
            default:
            case 0: return port0() | (port1() << 4);
            case 1: return port2() | (port3() << 4);
            case 2: return 0;
        }
    }
    switch ((n51.in_count++) % 3) {
        default:
        case 0: {
            int in = ~(port0() | (port1() << 4)) & 0xff;
            int toggle = in ^ n51.lastcoins;
            n51.lastcoins = in;
            if (n51.coins_per_cred[0] > 0) {
                if (n51.credits < 99) {
                    if (toggle & in & 0x10) {
                        n51.coins[0]++;
                        if (n51.coins[0] >= n51.coins_per_cred[0]) {
                            n51.credits += n51.creds_per_coin[0];
                            n51.coins[0] -= n51.coins_per_cred[0];
                        }
                    }
                    if (toggle & in & 0x20) {
                        n51.coins[1]++;
                        if (n51.coins[1] >= n51.coins_per_cred[1]) {
                            n51.credits += n51.creds_per_coin[1];
                            n51.coins[1] -= n51.coins_per_cred[1];
                        }
                    }
                    if (toggle & in & 0x40) n51.credits++;
                }
            } else {
                n51.credits = 100;                        /* free play */
            }
            if (n51.mode == 1) {
                if (toggle & in & 0x04) {
                    if (n51.credits >= 1) { n51.credits--; n51.mode = 2; }
                } else if (toggle & in & 0x08) {
                    if (n51.credits >= 2) { n51.credits -= 2; n51.mode = 2; }
                }
            }
            if (~port1() & 0x08) return 0xbb;             /* test switch */
            return (uint8_t)((n51.credits / 10) * 16 + n51.credits % 10);
        }
        case 1: {
            int joy = port2() & 0x0f;
            int in = ~port0() & 0x0f;
            int toggle = in ^ n51.lastbuttons;
            n51.lastbuttons = (n51.lastbuttons & 2) | (in & 1);
            if (n51.remap_joy) joy = joy_map[joy];
            joy |= ((toggle & in & 0x01) ^ 1) << 4;
            joy |= ((in & 0x01) ^ 1) << 5;
            return (uint8_t)joy;
        }
        case 2: {
            int joy = port3() & 0x0f;
            int in = ~port0() & 0x0f;
            int toggle = in ^ n51.lastbuttons;
            n51.lastbuttons = (n51.lastbuttons & 1) | (in & 2);
            if (n51.remap_joy) joy = joy_map[joy];
            joy |= ((toggle & in & 0x02) ^ 2) << 3;
            joy |= ((in & 0x02) ^ 2) << 4;
            return (uint8_t)joy;
        }
    }
}

/* ---- public ---- */
void ga_init(const ga_roms_t *r)
{
    roms = *r;
    map_init();
    ga_video_init(&roms);
    ga_wsg_init(roms.prom_wave);
    ga_n54_init();
    ga_reset();
}

void ga_reset(void)
{
    memset(vram, 0, sizeof(vram));
    memset(ga_videoram, 0, sizeof(ga_videoram));
    memset(ga_ram1, 0, sizeof(ga_ram1));
    memset(ga_ram2, 0, sizeof(ga_ram2));
    memset(ga_ram3, 0, sizeof(ga_ram3));
    ga_videolatch = 0;
    main_irq_en = sub_irq_en = 0; sub2_nmi_en = 0; subs_running = 0;
    irq_pending[0] = irq_pending[1] = 0;
    n06_ctrl = 0; n06_nmi_countdown = -1;
    memset(&n51, 0, sizeof(n51));
    memset(&input, 0, sizeof(input));
    for (int i = 0; i < 3; i++) {
        ResetZ80(&cpu[i]);
        cpu[i].IAutoReset = 1;
        cpu[i].TrapBadOps = 0;
    }
    ga_wsg_reset();
    ga_n54_reset();
    ga_video_reset();
}

void ga_set_dips(uint8_t a, uint8_t b) { dswa = a; dswb = b; }
ga_input_t *ga_input(void) { return &input; }

/* run CPU i for `cycles` (it may overshoot by one instruction; the debt is carried) */
static int32_t debt[3];
static void run_cpu(int i, int32_t cycles)
{
    cycles -= debt[i];
    debt[i] = 0;
    if (cycles <= 0) { debt[i] = -cycles; return; }
    if ((cpu[i].IFF & IFF_HALT) || (in_idle_loop(i, cpu[i].PC.W) && !irq_pending[i])) {
        halt_cycles[i] += cycles;          /* nothing observable until the next interrupt */
        return;
    }
    cur_cpu = i;
    cpu[i].IPeriod = cycles;
    cpu[i].ICount = cycles;
    RunZ80(&cpu[i]);
    int32_t overshoot = cycles - cpu[i].ICount;   /* ICount was reset to IPeriod - overshoot */
    if (overshoot > 0) debt[i] = overshoot;
}

static void deliver_irq(int i)
{
    if (irq_pending[i] && (cpu[i].IFF & IFF_1)) {
        cur_cpu = i;
        IntZ80(&cpu[i], INT_IRQ);
        irq_pending[i] = 0;
    }
}

void ga_run_frame(void)
{
    const int32_t slice = 128;
    int32_t t = 0;
    int nmi3_next = 64 * GA_CYCLES_PER_LINE;
    int vblank_at = GA_VBLANK_LINE * GA_CYCLES_PER_LINE;
    int vblank_done = 0;

    while (t < GA_CYCLES_PER_FRAME) {
        /* 06XX NMI to the main CPU while a transfer is active */
        if (n06_nmi_countdown >= 0) {
            n06_nmi_countdown -= slice;
            if (n06_nmi_countdown < 0) {
                n06_nmi_countdown += N06_NMI_PERIOD;
                cur_cpu = 0;
                IntZ80(&cpu[0], INT_NMI);
            }
        }
        /* CPU3 NMI at scanlines 64 and 192 */
        if (t >= nmi3_next) {
            if (sub2_nmi_en && subs_running) { cur_cpu = 2; IntZ80(&cpu[2], INT_NMI); }
            nmi3_next += 128 * GA_CYCLES_PER_LINE;
        }
        /* vblank: IRQs to CPU1/CPU2, render */
        if (!vblank_done && t >= vblank_at) {
            vblank_done = 1;
            if (main_irq_en) irq_pending[0] = 1;
            if (sub_irq_en && subs_running) irq_pending[1] = 1;
            ga_video_vblank();
        }
        deliver_irq(0);
        run_cpu(0, slice);
        if (subs_running) {
            deliver_irq(1);
            run_cpu(1, slice);
            run_cpu(2, slice);
        }
        t += slice;
    }
    frame_count++;
}

void ga_render(uint8_t *fb) { ga_video_render(fb); }
void ga_render_audio(int16_t *buf, int samples, int sample_rate)
{
    memset(buf, 0, samples * sizeof(int16_t));
    ga_wsg_render(buf, samples, sample_rate);
    ga_n54_render(buf, samples, sample_rate);
}

uint16_t ga_pc(int i) { return cpu[i].PC.W; }
uint32_t ga_frame_count(void) { return frame_count; }
uint32_t ga_halt_cycles(int i) { uint32_t v = halt_cycles[i]; halt_cycles[i] = 0; return v; }
int ga_credits(void) { return n51.credits; }
uint8_t ga_starfield_ctl(void) { return ga_videolatch; }
