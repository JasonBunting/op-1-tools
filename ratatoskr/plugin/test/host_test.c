/* Host test of Ratatoskr's DSP (rt_dsp.h): the knob laws, the playhead (speed, direction, wrap, width, wander), the
 * grains (inside the recording, pitch, loudness at full density), silence with nothing recorded, and WAVs to hear.
 *   gcc -O2 -Wall -I ratatoskr/plugin ratatoskr/plugin/test/host_test.c -lm -o /tmp/rt_test && /tmp/rt_test [wav dir]
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt_dsp.h"

#define SRC_RATE 44100
#define OUT_RATE 88200
#define C4_RATE  512u                       /* 22.10: 0.5 source samples per voice sample, the recording's speed */
static int16_t sine[8192];
static int16_t rec[SRC_RATE * 6];           /* 6 s, as the sampler's limit */
static int failures;

static void fail(const char *what) { printf("FAIL: %s\n", what); failures++; }
static void check(int ok, const char *what) { if (!ok) fail(what); }

static void wav(const char *dir, const char *name, const int16_t *s, int n, uint32_t rate)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); return; }
    uint32_t d = (uint32_t)n * 2, br = rate * 2, sz = 36 + d, fl = 16;
    uint16_t pcm = 1, ch = 1, ba = 2, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&sz, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fl, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&d, 4, 1, f);
    fwrite(s, 2, (size_t)n, f);
    fclose(f);
    printf("wrote %s\n", path);
}

/* a recording to roam through: 16 notes of a minor scale, 250 ms each, a few harmonics, plucked */
static int make_rec(void)
{
    static const int semis[16] = { 0, 3, 5, 7, 10, 12, 10, 7, 5, 3, 0, -2, 0, 3, 7, 12 };
    int n = SRC_RATE * 4;
    for (int i = 0; i < n; i++) {
        int note = i / (SRC_RATE / 4), t = i % (SRC_RATE / 4);
        double hz = 220.0 * pow(2.0, semis[note] / 12.0), ph = 2 * M_PI * hz * i / SRC_RATE;
        double env = exp(-3.0 * t / (SRC_RATE / 4.0));
        double s = sin(ph) + 0.4 * sin(2 * ph) + 0.2 * sin(3 * ph);
        rec[i] = (int16_t)(s * env * 16000);
    }
    return n;
}

/* the last argument sets drift, jumps and turns together (the old single "wander"); chaos and bounce start off */
static rt_knobs knobs(int32_t pos, int32_t size, int32_t speed, int32_t dens, int32_t spray, int32_t width, int32_t wander)
{
    rt_knobs k = { pos, size, speed, dens, spray, width, wander, wander, wander, 0, 0 };
    return k;
}

/* a speed knob word for a speed (x, signed) */
static int32_t speed_word(double x)
{
    double m = sqrt(fabs(x) / 4.0);
    int32_t a = RT_SPEED_DEAD + (int32_t)(m * (16383 - RT_SPEED_DEAD) + 0.5);
    return x < 0 ? 16384 - a : 16384 + a;
}

static void test_laws(void)
{
    check(rt_speed_q16(16384) == 0 && rt_speed_q16(16384 + RT_SPEED_DEAD) == 0 && rt_speed_q16(16384 - RT_SPEED_DEAD) == 0,
          "speed: the middle is frozen");
    int32_t full = rt_speed_q16(32767), back = rt_speed_q16(0);
    check(full > 262000 && full <= 262144, "speed: fully right is 4x");
    check(back < -262000 && back >= -262144, "speed: fully left is -4x");
    int32_t one = rt_speed_q16(speed_word(1.0));
    check(abs(one - 65536) < 700, "speed: 1x where expected");
    int32_t prev = -300000, mono = 1;
    for (int32_t k = 0; k <= 32767; k += 7) { int32_t s = rt_speed_q16(k); if (s < prev) mono = 0; prev = s; }
    check(mono, "speed: rises left to right");
    check(rt_size(0) == 882 && abs(rt_size(32767) - 44100) < 8, "size: 10 ms .. 500 ms");
    check(rt_ovl8(0) == 4 && rt_ovl8(32767) == 35, "density: 0.5x .. 4.4x");
    check(rt_spray(0) == 0 && abs(rt_spray(32767) - 66150) < 8, "spray: 0 .. 1.5 s");
    printf("laws: 1x at word %d, 0.25x at word %d\n", speed_word(1.0), speed_word(0.25));
}

