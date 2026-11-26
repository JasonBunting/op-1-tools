/* Host test of Norn's waveshaper (ws_dsp.h), FM (fm_dsp.h), formant (gr_dsp.h) and wave terrain (wt_dsp.h) as the OP-1 runs them: pitch, a silent start, every knob corner across the
 * keyboard (no overflow, no full-scale), loudness for NORN_GAIN, knob jumps and glides without clicks, a steal
 * mid-note, and WAVs to hear. Plaits itself is checked against in ref_compare.cc.
 *   gcc -O2 -Wall -I norn/plugin norn/plugin/test/host_test.c -lm -o /tmp/norn_test && /tmp/norn_test [wav dir]
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ws_dsp.h"
#include "fm_dsp.h"
#include "gr_dsp.h"
#include "wt_dsp.h"
#include "norn_level.h"

static int failures;
static int16_t buf[WS_RATE * 10];

static void fail(const char *what) { printf("FAIL: %s\n", what); failures++; }

static void wav(const char *dir, const char *name, const int16_t *s, int n)
{
    if (!dir) return;
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); return; }
    uint32_t d = (uint32_t)n * 2, r = WS_RATE, br = WS_RATE * 2, sz = 36 + d, fl = 16;
    uint16_t pcm = 1, ch = 1, ba = 2, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&sz, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fl, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&r, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&d, 4, 1, f);
    fwrite(s, 2, (size_t)n, f);
    fclose(f);
    printf("wrote %s\n", path);
}

static uint32_t inc_of(double hz) { return (uint32_t)(hz / WS_RATE * 4294967296.0); }
static double midi_hz(double n) { return 440.0 * pow(2.0, (n - 69) / 12); }

/* n samples of one voice at fixed knobs and pitch (a note at the start), as norn.c renders a block */
static int play(ws_voice *v, int note_on, int32_t h, int32_t t, int32_t m, double hz, int32_t gain, int16_t *out, int n)
{
    int32_t inc[WS_BLOCK];
    for (int j = 0; j < WS_BLOCK; j++) inc[j] = (int32_t)inc_of(hz);
    if (note_on) ws_voice_note(v);
    ws_knobs k;
    int done = 0;
    for (; done + WS_BLOCK <= n; done += WS_BLOCK) {
        ws_knobs_make(&k, h, t, m, (uint32_t)inc[0]);
        ws_render(v, &k, inc, WS_BLOCK, gain, out + done);
    }
    return done;
}

static double rms_of(const int16_t *s, int n)
{
    double m = 0, e = 0;
    for (int i = 0; i < n; i++) m += s[i];
    m /= n;
    for (int i = 0; i < n; i++) e += (s[i] - m) * (s[i] - m);
    return sqrt(e / n);
}

static int peak(const int16_t *s, int n) { int p = 0; for (int i = 0; i < n; i++) if (abs(s[i]) > p) p = abs(s[i]); return p; }

static void test_pitch(void)
{
    const double notes[] = { 24, 48, 60, 72, 96 };
    for (int i = 0; i < 5; i++) {
        ws_voice v; memset(&v, 0, sizeof v);
        const double hz = midi_hz(notes[i]);
        int n = play(&v, 1, 16384, 0, 0, hz, 32767, buf, WS_RATE);   /* linear curve, no fold: a plain triangle */
        int ups = 0, first = -1, last = -1;
        for (int j = 1; j < n; j++)
            if (buf[j - 1] < 0 && buf[j] >= 0) { if (first < 0) first = j; last = j; ups++; }
        const double got = (ups - 1) * (double)WS_RATE / (last - first);
        const double cents = 1200 * log2(got / hz);
        printf("pitch: note %2.0f %8.2f Hz -> %8.2f Hz (%+.2f cents)\n", notes[i], hz, got, cents);
        if (fabs(cents) > 1) fail("pitch");
    }
}

static void test_start(void)
{
    /* a note starts at the slope's midpoint: the first samples are small, not a jump to the curve's end */
    int worst = 0;
    for (int h = 0; h <= 32767; h += 8191)
        for (int t = 0; t <= 32767; t += 8191) {
            ws_voice v; memset(&v, 0, sizeof v);
            play(&v, 1, h, t, 0, midi_hz(60), 32767, buf, WS_BLOCK);
            if (abs(buf[0]) > worst) worst = abs(buf[0]);
        }
    printf("start: first sample at most %d (of 32767, curves with an offset at 0 start there)\n", worst);
    if (worst > 12000) fail("start");
}

static void test_corners(void)
{
    /* every knob corner and middle, lowest to highest OP-1 pitch: never full scale, the waveform alive */
    const int32_t kv[] = { 0, 16384, 32767 };
    const double notes[] = { 0, 21, 48, 84, 108, 127 };
    int worst_peak = 0, dead = 0;
    for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) for (int c = 0; c < 3; c++) for (int p = 0; p < 6; p++) {
        ws_voice v; memset(&v, 0, sizeof v);
        int n = play(&v, 1, kv[a], kv[b], kv[c], midi_hz(notes[p]), 32767, buf, WS_RATE / 4);
        int pk = peak(buf, n);
        if (pk > worst_peak) worst_peak = pk;
        if (rms_of(buf, n) < 30) dead++;
    }
    printf("corners: largest peak at gain 1 %d, silent %d of 162\n", worst_peak, dead);
    if (worst_peak >= 32767) fail("corners: full scale");
    if (dead) fail("corners: silent");
}

