/* Norn's weave: the three threads the keepers hold, as integers for the GUI thread. The geometry is the approved
 * mockup's point() (norn/design/norn-weave-comparison.html), ported formula for formula: an illustration of the
 * settings, not a waveform. No SDK calls: the host test checks it against the mockup's formulas in floating point.
 *
 * Numbers: screen coordinates are Q10 (320 x 160 units, 1.0 = 1024); knobs and u (along a thread, 0 .. 1) are Q15;
 * angles are Q16 turns (65536 = one turn), read from a full 8,192-point sine table (OP1_SINE_TABLE on the device).
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#ifndef NORN_WEAVE_H
#define NORN_WEAVE_H
#include <stdint.h>

#define NW_Q(v)     ((int32_t)(v) << 10)
#define NW_END_X    NW_Q(310)                 /* every thread ends here */
#define NW_GRID_X0  90                        /* threads are sampled at shared x: 90, 92, .. 310 (crossings line up) */
#define NW_GRID_N   111
#define NW_CROSS_X0 110                       /* crossings are marked from here on */
#define NW_CROSS_MAX 16

/* the hands (thread starts) and the threads' right ends: top green, middle white, bottom orange */
static const int32_t nw_hand_x[3] = { 81633, 86149, 89539 };          /* 79.72, 84.13, 87.44 */
static const int32_t nw_hand_y[3] = { 75069, 107080, 139469 };        /* 73.31, 104.57, 136.20 */
static const int32_t nw_end_y[3] = { NW_Q(120), NW_Q(108), NW_Q(96) };

typedef struct {
    int model;                                /* 0 waveshaper, 1 FM, 2 formant, 3 wave terrain */
    int32_t g, w, o;                          /* green, white, orange: Q15 0 .. 32767 */
    uint32_t t_ms;                            /* time, for the held-note drift */
    int drift;                                /* 1 while a note is held: the threads sway a pixel */
} nw_in;

static inline int32_t nw_sin(const int16_t *tab, int32_t turns) { return tab[((uint32_t)turns >> 3) & 8191]; }

/* the thread's offset from its straight path at u, in Q10 pixels (before the 0.88 scale): the mockup's `wave` */
static inline int32_t nw_wave(const int16_t *tab, const nw_in *in, int i, int32_t u)
{
    const int32_t g = in->g, w = in->w, a = 2 * in->o - 32767, i3 = i * 21845;   /* a: 2 morph - 1; i3: i/3 turn */
    switch (in->model) {
    case 0: {   /* (8 + 7|2g-1|)(sin f + 0.85 (g - .5) cos 2f + 0.45 w sin 3f) + 19 a (u - .35) */
        const int32_t f = ((u * (4563 + ((w * 6519) >> 15))) >> 11) + i3;        /* u (7 + 10 w) / 2 pi */
        const int32_t s = nw_sin(tab, f)
                        + ((((g - 16384) * 27853) >> 15) * nw_sin(tab, 2 * f + 16384) >> 15)
                        + (((w * 14746) >> 15) * nw_sin(tab, 3 * f) >> 15);
        const int32_t amp = 8192 + ((__builtin_abs(2 * g - 32767) * 7168) >> 15);
        return ((amp * s) >> 15) + ((((a * (u - 11469)) >> 15) * 19456) >> 15);
    }
    case 1: {   /* (10 + 13 w) sin(p + warp + 1.15 w sin 2p) + grit; warp left of centre, grit right */
        const int32_t p = ((u * (5215 + ((g * 7171) >> 15))) >> 11) + i3;        /* u (8 + 11 g) / 2 pi */
        int32_t warp = 0;
        if (a < 0) warp = ((((-a) * nw_sin(tab, ((u * 7823) >> 11) + i * 10430)) >> 15) * 25033) >> 15;
        const int32_t inner = (((w * nw_sin(tab, 2 * p)) >> 15) * 11995) >> 15;
        int32_t v = ((10240 + ((w * 13312) >> 15)) * nw_sin(tab, p + warp + inner)) >> 15;
        if (a > 0) v += (((a * (2048 + ((w * 4096) >> 15))) >> 15) * nw_sin(tab, 4 * p)) >> 15;
        return v;
    }
    case 2: {   /* 8 sin(u (8 + 20 w) + i 2.0944) + 25 (g - .5)(i - 1) */
        const int32_t f = ((u * (5215 + ((w * 13038) >> 15))) >> 11) + i3;
        return ((8192 * nw_sin(tab, f)) >> 15) + (((g - 16384) * (i - 1) * 25600) >> 15);
    }
    default: {  /* (7 + 15 w)(sin(q + 2.4 a) + 0.36 sin(q (1 + 2.45 g) + 7 g)) */
        const int32_t q = ((u * (3260 + ((w * 7823) >> 15))) >> 11) + i3;        /* u (5 + 12 w) / 2 pi */
        const int32_t q2 = ((q >> 2) * (4096 + ((g * 10035) >> 15))) >> 10;
        const int32_t s = nw_sin(tab, q + ((a * 25033) >> 15))
                        + ((11796 * nw_sin(tab, q2 + ((g * 36506) >> 14))) >> 15);
        return ((7168 + ((w * 15360) >> 15)) * s) >> 15;
    }
    }
}

