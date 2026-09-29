/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * gyruss.c - Konami Gyruss: four processors, five sound chips, and the latches between them.
 * Memory map and timing follow MAME's gyruss.cpp.
 */
#include "gyruss.h"
#include "gyruss_internal.h"
#include "ay8910.h"
#include "e6809.h"
#include "Z80.h"
#include <string.h>

gy_roms_t gy_roms;
uint8_t gy_colorram[0x400];
uint8_t gy_videoram[0x400];
uint8_t gy_spriteram[0x100];
uint8_t gy_flipscreen;

static uint8_t main_ram[0x1000];     /* 0x9000-0x9FFF */
static uint8_t shared_ram[0x800];    /* Z80 0xA000, 6809 0x6000 */
static uint8_t sub_ram[0x800];       /* the 6809's own 0x4000-0x47FF, sprites included */
static uint8_t audio_ram[0x400];     /* 0x6000-0x63FF on the sound Z80 */

static uint8_t dsw1 = 0xff, dsw2 = 0x5f, dsw3 = 0xff;
static gy_input_t input;
static uint32_t frame_count;

static Z80 cpu[2];                   /* 0 = the game, 1 = the sound board */
static int cur_cpu;                  /* the Z80 core's callbacks are global, so this selects the map */
static ay8910_t ay[5];
static uint64_t audio_cycles;        /* the sound Z80's clock, which is also its tempo source */

/*
 * The sound board's tempo comes off its own clock: a divide by 1024 feeding an LS90 wired as a
 * bi-quinary decade counter, whose four outputs the program reads through the third AY's port A.
 * It is the only clock that board has, so without this the music never advances - the sound Z80
 * sits in its interrupt handler waiting for a count that never changes, never re-enables
 * interrupts, and every command after the first one is ignored.
 */
static const uint8_t ls90_timer[10] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x09, 0x0a, 0x0b, 0x0a, 0x0d };

static uint8_t ay3_port_read(void *ctx, int port)
{
    (void)ctx;
    if (port == 0) return ls90_timer[(audio_cycles / 1024) % 10];
    return 0xff;
}

static int master_nmi_mask, slave_irq_mask;
static int slave_irq_pending, audio_irq_pending;
static uint8_t soundlatch, soundlatch2;
static int32_t debt[2], sub_debt;
static int scanline;

/* ---- the 8039 that drives the DAC ---- */
static uint8_t dac_value = 0x80;
static int i8039_irq;
/*
 * The 8039 does one thing: it reads a byte the sound Z80 leaves in a latch and writes samples
 * to a DAC on port 1. Its whole address space is the latch, so any BUS read returns it. A write
 * to port 2 is how it acknowledges the interrupt the Z80 raised.
 */
static void mcu_p1_out(uint8_t v);
static void mcu_p2_out(uint8_t v);
#define MCS48_ROM(a)      (gy_roms.audio2rom[(a) & 0x0fff])
#define MCS48_BUS_IN(a)   soundlatch2
#define MCS48_BUS_OUT(v)  ((void)(v))
#define MCS48_P2_IN()     0xff
#define MCS48_P1_OUT(v)   mcu_p1_out(v)
#define MCS48_P2_OUT(v)   mcu_p2_out(v)
#define MCS48_T0()        0
#define MCS48_T1()        0
#include "mcs48.h"
static mcs48_t mcu;
static void mcu_p1_out(uint8_t v) { dac_value = v; }
static void mcu_p2_out(uint8_t v) { (void)v; i8039_irq = 0; mcu.irq_line = 0; }

/* ---- input ports, all active low ---- */
static uint8_t read_system(void)
{
    uint8_t v = 0xff;
    if (input.coin1)  v &= (uint8_t)~0x01;
    if (input.start1) v &= (uint8_t)~0x08;
    if (input.start2) v &= (uint8_t)~0x10;
    return v;
}

static uint8_t read_player(void)
{
    uint8_t v = 0xff;
    if (input.left)  v &= (uint8_t)~0x01;
    if (input.right) v &= (uint8_t)~0x02;
    if (input.up)    v &= (uint8_t)~0x04;
    if (input.down)  v &= (uint8_t)~0x08;
    if (input.fire)  v &= (uint8_t)~0x10;
    return v;
}