static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }

static void test_level(void)
{
    /* NORN_GAIN: one voice at full velocity, median rms about 2500 of 32767 (as Mouth) over knobs and pitch */
    static double r[5 * 5 * 3 * 4];
    int m = 0, worst_peak = 0;
    const double notes[] = { 36, 48, 60, 72 };
    for (int h = 0; h < 5; h++) for (int t = 0; t < 5; t++) for (int mo = 0; mo < 3; mo++) for (int p = 0; p < 4; p++) {
        ws_voice v; memset(&v, 0, sizeof v);
        int n = play(&v, 1, h * 8191, t * 8191, mo * 16383, midi_hz(notes[p]), NORN_GAIN, buf, WS_RATE / 4);
        r[m++] = rms_of(buf + n / 2, n / 2);
        if (peak(buf, n) > worst_peak) worst_peak = peak(buf, n);
    }
    qsort(r, (size_t)m, sizeof r[0], cmp_d);
    printf("level at NORN_GAIN %d: rms min %.0f median %.0f max %.0f, peak %d (six voices %d)\n",
           NORN_GAIN, r[0], r[m / 2], r[m - 1], worst_peak, worst_peak * 6);
    if (r[m / 2] < 2200 || r[m / 2] > 2800) fail("level: median rms");
}

/* n samples gliding 65 Hz .. 1 kHz at fixed knobs, in blocks of bs */
static void glide(int16_t *out, int n, int bs, int32_t h, int32_t t, int32_t m)
{
    ws_voice v; memset(&v, 0, sizeof v);
    ws_voice_note(&v);
    int32_t inc[WS_BLOCK];
    for (int at = 0; at + bs <= n; at += bs) {
        for (int j = 0; j < bs; j++) inc[j] = (int32_t)inc_of(65 * pow(16, (double)(at + j) / n));
        ws_knobs k;
        ws_knobs_make(&k, h, t, m, (uint32_t)inc[0]);
        ws_render(&v, &k, inc, bs, NORN_GAIN, out + at);
    }
}

static int16_t buf2[WS_RATE * 2];

static void test_blocks(void)
{
    /* the block is not heard: a glide in 128-sample blocks against 16-sample blocks, at strong settings (the folded
     * waveform has steps of thousands of its own, so steps alone can't find a click) */
    const int n = WS_RATE * 2 / WS_BLOCK * WS_BLOCK;
    const int32_t set[][3] = { { 14137, 17831, 32767 }, { 32767, 32767, 0 }, { 0, 32767, 16384 }, { 16384, 0, 32767 } };
    int worst = 0;
    for (int s = 0; s < 4; s++) {
        glide(buf, n, WS_BLOCK, set[s][0], set[s][1], set[s][2]);
        glide(buf2, n, 16, set[s][0], set[s][1], set[s][2]);
        for (int i = 0; i < n; i++) if (abs(buf[i] - buf2[i]) > worst) worst = abs(buf[i] - buf2[i]);
    }
    printf("blocks: 128 against 16-sample blocks through a glide, largest difference %d\n", worst);
    if (worst > 400) fail("blocks");
}

static void test_jumps(void)
{
    /* knob jumps on a smooth waveform (no fold), where a step at a block edge would show: harmonics end to end,
     * morph end to end (pw glides) */
    int32_t inc[WS_BLOCK];
    for (int j = 0; j < WS_BLOCK; j++) inc[j] = (int32_t)inc_of(110);
    int worst_edge = 0, worst_in = 0;
    for (int knob = 0; knob < 2; knob++) {
        ws_voice v; memset(&v, 0, sizeof v);
        ws_voice_note(&v);
        int at = 0;
        for (int blk = 0; blk < 200; blk++) {
            const int32_t jump = (blk / 25) & 1 ? 32767 : 0;
            ws_knobs k;
            ws_knobs_make(&k, knob ? 16384 : jump, 0, knob ? jump : 0, (uint32_t)inc[0]);
            ws_render(&v, &k, inc, WS_BLOCK, NORN_GAIN, buf + at);
            if (at && abs(buf[at] - buf[at - 1]) > worst_edge) worst_edge = abs(buf[at] - buf[at - 1]);
            for (int j = 1; j < WS_BLOCK; j++) if (abs(buf[at + j] - buf[at + j - 1]) > worst_in) worst_in = abs(buf[at + j] - buf[at + j - 1]);
            at += WS_BLOCK;
        }
    }
    printf("jumps: largest step at a block edge %d, inside blocks %d\n", worst_edge, worst_in);
    if (worst_edge > worst_in + 100) fail("jumps: a step at a block edge");
}

