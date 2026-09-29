/*
 * harness.c - run Tempest on the host; frames to PPM, audio to WAV.
 * usage: harness <outdir> [seconds] [--every S] [--wav f] [--script "T:key=val,..."]
 * script keys: coin start fire zap spin (spinner counts per frame, signed)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "tempest.h"
#include "tempest_roms.h"
#include "scores.h"
#include "host_scores.h"

#define W 280
#define H 280

typedef struct { double t; char key[8]; int val; } event_t;
static uint8_t fb[H][W][3];

static void plot(int x, int y, int r, int g, int b, float a)
{
    if (x < 0 || x >= W || y < 0 || y >= H || a <= 0) return;
    uint8_t *p = fb[y][x];
    int nr = p[0] + (int)(r * a), ng = p[1] + (int)(g * a), nb = p[2] + (int)(b * a);
    p[0] = nr > 255 ? 255 : nr; p[1] = ng > 255 ? 255 : ng; p[2] = nb > 255 ? 255 : nb;
}

/* the same anti-aliased stroke the medal draws with, so the host preview matches */
static void line(int x0, int y0, int x1, int y1, int r, int g, int b, float inten)
{
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    int n = (dx > dy ? dx : dy);
    if (n == 0) { plot(x0, y0, r, g, b, inten); return; }
    for (int i = 0; i <= n; i++) {
        float fx = x0 + (float)(x1 - x0) * i / n;
        float fy = y0 + (float)(y1 - y0) * i / n;
        int ix = (int)fx, iy = (int)fy;
        float ax = fx - ix, ay = fy - iy;
        plot(ix,     iy,     r, g, b, inten * (1 - ax) * (1 - ay));
        plot(ix + 1, iy,     r, g, b, inten * ax * (1 - ay));
        plot(ix,     iy + 1, r, g, b, inten * (1 - ax) * ay);
        plot(ix + 1, iy + 1, r, g, b, inten * ax * ay);
    }
}

static void draw_frame(void)
{
    memset(fb, 0, sizeof(fb));
    int n; const avg_point_t *pts = tp_points(&n);
    int px = 0, py = 0;
    for (int i = 0; i < n; i++) {
        /* The monitor is mounted rotated a quarter turn (MAME calls it ROT270), which is
         * what makes Tempest a portrait picture and why it suits the medal's panel. */
        int bx = (int)(pts[i].x >> 16), by = (int)(pts[i].y >> 16);
        int rx = by, ry = bx;
        int x = rx * (W - 1) / AVG_YMAX;
        int y = ry * (H - 1) / AVG_XMAX;
        if (i && pts[i].intensity)
            line(px, py, x, y, pts[i].r, pts[i].g, pts[i].b, pts[i].intensity / 255.0f);
        px = x; py = y;
    }
}

static void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    fwrite(fb, 1, sizeof(fb), f);
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
                if (sscanf(tok, "%lf:%7[a-z0-9]=%i", &t, key, &val) == 3) {
                    evs[nev].t = t; strcpy(evs[nev].key, key); evs[nev].val = val; nev++;
                }
            }
        }
    }
    tp_roms_t r = { tp_rom, tp_vectorrom, tp_avgprom };
    tp_init(&r);
    hiscore_begin(&game_scores);
    tp_input_t *in = tp_input();

    FILE *wav = NULL; const int rate = 22050; uint32_t wav_samples = 0;
    if (wav_path) { wav = fopen(wav_path, "wb"); uint8_t hdr[44] = {0}; fwrite(hdr, 1, 44, wav); }
    static int16_t abuf[4096];
    const double fps = 60.0;
    int frames = (int)(seconds * fps), saved = 0;
    double next_save = 0, audio_acc = 0;
    for (int f = 0; f < frames; f++) {
        double now = f / fps;
        for (int e = 0; e < nev; e++) {
            if (evs[e].t <= now && evs[e].t > now - 1.0 / fps) {
                const char *k = evs[e].key; int v = evs[e].val;
                if (!strcmp(k, "coin")) in->coin1 = v; else if (!strcmp(k, "start")) in->start1 = v;
                else if (!strcmp(k, "fire")) in->fire = v; else if (!strcmp(k, "zap")) in->zap = v;
                else if (!strcmp(k, "spin")) in->spin = (int8_t)v;
            }
        }
        tp_run_frame();
        hiscore_frame();
        host_scores_frame(&game_scores, now);
        if (wav) {
            audio_acc += rate / fps; int n = (int)audio_acc; audio_acc -= n;
            tp_render_audio(abuf, n, rate); fwrite(abuf, 2, n, wav); wav_samples += n;
        }
        if (now >= next_save) {
            char path[512]; snprintf(path, sizeof(path), "%s/frame_%03d.ppm", outdir, saved);
            draw_frame(); write_ppm(path); saved++; next_save += every;
        }
        if ((f % 60) == 59) printf("t=%2ds pc=%04X vectors=%d\n", (int)(now + 1), tp_pc(), tp_vector_count());
    }
    if (wav) {
        uint32_t data = wav_samples * 2; uint8_t h[44];
        memcpy(h, "RIFF", 4); *(uint32_t *)(h + 4) = 36 + data; memcpy(h + 8, "WAVEfmt ", 8);
        *(uint32_t *)(h + 16) = 16; *(uint16_t *)(h + 20) = 1; *(uint16_t *)(h + 22) = 1;
        *(uint32_t *)(h + 24) = rate; *(uint32_t *)(h + 28) = rate * 2; *(uint16_t *)(h + 32) = 2;
        *(uint16_t *)(h + 34) = 16; memcpy(h + 36, "data", 4); *(uint32_t *)(h + 40) = data;
        fseek(wav, 0, SEEK_SET); fwrite(h, 1, 44, wav); fclose(wav);
    }
    hiscore_flush();
    host_scores_dump(&game_scores);
    printf("done: %.1fs, %u frames, %d images\n", seconds, tp_frame_count(), saved);
    return 0;
}
