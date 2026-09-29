/*
 * asteroids.c - Atari Asteroids board: memory map, Digital Vector Generator, sound.
 * The 6502 is the instruction-stepped core in m6502fast.h.
 */
#include "asteroids.h"
#include <string.h>

static ast_roms_t roms;
static uint8_t ram[0x400];               /* 0x0000-0x03FF work RAM */
static uint8_t vecram[0x800];            /* 0x4000-0x47FF display list */
static uint8_t dsw1 = 0x84;              /* English, 3 ships, 1 coin 1 play */
static ast_input_t input;
static uint32_t frame_count;
static uint32_t total_cycles;
static int32_t cycle_debt;

/* ---- vector generator ---- */
static ast_line_t lines_a[AST_MAX_LINES], lines_b[AST_MAX_LINES];
static ast_line_t *lines_draw = lines_a, *lines_done = lines_b;
static int nlines_draw, nlines_done;
static uint32_t dvg_go_cycle, dvg_busy_cycles;   /* the DVG holds VG_HALT low while it runs */

/* ---- sound latches ---- */
static uint8_t snd_saucer, snd_saucer_fire, snd_saucer_sel, snd_thrust, snd_fire, snd_life;
static uint8_t thump_en, thump_data, explode_vol, explode_pitch;

/* one word of the DVG's address space: vector RAM at 0x4000, vector ROM at 0x5000 */
static inline uint16_t vec_word(uint16_t pc)
{
    uint16_t a = (uint16_t)(pc << 1);                  /* byte offset from 0x4000 */
    if (a < 0x800) return (uint16_t)(vecram[a] | (vecram[a + 1] << 8));
    if (a >= 0x1000 && a < 0x1800) return (uint16_t)(roms.vecrom[a - 0x1000] | (roms.vecrom[a - 0x1000 + 1] << 8));
    return 0;                                          /* unmapped: reads as zero */
}

static inline void emit(int x0, int y0, int x1, int y1, int bright)
{
    if (nlines_draw >= AST_MAX_LINES) return;
    ast_line_t *l = &lines_draw[nlines_draw++];
    l->x0 = (int16_t)x0; l->y0 = (int16_t)y0;
    l->x1 = (int16_t)x1; l->y1 = (int16_t)y1;
    l->bright = (uint8_t)bright;
}

/*
 * One VCTR or SVEC. The hardware walks the vector with a pair of cascaded 7497 rate
 * multipliers: over `fin` clocks the x counter steps `fin * mx / 4096` times, and likewise
 * for y, which is what gives the line its slope. We can compute the endpoint directly.
 */
static void dvg_vector(int scale, int dvx, int dvy, int z, int *xpos, int *ypos)
{
    int fin = (2 << (scale & 0x0f)) & 0x7ff;
    int mx = (dvx << 2) & 0xfff;
    int my = (dvy << 2) & 0xfff;
    int sx = (fin * mx) >> 12;
    int sy = (fin * my) >> 12;
    int nx = *xpos + ((dvx & 0x400) ? -sx : sx);
    int ny = *ypos + ((dvy & 0x400) ? -sy : sy);
    if (z) emit(*xpos, *ypos, nx, ny, z);
    *xpos = nx; *ypos = ny;
    dvg_busy_cycles += (uint32_t)fin;      /* the DVG spends `fin` CPU cycles on this vector */
}