static void test_roam(int len)
{
    /* the region is anchored at its start: width only moves the right end, which stops at the recording's end */
    {
        int32_t lo0, hi0, anchored = 1, grows = 1, prev = -1;
        for (int32_t w = 0; w <= 32767; w += 512) {
            rt_region(len, 8192, w, &lo0, &hi0);
            if (lo0 != rt_at(8192, len)) anchored = 0;
            if (hi0 < prev) grows = 0;
            prev = hi0;
        }
        check(anchored, "region: width keeps the start in place");
        check(grows, "region: width only moves the end rightwards");
        rt_region(len, 8192, 32767, &lo0, &hi0);
        check(hi0 == len - 1, "region: full width runs to the recording's end");
        rt_region(len, 32767, 16384, &lo0, &hi0);
        check(lo0 <= hi0 && hi0 == len - 1, "region: a start at the very end stays a valid region");
    }
    rt_roam r;
    rt_knobs k;
    /* 1x forward over the whole recording: 64 source samples a block, wrapping at the end */
    memset(&r, 0, sizeof r);
    k = knobs(0, 14044, speed_word(1.0), 12288, 0, 32767, 0);
    rt_roam_step(&r, len, &k);
    int32_t p0 = r.pos;
    rt_roam_step(&r, len, &k);
    int32_t d = r.pos - p0;
    check(abs(d - (64 << 8)) < 64 << 2, "roam: 1x moves 64 samples a block");
    int wraps = 0, inside = 1;
    for (int b = 0; b < RT_BLOCKS_PER_S * 10; b++) {
        int32_t before = r.pos;
        rt_roam_step(&r, len, &k);
        if (r.pos < before) wraps++;
        if (r.read < r.lo || r.read > r.hi) inside = 0;
    }
    check(wraps >= 2 && wraps <= 3, "roam: 1x through 4 s wraps about every 4 s");
    check(inside, "roam: the read point stays in the region without wander");
    /* backwards */
    k.speed = speed_word(-1.0);
    p0 = r.pos;
    rt_roam_step(&r, len, &k);
    d = r.pos - p0;
    check(d < 0 || d > (len / 2) << 8, "roam: left of the middle goes backwards");
    /* frozen */
    k.speed = 16384;
    p0 = r.pos;
    for (int b = 0; b < 100; b++) rt_roam_step(&r, len, &k);
    check(r.pos == p0, "roam: frozen stays put");
    /* no width: on the position */
    k.width = 0; k.speed = speed_word(2.0); k.position = 8192;
    for (int b = 0; b < 50; b++) rt_roam_step(&r, len, &k);
    check(abs(r.read - rt_at(8192, len)) <= 1, "roam: zero width sits on the position");
    /* a narrow region keeps the playhead inside it */
    k.width = 3277; k.position = 24576;
    int ok = 1;
    for (int b = 0; b < RT_BLOCKS_PER_S * 5; b++) { rt_roam_step(&r, len, &k); if (r.read < r.lo || r.read > r.hi) ok = 0; }
    check(ok, "roam: a narrow region holds the playhead");
    /* wander: jumps and turns at about the designed rates, never outside the recording */
    memset(&r, 0, sizeof r);
    r.rng = 12345;
    k = knobs(0, 14044, speed_word(0.5), 12288, 0, 32767, 32767);
    int turns = 0, jumps = 0, out = 0, dir = 1;
    rt_roam_step(&r, len, &k);
    const int secs = 120;
    for (int b = 0; b < RT_BLOCKS_PER_S * secs; b++) {
        int32_t before = r.pos;
        rt_roam_step(&r, len, &k);
        if (r.dir != dir) { turns++; dir = r.dir; }
        int32_t step = abs(r.pos - before) >> 8;
        if (step > 200 && step < len - 200) jumps++;      /* not a wrap, not a normal step */
        if (r.read < 0 || r.read >= len) out++;
    }
    printf("wander: %.2f turns/s, %.2f jumps/s (aims 0.8, 0.6; jumps include ones that land near)\n",
           (double)turns / secs, (double)jumps / secs);
    check(turns > 0.5 * secs && turns < 1.2 * secs, "wander: turns about 0.8 a second");
    check(jumps > 0.3 * secs && jumps < 0.9 * secs, "wander: jumps about 0.6 a second");
    check(!out, "wander: the read point stays inside the recording");

    /* each part alone: jumps without turns, turns without jumps, drift without either */
    {
        int tj = 0, jj = 0, td = 1;
        memset(&r, 0, sizeof r); r.rng = 99;
        k = knobs(0, 14044, speed_word(0.5), 12288, 0, 32767, 0);
        k.jumps = 32767;
        for (int b = 0; b < RT_BLOCKS_PER_S * 60; b++) {
            int32_t before = r.pos;
            rt_roam_step(&r, len, &k);
            if (r.dir != 1) td = 0;
            int32_t step = abs(r.pos - before) >> 8;
            if (step > 200 && step < len - 200) jj++;
        }
        check(td && jj > 15, "wander parts: jumps alone jump and never turn");
        memset(&r, 0, sizeof r); r.rng = 99;
        k.jumps = 0; k.turns = 32767;
        int d = 1;
        for (int b = 0; b < RT_BLOCKS_PER_S * 60; b++) {
            int32_t before = r.pos;
            rt_roam_step(&r, len, &k);
            if (r.dir != d) { tj++; d = r.dir; }
            int32_t step = abs(r.pos - before) >> 8;
            if (step > 200 && step < len - 200) jj = -1000;
        }
        check(tj > 25 && jj > 0, "wander parts: turns alone turn and never jump");
    }

    /* bounce: at 2x through a 1 s region it reverses at the ends, never wraps, and stays inside */
    {
        memset(&r, 0, sizeof r);
        k = knobs(0, 14044, speed_word(2.0), 12288, 0, 8192, 0);
        k.bounce = 1;
        int flips = 0, wrapped = 0, inside = 1, d = 1;
        rt_roam_step(&r, len, &k);
        for (int b = 0; b < RT_BLOCKS_PER_S * 10; b++) {
            int32_t before = r.pos;
            rt_roam_step(&r, len, &k);
            if (r.dir != d) { flips++; d = r.dir; }
            if (abs(r.pos - before) > (512 << 8)) wrapped++;
            if (r.read < r.lo || r.read > r.hi) inside = 0;
        }
        printf("bounce: %d reversals in 10 s at 2x over a %.2f s region\n", flips, (r.hi - r.lo) / 44100.0);
        check(flips >= 18 && flips <= 22, "bounce: reverses at each end (about 20 in 10 s)");
        check(!wrapped, "bounce: never wraps");
        check(inside, "bounce: stays inside the region");
        /* a region narrower than one block's step still behaves */
        k.width = 40; k.speed = 32767;
        inside = 1;
        for (int b = 0; b < 2000; b++) { rt_roam_step(&r, len, &k); if (r.read < r.lo || r.read > r.hi) inside = 0; }
        check(inside, "bounce: a tiny region at 4x stays inside");
    }

    /* chaos: the jump rate swings between seconds, about the same on average */
    {
        int per[60] = { 0 }, total = 0;
        memset(&r, 0, sizeof r); r.rng = 4242;
        k = knobs(0, 14044, speed_word(0.5), 12288, 0, 32767, 0);
        k.jumps = 32767; k.chaos = 32767;
        for (int s = 0; s < 60; s++)
            for (int b = 0; b < RT_BLOCKS_PER_S; b++) {
                int32_t before = r.pos;
                rt_roam_step(&r, len, &k);
                int32_t step = abs(r.pos - before) >> 8;
                if (step > 200 && step < len - 200) { per[s]++; total++; }
            }
        int lo = 1000, hi = 0;
        for (int s = 0; s < 60; s += 2) { int n = per[s] + per[s + 1]; if (n < lo) lo = n; if (n > hi) hi = n; }
        printf("chaos: %.2f jumps/s, from %d to %d per 2 s\n", total / 60.0, lo, hi);
        check(total > 0.4 * 60 && total < 0.85 * 60, "chaos: about the same average rate (0.6 a second)");
        check(hi - lo >= 4, "chaos: bursts and lulls");
    }
}

