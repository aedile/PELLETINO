/*
 * harness.c - run the Star Wars board on the host and dump vector frames as PPM images.
 *
 * usage: harness <romdir> <outdir> [seconds] [--coin T] [--fire T] [--yaw V] [--pitch V]
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "starwars.h"
#include "sound.h"

#define SCALE 2
#define IMG_W ((AVG_XMAX + 1) * SCALE)
#define IMG_H ((AVG_YMAX + 1) * SCALE)

static uint8_t img[IMG_H][IMG_W][3];
extern uint32_t sw_dbg_in0_reads, sw_dbg_in1_reads, sw_dbg_snd_writes, sw_dbg_snd_flag_reads, sw_dbg_snd_reads, sw_dbg_math_runs, sw_dbg_div_ops, sw_dbg_adc_reads;
extern uint8_t sw_dbg_last_snd_cmd;
extern uint32_t snd_dbg_pc_hist[0x10000];
extern uint32_t sw_dbg_avg_frames, sw_dbg_avg_mismatch;
static void snd_pc_top(void)
{
    for (int k = 0; k < 10; k++) {
        uint32_t best = 0; int bi = -1;
        for (int i = 0; i < 0x10000; i++) if (snd_dbg_pc_hist[i] > best && !(i >= 0x7d4c && i <= 0x7d5f)) { best = snd_dbg_pc_hist[i]; bi = i; }
        if (bi < 0 || best == 0) break;
        printf("  snd pc %04X: %u\n", bi, best);
        snd_dbg_pc_hist[bi] = 0;
    }
    memset(snd_dbg_pc_hist, 0, sizeof(snd_dbg_pc_hist));
}
extern int sw_dbg_trace_arm; extern uint16_t sw_dbg_trace[2000]; extern int sw_dbg_trace_n;
extern uint32_t sw_dbg_irq_taken, sw_dbg_pc_hist[0x10000], sw_dbg_w483d, sw_dbg_r483d; extern uint16_t sw_dbg_w483d_pc; extern uint8_t sw_dbg_w483d_val;
static void pc_top(void)
{
    /* print the 8 most executed PCs since last call, then clear */
    for (int k = 0; k < 8; k++) {
        uint32_t best = 0; int bi = -1;
        for (int i = 0; i < 0x10000; i++) if (sw_dbg_pc_hist[i] > best) { best = sw_dbg_pc_hist[i]; bi = i; }
        if (bi < 0 || best == 0) break;
        printf("  pc %04X: %u\n", bi, best);
        sw_dbg_pc_hist[bi] = 0;
    }
    memset(sw_dbg_pc_hist, 0, sizeof(sw_dbg_pc_hist));
}
static int frames_seen, frames_saved, last_points, last_visible;
static char outdir[512];
static double save_every = 1.0;
static double next_save;
static double now_s;

static uint8_t *load(const char *dir, const char *name, size_t size)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
    uint8_t *buf = malloc(size);
    size_t n = fread(buf, 1, size, f);
    fclose(f);
    if (n != size) { fprintf(stderr, "%s: got %zu bytes, expected %zu\n", path, n, size); exit(1); }
    return buf;
}

static void plot(int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    if (x < 0 || y < 0 || x >= IMG_W || y >= IMG_H) return;
    uint8_t *p = img[y][x];
    if (r > p[0]) p[0] = r;
    if (g > p[1]) p[1] = g;
    if (b > p[2]) p[2] = b;
}

static void line(int x0, int y0, int x1, int y1, uint8_t r, uint8_t g, uint8_t b)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        plot(x0, y0, r, g, b);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void save_ppm(int idx)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/frame_%03d.ppm", outdir, idx);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", IMG_W, IMG_H);
    fwrite(img, 1, sizeof img, f);
    fclose(f);
}

