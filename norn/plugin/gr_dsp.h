/* Norn's formant model: Plaits' "granular formant oscillator" (plaits/dsp/engine/grain_engine.cc and
 * plaits/dsp/oscillator/grainlet_oscillator.h, Emilie Gillet, MIT; see NOTICE) in integers for the OP-1's 88.2 kHz
 * voices. OUT only: two grainlets summed and DC-blocked (Plaits' AUX, the Z oscillator, is not made).
 *
 * A grainlet is a carrier pulse (a shaped sine bump, once a period of the note) times a formant sine that restarts with
 * each pulse; the two grainlets share the carrier and differ in formant frequency. The knobs (Plaits names):
 *   harmonics : the second formant's interval from the first, -2 to +2 octaves; left of centre the carrier also bleeds
 *               through (the formant sine plus a constant, so the note's own pulse sounds)
 *   timbre    : the formant frequency, MIDI note 24 to 108 (33 Hz to 4.2 kHz), whatever note is played
 *   morph     : the carrier's shape: narrow pulses (left) through a smooth bump to wide ones (right); at low notes only
 *
 * Numbers: phases and increments are Q32 (2^32 = one cycle); samples, knobs, sines Q15; the carrier's phase map runs in
 * Q15 cycles. Changes from Plaits, all made for the integer port:
 *   1. Plaits runs at 47.9 kHz without oversampling; this runs at 88.2 kHz without oversampling, with the same
 *      frequency limits in hertz (carrier 6 kHz, formants 12 kHz);
 *   2. the two grainlets share one carrier (Plaits computes the same carrier twice) and are summed before the one
 *      multiply: carrier x (formant1 + formant2 + 2 bleed) / (1 + bleed);
 *   3. the carrier's piecewise phase map is a min/max form with a mantissa and shift for each slope (16 x 16 multiplies,
 *      no branch); its coefficients are worked out once a block from the shape, which glides half way to the knob each
 *      block; Plaits ramps the shape per sample;
 *   4. the reset time at each carrier period (a division per period in Plaits) uses one reciprocal a block, of the
 *      block's first frequency; the formants and bleed ramp linearly across the block;
 *   5. the sines read the OP-1's 8,192-point table at the nearest point (as fm_dsp.h);
 *   6. the DC blocker is a plain one-pole at 0.3 x the note (Plaits: a TPT one-pole with an approximated tan);
 *   7. at exactly shape 1 (morph right at the lowest notes) Plaits jumps to another curve; here the shape stops just
 *      below it;
 *   8. a note restarts the phases and filters, so every note starts the same (Plaits never resets them).
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#ifndef GR_DSP_H
#define GR_DSP_H
#include <stdint.h>
#include "fm_dsp.h"                       /* fm_sin, fm_mul16, fm_clamp */
#include "gr_tables.h"

#define GR_BLOCK     128
#define GR_OUT_GAIN  22938                /* Plaits' out_gain for this model, 0.7 (Q15) */
#define GR_F_MAX     582795166u           /* Plaits' kMaxFrequency (0.25 at 47,872 Hz: 11,968 Hz) at 88.2 kHz */
#define GR_C_MAX     291397583u           /* the carrier: half that, 5,984 Hz */
#define GR_C_MIN     (1u << 16)           /* 1.3 Hz: keeps the reset reciprocal in range */

typedef struct {
    uint32_t cph, ph1, ph2;               /* carrier and the two formants */
    uint32_t f1, f2;                      /* the formant increments at the end of the last block */
    int32_t g7, b7;                       /* the ramped gain 4 / (1 + bleed) (Q12) and 2 bleed (Q15), << 7 */
    int32_t shape;                        /* Q15, gliding to the knob */
    int32_t next;                         /* the next sample so far (Q15, with the BLEP's tail) */
    int32_t lp;                           /* the DC blocker's low-pass, Q27 */
    uint8_t start;
} gr_voice;

