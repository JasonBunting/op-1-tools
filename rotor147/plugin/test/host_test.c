/* Host test of rotor147's DSP (rc_dsp.h, the same code the plugin runs) and the listening renders.
 * Checks: silence and decay to exact zero, extreme inputs, mix 0 bit-exact and full wet without dry, rotor speeds and
 * ramp times (interrupted too), brake at several angles, state isolation of the three positions, the mono-sum master
 * (opposite polarity cancels), control sweeps, the drive's levels, near/far loudness and the mono fold-down.
 * Then WAVs under <dir>/rotor147/ (raw/ and level-matched matched/), with settings.txt.
 *   gcc -O2 -Wall -fsanitize=address,undefined -DRC_CHECK -I rotor147/plugin rotor147/plugin/test/host_test.c -lm \
 *       -o /tmp/rc_test && /tmp/rc_test build
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef RC_CHECK
#define RC_ASSERT(x) do { if (!(x)) { printf("ASSERT %s (%s:%d)\n", #x, __FILE__, __LINE__); abort(); } } while (0)
#endif
#include "rc_dsp.h"

#define FS 44100
#define BRAKE 5461
#define SLOW  16384
#define FAST  27306
static int16_t sine[8192];
static int failures;
static const char *outdir;

static void check(int ok, const char *what) { if (!ok) { printf("FAIL: %s\n", what); failures++; } }
static double db(double r) { return 20 * log10(r > 1e-12 ? r : 1e-12); }

/* ---- knob plans: a callback gives the four 0..32767 values for each block ---- */
typedef struct { int32_t a[7], b[7]; int from, to; } ramp;    /* a until block from, b after block to, linear between */
static const int32_t P2[3] = RC_P2_DEFAULTS;                  /* page 2: speed, acceleration, contour */
static void p2(ramp *r) { for (int i = 0; i < 3; i++) r->a[4 + i] = r->b[4 + i] = P2[i]; }
static void ramp_q(const ramp *p, int blk, int32_t q[7])
{
    for (int i = 0; i < 7; i++) {
        if (blk <= p->from) q[i] = p->a[i];
        else if (blk >= p->to) q[i] = p->b[i];
        else q[i] = p->a[i] + (int32_t)((int64_t)(p->b[i] - p->a[i]) * (blk - p->from) / (p->to - p->from));
    }
}
typedef struct { double t; int32_t q[7]; } step;                /* from time t on; page 2 all 0 = the defaults */
typedef struct { const step *s; int n; } plan;
static void plan_q(const plan *p, int blk, int32_t q[7])
{
    const double t = (double)blk * RC_N / FS;
    int k = 0;
    while (k + 1 < p->n && p->s[k + 1].t <= t) k++;
    memcpy(q, p->s[k].q, sizeof p->s[k].q);
    if (!q[4] && !q[5] && !q[6]) for (int i = 0; i < 3; i++) q[4 + i] = P2[i];
}
static ramp fixed(int32_t m, int32_t g, int32_t d, int32_t x) { ramp r = { { m, g, d, x }, { m, g, d, x }, 0, 1 }; p2(&r); return r; }

/* in place; n is a multiple of RC_N; stereo: l and r, mono: l only */
static void run_ramp(rc_state *s, int16_t *l, int16_t *r, int n, const ramp *p, int blk0)
{
    for (int b = 0; b < n / RC_N; b++) {
        int32_t q[7];
        ramp_q(p, blk0 + b, q);
        rc_control(s, q);
        if (r) rc_render_stereo(s, l + b * RC_N, r + b * RC_N, sine); else rc_render_mono(s, l + b * RC_N, sine);
    }
}
static void run_plan(rc_state *s, int16_t *l, int16_t *r, int n, const plan *p)
{
    for (int b = 0; b < n / RC_N; b++) {
        int32_t q[7];
        plan_q(p, b, q);
        rc_control(s, q);
        if (r) rc_render_stereo(s, l + b * RC_N, r + b * RC_N, sine); else rc_render_mono(s, l + b * RC_N, sine);
    }
}
static void blocks(rc_state *s, int nblk, int32_t mode, int stereo)     /* silence, full wet */
{
    int16_t l[RC_N], r[RC_N];
    ramp p = fixed(mode, 8192, 0, 32767);
    for (int b = 0; b < nblk; b++) { memset(l, 0, sizeof l); memset(r, 0, sizeof r); run_ramp(s, l, stereo ? r : 0, RC_N, &p, 0); }
}

static double rms(const int16_t *x, int n) { double a = 0; for (int i = 0; i < n; i++) a += (double)x[i] * x[i]; return sqrt(a / (n ? n : 1)); }
static int16_t *buf(int n) { int16_t *b = calloc((size_t)n, 2); if (!b) { printf("out of memory\n"); exit(1); } return b; }
static int16_t *copy(const int16_t *x, int n) { int16_t *b = buf(n); memcpy(b, x, (size_t)n * 2); return b; }
static int round_up(int n) { return (n + RC_N - 1) / RC_N * RC_N; }

