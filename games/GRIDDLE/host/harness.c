/*
 * harness.c - run Burger Time on the host; frames to PPM, audio to WAV.
 * usage: harness <outdir> [seconds] [--every S] [--wav f] [--script "T:key=val,..."] [--r10 X --r8 X]
 * script keys: coin start up down left right fire
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "btime.h"
#include "btime_roms.h"
#include "scores.h"
#include "host_scores.h"

typedef struct { double t; char key[8]; int val; } event_t;

static void write_ppm(const char *path, const uint8_t *fb, const uint16_t *pal)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", BT_FB_W, BT_FB_H);
    for (int y = 0; y < BT_FB_H; y++) {
        for (int x = 0; x < BT_FB_W; x++) {
            uint16_t c = pal[fb[y * BT_FB_W + x] % BT_PALETTE_SIZE];
            uint8_t rgb[3] = { (uint8_t)(((c >> 11) & 0x1f) << 3), (uint8_t)(((c >> 5) & 0x3f) << 2), (uint8_t)((c & 0x1f) << 3) };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s outdir [seconds] [--every S] [--wav f] [--script s] [--r10 X --r8 X]\n", argv[0]); return 1; }
    const char *outdir = argv[1];
    double seconds = argc > 2 && argv[2][0] != '-' ? atof(argv[2]) : 20;
    double every = 1.0; const char *wav_path = NULL;
    int r10 = 0x3f, r8 = 0xfb;   /* 1 coin 1 play, upright; 3 lives */
    event_t evs[64]; int nev = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--every") && i + 1 < argc) every = atof(argv[++i]);
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav_path = argv[++i];
        else if (!strcmp(argv[i], "--r10") && i + 1 < argc) r10 = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--r8") && i + 1 < argc) r8 = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) {
            char *sc = strdup(argv[++i]);
            for (char *tok = strtok(sc, ","); tok && nev < 64; tok = strtok(NULL, ",")) {
                double t; char key[8]; int val;
                if (sscanf(tok, "%lf:%7[a-z0-9]=%i", &t, key, &val) == 3) { evs[nev].t = t; strcpy(evs[nev].key, key); evs[nev].val = val; nev++; }
            }
        }
    }
    bt_roms_t roms = { bt_rom, bt_snd, bt_gfx1, bt_gfx2, bt_bgmap };
    bt_init(&roms);
    hiscore_begin(&game_scores);
    bt_set_dips((uint8_t)r10, (uint8_t)r8);
    bt_input_t *in = bt_input();

    FILE *wav = NULL; const int rate = 20050; uint32_t wav_samples = 0;
    if (wav_path) { wav = fopen(wav_path, "wb"); uint8_t hdr[44] = {0}; fwrite(hdr, 1, 44, wav); }
    static uint8_t fb[BT_FB_W * BT_FB_H];
    static int16_t abuf[4096];
    uint16_t pal[BT_PALETTE_SIZE];
    const double fps = 57.44;
    int frames = (int)(seconds * fps), saved = 0;
    double next_save = 0, audio_acc = 0;
    for (int f = 0; f < frames; f++) {
        double now = f / fps;
        for (int e = 0; e < nev; e++) {
            if (evs[e].t <= now && evs[e].t > now - 1.0 / fps) {
                const char *k = evs[e].key; int v = evs[e].val;
                if (!strcmp(k, "coin")) in->coin1 = v; else if (!strcmp(k, "start")) in->start1 = v;
                else if (!strcmp(k, "up")) in->up = v; else if (!strcmp(k, "down")) in->down = v;
                else if (!strcmp(k, "left")) in->left = v; else if (!strcmp(k, "right")) in->right = v; else if (!strcmp(k, "fire")) in->fire = v;
            }
        }
        bt_run_frame();
        hiscore_frame();
        host_scores_frame(&game_scores, now);
        if (wav) {
            audio_acc += rate / fps; int n = (int)audio_acc; audio_acc -= n;
            bt_render_audio(abuf, n, rate); fwrite(abuf, 2, n, wav); wav_samples += n;
        }
        if (now >= next_save) {
            char path[512]; snprintf(path, sizeof(path), "%s/frame_%03d.ppm", outdir, saved);
            bt_render(fb); bt_palette(pal); write_ppm(path, fb, pal); saved++; next_save += every;
        }
        if ((f % (int)fps) == (int)fps - 1) printf("t=%ds pc=%04X sndpc=%04X\n", (int)(now + 1), bt_pc(), bt_snd_pc());
    }
    if (wav) {
        uint32_t data = wav_samples * 2; uint8_t h[44];
        memcpy(h, "RIFF", 4); *(uint32_t *)(h + 4) = 36 + data; memcpy(h + 8, "WAVEfmt ", 8);
        *(uint32_t *)(h + 16) = 16; *(uint16_t *)(h + 20) = 1; *(uint16_t *)(h + 22) = 1; *(uint32_t *)(h + 24) = rate;
        *(uint32_t *)(h + 28) = rate * 2; *(uint16_t *)(h + 32) = 2; *(uint16_t *)(h + 34) = 16; memcpy(h + 36, "data", 4); *(uint32_t *)(h + 40) = data;
        fseek(wav, 0, SEEK_SET); fwrite(h, 1, 44, wav); fclose(wav);
    }
#ifdef BT_DEBUG
    { extern uint32_t bt_dbg_hist[0x10000]; printf("hot pcs:"); for (int k = 0; k < 16; k++) { uint32_t best = 0; int bi = -1; for (int i = 0; i < 0x10000; i++) if (bt_dbg_hist[i] > best) { best = bt_dbg_hist[i]; bi = i; } if (bi < 0) break; printf(" %04X:%u", bi, best); bt_dbg_hist[bi] = 0; } printf("\n"); }
#endif
    hiscore_flush();
    host_scores_dump(&game_scores);
    printf("done: %.1fs, %u frames, %d images saved\n", seconds, bt_frame_count(), saved);
    return 0;
}
