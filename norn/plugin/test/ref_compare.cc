/* Norn against Plaits itself: renders Plaits' own WaveshapingEngine, FMEngine (`fm` first), GrainEngine (`gr`) or WaveTerrainEngine (`wt`) ( float, 47,872 Hz, from
 * the Mutable Instruments checkout) and Norn's integer port (88.2 kHz) at the same pitch and knobs, and compares the
 * spectrum below 8 kHz (each harmonic; for FM every two bins) and the loudness. The two run at different rates, so
 * the comparison is by spectrum, not sample.
 *   MI=/mnt/d/src/mutable-instruments
 *   g++ -O2 -Wall -I norn/plugin -I $MI norn/test-ref... (see norn/README.md)
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plaits/dsp/engine/waveshaping_engine.h"
#define private public                                /* to start FMEngine's ramps at their targets */
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/grain_engine.h"
#include "plaits/dsp/engine2/wave_terrain_engine.h"
#undef private
#include "plaits/resources.h"
#include "stmlib/dsp/dsp.h"
#include "ws_dsp.h"
#include "fm_dsp.h"
#include "gr_dsp.h"
#include "wt_dsp.h"

static const double REF_RATE = 47872.34;
static double an_s = 0.5;                              /* analyse the last an_s seconds of an_s + 0.25 (env REF_SECONDS) */
static int failures, verbose;
static double worst_spec = -999, worst_noise = -999, worst_alias = -999;

/* magnitude of frequency hz in x (Hann window), as dB re full scale of a sine */
static double level_db(const float *x, int n, double rate, double hz)
{
    /* the window and the tone by rotating phasors (no cos per sample), renormalised now and then */
    double re = 0, im = 0, w_sum = 0;
    const double dw = 2 * M_PI / (n - 1), dp = -2 * M_PI * hz / rate;
    double wc = 1, ws = 0, pc = 1, ps = 0;
    const double cdw = cos(dw), sdw = sin(dw), cdp = cos(dp), sdp = sin(dp);
    for (int i = 0; i < n; i++) {
        const double w = 0.5 - 0.5 * wc;
        re += w * x[i] * pc; im += w * x[i] * ps; w_sum += w;
        double t = wc * cdw - ws * sdw; ws = wc * sdw + ws * cdw; wc = t;
        t = pc * cdp - ps * sdp; ps = pc * sdp + ps * cdp; pc = t;
        if ((i & 1023) == 1023) {
            const double a = 1 / sqrt(wc * wc + ws * ws), b = 1 / sqrt(pc * pc + ps * ps);
            wc *= a; ws *= a; pc *= b; ps *= b;
        }
    }
    return 20 * log10(2 * sqrt(re * re + im * im) / w_sum + 1e-9);
}

static double rms(const float *x, int n)
{
    double s = 0, m = 0;
    for (int i = 0; i < n; i++) m += x[i];
    m /= n;
    for (int i = 0; i < n; i++) s += (x[i] - m) * (x[i] - m);
    return sqrt(s / n);
}

static float ref_buf[48000 * 5], port_buf[88200 * 5];

static int fm_mode;                                   /* 1: compare the FM model */
static int gr_mode;                                   /* 1: compare the formant model */
static int wt_mode;                                   /* 1: compare the wave terrain model */