/* render secs of one voice at a pitch rate; returns peak, fills rms; checks every grain stays inside */
static int voice_run(int len, rt_knobs k, uint32_t rate, double secs, int16_t *dst, double *rms, int *outside)
{
    static rt_voice v;
    static rt_roam r;
    memset(&v, 0, sizeof v);
    memset(&r, 0, sizeof r);
    r.rng = 777;
    rt_voice_note(&v, 0);
    int blocks = (int)(secs * RT_BLOCKS_PER_S), peak = 0;
    double sum = 0;
    int16_t buf[RT_BLOCK];
    for (int b = 0; b < blocks; b++) {
        int32_t read = rt_roam_step(&r, len, &k);
        rt_voice_render(&v, rec, len, read, &k, rate, 32767, buf, sine);
        for (int i = 0; i < RT_GRAINS; i++)
            if (v.g[i].winc && (v.g[i].pos >> RT_FRAC) >= (uint32_t)len) (*outside)++;
        for (int n = 0; n < RT_BLOCK; n++) {
            int a = abs(buf[n]);
            if (a > peak) peak = a;
            sum += (double)buf[n] * buf[n];
            if (dst) dst[b * RT_BLOCK + n] = buf[n];
        }
    }
    *rms = sqrt(sum / ((double)blocks * RT_BLOCK));
    return peak;
}