static void test_moves(const char *dir)
{
    /* everything moving at once, to hear (norn_moves.wav): sweeps, morph jumps, a four-octave glide */
    ws_voice v; memset(&v, 0, sizeof v);
    int32_t inc[WS_BLOCK];
    int at = 0;
    ws_voice_note(&v);
    for (int blk = 0; blk < 4 * WS_RATE / WS_BLOCK; blk++) {
        const int32_t h = (blk * 397) % 32768, t = (blk * 211) % 32768, m = (blk & 64) ? 32767 : 0;
        const double hz = midi_hz(36 + 48.0 * blk / (4.0 * WS_RATE / WS_BLOCK));
        for (int j = 0; j < WS_BLOCK; j++) inc[j] = (int32_t)inc_of(hz * pow(2, j / (12.0 * WS_BLOCK)));
        ws_knobs k;
        ws_knobs_make(&k, h, t, m, (uint32_t)inc[0]);
        ws_render(&v, &k, inc, WS_BLOCK, NORN_GAIN, buf + at);
        at += WS_BLOCK;
    }
    printf("moves: peak %d\n", peak(buf, at));
    wav(dir, "norn_moves.wav", buf, at);
    if (peak(buf, at) > 8000) fail("moves: too loud");
}

static void test_steal(void)
{
    /* a voice taken mid-note restarts cleanly (norn.c calls ws_voice_note) */
    ws_voice v; memset(&v, 0, sizeof v);
    play(&v, 1, 32767, 32767, 32767, midi_hz(30), NORN_GAIN, buf, WS_RATE / 3);
    int n = play(&v, 1, 0, 0, 0, midi_hz(90), NORN_GAIN, buf, WS_BLOCK * 4);
    printf("steal: first sample %d, peak %d\n", buf[0], peak(buf, n));
    if (abs(buf[0]) > 4000) fail("steal");
}

static void demo(const char *dir)
{
    if (!dir) return;
    /* a scale through each curve: harmonics 0 .. 1, fold rising, morph alternating */
    int at = 0;
    for (int i = 0; i < 9; i++) {
        ws_voice v; memset(&v, 0, sizeof v);
        at += play(&v, 1, i * 4095, i * 4095, (i & 1) * 32767, midi_hz(48 + 2 * i), NORN_GAIN, buf + at, WS_RATE / 2);
    }
    wav(dir, "norn_tour.wav", buf, at);
}

/* ---- FM (fm_dsp.h) ---- */

static int fm_play(fm_voice *v, int note_on, int32_t h, int32_t t, int32_t m, double hz, int32_t gain, int16_t *out, int n)
{
    int32_t inc[FM_BLOCK];
    for (int j = 0; j < FM_BLOCK; j++) inc[j] = (int32_t)inc_of(hz);
    if (note_on) fm_voice_note(v);
    int done = 0;
    for (; done + FM_BLOCK <= n; done += FM_BLOCK) {
        fm_knobs k;
        fm_knobs_make(&k, h, t, m, (uint32_t)inc[FM_BLOCK - 1]);
        fm_render(v, &k, inc, FM_BLOCK, gain, out + done, fm_sine_q);
    }
    return done;
}

static void test_fm_math(void)
{
    double worst_mul = 0, worst_log = 0;
    for (uint32_t x = 1u << 18; x < (1u << 28); x = x * 3 / 2 + 7)     /* 10 Hz .. 12 kHz */
        for (uint32_t m = 1u << 15; m < (1u << 19); m = m * 5 / 4 + 3) {
            const double want = (double)x * m / 65536, got = fm_mul_q16(x, m);
            if (fabs(got - want) / want > worst_mul) worst_mul = fabs(got - want) / want;
        }
    for (uint32_t x = 1000; x < 0xF0000000u; x = x + x / 97 + 1) {
        const double e = fabs(fm_log2(x) / 4096.0 - log2((double)x));
        if (e > worst_log) worst_log = e;
    }
    printf("fm math: multiply within %.2g, log2 within %.4f octave\n", worst_mul, worst_log);
    if (worst_mul > 1e-4) fail("fm: fm_mul_q16");
    if (worst_log > 0.01) fail("fm: fm_log2");
}

static void test_fm_pitch(void)
{
    const double notes[] = { 24, 48, 60, 72, 96 };
    for (int i = 0; i < 5; i++) {
        fm_voice v; memset(&v, 0, sizeof v);
        const double hz = midi_hz(notes[i]);
        int n = fm_play(&v, 1, 16384, 0, 16384, hz, 32767, buf, WS_RATE);     /* no index: the carrier's sine */
        int ups = 0, first = -1, last = -1;
        for (int j = 1; j < n; j++)
            if (buf[j - 1] < 0 && buf[j] >= 0) { if (first < 0) first = j; last = j; ups++; }
        const double got = (ups - 1) * (double)WS_RATE / (last - first);
        const double cents = 1200 * log2(got / hz);
        printf("fm pitch: note %2.0f %8.2f Hz -> %8.2f Hz (%+.2f cents)\n", notes[i], hz, got, cents);
        if (fabs(cents) > 1) fail("fm pitch");
    }
}