static int render_ref(double note, float harm, float timbre, float morph)
{
    static char mem[1024];
    stmlib::BufferAllocator alloc(mem, sizeof mem);
    plaits::WaveshapingEngine ws;
    plaits::FMEngine fm;
    plaits::GrainEngine gr;
    plaits::WaveTerrainEngine wt;
    plaits::Engine &e = fm_mode ? (plaits::Engine &)fm : gr_mode ? (plaits::Engine &)gr
                      : wt_mode ? (plaits::Engine &)wt : (plaits::Engine &)ws;
    e.Init(&alloc);
    plaits::EngineParameters p;
    memset(&p, 0, sizeof p);
    p.note = (float)note; p.harmonics = harm; p.timbre = timbre; p.morph = morph;
    if (fm_mode) {
        /* start Plaits' ramps at their targets, as Norn's note does, so both engines' phases line up (Plaits would
         * glide the modulator up from a0 in its first block, shifting its phase; with a non-integer ratio, sidebands
         * a hertz apart then add differently and the spectra differ though the engines agree) */
        const float n4 = p.note - 24.0f;
        float mf = plaits::NoteToFrequency(n4 + stmlib::Interpolate(plaits::lut_fm_frequency_quantizer, harm, 128.0f));
        if (mf > 0.5f) mf = 0.5f;
        float hf = 1.0f - (n4 + stmlib::Interpolate(plaits::lut_fm_frequency_quantizer, harm, 128.0f) - 72.0f) * 0.025f;
        hf = hf < 0 ? 0 : hf > 1 ? 1 : hf;
        fm.previous_carrier_frequency_ = plaits::NoteToFrequency(n4);
        fm.previous_modulator_frequency_ = mf;
        fm.previous_amount_ = 2.0f * timbre * timbre * hf * hf;
        fm.previous_feedback_ = 2.0f * morph - 1.0f;
    }
    const int n = (int)(REF_RATE * (an_s + 0.25));
    float out[12], aux[12];
    bool env = false;
    for (int i = 0; i + 12 <= n; i += 12) {
        e.Render(p, out, aux, 12, &env);
        memcpy(ref_buf + i, out, sizeof out);
    }
    return n / 12 * 12;
}

static int self_mode;                                 /* REF_SELF with wt: Plaits against itself at the OP-1's rate */

static int render_port(double note, float harm, float timbre, float morph)
{
    if (self_mode) {                                  /* Plaits at 44.1 kHz (88.2 kHz inside, as Norn): the note
                                                       * shifted so its cycles a sample are hz / 44100 */
        static float keep[48000 * 5];
        memcpy(keep, ref_buf, sizeof keep);
        const int m = render_ref(note + 12 * log2(REF_RATE / 44100.0), harm, timbre, morph);
        memcpy(port_buf, ref_buf, m * sizeof(float));
        memcpy(ref_buf, keep, sizeof keep);
        return m;
    }
    ws_voice v;
    memset(&v, 0, sizeof v);
    ws_voice_note(&v);
    const double hz = 440.0 * pow(2.0, (note - 69) / 12);
    int32_t inc[WS_BLOCK];
    for (int j = 0; j < WS_BLOCK; j++) inc[j] = (int32_t)(uint32_t)(hz / WS_RATE * 4294967296.0);
    const int n = (int)(WS_RATE * (an_s + 0.25)) / WS_BLOCK * WS_BLOCK;
    int16_t out[WS_BLOCK];
    if (wt_mode) {
        /* Plaits' path is a "magic circle" whose step is Fast2Sin(f / 2), an approximation of 2 sin(pi f / 2) (up to
         * 7 cents off): it turns by 2 asin(e / 2) a step, at twice its 47.9 kHz rate. Norn plays that frequency. */
        const double f = plaits::NoteToFrequency((float)note) * 0.5, fp = f * M_PI;
        const double e = fp * (2.0 - (2.0 * 0.96 / 6.0) * fp * fp);
        const double path_hz = 2 * asin(e / 2) / (2 * M_PI) * 2 * REF_RATE;
        for (int j = 0; j < WS_BLOCK; j++) inc[j] = (int32_t)(uint32_t)(path_hz / WS_RATE * 4294967296.0);
        wt_voice w;
        memset(&w, 0, sizeof w);
        wt_voice_note(&w);
        wt_knobs k;
        wt_knobs_make(&k, (int32_t)(harm * 32767), (int32_t)(timbre * 32767), (int32_t)(morph * 32767), (uint32_t)inc[0]);
        for (int i = 0; i < n; i += WT_BLOCK) {
            wt_render(&w, &k, inc, WT_BLOCK, 32767 * 32767 / WT_OUT_GAIN, out, fm_sine_q);
            for (int j = 0; j < WT_BLOCK; j++) port_buf[i + j] = out[j] / 32767.0f;
        }
        return n;
    }
    if (gr_mode) {
        gr_voice g;
        memset(&g, 0, sizeof g);
        gr_voice_note(&g);
        gr_knobs k;
        gr_knobs_make(&k, (int32_t)(harm * 32767), (int32_t)(timbre * 32767), (int32_t)(morph * 32767), (uint32_t)inc[0]);
        for (int i = 0; i < n; i += GR_BLOCK) {
            gr_render(&g, &k, inc, GR_BLOCK, 32767 * 32767 / GR_OUT_GAIN, out, fm_sine_q);
            for (int j = 0; j < GR_BLOCK; j++) port_buf[i + j] = out[j] / 32767.0f;
        }
        return n;
    }
    if (fm_mode) {
        fm_voice f;
        memset(&f, 0, sizeof f);
        fm_voice_note(&f);
        fm_knobs k;
        /* Plaits' own frequencies: its SemitonesToRatio rounds a fractional semitone down to 1/256 (a ratio of 12.16
         * semitones plays 12.156, 0.4 cent flat), and over a long window that drift alone parts the spectra */
        const float n4 = (float)note - 24.0f;
        float mf = plaits::NoteToFrequency(n4 + stmlib::Interpolate(plaits::lut_fm_frequency_quantizer, harm, 128.0f));
        if (mf > 0.5f) mf = 0.5f;
        const double nudge = getenv("REF_NUDGE") ? 1 + atof(getenv("REF_NUDGE")) : 1;   /* chaos check */
        const double car_hz = plaits::NoteToFrequency(n4) * 4 * REF_RATE * nudge, mod_hz = mf * 4 * REF_RATE * nudge;
        for (int j = 0; j < WS_BLOCK; j++) inc[j] = (int32_t)(uint32_t)(car_hz / WS_RATE * 4294967296.0);
        fm_knobs_make(&k, (int32_t)(harm * 32767), (int32_t)(timbre * 32767), (int32_t)(morph * 32767), (uint32_t)inc[0]);
        k.mod = (uint32_t)(mod_hz / (2.0 * WS_RATE) * 4294967296.0);
        for (int i = 0; i < n; i += FM_BLOCK) {
            fm_render(&f, &k, inc, FM_BLOCK, 32767 * 32767 / FM_OUT_GAIN, out, fm_sine_q);
            for (int j = 0; j < FM_BLOCK; j++) port_buf[i + j] = out[j] / 32767.0f;
        }
        return n;
    }
    ws_knobs k;
    ws_knobs_make(&k, (int32_t)(harm * 32767), (int32_t)(timbre * 32767), (int32_t)(morph * 32767), (uint32_t)inc[0]);
    for (int i = 0; i < n; i += WS_BLOCK) {
        ws_render(&v, &k, inc, WS_BLOCK, 32767 * 32767 / WS_OUT_GAIN, out);    /* undo out_gain: compare raw */
        for (int j = 0; j < WS_BLOCK; j++) port_buf[i + j] = out[j] / 32767.0f;
    }
    return n;
}