/* ---- the game Z80 ---- */
static uint8_t main_read(uint16_t a)
{
    if (a < 0x8000) return gy_roms.rom[a];
    if (a < 0x8400) return gy_colorram[a - 0x8000];
    if (a < 0x8800) return gy_videoram[a - 0x8400];
    if (a >= 0x9000 && a < 0xa000) return main_ram[a - 0x9000];
    if (a >= 0xa000 && a < 0xa800) return shared_ram[a - 0xa000];
    switch (a & 0xffe0) {
        case 0xc000: return dsw2;
        case 0xc080: return read_system();
        case 0xc0a0: return read_player();
        case 0xc0c0: return read_player();     /* the cocktail player's stick, wired the same */
        case 0xc0e0: return dsw1;
        case 0xc100: return dsw3;
        default: return 0xff;
    }
}

static void main_write(uint16_t a, uint8_t d)
{
    if (a < 0x8000) return;
    if (a < 0x8400) { gy_colorram[a - 0x8000] = d; return; }
    if (a < 0x8800) { gy_videoram[a - 0x8400] = d; return; }
    if (a >= 0x9000 && a < 0xa000) { main_ram[a - 0x9000] = d; return; }
    if (a >= 0xa000 && a < 0xa800) { shared_ram[a - 0xa000] = d; return; }
    if (a >= 0xc180 && a <= 0xc187) {          /* an LS259 addressed by the low three bits */
        int bit = a & 7, v = d & 1;
        if (bit == 0) { master_nmi_mask = v; if (!v) cpu[0].IFF &= (uint8_t)~IFF_HALT; }
        else if (bit == 5) gy_flipscreen = (uint8_t)v;
        return;
    }
    switch (a & 0xffe0) {
        case 0xc080: audio_irq_pending = 1; return;   /* the sound board's interrupt */
        case 0xc100: soundlatch = d; return;
        default: return;
    }
}

/* ---- the sound Z80 ---- */
static uint8_t audio_read(uint16_t a)
{
    if (a < 0x6000) return gy_roms.audiorom[a];
    if (a < 0x6400) return audio_ram[a - 0x6000];
    if (a >= 0x8000 && a < 0x8100) return soundlatch;
    return 0xff;
}

static void audio_write(uint16_t a, uint8_t d)
{
    if (a >= 0x6000 && a < 0x6400) { audio_ram[a - 0x6000] = d; return; }
}

byte RdZ80(register word a) { return cur_cpu ? audio_read(a) : main_read(a); }
void WrZ80(register word a, register byte d) { if (cur_cpu) audio_write(a, d); else main_write(a, d); }

/* The sound Z80 reaches its five AY-3-8910s through its I/O ports, four addresses apiece. */
byte InZ80(register word p)
{
    int port = p & 0xff;
    if (port < 0x14 && (port & 3) == 1) return ay_data_r(&ay[port >> 2]);
    return 0xff;
}

void OutZ80(register word p, register byte v)
{
    int port = p & 0xff;
    if (port < 0x14) {
        int chip = port >> 2, reg = port & 3;
        if (reg == 0) ay_address_w(&ay[chip], v);
        else if (reg == 2) ay_data_w(&ay[chip], v);
        return;
    }
    if (port == 0x14) { i8039_irq = 1; mcu.irq_line = 1; return; }
    if (port == 0x18) { soundlatch2 = v; return; }
}

void PatchZ80(register Z80 *R) { (void)R; }
word LoopZ80(register Z80 *R) { (void)R; return INT_QUIT; }

/* ---- the KONAMI-1 sub-CPU ---- */
static unsigned char sub_read(unsigned a)
{
    a &= 0xffff;
    if (a == 0x0000) return (unsigned char)(scanline & 0xff);   /* 1V - 128V */
    if (a >= 0x4000 && a < 0x4800) return sub_ram[a - 0x4000];
    if (a >= 0x6000 && a < 0x6800) return shared_ram[a - 0x6000];
    if (a >= 0xe000) return gy_roms.subrom[a - 0xe000];
    return 0xff;
}

static void sub_write(unsigned a, unsigned char d)
{
    a &= 0xffff;
    if (a == 0x2000) { slave_irq_mask = d & 1; return; }
    if (a >= 0x4000 && a < 0x4800) {
        sub_ram[a - 0x4000] = d;
        /* 0x4040-0x40FF is the sprite list; the video reads it from its own copy */
        if (a >= 0x4040 && a < 0x4100) gy_spriteram[a - 0x4000] = d;
        return;
    }
    if (a >= 0x6000 && a < 0x6800) { shared_ram[a - 0x6000] = d; return; }
}

/*
 * The KONAMI-1 is a 6809 whose opcodes are scrambled - and only its opcodes, which is why the
 * fetch had to be split in two: operands and data come through untouched. Which of four masks
 * applies is decided by two address bits, so the encryption is this and nothing else.
 *
 * Every opcode the sub-CPU ever executes is in its own ROM, so that ROM is decrypted once at
 * init and the fetch becomes an array read. Doing it per fetch costs a switch on the hottest
 * path in the whole emulator - this processor is a third of the frame's work.
 */