/* the main frequency of n samples at rate, from zero crossings */
static double crossing_hz(const int16_t *s, int n, double rate)
{
    int c = 0;
    for (int i = 1; i < n; i++) if ((s[i - 1] < 0) != (s[i] < 0)) c++;
    return c / 2.0 * rate / n;
}

static void test_grains(int len, const char *dir)
{
    static int16_t out[OUT_RATE * 12];
    double rms;
    int outside = 0;
    /* a frozen texture on one note, a pure tone: C4 should play it at its own pitch */
    for (int i = 0; i < len; i++) rec[i] = (int16_t)(sin(2 * M_PI * 220.0 * i / SRC_RATE) * 12000);
    rt_knobs k = knobs(16384, 20000, 16384, 12288, 0, 0, 0);
    voice_run(len, k, C4_RATE, 2.0, out, &rms, &outside);
    double hz = crossing_hz(out + OUT_RATE / 2, OUT_RATE, OUT_RATE);
    printf("pitch: a 220 Hz recording plays at %.1f Hz at C4\n", hz);
    check(fabs(hz - 220.0) < 3.0, "grains: C4 plays the recording at its own pitch");
    voice_run(len, k, C4_RATE * 2, 1.0, out, &rms, &outside);
    hz = crossing_hz(out + OUT_RATE / 4, OUT_RATE / 2, OUT_RATE);
    check(fabs(hz - 440.0) < 6.0, "grains: an octave up doubles the pitch");

    /* full density, the biggest grains, full-scale sine: no more than a sliver clipped */
    for (int i = 0; i < len; i++) rec[i] = (int16_t)(sin(2 * M_PI * 220.0 * i / SRC_RATE) * 32000);
    k = knobs(16384, 32767, speed_word(0.5), 32767, 32767, 32767, 32767);
    int peak = voice_run(len, k, C4_RATE, 6.0, out, &rms, &outside);
    int clipped = 0;
    for (int i = 0; i < 6 * OUT_RATE; i++) if (abs(out[i]) >= 32767) clipped++;
    printf("loudness: full density peak %d, rms %.0f, %d clipped\n", peak, rms, clipped);
    check(clipped < 6 * OUT_RATE / 1000, "grains: full density barely clips a full-scale recording");

    /* every corner of every knob: nothing outside the recording, nothing silent that should sound */
    make_rec();
    static const int32_t corners[3] = { 0, 16384, 32767 };
    int silent = 0;
    for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) for (int c = 0; c < 3; c++) for (int d = 0; d < 3; d++)
        for (int e = 0; e < 3; e++) {
            k = knobs(corners[a], corners[b], corners[c], corners[d], corners[e], corners[(a + e) % 3], corners[(b + c) % 3]);
            voice_run(len, k, C4_RATE + (uint32_t)(a * 300), 0.5, 0, &rms, &outside);
            if (rms < 50) silent++;
        }
    check(!outside, "grains: never read outside the recording");
    printf("corners: %d of 243 nearly silent (the recording has quiet tails)\n", silent);
    check(silent < 40, "grains: knob corners sound");

    /* nothing recorded: silence */
    {
        rt_voice v; memset(&v, 0, sizeof v);
        int16_t buf[RT_BLOCK];
        k = knobs(16384, 16384, 16384, 16384, 0, 16384, 0);
        rt_voice_render(&v, rec, 100, 0, &k, C4_RATE, 32767, buf, sine);
        int z = 1;
        for (int n = 0; n < RT_BLOCK; n++) if (buf[n]) z = 0;
        check(z, "grains: silence with nothing recorded");
        rt_voice_render(&v, 0, 0, 0, &k, C4_RATE, 32767, buf, sine);
    }

    if (!dir) return;
    /* the WAVs: the melody recording, roamed */
    make_rec();
    k = knobs(8192, 14044, 16384, 16384, 3000, 0, 0);
    voice_run(len, k, C4_RATE, 6.0, out, &rms, &outside);
    wav(dir, "rt_frozen.wav", out, 6 * OUT_RATE, OUT_RATE);
    k = knobs(16384, 14044, speed_word(0.25), 12288, 8460, 32767, 0);
    voice_run(len, k, C4_RATE, 12.0, out, &rms, &outside);
    wav(dir, "rt_stretch_quarter.wav", out, 12 * OUT_RATE, OUT_RATE);
    k = knobs(16384, 9000, speed_word(-1.0), 20000, 2000, 32767, 0);
    voice_run(len, k, C4_RATE, 8.0, out, &rms, &outside);
    wav(dir, "rt_backwards.wav", out, 8 * OUT_RATE, OUT_RATE);
    k = knobs(16384, 14044, speed_word(0.5), 16384, 8460, 32767, 26000);
    voice_run(len, k, C4_RATE, 12.0, out, &rms, &outside);
    wav(dir, "rt_wander.wav", out, 12 * OUT_RATE, OUT_RATE);
    k = knobs(16384, 32767, speed_word(0.1), 32767, 20000, 16384, 12000);
    voice_run(len, k, C4_RATE, 12.0, out, &rms, &outside);
    wav(dir, "rt_cloud.wav", out, 12 * OUT_RATE, OUT_RATE);
}

int main(int argc, char **argv)
{
    for (int i = 0; i < 8192; i++) sine[i] = (int16_t)lrint(sin(2 * M_PI * i / 8192) * 32767);
    int len = make_rec();
    test_laws();
    test_roam(len);
    test_grains(len, argc > 1 ? argv[1] : 0);
    if (failures) { printf("%d FAILED\n", failures); return 1; }
    printf("all passed\n");
    return 0;
}