static void compare(double note, float harm, float timbre, float morph)
{
    const double hz = 440.0 * pow(2.0, (note - 69) / 12);
    const int nr = render_ref(note, harm, timbre, morph), np = render_port(note, harm, timbre, morph);
    const double prate = self_mode ? 44100.0 : WS_RATE;
    const int ar = (int)(REF_RATE * an_s), ap = (int)(prate * an_s);
    const float *r = ref_buf + nr - ar, *q = port_buf + np - ap;
    /* the spectral difference: the energy of (ref - port) over the harmonics below 8 kHz, against the ref's energy
     * there, in dB. Per-harmonic dB would flag harmonics near a null, where a small difference is many dB. */
    double diff = 0, total = 0, total_p = 0, worst = 0; int worst_h = 0;
    double band_a[32] = { 0 }, band_b[32] = { 0 };          /* third-octave bands from 20 Hz, for noisy settings */
    const double step = fm_mode ? 2.0 / an_s / hz : 1.0;   /* FM: every 2 bins (its sidebands are not harmonics) */
    for (double h = step; h * hz < 8000; h += step) {
        const double a = level_db(r, ar, REF_RATE, h * hz), b = level_db(q, ap, prate, h * hz);
        const double la = pow(10, a / 20), lb = pow(10, b / 20);
        diff += (la - lb) * (la - lb); total += la * la; total_p += lb * lb;
        const int band = h * hz < 20 ? 0 : (int)(3 * log2(h * hz / 20));
        band_a[band] += la * la; band_b[band] += lb * lb;
        if (fabs(la - lb) > worst) { worst = fabs(la - lb); worst_h = (int)(h * hz); }
        if (verbose && (!fm_mode || a > -60 || b > -60)) printf("    %7.1f Hz %7.2f %7.2f dB\n", h * hz, a, b);
    }
    double spec = 10 * log10(diff / total + 1e-12);
    /* loudness: the waveshaper's whole rms; FM's energy below 8 kHz, since at high ratios Norn's 88.2 kHz output carries
     * ultrasonic sidebands that Plaits' 48 kHz output can't (the OP-1's decimator removes them) */
    const double lr = fm_mode || gr_mode || wt_mode ? 10 * log10(total + 1e-18) : 20 * log10(rms(r, ar) + 1e-9);
    const double lp = fm_mode || gr_mode || wt_mode ? 10 * log10(total_p + 1e-18) : 20 * log10(rms(q, ap) + 1e-9);
    /* FM's frequency feedback (morph left of centre) turns strong settings into noise, the same level and colour in
     * both engines but different line by line: there, the third-octave band levels must match instead, within
     * -15 dB (about 1.5 dB a band) and 1 dB of loudness; the oversampling rates differ (176.4 against 191.5 kHz) and
     * high notes alias into the noise a little differently */
    double bd = 0, bt = 0;
    for (int i = 0; i < 32; i++) { const double x = sqrt(band_a[i]) - sqrt(band_b[i]); bd += x * x; bt += band_a[i]; }
    const double bands = 10 * log10(bd / bt + 1e-12);
    const int noisy = fm_mode && morph < 0.5f && spec > -20;
    /* wave terrain on high notes: its terrains fold harmonics far above Nyquist back into the band, differently at
     * each rate (Plaits computes at 95.7 kHz, Norn at 88.2 kHz). Plaits against itself at 88.2 kHz (REF_SELF=1) differs
     * by up to -12.9 dB and 1.4 dB there, so those settings are held to -10 dB and 1.5 dB */
    const int aliased = wt_mode && note >= 72;
    const int bad = noisy ? bands > -15 || fabs(lr - lp) > 1.0
                  : aliased ? spec > -10 || fabs(lr - lp) > 1.5 : spec > -20 || fabs(lr - lp) > 0.5;
    printf("%s note %3.0f harm %.2f timbre %.2f morph %.2f: rms %6.2f / %6.2f dB, difference %6.1f dB (most at %d Hz)%s\n",
           bad ? "FAIL" : "ok  ", note, harm, timbre, morph, lr, lp, noisy ? bands : spec, worst_h,
           noisy ? ", noise: by third-octave bands" : "");
    if (spec > worst_spec && !noisy && !aliased) worst_spec = spec;
    if (aliased && spec > worst_alias) worst_alias = spec;
    if (noisy && bands > worst_noise) worst_noise = bands;
    failures += bad;
}