/* ---- sources (host floating point; only the DSP under test is integer) ---- */
static uint32_t rng = 1;
static double noise(void) { rng = rng * 1664525u + 1013904223u; return ((int32_t)rng) / 2147483648.0; }
/* tonewheel-like organ: drawbars 16', 8', 5 1/3', 4' (888800000-ish), a short key click, chords C, Am, F, G (3 s each) */
static int16_t *organ(int n, double amp, int high)
{
    static const double chords[4][3] = { { 261.63, 329.63, 392.00 }, { 220.00, 261.63, 329.63 },
                                         { 174.61, 220.00, 261.63 }, { 196.00, 246.94, 293.66 } };
    int16_t *b = buf(n);
    for (int i = 0; i < n; i++) {
        const int c = (i / (3 * FS)) % 4, t = i % (3 * FS);
        double s = 0;
        if (high) {                                           /* one high note: C6 with its upper drawbars */
            const double f = 1046.5, ph = 2 * M_PI * f * i / FS;
            s = sin(ph) + 0.8 * sin(2 * ph) + 0.6 * sin(3 * ph) + 0.5 * sin(4 * ph);
            s *= 0.3;
        } else {
            for (int v = 0; v < 3; v++) {
                const double f = chords[c][v], ph = 2 * M_PI * f * i / FS;
                s += sin(ph / 2) + sin(ph) + sin(1.5 * ph) + 0.7 * sin(2 * ph);
            }
            s /= 10.0;
            if (t < FS / 200) s += 0.3 * noise() * (1 - t / (FS / 200.0));
        }
        const double env = t < 200 ? t / 200.0 : 1;
        b[i] = (int16_t)lrint(32767 * amp * s * env);
    }
    return b;
}
/* sustained synth chord: Cm9 of four slightly detuned saws per note through a gentle one-pole low-pass */
static int16_t *synth(int n, double amp)
{
    static const double notes[5] = { 130.81, 155.56, 196.00, 233.08, 293.66 };
    double ph[5][4] = { { 0 } }, lp = 0;
    int16_t *b = buf(n);
    for (int i = 0; i < n; i++) {
        double s = 0;
        for (int v = 0; v < 5; v++)
            for (int d = 0; d < 4; d++) {
                ph[v][d] += notes[v] * (1 + (d - 1.5) * 0.003) / FS;
                ph[v][d] -= floor(ph[v][d]);
                s += 2 * ph[v][d] - 1;
            }
        s /= 20.0;
        lp += 0.25 * (s - lp);
        const double env = i < FS / 20 ? i / (FS / 20.0) : 1;
        b[i] = (int16_t)lrint(32767 * amp * 2.2 * lp * env);
    }
    return b;
}

/* ---- WAVs ---- */
static void wav(const char *sub, const char *name, const int16_t *l, const int16_t *r, int n, double gain)
{
    char path[600];
    snprintf(path, sizeof path, "%s/rotor147/%s/%s", outdir, sub, name);
    FILE *f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); failures++; return; }
    const uint16_t ch = r ? 2 : 1, pcm = 1, bits = 16, ba = (uint16_t)(2 * ch);
    const uint32_t rate = FS, br = FS * ba, d = (uint32_t)n * ba, sz = 36 + d, fl = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&sz, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fl, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&d, 4, 1, f);
    for (int i = 0; i < n; i++) {
        int16_t v[2];
        for (int c = 0; c < ch; c++) {
            const double x = (c ? r[i] : l[i]) * gain;
            v[c] = (int16_t)(x > 32767 ? 32767 : (x < -32768 ? -32768 : lrint(x)));
        }
        fwrite(v, 2, ch, f);
    }
    fclose(f);
}

/* ==== tests ==== */
static void t_silence(void)
{
    rc_state s; rc_init(&s);
    const int n = FS * 4 / RC_N * RC_N;
    int16_t *l = buf(n), *r = buf(n);
    ramp p = fixed(FAST, 16384, 32767, 32767);
    run_ramp(&s, l, r, n, &p, 0);
    int nz = 0;
    for (int i = 0; i < n; i++) nz |= l[i] | r[i];
    check(!nz, "silence in, silence out (stereo, full drive, fast)");
    /* loud noise, then silence: the tail must be exact zeros (no fixed-point limit cycles), mono and stereo */
    for (int st = 0; st < 2; st++) {
        rc_init(&s);
        for (int i = 0; i < n; i++) { l[i] = i < FS ? (int16_t)(30000 * noise()) : 0; r[i] = i < FS ? (int16_t)(30000 * noise()) : 0; }
        run_ramp(&s, l, st ? r : 0, n, &p, 0);
        nz = 0;
        for (int i = n - FS; i < n; i++) nz |= l[i] | (st ? r[i] : 0);
        check(!nz, st ? "stereo: the last second after noise is exact zeros" : "mono: the last second after noise is exact zeros");
    }
    free(l); free(r);
}

