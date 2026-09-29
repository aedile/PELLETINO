/*
 * harness.c - run Galaga on the host; frames to PPM (portrait), audio to WAV.
 * usage: harness <outdir> [seconds] [--every S] [--wav f] [--script "T:key=val,..."] [--dswa X --dswb X]
 * script keys: coin start fire left right
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "galaga.h"
#include "galaga_roms.h"
#include "scores.h"
#include "host_scores.h"

static uint8_t fb[GA_FB_W * GA_FB_H];
static char outdir[512];
static int frames_saved;

static void save_ppm_portrait(int idx)
{
    /* ROT90: portrait width 224 (native y), height 288 (native x) */
    char path[600];
    snprintf(path, sizeof path, "%s/frame_%03d.ppm", outdir, idx);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    const uint16_t *pal = ga_palette();
    fprintf(f, "P6\n%d %d\n255\n", GA_FB_H, GA_FB_W);
    for (int py = 0; py < GA_FB_W; py++) {
        for (int px = 0; px < GA_FB_H; px++) {
            int nx = py, ny = GA_FB_H - 1 - px;
            uint16_t c = pal[fb[ny * GA_FB_W + nx]];
            uint8_t rgb[3] = { (uint8_t)((c >> 8) & 0xF8), (uint8_t)((c >> 3) & 0xFC), (uint8_t)((c << 3) & 0xF8) };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s outdir [seconds] [--every S] [--wav f] [--script s] [--dswa X --dswb X]\n", argv[0]); return 1; }
    snprintf(outdir, sizeof outdir, "%s", argv[1]);
    double seconds = (argc > 2 && argv[2][0] != '-') ? atof(argv[2]) : 10.0;
    double every = 1.0; const char *wav_path = NULL;
    int dswa = 0xf7, dswb = 0x97;
    struct ev { double t; char key[8]; int val; } evs[128]; int nev = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--every") && i + 1 < argc) every = atof(argv[++i]);
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav_path = argv[++i];
        else if (!strcmp(argv[i], "--dswa") && i + 1 < argc) dswa = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--dswb") && i + 1 < argc) dswb = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) {
            char *sc = strdup(argv[++i]);
            for (char *tok = strtok(sc, ","); tok && nev < 128; tok = strtok(NULL, ",")) {
                double t; char key[8]; int val;
                if (sscanf(tok, "%lf:%7[a-z0-9]=%i", &t, key, &val) == 3) { evs[nev].t = t; strcpy(evs[nev].key, key); evs[nev].val = val; nev++; }
            }
        }
    }
    ga_roms_t roms = { ga_rom_cpu1, ga_rom_cpu2, ga_rom_cpu3, ga_tiles, ga_sprites,
                       ga_prom_palette, ga_prom_charlut, ga_prom_spritelut, ga_prom_wave };
    ga_init(&roms);
    hiscore_begin(&game_scores);
    ga_set_dips((uint8_t)dswa, (uint8_t)dswb);

    FILE *wav = NULL; const int rate = 20050; uint32_t wav_samples = 0;
    if (wav_path) { wav = fopen(wav_path, "wb"); uint8_t h[44] = {0}; fwrite(h, 1, 44, wav); }
    static int16_t abuf[4096];
    double audio_acc = 0, next_save = 0, fps = (double)GA_CPU_CLOCK / GA_CYCLES_PER_FRAME;
    int total = (int)(seconds * fps);
    for (int fr = 0; fr < total; fr++) {
        double now = fr / fps;
        for (int e = 0; e < nev; e++) {
            if (evs[e].t >= 0 && now >= evs[e].t) {
                ga_input_t *in = ga_input();
                if (!strcmp(evs[e].key, "coin")) in->coin1 = evs[e].val;
                else if (!strcmp(evs[e].key, "start")) in->start1 = evs[e].val;
                else if (!strcmp(evs[e].key, "fire")) in->fire = evs[e].val;
                else if (!strcmp(evs[e].key, "left")) in->left = evs[e].val;
                else if (!strcmp(evs[e].key, "right")) in->right = evs[e].val;
                evs[e].t = -1;
            }
        }
        ga_run_frame();
        hiscore_frame();
        host_scores_frame(&game_scores, now);
        audio_acc += rate / fps;
        int n = (int)audio_acc; audio_acc -= n;
        ga_render_audio(abuf, n, rate);
        if (wav) { fwrite(abuf, 2, n, wav); wav_samples += n; }
        if (now >= next_save) {
            ga_render(fb);
            save_ppm_portrait(frames_saved);
            printf("t=%.2fs saved frame %d  pc1=%04X pc2=%04X pc3=%04X credits=%d starctl=%02X\n", now, frames_saved,
                   ga_pc(0), ga_pc(1), ga_pc(2), ga_credits(), ga_starfield_ctl());
            frames_saved++;
            next_save += every;
        }
        if (fr % (int)fps == 0 && fr) {
            uint32_t h0 = ga_halt_cycles(0), h1 = ga_halt_cycles(1), h2 = ga_halt_cycles(2);
            printf("t=%.0fs halt%%: cpu1 %u cpu2 %u cpu3 %u  pc1=%04X pc2=%04X pc3=%04X\n", now,
                   h0 * 100 / GA_CPU_CLOCK, h1 * 100 / GA_CPU_CLOCK, h2 * 100 / GA_CPU_CLOCK, ga_pc(0), ga_pc(1), ga_pc(2));
        }
    }
    if (wav) {
        uint32_t data_bytes = wav_samples * 2; uint8_t h[44]; uint32_t v; uint16_t w;
        memcpy(h, "RIFF", 4); v = 36 + data_bytes; memcpy(h + 4, &v, 4); memcpy(h + 8, "WAVEfmt ", 8);
        v = 16; memcpy(h + 16, &v, 4); w = 1; memcpy(h + 20, &w, 2); memcpy(h + 22, &w, 2);
        v = rate; memcpy(h + 24, &v, 4); v = rate * 2; memcpy(h + 28, &v, 4);
        w = 2; memcpy(h + 32, &w, 2); w = 16; memcpy(h + 34, &w, 2); memcpy(h + 36, "data", 4); memcpy(h + 40, &data_bytes, 4);
        fseek(wav, 0, SEEK_SET); fwrite(h, 1, 44, wav); fclose(wav);
    }
    hiscore_flush();
    host_scores_dump(&game_scores);
    printf("done: %.1fs, %u frames, %d images\n", seconds, ga_frame_count(), frames_saved);
    return 0;
}
