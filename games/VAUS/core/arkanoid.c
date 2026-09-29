/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * arkanoid.c - Taito Arkanoid board: the Z80, the YM2149, and the 68705 that reads the spinner.
 *
 * The MCU is not decoration. The Z80 hands it a byte at 0xD018 and waits on a pair of
 * semaphore flip-flops it can see at 0xD00C; the MCU answers with the paddle position. The
 * game checks that handshake, so the microcontroller is emulated rather than faked.
 */
#include "arkanoid_internal.h"
#include "ay8910.h"
#include "Z80.h"
#include <string.h>

ak_roms_t ak_roms;
uint8_t ak_vram[0x800];
uint8_t ak_sprram[0x40];
uint8_t ak_gfxbank, ak_palbank, ak_flipx, ak_flipy;

static uint8_t ram[0x800];               /* 0xC000-0xC7FF */
static uint8_t extra[0x7c0];             /* 0xE840-0xEFFF */
static uint8_t dsw = 0xfe;               /* factory settings */
static ak_input_t input;
static uint32_t frame_count;
static int32_t debt;
static Z80 cpu;
static ay8910_t ay;

/* ---- the handshake between the Z80 and the MCU ---- */
static uint8_t from_z80, from_mcu;
static int host_sem, mcu_sem;            /* the two flip-flops the game watches */
static int paddle_select;
static int mcu_reset_line;

/* ---- the MCU itself ---- */
static uint8_t mcu_ram[0x80];
static uint8_t port_a, port_b, port_c, ddr_a, ddr_b, ddr_c;
static uint8_t mcu_timer, mcu_tcr;
static uint32_t mcu_prescale;
static int32_t mcu_debt;

static uint8_t mcu_read(uint16_t a);
static void mcu_write(uint16_t a, uint8_t v);
#define M6805_READ(a)     mcu_read(a)
#define M6805_WRITE(a, v) mcu_write(a, v)
#include "m6805.h"
static m6805_t mcu;

/* the paddle and the buttons, as the MCU sees them on its port B */
static uint8_t input_mux(void)
{
    if (!paddle_select) {
        int p = input.paddle;
        if (p < 0) p = 0; else if (p > 255) p = 255;
        return (uint8_t)p;
    }
    return 0xff;                          /* the second player's spinner */
}

static uint8_t mcu_read(uint16_t a)
{
    a &= 0x7ff;
    switch (a) {
        case 0x00: return port_a;                       /* the shared data byte */
        case 0x01: return input_mux();                  /* the spinner */
        case 0x02:
            /*
             * The two semaphores as the MCU sees them, and they do not read the same way round.
             * Bit 0 is high when the host has left a byte, so the receive loop waits for it to
             * go high. Bit 1 is high when the host has taken the last byte the MCU sent - that
             * is, when it is safe to send another - so the send routine also waits for a high.
             * Reading bit 1 as "the MCU has written" leaves that routine waiting forever.
             */
            return (uint8_t)((port_c & 0xfc) | (host_sem ? 0x01 : 0) | (mcu_sem ? 0 : 0x02));
        case 0x04: return ddr_a;
        case 0x05: return ddr_b;
        case 0x06: return ddr_c;
        case 0x08: return mcu_timer;
        case 0x09: return mcu_tcr;
        default: break;
    }
    if (a < 0x80) return mcu_ram[a];
    return ak_roms.mcu[a];                              /* 0x080-0x7FF is the ROM */
}

static void mcu_write(uint16_t a, uint8_t v)
{
    a &= 0x7ff;
    switch (a) {
        case 0x00: port_a = v; return;
        case 0x01: port_b = v; return;
        case 0x02: {
            uint8_t before = port_c;
            port_c = v;
            /* Bit 2 falling: the MCU takes the byte the Z80 left, and the host flag clears.
             * Bit 3 falling: the MCU's byte is latched for the Z80, and its own flag sets. */
            if ((before & 0x04) && !(v & 0x04)) { port_a = from_z80; host_sem = 0; }
            if ((before & 0x08) && !(v & 0x08)) { from_mcu = port_a; mcu_sem = 1; }
            return;
        }
        case 0x04: ddr_a = v; return;
        case 0x05: ddr_b = v; return;
        case 0x06: ddr_c = v; return;
        case 0x08: mcu_timer = v; return;
        case 0x09: mcu_tcr = v; return;
        default: break;
    }
    if (a < 0x80) mcu_ram[a] = v;
}

static void mcu_sync(void);