static void t_extremes(void)
{
    /* each input at every mode, mic and drive corner: nothing may fault (sanitizers, RC_ASSERT on every delay read) */
    const int n = round_up(FS * 2);
    int16_t *l = buf(n), *r = buf(n);
    const int32_t modes[3] = { BRAKE, SLOW, FAST }, corner[2] = { 0, 32767 };
    int32_t peak = 0;
    for (int kind = 0; kind < 5; kind++)
        for (int m = 0; m < 3; m++)
            for (int g = 0; g < 2; g++)
                for (int d = 0; d < 2; d++)
                    for (int st = 0; st < 2; st++) {
                        rc_state s; rc_init(&s);
                        for (int i = 0; i < n; i++) {
                            int v = 0;
                            switch (kind) {
                            case 0: v = i == 0 ? 32767 : 0; break;                    /* impulse */
                            case 1: v = 32767; break;                                 /* positive full-scale DC */
                            case 2: v = -32768; break;                                /* negative full-scale DC */
                            case 3: v = (i / 50) & 1 ? 32767 : -32768; break;         /* full-scale square edges */
                            case 4: v = (int)(32767 * noise()); break;                /* full-scale noise */
                            }
                            l[i] = (int16_t)v; r[i] = (int16_t)(kind == 4 ? (int)(32767 * noise()) : v);
                        }
                        ramp p = fixed(modes[m], corner[g], corner[d], 32767);
                        run_ramp(&s, l, st ? r : 0, n, &p, 0);
                        for (int i = 0; i < n; i++) { int a = abs(l[i]); if (a > peak) peak = a; }
                    }
    printf("extremes: 120 runs, peak output %d\n", peak);
    free(l); free(r);
}

static void t_mix(void)
{
    const int n = round_up(FS);
    int16_t *l = buf(n), *r = buf(n);
    for (int i = 0; i < n; i++) { l[i] = (int16_t)(20000 * noise()); r[i] = (int16_t)(20000 * noise()); }
    int16_t *l0 = copy(l, n), *r0 = copy(r, n);
    rc_state s; rc_init(&s);
    ramp p = fixed(FAST, 16384, 32767, 0);
    run_ramp(&s, l, r, n, &p, 0);
    check(!memcmp(l, l0, (size_t)n * 2) && !memcmp(r, r0, (size_t)n * 2), "mix 0 stereo is bit-exact dry, no swap or crosstalk");
    memcpy(l, l0, (size_t)n * 2);
    rc_init(&s);
    run_ramp(&s, l, 0, n, &p, 0);
    check(!memcmp(l, l0, (size_t)n * 2), "mix 0 mono is bit-exact dry");
    /* left only, mix 0: the right stays silent */
    memcpy(l, l0, (size_t)n * 2); memset(r, 0, (size_t)n * 2);
    rc_init(&s);
    run_ramp(&s, l, r, n, &p, 0);
    int nz = 0;
    for (int i = 0; i < n; i++) nz |= r[i];
    check(!nz && !memcmp(l, l0, (size_t)n * 2), "mix 0, left-only input: right stays zero, left exact");
    /* mix full then back to 0: exact again once the 23 ms smoothing settles (well inside 0.5 s) */
    memcpy(l, l0, (size_t)n * 2); memcpy(r, r0, (size_t)n * 2);
    rc_init(&s);
    ramp back = { { FAST, 16384, 0, 32767 }, { FAST, 16384, 0, 0 }, 100, 100 };
    p2(&back);
    run_ramp(&s, l, r, n, &back, 0);
    const int from = 100 * RC_N + FS / 2;
    check(!memcmp(l + from, l0 + from, (size_t)(n - from) * 2) && !memcmp(r + from, r0 + from, (size_t)(n - from) * 2),
          "mix back to 0 settles to bit-exact dry");
    /* full wet has no dry feed: an impulse into a silent, braked cabinet gives nothing for the first 70 samples
     * (the shortest delay is 75 samples), mono and stereo */
    for (int st = 0; st < 2; st++) {
        rc_init(&s);
        blocks(&s, 50, BRAKE, st);
        int16_t a[RC_N] = { 30000 }, c[RC_N] = { 30000 };
        ramp w = fixed(BRAKE, 8192, 0, 32767);
        run_ramp(&s, a, st ? c : 0, RC_N, &w, 0);
        nz = 0;
        for (int i = 0; i < 64; i++) nz |= a[i] | (st ? c[i] : 0);
        check(!nz, st ? "stereo full wet: no dry contribution" : "mono full wet: no dry contribution");
    }
    free(l); free(r); free(l0); free(r0);
}

