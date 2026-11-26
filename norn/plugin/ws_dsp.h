/* Norn's waveshaper: Plaits' "waveshaping" model (plaits/dsp/engine/waveshaping_engine.cc, Emilie Gillet, MIT; see
 * NOTICE) in integers for the OP-1's 88.2 kHz voices. OUT only (Plaits' AUX, the sine / second fold, is not made).
 *
 * The chain, per sample: a band-limited slope (Plaits' Oscillator, OSCILLATOR_SHAPE_SLOPE: a triangle whose peak sits at
 * pw, with integrated-BLEP corners) -> one of five waveshaper curves, morphed (ws_shape) -> the wavefolder (ws_fold,
 * Hermite). The knobs (Plaits names):
 *   harmonics : the waveshaper curve, and how far from linear (centre = linear)
 *   timbre    : the wavefolder's drive
 *   morph     : the slope's peak, triangle (left) to ramp (right)
 *
 * Numbers: phase and pw are Q32 (2^32 = one cycle); the slope's samples are Q30; the knobs, curves and fold are Q15.
 * Plaits ran at 48 kHz in 12-sample blocks: here a block is 128 samples at 88.2 kHz. Changes from Plaits, all made for
 * the integer port:
 *   1. pw and the slopes (1 / pw, 1 / (1 - pw)) change once a block, pw gliding a quarter of the way to the knob each
 *      block (about 6 ms), not ramped per sample (no division per sample);
 *   2. the corner time t = overshoot / frequency uses the block's first frequency (one reciprocal a block), clamped
 *      to 0 .. 1;
 *   3. the anti-alias taming (Tame) sees the frequency as Plaits would at 48 kHz, so the knobs give the same
 *      sound at the same pitch;
 *   4. the waveshaper index is clamped to the curve instead of wrapping;
 *   5. a note starts the slope at its midpoint (output 0), so the T2 attack starts from silence, not -1;
 *   6. every per-sample multiply is 16 x 16 (one cycle on the Blackfin): the curves' difference is halved first, and
 *      the falling slope is scaled per block to fit; the corners are computed out of line (ws_corners).
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#ifndef WS_DSP_H
#define WS_DSP_H
#include <stdint.h>
#include "ws_tables.h"
#include "fm_dsp.h"                       /* fm_mul16, fm_mulu16 */

#define WS_RATE   88200
#define WS_BLOCK  128
#define WS_F_MAX  (1u << 30)             /* Plaits' kMaxFrequency, 0.25 cycle a sample */
#define WS_F_MIN  (1u << 16)             /* 1.3 Hz: keeps the corner reciprocal in range */
#define WS_OUT_GAIN 22938                /* Plaits' out_gain for this model, 0.7 (Q15) */

typedef struct {
    uint32_t phase;                      /* Q32 */
    uint32_t pw;                         /* Q32: the slope's peak, a quarter of the way to the knob each block */
    int32_t next;                        /* Q30: the slope's next sample, with the BLEP tail */
    int32_t shape, fold;                 /* Q15: the ramped values at the end of the last block */
    uint8_t high;                        /* 1 while phase < pw (rising) */
    uint8_t start;                       /* a note: start the slope and the ramps fresh */
} ws_voice;

typedef struct {                         /* worked out once a block */
    uint32_t pw;                         /* Q32, 0.5 .. 0.95: the target; the voice glides to it (ws_render) */
    uint32_t f;                          /* the block's first increment, clamped */
    int32_t recip;                       /* (1 << 30) / (f >> sh): t = ((over >> sh) * recip) >> 15 */
    int sh;
    int32_t shape, fold;                 /* Q15 targets */
} ws_knobs;

