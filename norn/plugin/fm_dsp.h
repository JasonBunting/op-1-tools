/* Norn's FM: Plaits' "two operator FM" model (plaits/dsp/engine/fm_engine.cc, Emilie Gillet, MIT; see NOTICE) in
 * integers for the OP-1's 88.2 kHz voices. OUT only (Plaits' AUX, the sub-octave sine, is not made).
 *
 * The chain, per oversampled step: a modulator sine (with feedback) phase-modulates a carrier sine at the note's pitch.
 * The knobs (Plaits names):
 *   harmonics : the modulator / carrier frequency ratio, quantized to musical ratios (fm_ratio)
 *   timbre    : the modulation index
 *   morph     : feedback; left of centre the modulator's frequency follows the output, right of centre its phase does
 *
 * Numbers: phases and increments are Q32 (2^32 = one cycle); sines, knobs and feedback are Q15; the index is Q14
 * cycles. Changes from Plaits, all made for the integer port:
 *   1. 2x oversampling at 176.4 kHz (Plaits: 4x at 47.9 kHz, 191.5 kHz), down to 88.2 kHz with a [1 3 3 1] / 8 FIR;
 *      the output's feedback filter is rescaled for the rate;
 *   2. the sines read the OP-1's 8,192-point table (the core keeps a quarter of it in L1) at the nearest point, where
 *      Plaits interpolates a 512-point one: no multiply, an error under -62 dB; every multiply is 16 x 16;
 *   3. the carrier follows the OP-1's increment sample by sample; the modulator, index and feedback ramp linearly
 *      across the 128-sample block (Plaits ramps them across its 12-sample block);
 *   4. the anti-alias taming of the index (above a 2,093 Hz modulator) uses a log2 approximation (within 0.1
 *      semitone);
 *   5. a note restarts both phases and the feedback at 0, so every note starts the same (Plaits never resets them).
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#ifndef FM_DSP_H
#define FM_DSP_H
#include <stdint.h>
#include "fm_tables.h"

#define FM_BLOCK     128
#define FM_OUT_GAIN  19661                /* Plaits' out_gain for this model, 0.6 (Q15) */
#define FM_MOD_MAX   0x7FFF0000u          /* the modulator below the oversampled Nyquist; mod >> 16 fits int16 */
#define FM_CAR_MAX   (1u << 28)           /* the carrier increment kept below 2^28 (12 kHz) for fm_mul_q16 */
#define FM_TAME_INC  50960237u            /* 2,093 Hz (Plaits' note 96) at 176.4 kHz: taming starts here */
#define FM_LP2       3546                 /* Plaits' ONE_POLE 0.05 at 191.5 kHz, rescaled to 176.4 kHz (Q15), x 2 */

typedef struct {
    uint32_t cph, mph;                    /* carrier, modulator phase */
    uint32_t mod;                         /* the modulator increment at the end of the last block */
    int32_t amount, pf, mfb;              /* the index (Q14 cycles), frequency and phase feedback (Q15), ramped */
    int32_t prev;                         /* the output, low-passed, fed back (Q15) */
    int32_t p0, p1;                       /* the FIR's last two oversampled samples */
    uint8_t start;
} fm_voice;

typedef struct {                          /* worked out once a block */
    uint32_t mod;                         /* the modulator increment, oversampled, at the block's end */
    int32_t amount, pf, mfb;
} fm_knobs;

static inline void fm_voice_note(fm_voice *v) { v->start = 1; }

static inline int32_t fm_clamp(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : x > hi ? hi : x; }

/* the sine of a Q32 phase, Q15: the OP-1's 8,192-point table, nearest point, no interpolation (error under -62 dB). tab
 * is a quarter wave, entries 0 .. 2048 (op1_sin_tab(): the core's copy in L1 on the device; fm_sine_q on a host),
 * folded as op1_sin_q does. No multiply. */
static inline __attribute__((always_inline)) int32_t fm_sin(const int16_t *tab, uint32_t ph)
{
    const uint32_t k = (ph + (1u << 18)) >> 19;                         /* one turn = 8192, rounded */
    const int32_t i = (int32_t)(k << 20) >> 20;                         /* -2048 .. 2047 around the half-wave's peak */
    const int32_t n = (int32_t)(k << 19) >> 31;                         /* -1 in the negative half-wave */
    const int32_t v = tab[__builtin_abs(i)];                            /* ABS: no branch */
    return (v ^ n) - n;
}

