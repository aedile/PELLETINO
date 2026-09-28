/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * dkong.c - Nintendo Donkey Kong board: the Z80, the memory map, and the 8257 DMA that
 * moves the sprite list once a frame.
 * The Z80 is Marat Fayzullin's portable core (see THIRD_PARTY_NOTICES.md).
 */
#include "dkong_internal.h"
#include "Z80.h"
#include <string.h>

dk_roms_t dk_roms;
uint8_t dk_vram[0x400];
uint8_t dk_sprram[0x400];
uint8_t dk_gfxbank, dk_palettebank, dk_spritebank, dk_flip;

static uint8_t ram[0xc00];               /* 0x6000-0x6BFF */
static uint8_t dsw = 0x80;               /* factory: 3 lives, bonus at 10000, 1 coin 1 play */
static dk_input_t input;
static uint32_t frame_count;
static int nmi_mask;
static int32_t debt;
static Z80 cpu;

/* the 8257: two channels and a latch between them, which together make a memory-to-memory copy */
static uint16_t dma_addr[4];
static uint16_t dma_count[4];
static uint8_t dma_flipflop;             /* the low/high byte toggle shared by all registers */
static uint8_t dma_mode;

static uint8_t read_in0(void)            /* joystick and jump, active high */
{
    uint8_t v = 0;
    if (input.right) v |= 0x01;
    if (input.left)  v |= 0x02;
    if (input.up)    v |= 0x04;
    if (input.down)  v |= 0x08;
    if (input.jump)  v |= 0x10;
    return v;
}
static uint8_t read_in1(void) { return 0; }        /* the cocktail player's controls */
static uint8_t read_in2(void)
{
    uint8_t v = 0;
    if (input.coin1)  v |= 0x80;
    if (input.start1) v |= 0x04;
    if (input.start2) v |= 0x08;
    if (dk_sound_status()) v |= 0x40;      /* the sound MCU tells the game when it is busy */
    return v;
}

/* ---- bus ---- */
byte RdZ80(register word a)
{
    if (a < 0x4000) return dk_roms.rom[a];
    if (a >= 0x6000 && a < 0x6c00) return ram[a - 0x6000];
    if (a >= 0x7000 && a < 0x7400) return dk_sprram[a & 0x3ff];
    if (a >= 0x7400 && a < 0x7800) return dk_vram[a & 0x3ff];
    if (a >= 0x7800 && a < 0x7810) return 0xff;                /* the 8257 is write-only here */
    switch (a & 0xff80) {
        case 0x7c00: return read_in0();
        case 0x7c80: return read_in1();
        case 0x7d00: return read_in2();
        case 0x7d80: return dsw;
        default: return 0xff;
    }
}

/* the sprite list is copied byte by byte through the control latch, so this is one memcpy */
static void dma_go(void)
{
    uint16_t n = (uint16_t)((dma_count[0] & 0x3fff) + 1);
    uint16_t src = dma_addr[0], dst = dma_addr[1];
    for (uint16_t i = 0; i < n; i++) {
        uint8_t v = RdZ80((word)(src + i));
        uint16_t d = (uint16_t)(dst + i);
        if (d >= 0x7000 && d < 0x7400) dk_sprram[d & 0x3ff] = v;
        else if (d >= 0x6000 && d < 0x6c00) ram[d - 0x6000] = v;
        else if (d >= 0x7400 && d < 0x7800) dk_vram[d & 0x3ff] = v;
    }
}