static void on_frame(const avg_t *avg, void *user)
{
    (void)user;
    frames_seen++;
    last_points = avg->npoints;
    last_visible = 0;
    memset(img, 0, sizeof img);
    int have_prev = 0, px = 0, py = 0;
    for (int i = 0; i < avg->npoints; i++) {
        const avg_point_t *p = &avg->points[i];
        int x = (p->x >> 16) * SCALE;
        int y = (p->y >> 16) * SCALE;
        if (have_prev && p->intensity) {
            uint8_t r = (p->color & 4) ? p->intensity : 0;   /* MAME color111: red is bit 2 */
            uint8_t g = (p->color & 2) ? p->intensity : 0;
            uint8_t b = (p->color & 1) ? p->intensity : 0;
            line(px, py, x, y, r, g, b);
            last_visible++;
        }
        px = x; py = y; have_prev = 1;
    }
    if (now_s >= next_save) {
        save_ppm(frames_saved++);
        printf("t=%.2fs saved frame %d: %d points, %d visible segments%s\n", now_s, frames_saved - 1,
               avg->npoints, last_visible, avg->overflow ? " (OVERFLOW)" : "");
        next_save += save_every;
    }
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s romdir outdir [seconds] [--coin T] [--fire T] [--yaw V] [--pitch V]\n", argv[0]); return 1; }
    const char *romdir = argv[1];
    snprintf(outdir, sizeof outdir, "%s", argv[2]);
    double seconds = argc > 3 && argv[3][0] != '-' ? atof(argv[3]) : 10.0;
    double coin_t = -1, fire_t = -1;
    int yaw = 0x80, pitch = 0x80, dsw1 = 0x02, hold = 0, nosound = 0;
    const char *wav_path = NULL;
    /* scripted events: "T:key=value,T:key=value" keys: fire coin pitch yaw b2 b3 b4 */
    struct ev { double t; char key[8]; int val; } evs[64]; int nev = 0;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--coin") && i + 1 < argc) coin_t = atof(argv[++i]);
        else if (!strcmp(argv[i], "--fire") && i + 1 < argc) fire_t = atof(argv[++i]);
        else if (!strcmp(argv[i], "--yaw") && i + 1 < argc) yaw = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pitch") && i + 1 < argc) pitch = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--every") && i + 1 < argc) save_every = atof(argv[++i]);
        else if (!strcmp(argv[i], "--dsw1") && i + 1 < argc) dsw1 = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--hold")) hold = 1;
        else if (!strcmp(argv[i], "--nosound")) nosound = 1;
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) wav_path = argv[++i];
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) {
            char *sc = strdup(argv[++i]);
            for (char *tok = strtok(sc, ","); tok && nev < 64; tok = strtok(NULL, ",")) {
                char key[8]; double t; int val;
                if (sscanf(tok, "%lf:%7[a-z0-9]=%i", &t, key, &val) == 3) {
                    evs[nev].t = t; strcpy(evs[nev].key, key); evs[nev].val = val; nev++;
                }
            }
        }
    }

    char probe[512]; snprintf(probe, sizeof(probe), "%s/136031.101", romdir);
    int esb = access(probe, R_OK) == 0;
    uint8_t *rom_main, *rom_bank, *rom_vector, *rom_slapstic = NULL, *prom_math = malloc(0x1000), *prom_avg, *rom_sound;
    unsigned sound_size;
    if (!esb) {
        rom_main = malloc(0x8000);
        memcpy(rom_main + 0x0000, load(romdir, "136021.102", 0x2000), 0x2000);
        memcpy(rom_main + 0x2000, load(romdir, "136021.203", 0x2000), 0x2000);
        memcpy(rom_main + 0x4000, load(romdir, "136021.104", 0x2000), 0x2000);
        memcpy(rom_main + 0x6000, load(romdir, "136021.206", 0x2000), 0x2000);
        memcpy(prom_math + 0x000, load(romdir, "136021.110", 0x400), 0x400);
        memcpy(prom_math + 0x400, load(romdir, "136021.111", 0x400), 0x400);
        memcpy(prom_math + 0x800, load(romdir, "136021.112", 0x400), 0x400);
        memcpy(prom_math + 0xc00, load(romdir, "136021.113", 0x400), 0x400);
        rom_bank = load(romdir, "136021.214", 0x4000);
        rom_vector = load(romdir, "136021.105", 0x1000);
        prom_avg = load(romdir, "136021-105.1l", 0x100);
        rom_sound = malloc(0x4000); sound_size = 0x4000;
        memcpy(rom_sound + 0x0000, load(romdir, "136021.107", 0x2000), 0x2000);
        memcpy(rom_sound + 0x2000, load(romdir, "136021.208", 0x2000), 0x2000);
    } else {
        /* The Empire Strikes Back: 16 KB ROMs whose halves are the two pages of each bank */
        uint8_t *r102 = load(romdir, "136031.102", 0x4000), *r203 = load(romdir, "136031.203", 0x4000), *r104 = load(romdir, "136031.104", 0x4000);
        rom_main = malloc(0xc000);
        memcpy(rom_main + 0x0000, r102, 0x2000); memcpy(rom_main + 0x2000, r203, 0x2000); memcpy(rom_main + 0x4000, r104, 0x2000);
        memcpy(rom_main + 0x6000, r102 + 0x2000, 0x2000); memcpy(rom_main + 0x8000, r203 + 0x2000, 0x2000); memcpy(rom_main + 0xa000, r104 + 0x2000, 0x2000);
        rom_bank = load(romdir, "136031.101", 0x4000);
        rom_slapstic = malloc(0x8000);
        memcpy(rom_slapstic, load(romdir, "136031.105", 0x4000), 0x4000);
        memcpy(rom_slapstic + 0x4000, load(romdir, "136031.106", 0x4000), 0x4000);
        rom_vector = load(romdir, "136031.111", 0x1000);
        memcpy(prom_math + 0x000, load(romdir, "136031.110", 0x400), 0x400);
        memcpy(prom_math + 0x400, load(romdir, "136031.109", 0x400), 0x400);
        memcpy(prom_math + 0x800, load(romdir, "136031.108", 0x400), 0x400);
        memcpy(prom_math + 0xc00, load(romdir, "136031.107", 0x400), 0x400);
        prom_avg = load(romdir, "136021-105.1l", 0x100);
        uint8_t *s113 = load(romdir, "136031.113", 0x4000), *s112 = load(romdir, "136031.112", 0x4000);
        rom_sound = malloc(0x8000); sound_size = 0x8000;
        memcpy(rom_sound + 0x0000, s113, 0x2000); memcpy(rom_sound + 0x2000, s112, 0x2000);
        memcpy(rom_sound + 0x4000, s113 + 0x2000, 0x2000); memcpy(rom_sound + 0x6000, s112 + 0x2000, 0x2000);
        printf("ROM set: The Empire Strikes Back\n");
    }

    sw_roms_t roms = {
        .rom_main = rom_main,
        .rom_bank = rom_bank,
        .rom_vector = rom_vector,
        .prom_mathbox = prom_math,
        .prom_avg = prom_avg,
        .rom_slapstic = rom_slapstic,
    };
    sw_init(&roms);
    if (!nosound) sw_attach_sound(rom_sound, sound_size);
    if (getenv("POKEYLOG")) snd_dbg_log_pokey = 1;
    if (getenv("TMSLOG")) snd_dbg_log_tms = 1;
    { extern int sw_dbg_log_flags; if (getenv("FLAGLOG")) sw_dbg_log_flags = 1; }
    { extern unsigned sw_dbg_halt_pc; if (getenv("HALT_PC")) sw_dbg_halt_pc = (unsigned)strtol(getenv("HALT_PC"), NULL, 16); }
    { extern int snd_render_mask; if (getenv("RENDERMASK")) snd_render_mask = (int)strtol(getenv("RENDERMASK"), NULL, 0); }
    FILE *wav = NULL;
    const int wav_rate = 20050;
    uint32_t wav_samples = 0;
    if (wav_path) {
        wav = fopen(wav_path, "wb");
        uint8_t hdr[44] = {0};
        fwrite(hdr, 1, 44, wav);   /* patched at the end */
    }
    static int16_t abuf[4096];
    double audio_acc = 0;
    sw_set_frame_callback(on_frame, NULL);
    sw_set_dips(esb ? 0xfb : 0x98, (uint8_t)dsw1);
    sw_input()->yaw = (uint8_t)yaw;
    sw_input()->pitch = (uint8_t)pitch;

    const uint32_t slice = SW_CPU_CYCLES_PER_IRQ;   /* ~4 ms of CPU time */
    uint64_t target = (uint64_t)(seconds * SW_CPU_CLOCK);
    double last_report = 0;
    while (sw_total_cycles() < target) {
        now_s = (double)sw_total_cycles() / SW_CPU_CLOCK;
        if (nev == 0) {
            sw_input()->coin1 = (coin_t >= 0 && now_s >= coin_t && now_s < coin_t + 0.25);
            sw_input()->fire  = (fire_t >= 0 && now_s >= fire_t && (hold || now_s < fire_t + 0.25));
        }
        for (int e = 0; e < nev; e++) {
            if (evs[e].t >= 0 && now_s >= evs[e].t) {
                sw_input_t *in = sw_input();
                if (!strcmp(evs[e].key, "fire")) in->fire = evs[e].val;
                else if (!strcmp(evs[e].key, "coin")) in->coin1 = evs[e].val;
                else if (!strcmp(evs[e].key, "pitch")) in->pitch = evs[e].val;
                else if (!strcmp(evs[e].key, "yaw")) in->yaw = evs[e].val;
                else if (!strcmp(evs[e].key, "b2")) in->button2 = evs[e].val;
                else if (!strcmp(evs[e].key, "b3")) in->button3 = evs[e].val;
                else if (!strcmp(evs[e].key, "b4")) in->button4 = evs[e].val;
                evs[e].t = -1;
            }
        }
        sw_run(slice);
        if (!nosound) {
            /* always run the audio path: the speech chip only advances while rendering */
            audio_acc += (double)slice * wav_rate / SW_CPU_CLOCK;
            int n = (int)audio_acc;
            audio_acc -= n;
            if (n > 4096) n = 4096;
            snd_render(abuf, n, wav_rate);
            if (wav) { fwrite(abuf, sizeof(int16_t), n, wav); wav_samples += n; }
        }
        if (now_s - last_report >= 1.0) {
            if (!nosound) {
                printf("   sound: pc=%04X irq=%u cmds=%u pokey_writes=%u\n", snd_pc(), snd_irq_count(), snd_commands(), snd_pokey_writes());
                if (getenv("SNDDUMP")) snd_dbg_dump();
                if (getenv("SNDTOP")) snd_pc_top();
            }
            last_report = now_s;
            printf("t=%.1fs pc=%04X irqs=%u frames=%u (last: %d pts, %d vis) in0=%u in1=%u adc=%u snd_w=%u(last %02X) snd_flag_r=%u snd_r=%u math=%u div=%u\n",
                   now_s, sw_pc(), sw_irq_count(), sw_frame_count(), last_points, last_visible,
                   sw_dbg_in0_reads, sw_dbg_in1_reads, sw_dbg_adc_reads, sw_dbg_snd_writes, sw_dbg_last_snd_cmd, sw_dbg_snd_flag_reads, sw_dbg_snd_reads, sw_dbg_math_runs, sw_dbg_div_ops);
            printf("   irq_taken=%u  483D: writes=%u (last pc %04X val %02X) reads=%u\n", sw_dbg_irq_taken, sw_dbg_w483d, sw_dbg_w483d_pc, sw_dbg_w483d_val, sw_dbg_r483d);
            if (getenv("PCTOP")) pc_top();
            if (getenv("TRACE_AT") && now_s >= atof(getenv("TRACE_AT")) && !sw_dbg_trace_n) sw_dbg_trace_arm = 1;
            if (sw_dbg_trace_n == 2000 || (sw_dbg_trace_n && getenv("TRACE_AT") && now_s >= atof(getenv("TRACE_AT")) + 1.5)) {
                printf("--- IRQ trace (%d instrs), runs of PCs:\n", sw_dbg_trace_n);
                int i = 0;
                while (i < sw_dbg_trace_n) {
                    int j = i;
                    printf("%04X", sw_dbg_trace[i]);
                    while (j + 1 < sw_dbg_trace_n && sw_dbg_trace[j + 1] > sw_dbg_trace[j] && sw_dbg_trace[j + 1] - sw_dbg_trace[j] <= 5) j++;
                    if (j > i) printf("-%04X", sw_dbg_trace[j]);
                    printf(" ");
                    i = j + 1;
                }
                printf("\n");
                sw_dbg_trace_n = -1;
            }
        }
    }
    if (wav) {
        uint32_t data_bytes = wav_samples * 2;
        uint8_t h[44];
        memcpy(h, "RIFF", 4); uint32_t v = 36 + data_bytes; memcpy(h + 4, &v, 4); memcpy(h + 8, "WAVEfmt ", 8);
        v = 16; memcpy(h + 16, &v, 4); uint16_t w = 1; memcpy(h + 20, &w, 2); memcpy(h + 22, &w, 2);
        v = wav_rate; memcpy(h + 24, &v, 4); v = wav_rate * 2; memcpy(h + 28, &v, 4);
        w = 2; memcpy(h + 32, &w, 2); w = 16; memcpy(h + 34, &w, 2); memcpy(h + 36, "data", 4); memcpy(h + 40, &data_bytes, 4);
        fseek(wav, 0, SEEK_SET); fwrite(h, 1, 44, wav); fclose(wav);
        printf("wrote %s (%u samples)\n", wav_path, wav_samples);
    }
    printf("AVG fast-path check: %u frames, %u mismatches\n", sw_dbg_avg_frames, sw_dbg_avg_mismatch);
    if (getenv("HALT_PC")) {
        extern uint16_t sw_dbg_ring[]; extern unsigned sw_dbg_ring_i; extern int sw_dbg_ring_frozen;
        printf("--- PC history before %s (%s), runs:\n", getenv("HALT_PC"), sw_dbg_ring_frozen ? "halt seen" : "halt NOT seen");
        unsigned n = sw_dbg_ring_i < 16384 ? sw_dbg_ring_i : 16384, start = sw_dbg_ring_i - n;
        unsigned run_lo = 0, run_hi = 0, have = 0; int printed = 0;
        for (unsigned k = 0; k < n; k++) {
            unsigned pc = sw_dbg_ring[(start + k) & 16383];
            if (have && pc >= run_lo && pc <= run_hi + 4) { if (pc > run_hi) run_hi = pc; continue; }
            if (have) { if (run_lo == run_hi) printf("%04X ", run_lo); else printf("%04X-%04X ", run_lo, run_hi); if (++printed % 12 == 0) printf("\n"); }
            run_lo = run_hi = pc; have = 1;
        }
        if (have) printf("%04X-%04X\n", run_lo, run_hi);
        { extern uint8_t sw_dbg_peek(uint16_t); extern uint8_t slapstic_state, slapstic_current_bank;
          printf("slapstic state %u bank %u\n", slapstic_state, slapstic_current_bank);
          for (unsigned a = 0x4e40; a < 0x4f10; a += 16) { printf("%04X:", a); for (unsigned i = 0; i < 16; i++) printf(" %02X", sw_dbg_peek((uint16_t)(a + i))); printf("\n"); }
          printf("4800:"); for (unsigned i = 0; i < 16; i++) printf(" %02X", sw_dbg_peek((uint16_t)(0x4800 + i))); printf("\n"); }
    }
    { extern uint32_t snd_dbg_pass_hist[64]; int any = 0;
      for (int i = 0; i < 64; i++) if (snd_dbg_pass_hist[i]) { if (!any) printf("sound poll-loop pass lengths:"); printf(" %d:%u", i, snd_dbg_pass_hist[i]); any = 1; }
      if (any) printf("\n"); }
    if (getenv("REGIONS")) {
        static const char *names[8] = { "0000-1FFF", "2000-3FFF", "4000-5FFF", "6000-7FFF bank", "8000-9FFF slapstic", "A000-BFFF", "C000-DFFF", "E000-FFFF" };
        uint64_t tot = 0, reg[8] = {0};
        for (int i = 0; i < 0x10000; i++) { reg[i >> 13] += sw_dbg_pc_hist[i]; tot += sw_dbg_pc_hist[i]; }
        for (int i = 0; i < 8; i++) printf("main CPU samples in %-20s %5.1f%%\n", names[i], tot ? 100.0 * reg[i] / tot : 0.0);
        { extern uint32_t sw_dbg_bank_hist[2][8];
          for (int b = 0; b < 2; b++) for (int i = 3; i < 8; i++) printf("  page %d %-20s %5.1f%%\n", b, names[i], tot ? 100.0 * sw_dbg_bank_hist[b][i] / tot : 0.0); }
    }
    { uint32_t w, r, c, rt, tk; snd_speech_debug(&w, &r, &c, &rt, &tk);
      printf("speech: written %u read %u gaps %u render-calls %u rate %u talkd %u\n", w, r, (unsigned)snd_speech_underruns(), c, rt, tk); }
    printf("done: %.1fs, %u irqs, %u vector frames (%.1f fps), %d images saved\n",
           now_s, sw_irq_count(), sw_frame_count(), sw_frame_count() / seconds, frames_saved);
    return 0;
}