/* thread i's height at u (Q15, 0 .. 32768), Q10: from the hand, easing to the right end (u^2 (3 - 2u)), the wave
 * (and the held-note drift) faded in and out by sin^2(pi u), so both ends are smooth */
static inline int32_t nw_y(const int16_t *tab, const nw_in *in, int i, int32_t u)
{
    int32_t wave = nw_wave(tab, in, i, u);
    if (in->drift) {                                           /* 1.1 sin(18 u - 3 t + i) */
        const int32_t tq = (int32_t)(((uint32_t)in->t_ms * 2003u) >> 6);   /* 3 t rad as Q16 turns, t in ms */
        wave += (1126 * nw_sin(tab, ((u * 11734) >> 11) - tq + i * 10430)) >> 15;
    }
    const int32_t u2 = (u * u) >> 15;
    const int32_t ease = ((u2 >> 1) * (98304 - 2 * u)) >> 14;
    const int32_t s = nw_sin(tab, u), env = (s * s) >> 15;     /* u as Q16 turns is half a turn at u = 1: sin(pi u) */
    return nw_hand_y[i] + (((nw_end_y[i] - nw_hand_y[i]) * ease) >> 15) + ((env * ((wave * 28836) >> 15)) >> 15);
}

/* u (Q15) of screen x (Q10) on thread i */
static inline int32_t nw_u_at(int i, int32_t x)
{
    const int32_t d = x - nw_hand_x[i], span = NW_END_X - nw_hand_x[i];
    if (d <= 0) return 0;
    if (d >= span) return 32768;
    return (int32_t)(((uint32_t)d << 5) / (uint32_t)(span >> 10));          /* d < 2^18: the shift fits */
}

/* x (Q10) of u on thread i */
static inline int32_t nw_x_at(int i, int32_t u) { return nw_hand_x[i] + (((NW_END_X - nw_hand_x[i]) >> 5) * u >> 10); }

/* the three threads at the shared x grid: ys[i][k] at x = NW_GRID_X0 + 2k (stride 2 px: every second point drawn
 * when the frame governor is busy) */
static inline void nw_threads(const int16_t *tab, const nw_in *in, int32_t ys[3][NW_GRID_N])
{
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < NW_GRID_N; k++) ys[i][k] = nw_y(tab, in, i, nw_u_at(i, NW_Q(NW_GRID_X0 + 2 * k)));
}

/* where the threads' centrelines cross (from NW_CROSS_X0 on), closer than 5 px to another merged; returns the count */
static inline int nw_crossings(const int32_t ys[3][NW_GRID_N], int32_t cx[NW_CROSS_MAX], int32_t cy[NW_CROSS_MAX])
{
    int n = 0;
    for (int a = 0; a < 2; a++)
        for (int b = a + 1; b < 3; b++)
            for (int k = (NW_CROSS_X0 - NW_GRID_X0) / 2; k + 1 < NW_GRID_N; k++) {
                const int32_t d = ys[a][k] - ys[b][k], e = ys[a][k + 1] - ys[b][k + 1];
                if (!((d < 0 && e > 0) || (d > 0 && e < 0))) continue;
                const int32_t f = (d << 10) / (d - e);         /* Q10, 0 .. 1 (the threads stay within 160 px: no overflow) */
                const int32_t x = NW_Q(NW_GRID_X0 + 2 * k) + 2 * f;
                const int32_t y = ys[a][k] + (((ys[a][k + 1] - ys[a][k]) * f) >> 10);
                int near = 0;
                for (int j = 0; j < n; j++)
                    if (__builtin_abs(cx[j] - x) + __builtin_abs(cy[j] - y) < NW_Q(5)) { near = 1; break; }
                if (near) continue;
                if (n == NW_CROSS_MAX) return n;
                cx[n] = x; cy[n] = y; n++;
            }
    return n;
}

#endif