void WrZ80(register word a, register byte d)
{
    if (a >= 0x6000 && a < 0x6c00) { ram[a - 0x6000] = d; return; }
    if (a >= 0x7000 && a < 0x7400) { dk_sprram[a & 0x3ff] = d; return; }
    if (a >= 0x7400 && a < 0x7800) { dk_vram[a & 0x3ff] = d; return; }
    if (a >= 0x7800 && a < 0x7810) {                           /* 8257 registers */
        int reg = a & 0x0f;
        if (reg < 8) {
            int ch = reg >> 1;
            if (reg & 1) {                                     /* count register */
                if (!dma_flipflop) dma_count[ch] = (uint16_t)((dma_count[ch] & 0xff00) | d);
                else dma_count[ch] = (uint16_t)((dma_count[ch] & 0x00ff) | (d << 8));
            } else {                                           /* address register */
                if (!dma_flipflop) dma_addr[ch] = (uint16_t)((dma_addr[ch] & 0xff00) | d);
                else dma_addr[ch] = (uint16_t)((dma_addr[ch] & 0x00ff) | (d << 8));
            }
            dma_flipflop ^= 1;
        } else if (reg == 8) { dma_mode = d; dma_flipflop = 0; }
        return;
    }
    if (a >= 0x7c00 && a < 0x7c80) { dk_sound_latch_w(d); return; }
    if (a >= 0x7c80 && a < 0x7d00) return;                     /* the radar scope grid colour */
    if (a >= 0x7d00 && a < 0x7d08) { dk_sound_sig_w(a & 7, d & 1); return; }
    if (a >= 0x7d80 && a < 0x7d88) {
        switch (a & 7) {
            case 0: dk_sound_irq_w(d & 1); break;
            case 2: dk_flip = d & 1; break;
            case 3: dk_spritebank = d & 1; break;
            case 4: nmi_mask = d & 1; break;
            case 5: if (d & 1) dma_go(); break;                /* DRQ: the transfer is immediate */
            case 6: dk_palettebank = (uint8_t)((dk_palettebank & 2) | (d & 1)); break;
            case 7: dk_palettebank = (uint8_t)((dk_palettebank & 1) | ((d & 1) << 1)); break;
            default: break;
        }
        return;
    }
}

byte InZ80(register word p) { (void)p; return 0xff; }
void OutZ80(register word p, register byte v) { (void)p; (void)v; }
void PatchZ80(register Z80 *R) { (void)R; }
word LoopZ80(register Z80 *R) { (void)R; return INT_QUIT; }

/* ---- public ---- */
void dk_reset(void)
{
    memset(ram, 0, sizeof(ram));
    memset(dk_vram, 0, sizeof(dk_vram));
    memset(dk_sprram, 0, sizeof(dk_sprram));
    memset(&input, 0, sizeof(input));
    memset(dma_addr, 0, sizeof(dma_addr));
    memset(dma_count, 0, sizeof(dma_count));
    dma_flipflop = 0; dma_mode = 0;
    dk_gfxbank = dk_palettebank = dk_spritebank = dk_flip = 0;
    nmi_mask = 0; debt = 0;
    dk_sound_reset();
    ResetZ80(&cpu);
}

void dk_init(const dk_roms_t *r)
{
    dk_roms = *r;
    memset(&cpu, 0, sizeof(cpu));
    cpu.IPeriod = 1000000;
    dk_video_init();
    dk_sound_init();
    dk_reset();
}

void dk_set_dips(uint8_t d) { dsw = d; }
dk_input_t *dk_input(void) { return &input; }

void dk_run_frame(void)
{
    /* Run in slices so the sound MCU stays roughly in step with the game: it is interrupted
     * by the main CPU and reads a latch, and letting a whole frame go by between them would
     * smear the music. */
    enum { SLICES = 16 };
    for (int s = 0; s < SLICES; s++) {
        int32_t cycles = DK_CYCLES_PER_FRAME / SLICES - debt;
        debt = 0;
        if (cycles > 0) {
            cpu.IPeriod = cycles;
            cpu.ICount = cycles;
            RunZ80(&cpu);
            int32_t overshoot = cycles - cpu.ICount;
            if (overshoot > 0) debt = overshoot;
        } else {
            debt = -cycles;
        }
        dk_sound_run(DK_CYCLES_PER_FRAME / SLICES);
    }
    if (nmi_mask) IntZ80(&cpu, INT_NMI);        /* vblank */
    frame_count++;
}

uint16_t dk_pc(void) { return cpu.PC.W; }
uint16_t dk_snd_pc(void) { return dk_sound_pc(); }
uint32_t dk_frame_count(void) { return frame_count; }
