/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * mario.c - Nintendo Mario Bros. board: the Z80 and its memory map.
 * The Z80 is Marat Fayzullin's portable core (see THIRD_PARTY_NOTICES.md).
 */
#include "mario_internal.h"
#include "Z80.h"
#include <string.h>

mb_roms_t mb_roms;
uint8_t mb_vram[0x400];
uint8_t mb_sprram[0x400];
uint8_t mb_gfxbank, mb_palettebank, mb_flip, mb_scroll;
uint32_t mb_dbg_writes[16];
uint32_t mb_dbg_dac_hist[256];

static uint8_t ram[0x1000];              /* 0x6000-0x6FFF (the top half is battery backed on the board) */
static uint8_t dsw = 0x00;               /* 3 lives, 1 coin 1 play, bonus at 20000 */
static mb_input_t input;
static uint32_t frame_count;
static int nmi_mask;
static int32_t debt;
static Z80 cpu;

static uint8_t read_in0(void)            /* player 1 and the start buttons, active high */
{
    uint8_t v = 0;
    if (input.right)  v |= 0x01;
    if (input.left)   v |= 0x02;
    if (input.jump)   v |= 0x10;
    if (input.start1) v |= 0x20;
    if (input.start2) v |= 0x40;
    return v;
}
static uint8_t read_in1(void)            /* the cocktail player's controls, and the coin */
{
    uint8_t v = 0;
    if (input.coin1) v |= 0x20;
    return v;
}

/* ---- bus ---- */
byte RdZ80(register word a)
{
    if (a < 0x6000) return mb_roms.rom[a];
    if (a < 0x7000) return ram[a - 0x6000];
    if (a < 0x7400) return mb_sprram[a & 0x3ff];
    if (a < 0x7800) return mb_vram[a & 0x3ff];
    if (a >= 0xf000) return mb_roms.rom[0x6000 + (a - 0xf000)];
    switch (a & 0xff80) {
        case 0x7c00: return read_in0();
        case 0x7c80: return read_in1();
        case 0x7f80: return dsw;
        default: return 0xff;
    }
}

void WrZ80(register word a, register byte d)
{
    if (a >= 0x6000 && a < 0x7000) { ram[a - 0x6000] = d; return; }
    if (a >= 0x7000 && a < 0x7400) { mb_sprram[a & 0x3ff] = d; return; }
    if (a >= 0x7400 && a < 0x7800) { mb_vram[a & 0x3ff] = d; return; }
    if (a < 0x7c00 || a >= 0x8000) return;
    switch (a & 0xff80) {
        case 0x7c00: mb_dbg_writes[0]++; mb_sound_run_w(0, d); return;   /* Mario's footsteps */
        case 0x7c80: mb_dbg_writes[1]++; mb_sound_run_w(1, d); return;   /* Luigi's */
        case 0x7d00: mb_scroll = d; return;
        case 0x7e00: mb_dbg_writes[2]++; mb_sound_latch_w(d); return;    /* the tune */
        case 0x7e80:                                                     /* the main latch */
            switch (a & 7) {
                case 0: mb_gfxbank = d & 1; break;
                case 2: mb_flip = d & 1; break;
                case 3: mb_palettebank = d & 1; break;
                case 4: nmi_mask = d & 1; break;
                case 5: if (d & 1) mb_dma_go(); break;                   /* DMA SET: the sprite list moves now */
                default: break;                                          /* coin counters */
            }
            return;
        case 0x7f00:                                                     /* the sound latch */
            mb_dbg_writes[3 + (a & 7)]++;
            mb_sound_sig_w(a & 7, d & 1);
            return;
        default: return;
    }
}

/* ---- the Z80 DMA: programmed through I/O port 0, fired by DMA SET on the main latch ---- */
/*
 * Enough of the Z80 DMA to follow the one program the game gives it every frame: a memory to
 * memory block transfer of the sprite list, port A the source at 0x6900, port B the sprite RAM
 * at 0x7000, 0x180 bytes. The registers are written as a command byte followed by however
 * many parameter bytes its bits call for, so this is a small state machine over that stream.
 */
static uint16_t dma_a, dma_b, dma_len;
static uint8_t  dma_pending[8], dma_npend, dma_slot[8], dma_dir;
enum { DMA_A_LO = 1, DMA_A_HI, DMA_LEN_LO, DMA_LEN_HI, DMA_B_LO, DMA_B_HI, DMA_SKIP };

