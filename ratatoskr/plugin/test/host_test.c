/* Host test of Ratatoskr's DSP (rt_dsp.h): the control laws, the route, the six motions, phrase restarts, the grain
 * shapes, grains kept inside the recording (scatter, short recordings, high keys), pitch, loudness at 4x overlap,
 * silence with nothing recorded, and WAVs to hear.
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
static rt_shapes shapes;
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

/* a recording to move through: 16 notes of a minor scale, 250 ms each, a few harmonics, plucked */
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

/* the knob word in the middle of a motion's or a shape's zone */
static int32_t zone(int i, int n) { return (int32_t)(((2 * i + 1) * 32768L) / (2 * n)); }

/* defaults: position 50%, 50 ms, no scatter, hold, the whole recording, hann, the middle speed, 2x */
static rt_knobs knobs(void)
{
    rt_knobs k = { 16384, 15400, 0, zone(RT_HOLD, RT_MOTIONS), 32767, zone(RT_HANN, RT_SHAPES), 16384, 0 };
    return k;
}

static void test_laws(void)
{
    check(abs(rt_size(0) - 1058) <= 2 && abs(rt_size(32767) - 22050) < 80, "size: 12 .. 250 ms");
    check(abs(rt_size(15400) - 4410) < 30, "size: 50 ms by default");
    int mono = 1;
    for (int32_t k = 7, p = rt_size(0); k <= 32767; k += 7) { int32_t s = rt_size(k); if (s < p) mono = 0; p = s; }
    check(mono, "size: rises");
    check(rt_scatter(0) == 0 && abs(rt_scatter(32767) - 22050) < 4, "scatter: 0 .. 500 ms");
    check(rt_scatter(16384) < 6000, "scatter: the low end gets most of the turn");
    for (int m = 0; m < RT_MOTIONS; m++) check(rt_motion(zone(m, RT_MOTIONS)) == m, "motion: six zones");
    check(rt_motion(0) == RT_HOLD && rt_motion(32767) == RT_JUMP, "motion: hold .. jump");
    for (int s = 0; s < RT_SHAPES; s++) check(rt_shape(zone(s, RT_SHAPES)) == s, "shape: four zones");
    check(rt_ovl8(0) == 16, "overlap: no record is 2x");
    int32_t lo = 99, hi = 0;
    for (int s = 0; s < RT_OVL_STEPS; s++) { int32_t o = rt_ovl8(s); if (o < lo) lo = o; if (o > hi) hi = o; }
    check(lo == 4 && hi == 32, "overlap: 0.5x .. 4x");
    double t0 = rt_travel_q16(0) / 65536.0, tm = rt_travel_q16(16384) / 65536.0, t1 = rt_travel_q16(32767) / 65536.0;
    check(fabs(t0 - 0.05) < 0.002 && fabs(tm - 0.25) < 0.002 && fabs(t1 - 2.0) < 0.02, "speed: 0.05x .. 0.25x .. 2x");
    double e0 = rt_events_q16(0) / 65536.0, em = rt_events_q16(16384) / 65536.0, e1 = rt_events_q16(32767) / 65536.0;
    check(fabs(e0 - 0.1) < 0.003 && fabs(em - 1.0) < 0.005 && fabs(e1 - 4.0) < 0.04, "speed: 0.1 .. 1 .. 4 a second");
    double worst = 0;
    for (int x = -16 * 4096; x < 14 * 4096; x += 37) {
        double want = pow(2.0, x / 4096.0) * 65536.0, got = rt_exp2(x);
        if (want > 4096) { double e = fabs(got / want - 1); if (e > worst) worst = e; }
    }
    check(worst < 0.004, "exp2 within 0.4%");
}

static void test_route(void)
{
    const int32_t len = 100000;
    int32_t lo, hi;
    rt_route(len, 16384, 32767, &lo, &hi);
    check(lo <= 4 && hi >= len - 10, "route: the whole recording at 100%");
    rt_route(len, 16384, 0, &lo, &hi);                         /* 5% around the middle */
    check(abs((hi - lo) - len / 20) < 20 && abs((lo + hi) / 2 - len / 2) < 20, "route: 5% centred on the position");
    rt_route(len, 0, 8192, &lo, &hi);
    int32_t w = hi - lo;
    check(lo == 0 && w > len / 4, "route: slid inward at the start, width kept");
    rt_route(len, 32767, 8192, &lo, &hi);
    check(hi == len - 1 && abs((hi - lo) - w) <= 1, "route: slid inward at the end, width kept");
}

