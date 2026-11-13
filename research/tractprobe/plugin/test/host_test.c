/* Host test of tractprobe's DSP: checks every knob setting stays stable and in range, and writes WAVs to listen to.
 *   gcc -O2 -Wall -I research/tractprobe/plugin research/tractprobe/plugin/test/host_test.c -lm -o /tmp/tp_test && /tmp/tp_test [outdir]
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "tp_dsp.h"

#define RATE 88200
static int16_t sine[8192];
static tp_tube tube;
static tp_shape shape;
static int failures;
static const int steps[] = { 12, 24, 44, 64, 96 };

static void wav(const char *path, const int16_t *s, int n)
{
    FILE *f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); return; }
    uint32_t d = (uint32_t)n * 2, r = RATE, br = RATE * 2, sz = 36 + d;
    uint16_t pcm = 1, ch = 1, ba = 2, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&sz, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    uint32_t fl = 16; fwrite(&fl, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&r, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&d, 4, 1, f); fwrite(s, 2, (size_t)n, f);
    fclose(f);
}

/* render secs of one note; returns the wave peak, fills out (if given) */
static int32_t note(int n, int32_t pos, int32_t height, int32_t breath, double hz, double secs, int16_t *out, int32_t *opeak)
{
    tp_voice v = { 0 };
    tp_voice_reset(&v, &tube);
    tp_shape_make(&shape, n, pos, height, sine);
    int32_t inc[128], peak = 0, op = 0;
    int16_t buf[128];
    for (int i = 0; i < 128; i++) inc[i] = (int32_t)(uint32_t)(hz / RATE * 4294967296.0);
    int blocks = (int)(secs * RATE / 128);
    for (int b = 0; b < blocks; b++) {
        int32_t p = tp_render(&v, &tube, &shape, inc, 128, breath, 8192, buf, sine);
        if (p > peak) peak = p;
        for (int i = 0; i < 128; i++) {
            int32_t a = buf[i] < 0 ? -buf[i] : buf[i];
            if (a > op) op = a;
            if (out) out[b * 128 + i] = buf[i];
        }
    }
    if (opeak) *opeak = op;
    return peak;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    for (int i = 0; i < 8192; i++) sine[i] = (int16_t)lrint(32767.0 * sin(2 * M_PI * i / 8192.0));

    /* 1. every section count and knob corner stays bounded (no blow-up, waves stay well inside int32 maths) */
    int32_t worst = 0, worst_out = 0;
    for (int s = 0; s < 5; s++)
        for (int32_t pos = 0; pos <= 32767; pos += 8191)
            for (int32_t h = 0; h <= 32767; h += 8191)
                for (int32_t br = 0; br <= 32767; br += 32767)
                    for (double hz = 55; hz <= 880; hz *= 4) {
                        int32_t op, p = note(steps[s], pos, h, br, hz, 0.5, 0, &op);
                        if (p > worst) worst = p;
                        if (op > worst_out) worst_out = op;
                        if (p > (1 << 20)) {
                            printf("FAIL: n=%d pos=%d h=%d br=%d hz=%.0f wave peak %d\n", steps[s], pos, h, br, hz, p);
                            failures++;
                        }
                    }
    printf("largest wave %d (limit %d), largest output %d at the engine's gain\n", worst, 1 << 20, worst_out);

    static int16_t out[RATE * 8];

    /* 2. the voice itself is loud enough with no breath (an output stage once left it ~1000x too quiet) */
    for (int s = 0; s < 5; s++) {
        note(steps[s], 4000, 9000, 0, 262, 1.0, out, 0);
        double acc = 0;
        for (int i = RATE / 4; i < RATE; i++) acc += (double)out[i] * out[i];
        double rms = sqrt(acc / (RATE * 3 / 4));
        printf("n=%d middle C, no breath: rms %.0f\n", steps[s], rms);
        if (rms < 1000) { printf("FAIL: n=%d voice too quiet\n", steps[s]); failures++; }
    }

    /* 3. WAVs to listen to: four vowels on 44 sections at 110 Hz, then the section counts on one vowel */
    static const struct { const char *name; int32_t pos, h; } vowels[] = {
        { "ah", 4000, 9000 }, { "ee", 30000, 26000 }, { "oo", 3000, 26000 }, { "eh", 24000, 14000 } };
    char path[512];
    int at = 0;
    for (int i = 0; i < 4; i++) { note(44, vowels[i].pos, vowels[i].h, 3000, 110, 1.0, out + at, 0); at += RATE; }
    snprintf(path, sizeof path, "%s/tractprobe-vowels.wav", dir); wav(path, out, at);
    at = 0;
    for (int s = 0; s < 5; s++) { note(steps[s], 4000, 9000, 3000, 110, 0.75, out + at, 0); at += RATE * 3 / 4; }
    snprintf(path, sizeof path, "%s/tractprobe-sections.wav", dir); wav(path, out, at);
    printf("wrote %s/tractprobe-vowels.wav and tractprobe-sections.wav\n", dir);

    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures != 0;
}