static void test_fm_start(void)
{
    /* a note (also a steal) restarts the phases, so the first sample is no bigger a step than the waveform's own
     * (a big index bends the carrier fast from the very first sample), whatever came before */
    int worst = 0, worst_over = -99999;
    for (int h = 0; h <= 32767; h += 8191)
        for (int t = 0; t <= 32767; t += 8191)
            for (int m = 0; m <= 32767; m += 16383) {
                fm_voice v; memset(&v, 0, sizeof v);
                fm_play(&v, 1, 32767, 32767, 32767, midi_hz(40), NORN_GAIN, buf, WS_RATE / 8);
                fm_play(&v, 1, h, t, m, midi_hz(60), 32767, buf, WS_BLOCK);
                int step = 0;
                for (int j = 1; j < WS_BLOCK; j++) if (abs(buf[j] - buf[j - 1]) > step) step = abs(buf[j] - buf[j - 1]);
                if (abs(buf[0]) > worst) worst = abs(buf[0]);
                if (abs(buf[0]) - step > worst_over) worst_over = abs(buf[0]) - step;
            }
    printf("fm start: first sample at most %d, at most %d beyond the waveform's own largest step\n", worst, worst_over);
    if (worst_over > 200) fail("fm start");
}

static void test_fm_corners(void)
{
    const int32_t kv[] = { 0, 16384, 32767 };
    const double notes[] = { 0, 21, 48, 84, 108, 127 };
    int worst_peak = 0, dead = 0;
    for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) for (int c = 0; c < 3; c++) for (int p = 0; p < 6; p++) {
        fm_voice v; memset(&v, 0, sizeof v);
        int n = fm_play(&v, 1, kv[a], kv[b], kv[c], midi_hz(notes[p]), 32767, buf, WS_RATE / 4);
        int pk = peak(buf, n);
        if (pk > worst_peak) worst_peak = pk;
        if (rms_of(buf, n) < 30) dead++;
    }
    printf("fm corners: largest peak at gain 1 %d, silent %d of 162\n", worst_peak, dead);
    if (worst_peak >= 32767) fail("fm corners: full scale");
    if (dead) fail("fm corners: silent");
}

static void test_fm_level(void)
{
    /* Plaits' out_gain (0.6, the waveshaper's 0.7) sets the balance between models; report it beside the waveshaper's */
    static double r[5 * 5 * 3 * 4];
    int m = 0, worst_peak = 0;
    const double notes[] = { 36, 48, 60, 72 };
    for (int h = 0; h < 5; h++) for (int t = 0; t < 5; t++) for (int mo = 0; mo < 3; mo++) for (int p = 0; p < 4; p++) {
        fm_voice v; memset(&v, 0, sizeof v);
        int n = fm_play(&v, 1, h * 8191, t * 8191, mo * 16383, midi_hz(notes[p]), NORN_GAIN, buf, WS_RATE / 4);
        r[m++] = rms_of(buf + n / 2, n / 2);
        if (peak(buf, n) > worst_peak) worst_peak = peak(buf, n);
    }
    qsort(r, (size_t)m, sizeof r[0], cmp_d);
    printf("fm level at NORN_GAIN %d: rms min %.0f median %.0f max %.0f, peak %d (six voices %d)\n",
           NORN_GAIN, r[0], r[m / 2], r[m - 1], worst_peak, worst_peak * 6);
    if (r[m / 2] < 1800 || r[m / 2] > 3400) fail("fm level: median rms");
}

static void test_fm_jumps(void)
{
    /* knob jumps, ratio, index and feedback end to end: the ramps keep block edges like the inside */
    int32_t inc[FM_BLOCK];
    for (int j = 0; j < FM_BLOCK; j++) inc[j] = (int32_t)inc_of(110);
    int worst_edge = 0, worst_in = 0;
    for (int knob = 0; knob < 3; knob++) {
        fm_voice v; memset(&v, 0, sizeof v);
        fm_voice_note(&v);
        int at = 0;
        for (int blk = 0; blk < 200; blk++) {
            const int32_t jump = (blk / 25) & 1 ? 32767 : 0;
            fm_knobs k;
            fm_knobs_make(&k, knob == 0 ? jump : 16384, knob == 1 ? jump : 12000, knob == 2 ? jump : 16384,
                          (uint32_t)inc[0]);
            fm_render(&v, &k, inc, FM_BLOCK, NORN_GAIN, buf + at, fm_sine_q);
            if (at && abs(buf[at] - buf[at - 1]) > worst_edge) worst_edge = abs(buf[at] - buf[at - 1]);
            for (int j = 1; j < FM_BLOCK; j++)
                if (abs(buf[at + j] - buf[at + j - 1]) > worst_in) worst_in = abs(buf[at + j] - buf[at + j - 1]);
            at += FM_BLOCK;
        }
    }
    printf("fm jumps: largest step at a block edge %d, inside blocks %d\n", worst_edge, worst_in);
    if (worst_edge > worst_in + 100) fail("fm jumps: a step at a block edge");
}

