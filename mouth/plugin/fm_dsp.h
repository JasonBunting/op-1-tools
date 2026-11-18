/* Mouth's DSP: a vocal-fold pulse through five formant resonators in series (a cascade formant synth).
 * Integer only; no division per sample; no OP-1 headers, so the host test can include it.
 *
 * Source: the slope of a Rosenberg flow pulse (rises over 40% of the period, falls over 16%, closed for the rest),
 * worked out in phase so its level doesn't change with pitch; plus breath noise that is louder while the folds are
 * open. A one-pole low-pass on the source follows velocity: soft notes are darker, hard ones brighter.
 * Tract: five Chamberlin state-variable low-passes (unity gain at DC, a peak at the formant), F1..F5 from
 * fm_tables.h, mixed between the vowels on the blue knob's path and the bodies on green. Bandwidths widen with
 * smaller bodies; fm_levels.h evens out the loudness of the vowels.
 * Life: vibrato (5.4 Hz, fading in after the note starts, up to a 14 Hz warble) and a slow random pitch drift on
 * every note, different in each voice (chords sound like a small choir).
 * Talk (page 2): made-up syllables in time with the OP-1's tempo. A word is two to five seeded syllables and then a
 * gap of one syllable slot; a held note says it over and over. By default the key picks the word, so the same key
 * says the same word; the WORD setting can fix one of 16 words instead. Some syllables start without the voice: a
 * puff of air after a closure ("p"/"t"), or a hiss ("s"/"sh").
 *
 * Fixed point: knob values and coefficients Q15; filter states stay below 2^17 (host test).
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#ifndef FM_DSP_H
#define FM_DSP_H
#include <stdint.h>
#include "fm_tables.h"
#include "fm_levels.h"

#define FM_RATE       88200
#define FM_SRC_AMP    8192               /* source peak (the closing slope): half scale leaves headroom at the extremes */
#define FM_VIB_INC    262958             /* 5.4 Hz at 88.2 kHz, per sample (2^32 = one cycle) */
#define FM_VIB_MAX    1966               /* +-6% (about a semitone) from orange halfway, Q15 */
#define FM_VIB_RAMP   172                /* blocks to fade the vibrato in: 0.25 s */
#define FM_DRIFT_MAX  98                 /* +-0.3% random pitch drift, Q15 */
#define FM_BLOCK_OUT  64                 /* output samples (44.1 kHz) per render block */
#ifndef FM_BODY
#define FM_BODY       8192               /* how much of the vocal folds' airflow joins its slope: the voice's low end */
#endif
#define FM_FLOW_MEAN  9893               /* the flow pulse's average, Q15 (0.4/2 + 0.16 * 2/pi), taken out */

/* The low end (measured: tools in test/; before these, middle-C "ah" had its 3rd harmonic 5x the fundamental, so a
 * tuner read G5 and the voice sounded thin): some airflow in the source, the airflow again after the resonances (chest,
 * which the vowel doesn't shape), and a wider first resonance. With these the fundamental is the loudest partial on
 * every vowel from C3 to C6. -D overrides them for tuning. */
#ifndef FM_B1
#define FM_B1 130                        /* the first formant's bandwidth, Hz */
#endif
#ifndef FM_CHEST
#define FM_CHEST 16384                   /* airflow added after the resonances, Q15 (half: the source is half scale) */
#endif
static const uint16_t fm_bw[FM_FORMANTS] = { FM_B1, 100, 150, 200, 250 };   /* a man's bandwidths, Hz */

/* talk speed steps: syllables per beat, times 2 (0 = talk off): off, 1/2, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32 */
#define FM_TALK_STEPS 9
static const uint8_t fm_talk_per2beats[FM_TALK_STEPS] = { 0, 1, 2, 3, 4, 6, 8, 12, 16 };
#define FM_WORDS      16                 /* fixed words; 0 = the key picks */

enum { FM_SYL_VOWEL, FM_SYL_STOP, FM_SYL_NASAL, FM_SYL_W, FM_SYL_Y,
       FM_SYL_PUFF,                      /* a stop without the voice, let go with a puff of air: "p"/"t" */
       FM_SYL_HISS,                      /* noise through the upper formants only, then the vowel: "s"/"sh" */
       FM_SYL_REST };                    /* the gap after a word */