int main(int argc, char **argv)
{
    if (getenv("REF_SECONDS")) an_s = atof(getenv("REF_SECONDS"));
    if (argc > 1 && !strcmp(argv[1], "fm")) { fm_mode = 1; argc--; argv++; }
    else if (argc > 1 && !strcmp(argv[1], "gr")) { gr_mode = 1; argc--; argv++; }
    else if (argc > 1 && !strcmp(argv[1], "wt")) { wt_mode = 1; argc--; argv++; self_mode = getenv("REF_SELF") != 0; }
    if (argc == 5) {                                   /* one setting, every harmonic: note harm timbre morph */
        verbose = 1;
        compare(atof(argv[1]), (float)atof(argv[2]), (float)atof(argv[3]), (float)atof(argv[4]));
        return failures != 0;
    }
    const double notes[] = { 36, 48, 60, 72, 84 };
    const float v[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, mv[] = { 0.0f, 0.5f, 1.0f };
    for (double n : notes)
        for (float h : v)
            for (float t : v)
                for (float m : mv)
                    compare(n, h, t, m);
    printf("largest difference %.1f dB", worst_spec);
    if (fm_mode) printf("; noisy settings, by bands, %.1f dB", worst_noise);
    if (wt_mode) printf("; notes 72 and up (aliasing), %.1f dB", worst_alias);
    printf("\n");
    printf(failures ? "%d FAILED\n" : "all match\n", failures);
    return failures != 0;
}
