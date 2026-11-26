/* rotor147 DSP: a two-rotor rotary speaker for the OP-1's FX slot, inspired by the Motion Sound R3-147.
 * Portable integer code, no firmware addresses: rotor147.c adapts it to the SDK, test/host_test.c runs the same code.
 *
 * Wet path, sample by sample (FX rate 44,100 Hz, blocks of RC_N samples):
 *   input (master: (L + R) / 2) -> contour high-cut (one pole, 1 kHz .. open) -> drive (gain, soft knee, bias, make-up)
 *   -> DC block (10 Hz) -> Linkwitz-Riley 4th-order band split at 800 Hz (three Butterworth state-variable filters:
 *   the bands stay in phase, so horn and bass delays that differ, as at brake, only dent a narrow band at 800 Hz)
 *   high -> horn ring -> per microphone: fractional delay (Doppler), one-pole brightness, directional gain
 *   low  -> bass ring -> per output side: small fractional delay, amplitude modulation
 *   wet = horn mics + bass, saturated to int16; out = dry + (wet - dry) * mix.
 * Every delay, gain and brightness value comes from the rotor angle at the block's start and end and is interpolated
 * linearly across the block. Horn and bass have their own phase, speed and target speed; a mode change moves the
 * target, and the speed ramps linearly to it (rc_rotor_*).
 *
 * Numbers: signals are int16-sized ints in int32; filter states keep 12 extra fraction bits (Q12) and round on output,
 * so silence settles to exact zeros. Delays are Q16 samples, gains and coefficients Q15, mix Q14 (16384 = all wet).
 * The tuning constants below are engineering starting points chosen by ear and reasoning, not R3-147 measurements.
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#ifndef RC_DSP_H
#define RC_DSP_H
#include <stdint.h>

#ifndef RC_ASSERT
#define RC_ASSERT(x) ((void)0)
#endif
#if defined(__GNUC__)
#define RC_INLINE static inline __attribute__((always_inline))
#else
#define RC_INLINE static inline
#endif

#define RC_N      64                       /* samples per FX block */
#define RC_RATE   44100
#define RC_RING   256                      /* delay ring length, a power of two (5.8 ms) */
#define RC_MASK   (RC_RING - 1)

/* ---- tuning (see README "Starting values") ---- */
#define RC_HZ(centi)   ((int32_t)(((uint64_t)(centi) << 32) / (RC_RATE * 100ull)))  /* phase step per sample */
#define RC_BLOCKS(ms)  ((int32_t)(((ms) * (int64_t)RC_RATE + RC_N * 500) / (RC_N * 1000)))
#define RC_US(us)      ((int32_t)(((int64_t)(us) * RC_RATE * 65536 + 500000) / 1000000))  /* Q16 samples */
#define RC_DEG(d)      ((uint32_t)(int32_t)((int64_t)(d) * 4294967296ll / 360))

/* crossover: Zavalishin's TPT state-variable filter, Butterworth (k = sqrt 2), g = tan(pi 800 / fs); Q15 */
#define RC_SV_A1       30230               /* 1 / (1 + g (g + k)) */
#define RC_SV_A2       1725                /* g a1 */
#define RC_SV_A3       98                  /* g a2 (98.4: the three filters share it, so the bands still sum flat) */
#define RC_SV_K        46341               /* sqrt 2 */
#define RC_R_DC        32721               /* DC block pole, 10 Hz: 32768 * exp(-2 pi f / fs) */
#define RC_BIAS_MAX    3277                /* drive asymmetry at full drive: 0.1 of full scale, before the knee */
#define RC_ZONE1       10923               /* blue: below = brake */
#define RC_ZONE2       21845               /* blue: below = slow, from here = fast */

typedef struct { int32_t slow, fast, up, down, stop; } rc_motor;   /* speeds (RC_HZ) and ramp times (blocks) */
static const rc_motor RC_HORN = { RC_HZ(80), RC_HZ(670), RC_BLOCKS(1000), RC_BLOCKS(1500), RC_BLOCKS(2000) };
static const rc_motor RC_BASS = { RC_HZ(70), RC_HZ(550), RC_BLOCKS(3000), RC_BLOCKS(4000), RC_BLOCKS(5000) };

