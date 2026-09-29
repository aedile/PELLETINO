/*
 * harness.c - run Centipede on the host; frames to PPM (upright), audio to WAV.
 * usage: harness <outdir> [seconds] [--every S] [--wav f] [--script "T:key=val,..."] [--dsw1 X --dsw2 X]
 * script keys: coin start fire tx ty (trackball counts per frame, signed) up down left right test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "centiped.h"
#include "centiped_roms.h"
#include "scores.h"
#include "host_scores.h"

typedef struct { double t; char key[8]; int val; } event_t;

static void write_ppm(const char *path, const uint8_t *fb, const uint16_t *pal)
{
    /* the cabinet turns the 256x240 picture on its side: native (x,y) -> upright (y, 255-x) */
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", CE_FB_H, CE_FB_W);
    for (int py = 0; py < CE_FB_W; py++) {
        for (int px = 0; px < CE_FB_H; px++) {
            uint16_t c = pal[fb[px * CE_FB_W + (CE_FB_W - 1 - py)]];
            uint8_t rgb[3] = { (uint8_t)(((c >> 11) & 0x1f) << 3), (uint8_t)(((c >> 5) & 0x3f) << 2), (uint8_t)((c & 0x1f) << 3) };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s outdir [seconds] [--every S] [--wav f] [--script s] [--dsw1 X --dsw2 X]\n", argv[0]); return 1; }
    const char *outdir = argv[1];
    double seconds = argc > 2 && argv[2][0] != '-' ? atof(argv[2]) : 20;
    double every = 1.0; const char *wav_path = NULL;
    int dsw1 = 0x54, dsw2 = 0x02;
    event_t evs[64]; int nev = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--every") && i + 1 < argc) every = atof(argv[++i]);
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav_path = argv[++i];
        else if (!strcmp(argv[i], "--dsw1") && i + 1 < argc) dsw1 = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--dsw2") && i + 1 < argc) dsw2 = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) {
            char *sc = strdup(argv[++i]);
            for (char *tok = strtok(sc, ","); tok && nev < 64; tok = strtok(NULL, ",")) {
                double t; char key[8]; int val;
                if (sscanf(tok, "%lf:%7[a-z0-9]=%i", &t, key, &val) == 3) { evs[nev].t = t; strcpy(evs[nev].key, key); evs[nev].val = val; nev++; }
            }
        }
    }
    ce_roms_t roms = { ce_rom, ce_tiles, ce_sprites };
    ce_init(&roms);
    hiscore_begin(&game_scores);
    ce_set_dips((uint8_t)dsw1, (uint8_t)dsw2);
    ce_input_t *in = ce_input();

    FILE *wav = NULL; const int rate = 20050; uint32_t wav_samples = 0;
    if (wav_path) { wav = fopen(wav_path, "wb"); uint8_t hdr[44] = {0}; fwrite(hdr, 1, 44, wav); }
    static uint8_t fb[CE_FB_W * CE_FB_H];
    static int16_t abuf[4096];
    uint16_t pal[CE_PALETTE_SIZE];
    int frames = (int)(seconds * 60), saved = 0;
    double next_save = 0, audio_acc = 0;
    for (int f = 0; f < frames; f++) {
        double now = f / 60.0;
        for (int e = 0; e < nev; e++) {
            if (evs[e].t <= now && evs[e].t > now - 1.0 / 60) {
                const char *k = evs[e].key; int v = evs[e].val;
                if (!strcmp(k, "coin")) in->coin1 = v; else if (!strcmp(k, "start")) in->start1 = v;
                else if (!strcmp(k, "fire")) in->fire = v; else if (!strcmp(k, "tx")) in->track_x = (int8_t)v;
                else if (!strcmp(k, "ty")) in->track_y = (int8_t)v; else if (!strcmp(k, "up")) in->joy_up = v;
                else if (!strcmp(k, "down")) in->joy_down = v; else if (!strcmp(k, "left")) in->joy_left = v;
                else if (!strcmp(k, "right")) in->joy_right = v; else if (!strcmp(k, "test")) in->test = v;
            }
        }
        ce_run_frame();
        hiscore_frame();
        host_scores_frame(&game_scores, now);
        if (wav) {
            audio_acc += (double)rate / 60; int n = (int)audio_acc; audio_acc -= n;
            ce_render_audio(abuf, n, rate); fwrite(abuf, 2, n, wav); wav_samples += n;
        }
        if (now >= next_save) {
            char path[512]; snprintf(path, sizeof(path), "%s/frame_%03d.ppm", outdir, saved);
            ce_render(fb); ce_palette(pal); write_ppm(path, fb, pal); saved++; next_save += every;
        }
        if ((f % 60) == 59) {
            printf("t=%ds pc=%04X irqs=%u idle=%lu%%\n", (f + 1) / 60, ce_pc(), ce_irq_count(),
                   (unsigned long)(ce_idle_cycles() / (CE_CPU_CLOCK / 100)));
            if (getenv("PCTOP")) {
                extern uint32_t ce_dbg_pc_hist[0x4000];
                uint64_t tot = 0; for (int i = 0; i < 0x4000; i++) tot += ce_dbg_pc_hist[i];
                for (int k = 0; k < 8; k++) {
                    uint32_t best = 0; int bi = 0;
                    for (int i = 0; i < 0x4000; i++) if (ce_dbg_pc_hist[i] > best) { best = ce_dbg_pc_hist[i]; bi = i; }
                    if (!best) break;
                    printf("  pc %04X: %5.1f%%\n", bi, 100.0 * best / (double)tot); ce_dbg_pc_hist[bi] = 0;
                }
                memset(ce_dbg_pc_hist, 0, sizeof(uint32_t) * 0x4000);
            }
        }
    }
    if (wav) {
        uint32_t data = wav_samples * 2; uint8_t h[44];
        memcpy(h, "RIFF", 4); *(uint32_t *)(h + 4) = 36 + data; memcpy(h + 8, "WAVEfmt ", 8);
        *(uint32_t *)(h + 16) = 16; *(uint16_t *)(h + 20) = 1; *(uint16_t *)(h + 22) = 1; *(uint32_t *)(h + 24) = rate;
        *(uint32_t *)(h + 28) = rate * 2; *(uint16_t *)(h + 32) = 2; *(uint16_t *)(h + 34) = 16; memcpy(h + 36, "data", 4); *(uint32_t *)(h + 40) = data;
        fseek(wav, 0, SEEK_SET); fwrite(h, 1, 44, wav); fclose(wav);
    }
    hiscore_flush();
    host_scores_dump(&game_scores);
    printf("done: %.1fs, %u frames, %u irqs, %d images saved\n", seconds, ce_frame_count(), ce_irq_count(), saved);
    return 0;
}
