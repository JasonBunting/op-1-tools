/* Norn's wave terrain: Plaits' "wave terrain" model (plaits/dsp/engine2/wave_terrain_engine.cc, Emilie Gillet, MIT;
 * see NOTICE) in integers for the OP-1's 88.2 kHz voices. OUT only (Plaits' AUX, a sine of the path plus height, is
 * not made).
 *
 * A point circles at the note's pitch over a surface z = f(x, y); its height is the output. The knobs (Plaits names):
 *   harmonics : the terrain, morphing through eight (five formulas, three made of Plaits' wavetables)
 *   timbre    : the circle's radius (smaller on high notes)
 *   morph     : the circle's centre, moved along x (which also shrinks it, keeping it on the surface)
 *
 * Numbers: x, y, heights and knobs are Q15; phases are Q32 (2^32 = one cycle); the terrain position is Q12. Changes
 * from Plaits, all made for the integer port:
 *   1. one terrain evaluation per sample at 88.2 kHz (Plaits: two at 95.7 kHz, averaged, for its 47.9 kHz output);
 *   2. the path is an exact circle from the OP-1's sine table (Plaits: a "magic circle" oscillator, slightly
 *      elliptical, its frequency within 7 cents); a note starts it at angle 0;
 *   3. each division in the formulas reads a 257-point table of the quotient over its input's range (wt_tables.h),
 *      and Squash(Sine(1.273 x)) is one 513-point table; the sines read the OP-1's 8,192-point table at the nearest
 *      point; every multiply is 16 x 16;
 *   4. the terrain pair is chosen once a block: the position glides across the block, at most half a terrain a block,
 *      and past the pair's edge holds the edge until the next block;
 *   5. a DC blocker (10 Hz) on the output, started at the first sample, since the terrains are not centred and an
 *      offset would thump with the envelope.
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#ifndef WT_DSP_H
#define WT_DSP_H
#include <stdint.h>
#include "fm_dsp.h"                       /* fm_sin, fm_mul16, fm_clamp */
#include "wt_tables.h"

#define WT_BLOCK     128
#define WT_OUT_GAIN  22938                /* Plaits' out_gain for this model, 0.7 (Q15) */
#define WT_DC        23                   /* the DC blocker: 2 pi 10 Hz / 88.2 kHz, Q15 */
#define WT_TERRAINS  8
#define WT_Z_STEP    2048                 /* the most the terrain position moves in a block, Q12 */

typedef struct {
    uint32_t ph;                          /* the path's angle */
    int32_t r, off, z;                    /* radius, centre (Q15) and terrain position (Q12) at the last block's end */
    int32_t lp;                           /* the DC blocker's low-pass, Q27 */
    uint8_t start;
} wt_voice;

typedef struct {                          /* worked out once a block */
    int32_t r, off, z;
} wt_knobs;

static inline void wt_voice_note(wt_voice *v) { v->start = 1; }

/* harm, timbre, morph: knob words 0 .. 32767; inc: the block's last increment at 88.2 kHz */
static inline void wt_knobs_make(wt_knobs *k, int32_t harm, int32_t timbre, int32_t morph, uint32_t inc)
{
    harm = fm_clamp(harm, 0, 32767); timbre = fm_clamp(timbre, 0, 32767); morph = fm_clamp(morph, 0, 32767);
    /* f0 as cycles a sample at 48 kHz (Plaits' rate), Q15: inc x 88200 / 48000 */
    const int32_t f0 = (int32_t)(((inc >> 16) * 120422u) >> 17);
    const int32_t att = fm_clamp(32767 - f0 * 8, 0, 32767);                         /* 1 - 8 f0 */
    const int32_t a2 = (att * (65536 - att)) >> 15;                                  /* att (2 - att) */
    int32_t r = 3277 + ((((timbre * a2) >> 15) * 29491) >> 15);                     /* 0.1 + 0.9 timbre att (2 - att) */
    r = (r * fm_clamp(32767 - f0 * 2, 0, 32767)) >> 15;                             /* the path's own 1 - 4 f (f = f0 / 2) */
    k->r = r > 32767 ? 32767 : r;                                                 /* fits int16 */
    k->off = morph * 2 - 32767 - ((morph * 3277) >> 15);                            /* 1.9 morph - 1 */
    const int32_t h = harm + ((harm * 1638) >> 15);                                 /* 1.05 harmonics */
    k->z = ((h > 32767 ? 32767 : h) * 28671) >> 15;                                 /* x 6.9999, Q12 */
}