/* run silence blocks with a mode plan, returning the block where each rotor first reached its target speed */
static void t_rotors(void)
{
    rc_state s; rc_init(&s);
    /* settled rates: fast for 10 s, counting the phase in 64 bits */
    blocks(&s, 1, FAST, 1);
    uint64_t h = 0, b = 0;
    const int nb = 6891;                                      /* 10.0 s */
    for (int i = 0; i < nb; i++) {
        const uint32_t ph0 = s.rh.ph, pb0 = s.rb.ph;
        blocks(&s, 1, FAST, 1);
        h += (uint32_t)(s.rh.ph - ph0); b += (uint32_t)(s.rb.ph - pb0);
    }
    const double secs = (double)nb * RC_N / FS, hz_h = h / 4294967296.0 / secs, hz_b = b / 4294967296.0 / secs;
    printf("fast: horn %.4f Hz, bass %.4f Hz (6.7, 5.5)\n", hz_h, hz_b);
    check(fabs(hz_h - 6.7) < 0.005 && fabs(hz_b - 5.5) < 0.005, "fast rotor rates");
    rc_init(&s); blocks(&s, 1, SLOW, 1);
    h = b = 0;
    for (int i = 0; i < nb; i++) {
        const uint32_t ph0 = s.rh.ph, pb0 = s.rb.ph;
        blocks(&s, 1, SLOW, 1);
        h += (uint32_t)(s.rh.ph - ph0); b += (uint32_t)(s.rb.ph - pb0);
    }
    printf("slow: horn %.4f Hz, bass %.4f Hz (0.8, 0.7)\n", h / 4294967296.0 / secs, b / 4294967296.0 / secs);
    check(fabs(h / 4294967296.0 / secs - 0.8) < 0.002 && fabs(b / 4294967296.0 / secs - 0.7) < 0.002, "slow rotor rates");

    /* ramp times: from settled speed to a new mode; interrupted at 0.5 s */
    static const struct { int32_t from, to, mid; int at; double horn, bass; const char *what; } cases[] = {
        { SLOW, FAST, 0, 0, 1.0, 3.0, "slow -> fast" },
        { FAST, SLOW, 0, 0, 1.5, 4.0, "fast -> slow" },
        { FAST, BRAKE, 0, 0, 2.0, 5.0, "fast -> brake" },
        { SLOW, BRAKE, 0, 0, 2.0, 5.0, "slow -> brake" },
        { BRAKE, SLOW, 0, 0, 1.0, 3.0, "brake -> slow" },
        { SLOW, FAST, SLOW, 345, 0.5 + 1.5, 0.5 + 4.0, "slow -> fast, back to slow at 0.5 s" },
    };
    for (unsigned c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        rc_init(&s);
        blocks(&s, 1, cases[c].from, 1);
        int done_h = -1, done_b = -1, jump = 0;
        uint32_t ph = s.rh.ph;
        int32_t sp = s.rh.sp;
        for (int i = 0; i < 6000 && (done_h < 0 || done_b < 0); i++) {
            const int32_t mode = cases[c].mid && i >= cases[c].at ? cases[c].mid : cases[c].to;
            blocks(&s, 1, mode, 1);
            /* phase continuity: each block advances by its mean speed, never more than the fast speed allows */
            if ((uint32_t)(s.rh.ph - ph) != ((uint32_t)(sp + s.rh.sp) << 5) || s.rh.sp > RC_HORN.fast) jump = 1;
            ph = s.rh.ph; sp = s.rh.sp;
            if (cases[c].mid && i < cases[c].at) continue;
            if (done_h < 0 && s.rh.sp == s.rh.tgt) done_h = i + 1;
            if (done_b < 0 && s.rb.sp == s.rb.tgt) done_b = i + 1;
        }
        const double th = done_h * (double)RC_N / FS, tb = done_b * (double)RC_N / FS;
        printf("%-38s horn %.3f s (%.1f), bass %.3f s (%.1f)\n", cases[c].what, th, cases[c].horn, tb, cases[c].bass);
        check(fabs(th - cases[c].horn) < 0.03 * cases[c].horn && fabs(tb - cases[c].bass) < 0.03 * cases[c].bass, cases[c].what);
        check(!jump, "phase continuous through the ramp");
    }
}