/* run the playhead for n blocks; returns its final position (source samples) */
static int32_t run(rt_roam *r, int32_t len, const rt_knobs *k, int n, int restart_first)
{
    int32_t x = 0;
    for (int i = 0; i < n; i++) x = rt_roam_step(r, len, k, restart_first && i == 0);
    return x;
}

static void test_motions(void)
{
    const int32_t len = make_rec();
    rt_roam r; memset(&r, 0, sizeof r); rt_roam_init(&r);
    rt_knobs k = knobs();
    int32_t home = rt_at(k.position, len);
    check(run(&r, len, &k, 689, 1) == home, "hold: stays at the position");

    k.motion = zone(RT_FORWARD, RT_MOTIONS);
    int32_t a = run(&r, len, &k, 1, 1), b = run(&r, len, &k, 689, 0);
    check(a > home - 64 && a <= home + 32 && abs((b - a) - SRC_RATE / 4) < 200, "forward: 0.25 s a second from the position");
    k.speed = 32767;
    int32_t c = run(&r, len, &k, 689, 0), moved = c - b;
    if (moved < 0) moved += len - 1;
    check(abs(moved - 2 * SRC_RATE) < 400, "forward: 2x at full speed, wrapping to the roots");

    k.speed = 16384; k.motion = zone(RT_REVERSE, RT_MOTIONS);
    a = run(&r, len, &k, 1, 1); b = run(&r, len, &k, 689, 0);
    check(abs((a - b) - SRC_RATE / 4) < 200, "reverse: down 0.25 s a second");

    /* bounce in a 10% range at 2x: about 0.4 s of range a second, so reversals, never leaving it */
    k.motion = zone(RT_BOUNCE, RT_MOTIONS); k.range = 1700; k.speed = 32767;
    int32_t lo, hi; rt_route(len, k.position, k.range, &lo, &hi);
    int out = 0, turns = 0, last = 0, prev = run(&r, len, &k, 1, 1);
    for (int i = 0; i < 689 * 4; i++) {
        int32_t x = rt_roam_step(&r, len, &k, 0);
        if (x < lo || x > hi) out = 1;
        int d = x > prev ? 1 : (x < prev ? -1 : 0);
        if (d && last && d != last) turns++;
        if (d) last = d;
        prev = x;
    }
    check(!out, "bounce: stays in the range");
    check(turns >= 15 && turns <= 25, "bounce: reverses at each end");

    /* wander: within 5% of the span of where it was a second ago, moving smoothly */
    k = knobs(); k.motion = zone(RT_WANDER, RT_MOTIONS);
    prev = run(&r, len, &k, 1, 1);
    int32_t far = 0, jump = 0, at = prev, moves = 0;
    for (int i = 0; i < 689 * 10; i++) {
        int32_t x = rt_roam_step(&r, len, &k, 0);
        if (abs(x - prev) > jump) jump = abs(x - prev);
        if (i % 689 == 688) { if (abs(x - at) > far) far = abs(x - at); if (x != at) moves++; at = x; }
        prev = x;
    }
    check(far <= len / 20 + 8 && moves >= 7, "wander: about one new target a second, within 5%");
    check(jump < 64, "wander: glides, no leaps");

    /* jump: about one a second at the middle speed, anywhere in the range, holding between */
    k = knobs(); k.motion = zone(RT_JUMP, RT_MOTIONS);
    prev = run(&r, len, &k, 1, 1);
    check(prev == home, "jump: a phrase starts at the position");
    int jumps = 0;
    for (int i = 0; i < 689 * 20; i++) { int32_t x = rt_roam_step(&r, len, &k, 0); if (x != prev) jumps++; prev = x; }
    check(jumps >= 17 && jumps <= 21, "jump: one a second");

    /* a new phrase starts at the position; turning the position moves it at once; a new motion goes on from where
     * it is */
    k = knobs(); k.motion = zone(RT_FORWARD, RT_MOTIONS);
    run(&r, len, &k, 689, 1);
    check(run(&r, len, &k, 1, 1) <= home + 32, "restart: back to the position");
    run(&r, len, &k, 689, 0);
    k.position = 8192;
    check(abs(rt_roam_step(&r, len, &k, 0) - rt_at(8192, len)) <= 32, "position: turning it moves the playhead");
    run(&r, len, &k, 689, 0);
    int32_t here = r.read;
    k.motion = zone(RT_HOLD, RT_MOTIONS);
    check(abs(rt_roam_step(&r, len, &k, 0) - here) <= 1, "motion: a change goes on from where it is");
}

