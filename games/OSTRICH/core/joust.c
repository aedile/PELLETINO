/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro
 * Written for PELLETINO; hardware behaviour referenced against MAME (BSD-3-Clause).
 * See LICENSING.md, "Emulator cores and MAME". */
/*
 * joust.c - the 6809 CPU board: memory map, the ROM/RAM bank over the low 36 KB, the two PIAs,
 * the scanline interrupts and the SC1 blitter. The 6809 is vecx's e6809, included here so its
 * bus accessors inline. The sound board is in joust_sound.c, the picture in joust_video.c.
 */
#include "joust_internal.h"
#include <string.h>
#include <stdio.h>
#include "e6809.h"

jo_roms_t jo_roms;
#ifdef JO_DEBUG
int jo_dbg_cmos_log = 0;
#endif
uint8_t jo_vram[0xc000];
uint8_t jo_palram[16];

static uint8_t cmos[0x400];
static jo_input_t input;
static uint32_t frame_count;
static int rom_bank, cur_line;
static int32_t cycle_debt, blit_stall;
static uint8_t irq_line;

/* ---- a 6821 PIA, enough of one for these three ---- */
typedef struct {
    uint8_t ddr[2], out[2], cr[2];
    uint8_t c1[2];                 /* the CA1 / CB1 input lines */
} pia_t;
static pia_t pia[2];

static inline int pia_irq(const pia_t *p)
{
    return ((p->cr[0] & 0x80) && (p->cr[0] & 0x01)) || ((p->cr[1] & 0x80) && (p->cr[1] & 0x01));
}

/* an edge on CA1 or CB1; `port` 0 = A, 1 = B */
static void pia_c1_w(pia_t *p, int port, int state)
{
    state = state ? 1 : 0;
    if (state != p->c1[port]) {
        int rising = (p->cr[port] & 0x02) != 0;
        if (state == rising) p->cr[port] |= 0x80;
        p->c1[port] = state;
    }
}

/* the C2 line as an output in set/reset mode (bits 5 and 4 set): its level is bit 3 */
static inline int pia_c2_out(const pia_t *p, int port) { return (p->cr[port] & 0x38) == 0x38; }

/* ---- the inputs ---- */
static uint8_t inp_player(int player)
{
    /* both muxes; player 2's stick never exists here */
    if (player != 1) return 0;
    uint8_t v = 0;
    if (input.left)  v |= 0x01;
    if (input.right) v |= 0x02;
    if (input.flap)  v |= 0x04;
    return v;
}
static uint8_t pia0_in_a(void)
{
    /* IN0: bit 4 start 2, bit 5 start 1; low nibble and top two bits are the muxed sticks
     * (CB2 high picks player 1) */
    uint8_t v = 0;
    if (input.start2) v |= 0x10;
    if (input.start1) v |= 0x20;
    v |= inp_player(pia_c2_out(&pia[0], 1) ? 1 : 2) & 0x0f;
    return v;
}
static int auto_advance;
static uint8_t pia1_in_a(void)
{
    /* IN2: coin 1 is bit 4, the ADVANCE switch inside the coin door is bit 1; the rest stay off */
    uint8_t v = input.coin1 ? 0x10 : 0x00;
    if (auto_advance) v |= 0x02;
    return v;
}

static uint8_t pia_read(int n, int reg)
{
    pia_t *p = &pia[n];
    int port = reg >> 1;
    if (reg & 1) return p->cr[port];
    if (!(p->cr[port] & 0x04)) return p->ddr[port];
    p->cr[port] &= 0x3f;                                    /* a port read clears its flags */
    uint8_t in = 0;
    if (n == 0) in = port ? 0 : pia0_in_a();
    else        in = port ? p->out[1] : pia1_in_a();
    return (uint8_t)((in & ~p->ddr[port]) | (p->out[port] & p->ddr[port]));
}

static void pia_write(int n, int reg, uint8_t d)
{
    pia_t *p = &pia[n];
    int port = reg >> 1;
    if (reg & 1) {
        p->cr[port] = (uint8_t)((p->cr[port] & 0xc0) | (d & 0x3f));
        return;
    }
    if (!(p->cr[port] & 0x04)) { p->ddr[port] = d; return; }
    p->out[port] = d;
    if (n == 1 && port == 1) jo_sound_cmd((uint8_t)(d | 0xc0));   /* the top two lines are pulled up */
}