/* page 2: speed scales both rotors' settled rates; acceleration scales ramp times; both reach their ends */
static double settled_hz(int32_t mode, int32_t speed, int horn)
{
    rc_state s; rc_init(&s);
    int16_t l[RC_N], r[RC_N];
    ramp p = fixed(mode, 8192, 0, 32767);
    p.a[4] = p.b[4] = speed;
    run_ramp(&s, (memset(l, 0, sizeof l), l), (memset(r, 0, sizeof r), r), RC_N, &p, 0);
    uint64_t t = 0;
    for (int i = 0; i < 689; i++) {
        const uint32_t ph0 = horn ? s.rh.ph : s.rb.ph;
        memset(l, 0, sizeof l); memset(r, 0, sizeof r);
        run_ramp(&s, l, r, RC_N, &p, 0);
        t += (uint32_t)((horn ? s.rh.ph : s.rb.ph) - ph0);
    }
    return t / 4294967296.0 / (689.0 * RC_N / FS);
}
static void t_page2(void)
{
    {   /* contour: open from RC_OPEN is a coefficient of exactly 1 (rc_mul passes the input unchanged); 16 kHz just below */
        int exact = 1;
        for (int32_t x = -(1 << 27); x < (1 << 27); x += 4093) if (rc_mul(x, rc_contour(32767)) != x) exact = 0;
        printf("contour: 31744 -> %d, %d -> %d, %d -> %d, 32767 -> %d\n", (int)rc_contour(31744), RC_OPEN - 1,
               (int)rc_contour(RC_OPEN - 1), RC_OPEN, (int)rc_contour(RC_OPEN), (int)rc_contour(32767));
        check(exact && rc_contour(RC_OPEN) == 32768 && rc_contour(31744) == 29415 && rc_contour(RC_OPEN - 1) > 32700,
              "contour open is exact");
    }
    static const struct { int32_t q; double x; } sp[] = { { 0, 0.25 }, { 16384, 1 }, { 32767, 4 } };
    for (int k = 0; k < 3; k++) {
        const double hs = settled_hz(SLOW, sp[k].q, 1), hf = settled_hz(FAST, sp[k].q, 1), bf = settled_hz(FAST, sp[k].q, 0);
        printf("speed x%.2f: horn slow %.3f Hz, fast %.3f Hz, bass fast %.3f Hz\n", sp[k].x, hs, hf, bf);
        check(fabs(hs / (0.8 * sp[k].x) - 1) < 0.01 && fabs(hf / (6.7 * sp[k].x) - 1) < 0.01 && fabs(bf / (5.5 * sp[k].x) - 1) < 0.01,
              "speed scales both rotors");
    }
    static const struct { int32_t q; double x; } ac[] = { { 0, 5 }, { 16384, 1 }, { 32767, 0.1 } };
    for (int k = 0; k < 3; k++) {
        rc_state s; rc_init(&s);
        int16_t l[RC_N], r[RC_N];
        ramp p = fixed(SLOW, 8192, 0, 32767);
        p.a[5] = p.b[5] = ac[k].q;
        memset(l, 0, sizeof l); memset(r, 0, sizeof r);
        run_ramp(&s, l, r, RC_N, &p, 0);
        p.a[0] = p.b[0] = FAST;
        int dh = -1, db_ = -1;
        for (int i = 0; i < 20000 && (dh < 0 || db_ < 0); i++) {
            memset(l, 0, sizeof l); memset(r, 0, sizeof r);
            run_ramp(&s, l, r, RC_N, &p, 0);
            if (dh < 0 && s.rh.sp == s.rh.tgt) dh = i + 1;
            if (db_ < 0 && s.rb.sp == s.rb.tgt) db_ = i + 1;
        }
        const double th = dh * (double)RC_N / FS, tb = db_ * (double)RC_N / FS;
        printf("acceleration x%.1f: slow -> fast horn %.3f s (%.2f), bass %.3f s (%.2f)\n", ac[k].x, th, 1.0 * ac[k].x, tb, 3.0 * ac[k].x);
        check(fabs(th / ac[k].x - 1.0) < 0.05 && fabs(tb / (3.0 * ac[k].x) - 1) < 0.05, "acceleration scales the ramp times");
    }
    /* the extremes together, with delay bounds checked: fastest speed and quickest ramps, noise at full scale */
    const int n = round_up(FS * 2);
    int16_t *l = buf(n), *r = buf(n);
    for (int i = 0; i < n; i++) { l[i] = (int16_t)(32767 * noise()); r[i] = (int16_t)(32767 * noise()); }
    rc_state s; rc_init(&s);
    ramp p = { { SLOW, 0, 32767, 32767, 32767, 32767, 0 }, { FAST, 32767, 32767, 32767, 32767, 32767, 32767 }, 20, n / RC_N - 20 };
    run_ramp(&s, l, r, n, &p, 0);
    printf("page 2 extremes: ran (fast x4, quickest ramps, contour 1 kHz .. open)\n");
    free(l); free(r);
}

static void t_brake(void)
{
    /* brake from fast at eight moments, then a -12 dBFS 1 kHz tone (all in the horn band) and a 200 Hz tone (bass) */
    const int n = round_up(FS);
    int16_t *l = buf(n), *r = buf(n);
    printf("brake stop angles (horn deg / bass deg): level re input, L and R, 1 kHz and 200 Hz\n");
    double worst = 0, spread = 0;
    for (int k = 0; k < 8; k++) {
        rc_state s; rc_init(&s);
        blocks(&s, 1 + k * 37, FAST, 1);
        blocks(&s, 3700, BRAKE, 1);                            /* 5.4 s: both stopped */
        const double ah = s.rh.ph * 360.0 / 4294967296.0, ab = s.rb.ph * 360.0 / 4294967296.0;
        double lv[2][2];
        for (int f = 0; f < 2; f++) {
            const double hz = f ? 200 : 1000;
            for (int i = 0; i < n; i++) l[i] = r[i] = (int16_t)lrint(8192 * sin(2 * M_PI * hz * i / FS));
            const double in = rms(l + n / 2, n / 2);
            ramp p = fixed(BRAKE, 8192, 0, 32767);
            run_ramp(&s, l, r, n, &p, 0);
            lv[f][0] = db(rms(l + n / 2, n / 2) / in); lv[f][1] = db(rms(r + n / 2, n / 2) / in);
            for (int c = 0; c < 2; c++) if (lv[f][c] < worst) worst = lv[f][c];
            if (fabs(lv[f][0] - lv[f][1]) > spread) spread = fabs(lv[f][0] - lv[f][1]);
        }
        printf("  %5.1f / %5.1f: 1k %5.1f %5.1f dB, 200 %5.1f %5.1f dB\n", ah, ab, lv[0][0], lv[0][1], lv[1][0], lv[1][1]);
        check(s.rh.sp == 0 && s.rb.sp == 0, "brake reaches zero speed");
    }
    printf("brake: lowest level %.1f dB, widest L/R difference %.1f dB\n", worst, spread);
    check(worst > -10, "brake never drops a side below -10 dB");
    free(l); free(r);
}