static uint8_t subrom_dec[0x2000];

static unsigned char konami1_decrypt(unsigned char v, unsigned a)
{
    switch (a & 0x0a) {
        case 0x00: return (unsigned char)(v ^ 0x22);
        case 0x02: return (unsigned char)(v ^ 0x82);
        case 0x08: return (unsigned char)(v ^ 0x28);
        default:   return (unsigned char)(v ^ 0x88);
    }
}

static unsigned char sub_read_op(unsigned a)
{
    a &= 0xffff;
    if (a >= 0xe000) return subrom_dec[a - 0xe000];
    return konami1_decrypt(sub_read(a), a);
}

/* ---- public ---- */
void gy_reset(void)
{
    memset(main_ram, 0, sizeof(main_ram));
    memset(shared_ram, 0, sizeof(shared_ram));
    memset(sub_ram, 0, sizeof(sub_ram));
    memset(audio_ram, 0, sizeof(audio_ram));
    memset(gy_colorram, 0, sizeof(gy_colorram));
    memset(gy_videoram, 0, sizeof(gy_videoram));
    memset(gy_spriteram, 0, sizeof(gy_spriteram));
    memset(&input, 0, sizeof(input));
    gy_flipscreen = 0;
    master_nmi_mask = slave_irq_mask = 0;
    slave_irq_pending = audio_irq_pending = 0;
    soundlatch = soundlatch2 = 0;
    debt[0] = debt[1] = sub_debt = 0;
    audio_cycles = 0;
    scanline = 0;
    dac_value = 0x80; i8039_irq = 0;
    for (int i = 0; i < 2; i++) { cur_cpu = i; ResetZ80(&cpu[i]); }
    cur_cpu = 0;
    for (int i = 0; i < 5; i++) ay_reset(&ay[i]);
    e6809_read8 = sub_read;
    e6809_write8 = sub_write;
    e6809_read8_op = sub_read_op;
    e6809_reset();
    mcs48_reset(&mcu);
}

void gy_init(const gy_roms_t *r)
{
    gy_roms = *r;
    for (unsigned a = 0; a < 0x2000; a++)
        subrom_dec[a] = konami1_decrypt(gy_roms.subrom[a], 0xe000 + a);
    for (int i = 0; i < 2; i++) { memset(&cpu[i], 0, sizeof(cpu[i])); cpu[i].IPeriod = 1000000; }
    for (int i = 0; i < 5; i++) ay_init(&ay[i], GY_AY_CLOCK, (i == 2) ? ay3_port_read : 0, 0);
    gy_video_init();
    gy_reset();
}

void gy_set_dips(uint8_t a, uint8_t b, uint8_t c) { dsw1 = a; dsw2 = b; dsw3 = c; }
gy_input_t *gy_input(void) { return &input; }

static void run_z80(int i, int32_t cycles)
{
    cycles -= debt[i];
    if (cycles <= 0) { debt[i] = -cycles; return; }
    cur_cpu = i;
    cpu[i].IPeriod = cycles;
    cpu[i].ICount = cycles;
    RunZ80(&cpu[i]);
    int32_t over = cycles - cpu[i].ICount;
    debt[i] = over > 0 ? over : 0;
    if (i == 1) audio_cycles += (uint64_t)(cycles + over);
}

/*
 * Two of the four processors spend most of their time waiting, and on a board with four of them
 * that is the difference between running and not. Both skips below are exact - build with
 * -DGY_NO_IDLE and the frames come out byte-for-byte the same, which is how they were checked.
 *
 * The game's main loop ends up at 05F4, spinning on the top bit of a byte in its own work RAM:
 *
 *   05F7  LD A,(5865)   the high half of a pointer, out of ROM
 *   05FB  LD A,(9433)   and the low half, out of RAM
 *   05FF  LD A,(HL)
 *   0600  RLCA
 *   0601  JP C,05F4     round again while the top bit is still set
 *
 * It is waiting for the vblank NMI to clear that bit. Nothing else can: the sub-CPU's address
 * space does not reach this RAM, only the shared 2 KB. So while the bit is set and no NMI is
 * due, the rest of the slice is ours.
 */
static int main_idle(void)
{
#ifdef GY_NO_IDLE
    return 0;
#else
    uint16_t pc = cpu[0].PC.W;
    if (pc != 0x05f4 && pc != 0x05f7) return 0;
    uint16_t hl = (uint16_t)((gy_roms.rom[0x5865] << 8) | main_ram[0x9433 - 0x9000]);
    if (hl < 0x9000 || hl >= 0xa000) return 0;      /* not pointing at work RAM: leave it alone */
    return (main_ram[hl - 0x9000] & 0x80) != 0;
#endif
}

