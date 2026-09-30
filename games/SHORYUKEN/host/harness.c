/* SPDX-License-Identifier: 0BSD  Copyright (C) 2026 Jesse Castro */
/*
 * harness.c - run SHORYUKEN's core on the host: frames to PPM, sound to WAV, and where the
 * time goes.
 *
 * usage: harness <roms.bin> <outdir> [seconds] [--every S] [--view native|scale|crop]
 *                [--layers N] [--strip R] [--wav file] [--script "T:key=val,..."] [--quiet]
 * script keys: coin start up down left right b1 b2 b3 b4 b5 b6
 *
 * The picture is drawn exactly as the device draws it: in strips of 16 rows, through the
 * same lookup tables, at the size of the view. "native" is the whole 384x224, for checking
 * the video against a reference; the other two are what the panel shows, bars included.
 *
 * What this cannot tell you: what flash costs, and whether it fits in RAM. Here the ROM is
 * a file in memory and every read of it is free.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "cps1.h"
#include "cps1_view.h"
#include "knobs.h"

#define PANEL_W 240
#define PANEL_H 280

typedef struct { double t; char key[8]; int val; } event_t;

#ifdef CPS1_PROFILE
static FILE *ym_log;
void cps1_ym_trace(int port, int v, int64_t clock) { if (ym_log) fprintf(ym_log, "%lld %d %d\n", (long long)clock, port, v); }
#endif

static int64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void write_ppm(const char *path, const uint16_t *px, int w, int h, int bar)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", w, h + 2 * bar);
    uint8_t black[3] = { 0, 0, 0 };
    for (int i = 0; i < bar * w; i++) fwrite(black, 1, 3, f);
    for (int i = 0; i < w * h; i++) {
        uint16_t c = (uint16_t)((px[i] >> 8) | (px[i] << 8));      /* it was swapped for the panel */
        uint8_t rgb[3] = { (uint8_t)(((c >> 11) & 0x1f) * 255 / 31), (uint8_t)(((c >> 5) & 0x3f) * 255 / 63), (uint8_t)((c & 0x1f) * 255 / 31) };
        fwrite(rgb, 1, 3, f);
    }
    for (int i = 0; i < bar * w; i++) fwrite(black, 1, 3, f);
    fclose(f);
}

static void set_key(cps1_input_t *in, const char *k, int v)
{
    if (!strcmp(k, "coin")) in->coin1 = v; else if (!strcmp(k, "start")) in->start1 = v;
    else if (!strcmp(k, "up")) in->p1_up = v; else if (!strcmp(k, "down")) in->p1_down = v;
    else if (!strcmp(k, "left")) in->p1_left = v; else if (!strcmp(k, "right")) in->p1_right = v;
    else if (!strcmp(k, "b1")) in->p1_b1 = v; else if (!strcmp(k, "b2")) in->p1_b2 = v;
    else if (!strcmp(k, "b3")) in->p1_b3 = v; else if (!strcmp(k, "b4")) in->p1_b4 = v;
    else if (!strcmp(k, "b5")) in->p1_b5 = v; else if (!strcmp(k, "b6")) in->p1_b6 = v;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s roms.bin outdir [seconds] [--every S] [--view native|scale|crop] [--layers N] [--strip R] [--wav f] [--script s] [--quiet]\n", argv[0]);
        return 1;
    }
    const char *outdir = argv[2];
    double seconds = argc > 3 && argv[3][0] != '-' ? atof(argv[3]) : 30;
    double every = 1.0;
    const char *view = VIDEO_MODE == VIDEO_MODE_CROP ? "crop" : "scale", *wav_path = NULL;
    int strip_rows = 16, quiet = 0;
    unsigned layer_mask = 15;
    event_t evs[64]; int nev = 0;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--every") && i + 1 < argc) every = atof(argv[++i]);
        else if (!strcmp(argv[i], "--view") && i + 1 < argc) view = argv[++i];
        else if (!strcmp(argv[i], "--layers") && i + 1 < argc) layer_mask = (unsigned)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--strip") && i + 1 < argc) strip_rows = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav_path = argv[++i];
        else if (!strcmp(argv[i], "--quiet")) quiet = 1;
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) {
            char *sc = strdup(argv[++i]);
            for (char *tok = strtok(sc, ","); tok && nev < 64; tok = strtok(NULL, ",")) {
                double t; char key[8]; int val;
                if (sscanf(tok, "%lf:%7[a-z0-9]=%i", &t, key, &val) == 3) { evs[nev].t = t; strcpy(evs[nev].key, key); evs[nev].val = val; nev++; }
            }
        }
    }

