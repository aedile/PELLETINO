/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * digdug.c - Namco Dig Dug board: three Z80s, the 06XX bus controller and its two custom
 * I/O chips. Ported from this project family's Galaga core, which is the same arrangement.
 * The Z80 is Marat Fayzullin's portable core (see THIRD_PARTY_NOTICES.md).
 */
#include "digdug_internal.h"
#include "Z80.h"
#include <string.h>

dd_roms_t dd_roms;
uint8_t dd_videoram[0x400];
uint8_t dd_objram[0x400], dd_posram[0x400], dd_flpram[0x400];
uint8_t dd_videolatch;

static Z80 cpu[3];
static int cur_cpu;
static uint8_t work[0x400];               /* 0x8400-0x87FF, shared by all three CPUs */
static uint8_t earom[0x40];               /* the high-score EAROM */
static uint8_t dswa = 0x99, dswb = 0x24;  /* factory: 1C/1C, bonus at 20K and 60K, 3 lives */
static dd_input_t input;
static uint32_t frame_count;

/* misc latch 0x6820-0x6827 */
static uint8_t main_irq_en, sub_irq_en, sub2_nmi_en, subs_running;
static uint8_t irq_pending[3];
static uint32_t halt_cycles[3];
static int32_t debt[3];

/* 06XX */
static uint8_t n06_ctrl;
static int32_t n06_nmi_countdown;
#define N06_NMI_PERIOD 614                /* 200 us at 3.072 MHz */

/* 51XX high level model (protocol from the old MAME HLE) */
static struct {
    int mode, in_count, credits;
    int coins[2], coins_per_cred[2], creds_per_coin[2];
    int coincred_mode, remap_joy, lastcoins, lastbuttons;
} n51;

/* 53XX: presents the two DIP banks as a repeating run of four nibbles */
static int n53_count;
uint8_t dd_dbg_latch, dd_dbg_misc, dd_dbg_n06; int dd_dbg_subs, dd_dbg_n51mode, dd_dbg_n53reads;

/* 256-byte page tables per CPU (NULL = I/O or unmapped) */
static const uint8_t *rpage[3][256];
static uint8_t *wpage[256];

static void map_init(void)
{
    memset(rpage, 0, sizeof(rpage));
    memset(wpage, 0, sizeof(wpage));
    for (int pg = 0; pg < 0x40; pg++) rpage[0][pg] = dd_roms.cpu1 + (pg << 8);
    for (int pg = 0; pg < 0x20; pg++) rpage[1][pg] = dd_roms.cpu2 + (pg << 8);
    for (int pg = 0; pg < 0x10; pg++) rpage[2][pg] = dd_roms.cpu3 + (pg << 8);
    for (int c = 0; c < 3; c++) {
        for (int pg = 0x80; pg < 0x84; pg++) rpage[c][pg] = dd_videoram + ((pg - 0x80) << 8);
        for (int pg = 0x84; pg < 0x88; pg++) rpage[c][pg] = work      + ((pg - 0x84) << 8);
        for (int pg = 0x88; pg < 0x8c; pg++) rpage[c][pg] = dd_objram + ((pg - 0x88) << 8);
        for (int pg = 0x90; pg < 0x94; pg++) rpage[c][pg] = dd_posram + ((pg - 0x90) << 8);
        for (int pg = 0x98; pg < 0x9c; pg++) rpage[c][pg] = dd_flpram + ((pg - 0x98) << 8);
    }
    for (int pg = 0x80; pg < 0x84; pg++) wpage[pg] = dd_videoram + ((pg - 0x80) << 8);
    for (int pg = 0x84; pg < 0x88; pg++) wpage[pg] = work      + ((pg - 0x84) << 8);
    for (int pg = 0x88; pg < 0x8c; pg++) wpage[pg] = dd_objram + ((pg - 0x88) << 8);
    for (int pg = 0x90; pg < 0x94; pg++) wpage[pg] = dd_posram + ((pg - 0x90) << 8);
    for (int pg = 0x98; pg < 0x9c; pg++) wpage[pg] = dd_flpram + ((pg - 0x98) << 8);
}