/* 16 x 16 -> 32: both operands must fit int16. On the Blackfin, the one-cycle 16-bit multiply written out: GCC picks it
 * from plain C in some loops and a sign extension plus a multi-cycle 32-bit multiply in others (the formant loop). */
static inline __attribute__((always_inline)) int32_t fm_mul16(int32_t a, int32_t b)
{
#if defined(__bfin__)
    int32_t r;
    __asm__("%0 = %1.L * %2.L (IS);" : "=d"(r) : "d"(a), "d"(b));
    return r;
#else
    return (int32_t)(int16_t)a * (int16_t)b;
#endif
}

/* 16 x 16 -> 32, unsigned: both operands must fit uint16 */
static inline __attribute__((always_inline)) uint32_t fm_mulu16(uint32_t a, uint32_t b)
{
#if defined(__bfin__)
    uint32_t r;
    __asm__("%0 = %1.L * %2.L (FU);" : "=d"(r) : "d"(a), "d"(b));
    return r;
#else
    return (uint32_t)(uint16_t)a * (uint16_t)b;
#endif
}

/* x * m >> 16 for x < 2^28 and m < 2^19, in 32-bit pieces (no 64-bit multiply) */
static inline uint32_t fm_mul_q16(uint32_t x, uint32_t m)
{
    return (x >> 16) * m + (((x & 0xFFFF) * (m >> 4)) >> 12);
}

/* log2(x) in Q12, within 0.006 octave: exponent plus log2(1 + t) ~ t (1.3466 - 0.3466 t) */
static inline int32_t fm_log2(uint32_t x)
{
    if (!x) return 0;
    const int e = 31 - __builtin_clz(x);
    const int32_t t = (int32_t)(((x << (31 - e)) >> 16) & 0x7FFF);           /* Q15 */
    const int32_t l = (t * (44126 - ((t * 11358) >> 15))) >> 15;               /* 1.3466 and 0.3466, Q15 */
    return (e << 12) + (l >> 3);
}

/* harm, timbre, morph: knob words 0 .. 32767; inc: the block's last increment at 88.2 kHz */
static inline void fm_knobs_make(fm_knobs *k, int32_t harm, int32_t timbre, int32_t morph, uint32_t inc)
{
    harm = fm_clamp(harm, 0, 32767); timbre = fm_clamp(timbre, 0, 32767); morph = fm_clamp(morph, 0, 32767);

    /* the ratio: Plaits' Interpolate(lut_fm_frequency_quantizer, harmonics, 128), as a multiplier */
    const int32_t x = harm << 2;                                    /* Q17: index 0 .. 128 in Q10 */
    const int i = x >> 10;
    const uint32_t fr = (uint32_t)(x & 1023);
    const uint32_t a = fm_ratio[i], b = fm_ratio[i + 1];
    const uint32_t mult = b >= a ? a + (((b - a) * fr) >> 10) : a - (((a - b) * fr) >> 10);

    uint32_t car = inc >> 1;
    if (car > FM_CAR_MAX) car = FM_CAR_MAX;
    uint32_t mod = fm_mul_q16(car, mult);
    if (mod > FM_MOD_MAX) mod = FM_MOD_MAX;
    k->mod = mod;

    /* hf_taming = (1 - 0.3 x octaves above 2,093 Hz)^2 */
    int32_t hf = 32767;
    if (mod > FM_TAME_INC) {
        const int32_t oct = fm_log2(mod) - fm_log2(FM_TAME_INC);              /* Q12 */
        hf = fm_clamp(32767 - ((oct * 2458) >> 10), 0, 32767);                /* 0.3 x 8 = 2.4 */
        hf = (hf * hf) >> 15;
    }
    const int32_t t2 = (timbre * timbre) >> 15;
    k->amount = (t2 * hf) >> 15;                                    /* 2 x timbre^2 x hf, Q14 cycles */

    const int32_t fb = fm_clamp(morph * 2 - 32767, -32767, 32767);  /* 2 morph - 1 */
    const int32_t fb2 = (fb * fb) >> 15;
    k->pf = fb < 0 ? fb2 >> 1 : 0;                                  /* 0.5 fb^2: modulator frequency feedback */
    k->mfb = fb > 0 ? fb2 >> 2 : 0;                                 /* 0.25 fb^2: modulator phase feedback */
}