/* table t (257 or 513 points, step 2^sh in pos units) at pos, linear */
static inline __attribute__((always_inline)) int32_t wt_lerp(const int16_t *t, int32_t pos, int sh)
{
    const int i = pos >> sh;
    const int32_t fr = (pos & ((1 << sh) - 1)) << (15 - sh);
    return t[i] + (fm_mul16(t[i + 1] - t[i], fr) >> 15);
}

/* the terrains, one block at a time: x, y in, z out (Q15). Plaits' Sine(k + v) is sin(2 pi v): v in cycles. */
static void __attribute__((noinline)) wt_terrain(int which, const int16_t *xs, const int16_t *ys, int16_t *zs, int n,
                                                 const int16_t *tab)
{
    int j;
    switch (which) {
    case 0:   /* (Squash(Sine(1.273 x), 2) - Sine(y (x + 1.571) 0.637)) x 0.57 */
        for (j = 0; j < n; j++) {
            const int32_t x = xs[j], y = ys[j];
            const int32_t h = wt_lerp(wt_sq, x + 32768, 7);
            const int32_t xq = (fm_mul16(x, 20873) >> 16) + 16396;                 /* 0.637 x + 1.0007, Q14 */
            const int32_t s = fm_sin(tab, (uint32_t)fm_mul16(y, xq) << 3);          /* Q29 cycles */
            zs[j] = (int16_t)fm_clamp(fm_mul16((h - s) >> 1, 18678) >> 14, -32767, 32767);
        }
        break;
    case 1:   /* Sine(Sine((x + y) 0.637) / (0.2 + xy^2) x 0.159) */
        for (j = 0; j < n; j++) {
            const int32_t x = xs[j], y = ys[j];
            const int32_t xy = fm_mul16(x, y) >> 15;
            const int32_t r = wt_lerp(wt_r1, fm_mul16(xy, xy) >> 15, 7);
            const int32_t s = fm_sin(tab, (uint32_t)fm_mul16((x + y) >> 1, 20873) << 3);   /* Q29 cycles */
            zs[j] = (int16_t)fm_sin(tab, (uint32_t)fm_mul16(s, r) << 2);            /* Q30 cycles */
        }
        break;
    case 2:   /* Sine(Sine(2.387 xy) / (0.35 + xy^2) x 0.159) */
        for (j = 0; j < n; j++) {
            const int32_t xy = fm_mul16(xs[j], ys[j]) >> 15;
            const int32_t r = wt_lerp(wt_r2, fm_mul16(xy, xy) >> 15, 7);
            const int32_t s = fm_sin(tab, (uint32_t)fm_mul16(xy, 19554) << 4);      /* 2.387 Q13: Q28 cycles */
            zs[j] = (int16_t)fm_sin(tab, (uint32_t)fm_mul16(s, r) << 2);
        }
        break;
    case 3:   /* Sine(xy / (2 + |5 (x - 0.25)(y + 0.25)|) x 6.366) */
        for (j = 0; j < n; j++) {
            const int32_t x = xs[j], y = ys[j];
            const int32_t xy = fm_mul16(x, y) >> 15;
            const int32_t w = __builtin_abs(fm_mul16((x - 8192) >> 1, (y + 8192) >> 1)) >> 13;   /* Q15, 0 .. 1.56 */
            const int32_t r = wt_lerp(wt_r3, w, 8);                                  /* Q12 */
            zs[j] = (int16_t)fm_sin(tab, (uint32_t)fm_mul16(xy, r) << 5);           /* Q27 cycles */
        }
        break;
    case 4:   /* Sine(0.159 / (0.17 + |y - 0.25|) + 0.477 / (0.35 + |(x + 0.5)(y + 1.5)|)) */
        for (j = 0; j < n; j++) {
            const int32_t x = xs[j], y = ys[j];
            const int32_t a = wt_lerp(wt_r4a, __builtin_abs(y - 8192), 8);          /* Q15 */
            const int32_t w = __builtin_abs(fm_mul16((x + 16384) >> 1, (y + 49152) >> 2)) >> 12;  /* Q15, 0 .. 3.75 */
            const int32_t b = wt_lerp(wt_r4b, w, 9);                                 /* Q14 */
            zs[j] = (int16_t)fm_sin(tab, ((uint32_t)a << 17) + ((uint32_t)b << 18));
        }
        break;
    default:  /* 5, 6, 7: Plaits' wavetable banks 2, 1, 0 as a terrain: x picks the wave, y the place in it */
        {
            const int16_t (*bank)[130] = wt_waves[7 - which];
            for (j = 0; j < n; j++) {
                const int32_t xp = (xs[j] + 32768) * 63;                            /* the wave, Q16 */
                const int32_t yp = ys[j] + 32768;                                    /* the sample, Q9 */
                const int16_t *w0 = bank[xp >> 16], *w1 = w0 + 130;
                const int si = yp >> 9;
                const int32_t sf = (yp & 511) << 6, wf = (xp & 65535) >> 1;
                const int32_t v0 = w0[si] + (fm_mul16(w0[si + 1] - w0[si], sf) >> 15);
                const int32_t v1 = w1[si] + (fm_mul16(w1[si + 1] - w1[si], sf) >> 15);
                const int32_t v = v0 + (fm_mul16(v1 - v0, wf) >> 15);              /* 1024 = full scale */
                zs[j] = (int16_t)fm_clamp(v << 5, -32767, 32767);
            }
        }
        break;
    }
}

