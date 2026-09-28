/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * mpatrol.c - the M52 CPU board: memory and port maps, inputs, the vblank interrupt, and the
 * per-frame pacing of the sound board. The Z80 is Marat Fayzullin's portable core.
 */
#include "mpatrol_internal.h"
#include "Z80.h"
#include <string.h>

mp_roms_t mp_roms;
uint8_t mp_vram[0x400], mp_cram[0x400], mp_sprram[0x100];
uint8_t mp_scroll, mp_bgx[2], mp_bgy[2], mp_bgctl;

static Z80 cpu;
const uint8_t *mp_z80_rom;           /* for the fetch fast path in z80_fastop.h */
uint8_t mp_z80_ram[0x800];
#define ram mp_z80_ram
static uint8_t dsw1 = 0xfe;          /* 3 lives, bonus 10000/30000/50000, 1 coin 1 play */
static uint8_t dsw2 = 0xfd;          /* upright, coin mode 1, no cheats, service off */
static mp_input_t input;
static uint32_t frame_count;

/* ---- inputs, active low ---- */
static uint8_t read_in0(void)
{
    uint8_t v = 0xff;
    if (input.start1) v &= (uint8_t)~0x01;
    if (input.start2) v &= (uint8_t)~0x02;
    if (input.coin1)  v &= (uint8_t)~0x08;
    return v;
}
static uint8_t read_in1(void)
{
    uint8_t v = 0xff;
    if (input.right) v &= (uint8_t)~0x01;
    if (input.left)  v &= (uint8_t)~0x02;
    if (input.jump)  v &= (uint8_t)~0x20;
    if (input.fire)  v &= (uint8_t)~0x80;
    return v;
}

/* a custom chip on the scroll board mangles the last background position written */
static uint8_t protection_r(void)
{
    int n = 0;
    for (int t = mp_bgx[0] & 0x7f; t; t >>= 1) n += t & 1;
    return (uint8_t)(n ^ (mp_bgx[0] >> 7));
}

byte RdZ80(register word a)
{
    if (a < 0x4000) return mp_roms.rom[a];
    if (a >= 0xe000 && a < 0xe800) return ram[a & 0x7ff];
    if (a >= 0x8000 && a < 0x8400) return mp_vram[a & 0x3ff];
    if (a >= 0x8400 && a < 0x8800) return mp_cram[a & 0x3ff];
    if (a >= 0x8800 && a < 0x9000) return protection_r();
    if (a >= 0xd000 && a < 0xd800) {
        switch (a & 7) {
            case 0: return read_in0();
            case 1: return read_in1();
            case 2: return 0xff;                        /* the cocktail player and coin 2 */
            case 3: return dsw1;
            case 4: return dsw2;
            default: return 0xff;
        }
    }
    return 0xff;
}

void WrZ80(register word a, register byte d)
{
    if (a >= 0xe000 && a < 0xe800) { ram[a & 0x7ff] = d; return; }
    if (a >= 0x8000 && a < 0x8400) { mp_vram[a & 0x3ff] = d; return; }
    if (a >= 0x8400 && a < 0x8800) { mp_cram[a & 0x3ff] = d; return; }
    if (a >= 0xc800 && a < 0xd000) { mp_sprram[a & 0xff] = d; return; }
    if (a >= 0xd000 && a < 0xd800) {
        if ((a & 3) == 0) mp_sound_cmd(d);
        /* d001: screen flip and the coin counters */
        return;
    }
}

byte InZ80(register word p) { (void)p; return 0xff; }
void OutZ80(register word p, register byte v)
{
    switch (p & 0xe0) {
        case 0x00: mp_scroll = v; break;
        case 0x40: mp_bgx[0] = v; break;
        case 0x60: mp_bgy[0] = v; break;
        case 0x80: mp_bgx[1] = v; break;
        case 0xa0: mp_bgy[1] = v; break;
        case 0xc0: mp_bgctl = v; break;
        default: break;
    }
}
void PatchZ80(register Z80 *R) { (void)R; }
word LoopZ80(register Z80 *R) { (void)R; return INT_QUIT; }

/* ---- public ---- */
void mp_reset(void)
{
    memset(ram, 0, sizeof(ram));
    memset(mp_vram, 0, sizeof(mp_vram));
    memset(mp_cram, 0, sizeof(mp_cram));
    memset(mp_sprram, 0, sizeof(mp_sprram));
    memset(&input, 0, sizeof(input));
    mp_scroll = 0; mp_bgx[0] = mp_bgx[1] = 0; mp_bgy[0] = mp_bgy[1] = 0; mp_bgctl = 0;
    mp_sound_reset();
    ResetZ80(&cpu);
    cpu.IPeriod = 1000000;
}

void mp_init(const mp_roms_t *r)
{
    mp_roms = *r;
    mp_z80_rom = r->rom;
    mp_video_init();
    mp_sound_init();
    mp_reset();
}

void mp_set_dips(uint8_t d1, uint8_t d2) { dsw1 = d1; dsw2 = d2; }
mp_input_t *mp_input(void) { return &input; }

static void run_cycles(int32_t cycles)
{
    cpu.IPeriod = cycles;
    cpu.ICount = cycles;
    RunZ80(&cpu);
}

void mp_run_frame(void)
{
    /* one interrupt a frame, at the start of vblank; the sound board keeps pace in slices */
    enum { SLICES = 8 };
    int32_t snd_acc = 0;
    for (int s = 0; s < SLICES; s++) {
        if (s == 0) IntZ80(&cpu, INT_IRQ);
        run_cycles(MP_CYCLES_PER_FRAME / SLICES);
        snd_acc += MP_SND_CYCLES_PER_FRAME;
        mp_sound_run(snd_acc / SLICES);
        snd_acc %= SLICES;
    }
    frame_count++;
}

uint16_t mp_pc(void) { return cpu.PC.W; }
uint32_t mp_frame_count(void) { return frame_count; }
