/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * frogger.c - Konami Frogger board: two Z80s, two 8255 PPIs and an AY-3-8910.
 * The Z80 is Marat Fayzullin's portable core (see THIRD_PARTY_NOTICES.md).
 */
#include "frogger.h"
#include "frogger_internal.h"
#include "ay8910.h"
#include "Z80.h"
#include <string.h>

fr_roms_t fr_roms;
uint8_t fr_vram[0x400];              /* 0xA800-0xABFF: the 32x32 character map */
uint8_t fr_oram[0x100];              /* 0xB000-0xB0FF: column scroll, colours and sprites */
uint8_t fr_flip_x, fr_flip_y;

static uint8_t ram[0x800];           /* 0x8000-0x87FF main work RAM */
static uint8_t audio_ram[0x400];     /* 0x4000-0x43FF sound work RAM */
static uint8_t dip_in1 = 0x00;       /* 3 lives */
static uint8_t dip_in2 = 0x00;       /* 1 coin 1 play, upright */
static fr_input_t input;
static uint32_t frame_count;
static int irq_enable;
static uint8_t sound_latch, sound_control;
static int audio_irq_pending;
static int32_t debt[2];      /* cycles a slice overran by, repaid from the next one */
static ay8910_t ay;
static uint64_t audio_total_cycles;

#ifdef FR_DEBUG
uint32_t fr_dbg_pc_hist[0x10000], fr_dbg_halt_cycles, fr_dbg_steps;
#endif
static Z80 cpu[2];
static int cur_cpu;                  /* the Z80 core's callbacks are global, so this selects the map */

/* ---- input ports, all active low ---- */
static uint8_t read_in0(void)
{
    uint8_t v = 0xff;
    if (input.right) v &= (uint8_t)~0x10;
    if (input.left)  v &= (uint8_t)~0x20;
    if (input.coin1) v &= (uint8_t)~0x80;
    return v;
}
static uint8_t read_in1(void)
{
    uint8_t v = (uint8_t)(0xfc | (dip_in1 & 0x03));
    if (input.start2) v &= (uint8_t)~0x40;
    if (input.start1) v &= (uint8_t)~0x80;
    return v;
}
static uint8_t read_in2(void)
{
    uint8_t v = (uint8_t)(0xf1 | (dip_in2 & 0x0e));
    if (input.up)   v &= (uint8_t)~0x10;
    if (input.down) v &= (uint8_t)~0x40;
    return v;
}

/* ---- the 8255s ----
 * PPI 0 reads the three input ports; PPI 1 writes the sound latch and the sound control line.
 * Only mode 0 is ever used, so the control register can be ignored. */
static uint8_t ppi_read(int chip, int reg)
{
    if (chip == 0) {
        switch (reg) {
            case 0: return read_in0();
            case 1: return read_in1();
            case 2: return read_in2();
            default: return 0xff;
        }
    }
    return 0x00;                     /* PPI 1 port C is unused and reads as zero */
}

static void ppi_write(int chip, int reg, uint8_t data)
{
    if (chip != 1) return;
    if (reg == 0) {
        sound_latch = data;
    } else if (reg == 1) {
        /* The falling edge of bit 3 clocks a flip-flop that interrupts the sound CPU. It is
         * only raised here, never delivered: we are running the main CPU at this moment, and
         * taking the interrupt would push the return address through the main CPU's memory
         * map. It is held until the sound CPU is next scheduled with interrupts enabled. */
        if ((sound_control & 0x08) && !(data & 0x08)) audio_irq_pending = 1;
        sound_control = data;
    }
}

/* ---- main CPU bus ---- */
static uint8_t main_read(uint16_t a)
{
    if (a < 0x3000) return fr_roms.rom[a];
    if (a < 0x4000) return 0xff;                       /* the rest of the ROM space is empty */
    if (a >= 0x8000 && a < 0x8800) return ram[a & 0x7ff];
    if (a >= 0x8800 && a < 0x9000) return 0xff;        /* watchdog reset on read */
    if (a >= 0xa800 && a < 0xb000) return fr_vram[a & 0x3ff];
    if (a >= 0xb000 && a < 0xb800) return fr_oram[a & 0xff];
    if (a >= 0xc000) {                                 /* both PPIs, very loosely decoded */
        uint16_t off = (uint16_t)(a - 0xc000);
        uint8_t result = 0xff;
        if (off & 0x1000) result &= ppi_read(1, (off >> 1) & 3);
        if (off & 0x2000) result &= ppi_read(0, (off >> 1) & 3);
        return result;
    }
    return 0xff;
}

