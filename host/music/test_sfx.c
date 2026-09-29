/*
 * test_sfx.c - the launcher's sound effects, checked as audio.
 *
 *     host/music/sfx.sh [dir-for-wavs]
 *
 * Nothing here can say whether an effect sounds good. It says whether it is the
 * kind of sound it claims to be: that it starts at once, is bright or dull as
 * intended, dies away, ends, cannot wrap when it lands on loud music, and
 * leaves the music alone once it is over.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "chiptune.h"
#include "sfx.h"

#define RATE 20050
static int failures;
#define CHECK(ok, ...) do { printf("  %-4s ", (ok) ? "ok" : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(ok)) failures++; } while (0)

static double rms(const int16_t *b, int from, int n)
{
    double s = 0;
    for (int i = from; i < from + n; i++) s += (double)b[i] * b[i];
    return sqrt(s / n);
}

static int crossings_per_second(const int16_t *b, int from, int n)
{
    int c = 0;
    for (int i = from + 1; i < from + n; i++) if ((b[i - 1] < 0) != (b[i] < 0)) c++;
    return (int)((long)c * RATE / n);
}

static void write_wav(const char *dir, const char *name, const int16_t *b, uint32_t n)
{
    if (!dir) return;
    char path[512];
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t bytes = n * 2, v32; uint16_t v16;
    fwrite("RIFF", 1, 4, f); v32 = 36 + bytes; fwrite(&v32, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v32 = 16; fwrite(&v32, 4, 1, f);
    v16 = 1; fwrite(&v16, 2, 1, f); fwrite(&v16, 2, 1, f);
    v32 = RATE; fwrite(&v32, 4, 1, f); v32 = RATE * 2; fwrite(&v32, 4, 1, f);
    v16 = 2; fwrite(&v16, 2, 1, f); v16 = 16; fwrite(&v16, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(b, 2, n, f);
    fclose(f);
    printf("       wrote %s\n", path);
}

/* render it the way the firmware does: in whatever odd-sized pieces audio_hal asks for */
static void render(int16_t *b, int n)
{
    for (int at = 0; at < n; ) {
        int piece = 200 + (at * 7) % 900;
        if (piece > n - at) piece = n - at;
        sfx_mix(b + at, piece, RATE);
        at += piece;
    }
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : NULL;
    static int16_t b[RATE * 4];
    int ms = RATE / 1000;

    printf("shing\n");
    memset(b, 0, sizeof b);
    chip_sfx(CHIP_SFX_SHING);
    render(b, RATE * 4);
    int peak = 0;
    for (int i = 0; i < 30 * ms; i++) if (abs(b[i]) > peak) peak = abs(b[i]);
    double early = rms(b, 20 * ms, 60 * ms), late = rms(b, 600 * ms, 60 * ms), gone = rms(b, 3500 * ms, 100 * ms);
    CHECK(peak > 4000 && peak < 30000, "starts at once: peak %d in the first 30 ms", peak);
    CHECK(crossings_per_second(b, 50 * ms, 200 * ms) > 5000, "is bright: %d zero crossings a second", crossings_per_second(b, 50 * ms, 200 * ms));
    CHECK(late > 1 && late < early * 0.45, "rings and dies away: %.0f rms at 50 ms, %.0f at 600 ms", early, late);
    CHECK(gone == 0 && !sfx_active(), "ends: %.0f rms at 3.5 s", gone);
    write_wav(dir, "shing", b, RATE * 2);

    printf("click\n");
    memset(b, 0, sizeof b);
    chip_sfx(CHIP_SFX_CLICK);
    render(b, RATE);
    peak = 0;
    for (int i = 0; i < 5 * ms; i++) if (abs(b[i]) > peak) peak = abs(b[i]);
    CHECK(peak > 3000, "starts at once: peak %d in the first 5 ms", peak);
    CHECK(rms(b, 150 * ms, 100 * ms) == 0 && !sfx_active(), "is over inside 150 ms");
    write_wav(dir, "click", b, RATE / 4);

    printf("coin\n");
    memset(b, 0, sizeof b);
    chip_sfx(CHIP_SFX_COIN);
    render(b, RATE * 2);
    int first = crossings_per_second(b, 10 * ms, 60 * ms), second = crossings_per_second(b, 100 * ms, 200 * ms);
    CHECK(abs(first - 2 * 988) < 80, "first note is B5: %d crossings a second, 1976 expected", first);
    CHECK(abs(second - 2 * 1319) < 80, "second is E6: %d crossings a second, 2638 expected", second);
    CHECK(rms(b, 1000 * ms, 100 * ms) == 0 && !sfx_active(), "is over inside a second");
    write_wav(dir, "coin", b, RATE);

    printf("battery low\n");
    memset(b, 0, sizeof b);
    chip_sfx(CHIP_SFX_LOW);
    render(b, RATE * 2);
    first = crossings_per_second(b, 10 * ms, 120 * ms); second = crossings_per_second(b, 230 * ms, 200 * ms);
    CHECK(second < first && abs(second - 2 * 440) < 60, "falls: %d then %d crossings a second", first, second);
    CHECK(rms(b, 160 * ms, 40 * ms) == 0, "with a gap between the two");
    CHECK(rms(b, 1000 * ms, 100 * ms) == 0 && !sfx_active(), "is over inside a second");
    write_wav(dir, "battery_low", b, RATE);

    printf("held tone\n");
    memset(b, 0, sizeof b);
    for (int i = 0; i < 100; i++) {                 /* two seconds, rising, as the hold fills */
        chip_tone(330 + i * 6);
        sfx_mix(b + i * (RATE / 50), RATE / 50, RATE);
    }
    chip_tone(0);
    sfx_mix(b + 2 * RATE, RATE, RATE);
    first = crossings_per_second(b, 100 * ms, 100 * ms); second = crossings_per_second(b, 1800 * ms, 100 * ms);
    CHECK(abs(first - 2 * 363) < 60 && abs(second - 2 * 873) < 80, "rises: %d then %d crossings a second", first, second);
    int jump = 0;
    for (int i = 1; i < 3 * RATE; i++) if (abs(b[i] - b[i - 1]) > jump) jump = abs(b[i] - b[i - 1]);
    CHECK(jump < 900, "never clicks: the biggest step between samples is %d", jump);
    CHECK(rms(b, 2100 * ms, 100 * ms) == 0 && !sfx_active(), "stops when told");
    write_wav(dir, "hold", b, RATE * 2 + RATE / 4);

    printf("over music\n");
    for (int i = 0; i < RATE; i++) b[i] = (i / 40) & 1 ? 32000 : -32000;     /* as loud as music gets */
    chip_sfx(CHIP_SFX_SHING);
    render(b, RATE);
    int wrapped = 0;
    for (int i = 0; i < RATE; i++) if (((i / 40) & 1) != (b[i] > 0)) wrapped++;
    CHECK(wrapped == 0, "clips instead of wrapping: %d samples changed sign", wrapped);
    static int16_t after[RATE * 4];
    render(after, RATE * 4);                                                 /* let it finish */
    for (int i = 0; i < RATE; i++) b[i] = (int16_t)(i * 3);
    render(b, RATE);
    int touched = 0;
    for (int i = 0; i < RATE; i++) if (b[i] != (int16_t)(i * 3)) touched++;
    CHECK(touched == 0, "leaves the music alone afterwards: %d samples altered", touched);

    printf("\n%s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