/* ---- the Z80's bus ---- */
static uint8_t read_system(void)
{
    uint8_t v = 0xff;
    if (input.start1)  v &= (uint8_t)~0x01;
    if (input.start2)  v &= (uint8_t)~0x02;
    if (input.service) v &= (uint8_t)~0x04;
    v &= (uint8_t)~0x30;                                /* the coin inputs are active high */
    if (input.coin1) v |= 0x10;
    /* the two semaphores, both active low */
    v = (uint8_t)((v & 0x3f) | (host_sem ? 0 : 0x40) | (mcu_sem ? 0 : 0x80));
    return v;
}

byte RdZ80(register word a)
{
    if (a < 0xc000) return ak_roms.rom[a];
    if (a < 0xd000) return ram[a & 0x7ff];
    if (a < 0xe000) {
        /*
         * Bit 2 is what separates 0xD008 from 0xD00C, and 0xD00C is where the game watches the
         * two semaphores. Decoding on bits 3 and 4 alone sends those reads to the unused
         * joystick port instead, and the game decides the hardware is broken.
         */
        switch (a & 0x0018) {
            case 0x0000: return (a & 1) ? ay_data_r(&ay) : 0xff;
            case 0x0008:
                if (a & 0x0004) return read_system();   /* 0xD00C: start, coins, semaphores */
                return 0xff;                            /* 0xD008: the unused second joysticks */
            case 0x0010: {                              /* 0xD010: the fire button */
                uint8_t v = 0xff;
                if (input.fire) v &= (uint8_t)~0x01;
                return v;
            }
            case 0x0018: mcu_sem = 0; return from_mcu;  /* reading clears the MCU's flag */
            default: return 0xff;
        }
    }
    if (a < 0xe800) return ak_vram[a & 0x7ff];
    if (a < 0xe840) return ak_sprram[a & 0x3f];
    if (a < 0xf000) return extra[a - 0xe840];
    return 0xff;
}

void WrZ80(register word a, register byte d)
{
    if (a < 0xc000) return;
    if (a < 0xd000) { ram[a & 0x7ff] = d; return; }
    if (a < 0xe000) {
        switch (a & 0x0018) {
            case 0x0000:
                if (a & 1) ay_data_w(&ay, d); else ay_address_w(&ay, d);
                return;
            case 0x0008:                                /* the control latch */
                ak_flipx = d & 0x01;
                ak_flipy = (d >> 1) & 1;
                paddle_select = (d >> 2) & 1;
                ak_gfxbank = (d >> 5) & 1;
                ak_palbank = (d >> 6) & 1;
                /*
                 * Bit 7 low holds the MCU and both semaphore flip-flops in reset. The board
                 * holds it there from power-on and the game releases it just before asking
                 * for the first byte - so the MCU must start from its reset vector at that
                 * moment, not have been running since power-up and wandered off somewhere.
                 */
                {
                    int want_reset = !(d & 0x80);
                    if (want_reset != mcu_reset_line) {
                        mcu_reset_line = want_reset;
                        host_sem = mcu_sem = 0;
                        if (!want_reset) m6805_reset(&mcu);   /* released: start clean */
                    }
                }
                return;
            case 0x0010: return;                        /* watchdog */
            case 0x0018: from_z80 = d; host_sem = 1; mcu_sync(); return;
            default: return;
        }
    }
    if (a < 0xe800) { ak_vram[a & 0x7ff] = d; return; }
    if (a < 0xe840) { ak_sprram[a & 0x3f] = d; return; }
    if (a < 0xf000) { extra[a - 0xe840] = d; return; }
}

byte InZ80(register word p) { (void)p; return 0xff; }
void OutZ80(register word p, register byte v) { (void)p; (void)v; }
void PatchZ80(register Z80 *R) { (void)R; }
word LoopZ80(register Z80 *R) { (void)R; return INT_QUIT; }

/* the YM2149's port B is the DIP switch bank */
static uint8_t ay_port(void *ctx, int port) { (void)ctx; return (port == 1) ? dsw : 0xff; }

/* ---- public ---- */
void ak_reset(void)
{
    memset(ram, 0, sizeof(ram));
    memset(extra, 0, sizeof(extra));
    memset(ak_vram, 0, sizeof(ak_vram));
    memset(ak_sprram, 0, sizeof(ak_sprram));
    memset(&input, 0, sizeof(input));
    memset(mcu_ram, 0, sizeof(mcu_ram));
    input.paddle = 128;
    ak_gfxbank = ak_palbank = ak_flipx = ak_flipy = 0;
    from_z80 = from_mcu = 0; host_sem = mcu_sem = 0;
    paddle_select = 0;
    mcu_reset_line = 1;          /* the board holds the MCU in reset until the game says otherwise */
    port_a = port_b = port_c = 0xff; ddr_a = ddr_b = ddr_c = 0;
    mcu_timer = 0xff; mcu_tcr = 0x7f; mcu_prescale = 0;
    debt = mcu_debt = 0;
    ay_reset(&ay);
    m6805_reset(&mcu);
    ResetZ80(&cpu);
}