static void main_write(uint16_t a, uint8_t d)
{
    if (a >= 0x8000 && a < 0x8800) { ram[a & 0x7ff] = d; return; }
    if (a >= 0xa800 && a < 0xb000) { fr_vram[a & 0x3ff] = d; return; }
    if (a >= 0xb000 && a < 0xb800) { fr_oram[a & 0xff] = d; return; }
    if (a >= 0xb800 && a < 0xc000) {                   /* control latches, mirrored on 0x07e3 */
        switch ((a >> 2) & 7) {
            case 2: irq_enable = d & 1; break;
            case 3: fr_flip_y = d & 1; break;
            case 4: fr_flip_x = d & 1; break;
            default: break;                            /* coin counters */
        }
        return;
    }
    if (a >= 0xc000) {
        uint16_t off = (uint16_t)(a - 0xc000);
        if (off & 0x1000) ppi_write(1, (off >> 1) & 3, d);
        if (off & 0x2000) ppi_write(0, (off >> 1) & 3, d);
        return;
    }
}

/* ---- sound CPU bus (A15 is not decoded) ---- */
static uint8_t audio_read(uint16_t a)
{
    a &= 0x7fff;
    if (a < 0x2000) return fr_roms.audiorom[a & 0x1fff];
    if (a >= 0x4000 && a < 0x6000) return audio_ram[a & 0x3ff];
    return 0xff;
}
static void audio_write(uint16_t a, uint8_t d)
{
    a &= 0x7fff;
    if (a >= 0x4000 && a < 0x6000) { audio_ram[a & 0x3ff] = d; return; }
    /* 0x6000-0x7FFF sets the analogue output filters, which we do not model */
}

/* ---- Z80 callbacks: the core's are global, so cur_cpu picks the map ---- */
byte RdZ80(register word a) { return cur_cpu ? audio_read(a) : main_read(a); }
void WrZ80(register word a, register byte d) { if (cur_cpu) audio_write(a, d); else main_write(a, d); }

byte InZ80(register word p)
{
    if (cur_cpu != 1) return 0xff;
    return (p & 0x40) ? ay_data_r(&ay) : 0xff;
}

void OutZ80(register word p, register byte v)
{
    if (cur_cpu != 1) return;
    if (p & 0x40) ay_data_w(&ay, v);
    else if (p & 0x80) ay_address_w(&ay, v);
}

void PatchZ80(register Z80 *R) { (void)R; }
/* RunZ80 calls this every IPeriod cycles; quitting there is how we get a bounded slice */
word LoopZ80(register Z80 *R) { (void)R; return INT_QUIT; }

/*
 * AY port A is the sound latch. Port B is a counter chain clocked from the sound clock: it
 * divides by 16*16*2*8*5*2 = 40960, and the sound CPU runs at one eighth of that clock, so the
 * counter index is the CPU's cycle count times eight. Frogger reads it with bits 3 and 5
 * swapped relative to the other Konami boards.
 */
static uint8_t ay_port_read(void *ctx, int port)
{
    (void)ctx;
    if (port == 0) return sound_latch;
    uint32_t cycles = (uint32_t)((audio_total_cycles * 8) % (16 * 16 * 2 * 8 * 5 * 2));
    uint8_t hibit = 0;
    if (cycles >= 16 * 16 * 2 * 8 * 5) { hibit = 1; cycles -= 16 * 16 * 2 * 8 * 5; }
    uint8_t konami = (uint8_t)((hibit << 7) |
                               (((cycles >> 14) & 1) << 6) |
                               (((cycles >> 13) & 1) << 5) |
                               (((cycles >> 11) & 1) << 4) | 0x0e);
    /* bits 3 and 5 swapped */
    return (uint8_t)((konami & 0xd7) | (((konami >> 5) & 1) << 3) | (((konami >> 3) & 1) << 5));
}

/* ---- public ---- */
void fr_reset(void)
{
    memset(ram, 0, sizeof(ram));
    memset(audio_ram, 0, sizeof(audio_ram));
    memset(fr_vram, 0, sizeof(fr_vram));
    memset(fr_oram, 0, sizeof(fr_oram));
    memset(&input, 0, sizeof(input));
    irq_enable = 0; sound_latch = 0; sound_control = 0; audio_irq_pending = 0;
    debt[0] = debt[1] = 0;
    fr_flip_x = fr_flip_y = 0;
    audio_total_cycles = 0;
    ay_reset(&ay);
    for (int i = 0; i < 2; i++) { cur_cpu = i; ResetZ80(&cpu[i]); }
    cur_cpu = 0;
}

