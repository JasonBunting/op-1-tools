/* Ratatoskr's DSP: a grain cloud over the sampler slot's recording, read around a playhead (the squirrel) that moves
 * through a range of it. Pure C (no OP-1 calls), so the host test can run it; ratatoskr.c is the OP-1 glue and the
 * screen.
 *
 * The playhead (rt_roam) is shared by every voice and moves once per audio block, in one of six motions: hold,
 * forward, reverse, bounce, wander, jump. Its route is the range: a window of the recording centred on the position,
 * slid inward at the ends. The first key after all keys are up starts it at the position again; turning the position
 * moves it there at once. Each voice launches grains around it (scattered) at its own key's pitch: C4 plays the
 * recording at its own speed. Each grain keeps the size and shape it was launched with.
 *
 * Numbers: the recording is int16 mono at 44.1 kHz; a voice renders RT_BLOCK samples at 88.2 kHz, so one block covers
 * 64 source samples at 1x. Grain positions are 22.10 fixed point; the playhead is Q8 source samples.
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#ifndef RT_DSP_H
#define RT_DSP_H
#include <stdint.h>

#define RT_BLOCK         128          /* voice samples per render call (88.2 kHz) */
#define RT_BLOCKS_PER_S  689          /* render calls per second (44100 / 64) */
#define RT_GRAINS        8            /* grains per voice: 4x overlap needs 4-5 at once */
#define RT_FRAC          10           /* grain position fraction bits */
#define RT_INC_C4        25480370u    /* as Hundekraft's granular: inc / (RT_INC_C4 >> RT_FRAC) = 0.5 at C4, which is
                                       * one source sample per two voice samples: the recording's own speed */
#define RT_MIN_LEN       256          /* shorter than this counts as nothing recorded */
#define RT_SR            44100

/* ---- the controls ----
 * page 1: blue position, green size, white scatter, orange motion; page 2 (encoder presses): blue range, green shape,
 * orange speed (white's press opens the sample browser); Shift + green (the engine's record): overlap. Words 0..32767. */
enum { RT_HOLD, RT_FORWARD, RT_REVERSE, RT_BOUNCE, RT_WANDER, RT_JUMP, RT_MOTIONS };
enum { RT_HANN, RT_TRIANGLE, RT_PLUCK, RT_SWELL, RT_SHAPES };
typedef struct {
    int32_t position, size, scatter, motion;    /* page 1 */
    int32_t range, shape, speed;                /* page 2 */
    int32_t overlap;                            /* Shift + green: the record's step (0 = 2x) */
} rt_knobs;

static inline int32_t rt_clamp(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : (x > hi ? hi : x); }

/* 2^(x / 4096) x 65536 for x in -16 x 4096 .. 14 x 4096 (Q12 octaves in, Q16 out); the fraction by a quadratic, within
 * 0.3% */
static inline int32_t rt_exp2(int32_t x)
{
    int32_t i = x >> 12, f = x & 4095;                               /* floor, so f is 0 .. 4095 */
    int32_t m = 65536 + ((f * (2690 + ((f * 1406) >> 12))) >> 8);   /* 2^f ~ 1 + f (0.6567 + 0.3433 f) */
    return i >= 0 ? m << i : m >> -i;
}