#ifdef CPS1_PROFILE
    if (getenv("YM_LOG")) ym_log = fopen(getenv("YM_LOG"), "w");
#endif
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *blob = malloc((size_t)size);
    if (fread(blob, 1, (size_t)size, f) != (size_t)size) { fprintf(stderr, "short read\n"); return 1; }
    fclose(f);
    if (size < CPS1_BLOB_HEADER_OFF + (long)sizeof(cps1_blob_t)) { fprintf(stderr, "%s: too short to be a ROM image\n", argv[1]); return 1; }
    const cps1_blob_t *head = (const cps1_blob_t *)(blob + CPS1_BLOB_HEADER_OFF);
    const char *bad = cps1_blob_check(head);
    if (bad) { fprintf(stderr, "%s: %s\n", argv[1], bad); return 1; }
    cps1_roms_t roms;
    cps1_blob_roms(head, blob, &roms);
    roms.oki = blob + head->oki_off;

    static uint16_t ram[0x10000 / 2], gfxram[0x30000 / 2];
    cps1_init(&roms, ram, gfxram);
    cps1_set_clock(now_us);
    printf("set %.16s, idle loop at %06X\n", roms.cfg->set_name, cps1_idle_loop());
    cps1_set_layers(layer_mask);

    static uint16_t cols[CPS1_SCREEN_W], rows[CPS1_SCREEN_H];
    int w, h;
    if (!strcmp(view, "native")) {
        w = CPS1_SCREEN_W; h = CPS1_SCREEN_H;
        for (int x = 0; x < w; x++) cols[x] = (uint16_t)x;
        for (int y = 0; y < h; y++) rows[y] = (uint16_t)y;
    } else if (!strcmp(view, "crop")) { w = VIEW_W; h = VIEW_CROP_H; view_crop(cols, rows); }
    else { w = VIEW_W; h = VIEW_SCALE_H; view_scale(cols, rows); }
    cps1_set_view(w, h, cols, rows);
    int bar = w == PANEL_W ? (PANEL_H - h) / 2 : 0;
    uint16_t *picture = malloc((size_t)w * h * 2);

    FILE *wav = NULL; uint32_t wav_samples = 0;
    static int16_t abuf[4096];
    if (wav_path) {
#if SOUND == SOUND_OFF
        fprintf(stderr, "--wav: this harness was built with SOUND=OFF\n"); return 1;
#else
        cps1_sound_init(&roms, SOUND_RATE);
        wav = fopen(wav_path, "wb"); uint8_t hdr[44] = { 0 }; fwrite(hdr, 1, 44, wav);
#endif
    }

    /* RAM_LOG=file: the work RAM after every frame, for finding where two builds part company */
    FILE *ram_log = getenv("RAM_LOG") ? fopen(getenv("RAM_LOG"), "wb") : NULL;

    /* HASH_LOG=file: a hash of every picture and of graphics RAM, for comparing two builds */
    FILE *hash_log = getenv("HASH_LOG") ? fopen(getenv("HASH_LOG"), "w") : NULL;

    const double fps = 1e6 / CPS1_FRAME_US;
    int frames = (int)(seconds * fps), saved = 0;
    double next_save = 0, audio_acc = 0;
    int last_audio = 0;
    int64_t t_cpu = 0, t_video = 0, t_audio = 0, t_z80 = 0, drawn = 0;
    cps1_input_t *in = cps1_input();
    for (int n = 0; n < frames; n++) {
        double now = n / fps;
        for (int e = 0; e < nev; e++)
            if (evs[e].t <= now && evs[e].t > now - 1.0 / fps) set_key(in, evs[e].key, evs[e].val);

        int64_t t0 = now_us();
        cps1_run_frame();
        int64_t t1 = now_us();
        int64_t z = (int64_t)cps1_z80_us();
        t_z80 += z; t_cpu += t1 - t0 - z;

        if (ram_log) fwrite(ram, 1, sizeof(ram), ram_log);

        if (wav) {
            audio_acc += SOUND_RATE / fps; int ns = (int)audio_acc; audio_acc -= ns;
            int64_t a0 = now_us();
            cps1_render_audio(abuf, ns);
            last_audio = ns;
            t_audio += now_us() - a0;
            fwrite(abuf, 2, (size_t)ns, wav); wav_samples += (uint32_t)ns;
        }

        /* every frame is drawn, so the video time below is the cost at FRAME_SKIP=0 */
        t0 = now_us();
        cps1_frame_begin();
        for (int y = 0; y < h; y += strip_rows)
            cps1_render(picture + y * w, y, y + strip_rows <= h ? strip_rows : h - y);
        t_video += now_us() - t0; drawn++;

        if (hash_log) {
            /* FNV-1a over the picture and over graphics RAM: one line a frame */
            uint32_t hh = 2166136261u, g = 2166136261u;
            for (int i = 0; i < w * h; i++) hh = (hh ^ picture[i]) * 16777619u;
            for (int i = 0; i < 0x30000 / 2; i++) g = (g ^ gfxram[i]) * 16777619u;
            uint32_t au = 2166136261u;
            for (int i = 0; i < last_audio; i++) au = (au ^ (uint16_t)abuf[i]) * 16777619u;
            fprintf(hash_log, "%d %08x %08x %08x\n", n, hh, g, au);
        }
        if (now >= next_save) {
            char path[512]; snprintf(path, sizeof(path), "%s/frame_%03d.ppm", outdir, saved);
            write_ppm(path, picture, w, h, bar); saved++; next_save += every;
        }
        if (!quiet && (n % 60) == 59) printf("t=%ds frame %u pc=%06X\n", (int)(now + 1), cps1_frame_count(), cps1_pc());
    }
    if (wav) {
        uint32_t data = wav_samples * 2; uint8_t hd[44];
        memcpy(hd, "RIFF", 4); *(uint32_t *)(hd + 4) = 36 + data; memcpy(hd + 8, "WAVEfmt ", 8);
        *(uint32_t *)(hd + 16) = 16; *(uint16_t *)(hd + 20) = 1; *(uint16_t *)(hd + 22) = 1; *(uint32_t *)(hd + 24) = SOUND_RATE;
        *(uint32_t *)(hd + 28) = SOUND_RATE * 2; *(uint16_t *)(hd + 32) = 2; *(uint16_t *)(hd + 34) = 16; memcpy(hd + 36, "data", 4); *(uint32_t *)(hd + 40) = data;
        fseek(wav, 0, SEEK_SET); fwrite(hd, 1, 44, wav); fclose(wav);
    }
    double emu_s = frames / fps;
    printf("done: %u frames = %.2f s of machine time (%.3f fps), %d images saved\n", cps1_frame_count(), emu_s, cps1_frame_count() / emu_s, saved);
    printf("host ms per second of machine time: m68k %.1f  z80 %.1f  audio %.1f  video %.1f (view %s %dx%d, every frame drawn)\n",
           t_cpu / 1000.0 / emu_s, t_z80 / 1000.0 / emu_s, t_audio / 1000.0 / emu_s, t_video / 1000.0 / emu_s, view, w, h);