typedef struct {                          /* worked out once a block */
    uint32_t f1, f2;                      /* formant increments */
    int32_t g, b2;                        /* 4 / (1 + bleed), Q12; 2 bleed, Q15 */
    int32_t shape;                        /* the shape's target, Q15 */
    int32_t dc;                           /* the DC blocker's coefficient, Q15 */
    int32_t recip; int sh;                /* the reset time: t = ((phase >> sh) * recip) >> 15 */
} gr_knobs;

typedef struct { int32_t a0, bp, ma, mb; int sa, sb; } gr_map;   /* out = a0 + min(p, bp) ka + max(p - bp, 0) kb */

static inline void gr_voice_note(gr_voice *v) { v->start = 1; }

/* MIDI note (Q8) -> increment at 88.2 kHz, linear between semitones */
static inline uint32_t gr_note(int32_t n8)
{
    n8 = fm_clamp(n8, 0, 133 * 256 - 1);
    const int i = n8 >> 8;
    const uint32_t a = gr_note_inc[i], b = gr_note_inc[i + 1];
    const uint32_t f = a + ((b - a) >> 8) * (uint32_t)(n8 & 255);
    return f > GR_F_MAX ? GR_F_MAX : f;
}

/* a slope k (Q16, 0 or 0.5 .. 512) as a 16-bit mantissa and a right shift: x k = (x m) >> s */
static inline void gr_slope(int32_t k16, int32_t *m, int *s)
{
    if (k16 <= 0) { *m = 0; *s = 15; return; }
    const int e = 31 - __builtin_clz((uint32_t)k16) - 16;        /* floor(log2 k) */
    int sh = 14 - e;                                               /* m = k 2^sh in 16384 .. 32767 */
    if (sh > 15) sh = 15;
    *m = sh <= 16 ? k16 >> (16 - sh) : k16 << (sh - 16);
    if (*m > 32767) *m = 32767;
    *s = sh;
}

/* Plaits' Carrier(phase, shape) as a piecewise-linear phase map, from the shape (Q15, below 1) */
static inline void gr_map_make(gr_map *c, int32_t shape)
{
    const int32_t s3 = shape * 3, mode = s3 >> 15, fr = s3 & 32767;
    int32_t ka, kb = 0;
    if (mode < 2) {
        const int32_t t = 32768 - fr, t2 = (t * t) >> 15, t3 = (t2 * t) >> 15;    /* (1 - fraction)^3 */
        c->a0 = 24576;                                                            /* + 0.75 */
        if (mode == 0) {                                   /* phase x (1 + 15 t^3), up to 1 */
            ka = 65536 + 30 * t3;
            c->bp = (int32_t)((1u << 31) / (uint32_t)ka);
        } else {                                           /* two slopes meeting at (b, 0.5), b = 0.001 + 0.499 t^3 */
            const int32_t b = 33 + ((16351 * t3) >> 15);
            c->bp = b;
            ka = (int32_t)((1u << 30) / (uint32_t)b);                             /* 0.5 / b */
            kb = (int32_t)((1u << 30) / (uint32_t)(32768 - b));                   /* 0.5 / (1 - b) */
        }
    } else {                                               /* 0.25 + phase x (0.5 + 14.5 t^3), up to 0.75 */
        const int32_t t2 = (fr * fr) >> 15, t3 = (t2 * fr) >> 15;
        c->a0 = 8192;
        ka = 32768 + 29 * t3;
        c->bp = (int32_t)((1u << 30) / (uint32_t)ka);                             /* 0.5 / k */
    }
    if (c->bp > 32767) c->bp = 32767;
    gr_slope(ka, &c->ma, &c->sa);
    gr_slope(kb, &c->mb, &c->sb);
}

