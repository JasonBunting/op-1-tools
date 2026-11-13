/* tractprobe DSP: a Kelly-Lochbaum vocal tract (a tube of n sections, one multiply per junction) driven by a
 * Rosenberg glottal pulse plus breath noise. Integer only, no division per sample. No OP-1 headers, so the host test
 * can include it.
 *
 * Fixed point: tube waves are int32 around +-2^16; reflection coefficients Q14; diameters and areas Q12.
 * At OP1_VOICE_RATE (88.2 kHz) one section is c / fs = 3.9 mm, so 44 sections make a 17 cm (adult male) tract.
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission. */
#ifndef TP_DSP_H
#define TP_DSP_H
#include <stdint.h>

#define TP_MAX        96                 /* most sections */
#define TP_GLOT_AMP   2048               /* peak glottal flow */
#define TP_GLOT_REFL  12288              /* 0.75 Q14: reflection at the glottis */
#define TP_LIP_REFL  (-13926)            /* -0.85 Q14: reflection at the lips */

typedef struct {                         /* one voice's tube: two buffers, swapped every sample */
    int32_t r[2][TP_MAX], l[2][TP_MAX];
} tp_tube;

typedef struct {                         /* one voice's scalars (fits in the voice object) */
    uint32_t phase, rng;
    int32_t prev, hp, par;
} tp_voice;

typedef struct {                         /* the tract shape, shared by every voice with the same knobs */
    int n;
    int32_t key;                         /* knobs this was made from, -1 = none */
    int32_t k[TP_MAX];                   /* k[i]: junction between sections i-1 and i, Q14 */
} tp_shape;

/* Pink Trombone's idea, simplified: a larynx that widens to a 1.5 tube, minus a raised-cosine tongue.
 * pos15 0..32767 = tongue back .. front, height15 0..32767 = open .. closed. sine = 8192-entry Q15 sine table. */
static void tp_shape_make(tp_shape *s, int n, int32_t pos15, int32_t height15, const int16_t *sine)
{
    int32_t area[TP_MAX];
    const int32_t larynx = n * 6 / 44 + 1;
    const int32_t centre = (n * (12 + ((17 * pos15) >> 15))) << 8;      /* Q8 section: 12/44 .. 29/44 of n */
    const int32_t width = (n * 10) / 44 + 1;                             /* sections either side of the centre */
    const int32_t h = (height15 * 4915) >> 15;                           /* bump 0 .. 1.2 (Q12) */
    for (int i = 0; i < n; i++) {
        int32_t d = 6144;                                                /* 1.5 Q12 */
        if (i < larynx) d = 2458 + (3686 * i) / larynx;                  /* 0.6 .. 1.5 */
        int32_t off = (i << 8) - centre / 44;                            /* Q8 sections from the tongue centre */
        if (off < 0) off = -off;
        if (off < (width << 8)) {
            int32_t idx = (off * 4096) / (width << 8);                   /* 0 .. pi in table steps */
            int32_t c = sine[(idx + 2048) & 8191];                       /* cos */
            d -= (h * (32767 + c)) >> 16;                                /* h * (1 + cos) / 2 */
        }
        if (d < 205) d = 205;                                            /* 0.05: never fully closed */
        area[i] = (d * d) >> 12;
    }
    s->n = n;
    s->k[0] = 0;
    for (int i = 1; i < n; i++) {
        int32_t a = area[i - 1], b = area[i];
        s->k[i] = ((a - b) << 14) / (a + b);
    }
}

static void tp_voice_reset(tp_voice *v, tp_tube *t)
{
    v->phase = 0; v->prev = 0; v->hp = 0; v->par = 0;
    if (!v->rng) v->rng = 0x9E3779B9u;
    for (int i = 0; i < TP_MAX; i++) t->r[0][i] = t->l[0][i] = t->r[1][i] = t->l[1][i] = 0;
}

/* Rosenberg flow pulse, 0 .. TP_GLOT_AMP: rise over 40% of the period, fall over 16%, closed for the rest. */
static inline int32_t tp_glottis(uint32_t phase, const int16_t *sine)
{
    int32_t p = (int32_t)(phase >> 17);                                  /* 0 .. 32767 */
    if (p < 13107) return (TP_GLOT_AMP * (32767 - sine[(((p * 5) >> 4) + 2048) & 8191])) >> 16;
    if (p < 18350) return (TP_GLOT_AMP * sine[((((p - 13107) * 25) >> 6) + 2048) & 8191]) >> 15;
    return 0;
}

/* count samples; inc = per-sample phase increments (2^32 = one cycle); breath15 0..32767; gain15 output gain;
 * out gets int16 samples. Returns the largest |wave| seen (for the host test). */
static int32_t tp_render(tp_voice *v, tp_tube *t, const tp_shape *s, const int32_t *inc, int count,
                         int32_t breath15, int32_t gain15, int16_t *out, const int16_t *sine)
{
    const int n = s->n;
    const int32_t *k = s->k;
    uint32_t phase = v->phase, rng = v->rng;
    int32_t prev = v->prev, hp = v->hp, par = v->par, peak = 0;
    for (int j = 0; j < count; j++) {
        phase += (uint32_t)inc[j];
        int32_t g = tp_glottis(phase, sine);
        rng = rng * 1664525u + 1013904223u;
        int32_t noise = ((int32_t)rng >> 17) * breath15 >> 15;           /* +-16384 at full breath */
        int32_t src = g + ((noise * (g + 512)) >> 14);                  /* breath follows the flow, +-2560 at most */

        int32_t *R = t->r[par], *L = t->l[par], *nR = t->r[par ^ 1], *nL = t->l[par ^ 1];
        nR[0] = ((L[0] * TP_GLOT_REFL) >> 14) + src;
        for (int i = 1; i < n; i++) {
            int32_t w = (k[i] * (R[i - 1] + L[i])) >> 14;
            nR[i] = R[i - 1] - w;
            nL[i - 1] = L[i] + w;
        }
        nL[n - 1] = (R[n - 1] * TP_LIP_REFL) >> 14;
        par ^= 1;

        int32_t lip = nR[n - 1];
        int32_t a = lip < 0 ? -lip : lip;
        if (a > peak) peak = a;
        /* Lip flow with its DC removed (a 14 Hz high-pass), not its derivative: at 88.2 kHz the derivative leaves the
         * voice ~1000x quieter than the breath noise. */
        hp += lip - prev - (hp >> 10);
        prev = lip;
        int32_t y = ((hp >> 2) * gain15) >> 10;               /* gain15 8192 = x2 */
        out[j] = (int16_t)(y > 32767 ? 32767 : (y < -32767 ? -32767 : y));
    }
    v->phase = phase; v->rng = rng; v->prev = prev; v->hp = hp; v->par = par;
    return peak;
}
#endif