void ak_init(const ak_roms_t *r)
{
    ak_roms = *r;
    memset(&cpu, 0, sizeof(cpu));
    cpu.IPeriod = 1000000;
    ay_init(&ay, 1500000, ay_port, NULL);      /* 3 MHz with pin 26 low is 1.5 MHz */
    ak_video_init();
    ak_reset();
}

void ak_set_dips(uint8_t d) { dsw = d; }
ak_input_t *ak_input(void) { return &input; }

static void mcu_tick_timer(int n)
{
    /* the timer counts the machine clock divided by the prescaler */
    mcu_prescale += (uint32_t)n;
    uint32_t div = 1u << (mcu_tcr & 7);
    while (mcu_prescale >= div) {
        mcu_prescale -= div;
        if (mcu_timer-- == 0) mcu_tcr |= 0x80;
    }
}

/*
 * The Z80's boot self-test hands the MCU all 48 KB of program ROM a byte at a time, waiting on
 * the semaphore after each one. Interleaving the two processors on a fixed slice makes that cost
 * one slice per byte - twelve seconds of black screen before the title ever appears. On the board
 * they run at the same time and the reply comes back in microseconds, so run the MCU the moment
 * the Z80 gives it something to do, until it is back in its idle loop with nothing pending. The
 * cycles are charged against the frame's allowance, so the MCU still gets no more time than its
 * clock would give it.
 */
static void mcu_sync(void)
{
    if (mcu_reset_line) return;
    int32_t spent = 0;
    while (spent < 4000 && !(mcu.pc == 0x080 && !host_sem)) {
        int n = m6805_step(&mcu);
        spent += n;
        mcu_tick_timer(n);
    }
    mcu_debt += spent;
}

static void run_mcu(int32_t cycles)
{
    cycles -= mcu_debt;
    mcu_debt = 0;
    if (cycles <= 0) { mcu_debt = -cycles; return; }
    if (mcu_reset_line) return;                /* held in reset by the control latch */
    /*
     * The MCU spends nearly all of its life at 0x080, the three-byte loop that waits for the
     * host to leave a byte. Nothing it can see changes until the Z80 writes, so those cycles
     * are ours to hand back - and they are most of the second processor's cost.
     */
    if (mcu.pc == 0x080 && !host_sem) return;
    int32_t done = 0;
    while (done < cycles) {
        int n = m6805_step(&mcu);
        done += n;
        mcu_tick_timer(n);
    }
    mcu_debt = done - cycles;
}

void ak_run_frame(void)
{
    /* The two processors talk to each other constantly, so they are interleaved finely -
     * MAME synchronises them a hundred times a second and this does rather better. */
    enum { SLICES = 64 };    /* fine enough for the handshake, without the scheduling cost of finer */
    for (int s = 0; s < SLICES; s++) {
        int32_t cycles = AK_CYCLES_PER_FRAME / SLICES - debt;
        debt = 0;
        if (cycles > 0) {
            cpu.IPeriod = cycles;
            cpu.ICount = cycles;
            RunZ80(&cpu);
            int32_t over = cycles - cpu.ICount;
            if (over > 0) debt = over;
        } else {
            debt = -cycles;
        }
        run_mcu(AK_MCU_CYCLES_PER_FRAME / SLICES);
    }
    IntZ80(&cpu, INT_IRQ);                     /* vblank, held until taken */
    frame_count++;
}

void ak_render(uint8_t *fb) { ak_video_render(fb); }

void ak_render_audio(int16_t *buf, int samples, int rate)
{
    memset(buf, 0, (size_t)samples * sizeof(int16_t));
    /* three channels at full tilt already reach about two thirds of full scale, so the
     * YM2149's own output is the right level; anything on top of it just clips */
    ay_render(&ay, buf, samples, rate);
}

uint16_t ak_pc(void) { return cpu.PC.W; }
uint16_t ak_mcu_pc(void) { return mcu.pc; }
uint32_t ak_frame_count(void) { return frame_count; }

/* the byte at a CPU address, where that is memory - for keeping the high scores */
uint8_t *ak_mem(uint16_t a)
{
    if (a >= 0xc000 && a < 0xc800) return &ram[a & 0x7ff];
    if (a >= 0xe840 && a < 0xf000) return &extra[a - 0xe840];
    return 0;
}