/* One horn microphone: angle from the rotor's zero, mean delay and excursion (Q16 samples), gain = gbase + gdep*cos,
 * brightness (one-pole coefficient) = kbase + kdep*cos; cos = 1 when the horn points at the microphone, where the
 * delay is shortest, the level highest and the sound brightest. */
typedef struct { uint32_t angle; int32_t mean, exc, gbase, gdep, kbase, kdep; } rc_mic;
/* close pair: gain 0.44 .. 1.0, brightness 3 kHz pole .. open; distant pair: 0.70 .. 0.94, 6 kHz .. 30000, and
 * 0.15 ms further away: blended with the close pair, the first comb notch sits near 3.3 kHz (0.5 ms put it at 1 kHz) */
#define RC_NEAR(a) { RC_DEG(a), RC_US(2000), RC_US(300), 23593, 9175, 22082, 10685 }
#define RC_FAR(a)  { RC_DEG(a), RC_US(2150), RC_US(200), 26869, 3932, 24415, 5585 }
/* Angles: the close pair 120 degrees apart, the distant pair 180 (opposite sides), for a wide master image; the synth
 * and drum positions hear only the left pair. */
static const rc_mic RC_MICS[4] = { RC_NEAR(-60), RC_FAR(-90), RC_NEAR(60), RC_FAR(90) };   /* L near, L far, R near, R far */
/* Bass rotor, per output side: angle, then close and distant mean delay, excursion and AM depth (gain 1 - depth/2
 * +- depth/2): close 0.5 .. 1.0 and +-0.1 ms, distant 0.75 .. 1.0 and +-0.07 ms (0.2 ms dug a -15 dB hole at 1 kHz at
 * one brake angle, where the bands overlap). Mean delays match the horn's so the
 * bands line up at the crossover. */
typedef struct { uint32_t angle; int32_t nmean, fmean, nexc, fexc, ndep, fdep; } rc_bassmic;
static const rc_bassmic RC_BASSMICS[2] = {
    { RC_DEG(-75), RC_US(2000), RC_US(2150), RC_US(100), RC_US(70), 16384, 8192 },
    { RC_DEG(75),  RC_US(2000), RC_US(2150), RC_US(100), RC_US(70), 16384, 8192 },
};
/* drive make-up gain (Q15) at drive 0, 1/32 .. 1 (G = 1 + 15 d^2): holds a -12 dBFS sine at its input level (AC RMS,
 * computed in floating point over the knee below); quieter inputs come out louder, hotter ones quieter. 32768 at 0. */
static const uint16_t RC_MAKEUP[33] = {
    32768, 32295, 30954, 28951, 26546, 23985, 21454, 19076, 16913, 15000, 13362, 11986, 10841, 9895, 9118, 8484,
    7974, 7572, 7269, 7050, 6890, 6764, 6661, 6575, 6502, 6440, 6386, 6339, 6298, 6261, 6229, 6199, 6173 };
/* page 2, each read at knob / 1024 with linear interpolation (33 points, Q12 unless noted):
 * blue: speed, both rotors' slow and fast speeds x 2^((q - 16384) / 8192): x0.25 .. x1 (centre) .. x4 */
static const uint16_t RC_SPEED[33] = {
    1024, 1117, 1218, 1328, 1448, 1579, 1722, 1878, 2048, 2233, 2435, 2656, 2896, 3158, 3444, 3756, 4096,
    4467, 4871, 5312, 5793, 6317, 6889, 7512, 8192, 8933, 9742, 10624, 11585, 12634, 13777, 15024, 16384 };
/* green: acceleration, every ramp time x5 (left, slow) .. x1 (centre) .. x0.1 (right, quick), exponential each side */
static const uint16_t RC_ACCEL[33] = {
    20480, 18520, 16748, 15145, 13696, 12385, 11200, 10128, 9159, 8282, 7490, 6773, 6125, 5539, 5009, 4529, 4096,
    3547, 3072, 2660, 2303, 1995, 1727, 1496, 1295, 1122, 971, 841, 728, 631, 546, 473, 410 };
/* white: contour, the high-cut before the drive: one-pole coefficient (Q15) for 1 kHz .. 16 kHz in equal octave steps,
 * open at the right end. Read through rc_contour: the last half-step of the knob (from RC_OPEN, where the screen says
 * "open") is a coefficient of exactly 1, the input passed bit for bit; the step before it runs 16 kHz to open. The word
 * 23807 is 8 kHz; the default is open. */