void fr_init(const fr_roms_t *r)
{
    fr_roms = *r;
    ay_init(&ay, FR_AUDIO_CLOCK, ay_port_read, NULL);
    fr_video_init();
    for (int i = 0; i < 2; i++) { memset(&cpu[i], 0, sizeof(cpu[i])); cpu[i].IPeriod = 1000000; }
    fr_reset();
}

void fr_set_dips(uint8_t in1, uint8_t in2) { dip_in1 = in1; dip_in2 = in2; }
fr_input_t *fr_input(void) { return &input; }

/* run one CPU for `cycles`, carrying any overshoot into the next slice */
static void run_cpu(int i, int32_t cycles)
{
    cycles -= debt[i];
    debt[i] = 0;
    if (cycles <= 0) { debt[i] = -cycles; return; }
    /*
     * The main program burns time in a 16-bit countdown at 0x036B:
     *     036B  LD A,H / OR L / DEC HL / JR NZ,036B
     * 26 T-states an iteration while HL is non-zero, then 21 to fall through with HL at 0xFFFF.
     * More than half the main CPU's time goes into it and none of it is observable, so run it as
     * arithmetic instead. This is cycle-exact rather than a skip: the same number of cycles is
     * charged, so the game's timing is unchanged. A and the flags are clobbered by the LD/OR
     * immediately after the loop, so they do not need reproducing.
     */
#ifndef FR_NO_DELAY_OPT
    if (i == 0 && cpu[0].PC.W == 0x036b) {
        int32_t need = 26 * (int32_t)cpu[0].HL.W + 21;
        if (need <= cycles) {
            cpu[0].HL.W = 0xffff;
            cpu[0].PC.W = 0x0370;
            cycles -= need;
            if (cycles <= 0) { debt[i] = -cycles; return; }
        } else {
            int32_t n = cycles / 26;
            cpu[0].HL.W = (uint16_t)(cpu[0].HL.W - n);
            cycles -= n * 26;
            if (cycles <= 0) { debt[i] = -cycles; return; }
        }
    }
#endif
    cur_cpu = i;
#ifdef FR_DEBUG
    if (i == 0) { fr_dbg_pc_hist[cpu[0].PC.W]++; fr_dbg_steps++;
                  if (cpu[0].IFF & IFF_HALT) fr_dbg_halt_cycles += (uint32_t)cycles; }
#endif
    cpu[i].IPeriod = cycles;
    cpu[i].ICount = cycles;
    RunZ80(&cpu[i]);
    int32_t overshoot = cycles - cpu[i].ICount;
    if (overshoot > 0) debt[i] = overshoot;
}

void fr_run_frame(void)
{
    /* Interleave the two CPUs in slices so the sound latch handshake lands in order. */
    enum { SLICES = 32 };   /* fine slices also let the delay-loop shortcut catch more often */
    for (int s = 0; s < SLICES; s++) {
        run_cpu(0, FR_MAIN_CYCLES_PER_FRAME / SLICES);
        if (audio_irq_pending && (cpu[1].IFF & IFF_1)) {
            cur_cpu = 1;
            IntZ80(&cpu[1], INT_IRQ);
            audio_irq_pending = 0;
        }
        run_cpu(1, FR_AUDIO_CYCLES_PER_FRAME / SLICES);
        audio_total_cycles += FR_AUDIO_CYCLES_PER_FRAME / SLICES;
    }
    /* vblank drives an NMI, gated by the enable latch at 0xB808 */
    if (irq_enable) { cur_cpu = 0; IntZ80(&cpu[0], INT_NMI); }
    frame_count++;
}

void fr_render_audio(int16_t *buf, int samples, int rate)
{
    memset(buf, 0, (size_t)samples * sizeof(int16_t));
    /* bit 4 of the sound control mutes the board */
    if (sound_control & 0x10) return;
    ay_render(&ay, buf, samples, rate);
    for (int i = 0; i < samples; i++) {          /* one AY where the cabinet had an amplifier */
        int32_t v = buf[i] * 5;
        buf[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
    }
}

uint16_t fr_pc(void) { return cpu[0].PC.W; }
uint32_t fr_frame_count(void) { return frame_count; }
const uint8_t *fr_ram(void) { return ram; }