#ifdef CPS1_PROFILE
    {
        extern uint64_t cps1_profile_instructions;
        extern uint32_t cps1_profile_pc[];
        printf("68000: %.0f instructions a second of machine time, %.2f cycles each if it never idled\n",
               cps1_profile_instructions / emu_s, (double)CPS1_M68K_CLOCK * emu_s / cps1_profile_instructions);
        printf("hottest instructions:");
        for (int k = 0; k < 24; k++) {
            uint32_t best = 0; int bi = -1;
            for (int i = 0; i < 0x80000; i++) if (cps1_profile_pc[i] > best) { best = cps1_profile_pc[i]; bi = i; }
            if (bi < 0) break;
            printf(" %06X:%.1f%%", bi * 2, 100.0 * best / cps1_profile_instructions);
            cps1_profile_pc[bi] = 0;
        }
        printf("\n");
#if SOUND != SOUND_OFF
        extern uint32_t cps1_z80_reads[]; extern uint64_t cps1_z80_cycles, cps1_z80_skipped;
        uint64_t total = 0; for (int i = 0; i < 0x10000; i++) total += cps1_z80_reads[i];
        printf("Z80: %.0f cycles a second executed and %.0f skipped (of %d), %.0f bus reads a second; most read:", cps1_z80_cycles / emu_s, cps1_z80_skipped / emu_s, CPS1_Z80_CLOCK, total / emu_s);
        for (int k = 0; k < 24; k++) {
            uint32_t best = 0; int bi = -1;
            for (int i = 0; i < 0x10000; i++) if (cps1_z80_reads[i] > best) { best = cps1_z80_reads[i]; bi = i; }
            if (bi < 0) break;
            printf(" %04X:%.1f%%", bi, 100.0 * best / total);
            cps1_z80_reads[bi] = 0;
        }
        printf("\n");
#endif
    }
#endif
    return 0;
}