#ifndef FM_HISS
#define FM_HISS       20000              /* the hiss's level, Q15 (measured: a third to half of a vowel's rms) */
#endif

typedef struct {                         /* one voice */
    uint32_t phase, rng, vib;
    int32_t drift, drift_to, age, blk, tilt, gain, dc;
    int32_t low[FM_FORMANTS], band[FM_FORMANTS];
    int32_t hz[FM_FORMANTS];             /* smoothed formants, Hz << 8; 0 = not yet set */
    int32_t pre;                         /* the source's level for high notes, Q15; 0 = not yet set */
    /* talk: this note's own syllables */
    uint32_t seed;                       /* 0 = not yet seeded (the first block seeds it) */
    uint32_t seed0;                      /* the word's seed: the word starts over from it after its gap */
    int32_t syl_i, word_n;               /* syllables said of the word; the word's length, 2 .. 5 */
    int32_t lvl;                         /* talk's level, Q15: fades out into a gap and back in */
    int32_t air, hiss, nz;               /* this block's puff and hiss, Q15 (fm_talk sets, fm_render plays); last noise */
    int32_t clock;                       /* talk clock: 2 x samples into the syllable slot */
    int32_t syl_t, syl_len, off_from, off_to, cons, kind, released;
    int32_t show_vowel, show_closure;    /* for the screen: this block's vowel and closure, Q15 */
} fm_voice;

typedef struct {                         /* page 2 */
    int speed;                           /* 0 .. FM_TALK_STEPS - 1; 0 = no talk */
    int word;                            /* 0 = the key picks, 1 .. FM_WORDS */
    int32_t wild;                        /* Q15 */
} fm_talk_knobs;

typedef struct {                         /* per block: what the knobs ask for */
    int32_t hz[FM_FORMANTS];             /* Hz << 8 */
    int32_t bw;                          /* bandwidth scale, Q15 */
    int32_t level;                       /* loudness evening, Q15 */
    int32_t breath, vib, bright;         /* Q15 */
} fm_knobs;

/* a new note on a voice: the note's state starts over; the sound's state (filters, phase, level) carries on, so a
 * stolen voice doesn't click. A fresh voice is all zeros anyway. */
static void fm_voice_note(fm_voice *v)
{
    if (!v->rng) v->rng = 0x9E3779B9u;
    v->drift = v->drift_to = v->age = v->blk = 0;
    v->seed = v->seed0 = 0; v->clock = 0;
    v->syl_i = v->word_n = v->air = v->hiss = 0; v->lvl = 32767;
    v->syl_t = v->syl_len = v->off_from = v->off_to = v->cons = v->kind = v->released = 0;
    v->show_closure = 0;
}
/* once per voice, before its first note: voice i's own random sequence (drift, breath) and vibrato phase, so voices
 * that start and stop together still differ */
static inline void fm_voice_seed(fm_voice *v, int i)
{
    v->rng = 0x9E3779B9u * (2u * (uint32_t)i + 1u);
    v->vib = (uint32_t)i * 0x2AAAAAABu;                                   /* a sixth of a cycle apart */
}
static inline void fm_voice_reset(fm_voice *v)   /* everything, for tests */
{
    uint32_t rng = v->rng;
    uint8_t *p = (uint8_t *)v;
    for (unsigned i = 0; i < sizeof *v; i++) p[i] = 0;
    v->rng = rng;
    fm_voice_note(v);
}

/* vowel15, body15: 0..32767 along the blue path and green bodies; breath15, vib15: 0..32767; bright15: the note's
 * velocity, 0..32767. Green runs half a body past each end (bigger than the man, smaller than the child),
 * extrapolating the table. */
static void fm_knobs_make(fm_knobs *k, int32_t vowel15, int32_t body15, int32_t breath15, int32_t vib15, int32_t bright15)
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
    {                                                                          /* the level table, the same mix */
        int32_t a = fm_levels[bi][vi], b = fm_levels[bi][vi + 1], c = fm_levels[bi + 1][vi], d = fm_levels[bi + 1][vi + 1];
        int32_t ab = a + (((b - a) * vf) >> 15), cd = c + (((d - c) * vf) >> 15);
        int32_t l = ab + (((cd - ab) * bf) >> 15);
        k->level = l < 1024 ? 1024 : (l > 32767 ? 32767 : l);
    }
    int32_t bw = 32768 + ((bp * 15) / 100);                                    /* x0.93 (giant) .. x1.38 (tiny) */
    k->bw = bw < 26214 ? 26214 : bw;
    k->breath = breath15;
    k->vib = vib15;
    k->bright = bright15;
}