static void fm_demo(const char *dir)
{
    if (!dir) return;
    /* ratios across harmonics, the index rising, feedback both ways; then everything sweeping over a rising pitch */
    int at = 0;
    for (int i = 0; i < 9; i++) {
        fm_voice v; memset(&v, 0, sizeof v);
        at += fm_play(&v, 1, i * 4095, 8000 + i * 3000, (i % 3) * 16383, midi_hz(48 + 2 * i), NORN_GAIN, buf + at,
                      WS_RATE / 2);
    }
    fm_voice v; memset(&v, 0, sizeof v);
    int32_t inc[FM_BLOCK];
    fm_voice_note(&v);
    for (int blk = 0; blk < 4 * WS_RATE / FM_BLOCK; blk++) {
        const int32_t h = (blk * 97) % 32768, t = (blk * 211) % 32768, m = (blk * 53) % 32768;
        for (int j = 0; j < FM_BLOCK; j++) inc[j] = (int32_t)inc_of(midi_hz(36 + 36.0 * blk / (4.0 * WS_RATE / FM_BLOCK)));
        fm_knobs k;
        fm_knobs_make(&k, h, t, m, (uint32_t)inc[FM_BLOCK - 1]);
        fm_render(&v, &k, inc, FM_BLOCK, NORN_GAIN, buf + at, fm_sine_q);
        at += FM_BLOCK;
    }
    wav(dir, "norn_fm_tour.wav", buf, at);
}

/* ---- formant (gr_dsp.h) ---- */

static int gr_play(gr_voice *v, int note_on, int32_t h, int32_t t, int32_t m, double hz, int32_t gain, int16_t *out, int n)
{
    int32_t inc[GR_BLOCK];
    for (int j = 0; j < GR_BLOCK; j++) inc[j] = (int32_t)inc_of(hz);
    if (note_on) gr_voice_note(v);
    int done = 0;
    for (; done + GR_BLOCK <= n; done += GR_BLOCK) {
        gr_knobs k;
        gr_knobs_make(&k, h, t, m, (uint32_t)inc[0]);
        gr_render(v, &k, inc, GR_BLOCK, gain, out + done, fm_sine_q);
    }
    return done;
}

/* the level of frequency hz in s (Hann window), linear */
static double tone(const int16_t *s, int n, double hz)
{
    double re = 0, im = 0, ws = 0;
    for (int i = 0; i < n; i++) {
        const double w = 0.5 - 0.5 * cos(2 * M_PI * i / (n - 1)), p = 2 * M_PI * hz * i / WS_RATE;
        re += w * s[i] * cos(p); im += w * s[i] * sin(p); ws += w;
    }
    return 2 * sqrt(re * re + im * im) / ws;
}

static void test_gr_pitch(void)
{
    /* the pulse train repeats at the note: its energy sits on the note's harmonics, not between them */
    const double notes[] = { 36, 48, 60, 72, 84 };
    const int32_t set[][3] = { { 16384, 16384, 16384 }, { 0, 8000, 32767 }, { 32767, 30000, 0 } };
    double worst = -999;
    for (int i = 0; i < 5; i++) for (int s = 0; s < 3; s++) {
        gr_voice v; memset(&v, 0, sizeof v);
        const double hz = midi_hz(notes[i]);
        int n = gr_play(&v, 1, set[s][0], set[s][1], set[s][2], hz, 32767, buf, WS_RATE / 2);
        double on = 0, off = 0;
        for (int h = 1; h * hz < 8000 && h < 40; h++) {
            const double a = tone(buf + n / 2, n / 2, h * hz), b = tone(buf + n / 2, n / 2, (h + 0.5) * hz);
            on += a * a; off += b * b;
        }
        const double db = 10 * log10(off / on + 1e-12);
        if (db > -30) printf("  note %.0f set %d: %.1f dB (on %.0f)\n", notes[i], s, db, sqrt(on));
        if (db > worst) worst = db;
    }
    printf("formant pitch: energy between the note's harmonics at most %.1f dB below them\n", worst);
    if (worst > -30) fail("formant pitch");
}

static void test_gr_start(void)
{
    int worst = 0, worst_over = -99999;
    for (int h = 0; h <= 32767; h += 8191)
        for (int t = 0; t <= 32767; t += 8191)
            for (int m = 0; m <= 32767; m += 16383) {
                gr_voice v; memset(&v, 0, sizeof v);
                gr_play(&v, 1, 32767, 32767, 32767, midi_hz(40), NORN_GAIN, buf, WS_RATE / 8);
                gr_play(&v, 1, h, t, m, midi_hz(60), 32767, buf, WS_BLOCK);
                int step = 0;
                for (int j = 1; j < WS_BLOCK; j++) if (abs(buf[j] - buf[j - 1]) > step) step = abs(buf[j] - buf[j - 1]);
                if (abs(buf[0]) > worst) worst = abs(buf[0]);
                if (abs(buf[0]) - step > worst_over) worst_over = abs(buf[0]) - step;
            }
    printf("formant start: first sample at most %d, at most %d beyond the waveform's own largest step\n", worst, worst_over);
    if (worst_over > 200) fail("formant start");
}