/* ---- the bus ---- */
static uint8_t cpu_read(unsigned a)
{
    a &= 0xffff;
    if (a < 0x9000) return rom_bank ? jo_roms.rom_lo[a] : jo_vram[a];
    if (a < 0xc000) return jo_vram[a];
    if (a >= 0xd000) return jo_roms.rom_hi[a - 0xd000];
    if (a >= 0xcc00) return cmos[a & 0x3ff];
    switch (a & 0xff00) {
        case 0xc800:
            if ((a & 0x0c) == 0x04) return pia_read(0, a & 3);
            if ((a & 0x0c) == 0x0c) return pia_read(1, a & 3);
            return 0xff;
        case 0xcb00: return (uint8_t)(cur_line < 0x100 ? (cur_line & 0xfc) : 0xfc);
        default: return 0xff;
    }
}

static void blit_control_w(uint8_t d);
static uint8_t blit_reg[8];

static void cpu_write(unsigned a, unsigned char d)
{
    a &= 0xffff;
    if (a < 0xc000) { jo_vram[a] = d; return; }            /* the ROM only sits over the RAM's reads */
    if (a >= 0xd000) return;
    if (a >= 0xcc00) {
#ifdef JO_DEBUG
        extern int jo_dbg_cmos_log; if (jo_dbg_cmos_log) printf("  cmos[%03X] = %X at pc %04X\n", a & 0x3ff, d & 0xf, e6809_get_pc() & 0xffff);
#endif
        cmos[a & 0x3ff] = (uint8_t)(d | 0xf0); return;
    }
    switch (a & 0xff00) {
        case 0xc000: jo_palram[a & 0x0f] = d; break;
        case 0xc800:
            if ((a & 0x0c) == 0x04) pia_write(0, a & 3, d);
            else if ((a & 0x0c) == 0x0c) pia_write(1, a & 3, d);
            break;
        case 0xc900: rom_bank = d & 1; break;               /* bit 1 would flip a cocktail */
        case 0xca00:
            blit_reg[a & 7] = d;
            if ((a & 7) == 0) blit_control_w(d);
            break;
        default: break;                                     /* 0xcbff: watchdog */
    }
}

#define E6809_READ8(a)     cpu_read(a)
#define E6809_WRITE8(a, d) cpu_write((a), (d))
#include "e6809.c"

/*
 * The Special Chip 1. Width and height come xor 4 (the SC1's bug, which the programs allow
 * for). Bit 0 of the control byte says the source is laid out as the screen is, 256 bytes from
 * one pixel pair to the next below it; bit 1 says the same of the destination. Bit 3 leaves
 * pixels of colour 0 alone, bit 4 paints with the solid colour instead of the source, bit 5
 * shifts the image a pixel right, bits 6 and 7 leave the even or the odd pixels of every pair
 * alone. Bit 2 only slows it down.
 */
static inline void blit_pixel(unsigned dst, uint8_t src, uint8_t ctl)
{
    uint8_t cur = dst < 0xc000 ? jo_vram[dst] : cpu_read(dst);
    uint8_t keep = 0xff;
    int fg_only = ctl & 0x08, no_even = ctl & 0x80, no_odd = ctl & 0x40;
    if (fg_only && !(src & 0xf0)) { if (no_even) keep &= 0x0f; }
    else if (!no_even) keep &= 0x0f;
    if (fg_only && !(src & 0x0f)) { if (no_odd) keep &= 0xf0; }
    else if (!no_odd) keep &= 0xf0;
    cur &= keep;
    cur |= (uint8_t)(((ctl & 0x10) ? blit_reg[1] : src) & ~keep);
    cpu_write(dst, cur);
}