static const uint16_t RC_CONTOUR[33] = {
    4351, 4728, 5133, 5570, 6040, 6545, 7086, 7665, 8283, 8942, 9641, 10383, 11167, 11993, 12860, 13767, 14712,
    15691, 16701, 17738, 18795, 19866, 20943, 22019, 23083, 24127, 25140, 26113, 27035, 27897, 28693, 29415, 32768 };
#define RC_P2_DEFAULTS { 16384, 16384, 32767 }      /* speed x1, acceleration x1, contour open (plugin.json too) */
#define RC_OPEN 32256                               /* contour from here: open */

/* ---- state ---- */
typedef struct { uint32_t ph; int32_t sp, tgt, step, acc; } rc_rotor;   /* phase, speed, target (RC_HZ), step, ramp scale */
typedef struct {
    int16_t horn[RC_RING], bass[RC_RING];
    uint32_t w;                                  /* ring write index */
    rc_rotor rh, rb;
    int32_t cont, dcx, dcy;                      /* contour (Q12), DC block (input, Q12 output) */
    int32_t sv[3][2];                            /* crossover filters' integrator states (Q12): split, low, high */
    int32_t br[4];                               /* horn brightness states (Q12), RC_MICS order */
    int32_t blend, drive, mix, contour, speed;   /* smoothed controls: blend, drive, contour Q15, mix Q14, speed word */
    int32_t tblend, tdrive, tmix, tcontour, tspeed, accel, mode;   /* from rc_control (accel: Q12 ramp scale) */
    int32_t primed;                              /* 0 until the first render: controls and speeds jump there */
} rc_state;

RC_INLINE int16_t rc_sat16(int32_t x) { return (int16_t)(x > 32767 ? 32767 : (x < -32768 ? -32768 : x)); }
/* a * k / 32768 for any int32 a and |k| <= 32768, rounded on the low half (no 64-bit multiply) */
RC_INLINE int32_t rc_mul(int32_t a, int32_t k) { return (a >> 15) * k + (((a & 0x7FFF) * k + 16384) >> 15); }
/* cos of a phase (2^32 a turn) from an 8192-entry sine table, linearly interpolated; +-32767 */
RC_INLINE int32_t rc_cos(const int16_t *tab, uint32_t ph)
{
    ph += 0x40000000u;
    const uint32_t i = ph >> 19;
    const int32_t f = (int32_t)((ph >> 4) & 0x7FFF), a = tab[i], b = tab[(i + 1) & 8191];
    return a + (((b - a) * f) >> 15);
}
/* step (Q15 or Q14 values) toward a target by 1/16 a block (23 ms), landing exactly on it */
RC_INLINE int32_t rc_smooth(int32_t v, int32_t t)
{
    const int32_t d = t - v;
    return (d < 16 && d > -16) ? t : v + (d >> 4);
}
/* per-sample slope that does not overshoot (rounds toward zero) */
RC_INLINE int32_t rc_slope(int32_t a, int32_t b) { const int32_t d = b - a; return (d + (d < 0 ? RC_N - 1 : 0)) >> 6; }

static inline void rc_init(rc_state *s)
{
    uint32_t *p = (uint32_t *)s;
    for (uint32_t i = 0; i < sizeof *s / 4; i++) p[i] = 0;
}

/* ---- rotors ---- */
static inline int32_t rc_mode(int32_t blue_q15) { return blue_q15 < RC_ZONE1 ? 0 : (blue_q15 < RC_ZONE2 ? 1 : 2); }
RC_INLINE int32_t rc_mul12(int32_t a, int32_t k) { return (a >> 12) * k + (((a & 4095) * k) >> 12); }   /* a >= 0, k Q12 */
RC_INLINE int32_t rc_tab(const uint16_t *t, int32_t q)                                                /* q 0..32767 */
{
    const int32_t i = q >> 10;
    return t[i] + ((((int32_t)t[i + 1] - t[i]) * (q & 1023)) >> 10);
}
/* contour coefficient (Q15, 32768 = open) for a contour word: 16 kHz at 31744, open from RC_OPEN */
RC_INLINE int32_t rc_contour(int32_t q)
{
    if (q >= RC_OPEN) return 32768;
    return rc_tab(RC_CONTOUR, q < 31744 ? q : 31744 + (q - 31744) * 2);
}
/* new target: the mode's speed times the speed scale (Q12). The ramp takes the motor's time for that move, times the
 * acceleration scale, from the current speed (an interrupted ramp starts where it is): speeding up uses the up time
 * (also out of brake into slow), slowing uses the down time, and stopping the stop time. A target or acceleration
 * that changes mid-ramp starts a new ramp from where the speed is. */