static void dvg_run(void)
{
    uint16_t pc = 0, stack[4];
    int sp = 0, scale = 0, xpos = 0, ypos = 0;
    nlines_draw = 0;
    dvg_busy_cycles = 0;
    /* the display list is the CPU's own data: a corrupt one must not hang us */
    for (int guard = 0; guard < 4096; guard++) {
        uint16_t w0 = vec_word(pc++);
        int op = w0 >> 12;
        if (op <= 0x0a) {
            uint16_t w1 = vec_word(pc++);
            int dvy = w0 & 0xfff, dvx = w1 & 0xfff, z = w1 >> 12;
            if (op == 0x0a) { xpos = dvx; ypos = dvy; scale = z; }   /* LABS */
            else dvg_vector(scale + op, dvx, dvy, z, &xpos, &ypos);  /* VCTR */
        } else if (op == 0x0b) {                                     /* HALT */
            break;
        } else if (op == 0x0c) {                                     /* JSRL */
            stack[sp++ & 3] = pc; pc = w0 & 0xfff;
        } else if (op == 0x0d) {                                     /* RTSL */
            pc = stack[--sp & 3];
        } else if (op == 0x0e) {                                     /* JMPL */
            pc = w0 & 0xfff;
        } else {                                                     /* SVEC, one word */
            int dvy = w0 & 0xf00;
            int dvx = (w0 & 0x00f) << 8;
            int z = (w0 >> 4) & 0x0f;
            /* the short vector borrows three scale bits from the top bits of dx and dy */
            int adj = ((dvy & 0x800) >> 11) | (((dvx & 0x800) ^ 0x800) >> 10) | ((dvx & 0x800) >> 9);
            dvg_vector(scale + adj, dvx, dvy, z, &xpos, &ypos);
        }
    }
    /* swap: the list the CPU just built becomes the one the display shows */
    ast_line_t *t = lines_done; lines_done = lines_draw; lines_draw = t;
    nlines_done = nlines_draw;
}

/* ---- bus ---- */
static uint8_t bus_read(uint16_t addr)
{
    uint16_t a = addr & 0x7fff;                /* A15 is not decoded */
    if (a < 0x400) return ram[a];
    if (a >= 0x6800) return roms.rom[a - 0x6800];
    if (a >= 0x5000 && a < 0x5800) return roms.vecrom[a - 0x5000];
    if (a >= 0x4000 && a < 0x4800) return vecram[a - 0x4000];
    switch (a & 0xff00) {
        case 0x2000: {                         /* IN0, one bit per address in bit 7 */
            int bit = a & 7, v;
            switch (bit) {
                case 1: v = (total_cycles & 0x100) ? 1 : 0; break;   /* the 3 kHz clock */
                /* VG_HALT is wired active low: the bit reads 1 while the DVG is still running */
                case 2: v = (total_cycles - dvg_go_cycle < dvg_busy_cycles) ? 1 : 0; break;
                case 3: v = input.hyperspace; break;
                case 4: v = input.fire; break;
                default: v = 0; break;         /* diagnostic step, tilt, self-test all off */
            }
            return v ? 0x80 : 0x7f;
        }
        case 0x2400: {                         /* IN1, likewise */
            int bit = a & 7, v;
            switch (bit) {
                case 0: v = input.coin1; break;
                case 3: v = input.start1; break;
                case 4: v = input.start2; break;
                case 5: v = input.thrust; break;
                case 6: v = input.right; break;
                case 7: v = input.left; break;
                default: v = 0; break;
            }
            return v ? 0x80 : 0x7f;
        }
        case 0x2800: {                         /* DSW1 through a 74LS253: two bits per address */
            int pair = 3 - (a & 3);
            return (uint8_t)(0xfc | ((dsw1 >> (pair * 2)) & 3));
        }
        default: return 0xff;
    }
}