/* size in voice samples (88.2 kHz): 12 .. 250 ms, logarithmic (4.38 octaves); 50 ms at 15400 */
#define RT_SIZE_MIN 1058
static inline int32_t rt_size(int32_t k)
{
    int32_t o = (rt_clamp(k, 0, 32767) * 17945) >> 15;              /* Q12 octaves: 0 .. 4.381 */
    return (RT_SIZE_MIN * (rt_exp2(o) >> 6)) >> 10;
}
/* scatter in source samples, to either side: 0 .. 500 ms, squared so the short end gets most of the turn */
static inline int32_t rt_scatter(int32_t k)
{
    k = rt_clamp(k, 0, 32767);
    return (((k * k) >> 15) * 22050) >> 15;
}
static inline int rt_motion(int32_t k) { return rt_clamp((rt_clamp(k, 0, 32767) * RT_MOTIONS) >> 15, 0, RT_MOTIONS - 1); }
static inline int rt_shape(int32_t k) { return rt_clamp((rt_clamp(k, 0, 32767) * RT_SHAPES) >> 15, 0, RT_SHAPES - 1); }
/* overlap x 8 from the record's step: 0 (no record, a new sound) is 2x, then up to 4x and round to 0.5x .. 1.5x */
#define RT_OVL_STEPS 8
static inline int32_t rt_ovl8(int32_t step)
{
    static const uint8_t t[RT_OVL_STEPS] = { 16, 20, 24, 28, 32, 4, 8, 12 };
    return t[rt_clamp(step, 0, RT_OVL_STEPS - 1)];
}
/* range in Q15 of the recording: 5% .. 100% */
static inline int32_t rt_range_q15(int32_t k) { return 1638 + ((rt_clamp(k, 0, 32767) * 31129) >> 15); }

/* speed, two logarithmic halves meeting at the middle of the knob (the spec's starting rates):
 * travel (forward, reverse, bounce), Q16 x the recording's speed: 0.05x .. 0.25x (middle) .. 2x;
 * events (wander's targets, jumps), Q16 per second: 0.1 .. 1 (middle) .. 4 a second */
