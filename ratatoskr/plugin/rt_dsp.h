/* Ratatoskr's DSP: a grain cloud over the sampler slot's recording, read around a playhead that scurries through a
 * region of it. Pure C (no OP-1 calls), so the host test can run it; ratatoskr.c is the OP-1 glue and the screen.
 *
 * The playhead (rt_roam) is shared by every voice and moves once per audio block: through the region at the speed
 * knob's rate (backwards for the left half; frozen in the middle), wrapping at the region's ends, and, with wander up,
 * drifting off its line, jumping to random places and turning round now and then. Each voice (rt_voice) sprays
 * grains around it at its own key's pitch: C4 plays the recording at its own speed.
 *
 * Numbers: the recording is int16 mono at 44.1 kHz; a voice renders RT_BLOCK samples at 88.2 kHz, so one block covers
 * 64 source samples at 1x. Grain positions are 22.10 fixed point; the playhead is Q8 source samples.
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#ifndef RT_DSP_H
#define RT_DSP_H
#include <stdint.h>

#define RT_BLOCK         128          /* voice samples per render call (88.2 kHz) */
#define RT_SRC_BLOCK     64           /* source samples a block covers at 1x */
#define RT_BLOCKS_PER_S  689          /* render calls per second (44100 / 64) */
#define RT_GRAINS        6            /* grains per voice */
#define RT_FRAC          10           /* grain position fraction bits */
#define RT_INC_C4        25480370u    /* as Hundekraft's granular: inc / (RT_INC_C4 >> RT_FRAC) = 0.5 at C4, which is
                                       * one source sample per two voice samples: the recording's own speed */
#define RT_MIN_LEN       256          /* shorter than this counts as nothing recorded */

/* ---- knobs ----
 * page 1: blue position, green size, white speed, red density; page 2 (encoder presses): blue spray, green width,
 * white wander. Words are 0..32767. */
typedef struct {
    int32_t position, size, speed, density;     /* page 1 */
    int32_t spray, width, wander;               /* page 2 */
} rt_knobs;

static inline int32_t rt_clamp(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : (x > hi ? hi : x); }

/* grain size in voice samples: 10 ms .. 500 ms, squared so the short end is fine to set */
static inline int32_t rt_size(int32_t k)
{
    k = rt_clamp(k, 0, 32767);
    return 882 + (int32_t)((((k * k) >> 15) * 43218) >> 15);
}
/* overlap x 8: 4 (0.5x) .. 35 (4.4x) */
static inline int32_t rt_ovl8(int32_t k) { return 4 + (rt_clamp(k, 0, 32767) >> 10); }
/* spray in source samples: 0 .. 1.5 s, squared */
static inline int32_t rt_spray(int32_t k)
{
    k = rt_clamp(k, 0, 32767);
    return (((k * k) >> 15) * 33075) >> 14;                  /* 33075: the product stays in 32 bits */
}
/* speed, signed, Q16 (65536 = 1x): the middle (+-RT_SPEED_DEAD) is frozen, the rest squared out to 4x either way, so
 * 1x sits three quarters of the way out and the slow, stretched speeds get most of the turn */
#define RT_SPEED_DEAD 1000
static inline int32_t rt_speed_q16(int32_t k)
{
    int32_t d = rt_clamp(k, 0, 32767) - 16384, a = d < 0 ? -d : d;
    if (a <= RT_SPEED_DEAD) return 0;
    int32_t m = (a - RT_SPEED_DEAD) * 32767 / (16383 - RT_SPEED_DEAD);     /* 0 .. 32767 */
    if (m > 32767) m = 32767;
    int32_t s = ((m * m) >> 15) * 8;                                       /* Q15 x 4, as Q16: up to 4x */
    return d < 0 ? -s : s;
}
/* gain for overlapping grains (as Hundekraft's granular): min(32767, 32767 * 8 / ovl8 + 6000) */
static inline int32_t rt_ovl_gain(int32_t ovl8)
{
    if (ovl8 <= 8) return 32767;
    int32_t g = 32767 * 8 / ovl8 + 6000;
    return g > 32767 ? 32767 : g;
}