/*
 * The third CPU spends most of its life here:
 *      00B0  LD SP,8B80
 *      00B3  JR 00B0
 * waiting for the NMI that gives it work. Reloading the stack pointer to the same value over
 * and over is not observable, so those cycles can be handed back - and they are most of that
 * CPU's time, which is a third of the emulation budget.
 *
 * The second CPU's loop is deliberately left alone. It looks similar, but it calls a routine
 * that polls a mailbox in shared RAM which the main CPU writes, so skipping ahead could step
 * over a message.
 */
static inline int in_idle_loop(int c, unsigned pc)
{
    if (c == 2) return pc == 0x00b0 || pc == 0x00b3;
    return 0;
}

static uint8_t n51_read(void);
static void n51_write(uint8_t d);
static uint8_t n53_read(void);

byte RdZ80(register word a)
{
    const uint8_t *p = rpage[cur_cpu][a >> 8];
    if (p) return p[a & 0xff];
    if (a >= 0x7000 && a < 0x7100) {
        if (!(n06_ctrl & 0x10)) return 0;                /* read while set to write */
        uint8_t r = 0xff;
        if (n06_ctrl & 0x01) r &= n51_read();
        if (n06_ctrl & 0x02) r &= n53_read();
        return r;
    }
    if (a == 0x7100) return n06_ctrl;
    if (a >= 0xb800 && a < 0xb840) return earom[a & 0x3f];
    return 0xff;
}

void WrZ80(register word a, register byte d)
{
    uint8_t *p = wpage[a >> 8];
    if (p) { p[a & 0xff] = d; return; }
    if (a >= 0x6800 && a < 0x6820) { dd_wsg_write(a & 0x1f, d); return; }
    if (a >= 0x6820 && a < 0x6828) {
        int bit = a & 7, v = d & 1;
        switch (bit) {
            case 0: main_irq_en = v; if (!v) { irq_pending[0] = 0; cpu[0].IRequest = INT_NONE; } break;
            case 1: sub_irq_en = v;  if (!v) { irq_pending[1] = 0; cpu[1].IRequest = INT_NONE; } break;
            case 2: sub2_nmi_en = !v; break;
            case 3:
                if (v && !subs_running) { ResetZ80(&cpu[1]); ResetZ80(&cpu[2]); }
                if (!v) { memset(&n51, 0, sizeof(n51)); n53_count = 0; }
                subs_running = v;
                break;
            default: break;
        }
        return;
    }
    if (a == 0x6830) return;                             /* watchdog */
    if (a >= 0x7000 && a < 0x7100) {
        if (n06_ctrl & 0x10) return;                     /* write while set to read */
        if (n06_ctrl & 0x01) n51_write(d);
        return;
    }
    if (a == 0x7100) {
        n06_ctrl = d;
        if ((d & 0x0f) == 0) n06_nmi_countdown = -1;
        else { n06_nmi_countdown = N06_NMI_PERIOD; n53_count = 0; }
        return;
    }
    if (a >= 0xa000 && a < 0xa008) {                     /* video latch */
        int bit = a & 7;
        if (d & 1) dd_videolatch |= (uint8_t)(1 << bit); else dd_videolatch &= (uint8_t)~(1 << bit);
        return;
    }
    if (a >= 0xb800 && a < 0xb840) { earom[a & 0x3f] = d; return; }
    if (a == 0xb840) return;                             /* EAROM control */
}

byte InZ80(register word p) { (void)p; return 0xff; }
void OutZ80(register word p, register byte v) { (void)p; (void)v; }
void PatchZ80(register Z80 *R) { (void)R; }
word LoopZ80(register Z80 *R) { (void)R; return INT_QUIT; }

