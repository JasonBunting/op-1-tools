/* Host test of the Mouth engine's DSP: coefficients, stability at every knob corner, loudness, talk (tempo, words,
 * release), voice stealing, and WAVs to hear.
 *   gcc -O2 -Wall -I mouth/plugin mouth/plugin/test/host_test.c -lm -o /tmp/fm_test && /tmp/fm_test [wav dir]
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fm_dsp.h"

#define GAIN 1831                          /* mouth.c FM_GAIN, at full velocity */
#define BEAT 22050                         /* 120 BPM */
static int16_t sine[8192];
static int failures;
static int16_t out[FM_RATE * 24];
static fm_talk_knobs talk;                 /* page 2: off unless a test sets it */
static int32_t vel = 32767;

static void fail(const char *what) { printf("FAIL: %s\n", what); failures++; }

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

/* one block of the engine (mouth.c fm_render_cb without the OP-1) */
static int32_t block(fm_voice *v, int32_t vowel15, int32_t body15, int32_t breath, int32_t vib, uint32_t inc0,
                     const int32_t *inc, int16_t *buf)
{
    int32_t vowel, closure;
    int32_t level = fm_talk(v, vowel15, &talk, inc0, BEAT, &vowel, &closure);
    v->show_vowel = vowel; v->show_closure = closure;
    fm_knobs k;
    fm_knobs_make(&k, vowel, body15, breath, vib, vel);
    fm_track(&k, inc0);
    fm_close(&k, v->kind, closure);
    int32_t gain = (((((GAIN * vel) >> 15) * level) >> 15) * k.level) >> 15;
    return fm_render(v, &k, inc, 128, gain, buf, sine);
}

/* one note; vowel/body may glide from a to b over the note; release_at: blocks before the key is let go (-1: held).
 * Returns the largest filter state; fills dst if given. The voice carries over between notes, as on the OP-1. */
static fm_voice voice;
static int32_t note(int32_t va, int32_t vb, int32_t ba, int32_t bb, int32_t breath, int32_t vib, double hz, double secs,
                    int16_t *dst, double *rms, int release_at)
{
    fm_voice_note(&voice);
    int32_t inc[128], big = 0;
    int16_t buf[128];
    for (int i = 0; i < 128; i++) inc[i] = (int32_t)(uint32_t)(hz / FM_RATE * 4294967296.0);
    int blocks = (int)(secs * FM_RATE / 128);
    double acc = 0;
    for (int b = 0; b < blocks; b++) {
        if (b == release_at) voice.released = 1;
        int32_t t = (int32_t)((int64_t)b * 32767 / (blocks > 1 ? blocks - 1 : 1));
        int32_t g = block(&voice, va + (int32_t)((int64_t)(vb - va) * t / 32767), ba + (int32_t)((int64_t)(bb - ba) * t / 32767),
                          breath, vib, (uint32_t)inc[0], inc, buf);
        if (g > big) big = g;
        for (int i = 0; i < 128; i++) {
            if (b >= blocks / 4) acc += (double)buf[i] * buf[i];
            if (dst) dst[b * 128 + i] = buf[i];
        }
    }
    if (rms) *rms = sqrt(acc / ((blocks - blocks / 4) * 128.0));
    return big;
}

