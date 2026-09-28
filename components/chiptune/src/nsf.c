/*
 * nsf.c - plays NES Sound Format files.
 *
 * An NSF is the music driver lifted out of a game: 6502 code with an INIT entry
 * and a PLAY entry, plus the data they read. Playing one means running INIT
 * once, then PLAY sixty times a second, and listening to what they write to the
 * sound chip. So this is a 6502 (the instruction-stepped core the Atari games
 * here already use), two kilobytes of RAM, and the APU in apu2a03.c.
 *
 * The file is executed in place from flash; only the RAM is allocated.
 *
 * Expansion sound chips (VRC6, FDS, N163 and the rest) are not emulated. A file
 * that uses one still plays, without those channels.
 */
#include "player.h"
#include "apu2a03.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "nsf";

#define HDR        0x80
#define SENTINEL   0x4ff0               /* a return address nothing real lives at */
#define INIT_BUDGET 4000000             /* CPU cycles INIT may take before we give up on it */
#define PLAY_BUDGET  120000             /* four frames: a PLAY that overruns is cut off */

static const uint8_t *rom;              /* the program, after the header */
static size_t   rom_len;
static uint16_t load_addr, init_addr, play_addr;
static uint8_t  songs, first_song, song;
static uint16_t speed_us;
static bool     banked;
static uint8_t  bank[8], bank_init[8];
static uint8_t *ram, *sram;             /* 0x0000-0x07ff and 0x6000-0x7fff */
static apu_t    apu;
static uint32_t spp_q16;                /* samples per PLAY call, 16.16 */
static int32_t  until_q16;

static uint8_t nsf_rd(uint16_t a)
{
    if (a < 0x2000) return ram[a & 0x7ff];
    if (a >= 0x8000) {
        long off = banked
            ? (long)bank[(a - 0x8000) >> 12] * 4096 + (a & 0xfff) - (load_addr & 0xfff)
            : (long)a - load_addr;
        return (off >= 0 && (size_t)off < rom_len) ? rom[off] : 0;
    }
    if (a >= 0x6000) return sram[a - 0x6000];
    if (a == 0x4015) return apu_status(&apu);
    return 0;
}

static void nsf_wr(uint16_t a, uint8_t v)
{
    if (a < 0x2000) ram[a & 0x7ff] = v;
    else if (a >= 0x4000 && a <= 0x4017) apu_write(&apu, a, v);
    else if (a >= 0x5ff8 && a <= 0x5fff) { if (banked) bank[a - 0x5ff8] = v; }
    else if (a >= 0x6000 && a < 0x8000) sram[a - 0x6000] = v;
}

#define M6502F_READ(a)      nsf_rd((uint16_t)(a))
#define M6502F_WRITE(a, v)  nsf_wr((uint16_t)(a), (uint8_t)(v))
#define M6502F_NO_DECIMAL
#include "m6502fast.h"

static m6502f_t cpu;

/* JSR to a routine and run it until it returns, or until it has plainly hung */
static void call(uint16_t addr, uint8_t a, uint8_t x, int budget)
{
    cpu.a = a; cpu.x = x; cpu.y = 0;
    cpu.s = 0xff;
    cpu.p = M6502F_U | M6502F_I;
    nsf_wr((uint16_t)(0x0100 + cpu.s--), (uint8_t)((SENTINEL - 1) >> 8));
    nsf_wr((uint16_t)(0x0100 + cpu.s--), (uint8_t)((SENTINEL - 1) & 0xff));
    cpu.pc = addr;
    while (cpu.pc != SENTINEL && budget > 0) budget -= m6502f_step(&cpu);
    if (budget <= 0) ESP_LOGW(TAG, "routine at %04x did not return", addr);
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static void nsf_rewind(void)
{
    if (!ram) return;
    memset(ram, 0, 0x800);
    memset(sram, 0, 0x2000);
    memcpy(bank, bank_init, sizeof bank);
    apu_reset(&apu, nsf_rd);
    for (uint16_t r = 0x4000; r <= 0x4013; r++) apu_write(&apu, r, r == 0x4010 ? 0x10 : 0);
    apu_write(&apu, 0x4015, 0x0f);
    apu_write(&apu, 0x4017, 0x40);
    call(init_addr, (uint8_t)(song - 1), 0 /* NTSC */, INIT_BUDGET);
    until_q16 = 0;
    spp_q16 = 0;                        /* worked out at the first render, when the rate is known */
}

static bool nsf_load(const uint8_t *d, size_t len, int track)
{
    if (len <= HDR || memcmp(d, "NESM\x1a", 5)) return false;
    songs      = d[6] ? d[6] : 1;
    first_song = d[7] ? d[7] : 1;
    load_addr  = le16(d + 8);
    init_addr  = le16(d + 10);
    play_addr  = le16(d + 12);
    speed_us   = le16(d + 0x6e);
    if (!speed_us) speed_us = 16639;
    if (load_addr < 0x8000) { ESP_LOGW(TAG, "load address %04x is below 8000", load_addr); return false; }

    memcpy(bank_init, d + 0x70, 8);
    banked = false;
    for (int i = 0; i < 8; i++) if (bank_init[i]) banked = true;
    rom = d + HDR;
    rom_len = len - HDR;

    song = (uint8_t)((track >= 1 && track <= songs) ? track : first_song);
    if (track > songs) ESP_LOGW(TAG, "track %d asked for, file has %d: playing %d", track, songs, song);

    ram  = malloc(0x800);
    sram = malloc(0x2000);
    if (!ram || !sram) { free(ram); free(sram); ram = sram = NULL; return false; }

    if (d[0x7b]) ESP_LOGW(TAG, "uses expansion sound (0x%02x), which is not emulated", d[0x7b]);
    ESP_LOGI(TAG, "\"%.32s\" - %.32s - track %d of %d", (const char *)d + 0x0e,
             (const char *)d + 0x4e, song, songs);
    nsf_rewind();
    return true;
}

static void nsf_render(int16_t *buf, int samples, int rate)
{
    if (!spp_q16) spp_q16 = (uint32_t)((((uint64_t)rate * speed_us) << 16) / 1000000u);
    while (samples > 0) {
        if (until_q16 <= 0) {
            call(play_addr, 0, 0, PLAY_BUDGET);
            until_q16 += (int32_t)spp_q16;
        }
        int n = until_q16 >> 16;
        if (n < 1) n = 1;
        if (n > samples) n = samples;
        apu_render(&apu, buf, n, rate);
        buf += n; samples -= n; until_q16 -= n << 16;
    }
}

static void nsf_silence(void) { if (ram) apu_write(&apu, 0x4015, 0); }

static void nsf_unload(void)
{
    free(ram);  ram = NULL;
    free(sram); sram = NULL;
}

const player_t nsf_player = { "NSF", nsf_load, nsf_rewind, nsf_render, nsf_silence, nsf_unload };