static inline void rc_rotor_mode(rc_rotor *r, const rc_motor *m, int32_t mode, int32_t speed, int32_t acc, int jump)
{
    const int32_t tgt = mode == 0 ? 0 : rc_mul12(mode == 1 ? m->slow : m->fast, speed);
    if (jump) { r->sp = r->tgt = tgt; r->step = 0; r->acc = acc; return; }
    if (tgt == r->tgt && acc == r->acc) return;
    int32_t blocks = rc_mul12(tgt == 0 ? m->stop : (tgt > r->sp ? m->up : m->down), acc);
    if (blocks < 1) blocks = 1;
    int32_t d = tgt - r->sp;
    if (d < 0) d = -d;
    r->tgt = tgt; r->acc = acc;
    r->step = (d + blocks / 2) / blocks;         /* only when the target or acceleration changes, not per sample */
    if (r->step < 1) r->step = 1;
}
/* one block: the speed moves one step toward the target (never past it); the phase advances by the block's mean
 * speed. Returns the phase at the block's start; r->ph is the phase at its end. */
RC_INLINE uint32_t rc_rotor_block(rc_rotor *r)
{
    const int32_t s0 = r->sp, t = r->tgt;
    int32_t s1 = s0;
    if (s0 < t) { s1 = s0 + r->step; if (s1 > t) s1 = t; }
    else if (s0 > t) { s1 = s0 - r->step; if (s1 < t) s1 = t; }
    r->sp = s1;
    const uint32_t p0 = r->ph;
    r->ph = p0 + ((uint32_t)(s0 + s1) << 5);    /* 64 samples * (s0 + s1) / 2 */
    return p0;
}

/* ---- controls (once per block, before render), all 0..32767: blue, green, white, orange, then page 2 of blue
 * (speed), green (acceleration) and white (contour) ---- */
static inline void rc_control(rc_state *s, const int32_t q[7])
{
    s->mode = rc_mode(q[0]);
    s->tblend = q[1];
    s->tdrive = q[2];
    s->tmix = (q[3] * 16385) >> 15;              /* 0 .. 16384 exactly */
    s->tspeed = q[4];
    s->accel = rc_tab(RC_ACCEL, q[5]);
    s->tcontour = q[6];
}

/* ---- drive ---- */
typedef struct { int32_t g, m, b; } rc_drv;      /* gain Q11 (1 .. 16), make-up Q15, bias */
RC_INLINE rc_drv rc_drive_params(int32_t d)
{
    rc_drv p;
    const int32_t i = d >> 10, f = d & 1023;
    p.g = 2048 + ((30720 * ((d * d) >> 15)) >> 15);
    p.m = RC_MAKEUP[i] + (((RC_MAKEUP[i + 1] - RC_MAKEUP[i]) * f) >> 10);
    p.b = (d * RC_BIAS_MAX) >> 15;
    return p;
}
/* soft knee in units of 32768 = full scale: linear to 0.5, a parabola to 1.0 at 1.5, flat beyond */
RC_INLINE int32_t rc_knee(int32_t v)
{
    const int32_t a = v < 0 ? -v : v;
    int32_t y;
    if (a <= 16384) y = a;
    else if (a >= 49152) y = 32768;
    else { const int32_t t = a - 16384; y = a - ((t * t) >> 16); }
    return v < 0 ? -y : y;
}
/* x (int16 range) -> driven sample; g, m, b as rc_drv. Input range +-32768, intermediate |x*g| <= 2^30. */
RC_INLINE int32_t rc_drive(int32_t x, int32_t g, int32_t m, int32_t b)
{
    int32_t u = ((x * g) >> 11) + b;
    if (u > 49152) u = 49152; else if (u < -49152) u = -49152;
    return ((rc_knee(u) - b) * m) >> 15;
}