static void bus_write(uint16_t addr, uint8_t data)
{
    uint16_t a = addr & 0x7fff;
    if (a < 0x400) { ram[a] = data; return; }
    if (a >= 0x4000 && a < 0x4800) { vecram[a - 0x4000] = data; return; }
    switch (a & 0xff00) {
        case 0x3000:                           /* VGGO: draw the list the CPU just built */
            dvg_go_cycle = total_cycles;
            dvg_run();
            return;
        case 0x3200: return;                   /* start lamps, coin counters, RAM select */
        case 0x3400: return;                   /* watchdog */
        case 0x3600:                           /* explosion: volume and pitch */
            explode_vol = (uint8_t)((data & 0x3c) >> 2);
            switch (data & 0xc0) {
                case 0x00: explode_pitch = 12; break;
                case 0x40: explode_pitch = 6;  break;
                case 0x80: explode_pitch = 3;  break;
                default:   explode_pitch = 5;  break;
            }
            return;
        case 0x3a00:                           /* the background thump VCO */
            thump_en = (data & 0x10) ? 1 : 0;
            thump_data = data & 0x0f;
            return;
        case 0x3c00: {                         /* LS259 audio latch, one bit per address */
            uint8_t on = (data & 0x80) ? 1 : 0;
            switch (a & 7) {
                case 0: snd_saucer = on; break;
                case 1: snd_saucer_fire = on; break;
                case 2: snd_saucer_sel = on; break;
                case 3: snd_thrust = on; break;
                case 4: snd_fire = on; break;
                case 5: snd_life = on; break;
                default: break;
            }
            return;
        }
        case 0x3e00: return;                   /* noise reset */
        default: return;
    }
}

#define M6502F_READ(a)     bus_read(a)
#define M6502F_WRITE(a, v) bus_write(a, v)
#include "m6502fast.h"

static m6502f_t cpu;

/* ---- public ---- */
void ast_reset(void)
{
    memset(ram, 0, sizeof(ram));
    memset(vecram, 0, sizeof(vecram));
    memset(&input, 0, sizeof(input));
    nlines_draw = nlines_done = 0;
    total_cycles = 0; cycle_debt = 0;
    dvg_go_cycle = 0; dvg_busy_cycles = 0;
    snd_saucer = snd_saucer_fire = snd_saucer_sel = snd_thrust = snd_fire = snd_life = 0;
    thump_en = thump_data = explode_vol = 0; explode_pitch = 12;
    m6502f_reset(&cpu);
}

void ast_init(const ast_roms_t *r) { roms = *r; ast_reset(); }
void ast_set_dips(uint8_t d) { dsw1 = d; }
ast_input_t *ast_input(void) { return &input; }

static void run_cycles(int32_t n)
{
    int32_t budget = n - cycle_debt;
    while (budget > 0) {
        int cy = m6502f_step(&cpu);
        total_cycles += (uint32_t)cy;
        budget -= cy;
    }
    cycle_debt = -budget;
}

void ast_run_frame(void)
{
    for (int i = 0; i < AST_NMIS_PER_FRAME; i++) {
        run_cycles(AST_NMI_CYCLES);
        /* the periodic interrupt is an NMI, and is inhibited only by the self-test switch */
        total_cycles += (uint32_t)m6502f_nmi(&cpu);
    }
    frame_count++;
}

const ast_line_t *ast_lines(int *count) { *count = nlines_done; return lines_done; }

uint16_t ast_pc(void) { return cpu.pc; }
uint32_t ast_frame_count(void) { return frame_count; }
uint32_t ast_idle_cycles(void) { return 0; }
const uint8_t *ast_ram(void) { return ram; }

/* ---- sound ----
 * The board's audio is discrete analogue: a 555 thump VCO, an LFSR noise source and a few
 * one-shots. This is a synthesiser driven from the same latches, not a netlist.
 */
static uint32_t sr_noise = 0x1u;
static float ph_thump, ph_saucer, ph_fire, ph_sfire, ph_life, warble;
static float env_fire, env_sfire, env_life, env_explode, lp_thrust, lp_explode;
static uint8_t prev_fire, prev_sfire, prev_life;
static float explode_hold;

static inline float noise_step(void)
{                                        /* 17-bit maximal LFSR, as on the board */
    uint32_t bit = ((sr_noise >> 0) ^ (sr_noise >> 3)) & 1;
    sr_noise = (sr_noise >> 1) | (bit << 16);
    return (sr_noise & 1) ? 1.0f : -1.0f;
}