static void test_shapes(void)
{
    int ok = 1;
    for (int s = 0; s < RT_SHAPES; s++) {
        int peak = 0, at = 0;
        for (int i = 0; i <= 256; i++) if (shapes[s][i] > peak) { peak = shapes[s][i]; at = i; }
        if (shapes[s][0] != 0 || shapes[s][256] != 0 || peak < 32000) ok = 0;
        if (s == RT_HANN && abs(at - 128) > 1) ok = 0;
        if (s == RT_TRIANGLE && abs(at - 128) > 1) ok = 0;
        if (s == RT_PLUCK && abs(at - 38) > 1) ok = 0;
        if (s == RT_SWELL && abs(at - 218) > 1) ok = 0;
    }
    check(ok, "shapes: from 0 to their peaks (middle, middle, 15%, 85%) and back to 0");
    check(abs(shapes[RT_HANN][64] - 16384) < 200, "hann: half way up at a quarter");
}

/* render n blocks of one voice; returns the peak */
static int render(rt_voice *v, rt_roam *r, const int16_t *data, int32_t len, const rt_knobs *k, uint32_t rate,
                  int16_t *out, int n, rt_flashes *fl)
{
    rt_launch l; rt_launch_of(k, shapes, &l);
    int peak = 0;
    for (int b = 0; b < n; b++) {
        int32_t read = rt_roam_step(r, len, k, b == 0);
        rt_voice_render(v, data, len, read, &l, rate, 32767, out + b * RT_BLOCK, fl, (uint32_t)b);
        for (int i = 0; i < RT_BLOCK; i++) if (abs(out[b * RT_BLOCK + i]) > peak) peak = abs(out[b * RT_BLOCK + i]);
    }
    return peak;
}

static int32_t zero_crossings(const int16_t *s, int n)
{
    int32_t z = 0;
    for (int i = 1; i < n; i++) if ((s[i - 1] < 0) != (s[i] < 0)) z++;
    return z;
}