static void blit_control_w(uint8_t ctl)
{
    int w = blit_reg[6] ^ 4, h = blit_reg[7] ^ 4;
    if (!w) w = 1;
    if (!h) h = 1;
    unsigned sstart = (unsigned)((blit_reg[2] << 8) | blit_reg[3]);
    unsigned dstart = (unsigned)((blit_reg[4] << 8) | blit_reg[5]);
    int src_screen = ctl & 0x01, dst_screen = ctl & 0x02, shift = ctl & 0x20;
    int sxadv = src_screen ? 0x100 : 1, syadv = src_screen ? 1 : w;
    int dxadv = dst_screen ? 0x100 : 1, dyadv = dst_screen ? 1 : w;
    unsigned pixdata = 0;
    for (int y = 0; y < h; y++) {
        unsigned s = sstart, d = dstart;
        for (int x = 0; x < w; x++) {
            uint8_t raw = cpu_read(s);
            if (shift) { pixdata = (pixdata << 8) | raw; blit_pixel(d & 0xffff, (uint8_t)(pixdata >> 4), ctl); }
            else blit_pixel(d & 0xffff, raw, ctl);
            s = (s + sxadv) & 0xffff; d = (d + dxadv) & 0xffff;
        }
        if (dst_screen) dstart = (dstart & 0xff00) | ((dstart + dyadv) & 0xff); else dstart = (dstart + dyadv) & 0xffff;
        if (src_screen) sstart = (sstart & 0xff00) | ((sstart + syadv) & 0xff); else sstart = (sstart + syadv) & 0xffff;
    }
    /* the chip halts the CPU while it works: two accesses a pixel at 4 MHz, or four when slow */
    int accesses = 2 * w * h;
    int clocks = 4 + ((ctl & 0x04) ? 4 * (accesses + 2) : 2 * (accesses + 3));
    blit_stall += (clocks + 3) / 4;
}

/* ---- public ---- */
void jo_reset(void)
{
    memset(jo_vram, 0, sizeof(jo_vram));
    memset(jo_palram, 0, sizeof(jo_palram));
    memset(pia, 0, sizeof(pia));
    memset(blit_reg, 0, sizeof(blit_reg));
    memset(&input, 0, sizeof(input));
    rom_bank = 0; cur_line = 0; cycle_debt = 0; blit_stall = 0; irq_line = 0;
    jo_sound_reset();
    e6809_reset();
}

void jo_init(const jo_roms_t *r)
{
    jo_roms = *r;
    memset(cmos, 0xf0, sizeof(cmos));
    jo_sound_init();
    jo_reset();
}

jo_input_t *jo_input(void) { return &input; }

#ifdef JO_DEBUG
uint32_t jo_dbg_hist[0x10000];
uint64_t jo_dbg_steps, jo_dbg_cycles, jo_dbg_stall, jo_dbg_snd_steps;
#endif
static void run_cycles(int32_t n)
{
    int32_t budget = n - cycle_debt;
    while (budget > 0) {
#ifdef JO_DEBUG
        jo_dbg_hist[e6809_get_pc() & 0xffff]++;
#endif
        irq_line = (uint8_t)pia_irq(&pia[1]);
        int32_t cy = (int32_t)e6809_sstep(irq_line, 0);
#ifdef JO_DEBUG
        jo_dbg_steps++; jo_dbg_cycles += cy; jo_dbg_stall += blit_stall;
#endif
        budget -= cy;
        budget -= blit_stall; blit_stall = 0;
    }
    cycle_debt = -budget;
}

void jo_run_frame(void)
{
    /*
     * Eight lines a slice. VA11 - bit 5 of the line count - reaches PIA 1's CB1 and changes
     * every 32 lines (but not at 256); COUNT240 reaches CA1 and is high from line 240 to the
     * end of the frame. The program picks the edges it wants.
     */
    int32_t snd_acc = 0;
    for (int line = 0; line < JO_LINES; line += 8) {
        int lines = (line + 8 <= JO_LINES) ? 8 : JO_LINES - line;
        cur_line = line;
        if ((line & 31) == 0 && line != 256) pia_c1_w(&pia[1], 1, (line >> 5) & 1);
        pia_c1_w(&pia[1], 0, line >= 240);
        run_cycles(lines * JO_CYCLES_PER_LINE);
        snd_acc += lines * JO_SND_CYCLES_PER_FRAME;
        jo_sound_run(snd_acc / JO_LINES);
        snd_acc %= JO_LINES;
    }
    /*
     * The CMOS is empty at every power-up here, so the program restores its factory settings
     * and then, as the real board does, waits for the operator to press ADVANCE inside the
     * coin door. The medal has no coin door: press it for them.
     */
    unsigned pc = e6809_get_pc() & 0xffff;
    auto_advance = (pc >= 0x3d69 && pc < 0x3d75);
    frame_count++;
}

uint16_t jo_pc(void) { return (uint16_t)e6809_get_pc(); }
uint32_t jo_frame_count(void) { return frame_count; }
