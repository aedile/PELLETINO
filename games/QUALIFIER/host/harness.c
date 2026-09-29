/*
 * harness.c - run Pole Position on the host; frames (landscape 256x224) to PPM, audio to WAV.
 * usage: harness <outdir> [seconds] [--every S] [--wav f] [--script "T:key=val,..."] [--dswa X --dswb X]
 * script keys: coin gear accel brake steer (absolute wheel value) test
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "polepos.h"
#include "polepos_roms.h"
#include "scores.h"
#include "host_scores.h"

static uint8_t fb[PP_FB_W * PP_FB_H];
extern uint32_t pp_dbg_sub_hist[2][0x10000];
static void sub_top(int c)
{
    for (int k = 0; k < 6; k++) {
        uint32_t best = 0; int bi = -1;
        for (int i = 0; i < 0x10000; i++) if (pp_dbg_sub_hist[c][i] > best) { best = pp_dbg_sub_hist[c][i]; bi = i; }
        if (bi < 0 || !best) break;
        printf("    sub%d pc %04X: %u\n", c + 1, bi, best); pp_dbg_sub_hist[c][bi] = 0;
    }
    memset(pp_dbg_sub_hist[c], 0, sizeof(pp_dbg_sub_hist[c]));
}
extern uint32_t pp_dbg_irq, pp_dbg_nmi, pp_dbg_nvi, pp_dbg_n51r, pp_dbg_n53r, pp_dbg_adc_w, pp_dbg_adc_r, pp_dbg_pc_hist[0x10000];
static void pc_top(void)
{
    for (int k = 0; k < 6; k++) {
        uint32_t best = 0; int bi = -1;
        for (int i = 0; i < 0x10000; i++) if (pp_dbg_pc_hist[i] > best) { best = pp_dbg_pc_hist[i]; bi = i; }
        if (bi < 0 || !best) break;
        printf("    z80 pc %04X: %u\n", bi, best); pp_dbg_pc_hist[bi] = 0;
    }
    memset(pp_dbg_pc_hist, 0, sizeof(pp_dbg_pc_hist));
}
static char outdir[512];
static int frames_saved;

static void save_ppm(int idx)
{
    char path[600];
    snprintf(path, sizeof path, "%s/frame_%03d.ppm", outdir, idx);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    const uint16_t *pal = pp_palette();
    fprintf(f, "P6\n%d %d\n255\n", PP_FB_W, PP_FB_H);
    for (int i = 0; i < PP_FB_W * PP_FB_H; i++) {
        uint16_t c = pal[fb[i] & 0x7f];
        uint8_t rgb[3] = { (uint8_t)((c >> 8) & 0xF8), (uint8_t)((c >> 3) & 0xFC), (uint8_t)((c << 3) & 0xF8) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s outdir [seconds] [--every S] [--wav f] [--script s] [--dswa X --dswb X]\n", argv[0]); return 1; }
    snprintf(outdir, sizeof outdir, "%s", argv[1]);
    double seconds = (argc > 2 && argv[2][0] != '-') ? atof(argv[2]) : 10.0;
    double every = 1.0; const char *wav_path = NULL;
    int dswa = 0xff, dswb = 0x6b;
    struct ev { double t; char key[8]; int val; } evs[256]; int nev = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--every") && i + 1 < argc) every = atof(argv[++i]);
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav_path = argv[++i];
        else if (!strcmp(argv[i], "--dswa") && i + 1 < argc) dswa = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--dswb") && i + 1 < argc) dswb = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) {
            char *sc = strdup(argv[++i]);
            for (char *tok = strtok(sc, ","); tok && nev < 256; tok = strtok(NULL, ",")) {
                double t; char key[8]; int val;
                if (sscanf(tok, "%lf:%7[a-z0-9]=%i", &t, key, &val) == 3) { evs[nev].t = t; strcpy(evs[nev].key, key); evs[nev].val = val; nev++; }
            }
        }
    }
    pp_roms_t roms = { pp_rom_z80, pp_rom_sub1, pp_rom_sub2, pp_chars, pp_tiles, pp_sprites, pp_bigsprites,
                       pp_road, pp_scalelut, pp_proms, pp_wave, pp_engine, pp_voice };
    pp_init(&roms);
    hiscore_begin(&game_scores);
    { extern int pp_render_mask; if (getenv("RENDERMASK")) pp_render_mask = (int)strtol(getenv("RENDERMASK"), NULL, 0); }
    pp_set_dips((uint8_t)dswa, (uint8_t)dswb);

    FILE *wav = NULL; const int rate = 20050; uint32_t wav_samples = 0;
    if (wav_path) { wav = fopen(wav_path, "wb"); uint8_t h[44] = {0}; fwrite(h, 1, 44, wav); }
    static int16_t abuf[4096];
    double audio_acc = 0, next_save = 0, fps = (double)PP_CPU_CLOCK / PP_CYCLES_PER_FRAME;
    int total = (int)(seconds * fps);
    for (int fr = 0; fr < total; fr++) {
        double now = fr / fps;
        for (int e = 0; e < nev; e++) {
            if (evs[e].t >= 0 && now >= evs[e].t) {
                pp_input_t *in = pp_input();
                if (!strcmp(evs[e].key, "coin")) in->coin1 = evs[e].val;
                else if (!strcmp(evs[e].key, "gear")) in->gear = evs[e].val;
                else if (!strcmp(evs[e].key, "accel")) in->accel = evs[e].val;
                else if (!strcmp(evs[e].key, "brake")) in->brake = evs[e].val;
                else if (!strcmp(evs[e].key, "steer")) in->steer = evs[e].val;
                else if (!strcmp(evs[e].key, "test")) in->test = evs[e].val;
                evs[e].t = -1;
            }
        }
        pp_run_frame();
        hiscore_frame();
        host_scores_frame(&game_scores, now);
        audio_acc += rate / fps;
        int n = (int)audio_acc; audio_acc -= n;
        pp_render_audio(abuf, n, rate);
        if (wav) { fwrite(abuf, 2, n, wav); wav_samples += n; }
        if (now >= next_save) {
            pp_render(fb);
            save_ppm(frames_saved);
            printf("t=%.2fs saved frame %d  z80=%04X sub1=%04X sub2=%04X latch=%02X\n", now, frames_saved, pp_pc(0), pp_pc(1), pp_pc(2), pp_latch());
            frames_saved++;
            next_save += every;
        }
        if (fr % (int)fps == 0 && fr) {
            printf("t=%.0fs idle%%: z80 %u sub1 %u sub2 %u  pcs %04X %04X %04X latch %02X\n", now,
                   pp_idle_cycles(0) * 100 / PP_CPU_CLOCK, pp_idle_cycles(1) * 100 / PP_CPU_CLOCK, pp_idle_cycles(2) * 100 / PP_CPU_CLOCK,
                   pp_pc(0), pp_pc(1), pp_pc(2), pp_latch());
            printf("    irq %u nmi %u nvi %u n51r %u n53r %u adc w/r %u/%u\n", pp_dbg_irq, pp_dbg_nmi, pp_dbg_nvi, pp_dbg_n51r, pp_dbg_n53r, pp_dbg_adc_w, pp_dbg_adc_r);
            if (getenv("PCTOP")) pc_top();
            if (getenv("SUBTOP")) { sub_top(0); sub_top(1); }
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
    printf("done: %.1fs, %u frames, %d images\n", seconds, pp_frame_count(), frames_saved);
    return 0;
}