static void test_grains(const char *dir)
{
    const int32_t len = make_rec();
    static int16_t out[OUT_RATE * 8];
    static rt_flashes fl;
    rt_voice v; rt_roam r;
    const int blocks = OUT_RATE * 4 / RT_BLOCK;

    /* a steady tone: hold on one sine at C4 and an octave up */
    static int16_t tone[SRC_RATE * 2];
    for (int i = 0; i < SRC_RATE * 2; i++) tone[i] = (int16_t)(12000 * sin(2 * M_PI * 441.0 * i / SRC_RATE));
    rt_knobs k = knobs();
    memset(&v, 0, sizeof v); memset(&r, 0, sizeof r); rt_roam_init(&r); rt_voice_note(&v, 0);
    render(&v, &r, tone, SRC_RATE * 2, &k, C4_RATE, out, 689, 0);
    double hz = zero_crossings(out + OUT_RATE / 4, OUT_RATE / 2) / 2.0 * 2;
    check(fabs(hz - 441) < 12, "pitch: C4 plays the recording's own pitch");
    memset(&v, 0, sizeof v); rt_voice_note(&v, 0);
    render(&v, &r, tone, SRC_RATE * 2, &k, C4_RATE * 2, out, 689, 0);
    hz = zero_crossings(out + OUT_RATE / 4, OUT_RATE / 2) / 2.0 * 2;
    check(fabs(hz - 882) < 20, "pitch: an octave up");

    /* every shape and overlap at full scale: no clipping */
    static int16_t loud[SRC_RATE * 2];
    for (int i = 0; i < SRC_RATE * 2; i++) loud[i] = (int16_t)(32000 * sin(2 * M_PI * 110.0 * i / SRC_RATE));
    int clip = 0, quiet = 0;
    for (int s = 0; s < RT_SHAPES; s++) for (int o = 0; o < RT_OVL_STEPS; o++) {
        k = knobs(); k.shape = zone(s, RT_SHAPES); k.overlap = o; k.size = 32767;
        memset(&v, 0, sizeof v); memset(&r, 0, sizeof r); rt_roam_init(&r); rt_voice_note(&v, 0);
        int p = render(&v, &r, loud, SRC_RATE * 2, &k, C4_RATE, out, 689, 0);
        if (p >= 32767) clip++;
        if (p < 8000) quiet++;
    }
    check(!clip, "loudness: no clipping at any shape and overlap");
    check(!quiet, "loudness: every shape and overlap is heard");

    /* grains stay whole inside the recording: wide scatter at the ends, a short recording, a high key */
    int bad = 0;
    for (int t = 0; t < 4; t++) {
        int32_t l = t == 2 ? 2000 : len;
        k = knobs(); k.scatter = 32767; k.position = (t & 1) ? 32767 : 0; k.size = 32767;
        uint32_t rate = t == 3 ? C4_RATE * 8 : C4_RATE;
        memset(&v, 0, sizeof v); memset(&r, 0, sizeof r); rt_roam_init(&r); rt_voice_note(&v, 0);
        memset(&fl, 0, sizeof fl);
        rt_launch la; rt_launch_of(&k, shapes, &la);
        for (int b = 0; b < 2000; b++) {
            int32_t read = rt_roam_step(&r, l, &k, b == 0);
            rt_voice_render(&v, rec, l, read, &la, rate, 32767, out, &fl, (uint32_t)b);
            for (int g = 0; g < RT_GRAINS; g++) {
                if (!v.g[g].winc) continue;
                uint32_t left = (0x80000000u - v.g[g].wph) / v.g[g].winc;
                uint64_t end = (uint64_t)v.g[g].pos + (uint64_t)left * v.g[g].rate;
                if ((end >> RT_FRAC) >= (uint64_t)(l - 1)) bad++;
            }
        }
        if (fl.n == 0) bad++;
    }
    check(!bad, "grains: whole inside the recording (scatter at the ends, short, high)");

    /* hold + scatter: flashes spread around a still playhead */
    k = knobs(); k.scatter = 20000;
    memset(&v, 0, sizeof v); memset(&r, 0, sizeof r); rt_roam_init(&r); rt_voice_note(&v, 0); memset(&fl, 0, sizeof fl);
    render(&v, &r, rec, len, &k, C4_RATE, out, 689, &fl);
    int32_t mn = len, mx = 0;
    for (uint32_t i = 0; i < fl.n && i < RT_FLASHES; i++) { if (fl.at[i] < mn) mn = fl.at[i]; if (fl.at[i] > mx) mx = fl.at[i]; }
    check(fl.n > 30 && fl.n < 50 && mx - mn > rt_scatter(20000), "hold + scatter: about 40 launches a second, spread out");

    memset(&v, 0, sizeof v); rt_voice_note(&v, 0);
    check(render(&v, &r, 0, 0, &k, C4_RATE, out, 10, 0) == 0, "silence with nothing recorded");

    if (dir) {
        const char *names[3] = { "rt_hold.wav", "rt_wander.wav", "rt_jump.wav" };
        const int modes[3] = { RT_HOLD, RT_WANDER, RT_JUMP };
        for (int m = 0; m < 3; m++) {
            k = knobs(); k.motion = zone(modes[m], RT_MOTIONS); k.scatter = m == 0 ? 12000 : 0;
            memset(&v, 0, sizeof v); memset(&r, 0, sizeof r); rt_roam_init(&r); rt_voice_note(&v, 0);
            render(&v, &r, rec, len, &k, C4_RATE, out, blocks, 0);
            wav(dir, names[m], out, blocks * RT_BLOCK, OUT_RATE);
        }
        const char *sn[RT_SHAPES] = { "rt_shape_hann.wav", "rt_shape_triangle.wav", "rt_shape_pluck.wav", "rt_shape_swell.wav" };
        for (int s = 0; s < RT_SHAPES; s++) {
            k = knobs(); k.shape = zone(s, RT_SHAPES); k.size = 26000; k.overlap = 5;   /* 0.5x: hear each grain */
            k.motion = zone(RT_FORWARD, RT_MOTIONS);
            memset(&v, 0, sizeof v); memset(&r, 0, sizeof r); rt_roam_init(&r); rt_voice_note(&v, 0);
            render(&v, &r, rec, len, &k, C4_RATE, out, blocks / 2, 0);
            wav(dir, sn[s], out, blocks / 2 * RT_BLOCK, OUT_RATE);
        }
    }
}

int main(int argc, char **argv)
{
    for (int i = 0; i < 8192; i++) sine[i] = (int16_t)lrint(32767 * sin(2 * M_PI * i / 8192));
    rt_shapes_init(shapes, sine);
    test_laws();
    test_route();
    test_motions();
    test_shapes();
    test_grains(argc > 1 ? argv[1] : 0);
    if (failures) { printf("%d failed\n", failures); return 1; }
    printf("all passed\n");
    return 0;
}