/* Chamberlin coefficient f = 2 sin(pi F / fs), Q15, from F in Hz << 8 (a cubic: under 0.1% error up to 6 kHz) */
static inline int32_t fm_coef_f(int32_t hz8)
{
    int32_t x = ((hz8 >> 4) * 2390) >> 15;                              /* pi F / fs, Q15 */
    int32_t x3 = (((x * x) >> 15) * x) >> 15;
    return 2 * x - ((x3 * 10923) >> 15);
}

/* the integer square root of x >= 0 */
static inline int32_t fm_isqrt(int32_t x)
{
    uint32_t r = 0, b = 1u << 30, v = (uint32_t)(x < 0 ? 0 : x);
    while (b > v) b >>= 2;
    while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
    return (int32_t)r;
}

/* What the five resonators do to a tone at f Hz, as 1 / gain, Q12 (4096: no change; at most 32x; small: a loud
 * resonance). hz8: the formants, Hz << 8; cq: their 1/Q, Q15. One resonator's 1 / gain is
 * sqrt((1 - r^2)^2 + (r / Q)^2), r = f / F (r above 2 counts as 2). dev15: how far the tone may move (vibrato, drift),
 * Q15 of f; each resonator counts at the nearest the tone comes to it. */
static inline int32_t fm_loss_at(int32_t f, int32_t dev15, const int32_t *hz8, const int32_t *cq)
{
    int32_t p = 4096;
    if (f < 1) f = 1;
    if (f > 30000) f = 30000;
    for (int i = 0; i < FM_FORMANTS; i++) {
        int32_t r = (f << 12) / (hz8[i] >> 8);
        if (r > 8192) r = 8192;
        int32_t r2 = (r * r) >> 12, a = 4096 - r2, b = (r * cq[i]) >> 15;
        if (a < 0) a = -a;
        a -= (2 * r2 * dev15) >> 15;
        if (a < 0) a = 0;
        p = (p * fm_isqrt(a * a + b * b)) >> 12;
        if (p > (1 << 17)) p = 1 << 17;
    }
    return p < 1 ? 1 : p;
}

static inline uint32_t fm_rand(uint32_t *r) { *r = *r * 1664525u + 1013904223u; return *r; }

/* Talk, once per block. Each syllable slot (a step of the tempo) may start a new syllable: it picks a vowel near
 * blue (wider with WILD), glides there, and starts with an edge: a stop ("b"/"d": the level drops away), a nasal
 * ("m"/"n": a hum), a glide ("w"/"y": the vowel swings), or nothing (vowel to vowel). WILD also makes stops more
 * likely and edges stronger, and lets some slots hold the last syllable instead (a speech-like rhythm on the grid).
 * A puff ("p"/"t") is a stop whose voice stays off until the air has gone; a hiss ("s"/"sh") is noise before the
 * vowel (at low WILD the voice hums along: "z"). After a word's last syllable comes a silent slot, then the word
 * starts over from its seed: its first syllable never holds, so every repeat is the same.
 * beat = samples per beat at 44.1 kHz. After the key is let go no new syllable starts.
 * Writes the vowel and closure (Q15) and the voice's air and hiss; returns the level (Q15). */