static inline int32_t rt_travel_q16(int32_t k)
{
    int32_t d = (rt_clamp(k, 0, 32767) - 16384) >> 2;               /* -4096 .. 4095 */
    return rt_exp2(-8192 + ((d * (d < 0 ? 9511 : 12288)) >> 12));  /* 0.25 x 2^(d log2 5 or d x 3) */
}
static inline int32_t rt_events_q16(int32_t k)
{
    int32_t d = (rt_clamp(k, 0, 32767) - 16384) >> 2;
    return rt_exp2((d * (d < 0 ? 13607 : 8192)) >> 12);              /* 1 x 2^(d log2 10 or d x 2) */
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

/* a share of len (up to 264600, so len >> 3 times a Q15 fits 32 bits; 8-sample steps are plenty) */
static inline int32_t rt_at(int32_t k, int32_t len) { return ((len >> 3) * rt_clamp(k, 0, 32767)) >> 12; }

/* the range: rt_range_q15 of the recording centred on the position, slid inward at the ends to keep its width */
static inline void rt_route(int32_t len, int32_t position, int32_t range, int32_t *lo, int32_t *hi)
{
    int32_t w = rt_clamp(rt_at(rt_range_q15(range), len), 1, len - 1);
    int32_t a = rt_clamp(rt_at(position, len), 0, len - 1) - (w >> 1);
    a = rt_clamp(a, 0, len - 1 - w);
    *lo = a; *hi = a + w;
}

/* ---- the windows: 256 steps of each shape (Q15, 0 at both ends), with a guard entry; built once ---- */
typedef int16_t rt_shapes[RT_SHAPES][257];
/* sine: an 8192-entry sine table (sin(2 pi k / 8192) x 32767) */
static inline int32_t rt_sin2(const int16_t *sine, int32_t a)          /* sin^2 for a quarter turn a = 0 .. 2048 */
{
    int32_t s = sine[rt_clamp(a, 0, 2048) & 8191];
    return (s * s) >> 15;
}
static void rt_shapes_init(rt_shapes t, const int16_t *sine)
{
    for (int i = 0; i <= 256; i++) {
        int32_t h = sine[(i * 16) & 8191];                                     /* sin(pi t): half a turn */
        t[RT_HANN][i] = (int16_t)((h * h) >> 15);                              /* sin^2(pi t) */
        t[RT_TRIANGLE][i] = (int16_t)rt_clamp(i <= 128 ? i * 256 : (256 - i) * 256, 0, 32767);
        /* pluck: a raised-cosine rise to its peak at 15% (38 / 256), then a long one down; swell: the mirror */
        const int p = 38;
        int32_t pl = i <= p ? rt_sin2(sine, i * 2048 / p) : rt_sin2(sine, (256 - i) * 2048 / (256 - p));
        int j = 256 - i;
        int32_t sw = j <= p ? rt_sin2(sine, j * 2048 / p) : rt_sin2(sine, (256 - j) * 2048 / (256 - p));
        t[RT_PLUCK][i] = (int16_t)pl;
        t[RT_SWELL][i] = (int16_t)sw;
    }
    for (int s = 0; s < RT_SHAPES; s++) { t[s][0] = 0; t[s][256] = 0; }
}

/* ---- the playhead ---- */
typedef struct {
    int32_t pos;            /* Q8 source samples */
    int32_t dir;            /* bounce: +1 up, -1 down */
    int32_t from, to;       /* wander: the glide's start and target (Q8) */
    int32_t t, period;      /* blocks into the current wander glide or jump, and its length */
    int32_t mode;           /* the motion last step (-1: none yet) */
    int32_t was_position;   /* the position knob last step, to see it turned */
    uint32_t rng;
    int32_t lo, hi, read;   /* the last step's route and read point (source samples), for the screen */
} rt_roam;

static inline void rt_roam_init(rt_roam *r) { r->mode = -1; r->was_position = -1; r->dir = 1; if (!r->rng) r->rng = 0x1234567u; }

/* blocks per event at k's speed, at least 1 */
static inline int32_t rt_period(int32_t k)
{
    int32_t e = rt_events_q16(k) >> 6;                                   /* Q10 per second */
    return e > 0 ? rt_clamp((RT_BLOCKS_PER_S << 10) / e, 1, 7000) : 7000;
}

/* one audio block of the playhead. restart: the first key of a new phrase (back to the position). Returns the read
 * point (source samples) the grains launch around. */
static inline int32_t rt_roam_step(rt_roam *r, int32_t len, const rt_knobs *k, int restart)
{
    int32_t lo, hi;
    rt_route(len, k->position, k->range, &lo, &hi);
    const int32_t lo8 = lo << 8, hi8 = hi << 8, span8 = hi8 - lo8;
    const int mode = rt_motion(k->motion);
    const int32_t home = rt_clamp(rt_at(k->position, len), lo, hi) << 8;
    if (restart || k->position != r->was_position || r->mode < 0) {
        r->pos = home;
        r->t = r->period = 0;
        r->dir = mode == RT_REVERSE ? -1 : 1;
    }
    if (mode != r->mode && r->mode >= 0) {                                 /* a new motion from where it is now */
        r->t = r->period = 0;
        if (mode == RT_BOUNCE) r->dir = r->pos >= hi8 - 256 ? -1 : 1;
    }
    r->mode = mode;
    r->was_position = k->position;
    int32_t p = rt_clamp(r->pos, lo8, hi8);                               /* the range may have moved under it */
    const int32_t step = rt_travel_q16(k->speed) >> 2;                    /* Q8 per block: 1x = 64 << 8 */
    switch (mode) {
    case RT_FORWARD:
        p += step;
        if (p > hi8) p = span8 ? lo8 + (p - hi8) % span8 : lo8;          /* to the roots again */
        break;
    case RT_REVERSE:
        p -= step;
        if (p < lo8) p = span8 ? hi8 - (lo8 - p) % span8 : hi8;          /* to the crown again */
        break;
    case RT_BOUNCE:
        p += r->dir * step;
        for (int n = 0; n < 8 && (p > hi8 || p < lo8); n++) {
            if (p > hi8) { p = 2 * hi8 - p; r->dir = -1; } else { p = 2 * lo8 - p; r->dir = 1; }
        }
        p = rt_clamp(p, lo8, hi8);
        break;
    case RT_WANDER:                                                       /* glide to a target within 5% of the span */
        if (r->t >= r->period) {
            r->period = rt_period(k->speed);
            r->t = 0;
            r->from = p;
            int32_t reach = span8 / 20;
            int32_t d = reach ? (int32_t)(rt_rand(&r->rng) % (uint32_t)(2 * reach + 1)) - reach : 0;
            r->to = rt_clamp(p + d, lo8, hi8);
        }
        r->t++;
        /* (to - from) is at most span8 / 10 (< 2^24), t / period in Q8: fits */
        p = r->from + (int32_t)(((r->to - r->from) >> 4) * ((r->t << 8) / r->period) >> 4);
        p = rt_clamp(p, lo8, hi8);
        break;
    case RT_JUMP:
        if (r->t >= r->period) {                                         /* (period 0: just started, stay a while) */
            if (r->period && span8 > 0) p = lo8 + (int32_t)(rt_rand(&r->rng) % (uint32_t)span8);
            r->period = rt_period(k->speed);
            r->t = 0;
        }
        r->t++;
        break;
    default:                                                              /* hold */
        break;
    }
    r->pos = p;
    r->lo = lo; r->hi = hi;
    r->read = rt_clamp(p >> 8, 0, len - 1);
    return r->read;
}

/* ---- the grains ---- */
typedef struct {
    uint32_t pos;           /* 22.10 source position */
    uint32_t rate;          /* 22.10 per voice sample */
    uint32_t wph;           /* window phase, 0 .. 2^31 = the whole grain */
    uint32_t winc;          /* window step per voice sample; 0 = free slot */
    const int16_t *win;     /* its shape, as launched */
} rt_grain;

typedef struct {
    rt_grain g[RT_GRAINS];
    int32_t next;           /* voice samples until the next grain */
    uint32_t rng;
    volatile uint32_t blocks;   /* render calls, so the screen can tell which voices sound */
} rt_voice;

/* the grains just launched, for the screen's flashes: their centres (source samples) and the block they launched in */
#define RT_FLASHES 32
typedef struct {
    int32_t at[RT_FLASHES];
    uint32_t blk[RT_FLASHES];
    volatile uint32_t n;    /* launches so far; the newest is at[(n - 1) % RT_FLASHES] */
} rt_flashes;

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

/* what a block of grains needs, worked out once a block */
typedef struct {
    int32_t size, ovl8, scatter;
    const int16_t *win;
} rt_launch;

static inline void rt_launch_of(const rt_knobs *k, const rt_shapes shapes, rt_launch *l)
{
    l->size = rt_size(k->size);
    l->ovl8 = rt_ovl8(k->overlap);
    l->scatter = rt_scatter(k->scatter);
    l->win = shapes[rt_shape(k->shape)];
}

static void rt_spawn(rt_voice *v, int32_t len, int32_t read, const rt_launch *l, uint32_t rate, rt_flashes *fl,
                     uint32_t blk)
{
    int i;
    for (i = 0; i < RT_GRAINS; i++) if (!v->g[i].winc) break;
    if (i == RT_GRAINS) return;                              /* all busy: skip this one */
    rt_grain *g = &v->g[i];
    int32_t size = l->size;
    const int32_t room = len - 3;                            /* the interpolation reads one past */
    uint32_t span32 = ((uint32_t)size * rate) >> RT_FRAC;   /* source samples the grain reads (size <= 22050, rate
                                                             * < 97000 in practice: fits 32 bits) */
    if (span32 > (uint32_t)room) {                          /* too long for the recording at this pitch: shorten */
        size = (int32_t)(((uint32_t)room << RT_FRAC) / rate);
        if (size < 64) return;
        span32 = ((uint32_t)size * rate) >> RT_FRAC;
    }
    const int32_t half = (int32_t)span32 >> 1;
    /* centres that keep the grain inside the recording, meeting the scatter around the read point */
    int32_t c0 = half, c1 = room - ((int32_t)span32 - half);
    int32_t a = read - l->scatter, b = read + l->scatter;
    if (a < c0) a = c0;
    if (b > c1) b = c1;
    int32_t c;
    if (a <= b) c = a + (b > a ? (int32_t)(rt_rand(&v->rng) % (uint32_t)(b - a + 1)) : 0);
    else c = read < c0 ? c0 : c1;                            /* they don't meet: the nearest whole grain */
    g->pos = (uint32_t)(c - half) << RT_FRAC;
    g->rate = rate;
    g->wph = 0;
    g->winc = 0x80000000u / (uint32_t)size;
    g->win = l->win;
    if (fl) { uint32_t n = fl->n; fl->at[n % RT_FLASHES] = c; fl->blk[n % RT_FLASHES] = blk; fl->n = n + 1; }
}

/* samples k = 0 .. seg-1 for which base + k * step stays below limit */
static inline int32_t rt_count(uint32_t base, uint32_t step, uint32_t limit, int32_t seg)
{
    if (base >= limit) return 0;
    uint32_t n = (limit - base + step - 1) / step;
    return n < (uint32_t)seg ? (int32_t)n : seg;
}

/* the live grains into acc[n0 .. n1) */
static void rt_piece(rt_voice *v, const int16_t *data, int32_t len, int32_t *acc, int32_t n0, int32_t n1)
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
        const int16_t *win = g->win;
        int32_t *a = acc + n0;
        for (int32_t n = 0; n < c; n++) {
            uint32_t idx = pos >> RT_FRAC;
            int32_t s0 = data[idx], s1 = data[idx + 1];
            int32_t s = s0 + (((s1 - s0) * (int32_t)(pos & ((1u << RT_FRAC) - 1))) >> RT_FRAC);
            uint32_t wi = wph >> 23;                         /* 0 .. 255 */
            int32_t w0 = win[wi], w1 = win[wi + 1];
            int32_t wv = w0 + (((w1 - w0) * (int32_t)((wph >> 8) & 0x7FFF)) >> 15);
            a[n] += (s * wv) >> 15;
            pos += rate;
            wph += winc;
        }
        g->pos = pos; g->wph = wph;
        if (c < seg || wph >= 0x80000000u) g->winc = 0;      /* its window ended, or it reached the recording's end */
    }
}

