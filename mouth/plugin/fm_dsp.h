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
 * Talk (page 2): made-up syllables in time with the OP-1's tempo. A word is a seeded sequence: by default the key
 * picks it, so the same key says the same word; the WORD setting can fix one of 16 words instead.
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

enum { FM_SYL_VOWEL, FM_SYL_STOP, FM_SYL_NASAL, FM_SYL_W, FM_SYL_Y };

typedef struct {                         /* one voice */
    uint32_t phase, rng, vib;
    int32_t drift, drift_to, age, blk, tilt, gain, dc;
    int32_t low[FM_FORMANTS], band[FM_FORMANTS];
    int32_t hz[FM_FORMANTS];             /* smoothed formants, Hz << 8; 0 = not yet set */
    /* talk: this note's own syllables */
    uint32_t seed;                       /* 0 = not yet seeded (the first block seeds it) */
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
    v->seed = 0; v->clock = 0;
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

static inline uint32_t fm_rand(uint32_t *r) { *r = *r * 1664525u + 1013904223u; return *r; }

/* Talk, once per block. Each syllable slot (a step of the tempo) may start a new syllable: it picks a vowel near
 * blue (wider with WILD), glides there, and starts with an edge: a stop ("b"/"d": the level drops away), a nasal
 * ("m"/"n": a hum), a glide ("w"/"y": the vowel swings), or nothing (vowel to vowel). WILD also makes stops more
 * likely and edges stronger, and lets some slots hold the last syllable instead (a speech-like rhythm on the grid).
 * beat = samples per beat at 44.1 kHz. After the key is let go no new syllable starts.
 * Writes the vowel and closure (Q15); returns the level (Q15). */
static inline int32_t fm_talk(fm_voice *v, int32_t center15, const fm_talk_knobs *t, uint32_t inc0, int32_t beat,
                       int32_t *vowel15, int32_t *closure15)
{
    *vowel15 = center15; *closure15 = 0;
    int per2 = t->speed > 0 && t->speed < FM_TALK_STEPS ? fm_talk_per2beats[t->speed] : 0;
    if (!per2) { v->syl_len = 0; v->kind = FM_SYL_VOWEL; return 32767; }
    if (beat < 5512) beat = 5512;                                         /* 30 .. 480 BPM */
    if (beat > 88200) beat = 88200;
    const int32_t slot2 = 2 * beat;                                       /* the clock counts 2 x samples */
    const int32_t wild = t->wild;
    int first = !v->seed;
    if (first) v->seed = (t->word ? (uint32_t)t->word * 0x9E3779B1u : ((inc0 >> 12) | 1u) * 2654435761u) | 1u;
    v->clock += FM_BLOCK_OUT * per2;
    int slot = first || v->clock >= slot2;
    if (slot && !first) v->clock -= slot2;
    if (slot && !v->released) {
        uint32_t r = fm_rand(&v->seed);
        int hold = !first && (int32_t)((r >> 8) & 0x7FFF) < (wild * 5) / 16;    /* up to 31% of slots hold */
        int32_t len = (slot2 / per2) / (2 * FM_BLOCK_OUT);                      /* blocks in a slot */
        v->syl_len = len < 2 ? 2 : len;
        if (!hold) {
            v->off_from = first ? 0 : v->off_to;
            r = fm_rand(&v->seed);
            int32_t spread = 4915 + ((wild * 27852) >> 15);                     /* 15% .. 100% of the strip */
            v->off_to = (((int32_t)(r >> 16) - 32768) * spread) >> 15;
            if (first) v->off_from = v->off_to;
            r = fm_rand(&v->seed);
            int32_t u = (int32_t)((r >> 17) & 0x7FFF);                          /* pick the edge */
            int32_t p_stop = 4915 + ((wild * 11469) >> 15);                     /* 15% .. 50% */
            v->kind = u < p_stop ? FM_SYL_STOP : u < p_stop + 8192 ? FM_SYL_NASAL :
                      u < p_stop + 12288 ? FM_SYL_W : u < p_stop + 16384 ? FM_SYL_Y : FM_SYL_VOWEL;
            v->cons = 16384 + ((wild * 16383) >> 15);                           /* edge strength 50% .. 100% */
            v->syl_t = 0;
        }
    }
    int32_t tt = v->syl_t < 0x7FFFFFF ? v->syl_t++ : v->syl_t, len = v->syl_len;
    int32_t glide = len / 2 < 25 ? len / 2 : 25;                          /* <= 36 ms */
    int32_t dip = v->kind == FM_SYL_NASAL ? (len / 2 < 48 ? len / 2 : 48) : (len / 3 < 30 ? len / 3 : 30);
    if (dip < 1) dip = 1;
    if (glide < 1) glide = 1;
    int32_t off = tt >= glide ? v->off_to : v->off_from + (v->off_to - v->off_from) * tt / glide;
    int32_t vw = center15 + off;
    *vowel15 = vw < 0 ? 0 : (vw > 32767 ? 32767 : vw);
    if (v->kind == FM_SYL_VOWEL) return 32767;                            /* a released note finishes its edge: no jump */
    int32_t shape = tt >= dip ? 0 : (dip - tt) * 32767 / dip;
    int32_t c = (v->cons * shape) >> 15;
    *closure15 = c;
    static const int16_t drop[5] = { 0, 29491, 18022, 6554, 6554 };     /* level lost at a full edge: 90% 55% 20% */
    return 32767 - ((c * drop[v->kind]) >> 15);
}

/* the edge's effect on the formants, by closure15 */
static inline void fm_close(fm_knobs *k, int kind, int32_t closure15)
{
    if (!closure15) return;
    int32_t f1 = k->hz[0] >> 8, f2 = k->hz[1] >> 8, c = closure15;
    switch (kind) {
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
    const int32_t breath = k->breath, voice = 32767 - ((k->breath * 22937) >> 15);
    const int32_t tc = 8192 + ((k->bright * 16384) >> 15);               /* source low-pass: soft dark, hard brighter */

    uint32_t phase = v->phase, rng = v->rng, vib = v->vib;
    int32_t tilt = v->tilt, big = 0, g = v->gain, gstep = (gain15 - v->gain) / count, dc = v->dc;
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
            s = (sine[a] * (FM_SRC_AMP * 2 / 5)) >> 15; fl = (32767 - sine[(a + 2048) & 8191]) >> 1; open = 1;
        } else if (p < 18350) {
            int32_t a = ((p - 13107) * 25) >> 6;                         /* 0 .. pi/2 */
            s = -((sine[a] * FM_SRC_AMP) >> 15); fl = sine[a + 2048]; open = 1;
        } else { s = 0; fl = 0; open = 0; }
        s += ((fl - FM_FLOW_MEAN) * FM_BODY) >> 15;
        s = (s * voice) >> 15;
        int32_t noise = ((int32_t)fm_rand(&rng) >> 19) * breath >> 13;  /* +-16384 at full white */
        s += open ? noise : noise >> 1;
        tilt += ((s - tilt) * tc) >> 15;

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
        int32_t o = l4 + (((fl - FM_FLOW_MEAN) * FM_CHEST) >> 15);
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
    v->phase = phase; v->rng = rng; v->vib = vib; v->tilt = tilt; v->dc = dc;
    v->low[0] = l0; v->low[1] = l1; v->low[2] = l2; v->low[3] = l3; v->low[4] = l4;
    v->band[0] = b0; v->band[1] = b1; v->band[2] = b2; v->band[3] = b3; v->band[4] = b4;
    return big;
}
#endif