static inline int32_t ws_clamp(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline int32_t ws_abs(int32_t x) { return x < 0 ? -x : x; }

static inline void ws_voice_note(ws_voice *v) { v->start = 1; }

/* Plaits' Tame: 1 below 0.5 / order of the (frequency x slope), falling to 0 at Nyquist, cubed. f is cycles a sample
 * at 48 kHz (Q16), slope Q8. Q15. */
static inline int32_t ws_tame(int32_t f, int32_t slope, int order)
{
    f = (f * slope) >> 8;                                        /* f < 2^15, slope < 2^14 */
    const int32_t max_f = 32768 / order;
    if (f <= max_f) return 32767;
    if (f >= 32768) return 0;
    int32_t a = 32768 - (int32_t)(((f - max_f) << 15) / (32768 - max_f));
    a = ws_clamp(a, 0, 32767);
    return (int32_t)((((a * a) >> 15) * a) >> 15);
}

/* harm, timbre, morph: knob words 0 .. 32767; inc0: the block's first phase increment */
static inline void ws_knobs_make(ws_knobs *k, int32_t harm, int32_t timbre, int32_t morph, uint32_t inc0)
{
    harm = ws_clamp(harm, 0, 32767); timbre = ws_clamp(timbre, 0, 32767); morph = ws_clamp(morph, 0, 32767);
    uint32_t f = inc0 > WS_F_MAX ? WS_F_MAX : inc0 < WS_F_MIN ? WS_F_MIN : inc0;

    k->pw = 0x80000000u + (uint32_t)morph * 58982u;           /* pw = 0.5 + 0.45 morph */
    k->f = f;

    int sh = 31 - __builtin_clz(f) - 14;                 /* f >> sh in 2^14 .. 2^15 */
    k->sh = sh;
    k->recip = (int32_t)((1u << 30) / (f >> sh));

    /* Plaits' block math with f0 as at 48 kHz: f0_48 = f x 88200 / 48000 */
    const int32_t f0 = (int32_t)(((f >> 16) * 120422u) >> 16);                    /* Q16 */
    const int32_t slope = 768 + ((ws_abs(morph - 16384) * 1280) >> 15);              /* Q8: 3 + |morph - 0.5| x 5 */
    const int32_t amount = ws_clamp(ws_abs(harm - 16384) * 2, 0, 32767);             /* |harmonics - 0.5| x 2 */
    const int32_t shape_att = ws_tame(f0, slope, 16);
    const int32_t inner = 768 + ((((amount * shape_att) >> 15) * 1280) >> 15);       /* Q8: 3 + amount x att x 5 */
    const int32_t fold_att = ws_tame(f0, (slope * inner) >> 8, 12);
    k->shape = 16384 + (((harm - 16384) * shape_att) >> 15);
    k->fold = 983 + ((((timbre * fold_att) >> 15) * 15073) >> 15);                 /* 0.03 + 0.46 x timbre x att */
}

/* Plaits' integrated-BLEP halves, Q15 in and out: next(t) = 0.1875 - t/2 + 1.5 (t/2)^2 - (t/2)^4 */
static inline int32_t ws_blep_next(int32_t t)
{
    const int32_t t1 = t >> 1, t2 = (t1 * t1) >> 15, t4 = (t2 * t2) >> 15;
    return 6144 - t1 + ((3 * t2) >> 1) - t4;
}
#define ws_blep_this(t) ws_blep_next(32768 - (t))

static inline int32_t ws_corner_t(const ws_knobs *k, uint32_t over)
{
    const uint32_t q = over >> k->sh;                            /* past a whole sample (pw moved, a glide): t = 1 */
    if (q >= 32768u) return 32767;
    int32_t t = (int32_t)((q * (uint32_t)k->recip) >> 15);
    return t > 32767 ? 32767 : t;
}

/* the slope's corners, twice a cycle, out of the per-sample path: the BLEP into this sample (*this_s) and the next
 * (*blep); returns the new rising flag */
static __attribute__((noinline)) int ws_corners(const ws_knobs *k, uint32_t phase, uint32_t pw, int high, int below,
                                                int wrapped, int32_t disc, int32_t *this_s, int32_t *blep)
{
    if (high ^ below) {                                          /* past the peak (or pw moved): the corner */
        const int32_t t = ws_corner_t(k, phase - pw);
        *this_s -= ws_blep_this(t) * disc;
        *blep -= ws_blep_next(t) * disc;
        high = below;
    }
    if (wrapped) {                                               /* the cycle's start: the corner at the bottom */
        const int32_t t = ws_corner_t(k, phase);
        *this_s += ws_blep_this(t) * disc;
        *blep += ws_blep_next(t) * disc;
        high = 1;
    }
    return high;
}

/* the waveshaper curve: s in -1 .. 1 (Q15) through curve c, linear between points (neighbours differ by under 6,000) */
static inline __attribute__((always_inline)) int32_t ws_curve(const int16_t *c, int32_t i, int32_t fr)
{
    return c[i] + (fm_mul16(c[i + 1] - c[i], fr) >> 15);
}

/* the wavefolder: Plaits' InterpolateHermite(lut_fold + 1, index, 512), index Q15 in 0 .. 1; every term fits 16 bits
 * for this table (a, b under 128) */
static inline __attribute__((always_inline)) int32_t ws_fold_at(int32_t index)
{
    const int i = index >> 6;
    const int32_t f = (index & 63) << 9;
    const int16_t *p = ws_fold + i;
    const int32_t xm1 = p[0], x0 = p[1], x1 = p[2], x2 = p[3];
    const int32_t c = (x1 - xm1) >> 1, v = x0 - x1, w = c + v, a = w + v + ((x2 - x0) >> 1), b = w + a;
    const int32_t u = (fm_mul16(a, f) >> 15) - b;
    const int32_t u2 = (fm_mul16(u, f) >> 15) + c;
    return (fm_mul16(u2, f) >> 15) + x0;
}

/* n samples (n = WS_BLOCK on the OP-1, a power of two): inc[j] the phase increment of each sample, gain Q15
 * (out_gain is applied too). Every per-sample multiply is 16 x 16; the corners go through ws_corners. */
static inline void ws_render(ws_voice *v, const ws_knobs *k, const int32_t *inc, int n, int32_t gain, int16_t *out)
{
    /* the slope's peak glides to the knob (a jump would step the waveform at the block's edge), kept 2f from the top
     * (Plaits' CONSTRAIN; the bottom never binds); its slopes 1 / pw and 1 / (1 - pw) and corner size from it */
    uint32_t pw = v->start ? k->pw : v->pw + (uint32_t)((int32_t)(k->pw - v->pw) >> 2);
    if (pw > 0u - 2u * k->f) pw = 0u - 2u * k->f;
    v->pw = pw;
    const int32_t up = (int32_t)((1u << 30) / (pw >> 16));                       /* Q14, 2^14 .. 2^15: fits uint16 */
    const int32_t down = (int32_t)((1u << 30) / ((0u - pw) >> 16));              /* Q14, 2^15 .. 20 x 2^14 */
    const int32_t disc = (int32_t)(((up + down) * (k->f >> 16)) >> 15);          /* Q14 x Q16 -> Q15, at most 1 */
    /* the falling side as (phase - pw) >> fsh times dn, dn in 2^15 .. 2^16 (uint16); (phase - pw) >> fsh stays below
     * 2^30 / dn, so it fits too */
    const int dsh = 31 - __builtin_clz((uint32_t)down) - 15;
    const uint32_t dn = (uint32_t)down >> dsh;
    const int fsh = 16 - dsh;
    if (v->start) {                                              /* the slope's midpoint, rising; the ramps at target */
        v->start = 0;
        v->phase = pw >> 1;
        v->next = (int32_t)fm_mulu16(v->phase >> 16, (uint32_t)up);
        v->high = 1;
        v->shape = k->shape;
        v->fold = k->fold;
    }
    uint32_t phase = v->phase;
    int32_t next = v->next;
    int high = v->high;
    const int lg = 31 - __builtin_clz((unsigned)n);
    int32_t shape = v->shape << lg, fold = v->fold << lg;         /* linear ramps to the targets over the block */
    const int32_t dshape = k->shape - v->shape, dfold = k->fold - v->fold;
    const int32_t g = (gain * WS_OUT_GAIN) >> 15;

    for (int j = 0; j < n; j++) {
        int32_t this_s = next, blep = 0;
        const uint32_t f = (uint32_t)(inc[j] < (int32_t)WS_F_MAX ? inc[j] : (int32_t)WS_F_MAX);
        const uint32_t old = phase;
        phase += f;
        const int wrapped = phase < old;
        const int below = !wrapped & (phase < pw);
        if (__builtin_expect((high ^ below) | wrapped, 0))
            high = ws_corners(k, phase, pw, high, below, wrapped, disc, &this_s, &blep);
        const int32_t rise = (int32_t)fm_mulu16(phase >> 16, (uint32_t)up);
        const int32_t fall = (1 << 30) - (int32_t)fm_mulu16((phase - pw) >> fsh, dn);
        next = blep + fall + ((rise - fall) & -high);            /* high ? rise : fall, no branch */
        const int32_t x = (this_s >> 14) - 32768;                /* 2 x - 1, Q15 */

        shape += dshape; fold += dfold;
        const int32_t s15 = shape >> lg;
        const int32_t sh = s15 * 4 - (s15 >> 13);                /* x 3.9999 */
        const int si = sh >> 15;
        const int32_t sfr = sh & 32767;
        const int32_t idx = ws_clamp(x * 127 + (128 << 15), 0, (256 << 15) - 1);
        const int wi = idx >> 15;
        const int32_t wfr = idx & 32767;
        const int32_t a = ws_curve(ws_shape[si], wi, wfr), b = ws_curve(ws_shape[si + 1], wi, wfr);
        const int32_t mix = a + (fm_mul16((b - a) >> 1, sfr) >> 14);   /* curves differ by up to 43,107: halved */
        const int32_t index = ws_clamp((fm_mul16(mix, fold >> lg) >> 15) + 16384, 0, 32767);
        const int32_t y = ws_clamp(ws_fold_at(index), -32767, 32767);
        out[j] = (int16_t)(fm_mul16(y, g) >> 15);
    }
    v->phase = phase;
    v->next = next;
    v->high = (uint8_t)high;
    v->shape = k->shape;
    v->fold = k->fold;
}

#endif
