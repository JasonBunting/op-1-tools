/* formant DSP: a vocal-fold pulse through five formant resonators in series (a cascade formant synth).
 * Integer only; no division per sample; no OP-1 headers, so the host test can include it.
 *
 * Source: the slope of a Rosenberg flow pulse (rises over 40% of the period, falls over 16%, closed for the rest),
 * worked out in phase so its level doesn't change with pitch; plus breath noise that is louder while the folds are
 * open. A one-pole low-pass softens the closing click.
 * Tract: five Chamberlin state-variable low-passes (unity gain at DC, a peak at the formant), F1..F5 from
 * fm_tables.h, mixed between the vowels on the blue knob's path and the bodies on green.
 * Life: vibrato (5.4 Hz, fading in after the note starts, up to a 14 Hz warble) and a slow random pitch drift on
 * every note, different in each voice (chords sound like a small choir).
 * Talk (page 2): made-up syllables, seeded by the key, so a held note "says" a word that isn't one.
 *
 * Fixed point: knob values and coefficients Q15; filter states stay below 2^17 (host test).
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#ifndef FM_DSP_H
#define FM_DSP_H
#include <stdint.h>
#include "fm_tables.h"

#define FM_RATE       88200
#define FM_SRC_AMP    16384              /* source peak (the closing slope) */
#define FM_VIB_INC    262958             /* 5.4 Hz at 88.2 kHz, per sample (2^32 = one cycle) */
#define FM_VIB_MAX    1966               /* +-6% (about a semitone) from orange halfway, Q15 */
#define FM_VIB_RAMP   172                /* blocks to fade the vibrato in: 0.25 s */
#define FM_DRIFT_MAX  98                 /* +-0.3% random pitch drift, Q15 */

static const uint16_t fm_bw[FM_FORMANTS] = { 80, 100, 150, 200, 250 };   /* bandwidths, Hz */

typedef struct {                         /* one voice */
    uint32_t phase, rng, vib;
    int32_t drift, drift_to, age, blk, tilt, gain;
    int32_t low[FM_FORMANTS], band[FM_FORMANTS];
    int32_t hz[FM_FORMANTS];             /* smoothed formants, Hz << 8; 0 = not yet set */
    /* talk: the note's own syllable sequence */
    uint32_t seed;                       /* 0 = not yet seeded (the first block seeds it from the pitch) */
    int32_t syl_t, syl_len, off_from, off_to, cons;
    int32_t show_vowel, show_closure;    /* for the screen: this block's vowel and closure, Q15 */
} fm_voice;

typedef struct {                         /* page 2, Q15 */
    int32_t rate, spread, cons;          /* rate below FM_TALK_OFF: no talk */
} fm_talk_knobs;

#define FM_TALK_OFF   1024

typedef struct {                         /* per block: what the knobs ask for */
    int32_t hz[FM_FORMANTS];             /* Hz << 8 */
    int32_t breath, vib;                 /* Q15 */
} fm_knobs;

static void fm_voice_reset(fm_voice *v)
{
    uint32_t rng = v->rng ? v->rng : 0x9E3779B9u;
    uint8_t *p = (uint8_t *)v;
    for (unsigned i = 0; i < sizeof *v; i++) p[i] = 0;
    v->rng = rng;
}

/* vowel15, body15: 0..32767 along the blue path and green bodies; breath15, vib15: 0..32767.
 * Green runs half a body past each end (bigger than the man, smaller than the child), extrapolating the table. */
static void fm_knobs_make(fm_knobs *k, int32_t vowel15, int32_t body15, int32_t breath15, int32_t vib15)
{
    int32_t vp = vowel15 * (FM_VOWELS - 1), bp = body15 * FM_BODIES - 16384;   /* Q15 positions; bp -0.5 .. 2.5 */
    int32_t vi = vp >> 15, vf = vp & 32767, bi = bp >> 15;
    if (vi >= FM_VOWELS - 1) { vi = FM_VOWELS - 2; vf = 32767; }
    if (bi < 0) bi = 0;
    if (bi > FM_BODIES - 2) bi = FM_BODIES - 2;
    int32_t bf = bp - (bi << 15);                                              /* below 0 or above 1: extrapolate */
    for (int f = 0; f < FM_FORMANTS; f++) {
        int32_t a = fm_formants[bi][vi][f], b = fm_formants[bi][vi + 1][f];
        int32_t c = fm_formants[bi + 1][vi][f], d = fm_formants[bi + 1][vi + 1][f];
        int32_t ab = (a << 8) + (((b - a) * vf) >> 7), cd = (c << 8) + (((d - c) * vf) >> 7);   /* Hz << 8 */
        k->hz[f] = ab + ((((cd - ab) >> 4) * bf) >> 11);
    }
    k->breath = breath15;
    k->vib = vib15;
}