/* the carrier at phase p (Q15, 0 .. 32767): (sine(map(p)) + 1) / 4, Q15 (0 .. 0.5) */
static inline __attribute__((always_inline)) int32_t gr_carrier(const int16_t *tab, const gr_map *c, int32_t p)
{
    const int32_t x = p < c->bp ? p : c->bp, y = p - x;
    const int32_t q = c->a0 + (fm_mul16(x, c->ma) >> c->sa) + (fm_mul16(y, c->mb) >> c->sb);
    return (fm_sin(tab, (uint32_t)q << 17) + 32767) >> 2;
}

/* both grainlets: carrier c (Q15) x (sine 1 + sine 2 + 2 bleed) / (1 + bleed), Q15 */
static inline __attribute__((always_inline)) int32_t gr_pair(const int16_t *tab, int32_t c, uint32_t ph1, uint32_t ph2,
                                                             int32_t b2, int32_t g)
{
    const int32_t s4 = (fm_sin(tab, ph1) + fm_sin(tab, ph2) + b2) >> 2;          /* (s1 + s2 + 2 bleed) / 4 */
    return fm_mul16(fm_mul16(c, s4) >> 15, g) >> 12;
}

/* the carrier's period restarts within this sample: a BLEP for the step (Plaits' grainlet reset), and the formants
 * restart at the time since the restart. Out of the loop's line: it runs once a period. */
static __attribute__((noinline)) void gr_restart(const int16_t *tab, const gr_map *c, const gr_knobs *k, uint32_t cph,
                                                 uint32_t *ph1, uint32_t *ph2, uint32_t f1, uint32_t f2, int32_t b2,
                                                 int32_t g, int32_t *this_s, int32_t *next)
{
    const uint32_t q = cph >> k->sh;
    int32_t t = q >= 32768u ? 32767 : (int32_t)((q * (uint32_t)k->recip) >> 15);
    if (t > 32767) t = 32767;                                     /* the time since the restart, in samples (Q15) */
    const uint32_t u = (uint32_t)(32768 - t);
    const int32_t before = gr_pair(tab, gr_carrier(tab, c, 32767), *ph1 + (f1 >> 15) * u, *ph2 + (f2 >> 15) * u, b2, g);
    const int32_t after = gr_pair(tab, gr_carrier(tab, c, 0), 0, 0, b2, g);
    const int32_t disc = after - before;
    *this_s += (disc * ((t * t) >> 16)) >> 15;                                    /* + d t^2 / 2 */
    *next -= (disc * (int32_t)((u * u) >> 16)) >> 15;                            /* - d (1 - t)^2 / 2 */
    *ph1 = (f1 >> 15) * (uint32_t)t;
    *ph2 = (f2 >> 15) * (uint32_t)t;
}

/* harm, timbre, morph: knob words 0 .. 32767; inc0: the block's first increment at 88.2 kHz */
static inline void gr_knobs_make(gr_knobs *k, int32_t harm, int32_t timbre, int32_t morph, uint32_t inc0)
{
    harm = fm_clamp(harm, 0, 32767); timbre = fm_clamp(timbre, 0, 32767); morph = fm_clamp(morph, 0, 32767);
    uint32_t f = inc0 > GR_C_MAX ? GR_C_MAX : inc0 < GR_C_MIN ? GR_C_MIN : inc0;

    const int32_t n1 = 6144 + ((timbre * 21504) >> 15);                 /* 24 + 84 timbre, Q8 */
    k->f1 = gr_note(n1);
    k->f2 = gr_note(n1 - 6144 + ((harm * 12288) >> 15));                 /* + (-24 + 48 harmonics) */

    const int32_t cb = harm < 16384 ? 32767 - 2 * harm : 0;              /* carrier bleed: 1 - 2 harmonics */
    const int32_t bleed = (cb * (65536 - cb)) >> 15;                     /* cb (2 - cb) */
    k->b2 = 2 * bleed;
    k->g = (int32_t)((16384u * 32768u) / (uint32_t)(32768 + bleed));    /* 4 / (1 + bleed), Q12 */

    /* carrier_shape = 0.33 + (morph - 0.33) max(1 - 24 f0, 0), f0 the note at 47,872 Hz */
    const int32_t low = 32768 - (int32_t)(((f >> 16) * 22640u) >> 10);
    k->shape = fm_clamp(10813 + (((morph - 10813) * (low > 0 ? low : 0)) >> 15), 0, 32767);

    const uint32_t dc = ((f >> 16) * 30884u) >> 15;                      /* 2 pi 0.3 f, Q15 */
    k->dc = dc > 32767 ? 32767 : (int32_t)dc;

    const int sh = 31 - __builtin_clz(f) - 14;                           /* f >> sh in 2^14 .. 2^15 */
    k->sh = sh;
    k->recip = (int32_t)((1u << 30) / (f >> sh));
}