static void t_positions(void)
{
    /* three states as the plugin keeps them: synth noise, drum silence, master stereo tone. The master output must
     * match a run on its own, and re-initialising the synth position halfway must not touch the others. */
    const int n = round_up(FS * 2);
    rc_state pos[3], ref;
    for (int i = 0; i < 3; i++) rc_init(&pos[i]);
    rc_init(&ref);
    int16_t *a = buf(n), *d = buf(n), *ml = buf(n), *mr = buf(n);
    for (int i = 0; i < n; i++) {
        a[i] = (int16_t)(20000 * noise());
        ml[i] = (int16_t)lrint(9000 * sin(2 * M_PI * 440 * i / FS)); mr[i] = (int16_t)lrint(9000 * sin(2 * M_PI * 660 * i / FS));
    }
    int16_t *rl = copy(ml, n), *rr = copy(mr, n);
    ramp pa = fixed(FAST, 0, 20000, 32767), pd = fixed(SLOW, 32767, 0, 32767), pm = fixed(SLOW, 8192, 8000, 32767);
    for (int b = 0; b < n / RC_N; b++) {
        if (b == n / RC_N / 2) rc_init(&pos[0]);
        run_ramp(&pos[0], a + b * RC_N, 0, RC_N, &pa, b);
        run_ramp(&pos[1], d + b * RC_N, 0, RC_N, &pd, b);
        run_ramp(&pos[2], ml + b * RC_N, mr + b * RC_N, RC_N, &pm, b);
    }
    run_ramp(&ref, rl, rr, n, &pm, 0);
    int nz = 0;
    for (int i = 0; i < n; i++) nz |= d[i];
    check(!nz, "positions: the drum position stays silent while the others run");
    check(!memcmp(ml, rl, (size_t)n * 2) && !memcmp(mr, rr, (size_t)n * 2), "positions: master identical to a run on its own");
    free(a); free(d); free(ml); free(mr); free(rl); free(rr);
}

static void t_master_routing(const int16_t *org, int n)
{
    int16_t *l = copy(org, n), *r = buf(n);
    for (int i = 0; i < n; i++) r[i] = (int16_t)-l[i];
    for (int i = 0; i < n; i++) if (r[i] == -32768 && l[i] == -32768) r[i] = 32767;
    rc_state s; rc_init(&s);
    ramp p = fixed(FAST, 8192, 0, 32767);
    run_ramp(&s, l, r, n, &p, 0);
    int nz = 0;
    for (int i = 0; i < n; i++) nz |= l[i] | r[i];
    check(!nz, "master: opposite-polarity L/R cancels at the mono sum (documented)");
    /* left only, full wet: both sides carry the cabinet, about 6 dB below a centred signal */
    memcpy(l, org, (size_t)n * 2); memset(r, 0, (size_t)n * 2);
    rc_init(&s);
    run_ramp(&s, l, r, n, &p, 0);
    printf("master, left-only input, full wet: L %.1f dB, R %.1f dB re input\n", db(rms(l, n) / rms(org, n)), db(rms(r, n) / rms(org, n)));
    check(rms(r, n) > 0.2 * rms(org, n), "master: left-only input reaches both cabinet microphones");
    free(l); free(r);
}

/* largest sample-to-sample step relative to the output's RMS: a click shows as a spike above the steady value */
static double max_step(const int16_t *x, int n) { int m = 0; for (int i = 1; i < n; i++) { int d = abs(x[i] - x[i - 1]); if (d > m) m = d; } return m / (rms(x, n) + 1); }
static void t_sweeps(const int16_t *org, int n)
{
    static const struct { int knob; const char *what; } k[] = { { 1, "mics" }, { 2, "drive" }, { 3, "mix" }, { 0, "motion" },
                                                                { 4, "speed" }, { 5, "accel" }, { 6, "contour" } };
    for (int j = 0; j < 7; j++) {
        int16_t *l = copy(org, n), *r = copy(org, n), *sl = copy(org, n), *sr = copy(org, n);
        ramp sw = { { FAST, 8192, 0, 32767 }, { FAST, 8192, 0, 32767 }, 100, n / RC_N - 100 };
        p2(&sw);
        sw.a[k[j].knob] = 0; sw.b[k[j].knob] = 32767;
        if (k[j].knob == 0) { sw.a[0] = 0; sw.b[0] = 32767; }
        rc_state s; rc_init(&s);
        run_ramp(&s, l, r, n, &sw, 0);
        double b = 0;                                         /* the reference: the same knob held at 0, 50%, 100% */
        for (int v = 0; v < 3; v++) {
            memcpy(sl, org, (size_t)n * 2); memcpy(sr, org, (size_t)n * 2);
            ramp st = fixed(FAST, 8192, 0, 32767);
            st.a[k[j].knob] = st.b[k[j].knob] = v * 16383 + (v == 2);
            rc_init(&s);
            run_ramp(&s, sl, sr, n, &st, 0);
            const double m = max_step(sl, n);
            if (m > b) b = m;
        }
        const double a = max_step(l, n);
        printf("sweep %-7s: largest step %.2f x RMS (held at 0/50/100%%: up to %.2f)\n", k[j].what, a, b);
        check(a < 1.6 * b + 0.3, "control sweep without clicks");
        free(l); free(r); free(sl); free(sr);
    }
    /* near/far loudness: RMS at five mic settings, slow and fast */
    for (int m = 0; m < 2; m++) {
        printf("mics, %s, L RMS re dry:", m ? "fast" : "slow");
        for (int g = 0; g <= 4; g++) {
            int16_t *l = copy(org, n), *r = copy(org, n);
            rc_state s; rc_init(&s);
            ramp p = fixed(m ? FAST : SLOW, g * 8191, 0, 32767);
            run_ramp(&s, l, r, n, &p, 0);
            printf(" %d%% %.1f dB", g * 25, db(rms(l + FS, n - FS) / rms(org + FS, n - FS)));
            free(l); free(r);
        }
        printf("\n");
    }
}