void ast_render_audio(int16_t *buf, int samples, int rate)
{
    const float dt = 1.0f / (float)rate;
    /* the 4-bit thump DAC drives a 555 current source: more data, lower pitch */
    const float thump_hz = 90.0f + (float)(15 - thump_data) * 28.0f;
    const float saucer_hz = snd_saucer_sel ? 480.0f : 300.0f;

    if (snd_fire && !prev_fire) { env_fire = 1.0f; ph_fire = 0.0f; }
    prev_fire = snd_fire;
    if (snd_saucer_fire && !prev_sfire) { env_sfire = 1.0f; ph_sfire = 0.0f; }
    prev_sfire = snd_saucer_fire;
    if (snd_life && !prev_life) { env_life = 1.0f; ph_life = 0.0f; }
    prev_life = snd_life;
    /* an explosion is a write of a fresh volume, not a level, so retrigger on a rise */
    if (explode_vol > 0 && (float)explode_vol / 15.0f > explode_hold) {
        env_explode = (float)explode_vol / 15.0f;
    }
    explode_hold = (float)explode_vol / 15.0f;

    for (int i = 0; i < samples; i++) {
        float out = 0.0f;

        if (thump_en) {                                  /* the background heartbeat */
            ph_thump += thump_hz * dt;
            if (ph_thump >= 1.0f) ph_thump -= 1.0f;
            out += (ph_thump < 0.5f ? 0.30f : -0.30f);
        }

        float n = noise_step();
        if (snd_thrust) {                                /* filtered noise rumble */
            lp_thrust += (n - lp_thrust) * 0.02f;
            out += lp_thrust * 0.55f;
        }
        if (env_explode > 0.0005f) {                     /* noise through a pitch divider */
            float k = 0.35f / (float)explode_pitch;
            lp_explode += (n - lp_explode) * k;
            out += lp_explode * env_explode * 1.6f;
            env_explode -= env_explode * 2.2f * dt;
        }
        if (snd_saucer) {                                /* two tones, alternating */
            warble += dt;
            if (warble >= 0.18f) warble -= 0.18f;
            float f = saucer_hz * (warble < 0.09f ? 1.0f : 0.75f);
            ph_saucer += f * dt;
            if (ph_saucer >= 1.0f) ph_saucer -= 1.0f;
            out += (ph_saucer < 0.5f ? 0.18f : -0.18f);
        }
        if (env_fire > 0.002f) {                         /* the player's shot: a falling pew */
            float f = 700.0f + 1500.0f * env_fire;
            ph_fire += f * dt; if (ph_fire >= 1.0f) ph_fire -= 1.0f;
            out += (ph_fire < 0.5f ? 0.16f : -0.16f) * env_fire;
            env_fire -= env_fire * 22.0f * dt;
        }
        if (env_sfire > 0.002f) {
            float f = 400.0f + 900.0f * env_sfire;
            ph_sfire += f * dt; if (ph_sfire >= 1.0f) ph_sfire -= 1.0f;
            out += (ph_sfire < 0.5f ? 0.15f : -0.15f) * env_sfire;
            env_sfire -= env_sfire * 18.0f * dt;
        }
        if (env_life > 0.002f) {                         /* the extra-ship ping */
            ph_life += 900.0f * dt; if (ph_life >= 1.0f) ph_life -= 1.0f;
            out += (ph_life < 0.5f ? 0.22f : -0.22f) * env_life;
            env_life -= env_life * 9.0f * dt;
        }

        int32_t v = (int32_t)(out * 14000.0f);
        buf[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
    }
}

/* the byte at a CPU address, where that is memory - for keeping the high scores */
uint8_t *ast_mem(uint16_t a)
{
    if (a < 0x400) return &ram[a];
    if (a >= 0x4000 && a < 0x4800) return &vecram[a - 0x4000];
    return 0;
}