/* ---- 51XX ----
 * This chip has four input ports, and Dig Dug wires them differently from Galaga:
 *
 *      port 0   the four-way joystick          (Galaga: buttons and starts)
 *      port 1   the cocktail joystick          (Galaga: coins and service)
 *      port 2   button, and the start buttons  (Galaga: the joystick)
 *      port 3   coins and service
 *
 * That matters in two different ways. In switch mode the chip hands the ports back raw, in
 * hardware order, and the game parses them as they come - so those reads have to follow this
 * board's wiring. In credit mode the chip is doing the coin arithmetic itself, and the logic
 * below (which came from MAME's old high-level model, written against Galaga) wants the
 * coin/start byte and the joystick nibble regardless of which pins they arrived on. So the
 * two modes read the same four ports through different lenses.
 */
static const uint8_t joy_map[16] = {
    0xf, 0xe, 0xd, 0x5, 0xc, 0x9, 0x7, 0x6, 0xb, 0x3, 0xa, 0x4, 0x1, 0x2, 0x0, 0x8 };

/* the chip's four input nibbles as this board wires them, active low like the switches */
static uint8_t hw_p0(void)   /* joystick */
{
    return (uint8_t)(~((input.up ? 1 : 0) | (input.right ? 2 : 0) |
                       (input.down ? 4 : 0) | (input.left ? 8 : 0)) & 0x0f);
}
static uint8_t hw_p1(void) { return 0x0f; }                  /* cocktail joystick */
static uint8_t hw_p2(void)   /* button and the two start buttons */
{
    return (uint8_t)(~((input.fire ? 1 : 0) | (input.start1 ? 4 : 0) |
                       (input.start2 ? 8 : 0)) & 0x0f);
}
static uint8_t hw_p3(void)   /* coins and service */
{
    return (uint8_t)(~((input.coin1 ? 1 : 0) | (input.coin2 ? 2 : 0) |
                       (input.service ? 4 : 0)) & 0x0f);
}

/* the same ports, seen the way the credit-mode logic expects them */
static uint8_t port0(void) { return hw_p2(); }
static uint8_t port1(void) { return hw_p3(); }
static uint8_t port2(void) { return hw_p0(); }
static uint8_t port3(void) { return hw_p1(); }

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
            case 0: return (uint8_t)(hw_p0() | (hw_p1() << 4));
            case 1: return (uint8_t)(hw_p2() | (hw_p3() << 4));
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
                if (toggle & in & 0x04) { if (n51.credits >= 1) { n51.credits--; n51.mode = 2; } }
                else if (toggle & in & 0x08) { if (n51.credits >= 2) { n51.credits -= 2; n51.mode = 2; } }
            }
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

/* ---- 53XX ----
 * Where Galaga has the 54XX noise generator, Dig Dug has a chip whose only job is to read the
 * DIP switches. Its four input nibbles are the two halves of each bank, and it hands back one
 * bank per read: the game asks for exactly two bytes per transfer, which is the whole of both.
 */
static uint8_t n53_read(void)
{
    uint8_t v = (n53_count & 1) ? dswb : dswa;
    n53_count++;
    return v;
}

/* ---- public ---- */
void dd_reset(void)
{
    memset(dd_videoram, 0, sizeof(dd_videoram));
    memset(dd_objram, 0, sizeof(dd_objram));
    memset(dd_posram, 0, sizeof(dd_posram));
    memset(dd_flpram, 0, sizeof(dd_flpram));
    memset(work, 0, sizeof(work));
    memset(earom, 0, sizeof(earom));
    dd_videolatch = 0;
    main_irq_en = sub_irq_en = 0; sub2_nmi_en = 0; subs_running = 0;
    irq_pending[0] = irq_pending[1] = irq_pending[2] = 0;
    n06_ctrl = 0; n06_nmi_countdown = -1; n53_count = 0;
    memset(&n51, 0, sizeof(n51));
    memset(&input, 0, sizeof(input));
    memset(debt, 0, sizeof(debt));
    for (int i = 0; i < 3; i++) {
        ResetZ80(&cpu[i]);
        cpu[i].IAutoReset = 1;
        cpu[i].TrapBadOps = 0;
    }
    dd_wsg_reset();
}