static inline int32_t fm_talk(fm_voice *v, int32_t center15, const fm_talk_knobs *t, uint32_t inc0, int32_t beat,
                       int32_t *vowel15, int32_t *closure15)
{
    *vowel15 = center15; *closure15 = 0;
    v->air = v->hiss = 0;
    int per2 = t->speed > 0 && t->speed < FM_TALK_STEPS ? fm_talk_per2beats[t->speed] : 0;
    if (!per2) { v->syl_len = 0; v->kind = FM_SYL_VOWEL; v->lvl = 32767; return 32767; }
    if (beat < 5512) beat = 5512;                                         /* 30 .. 480 BPM */
    if (beat > 88200) beat = 88200;
    const int32_t slot2 = 2 * beat;                                       /* the clock counts 2 x samples */
    const int32_t wild = t->wild;
    int first = !v->seed;
    if (first) {
        v->seed = v->seed0 = (t->word ? (uint32_t)t->word * 0x9E3779B1u : ((inc0 >> 12) | 1u) * 2654435761u) | 1u;
        uint32_t s = v->seed0;
        v->word_n = 2 + (int32_t)((fm_rand(&s) >> 13) & 3u);
        v->syl_i = 0;
    }
    v->clock += FM_BLOCK_OUT * per2;
    int slot = first || v->clock >= slot2;
    if (slot && !first) v->clock -= slot2;
    if (slot && !v->released) {
        int32_t len = (slot2 / per2) / (2 * FM_BLOCK_OUT);                      /* blocks in a slot */
        v->syl_len = len < 2 ? 2 : len;
        if (v->syl_i >= v->word_n) {                                            /* the gap, then the word again */
            v->kind = FM_SYL_REST; v->syl_i = 0; v->seed = v->seed0; v->syl_t = 0;
        } else {
            uint32_t r = fm_rand(&v->seed);
            int start = !v->syl_i++;                                            /* the word's first syllable */
            int hold = !start && (int32_t)((r >> 8) & 0x7FFF) < (wild * 5) / 16;    /* up to 31% of slots hold */
            if (!hold) {
                v->off_from = v->off_to;
                r = fm_rand(&v->seed);
                int32_t spread = 4915 + ((wild * 27852) >> 15);                 /* 15% .. 100% of the strip */
                v->off_to = (((int32_t)(r >> 16) - 32768) * spread) >> 15;
                if (start) v->off_from = v->off_to;                             /* out of silence: no glide */
                r = fm_rand(&v->seed);
                int32_t u = (int32_t)((r >> 17) & 0x7FFF);                      /* pick the edge */
                int32_t p_stop = 3277 + ((wild * 4915) >> 15);                  /* 10% .. 25%, and as many puffs */
                int32_t p_hiss = 3277 + ((wild * 1638) >> 15);                  /* 10% .. 15% */
                u -= p_stop;
                if (u < 0) v->kind = FM_SYL_STOP;
                else if ((u -= p_stop) < 0) v->kind = FM_SYL_PUFF;
                else if ((u -= 4915) < 0) v->kind = FM_SYL_NASAL;               /* 15% */
                else if ((u -= p_hiss) < 0) v->kind = FM_SYL_HISS;
                else if ((u -= 2458) < 0) v->kind = FM_SYL_W;                   /* 7.5% */
                else if ((u -= 2458) < 0) v->kind = FM_SYL_Y;                   /* 7.5% */
                else v->kind = FM_SYL_VOWEL;                                    /* the rest: 40% .. 5% */
                v->cons = 16384 + ((wild * 16383) >> 15);                       /* edge strength 50% .. 100% */
                v->syl_t = 0;
            }
        }
    }
    int32_t tt = v->syl_t < 0x7FFFFFF ? v->syl_t++ : v->syl_t, len = v->syl_len, kind = v->kind;
    int32_t glide = len / 2 < 25 ? len / 2 : 25;                          /* <= 36 ms */
    int32_t dip = kind == FM_SYL_NASAL ? (len / 2 < 48 ? len / 2 : 48) : (len / 3 < 30 ? len / 3 : 30);
    if (dip < 1) dip = 1;
    if (glide < 1) glide = 1;
    int32_t off = tt >= glide ? v->off_to : v->off_from + (v->off_to - v->off_from) * tt / glide;
    int32_t vw = center15 + off;
    *vowel15 = vw < 0 ? 0 : (vw > 32767 ? 32767 : vw);
    if (kind == FM_SYL_REST) {                                            /* fade out in 12 ms, stay silent */
        v->lvl = v->lvl > 4096 ? v->lvl - 4096 : 0;
        *closure15 = 32767 - v->lvl;
        return v->lvl;
    }
    int32_t target = 32767;
    if (kind == FM_SYL_HISS) {                                            /* full for half the edge, then away */
        int32_t shape = tt >= dip ? 0 : tt < dip / 2 ? 32767 : (dip - tt) * 32767 / (dip - dip / 2);
        v->hiss = (v->cons * shape) >> 15;
        *closure15 = v->hiss >> 1;                                        /* the teeth nearly together */
    } else if (kind != FM_SYL_VOWEL) {
        static const int16_t drop[6] = { 0, 29491, 18022, 6554, 6554, 31129 };   /* level lost at a full edge */
        int32_t shape = tt >= dip ? 0 : (dip - tt) * 32767 / dip;
        int32_t c = (v->cons * shape) >> 15;
        *closure15 = c;
        target = 32767 - ((c * drop[kind]) >> 15);
        if (kind == FM_SYL_PUFF) {                                        /* air while it opens, then the voice */
            int32_t a = tt >= 2 * dip ? 0 : tt < dip ? 32767 : (2 * dip - tt) * 32767 / dip;
            v->air = (v->cons * a) >> 15;
        }
    }
    v->lvl = v->lvl + 8192 < target ? v->lvl + 8192 : target;             /* back in from a gap in 6 ms */
    return v->lvl;
}