/* render n (<= 128, a power of two) samples at 88.2 kHz; inc: per-sample increments; gain: Q15 level, Plaits'
 * out_gain applied here; tab: the quarter sine (op1_sin_tab() on the device, fm_sine_q on a host) */
static inline void wt_render(wt_voice *v, const wt_knobs *k, const int32_t *inc, int n, int32_t gain, int16_t *out,
                             const int16_t *tab)
{
    static int16_t xs[WT_BLOCK], ys[WT_BLOCK], za[WT_BLOCK], zb[WT_BLOCK];   /* voices render one at a time */
    if (v->start) {
        v->ph = 0; v->r = k->r; v->off = k->off; v->z = k->z;
    }
    const int lg = 31 - __builtin_clz((unsigned)n);
    int j;

    /* the path: radius and centre glide across the block */
    int32_t r = v->r << lg, off = v->off << lg;
    const int32_t dr = k->r - v->r, doff = k->off - v->off;
    uint32_t ph = v->ph;
    for (j = 0; j < n; j++) {
        r += dr; off += doff;
        const int32_t rr = r >> lg, o = off >> lg;
        const int32_t px = fm_mul16(fm_sin(tab, ph + (1u << 30)), rr) >> 15;     /* r cos */
        const int32_t py = fm_mul16(fm_sin(tab, 0u - ph), rr) >> 15;             /* -r sin: Plaits' direction */
        ph += (uint32_t)inc[j];
        xs[j] = (int16_t)((fm_mul16(px, 32767 - __builtin_abs(o)) >> 15) + o);   /* x (1 - |offset|) + offset */
        ys[j] = (int16_t)py;
    }
    v->ph = ph;

    /* the terrain position moves at most half a terrain a block (a jump end to end takes about 20 ms); the pair is
     * the one below the block's lower position, and the fraction glides, held to 0 .. 1 */
    const int32_t z0 = v->z, z1 = fm_clamp(k->z, z0 - WT_Z_STEP, z0 + WT_Z_STEP);
    int pair = (z0 < z1 ? z0 : z1) >> 12;
    if (pair > WT_TERRAINS - 2) pair = WT_TERRAINS - 2;
    wt_terrain(pair, xs, ys, za, n, tab);
    wt_terrain(pair + 1, xs, ys, zb, n, tab);

    int32_t z = (z0 - (pair << 12)) << lg;
    const int32_t dz = z1 - z0;
    const int32_t g = (gain * WT_OUT_GAIN) >> 15;
    int32_t lp = v->lp;
    if (v->start) {                                              /* the blocker starts at the first sample: no thump */
        const int32_t fr = fm_clamp((z0 - (pair << 12)) << 3, 0, 32767);
        lp = (za[0] + (fm_mul16((zb[0] - za[0]) >> 1, fr) >> 14)) << 12;
        v->start = 0;
    }
    for (j = 0; j < n; j++) {
        z += dz;
        const int32_t fr = fm_clamp((z >> lg) << 3, 0, 32767);                    /* Q15 */
        const int32_t a = za[j], b = zb[j];
        const int32_t x = a + (fm_mul16((b - a) >> 1, fr) >> 14);
        lp += fm_mul16((x - (lp >> 12)) >> 1, WT_DC) >> 2;
        const int32_t y = fm_clamp(x - (lp >> 12), -32767, 32767);
        out[j] = (int16_t)(fm_mul16(y, g) >> 15);
    }
    v->lp = lp;
    v->r = k->r; v->off = k->off; v->z = z1;
    if (n != WT_BLOCK) { v->r = r >> lg; v->off = off >> lg; v->z = (z >> lg) + (pair << 12); }
}

#endif