/*
 * The sub-CPU's main loop ends at F0B6, which is BRA to itself: it has finished this frame's
 * sprite work and is waiting for the vblank interrupt. Nothing it can see changes until that
 * interrupt arrives, and it spends nearly two thirds of its instructions there - which, on a
 * board with four processors, is the single largest thing the emulator does. Those cycles are
 * ours to hand back.
 */
#define SUB_IDLE_PC 0xf0b6

static void run_sub(int32_t cycles)
{
    cycles -= sub_debt;
    if (cycles <= 0) { sub_debt = -cycles; return; }
#ifndef GY_NO_IDLE
    if (!slave_irq_pending && (e6809_get_pc() & 0xffff) == SUB_IDLE_PC) { sub_debt = 0; return; }
#endif
    int32_t done = 0;
    while (done < cycles) {
        unsigned irq = 0;
        if (slave_irq_pending) { irq = 1; }
        done += (int32_t)e6809_sstep(irq, 0);
        if (slave_irq_pending && irq) slave_irq_pending = 0;
    }
    sub_debt = done - cycles;
}

/* the 8039's machine cycle is its 8 MHz clock divided by fifteen */
#define GY_MCU_CYCLES_PER_FRAME (8000000 / 15 / GY_FPS)

void gy_run_frame(void)
{
    /*
     * The four processors talk to each other constantly, so they are interleaved finely. The
     * sub-CPU also reads the scanline counter directly, which is what times its sprite work,
     * so the slice count doubles as the resolution of that counter.
     */
    enum { SLICES = 16 };
    for (int s = 0; s < SLICES; s++) {
        scanline = 256 * s / SLICES;

#ifndef GY_NO_MAIN
        if (!main_idle()) run_z80(0, GY_MAIN_CYCLES_PER_FRAME / SLICES);
#endif
#ifndef GY_NO_SUB
        run_sub(GY_SUB_CYCLES_PER_FRAME / SLICES);
#endif

        /* the sound board's interrupt is level-triggered: hold it until it can be taken */
        if (audio_irq_pending && (cpu[1].IFF & IFF_1)) {
            cur_cpu = 1;
            IntZ80(&cpu[1], INT_IRQ);
            audio_irq_pending = 0;
        }
#ifndef GY_NO_AUDIO
        run_z80(1, GY_AUDIO_CYCLES_PER_FRAME / SLICES);
#endif

#ifndef GY_NO_MCU
        int32_t n = GY_MCU_CYCLES_PER_FRAME / SLICES;
        while (n > 0) n -= mcs48_step(&mcu);
#endif
    }

    /* vblank: an NMI to the game and an IRQ to the sprite processor, each with its own mask */
    if (master_nmi_mask) { cur_cpu = 0; IntZ80(&cpu[0], INT_NMI); }
    if (slave_irq_mask) slave_irq_pending = 1;
    frame_count++;
}

void gy_render(uint8_t *fb) { gy_video_render(fb); }

void gy_render_audio(int16_t *buf, int samples, int rate)
{
    memset(buf, 0, (size_t)samples * sizeof(int16_t));
    for (int i = 0; i < 5; i++) ay_render(&ay[i], buf, samples, rate);

    /*
     * The 8039's DAC is the drums, and it is unipolar - the board's coupling capacitor takes
     * the offset away, so a one-pole high pass does it here.
     */
    static float dac_dc = 128.0f;
    for (int i = 0; i < samples; i++) {
        float raw = (float)dac_value;
        dac_dc += (raw - dac_dc) * 0.0063f;
        int32_t v = buf[i] + (int32_t)((raw - dac_dc) * 90.0f);
        buf[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
    }
}

uint16_t gy_pc(void) { return cpu[0].PC.W; }
uint16_t gy_audio_pc(void) { return cpu[1].PC.W; }
uint16_t gy_sub_pc(void) { return (uint16_t)e6809_get_pc(); }
uint16_t gy_mcu_pc(void) { return mcu.pc; }
uint8_t  gy_soundlatch(void) { return soundlatch; }
uint8_t  gy_audio_iff(void) { return cpu[1].IFF; }
uint32_t gy_frame_count(void) { return frame_count; }

/* the byte at a CPU address, where that is memory - for keeping the high scores */
uint8_t *gy_mem(uint16_t a)
{
    if (a >= 0x9000 && a < 0xa000) return &main_ram[a - 0x9000];
    return 0;
}