static void t_drive(void)
{
    printf("drive stage alone (1 kHz sine): output re input\n");
    static const double lev[3] = { -24, -12, -3 };
    for (int d = 0; d <= 2; d++) {
        const rc_drv p = rc_drive_params(d == 2 ? 32767 : d * 16384);
        printf("  drive %3d%%:", d * 50);
        for (int k = 0; k < 3; k++) {
            const double a = 32767 * pow(10, lev[k] / 20);
            double si = 0, so = 0, mean = 0;
            for (int i = 0; i < FS; i++) {
                const int32_t x = (int32_t)lrint(a * sin(2 * M_PI * 1000 * i / FS)), y = rc_drive(x, p.g, p.m, p.b);
                si += (double)x * x; so += (double)y * y; mean += y;
            }
            mean /= FS;
            const double r = db(sqrt(so / si));
            printf(" %3.0f dBFS %+5.1f dB", lev[k], r);
            if (d == 0 && k < 2) check(fabs(r) < 0.05, "drive 0 is clean below -12 dBFS");
        }
        printf("\n");
    }
    const rc_drv z = rc_drive_params(0);
    int exact = 1;
    for (int x = -32768; x <= 32767; x += 7) if (abs(x) <= 16384 && rc_drive(x, z.g, z.m, z.b) != x) exact = 0;
    check(exact, "drive 0 passes samples up to half scale unchanged");
}

/* ==== renders ==== */
typedef struct { const char *name; int16_t *src; int n; } source;
static FILE *notes;

static void render_set(const source *so)
{
    const int n = so->n;
    const double dry = rms(so->src, n);
    char name[128];
    #define EMIT(tag, L, R, desc) do { \
        snprintf(name, sizeof name, "%s_%s.wav", so->name, tag); \
        wav("raw", name, L, R, n, 1.0); \
        const double lvl = R ? sqrt((rms(L, n) * rms(L, n) + rms(R, n) * rms(R, n)) / 2) : rms(L, n); \
        wav("matched", name, L, R, n, dry / (lvl + 1e-9)); \
        fprintf(notes, "%-34s %s; raw level %+.1f dB re dry\n", name, desc, db(lvl / dry)); } while (0)

    EMIT("1_dry", so->src, so->src, "the source, untouched");
    static const struct { const char *tag, *desc; step s[4]; int ns; } sets[] = {
        { "2_slow", "slow, mics 25%, clean, full wet", { { 0, { SLOW, 8192, 0, 32767 } } }, 1 },
        { "3_fast", "fast, mics 25%, clean, full wet", { { 0, { FAST, 8192, 0, 32767 } } }, 1 },
        { "4_transition", "slow 0-4 s, fast 4-10 s, brake 10 s on (whole bass coast-down)",
          { { 0, { SLOW, 8192, 0, 32767 } }, { 4, { FAST, 8192, 0, 32767 } }, { 10, { BRAKE, 8192, 0, 32767 } } }, 3 },
        { "5_near_far", "slow; mics 0% (close) to 6 s, then 100% (distant)",
          { { 0, { SLOW, 0, 0, 32767 } }, { 6, { SLOW, 32767, 0, 32767 } } }, 2 },
        { "6_drive", "fast, mics 25%, drive 75%, full wet", { { 0, { FAST, 8192, 24575, 32767 } } }, 1 },
        { "7_speed_x4", "page 2: fast at speed x4 (horn 27 Hz)", { { 0, { FAST, 8192, 0, 32767, 32767, 16384, 23807 } } }, 1 },
        { "8_speed_x025", "page 2: slow at speed x0.25 (horn 0.2 Hz)", { { 0, { SLOW, 8192, 0, 32767, 1, 16384, 23807 } } }, 1 },
        { "9_quick_accel", "page 2: acceleration x0.1; slow, fast at 4 s, slow at 8 s, brake at 12 s",
          { { 0, { SLOW, 8192, 0, 32767, 16384, 32767, 23807 } }, { 4, { FAST, 8192, 0, 32767, 16384, 32767, 23807 } },
            { 8, { SLOW, 8192, 0, 32767, 16384, 32767, 23807 } }, { 12, { BRAKE, 8192, 0, 32767, 16384, 32767, 23807 } } }, 4 },
        { "10_slow_accel", "page 2: acceleration x5; slow, fast at 2 s (bass takes 15 s)",
          { { 0, { SLOW, 8192, 0, 32767, 16384, 1, 23807 } }, { 2, { FAST, 8192, 0, 32767, 16384, 1, 23807 } } }, 2 },
        { "11_contour_dark", "page 2: contour 1.5 kHz, fast, drive 50%", { { 0, { FAST, 8192, 16384, 32767, 16384, 16384, 5000 } } }, 1 },
    };
    for (unsigned k = 0; k < sizeof sets / sizeof sets[0]; k++) {
        int16_t *l = copy(so->src, n), *r = copy(so->src, n);
        rc_state s; rc_init(&s);
        plan p = { sets[k].s, sets[k].ns };
        run_plan(&s, l, r, n, &p);
        EMIT(sets[k].tag, l, r, sets[k].desc);
        if (k == 1) {                                         /* fast: mono fold-down, and the synth-position insert */
            int16_t *m = buf(n);
            for (int i = 0; i < n; i++) m[i] = (int16_t)((l[i] + r[i]) >> 1);
            EMIT("3_fast_mono_folddown", m, (int16_t *)0, "fast master, L+R folded to mono");
            fprintf(notes, "%34s fold-down %+.1f dB re the stereo channels\n", "", db(rms(m, n) / sqrt((rms(l, n) * rms(l, n) + rms(r, n) * rms(r, n)) / 2)));
            memcpy(m, so->src, (size_t)n * 2);
            rc_init(&s);
            run_plan(&s, m, 0, n, &p);
            EMIT("3_fast_synth_position", m, (int16_t *)0, "fast at the synth/drum position (mono, left close+far mics)");
            free(m);
        }
        free(l); free(r);
    }
}