void dd_init(const dd_roms_t *r)
{
    dd_roms = *r;
    map_init();
    dd_video_init();
    dd_wsg_init(dd_roms.wave);
    dd_reset();
}

void dd_set_dips(uint8_t a, uint8_t b) { dswa = a; dswb = b; }
dd_input_t *dd_input(void) { return &input; }

static void run_cpu(int i, int32_t cycles)
{
    cycles -= debt[i];
    debt[i] = 0;
    if (cycles <= 0) { debt[i] = -cycles; return; }
    if ((cpu[i].IFF & IFF_HALT) || (in_idle_loop(i, cpu[i].PC.W) && !irq_pending[i])) {
        halt_cycles[i] += (uint32_t)cycles;               /* nothing observable until the next interrupt */
        return;
    }
    cur_cpu = i;
    cpu[i].IPeriod = cycles;
    cpu[i].ICount = cycles;
    RunZ80(&cpu[i]);
    int32_t overshoot = cycles - cpu[i].ICount;
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

void dd_run_frame(void)
{
    const int32_t slice = 128;
    int32_t t = 0;
    int nmi3_next = 64 * DD_CYCLES_PER_LINE;
    int vblank_at = DD_VBLANK_LINE * DD_CYCLES_PER_LINE;
    int vblank_done = 0;

    while (t < DD_CYCLES_PER_FRAME) {
        /* the 06XX pulses an NMI at the main CPU for as long as a transfer is running */
        if (n06_nmi_countdown >= 0) {
            n06_nmi_countdown -= slice;
            if (n06_nmi_countdown < 0) {
                n06_nmi_countdown += N06_NMI_PERIOD;
                cur_cpu = 0;
                IntZ80(&cpu[0], INT_NMI);
            }
        }
        if (t >= nmi3_next) {
            if (sub2_nmi_en && subs_running) { cur_cpu = 2; IntZ80(&cpu[2], INT_NMI); }
            nmi3_next += 128 * DD_CYCLES_PER_LINE;
        }
        if (!vblank_done && t >= vblank_at) {
            vblank_done = 1;
            if (main_irq_en) irq_pending[0] = 1;
            if (sub_irq_en && subs_running) irq_pending[1] = 1;
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
    dd_dbg_latch = dd_videolatch; dd_dbg_n06 = n06_ctrl; dd_dbg_subs = subs_running;
    dd_dbg_n51mode = n51.mode;
    frame_count++;
}

void dd_render(uint8_t *fb) { dd_video_render(fb); }

void dd_render_audio(int16_t *buf, int samples, int rate)
{
    memset(buf, 0, (size_t)samples * sizeof(int16_t));
    dd_wsg_render(buf, samples, rate);
}

uint16_t dd_pc(int i) { return cpu[i].PC.W; }
uint32_t dd_frame_count(void) { return frame_count; }
uint32_t dd_halt_cycles(int i) { uint32_t v = halt_cycles[i]; halt_cycles[i] = 0; return v; }
int dd_credits(void) { return n51.credits; }

/* the byte at a CPU address, where that is memory - for keeping the high scores */
uint8_t *dd_mem(uint16_t a)
{
    if (a >= 0x8000 && a < 0x8400) return &dd_videoram[a - 0x8000];
    if (a >= 0x8400 && a < 0x8800) return &work[a - 0x8400];
    if (a >= 0x8800 && a < 0x8c00) return &dd_objram[a - 0x8800];
    if (a >= 0x9000 && a < 0x9400) return &dd_posram[a - 0x9000];
    if (a >= 0x9800 && a < 0x9c00) return &dd_flpram[a - 0x9800];
    return 0;
}