/* Chamberlin coefficient f = 2 sin(pi F / fs), Q15, from F in Hz << 8 (a cubic: under 0.1% error up to 6 kHz) */
static inline int32_t fm_coef_f(int32_t hz8)
{
    int32_t x = ((hz8 >> 4) * 2390) >> 15;                              /* pi F / fs, Q15 */
    int32_t x3 = (((x * x) >> 15) * x) >> 15;
    return 2 * x - ((x3 * 10923) >> 15);
}

static inline uint32_t fm_rand(uint32_t *r) { *r = *r * 1664525u + 1013904223u; return *r; }

/* Talk: turns a held note into made-up syllables. Every syllable picks a vowel near blue (spread wide), glides
 * there quickly, and starts with a dip that closes the mouth (F1 and F2 drop, the level falls): an "m" / "w" / "b"
 * sort of edge. The sequence is seeded by the note's pitch, so the same key says the same "word" every time.
 * Per block: writes the vowel to use and the closure (Q15), returns the level (Q15). */
static int32_t fm_talk(fm_voice *v, int32_t center15, const fm_talk_knobs *t, uint32_t inc0,
                       int32_t *vowel15, int32_t *closure15)
{
    *vowel15 = center15; *closure15 = 0;
    if (t->rate < FM_TALK_OFF) { v->syl_len = 0; return 32767; }
    if (!v->seed) v->seed = ((inc0 >> 12) | 1u) * 2654435761u;
    if (v->syl_t >= v->syl_len) {                                         /* the next syllable */
        uint32_t r = fm_rand(&v->seed);
        int32_t base = 1378 / (3 + ((t->rate * 17) >> 15));               /* 1.5 .. 10 syllables a second */
        v->syl_len = base * 3 / 4 + (int32_t)((r >> 8) % (uint32_t)(base / 2 + 1));   /* +-25% */
        v->off_from = v->off_to;
        r = fm_rand(&v->seed);
        int32_t off = (((int32_t)(r >> 16) - 32768) * t->spread) >> 15;  /* -spread .. +spread */
        v->off_to = off;
        if (!v->blk) v->off_from = off;                                   /* the note's first: no glide */
        r = fm_rand(&v->seed);
        v->cons = (r >> 30) == 0 ? 0 : (t->cons >> 1) + (int32_t)(((r >> 8) & 0xFFFF) * (uint32_t)t->cons >> 17);
        v->syl_t = 0;
    }
    int32_t tt = v->syl_t++, len = v->syl_len;
    int32_t glide = len / 2 < 25 ? len / 2 : 25;                          /* <= 36 ms */
    int32_t dip = len / 3 < 40 ? len / 3 : 40;                            /* <= 58 ms */
    int32_t off = tt >= glide ? v->off_to : v->off_from + (v->off_to - v->off_from) * tt / glide;
    int32_t vw = center15 + off;
    *vowel15 = vw < 0 ? 0 : (vw > 32767 ? 32767 : vw);
    int32_t shape = tt >= dip ? 0 : (dip - tt) * 32767 / dip;
    int32_t c = (v->cons * shape) >> 15;
    *closure15 = c;
    return 32767 - ((c * 27853) >> 15);                                   /* down to 15% at a full closure */
}

/* close the mouth by closure15: F1 down to 45%, F2 to 70% */
static void fm_close(fm_knobs *k, int32_t closure15)
{
    k->hz[0] -= (((k->hz[0] >> 8) * ((closure15 * 18022) >> 15)) >> 15) << 8;
    k->hz[1] -= (((k->hz[1] >> 8) * ((closure15 * 9830) >> 15)) >> 15) << 8;
}

/* count samples into out; inc = per-sample phase increments (2^32 = one cycle); gain15 output gain, reached by the
 * end of the block from the last block's. sine = 8192-entry Q15 table. Returns the largest filter state seen. */