static inline uint32_t rt_rand(uint32_t *s)            /* xorshift32; never 0 */
{
    uint32_t x = *s ? *s : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

/* ---- the playhead ---- */
typedef struct {
    int32_t pos;            /* Q8 source samples */
    int32_t dir;            /* +1, or -1 after wander turned it round */
    int32_t drift, drift_to;/* Q8: wander's slow offset, and where it is heading */
    int32_t drift_left;     /* blocks until drift_to changes */
    uint32_t rng;
    int32_t lo, hi, read;   /* the last step's region and read point (source samples), for the screen */
} rt_roam;

/* a knob's share of len (up to 264600, so len >> 3 times a word fits 32 bits; 8-sample steps are plenty) */
static inline int32_t rt_at(int32_t k, int32_t len) { return ((len >> 3) * rt_clamp(k, 0, 32767)) >> 12; }

/* the region: around position, width of the recording wide, kept inside it */
static inline void rt_region(int32_t len, int32_t position, int32_t width, int32_t *lo, int32_t *hi)
{
    int32_t c = rt_at(position, len);
    int32_t half = rt_at(width, len) >> 1;
    int32_t a = c - half, b = c + half;
    if (a < 0) { b -= a; a = 0; }
    if (b > len - 1) { a -= b - (len - 1); b = len - 1; }
    if (a < 0) a = 0;
    *lo = a; *hi = b;
}

/* one audio block of the playhead; returns the read point (source samples) the grains spray around */
static inline int32_t rt_roam_step(rt_roam *r, int32_t len, const rt_knobs *k)
{
    int32_t lo, hi;
    rt_region(len, k->position, k->width, &lo, &hi);
    if (!r->dir) r->dir = 1;
    const int32_t lo8 = lo << 8, range8 = (hi - lo) << 8;
    const int32_t w = rt_clamp(k->wander, 0, 32767), w2 = (w * w) >> 15;
    if (range8 < (2 << 8)) {                                   /* no width: the playhead sits on the position */
        r->pos = lo8;
    } else {
        const int32_t step = rt_speed_q16(k->speed) >> 2;     /* Q8 per block: 1x = 64 << 8 */
        int32_t p = r->pos + r->dir * step - lo8;
        if (p < 0 || p >= range8) {                            /* wrapped, or the region moved under it */
            p %= range8;
            if (p < 0) p += range8;
        }
        if (w2) {
            /* jumps: up to 0.6 a second; turns: up to 0.8 a second (probabilities per block, Q32 thresholds) */
            if (rt_rand(&r->rng) < (uint32_t)w2 * 114u) p = (int32_t)(rt_rand(&r->rng) % (uint32_t)range8);
            if (rt_rand(&r->rng) < (uint32_t)w2 * 152u) r->dir = -r->dir;
        }
        r->pos = lo8 + p;
    }
    /* the drift: a new aim every 0.3 .. 1.2 s, up to a quarter of the region either way, followed with a ~0.1 s lag */
    if (--r->drift_left <= 0) {
        r->drift_left = 207 + (int32_t)(rt_rand(&r->rng) % 620u);
        int32_t u = (int32_t)(rt_rand(&r->rng) >> 17) - 16384;            /* -16384 .. 16383 */
        r->drift_to = (((u * w) >> 15) * (range8 >> 14)) >> 2;            /* +-range8 / 4 at full wander */
    }
    if (!w) r->drift_to = 0;
    r->drift += (r->drift_to - r->drift) >> 6;
    int32_t read = (r->pos + r->drift) >> 8;
    read = rt_clamp(read, 0, len - 1);
    r->lo = lo; r->hi = hi; r->read = read;
    return read;
}

/* ---- the grains ---- */
typedef struct {
    uint32_t pos;           /* 22.10 source position */
    uint32_t rate;          /* 22.10 per voice sample */
    uint32_t wph;           /* window phase, 0 .. 2^31 = the whole grain */
    uint32_t winc;          /* window step per voice sample; 0 = free slot */
} rt_grain;

typedef struct {
    rt_grain g[RT_GRAINS];
    int32_t next;           /* voice samples until the next grain */
    uint32_t rng;
    volatile uint32_t blocks;   /* render calls, so the screen can tell which voices sound */
} rt_voice;

static inline void rt_voice_note(rt_voice *v, int index)
{
    for (int i = 0; i < RT_GRAINS; i++) v->g[i].winc = 0;
    v->next = 0;
    v->rng ^= 0x2545F491u * (uint32_t)(index + 1) + v->blocks;
    if (!v->rng) v->rng = 1;
}

/* the playback rate (22.10 source samples per voice sample) for a voice's phase increment */
static inline uint32_t rt_rate(uint32_t inc)
{
    uint32_t r = inc / (RT_INC_C4 >> RT_FRAC);
    return r < 16 ? 16 : r;
}

static void rt_spawn(rt_voice *v, int32_t len, int32_t read, int32_t spray, uint32_t rate, int32_t size)
{
    int i;
    for (i = 0; i < RT_GRAINS; i++) if (!v->g[i].winc) break;
    if (i == RT_GRAINS) return;                              /* all busy: skip this one */
    rt_grain *g = &v->g[i];
    uint32_t span32 = ((uint32_t)size * rate) >> RT_FRAC;   /* source samples the grain reads (size <= 44100, rate
                                                             * < 97000 in practice: the product fits 32 bits) */
    int32_t span = span32 > (uint32_t)len ? len : (int32_t)span32;
    int32_t start = read - (span >> 1);                      /* centred on the read point */
    if (spray) start += (int32_t)(rt_rand(&v->rng) % (uint32_t)(2 * spray + 1)) - spray;
    if (start > len - 2 - span) start = len - 2 - span;      /* kept whole inside the recording */
    if (start < 0) start = 0;
    g->pos = (uint32_t)start << RT_FRAC;
    g->rate = rate;
    g->wph = 0;
    g->winc = 0x80000000u / (uint32_t)size;
}

/* samples k = 0 .. seg-1 for which base + k * step stays below limit */
static inline int32_t rt_count(uint32_t base, uint32_t step, uint32_t limit, int32_t seg)
{
    if (base >= limit) return 0;
    uint32_t n = (limit - base + step - 1) / step;
    return n < (uint32_t)seg ? (int32_t)n : seg;
}

/* the live grains into acc[n0 .. n1) */
static void rt_piece(rt_voice *v, const int16_t *data, int32_t len, int32_t *acc, int32_t n0, int32_t n1,
                     const int16_t *sine)
{
    const int32_t seg = n1 - n0;
    const uint32_t lim = (uint32_t)(len - 1) << RT_FRAC;
    for (int i = 0; i < RT_GRAINS; i++) {
        rt_grain *g = &v->g[i];
        if (!g->winc) continue;
        int32_t c = rt_count(g->pos, g->rate, lim, seg);
        int32_t cw = rt_count(g->wph, g->winc, 0x80000000u, seg);
        if (cw < c) c = cw;
        uint32_t pos = g->pos, wph = g->wph;
        const uint32_t rate = g->rate, winc = g->winc;
        int32_t *a = acc + n0;
        for (int32_t n = 0; n < c; n++) {
            uint32_t idx = pos >> RT_FRAC;
            int32_t s0 = data[idx], s1 = data[idx + 1];
            int32_t s = s0 + (((s1 - s0) * (int32_t)(pos & ((1u << RT_FRAC) - 1))) >> RT_FRAC);
            int32_t wv = sine[wph >> 19];                    /* sin(pi t), 0 .. 32767 */
            wv = (wv * wv) >> 15;                            /* Hann */
            a[n] += (s * wv) >> 15;
            pos += rate;
            wph += winc;
        }
        g->pos = pos; g->wph = wph;
        if (c < seg || wph >= 0x80000000u) g->winc = 0;      /* its window ended, or it reached the recording's end */
    }
}

/* One block of one voice into out[RT_BLOCK]. read: the playhead's read point; rate: rt_rate(inc); gain: Q15 (velocity
 * and level folded in). sine: an 8192-entry sine table (sin(2 pi k / 8192) x 32767). */
static void rt_voice_render(rt_voice *v, const int16_t *data, int32_t len, int32_t read, const rt_knobs *k,
                            uint32_t rate, int32_t gain, int16_t *out, const int16_t *sine)
{
    static int32_t acc[RT_BLOCK];
    v->blocks++;
    if (!data || len < RT_MIN_LEN) {
        for (int n = 0; n < RT_BLOCK; n++) out[n] = 0;
        return;
    }
    const int32_t size = rt_size(k->size), ovl8 = rt_ovl8(k->density), spray = rt_spray(k->spray);
    int32_t interval = size * 8 / ovl8;
    if (interval < 64) interval = 64;
    int32_t n = 0;
    while (n < RT_BLOCK) {
        if (v->next <= 0) {
            rt_spawn(v, len, read, spray, rate, size);
            v->next = interval;
        }
        int32_t seg = v->next < RT_BLOCK - n ? v->next : RT_BLOCK - n;
        rt_piece(v, data, len, acc, n, n + seg, sine);
        n += seg;
        v->next -= seg;
    }
    /* acc can reach a few times full scale where grains overlap: >> 2 keeps the product in 32 bits */
    const int32_t g = rt_clamp((rt_ovl_gain(ovl8) * rt_clamp(gain, 0, 65535)) >> 15, 0, 32767);
    for (n = 0; n < RT_BLOCK; n++) {
        int32_t x = ((acc[n] >> 2) * g) >> 13;
        out[n] = (int16_t)rt_clamp(x, -32768, 32767);
        acc[n] = 0;
    }
}

#endif
