/* Host test of the formant engine's DSP: coefficients, stability at every knob corner, loudness, and WAVs to hear.
 *   gcc -O2 -Wall -I formant/plugin formant/plugin/test/host_test.c -lm -o /tmp/fm_test && /tmp/fm_test [wav dir]
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "fm_dsp.h"

#define GAIN 1638                          /* the engine's output gain at full velocity */
static int16_t sine[8192];
static int failures;
static int16_t out[FM_RATE * 20];

static void wav(const char *dir, const char *name, const int16_t *s, int n)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); return; }
    uint32_t d = (uint32_t)n * 2, r = FM_RATE, br = FM_RATE * 2, sz = 36 + d, fl = 16;
    uint16_t pcm = 1, ch = 1, ba = 2, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&sz, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fl, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&r, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&d, 4, 1, f);
    fwrite(s, 2, (size_t)n, f);
    fclose(f);
    printf("wrote %s\n", path);
}

static fm_talk_knobs talk;                  /* page 2: off unless a test sets it */

/* one note, as the engine renders it; vowel/body may glide from a to b over the note. Returns the largest filter
 * state; fills out if given. */
static int32_t note(int32_t va, int32_t vb, int32_t ba, int32_t bb, int32_t breath, int32_t vib, double hz, double secs,
                    int16_t *dst, double *rms)
{
    static fm_voice v;                     /* kept between notes, as the engine's are (only the noise seed carries) */
    fm_voice_reset(&v);
    int32_t inc[128], big = 0;
    int16_t buf[128];
    for (int i = 0; i < 128; i++) inc[i] = (int32_t)(uint32_t)(hz / FM_RATE * 4294967296.0);
    int blocks = (int)(secs * FM_RATE / 128);
    double acc = 0;
    for (int b = 0; b < blocks; b++) {
        fm_knobs k;
        int32_t t = (int32_t)((int64_t)b * 32767 / (blocks > 1 ? blocks - 1 : 1)), vowel, closure;
        int32_t level = fm_talk(&v, va + (int32_t)((int64_t)(vb - va) * t / 32767), &talk, (uint32_t)inc[0], &vowel, &closure);
        fm_knobs_make(&k, vowel, ba + (int32_t)((int64_t)(bb - ba) * t / 32767), breath, vib);
        fm_close(&k, closure);
        int32_t g = fm_render(&v, &k, inc, 128, (GAIN * level) >> 15, buf, sine);
        if (g > big) big = g;
        for (int i = 0; i < 128; i++) {
            if (b >= blocks / 4) acc += (double)buf[i] * buf[i];
            if (dst) dst[b * 128 + i] = buf[i];
        }
    }
    if (rms) *rms = sqrt(acc / ((blocks - blocks / 4) * 128.0));
    return big;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    for (int i = 0; i < 8192; i++) sine[i] = (int16_t)lrint(32767.0 * sin(2 * M_PI * i / 8192.0));

    /* 1. the filter coefficient f = 2 sin(pi F / fs) within 0.5% from 150 Hz to 6.5 kHz */
    for (int hz = 150; hz <= 6500; hz += 50) {
        double want = 2 * sin(M_PI * hz / FM_RATE) * 32768, got = fm_coef_f(hz << 8);
        if (fabs(got - want) > want * 0.005 + 1) { printf("FAIL: coef at %d Hz: %.0f, want %.0f\n", hz, got, want); failures++; }
    }

    /* 2. every knob corner and pitch stays bounded (filter states below 2^17 keep the products in 32 bits) */
    int32_t worst = 0;
    for (int32_t vw = 0; vw <= 32767; vw += 4095)
        for (int32_t bd = 0; bd <= 32767; bd += 16383)
            for (int32_t br = 0; br <= 32767; br += 32767)
                for (double hz = 55; hz <= 1800; hz *= 2)
                    for (int tk = 0; tk < 2; tk++) {
                        talk = (fm_talk_knobs){ tk ? 32767 : 0, 32767, 32767 };
                        int32_t g = note(vw, vw, bd, bd, br, 32767, hz, 0.3, 0, 0);
                        if (g > worst) worst = g;
                    }
    talk = (fm_talk_knobs){ 0, 0, 0 };
    printf("largest filter state %d (limit %d)\n", worst, 1 << 17);
    if (worst >= 1 << 17) { printf("FAIL: filter state too large\n"); failures++; }

    /* 3. loud enough with no breath, and not clipping, across the vowels at middle C */
    double lo = 1e9, hi = 0;
    for (int32_t vw = 0; vw <= 32767; vw += 4095) {
        double r;
        note(vw, vw, 0, 0, 0, 0, 261.6, 1.0, 0, &r);
        if (r < lo) lo = r;
        if (r > hi) hi = r;
    }
    printf("middle C, male, no breath: rms %.0f .. %.0f across the vowels\n", lo, hi);
    if (lo < 1000) { printf("FAIL: too quiet\n"); failures++; }
    if (hi > 6000) { printf("FAIL: too loud for six voices\n"); failures++; }

    /* 4. talk: the same key says the same "word" (vowels and consonants) every time; another key says another.
     * Only the syllables repeat: the pitch drift differs on every note, as it should. */
    {
        static int32_t w[3][689 * 2];
        const uint32_t key[3] = { (uint32_t)(220.0 / FM_RATE * 4294967296.0), (uint32_t)(220.0 / FM_RATE * 4294967296.0),
                                  (uint32_t)(247.0 / FM_RATE * 4294967296.0) };
        fm_talk_knobs tk = { 14000, 20000, 16000 };
        for (int n = 0; n < 3; n++) {
            fm_voice v;
            memset(&v, 0, sizeof v);
            v.rng = 12345u * (uint32_t)(n + 1);    /* a different noise seed each time, as in the engine */
            fm_voice_reset(&v);
            for (int b = 0; b < 689; b++) {
                fm_talk(&v, 20480, &tk, key[n], &w[n][2 * b], &w[n][2 * b + 1]);
                v.blk++;
            }
        }
        if (memcmp(w[0], w[1], sizeof w[0])) { printf("FAIL: the same key said a different word\n"); failures++; }
        if (!memcmp(w[0], w[2], sizeof w[0])) { printf("FAIL: another key said the same word\n"); failures++; }
    }

    /* 5. WAVs */
    int at = 0;
    for (int b = 0; b < 3; b++) { note(0, 32767, b * 16383, b * 16383, 2000, 12000, b ? 220 : 110, 3.0, out + at, 0); at += FM_RATE * 3; }
    wav(dir, "formant-vowel-sweep.wav", out, at);       /* ee -> oo, male, female, child */
    static const int32_t pick[] = { 0, 20480, 32767, 12288, 0 };   /* ee, ah, oo, eh, ee */
    static const double mel[] = { 196, 220, 247, 262, 294, 262, 247, 220 };
    at = 0;
    for (int n = 0; n < 8; n++) { note(pick[n % 5], pick[n % 5], 6000, 6000, 3000, 16000, mel[n], 0.6, out + at, 0); at += (int)(FM_RATE * 0.6); }
    wav(dir, "formant-melody.wav", out, at);
    at = 0;
    for (int b = 0; b < 3; b++) { note(20480, 20480, 0, 0, b * 16383, 0, 147, 1.5, out + at, 0); at += (int)(FM_RATE * 1.5); }
    wav(dir, "formant-breath.wav", out, at);            /* "ah": no breath, half, full */
    at = 0;
    for (int b = 0; b < 2; b++) { note(20480, 20480, b * 32767, b * 32767, 2000, 12000, b ? 330 : 98, 2.0, out + at, 0); at += FM_RATE * 2; }
    wav(dir, "formant-body-ends.wav", out, at);         /* green fully left (low note), fully right (high note) */
    at = 0;
    for (int b = 0; b < 3; b++) { note(20480, 20480, 10000, 10000, 2000, 16384 + b * 8191, 196, 2.0, out + at, 0); at += FM_RATE * 2; }
    wav(dir, "formant-orange.wav", out, at);            /* orange halfway, three-quarters, full */

    /* talk: one long note, then the same key three times (the same word), then two other keys */
    talk = (fm_talk_knobs){ 14000, 20000, 16000 };
    at = 0;
    note(20480, 20480, 10000, 10000, 3000, 12000, 220, 3.0, out + at, 0); at += FM_RATE * 3;
    for (int n = 0; n < 3; n++) { note(20480, 20480, 10000, 10000, 3000, 12000, 220, 0.8, out + at, 0); at += (int)(FM_RATE * 0.8); }
    note(20480, 20480, 10000, 10000, 3000, 12000, 247, 1.2, out + at, 0); at += (int)(FM_RATE * 1.2);
    note(20480, 20480, 10000, 10000, 3000, 12000, 196, 1.2, out + at, 0); at += (int)(FM_RATE * 1.2);
    wav(dir, "formant-talk.wav", out, at);
    talk = (fm_talk_knobs){ 30000, 32767, 32767 };
    at = 0;
    note(16384, 16384, 26000, 26000, 3000, 20000, 262, 4.0, out + at, 0); at += FM_RATE * 4;
    wav(dir, "formant-talk-wild.wav", out, at);         /* fast, wide, choppy, a child, a warble */
    talk = (fm_talk_knobs){ 0, 0, 0 };

    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures != 0;
}