static int32_t fm_render(fm_voice *v, const fm_knobs *k, const int32_t *inc, int count, int32_t gain15,
                         int16_t *out, const int16_t *sine)
{
    /* per block: glide the formants (a 6 ms lag, no zipper noise), then the filter coefficients */
    int32_t cf[FM_FORMANTS], cq[FM_FORMANTS];
    for (int f = 0; f < FM_FORMANTS; f++) {
        int32_t hz = v->hz[f] ? v->hz[f] + ((k->hz[f] - v->hz[f]) >> 3) : k->hz[f];
        v->hz[f] = hz;
        cf[f] = fm_coef_f(hz);
        cq[f] = ((int32_t)fm_bw[f] << 15) / (hz >> 8);                  /* 1/Q = BW / F, Q15 */
    }
    /* per block: vibrato (orange's first half: depth, fading in; second half: faster, up to a 14 Hz warble) */
    int32_t age = v->age < FM_VIB_RAMP ? ++v->age : FM_VIB_RAMP;
    int32_t d2 = k->vib * 2 > 32767 ? 32767 : k->vib * 2, w2 = k->vib > 16384 ? (k->vib - 16384) * 2 : 0;
    int32_t vdepth = (((d2 * FM_VIB_MAX) >> 15) * age) / FM_VIB_RAMP;
    uint32_t vinc = FM_VIB_INC + (uint32_t)(w2 * 13 + ((w2 * 12124) >> 15));
    if ((v->blk & 31) == 1) v->drift_to = (int32_t)(fm_rand(&v->rng) >> 16) % (2 * FM_DRIFT_MAX + 1) - FM_DRIFT_MAX;
    v->drift += (v->drift_to - v->drift) >> 4;
    /* white: breath up to the voice's own level, and the voice down to 30% (fully right whispers) */
    const int32_t breath = k->breath, voice = 32767 - ((k->breath * 22937) >> 15);

    uint32_t phase = v->phase, rng = v->rng, vib = v->vib;
    int32_t tilt = v->tilt, big = 0, g = v->gain, gstep = (gain15 - v->gain) / count;
    int32_t l0 = v->low[0], l1 = v->low[1], l2 = v->low[2], l3 = v->low[3], l4 = v->low[4];
    int32_t b0 = v->band[0], b1 = v->band[1], b2 = v->band[2], b3 = v->band[3], b4 = v->band[4];

    for (int j = 0; j < count; j++) {
        /* pitch: the note's, times (1 + vibrato + drift) */
        vib += vinc;
        int32_t dev = ((sine[vib >> 19] * vdepth) >> 15) + v->drift;     /* Q15 */
        uint32_t i = (uint32_t)inc[j];
        phase += i + (uint32_t)((int32_t)(i >> 15) * dev);

        /* source: the flow pulse's slope */
        int32_t p = (int32_t)(phase >> 17), s, open;                     /* p: 0 .. 32767 */
        if (p < 13107)      { s = (sine[(p * 5) >> 4] * (FM_SRC_AMP * 2 / 5)) >> 15; open = 1; }
        else if (p < 18350) { s = -((sine[((p - 13107) * 25) >> 6] * FM_SRC_AMP) >> 15); open = 1; }
        else                { s = 0; open = 0; }
        s = (s * voice) >> 15;
        int32_t noise = ((int32_t)fm_rand(&rng) >> 19) * breath >> 13;  /* +-16384 at full white */
        s += open ? noise : noise >> 1;
        tilt += (s - tilt) >> 1;                                         /* soften the closing click */

        /* tract: five resonators in series */
        int32_t x = tilt, h;
        h = x  - l0 - ((cq[0] * b0) >> 15); b0 += (cf[0] * h) >> 15; l0 += (cf[0] * b0) >> 15;
        h = l0 - l1 - ((cq[1] * b1) >> 15); b1 += (cf[1] * h) >> 15; l1 += (cf[1] * b1) >> 15;
        h = l1 - l2 - ((cq[2] * b2) >> 15); b2 += (cf[2] * h) >> 15; l2 += (cf[2] * b2) >> 15;
        h = l2 - l3 - ((cq[3] * b3) >> 15); b3 += (cf[3] * h) >> 15; l3 += (cf[3] * b3) >> 15;
        h = l3 - l4 - ((cq[4] * b4) >> 15); b4 += (cf[4] * h) >> 15; l4 += (cf[4] * b4) >> 15;

        int32_t a0 = b0 < 0 ? -b0 : b0, a1 = l0 < 0 ? -l0 : l0;
        if (a0 > big) big = a0;
        if (a1 > big) big = a1;
        g += gstep;
        int32_t y = (l4 * g) >> 13;
        out[j] = (int16_t)(y > 32767 ? 32767 : (y < -32767 ? -32767 : y));
    }
    v->gain = gain15;
    v->blk++;
    v->phase = phase; v->rng = rng; v->vib = vib; v->tilt = tilt;
    v->low[0] = l0; v->low[1] = l1; v->low[2] = l2; v->low[3] = l3; v->low[4] = l4;
    v->band[0] = b0; v->band[1] = b1; v->band[2] = b2; v->band[3] = b3; v->band[4] = b4;
    return big;
}
#endif