/* one oversampled step: the modulator (with feedback) bends the carrier; returns the carrier. Every multiply is 16 x 16:
 * prev and m are Q15 sines; pf, mfb, amount and mod >> 16 are below 32768. */
static inline __attribute__((always_inline)) int32_t fm_step(const int16_t *tab, uint32_t *mph, uint32_t *cph,
                                                             int32_t *prev, uint32_t mod, uint32_t mod16, uint32_t c,
                                                             int32_t pf, int32_t mfb, int32_t amount)
{
    const int32_t p = *prev;
    const int32_t pfx = fm_mul16(p, pf) >> 15;                                       /* Q15, |pfx| <= 0.5 */
    *mph += mod + ((uint32_t)fm_mul16((int32_t)mod16, pfx) << 1);
    *cph += c;
    const int32_t m = fm_sin(tab, *mph + ((uint32_t)fm_mul16(p, mfb) << 2));        /* Q30 cycles -> Q32 */
    const int32_t car = fm_sin(tab, *cph + ((uint32_t)fm_mul16(amount, m) << 3));   /* Q29 cycles -> Q32 */
    *prev = p + (fm_mul16((car - p) >> 1, FM_LP2) >> 15);
    return car;
}

/* render n (<= 128, a power of two for the ramps when 128) samples at 88.2 kHz; inc: per-sample increments; gain:
 * Q15 level (velocity etc.), Plaits' out_gain applied here */
static inline void fm_render(fm_voice *v, const fm_knobs *k, const int32_t *inc, int n, int32_t gain, int16_t *out,
                             const int16_t *tab)
{
    if (v->start) {
        v->cph = v->mph = 0; v->prev = 0; v->p0 = v->p1 = 0;
        v->mod = k->mod; v->amount = k->amount; v->pf = k->pf; v->mfb = k->mfb;
        v->start = 0;
    }
    const int32_t g = (gain * FM_OUT_GAIN) >> 15;
    /* the ramps: value << 7 plus a step a sample, so a 128-sample block lands on the target */
    int32_t amount7 = v->amount << 7, pf7 = v->pf << 7, mfb7 = v->mfb << 7;
    const int32_t d_amount = k->amount - v->amount, d_pf = k->pf - v->pf, d_mfb = k->mfb - v->mfb;
    uint32_t mod = v->mod;
    const int32_t d_mod = ((int32_t)(k->mod - v->mod)) >> 7;
    uint32_t cph = v->cph, mph = v->mph;
    int32_t prev = v->prev, p0 = v->p0, p1 = v->p1;

    for (int j = 0; j < n; j++) {
        amount7 += d_amount; pf7 += d_pf; mfb7 += d_mfb; mod += (uint32_t)d_mod;
        const int32_t amount = amount7 >> 7, pf = pf7 >> 7, mfb = mfb7 >> 7;
        const uint32_t c1 = (uint32_t)inc[j] >> 1, c2 = (uint32_t)inc[j] - c1;
        const uint32_t mod16 = mod >> 16;
        const int32_t s0 = fm_step(tab, &mph, &cph, &prev, mod, mod16, c1, pf, mfb, amount);
        const int32_t s1 = fm_step(tab, &mph, &cph, &prev, mod, mod16, c2, pf, mfb, amount);
        const int32_t y = (p0 + 3 * p1 + 3 * s0 + s1) >> 3;
        p0 = s0; p1 = s1;
        out[j] = (int16_t)fm_clamp(fm_mul16(y, g) >> 15, -32767, 32767);
    }
    v->cph = cph; v->mph = mph; v->prev = prev; v->p0 = p0; v->p1 = p1;
    if (n == FM_BLOCK) { v->mod = k->mod; v->amount = k->amount; v->pf = k->pf; v->mfb = k->mfb; }
    else { v->mod = mod; v->amount = amount7 >> 7; v->pf = pf7 >> 7; v->mfb = mfb7 >> 7; }
}

#endif