/* ---- crossover: one TPT SVF step on a Q12 sample, low and high outputs (Q12) ---- */
RC_INLINE void rc_svf(int32_t *ic, int32_t x, int32_t *lp, int32_t *hp)
{
    const int32_t v3 = x - ic[1];
    const int32_t v1 = rc_mul(ic[0], RC_SV_A1) + rc_mul(v3, RC_SV_A2);
    const int32_t v2 = ic[1] + rc_mul(ic[0], RC_SV_A2) + rc_mul(v3, RC_SV_A3);
    ic[0] = 2 * v1 - ic[0];
    ic[1] = 2 * v2 - ic[1];
    *lp = v2;
    *hp = x - rc_mul(v1, RC_SV_K) - v2;
}

/* ---- the block ---- */
typedef struct { int32_t d, sd, g, sg, k, sk; } rc_tap;   /* delay Q16, gain Q15, brightness Q15 and their slopes */

RC_INLINE void rc_horn_tap(rc_tap *t, const int16_t *tab, uint32_t a0, uint32_t a1, const rc_mic *m, int32_t w0, int32_t w1)
{
    const int32_t c0 = rc_cos(tab, a0 + m->angle), c1 = rc_cos(tab, a1 + m->angle);
    const int32_t d0 = m->mean - rc_mul(m->exc, c0), d1 = m->mean - rc_mul(m->exc, c1);
    const int32_t g0 = ((m->gbase + ((m->gdep * c0) >> 15)) * w0) >> 15, g1 = ((m->gbase + ((m->gdep * c1) >> 15)) * w1) >> 15;
    const int32_t k0 = m->kbase + ((m->kdep * c0) >> 15), k1 = m->kbase + ((m->kdep * c1) >> 15);
    t->d = d0; t->sd = rc_slope(d0, d1);
    t->g = g0; t->sg = rc_slope(g0, g1);
    t->k = k0; t->sk = rc_slope(k0, k1);
}
RC_INLINE void rc_bass_tap(rc_tap *t, const int16_t *tab, uint32_t a0, uint32_t a1, const rc_bassmic *m, int32_t b0, int32_t b1)
{
    const int32_t c0 = rc_cos(tab, a0 + m->angle), c1 = rc_cos(tab, a1 + m->angle);
    const int32_t mean0 = m->nmean + rc_mul(m->fmean - m->nmean, b0), mean1 = m->nmean + rc_mul(m->fmean - m->nmean, b1);
    const int32_t exc0 = m->nexc + rc_mul(m->fexc - m->nexc, b0), exc1 = m->nexc + rc_mul(m->fexc - m->nexc, b1);
    const int32_t h0 = (m->ndep + (((m->fdep - m->ndep) * b0) >> 15)) >> 1, h1 = (m->ndep + (((m->fdep - m->ndep) * b1) >> 15)) >> 1;
    const int32_t d0 = mean0 - rc_mul(exc0, c0), d1 = mean1 - rc_mul(exc1, c1);
    const int32_t g0 = 32767 - h0 + ((h0 * c0) >> 15), g1 = 32767 - h1 + ((h1 * c1) >> 15);
    t->d = d0; t->sd = rc_slope(d0, d1);
    t->g = g0; t->sg = rc_slope(g0, g1);
    t->k = t->sk = 0;
}
/* fractional read d (Q16 samples) behind write index w */
RC_INLINE int32_t rc_read(const int16_t *ring, uint32_t w, int32_t d)
{
    const int32_t i = d >> 16, f = (d >> 1) & 0x7FFF;
    RC_ASSERT(i >= 2 && i <= RC_RING - 3);
    const uint32_t p = w - (uint32_t)i;
    const int32_t a = ring[p & RC_MASK], b = ring[(p - 1) & RC_MASK];
    return a + (((b - a) * f) >> 15);
}

/* Stereo (sides 2): l and r in place, the wet path from (l + r) / 2, two microphone perspectives per side.
 * Mono (sides 1, r unused): one side's perspective (the left microphones), in place in l. */