static void test_gr_corners(void)
{
    const int32_t kv[] = { 0, 16384, 32767 };
    const double notes[] = { 0, 21, 48, 84, 108, 127 };
    int worst_peak = 0, dead = 0;
    for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) for (int c = 0; c < 3; c++) for (int p = 0; p < 6; p++) {
        gr_voice v; memset(&v, 0, sizeof v);
        int n = gr_play(&v, 1, kv[a], kv[b], kv[c], midi_hz(notes[p]), 32767, buf, WS_RATE / 4);
        int pk = peak(buf, n);
        if (pk > worst_peak) worst_peak = pk;
        if (rms_of(buf + n / 2, n / 2) < 30) { dead++; printf("  silent: h %d t %d m %d note %.0f\n", kv[a], kv[b], kv[c], notes[p]); }
    }
    printf("formant corners: largest peak at gain 1 %d, silent %d of 162\n", worst_peak, dead);
    if (worst_peak >= 32767) fail("formant corners: full scale");
    if (dead) fail("formant corners: silent");
}

static void test_gr_level(void)
{
    static double r[5 * 5 * 3 * 4];
    int m = 0, worst_peak = 0;
    const double notes[] = { 36, 48, 60, 72 };
    for (int h = 0; h < 5; h++) for (int t = 0; t < 5; t++) for (int mo = 0; mo < 3; mo++) for (int p = 0; p < 4; p++) {
        gr_voice v; memset(&v, 0, sizeof v);
        int n = gr_play(&v, 1, h * 8191, t * 8191, mo * 16383, midi_hz(notes[p]), NORN_GAIN, buf, WS_RATE / 4);
        r[m++] = rms_of(buf + n / 2, n / 2);
        if (peak(buf, n) > worst_peak) worst_peak = peak(buf, n);
    }
    qsort(r, (size_t)m, sizeof r[0], cmp_d);
    printf("formant level at NORN_GAIN %d: rms min %.0f median %.0f max %.0f, peak %d (six voices %d)\n",
           NORN_GAIN, r[0], r[m / 2], r[m - 1], worst_peak, worst_peak * 6);
    /* Plaits' own balance makes this model quieter (narrow pulses carry little energy): ref_compare checks the level
     * against Plaits; here only that it is neither silent nor loud */
    if (r[m / 2] < 300 || r[m / 2] > 3400) fail("formant level: median rms");
}

static void test_gr_jumps(void)
{
    int32_t inc[GR_BLOCK];
    for (int j = 0; j < GR_BLOCK; j++) inc[j] = (int32_t)inc_of(110);
    int worst_edge = 0, worst_in = 0;
    for (int knob = 0; knob < 3; knob++) {
        gr_voice v; memset(&v, 0, sizeof v);
        gr_voice_note(&v);
        int at = 0;
        for (int blk = 0; blk < 200; blk++) {
            const int32_t jump = (blk / 25) & 1 ? 32767 : 0;
            gr_knobs k;
            gr_knobs_make(&k, knob == 0 ? jump : 24000, knob == 1 ? jump : 12000, knob == 2 ? jump : 16384,
                          (uint32_t)inc[0]);
            gr_render(&v, &k, inc, GR_BLOCK, NORN_GAIN, buf + at, fm_sine_q);
            if (at && abs(buf[at] - buf[at - 1]) > worst_edge) worst_edge = abs(buf[at] - buf[at - 1]);
            for (int j = 1; j < GR_BLOCK; j++)
                if (abs(buf[at + j] - buf[at + j - 1]) > worst_in) worst_in = abs(buf[at + j] - buf[at + j - 1]);
            at += GR_BLOCK;
        }
    }
    printf("formant jumps: largest step at a block edge %d, inside blocks %d\n", worst_edge, worst_in);
    if (worst_edge > worst_in + 100) fail("formant jumps: a step at a block edge");
}

static void gr_demo(const char *dir)
{
    if (!dir) return;
    /* the formant sweeping (a vowel-like "wow"), the second formant's interval, the carrier's shape; then everything
     * moving over a rising pitch */
    int at = 0;
    for (int i = 0; i < 9; i++) {
        gr_voice v; memset(&v, 0, sizeof v);
        at += gr_play(&v, 1, i * 4095, 4000 + i * 3000, (i % 3) * 16383, midi_hz(43 + 2 * i), NORN_GAIN, buf + at,
                      WS_RATE / 2);
    }
    gr_voice v; memset(&v, 0, sizeof v);
    int32_t inc[GR_BLOCK];
    gr_voice_note(&v);
    for (int blk = 0; blk < 4 * WS_RATE / GR_BLOCK; blk++) {
        const int32_t h = (blk * 97) % 32768, t = (int32_t)(16384 + 16000 * sin(blk / 40.0)), m = (blk * 53) % 32768;
        for (int j = 0; j < GR_BLOCK; j++) inc[j] = (int32_t)inc_of(midi_hz(36 + 24.0 * blk / (4.0 * WS_RATE / GR_BLOCK)));
        gr_knobs k;
        gr_knobs_make(&k, h, t, m, (uint32_t)inc[0]);
        gr_render(&v, &k, inc, GR_BLOCK, NORN_GAIN, buf + at, fm_sine_q);
        at += GR_BLOCK;
    }
    wav(dir, "norn_formant_tour.wav", buf, at);
}

