/*
 * harness.c - run Moon Patrol on the host; frames to PPM, audio to WAV.
 * usage: harness <outdir> [seconds] [--every S] [--wav f] [--script "T:key=val,..."]
 * script keys: coin start left right jump fire
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mpatrol.h"
#include "mpatrolw_roms.h"
#include "scores.h"
#include "host_scores.h"

typedef struct { double t; char key[8]; int val; } event_t;

static void write_ppm(const char *path, const uint8_t *fb, const uint16_t *pal)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", MP_FB_W, MP_FB_H);
    for (int y = 0; y < MP_FB_H; y++) {
        for (int x = 0; x < MP_FB_W; x++) {
            uint16_t c = pal[fb[y * MP_FB_W + x]];
            uint8_t rgb[3] = { (uint8_t)(((c >> 11) & 0x1f) << 3), (uint8_t)(((c >> 5) & 0x3f) << 2), (uint8_t)((c & 0x1f) << 3) };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s outdir [seconds] [--every S] [--wav f] [--script s]\n", argv[0]); return 1; }
    const char *outdir = argv[1];
    double seconds = argc > 2 && argv[2][0] != '-' ? atof(argv[2]) : 20;
    double every = 1.0; const char *wav_path = NULL;
    event_t evs[64]; int nev = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--every") && i + 1 < argc) every = atof(argv[++i]);
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav_path = argv[++i];
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) {
            char *sc = strdup(argv[++i]);
            for (char *tok = strtok(sc, ","); tok && nev < 64; tok = strtok(NULL, ",")) {
                double t; char key[8]; int val;
                if (sscanf(tok, "%lf:%7[a-z0-9]=%i", &t, key, &val) == 3) { evs[nev].t = t; strcpy(evs[nev].key, key); evs[nev].val = val; nev++; }
            }
        }
    }
    mp_roms_t roms = { mp_rom, mp_snd, mp_tx, mp_sp, mp_bg, mp_proms };
    mp_init(&roms);
    hiscore_begin(&game_scores);
    mp_input_t *in = mp_input();

    FILE *wav = NULL; const int rate = MP_AUDIO_RATE; uint32_t wav_samples = 0;
    if (wav_path) { wav = fopen(wav_path, "wb"); uint8_t hdr[44] = {0}; fwrite(hdr, 1, 44, wav); }
    static uint8_t fb[MP_FB_W * MP_FB_H];
    static int16_t abuf[4096];
    uint16_t pal[MP_PALETTE_SIZE];
    const double fps = 1000000.0 / MP_FRAME_US;
    int frames = (int)(seconds * fps), saved = 0;
    double next_save = 0, audio_acc = 0;
    for (int f = 0; f < frames; f++) {
        double now = f / fps;
        for (int e = 0; e < nev; e++) {
            if (evs[e].t <= now && evs[e].t > now - 1.0 / fps) {
                const char *k = evs[e].key; int v = evs[e].val;
                if (!strcmp(k, "coin")) in->coin1 = v; else if (!strcmp(k, "start")) in->start1 = v;
                else if (!strcmp(k, "left")) in->left = v; else if (!strcmp(k, "right")) in->right = v;
                else if (!strcmp(k, "jump")) in->jump = v; else if (!strcmp(k, "fire")) in->fire = v;
            }
        }
        mp_run_frame();
        hiscore_frame();
        host_scores_frame(&game_scores, now);
        if (wav) {
            audio_acc += rate / fps; int n = (int)audio_acc; audio_acc -= n;
            mp_render_audio(abuf, n, rate); fwrite(abuf, 2, n, wav); wav_samples += n;
        }
        if (now >= next_save) {
            char path[512]; snprintf(path, sizeof(path), "%s/frame_%03d.ppm", outdir, saved);
            mp_render(fb); mp_palette(pal); write_ppm(path, fb, pal); saved++; next_save += every;
        }
        if ((f % (int)fps) == (int)fps - 1) printf("t=%ds pc=%04X sndpc=%04X\n", (int)(now + 1), mp_pc(), mp_snd_pc());
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
    printf("done: %.1fs, %u frames, %d images saved\n", seconds, mp_frame_count(), saved);
    return 0;
}