/* the syllables a voice says over n blocks: vowel and closure per block */
static void syllables(int32_t *w, int n, uint32_t inc0, int release_at)
{
    fm_voice v;
    memset(&v, 0, sizeof v);
    fm_voice_reset(&v);
    for (int b = 0; b < n; b++) {
        if (b == release_at) v.released = 1;
        fm_talk(&v, 20480, &talk, inc0, BEAT, &w[2 * b], &w[2 * b + 1]);
        v.blk++;
    }
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    for (int i = 0; i < 8192; i++) sine[i] = (int16_t)lrint(32767.0 * sin(2 * M_PI * i / 8192.0));
    const uint32_t key_a = (uint32_t)(220.0 / FM_RATE * 4294967296.0), key_b = (uint32_t)(247.0 / FM_RATE * 4294967296.0);

    /* 1. the filter coefficient f = 2 sin(pi F / fs) within 0.5% from 150 Hz to 6.5 kHz */
    for (int hz = 150; hz <= 6500; hz += 50) {
        double want = 2 * sin(M_PI * hz / FM_RATE) * 32768, got = fm_coef_f(hz << 8);
        if (fabs(got - want) > want * 0.005 + 1) { printf("FAIL: coef at %d Hz: %.0f, want %.0f\n", hz, got, want); failures++; }
    }

    /* 2. every knob corner and pitch stays bounded (filter states below 2^17 keep the products in 32 bits), talk off
     *    and at its fastest and wildest, soft and hard notes */
    int32_t worst = 0;
    for (int32_t vw = 0; vw <= 32767; vw += 4095)
        for (int32_t bd = 0; bd <= 32767; bd += 8191)
            for (int32_t br = 0; br <= 32767; br += 32767)
                for (double hz = 55; hz <= 1800; hz *= 2)
                    for (int tk = 0; tk < 2; tk++) {
                        talk = (fm_talk_knobs){ tk ? FM_TALK_STEPS - 1 : 0, 0, 32767 };
                        vel = tk ? 32767 : 4000;
                        int32_t g = note(vw, vw, bd, bd, br, 32767, hz, 0.3, 0, 0, -1);
                        if (g > worst) worst = g;
                    }
    talk = (fm_talk_knobs){ 0, 0, 0 };
    vel = 32767;
    printf("largest filter state %d (limit %d)\n", worst, 1 << 17);
    if (worst >= 1 << 17) fail("filter state too large");

    /* 3. every vowel and body about equally loud, and not clipping (fm_levels.h) */
    double lo = 1e9, hi = 0;
    for (int32_t bd = 5461; bd <= 27307; bd += 10923)
        for (int32_t vw = 0; vw <= 32767; vw += 4095) {
            double r, sum = 0;                                         /* over three pitches, as level_fit */
            for (double hz = 110; hz <= 440; hz *= 2) { note(vw, vw, bd, bd, 0, 0, hz, 1.0, 0, &r, -1); sum += r * r; }
            r = sqrt(sum / 3);
            if (r < lo) lo = r;
            if (r > hi) hi = r;
        }
    printf("man, woman, child at 110/220/440 Hz, no breath: rms %.0f .. %.0f\n", lo, hi);
    if (lo < 1200) fail("too quiet");
    if (hi > 5000) fail("too loud for six voices");
    if (hi > lo * 2.0) fail("vowels or bodies differ by more than 6 dB");

    /* 3b. the note is the loudest partial ("ee", "ah", "oo" at C3, C4, C5): a tuner reads the right note, and the voice
     *     isn't thin (before the airflow and chest, middle-C "ah" had its 3rd harmonic 5x the fundamental: read as G5) */
    {
        static int16_t s[FM_RATE];
        static const int32_t vw[3] = { 0, 20480, 32767 };
        static const double hz[3] = { 130.81, 261.63, 523.25 };
        for (int a = 0; a < 3; a++)
            for (int p = 0; p < 3; p++) {
                note(vw[a], vw[a], 10000, 10000, 0, 0, hz[p], 1.0, s, 0, -1);
                double m[6];
                for (int h = 1; h <= 5; h++) {
                    double re = 0, im = 0;
                    for (int i = FM_RATE / 4; i < FM_RATE; i++) {
                        double ph = 2 * M_PI * hz[p] * h * i / FM_RATE;
                        re += s[i] * cos(ph); im += s[i] * sin(ph);
                    }
                    m[h] = sqrt(re * re + im * im);
                }
                for (int h = 2; h <= 5; h++)
                    if (m[h] > m[1]) { printf("FAIL: vowel %d at %.0f Hz: harmonic %d is louder than the note\n", a, hz[p], h); failures++; break; }
            }
    }

    /* 4. talk: the same key says the same word every time, another key another; a fixed word is the same on every
     *    key; syllables follow the tempo; after the key is let go no new syllable starts */
    {
        static int32_t a[2][1378 * 2], b[1378 * 2], c[2][1378 * 2];
        talk = (fm_talk_knobs){ 4, 0, 16384 };                       /* 1/8 */
        syllables(a[0], 1378, key_a, -1);
        syllables(a[1], 1378, key_a, -1);
        syllables(b, 1378, key_b, -1);
        if (memcmp(a[0], a[1], sizeof a[0])) fail("the same key said a different word");
        if (!memcmp(a[0], b, sizeof b)) fail("another key said the same word");
        talk.word = 7;
        syllables(c[0], 1378, key_a, -1);
        syllables(c[1], 1378, key_b, -1);
        if (memcmp(c[0], c[1], sizeof c[0])) fail("word 7 differs between keys");
        /* tempo: at 1/8 and 120 BPM a syllable slot is 0.25 s = 172.3 blocks; count where the vowel target jumps */
        talk = (fm_talk_knobs){ 4, 0, 0 };
        fm_voice v;
        memset(&v, 0, sizeof v);
        fm_voice_reset(&v);
        int starts = 0;
        for (int n = 0; n < 1378; n++) {                             /* 2 s: 8 slots */
            int32_t w0, c0, t0 = v.syl_t;
            fm_talk(&v, 20480, &talk, key_a, BEAT, &w0, &c0);
            v.blk++;
            if (v.syl_t == 1 && (t0 != 0 || n == 0)) starts++;
        }
        printf("1/8 at 120 BPM: %d syllables in 2 s (want 8, fewer when a slot holds)\n", starts);
        if (starts < 6 || starts > 8) fail("talk does not follow the tempo");
        /* release: let go after 0.5 s; the vowel must stop changing once the last glide ends */
        talk = (fm_talk_knobs){ 6, 0, 32767 };                        /* 1/16, wild */
        syllables(a[0], 1378, key_a, 344);
        int moved = 0;
        for (int n = 344 + 40; n < 1378; n++) if (a[0][2 * n] != a[0][2 * (n - 1)] || a[0][2 * n + 1]) moved = 1;
        if (moved) fail("talk kept talking after the key was let go");
        talk = (fm_talk_knobs){ 0, 0, 0 };
    }

    /* 5. voice stealing: a new note on a sounding voice doesn't jump (no click) */
    {
        static int16_t s1[FM_RATE], s2[FM_RATE];
        note(20480, 20480, 16384, 16384, 0, 0, 220, 0.5, s1, 0, -1);
        note(0, 0, 16384, 16384, 0, 0, 330, 0.5, s2, 0, -1);           /* the same voice, at once */
        int last = s1[(int)(0.5 * FM_RATE / 128) * 128 - 1], jump = abs(s2[0] - last), pk = 0, step = 0;
        for (int i = 1; i < FM_RATE / 4; i++) {
            int d = abs(s1[i] - s1[i - 1]); if (d > step) step = d;
            if (abs(s1[i]) > pk) pk = abs(s1[i]);
        }
        printf("steal: jump %d at the new note, largest step within a note %d\n", jump, step);
        if (jump > step) fail("a stolen voice jumps (clicks)");
    }

    /* 6. WAVs */
    int at = 0;
    for (int b = 0; b < 3; b++) { note(0, 32767, 5461 + b * 10923, 5461 + b * 10923, 2000, 12000, b ? 220 : 110, 3.0, out + at, 0, -1); at += FM_RATE * 3; }
    wav(dir, "mouth-vowel-sweep.wav", out, at);          /* ee -> oo, man, woman, child: now even in loudness */
    at = 0;
    for (int b = 0; b < 2; b++) { note(20480, 20480, b * 32767, b * 32767, 2000, 12000, b ? 330 : 98, 2.0, out + at, 0, -1); at += FM_RATE * 2; }
    wav(dir, "mouth-body-ends.wav", out, at);            /* giant (low note), tiny (high note) */
    at = 0;
    for (int b = 0; b < 3; b++) { note(20480, 20480, 10000, 10000, 2000, 16384 + b * 8191, 196, 2.0, out + at, 0, -1); at += FM_RATE * 2; }
    wav(dir, "mouth-orange.wav", out, at);               /* orange halfway, three-quarters, full */
    at = 0;
    for (int b = 0; b < 3; b++) { note(20480, 20480, 0, 0, b * 16383, 0, 147, 1.5, out + at, 0, -1); at += (int)(FM_RATE * 1.5); }
    wav(dir, "mouth-breath.wav", out, at);               /* "ah": no breath, half, full (a whisper) */

    /* talk at 120 BPM: 1/8 on one key (2 s, let go at 1.5 s), the same key twice (the same word), another key, then
     * 1/16 wild, then word 3 on two keys */
    at = 0;
    talk = (fm_talk_knobs){ 4, 0, 16384 };
    note(20480, 20480, 10000, 10000, 2000, 12000, 220, 2.0, out + at, 0, 1033); at += FM_RATE * 2;
    for (int n = 0; n < 2; n++) { note(20480, 20480, 10000, 10000, 2000, 12000, 220, 1.0, out + at, 0, -1); at += FM_RATE; }
    note(20480, 20480, 10000, 10000, 2000, 12000, 247, 1.0, out + at, 0, -1); at += FM_RATE;
    wav(dir, "mouth-talk.wav", out, at);
    at = 0;
    talk = (fm_talk_knobs){ 6, 0, 32767 };
    note(16384, 16384, 26000, 26000, 2000, 20000, 262, 3.0, out + at, 0, -1); at += FM_RATE * 3;
    talk = (fm_talk_knobs){ 4, 3, 16384 };
    note(20480, 20480, 2000, 2000, 2000, 8000, 98, 2.0, out + at, 0, -1); at += FM_RATE * 2;
    note(20480, 20480, 2000, 2000, 2000, 8000, 131, 2.0, out + at, 0, -1); at += FM_RATE * 2;
    wav(dir, "mouth-talk-wild.wav", out, at);            /* a child at 1/16 wild with warble; a giant saying word 3 twice */
    talk = (fm_talk_knobs){ 0, 0, 0 };

    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures != 0;
}