/* ---- wave terrain (wt_dsp.h) ---- */

static int wt_play(wt_voice *v, int note_on, int32_t h, int32_t t, int32_t m, double hz, int32_t gain, int16_t *out, int n)
{
    int32_t inc[WT_BLOCK];
    for (int j = 0; j < WT_BLOCK; j++) inc[j] = (int32_t)inc_of(hz);
    if (note_on) wt_voice_note(v);
    int done = 0;
    for (; done + WT_BLOCK <= n; done += WT_BLOCK) {
        wt_knobs k;
        wt_knobs_make(&k, h, t, m, (uint32_t)inc[0]);
        wt_render(v, &k, inc, WT_BLOCK, gain, out + done, fm_sine_q);
    }
    return done;
}

static void test_wt_pitch(void)
{
    /* the path circles at the note: the output repeats at it, every terrain */
    const double notes[] = { 36, 48, 60, 72, 84 };
    double worst = -999;
    for (int i = 0; i < 5; i++) for (int s = 0; s < 8; s++) {
        wt_voice v; memset(&v, 0, sizeof v);
        const double hz = midi_hz(notes[i]);
        int n = wt_play(&v, 1, s * 4681, 24000, 16384 + (s & 1) * 12000, hz, 32767, buf, WS_RATE / 2);
        double on = 0, off = 0;
        for (int h = 1; h * hz < 8000 && h < 40; h++) {
            const double a = tone(buf + n / 2, n / 2, h * hz), b = tone(buf + n / 2, n / 2, (h + 0.5) * hz);
            on += a * a; off += b * b;
        }
        const double db = 10 * log10(off / on + 1e-12);
        if (db > -30) printf("  note %.0f terrain %d: %.1f dB\n", notes[i], s, db);
        if (db > worst) worst = db;
    }
    printf("terrain pitch: energy between the note's harmonics at most %.1f dB below them\n", worst);
    if (worst > -30) fail("terrain pitch");
}

static void test_wt_start(void)
{
    int worst = 0, worst_over = -99999;
    for (int h = 0; h <= 32767; h += 4681)
        for (int t = 0; t <= 32767; t += 8191)
            for (int m = 0; m <= 32767; m += 16383) {
                wt_voice v; memset(&v, 0, sizeof v);
                wt_play(&v, 1, 32767, 32767, 32767, midi_hz(40), NORN_GAIN, buf, WS_RATE / 8);
                wt_play(&v, 1, h, t, m, midi_hz(60), 32767, buf, WS_BLOCK);
                int step = 0;
                for (int j = 1; j < WS_BLOCK; j++) if (abs(buf[j] - buf[j - 1]) > step) step = abs(buf[j] - buf[j - 1]);
                if (abs(buf[0]) > worst) worst = abs(buf[0]);
                if (abs(buf[0]) - step > worst_over) worst_over = abs(buf[0]) - step;
            }
    printf("terrain start: first sample at most %d, at most %d beyond the waveform's own largest step\n", worst, worst_over);
    if (worst_over > 200) fail("terrain start");
}

static void test_wt_corners(void)
{
    const int32_t kv[] = { 0, 16384, 32767 }, hv[] = { 0, 4681, 9362, 14043, 18724, 23405, 28086, 32767 };
    const double notes[] = { 0, 21, 48, 84, 108, 127 };
    int worst_peak = 0, dead = 0, total = 0;
    for (int a = 0; a < 8; a++) for (int b = 0; b < 3; b++) for (int c = 0; c < 3; c++) for (int p = 0; p < 6; p++) {
        wt_voice v; memset(&v, 0, sizeof v);
        int n = wt_play(&v, 1, hv[a], kv[b], kv[c], midi_hz(notes[p]), 32767, buf, WS_RATE / 4);
        int pk = peak(buf, n);
        total++;
        if (pk > worst_peak) worst_peak = pk;
        /* silence is allowed only where Plaits is silent too: the top notes, whose circle shrinks to nothing */
        if (rms_of(buf + n / 2, n / 2) < 30 && notes[p] < 108) {
            dead++; printf("  silent: h %d t %d m %d note %.0f\n", hv[a], kv[b], kv[c], notes[p]);
        }
    }
    printf("terrain corners: largest peak at gain 1 %d, silent %d of %d\n", worst_peak, dead, total);
    if (worst_peak > 32767) fail("terrain corners: over full scale");
    if (dead) fail("terrain corners: silent");
}