static void renders(void)
{
    char path[600];
    snprintf(path, sizeof path, "%s/rotor147/settings.txt", outdir);
    notes = fopen(path, "w");
    if (!notes) { printf("cannot write %s (make %s/rotor147/raw and matched first)\n", path, outdir); failures++; return; }
    fprintf(notes, "rotor147 listening renders: the actual integer DSP (rc_dsp.h), 44.1 kHz stereo 16-bit.\n"
                   "raw/: the output as is (levels, headroom). matched/: each file scaled to the dry source's RMS.\n"
                   "Sources: generated by host_test.c (a tonewheel-like organ, chords C Am F G, about -12 dBFS RMS;\n"
                   "a sustained Cm9 synth chord of detuned saws; one high organ note C6 for drive aliasing). No vocal\n"
                   "sample yet. Knobs: motion brake/slow/fast, mics 0..100%% (close to distant), drive, mix.\n"
                   "Command: see the header of rotor147/plugin/test/host_test.c.\n\n");
    const int n = round_up(FS * 20);
    source so[3] = { { "organ", organ(n, 0.5, 0), n }, { "synth", synth(n, 0.5), n }, { "organ_high", organ(round_up(FS * 6), 0.5, 1), round_up(FS * 6) } };
    render_set(&so[0]);
    render_set(&so[1]);
    {   /* the high note: clean fast and fully driven fast, for aliasing */
        const source *h = &so[2];
        const int hn = h->n;
        for (int d = 0; d < 2; d++) {
            int16_t *l = copy(h->src, hn), *r = copy(h->src, hn);
            rc_state s; rc_init(&s);
            ramp p = fixed(FAST, 8192, d ? 32767 : 0, 32767);
            run_ramp(&s, l, r, hn, &p, 0);
            const char *nm = d ? "organ_high_drive100.wav" : "organ_high_clean.wav";
            wav("raw", nm, l, r, hn, 1.0);
            fprintf(notes, "%-34s C6 organ note, fast, drive %s (listen for aliasing)\n", nm, d ? "100%" : "0");
            free(l); free(r);
        }
    }
    fclose(notes);
    for (int i = 0; i < 3; i++) free(so[i].src);
    printf("renders: %s/rotor147/ (raw, matched, settings.txt)\n", outdir);
}

int main(int argc, char **argv)
{
    outdir = argc > 1 ? argv[1] : 0;
    for (int k = 0; k < 8192; k++) sine[k] = (int16_t)lrint(32767 * sin(2 * M_PI * k / 8192));
    printf("state: %u bytes per position, %u for three\n", (unsigned)sizeof(rc_state), (unsigned)(3 * sizeof(rc_state)));
    t_silence();
    t_extremes();
    t_mix();
    t_rotors();
    t_page2();
    t_brake();
    t_positions();
    const int n = round_up(FS * 6);
    int16_t *org = organ(n, 0.5, 0);
    t_master_routing(org, n);
    t_sweeps(org, n);
    free(org);
    t_drive();
    if (outdir) renders();
    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
