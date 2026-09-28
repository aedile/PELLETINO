/*
 * test_music.c - run the launcher's music player on the host and listen to it.
 *
 *     host/music/run.sh music/splash.nsf [track]
 *
 * Works on either format, and works on the AUDIO rather than on the player's
 * internals. That is deliberate: an earlier version checked the sequencer's note
 * bookkeeping, reported "ok", and the medal droned one note for ever - the fault
 * was below the sequencer, in how the buffer was handed to the sound chip.
 *
 *   1. it makes sound        - the peak gets off the floor
 *   2. the sound changes     - pitch content differs from one moment to the next;
 *                              a stuck note has the same zero-crossing rate for ever
 *   3. it stops when told    - after chip_stop() the output is silence
 *
 * With a third argument it also writes what it heard to a WAV file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "chiptune.h"
#include "audio_hal.h"

#define RATE     20050
#define SECONDS  30
#define WIN      1024                   /* ~50 ms */

static void wav_header(FILE *f, uint32_t nsamples)
{
    uint32_t bytes = nsamples * 2, v32; uint16_t v16;
    fwrite("RIFF", 1, 4, f); v32 = 36 + bytes; fwrite(&v32, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v32 = 16; fwrite(&v32, 4, 1, f);
    v16 = 1; fwrite(&v16, 2, 1, f); fwrite(&v16, 2, 1, f);
    v32 = RATE; fwrite(&v32, 4, 1, f); v32 = RATE * 2; fwrite(&v32, 4, 1, f);
    v16 = 2; fwrite(&v16, 2, 1, f); v16 = 16; fwrite(&v16, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
}

int main(int argc, char **argv)
{
    if (argc < 2) { puts("usage: test_music <file.nsf|file.mid> [track] [out.wav]"); return 2; }
    int track = argc > 2 ? atoi(argv[2]) : 0;
    FILE *f = fopen(argv[1], "rb");
    if (!f) { printf("cannot open %s\n", argv[1]); return 2; }
    static uint8_t data[1 << 20];
    size_t len = fread(data, 1, sizeof data, f);
    fclose(f);

    if (!chip_load_memory(data, len, track)) { puts("FAIL: would not load"); return 1; }
    chip_play();

    FILE *wav = argc > 3 ? fopen(argv[3], "wb") : NULL;
    if (wav) wav_header(wav, RATE * SECONDS / WIN * WIN);

    static int16_t buf[WIN];
    int peak = 0, loud = 0, windows = 0, distinct = 0, last_zc = -1;
    static uint8_t seen[WIN];
    for (int w = 0; w < RATE * SECONDS / WIN; w++, windows++) {
        audio_render(buf, WIN, RATE);
        if (wav) fwrite(buf, 2, WIN, wav);
        int pk = 0, zc = 0;
        for (int i = 0; i < WIN; i++) {
            int v = buf[i] < 0 ? -buf[i] : buf[i];
            if (v > pk) pk = v;
            if (i && ((buf[i] < 0) != (buf[i - 1] < 0))) zc++;
        }
        if (pk > peak) peak = pk;
        if (pk > 500) {
            loud++;
            if (!seen[zc]) { seen[zc] = 1; distinct++; }
            last_zc = zc;
        }
    }
    (void)last_zc;
    if (wav) fclose(wav);

    chip_stop();
    int after = 0;
    for (int w = 0; w < 8; w++) {
        audio_render(buf, WIN, RATE);
        for (int i = 0; i < WIN; i++) { int v = buf[i] < 0 ? -buf[i] : buf[i]; if (v > after) after = v; }
    }

    int fail = 0;
    printf("%s: %d s rendered\n", argv[1], SECONDS);
    printf("  peak level            %5d / 32767   %s\n", peak, peak > 2000 ? "" : "<-- TOO QUIET");
    printf("  windows with sound    %5d / %d\n", loud, windows);
    printf("  distinct pitch mixes  %5d            %s\n", distinct, distinct >= 12 ? "" : "<-- STUCK");
    printf("  peak after stop       %5d            %s\n", after, after == 0 ? "(silent)" : "<-- NOT SILENT");
    if (peak <= 2000 || distinct < 12 || after != 0) fail = 1;
    puts(fail ? "\nFAIL" : "\nok");
    chip_free();
    return fail;
}