static void test_wt_level(void)
{
    static double r[8 * 5 * 3 * 4];
    int m = 0, worst_peak = 0;
    const double notes[] = { 36, 48, 60, 72 };
    for (int h = 0; h < 8; h++) for (int t = 0; t < 5; t++) for (int mo = 0; mo < 3; mo++) for (int p = 0; p < 4; p++) {
        wt_voice v; memset(&v, 0, sizeof v);
        int n = wt_play(&v, 1, h * 4681, t * 8191, mo * 16383, midi_hz(notes[p]), NORN_GAIN, buf, WS_RATE / 4);
        r[m++] = rms_of(buf + n / 2, n / 2);
        if (peak(buf, n) > worst_peak) worst_peak = peak(buf, n);
    }
    qsort(r, (size_t)m, sizeof r[0], cmp_d);
    printf("terrain level at NORN_GAIN %d: rms min %.0f median %.0f max %.0f, peak %d (six voices %d)\n",
           NORN_GAIN, r[0], r[m / 2], r[m - 1], worst_peak, worst_peak * 6);
    if (r[m / 2] < 1500 || r[m / 2] > 3400) fail("terrain level: median rms");
}

static void test_wt_jumps(void)
{
    /* each knob jumping end to end every 25 blocks, and harmonics gliding across all eight terrains: no step at a
     * block edge beyond the waveform's own (a harmonics jump crosses terrains, so it is reported, not judged) */
    static const char *names[] = { "harmonics jumps", "timbre jumps", "morph jumps", "harmonics glide" };
    int32_t inc[WT_BLOCK];
    for (int j = 0; j < WT_BLOCK; j++) inc[j] = (int32_t)inc_of(110);
    for (int knob = 0; knob < 4; knob++) {
        int worst_edge = 0, worst_in = 0;
        wt_voice v; memset(&v, 0, sizeof v);
        wt_voice_note(&v);
        int at = 0;
        for (int blk = 0; blk < 200; blk++) {
            const int32_t jump = (blk / 25) & 1 ? 32767 : 0;
            const int32_t h = knob == 0 ? jump : knob == 3 ? blk * 32767 / 199 : 12000;
            wt_knobs k;
            wt_knobs_make(&k, h, knob == 1 ? jump : 20000, knob == 2 ? jump : 16384, (uint32_t)inc[0]);
            wt_render(&v, &k, inc, WT_BLOCK, NORN_GAIN, buf + at, fm_sine_q);
            if (at && abs(buf[at] - buf[at - 1]) > worst_edge) worst_edge = abs(buf[at] - buf[at - 1]);
            for (int j = 1; j < WT_BLOCK; j++)
                if (abs(buf[at + j] - buf[at + j - 1]) > worst_in) worst_in = abs(buf[at + j] - buf[at + j - 1]);
            at += WT_BLOCK;
        }
        printf("terrain %s: largest step at a block edge %d, inside blocks %d\n", names[knob], worst_edge, worst_in);
        if (knob != 0 && worst_edge > worst_in + 100) fail("terrain jumps: a step at a block edge");
    }
}

static void wt_demo(const char *dir)
{
    if (!dir) return;
    /* each terrain in turn with the circle growing, then everything moving over a rising pitch */
    int at = 0;
    int32_t inc[WT_BLOCK];
    for (int i = 0; i < 8; i++) {
        wt_voice v; memset(&v, 0, sizeof v);
        wt_voice_note(&v);
        for (int j = 0; j < WT_BLOCK; j++) inc[j] = (int32_t)inc_of(midi_hz(45));
        for (int blk = 0; blk < WS_RATE * 7 / 10 / WT_BLOCK; blk++) {
            wt_knobs k;
            wt_knobs_make(&k, i * 4681, blk * 32767 / (WS_RATE * 7 / 10 / WT_BLOCK), 16384, (uint32_t)inc[0]);
            wt_render(&v, &k, inc, WT_BLOCK, NORN_GAIN, buf + at, fm_sine_q);
            at += WT_BLOCK;
        }
    }
    wt_voice v; memset(&v, 0, sizeof v);
    wt_voice_note(&v);
    for (int blk = 0; blk < 4 * WS_RATE / WT_BLOCK; blk++) {
        const int32_t h = (blk * 47) % 32768, t = (int32_t)(16384 + 16000 * sin(blk / 40.0)),
                      m = (int32_t)(16384 + 16000 * sin(blk / 97.0));
        for (int j = 0; j < WT_BLOCK; j++) inc[j] = (int32_t)inc_of(midi_hz(36 + 24.0 * blk / (4.0 * WS_RATE / WT_BLOCK)));
        wt_knobs k;
        wt_knobs_make(&k, h, t, m, (uint32_t)inc[0]);
        wt_render(&v, &k, inc, WT_BLOCK, NORN_GAIN, buf + at, fm_sine_q);
        at += WT_BLOCK;
    }
    wav(dir, "norn_terrain_tour.wav", buf, at);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : 0;
    test_pitch();
    test_start();
    test_corners();
    test_level();
    test_blocks();
    test_jumps();
    test_moves(dir);
    test_steal();
    demo(dir);
    test_fm_math();
    test_fm_pitch();
    test_fm_start();
    test_fm_corners();
    test_fm_level();
    test_fm_jumps();
    fm_demo(dir);
    test_gr_pitch();
    test_gr_start();
    test_gr_corners();
    test_gr_level();
    test_gr_jumps();
    gr_demo(dir);
    test_wt_pitch();
    test_wt_start();
    test_wt_corners();
    test_wt_level();
    test_wt_jumps();
    wt_demo(dir);
    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures != 0;
}