void mb_dma_write(uint8_t v)
{
    if (dma_npend) {
        uint8_t what = dma_slot[0];
        memmove(dma_slot, dma_slot + 1, --dma_npend);
        switch (what) {
            case DMA_A_LO:   dma_a   = (uint16_t)((dma_a & 0xff00) | v); break;
            case DMA_A_HI:   dma_a   = (uint16_t)((dma_a & 0x00ff) | (v << 8)); break;
            case DMA_LEN_LO: dma_len = (uint16_t)((dma_len & 0xff00) | v); break;
            case DMA_LEN_HI: dma_len = (uint16_t)((dma_len & 0x00ff) | (v << 8)); break;
            case DMA_B_LO:   dma_b   = (uint16_t)((dma_b & 0xff00) | v); break;
            case DMA_B_HI:   dma_b   = (uint16_t)((dma_b & 0x00ff) | (v << 8)); break;
            default: break;
        }
        return;
    }
    dma_npend = 0;
    if (!(v & 0x80) && (v & 0x03) != 0x00) {                  /* WR0: bit 7 clear, bits 0-1 not 00 */
        dma_dir = (v >> 2) & 1;
        if (v & 0x08) dma_slot[dma_npend++] = DMA_A_LO;
        if (v & 0x10) dma_slot[dma_npend++] = DMA_A_HI;
        if (v & 0x20) dma_slot[dma_npend++] = DMA_LEN_LO;
        if (v & 0x40) dma_slot[dma_npend++] = DMA_LEN_HI;
    } else if ((v & 0x87) == 0x04 || (v & 0x87) == 0x00) {   /* WR1 / WR2: port configuration */
        if (v & 0x40) dma_slot[dma_npend++] = DMA_SKIP;        /* a timing byte */
    } else if ((v & 0x83) == 0x80) {                           /* WR3 */
        if (v & 0x08) dma_slot[dma_npend++] = DMA_SKIP;
        if (v & 0x10) dma_slot[dma_npend++] = DMA_SKIP;
    } else if ((v & 0x83) == 0x81) {                           /* WR4: mode, port B address */
        if (v & 0x04) dma_slot[dma_npend++] = DMA_B_LO;
        if (v & 0x08) dma_slot[dma_npend++] = DMA_B_HI;
        if (v & 0x10) dma_slot[dma_npend++] = DMA_SKIP;        /* interrupt control (and we ignore its own followers) */
    } else if ((v & 0xc7) == 0x82) {                           /* WR5 */
    } else if ((v & 0x83) == 0x83) {                           /* WR6: commands */
        if (v == 0xbb) dma_slot[dma_npend++] = DMA_SKIP;       /* read mask follows */
    }
}

void mb_dma_go(void)
{
    uint16_t n = (uint16_t)(dma_len + 1);
    uint16_t src = dma_dir ? dma_a : dma_b, dst = dma_dir ? dma_b : dma_a;
    for (uint16_t i = 0; i < n; i++) WrZ80((word)(dst + i), RdZ80((word)(src + i)));
}
byte InZ80(register word p) { (void)p; return 0xff; }
void OutZ80(register word p, register byte v)
{
    if ((p & 0xff) != 0) return;
    mb_dma_write(v);
}
void PatchZ80(register Z80 *R) { (void)R; }
word LoopZ80(register Z80 *R) { (void)R; return INT_QUIT; }

/* ---- public ---- */
void mb_reset(void)
{
    memset(ram, 0, sizeof(ram));
    memset(mb_vram, 0, sizeof(mb_vram));
    memset(mb_sprram, 0, sizeof(mb_sprram));
    memset(&input, 0, sizeof(input));
    mb_gfxbank = mb_palettebank = mb_flip = mb_scroll = 0;
    nmi_mask = 0; debt = 0;
    mb_sound_reset();
    ResetZ80(&cpu);
}

void mb_init(const mb_roms_t *r)
{
    mb_roms = *r;
    memset(&cpu, 0, sizeof(cpu));
    cpu.IPeriod = 1000000;
    mb_video_init();
    mb_sound_init();
    mb_reset();
}

void mb_set_dips(uint8_t d) { dsw = d; }
mb_input_t *mb_input(void) { return &input; }

void mb_run_frame(void)
{
    /* Run in slices so the sound MCU stays roughly in step with the game. */
    enum { SLICES = 16 };
    for (int s = 0; s < SLICES; s++) {
        int32_t cycles = MB_CYCLES_PER_FRAME / SLICES - debt;
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
        mb_sound_run(MB_CYCLES_PER_FRAME / SLICES);
    }
    if (nmi_mask) IntZ80(&cpu, INT_NMI);        /* vblank */
    frame_count++;
}

uint16_t mb_pc(void) { return cpu.PC.W; }
uint16_t mb_snd_pc(void) { return mb_sound_pc(); }
uint32_t mb_frame_count(void) { return frame_count; }