RC_INLINE void rc_render(rc_state *s, int16_t *l, int16_t *r, const int16_t *tab, const int sides)
{
    if (!s->primed) {
        s->blend = s->tblend; s->drive = s->tdrive; s->mix = s->tmix; s->contour = s->tcontour; s->speed = s->tspeed;
    }
    s->speed = rc_smooth(s->speed, s->tspeed);
    const int32_t sp = rc_tab(RC_SPEED, s->speed);
    rc_rotor_mode(&s->rh, &RC_HORN, s->mode, sp, s->accel, !s->primed);
    rc_rotor_mode(&s->rb, &RC_BASS, s->mode, sp, s->accel, !s->primed);
    s->primed = 1;
    const int32_t b0 = s->blend, dr0 = s->drive, m0 = s->mix;
    const int32_t b1 = s->blend = rc_smooth(b0, s->tblend);
    const int32_t dr1 = s->drive = rc_smooth(dr0, s->tdrive);
    const int32_t m1 = s->mix = rc_smooth(m0, s->tmix);
    const int32_t kc0 = rc_contour(s->contour), kc1 = rc_contour(s->contour = rc_smooth(s->contour, s->tcontour));
    const uint32_t h0 = rc_rotor_block(&s->rh), h1 = s->rh.ph;
    const uint32_t q0 = rc_rotor_block(&s->rb), q1 = s->rb.ph;

    rc_tap ht[4], bt[2];
    for (int k = 0; k < sides; k++) {
        rc_horn_tap(&ht[2 * k], tab, h0, h1, &RC_MICS[2 * k], 32767 - b0, 32767 - b1);
        rc_horn_tap(&ht[2 * k + 1], tab, h0, h1, &RC_MICS[2 * k + 1], b0, b1);
        rc_bass_tap(&bt[k], tab, q0, q1, &RC_BASSMICS[k], b0, b1);
    }
    const rc_drv p0 = rc_drive_params(dr0), p1 = rc_drive_params(dr1);
    int32_t g = p0.g, m = p0.m, bias = p0.b, mix = m0, kc = kc0;
    const int32_t sg = rc_slope(p0.g, p1.g), sm = rc_slope(p0.m, p1.m), sb = rc_slope(p0.b, p1.b), smix = rc_slope(m0, m1);
    const int32_t skc = rc_slope(kc0, kc1);
    uint32_t w = s->w;
    int32_t cont = s->cont, dcx = s->dcx, dcy = s->dcy;

    for (int n = 0; n < RC_N; n++) {
        const int32_t xl = l[n], xr = sides == 2 ? r[n] : xl;
        const int32_t x = sides == 2 ? (xl + xr) >> 1 : xl;
        cont += rc_mul((x * 4096) - cont, kc);
        const int32_t xd = rc_drive((cont + 2048) >> 12, g, m, bias);
        dcy = rc_mul(dcy, RC_R_DC) + ((xd - dcx) * 4096);
        dcx = xd;
        const int32_t xc = (dcy + 2048) >> 12;
        int32_t l1, h1, lo, hi, unused;
        rc_svf(s->sv[0], xc * 4096, &l1, &h1);
        rc_svf(s->sv[1], l1, &lo, &unused);
        rc_svf(s->sv[2], h1, &unused, &hi);
        s->horn[w] = rc_sat16((hi + 2048) >> 12);
        s->bass[w] = rc_sat16((lo + 2048) >> 12);
        for (int k = 0; k < sides; k++) {
            int32_t acc = 0;
            for (int j = 2 * k; j < 2 * k + 2; j++) {
                rc_tap *t = &ht[j];
                const int32_t y = rc_read(s->horn, w, t->d);
                s->br[j] += rc_mul((y * 4096) - s->br[j], t->k);
                acc += (((s->br[j] + 2048) >> 12) * t->g) >> 15;
                t->d += t->sd; t->g += t->sg; t->k += t->sk;
            }
            rc_tap *t = &bt[k];
            acc += (rc_read(s->bass, w, t->d) * t->g) >> 15;
            t->d += t->sd; t->g += t->sg;
            const int32_t dry = k ? xr : xl, wet = rc_sat16(acc + (acc >> 2));   /* +1.9 dB: the rotors average about -2.5 dB */
            const int16_t out = rc_sat16(dry + (((wet - dry) * mix) >> 14));
            if (k) r[n] = out; else l[n] = out;
        }
        w = (w + 1) & RC_MASK;
        g += sg; m += sm; bias += sb; mix += smix; kc += skc;
    }
    s->w = w; s->cont = cont; s->dcx = dcx; s->dcy = dcy;
}
static inline void rc_render_mono(rc_state *s, int16_t *buf, const int16_t *tab) { rc_render(s, buf, buf, tab, 1); }
static inline void rc_render_stereo(rc_state *s, int16_t *l, int16_t *r, const int16_t *tab) { rc_render(s, l, r, tab, 2); }

#endif