/* the edge's effect on the formants, by closure15 */
static inline void fm_close(fm_knobs *k, int kind, int32_t closure15)
{
    if (!closure15) return;
    int32_t f1 = k->hz[0] >> 8, f2 = k->hz[1] >> 8, c = closure15;
    switch (kind) {
    case FM_SYL_PUFF:
    case FM_SYL_STOP:  f1 -= (f1 * ((c * 18022) >> 15)) >> 15; f2 -= (f2 * ((c * 9830) >> 15)) >> 15; break;
    case FM_SYL_NASAL: f1 += ((260 - f1) * c) >> 15; f2 -= (f2 * ((c * 9830) >> 15)) >> 15; break;   /* a hum near 260 Hz */
    case FM_SYL_W:     f1 -= (f1 * ((c * 14746) >> 15)) >> 15; f2 -= (f2 * ((c * 16384) >> 15)) >> 15; break;
    case FM_SYL_Y:     f1 -= (f1 * ((c * 14746) >> 15)) >> 15; f2 += (f2 * ((c * 11469) >> 15)) >> 15; break;
    default: return;
    }
    int32_t f3 = k->hz[2] >> 8;
    if (f2 > f3 - 200) f2 = f3 - 200;                                     /* keep the formants in order */
    if (f1 < 150) f1 = 150;
    k->hz[0] = f1 << 8; k->hz[1] = f2 << 8;
}

/* High notes: a singer raises the first formant to stay above the note, or the voice turns thin and squeaky. Keep F1
 * at least 1.15 x the note (inc0: its per-sample phase step), but no closer than 300 Hz under F2: moving the formants
 * above it would stack the resonators on one frequency, and their gains multiply. Before fm_close. */
static inline void fm_track(fm_knobs *k, uint32_t inc0)
{
    int32_t f0 = (int32_t)(((inc0 >> 16) * 1378u) >> 10);                 /* Hz: inc * 88200 / 2^32 */
    int32_t want = (f0 * 1178) >> 10, cap = (k->hz[1] >> 8) - 300;        /* x1.15; under F2 */
    if (want > cap) want = cap;
    if ((k->hz[0] >> 8) < want) k->hz[0] = want << 8;
}

/* count samples into out; inc = per-sample phase increments (2^32 = one cycle); gain15 output gain, reached by the
 * end of the block from the last block's. sine = 8192-entry Q15 table. Returns the largest filter state seen (the
 * first resonator's on every sample, the others' at the end of the block). */