/* render n (<= 128) samples at 88.2 kHz; inc: per-sample increments; gain: Q15 level, Plaits' out_gain applied here */
static inline void gr_render(gr_voice *v, const gr_knobs *k, const int32_t *inc, int n, int32_t gain, int16_t *out,
                             const int16_t *tab)
{
    if (v->start) {
        v->cph = v->ph1 = v->ph2 = 0; v->next = 0; v->lp = 0;
        v->f1 = k->f1; v->f2 = k->f2; v->g7 = k->g << 7; v->b7 = k->b2 << 7; v->shape = k->shape;
        v->start = 0;
    }
    v->shape += (k->shape - v->shape) >> 1;
    gr_map c;
    gr_map_make(&c, v->shape);

    const int32_t go = (gain * GR_OUT_GAIN) >> 15;
    uint32_t f1 = v->f1, f2 = v->f2;
    const int32_t d1 = ((int32_t)(k->f1 - f1)) >> 7, d2 = ((int32_t)(k->f2 - f2)) >> 7;
    int32_t g7 = v->g7, b7 = v->b7;
    const int32_t dg = k->g - (g7 >> 7), db = k->b2 - (b7 >> 7);
    uint32_t cph = v->cph, ph1 = v->ph1, ph2 = v->ph2;
    int32_t next = v->next, lp = v->lp;

    for (int j = 0; j < n; j++) {
        f1 += (uint32_t)d1; f2 += (uint32_t)d2; g7 += dg; b7 += db;
        const int32_t g = g7 >> 7, b2 = b7 >> 7;
        const uint32_t ci = (uint32_t)inc[j] > GR_C_MAX ? GR_C_MAX : (uint32_t)inc[j];
        int32_t this_s = next;
        next = 0;
        const uint32_t was = cph;
        cph += ci;
        if (__builtin_expect(cph < was, 0)) {             /* the carrier's period restarts (once a period) */
            gr_restart(tab, &c, k, cph, &ph1, &ph2, f1, f2, b2, g, &this_s, &next);
        } else {
            ph1 += f1; ph2 += f2;
        }
        next += gr_pair(tab, gr_carrier(tab, &c, (int32_t)(cph >> 17)), ph1, ph2, b2, g);

        /* DC blocker: x - lowpass(x) */
        const int32_t x = fm_clamp(this_s, -32767, 32767);
        lp += fm_mul16((x - (lp >> 12)) >> 1, k->dc) >> 2;
        const int32_t y = fm_clamp(x - (lp >> 12), -32767, 32767);
        out[j] = (int16_t)fm_clamp(fm_mul16(y, go) >> 15, -32767, 32767);
    }
    v->cph = cph; v->ph1 = ph1; v->ph2 = ph2; v->next = next; v->lp = lp;
    if (n == GR_BLOCK) { v->f1 = k->f1; v->f2 = k->f2; v->g7 = k->g << 7; v->b7 = k->b2 << 7; }
    else { v->f1 = f1; v->f2 = f2; v->g7 = g7; v->b7 = b7; }
}

#endif