/* One block of one voice into out[RT_BLOCK]. read: the playhead's read point; rate: rt_rate(inc); gain: Q15 (velocity
 * and level folded in). fl, blk: where to note the launches for the screen (fl may be 0). */
static void rt_voice_render(rt_voice *v, const int16_t *data, int32_t len, int32_t read, const rt_launch *l,
                            uint32_t rate, int32_t gain, int16_t *out, rt_flashes *fl, uint32_t blk)
{
    static int32_t acc[RT_BLOCK];
    v->blocks++;
    if (!data || len < RT_MIN_LEN) {
        for (int n = 0; n < RT_BLOCK; n++) out[n] = 0;
        return;
    }
    int32_t interval = l->size * 8 / l->ovl8;                /* from the requested size, even when one is shortened */
    if (interval < 64) interval = 64;
    int32_t n = 0;
    while (n < RT_BLOCK) {
        if (v->next <= 0) {
            rt_spawn(v, len, read, l, rate, fl, blk);
            v->next = interval;
        }
        int32_t seg = v->next < RT_BLOCK - n ? v->next : RT_BLOCK - n;
        rt_piece(v, data, len, acc, n, n + seg);
        n += seg;
        v->next -= seg;
    }
    /* acc can reach a few times full scale where grains overlap: >> 2 keeps the product in 32 bits */
    const int32_t g = rt_clamp((rt_ovl_gain(l->ovl8) * rt_clamp(gain, 0, 65535)) >> 15, 0, 32767);
    for (n = 0; n < RT_BLOCK; n++) {
        int32_t x = ((acc[n] >> 2) * g) >> 13;
        out[n] = (int16_t)rt_clamp(x, -32768, 32767);
        acc[n] = 0;
    }
}

#endif