static int32_t fm_render(fm_voice *v, const fm_knobs *k, const int32_t *inc, int count, int32_t gain15,
                         int16_t *out, const int16_t *sine)
{
    /* per block: glide the formants (a 6 ms lag, no zipper noise), then the filter coefficients */
    int32_t cf[FM_FORMANTS], cq[FM_FORMANTS];
    for (int f = 0; f < FM_FORMANTS; f++) {
        int32_t hz = v->hz[f] ? v->hz[f] + ((k->hz[f] - v->hz[f]) >> 3) : k->hz[f];
        v->hz[f] = hz;
        cf[f] = fm_coef_f(hz);
        cq[f] = ((((int32_t)fm_bw[f] * k->bw) >> 15) << 15) / (hz >> 8);   /* 1/Q = BW / F, Q15 */
    }
    /* per block: vibrato (orange's first half: depth, fading in; second half: faster, up to a 14 Hz warble) */
    int32_t age = v->age < FM_VIB_RAMP ? ++v->age : FM_VIB_RAMP;
    int32_t d2 = k->vib * 2 > 32767 ? 32767 : k->vib * 2, w2 = k->vib > 16384 ? (k->vib - 16384) * 2 : 0;
    int32_t vdepth = (((d2 * FM_VIB_MAX) >> 15) * age) / FM_VIB_RAMP;
    uint32_t vinc = FM_VIB_INC + (uint32_t)(w2 * 13 + ((w2 * 12124) >> 15));
    if ((v->blk & 31) == 1) v->drift_to = (int32_t)(fm_rand(&v->rng) >> 16) % (2 * FM_DRIFT_MAX + 1) - FM_DRIFT_MAX;
    v->drift += (v->drift_to - v->drift) >> 4;
    /* white: breath up to the voice's own level, and the voice down to 30% (fully right whispers) */
    /* talk: a puff turns the breath up and the voice down; a hiss is noise into the third resonator (so only the
     * upper formants shape it), with the voice down */
    const int32_t unv = v->air > v->hiss ? v->air : v->hiss;
    const int32_t breath = k->breath + (((32767 - k->breath) * v->air) >> 15);
    const int32_t voice = ((32767 - ((k->breath * 22937) >> 15)) * (32767 - unv)) >> 15;
    /* High notes: a note that lands on a resonance is the resonance's gain louder (measured: up to 10x from 600 Hz up,
     * clipping from 1.2 kHz, and the resonators left their range). So, from the gain the five resonators give the note
     * (anywhere in its vibrato) and half of what they give its octave: above 2x the source is turned down to keep
     * the resonators in range, and the output gain makes that up, but only up to 2.67x in all: a note alone on a
     * resonance is then about as loud as a low note. Down at once, back up over 12 ms. Low notes skip it. */
    int32_t f0 = (int32_t)((((uint32_t)inc[0] >> 16) * 1378u) >> 10), want = 32767;
    if (f0 >= v->hz[0] >> 10) {
        int32_t dev = vdepth + FM_DRIFT_MAX;
        int32_t p = fm_loss_at(f0, dev, v->hz, cq), p2 = 2 * fm_loss_at(2 * f0, dev, v->hz, cq);
        if (p2 < p) p = p2;
        if (p < 2048) want = p < 4 ? 64 : p << 4;                        /* 2 / gain, Q15 */
    }
    if (f0 > 1760) want = (want * 1760 / f0) * 1760 / f0;                /* above A6 the resonators ring harder than
                                                                          * the estimate: leave them more room */
    v->pre = !v->pre || want < v->pre ? want : v->pre + ((want - v->pre + 7) >> 3);
    int32_t makeup = (4096 << 15) / v->pre;                              /* Q12 */
    gain15 = (gain15 * (makeup > 5461 ? 5461 : makeup)) >> 12;           /* 2.67x in all: 2x inside, 1.33x here */
    const int32_t pre = v->pre;
    const int32_t hissg = (((v->hiss * FM_HISS) >> 15) * pre) >> 15, chest = (FM_CHEST * pre) >> 15;
    const int32_t body = (FM_BODY * pre) >> 15, amp = (FM_SRC_AMP * pre) >> 15, nbreath = (breath * pre) >> 15;
    const int32_t tc = 8192 + ((k->bright * 16384) >> 15);               /* source low-pass: soft dark, hard brighter */

    uint32_t phase = v->phase, rng = v->rng, vib = v->vib;
    int32_t nz = v->nz, tilt = v->tilt, big = 0, g = v->gain, gstep = (gain15 - v->gain) / count, dc = v->dc;
    int32_t l0 = v->low[0], l1 = v->low[1], l2 = v->low[2], l3 = v->low[3], l4 = v->low[4];
    int32_t b0 = v->band[0], b1 = v->band[1], b2 = v->band[2], b3 = v->band[3], b4 = v->band[4];

    for (int j = 0; j < count; j++) {
        /* pitch: the note's, times (1 + vibrato + drift) */
        vib += vinc;
        int32_t dev = ((sine[vib >> 19] * vdepth) >> 15) + v->drift;     /* Q15 */
        uint32_t i = (uint32_t)inc[j];
        phase += i + (uint32_t)((int32_t)(i >> 15) * dev);

        /* source: the flow pulse's slope */
        /* source: the flow pulse's slope (the voice's edge), plus some of the flow itself (its low end: the
         * slope alone leaves the fundamental weak, so the loudest harmonic was often the 3rd) */
        int32_t p = (int32_t)(phase >> 17), s, open, fl;                 /* p: 0 .. 32767 */
        if (p < 13107) {
            int32_t a = (p * 5) >> 4;                                    /* 0 .. pi */
            s = (sine[a] * (amp * 2 / 5)) >> 15; fl = (32767 - sine[(a + 2048) & 8191]) >> 1; open = 1;
        } else if (p < 18350) {
            int32_t a = ((p - 13107) * 25) >> 6;                         /* 0 .. pi/2 */
            s = -((sine[a] * amp) >> 15); fl = sine[a + 2048]; open = 1;
        } else { s = 0; fl = 0; open = 0; }
        s += ((fl - FM_FLOW_MEAN) * body) >> 15;
        s = (s * voice) >> 15;
        int32_t nr = (int32_t)fm_rand(&rng) >> 19;                       /* +-4096 */
        int32_t noise = nr * nbreath >> 13;                               /* +-16384 at full white */
        int32_t sib = ((nr - nz) * hissg) >> 15;                         /* the hiss: the noise's slope (its highs) */
        nz = nr;
        s += open ? noise : noise >> 1;
        tilt += ((s - tilt) * tc) >> 15;

        /* tract: five resonators in series */
        int32_t x = tilt, h;
        h = x  - l0 - ((cq[0] * b0) >> 15); b0 += (cf[0] * h) >> 15; l0 += (cf[0] * b0) >> 15;
        h = l0 - l1 - ((cq[1] * b1) >> 15); b1 += (cf[1] * h) >> 15; l1 += (cf[1] * b1) >> 15;
        h = l1 + sib - l2 - ((cq[2] * b2) >> 15); b2 += (cf[2] * h) >> 15; l2 += (cf[2] * b2) >> 15;
        h = l2 - l3 - ((cq[3] * b3) >> 15); b3 += (cf[3] * h) >> 15; l3 += (cf[3] * b3) >> 15;
        h = l3 - l4 - ((cq[4] * b4) >> 15); b4 += (cf[4] * h) >> 15; l4 += (cf[4] * b4) >> 15;

        int32_t a0 = b0 < 0 ? -b0 : b0, a1 = l0 < 0 ? -l0 : l0;
        if (a0 > big) big = a0;
        if (a1 > big) big = a1;
        g += gstep;
        int32_t o = l4 + (((fl - FM_FLOW_MEAN) * chest) >> 15);
        dc += (o - dc) >> 10;                                            /* a 14 Hz high-pass: no DC */
        int32_t y = ((o - dc) * g) >> 13;
        out[j] = (int16_t)(y > 32767 ? 32767 : (y < -32767 ? -32767 : y));
    }
    v->gain = gain15;
    v->blk++;
    /* a guard: a filter pushed past its range (it shouldn't happen; the host test checks) would wrap and lock into a loud
     * runaway, so a voice that gets there starts its filters over instead */
    int32_t worst = 0, all[11] = { l0, l1, l2, l3, l4, b0, b1, b2, b3, b4, dc };
    for (int i = 0; i < 11; i++) { int32_t a = all[i] < 0 ? -all[i] : all[i]; if (a > worst) worst = a; }
    if (worst > (1 << 18)) { l0 = l1 = l2 = l3 = l4 = b0 = b1 = b2 = b3 = b4 = dc = tilt = 0; }
    v->nz = nz;
    v->phase = phase; v->rng = rng; v->vib = vib; v->tilt = tilt; v->dc = dc;
    v->low[0] = l0; v->low[1] = l1; v->low[2] = l2; v->low[3] = l3; v->low[4] = l4;
    v->band[0] = b0; v->band[1] = b1; v->band[2] = b2; v->band[3] = b3; v->band[4] = b4;
    return big > worst ? big : worst;                                     /* worst: every resonator, at the block's end */
}
#endif
