/* Bifrost knob map: a MIDI controller's knobs as the OP-1's four encoders. Knobs 1-4 (learned first) turn the encoders
 * on page 1, knobs 5-8 on page 2. The parts with no hardware in them, for the host test: learning which CCs are the
 * knobs, telling absolute knobs from relative ones, and a knob's message as a number of detents.
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#ifndef KNOBMAP_H
#define KNOBMAP_H
#include <stdint.h>

#define KM_KNOBS   8
#define KM_SAMPLES 8

/* how a knob sends: a position, or a change in one of the two common relative codes */
enum { KM_ABS = 0,      /* 0..127, a position */
       KM_REL2C = 1,    /* 1..63 up, 65..127 down (127 = one down): two's complement */
       KM_REL64 = 2 };  /* 64 + n up, 64 - n down: offset binary */

typedef struct {
    uint8_t n;                      /* knobs known, 0..8 */
    uint16_t id[KM_KNOBS];          /* channel << 8 | CC */
    uint8_t mode[KM_KNOBS];
    uint8_t last[KM_KNOBS];         /* KM_ABS: the last position */
    uint8_t have[KM_KNOBS];         /* KM_ABS: last is known */
} km_map;

typedef struct {
    uint8_t n;
    uint16_t id[KM_KNOBS];
    uint8_t ns[KM_KNOBS];
    uint8_t s[KM_KNOBS][KM_SAMPLES];
} km_learn;

/* the knob a message belongs to, or -1 */
static inline int km_find(const km_map *m, int ch, int cc)
{
    const uint16_t id = (uint16_t)((ch & 15) << 8 | (cc & 127));
    for (int i = 0; i < m->n; i++) if (m->id[i] == id) return i;
    return -1;
}

/* An absolute knob turned one way steps steadily through positions (60, 61, 62); a relative one stays in a small band
 * without a trend (65, 65, 66, 65 near 64: offset binary; 1, 1, 2 or 127, 126, 127 near the ends: two's complement). */
static inline int km_detect(const uint8_t *s, int n)
{
    if (n < 3) return KM_ABS;
    int near64 = 0, ends = 0, up = 1, down = 1;
    for (int i = 0; i < n; i++) {
        if (s[i] != 64 && s[i] >= 57 && s[i] <= 71) near64++;
        if ((s[i] >= 1 && s[i] <= 7) || (s[i] >= 121 && s[i] <= 127)) ends++;
        if (i && s[i] <= s[i - 1]) up = 0;
        if (i && s[i] >= s[i - 1]) down = 0;
    }
    if (up || down) return KM_ABS;                              /* a steady trend: positions */
    if (near64 == n) return KM_REL64;
    if (ends == n) return KM_REL2C;
    return KM_ABS;
}

/* learning: each new CC is the next knob; its first values decide its kind. -> the knob's index when it is new, -1 */
static inline int km_learn_cc(km_learn *l, int ch, int cc, int v)
{
    const uint16_t id = (uint16_t)((ch & 15) << 8 | (cc & 127));
    for (int i = 0; i < l->n; i++)
        if (l->id[i] == id) { if (l->ns[i] < KM_SAMPLES) l->s[i][l->ns[i]++] = (uint8_t)v; return -1; }
    if (l->n >= KM_KNOBS) return -1;
    const int i = l->n++;
    l->id[i] = id; l->ns[i] = 1; l->s[i][0] = (uint8_t)v;
    return i;
}

/* all eight learned and the eighth turned a little */
static inline int km_learn_full(const km_learn *l) { return l->n == KM_KNOBS && l->ns[KM_KNOBS - 1] >= 4; }

static inline void km_learn_finish(const km_learn *l, km_map *m)
{
    m->n = l->n;
    for (int i = 0; i < l->n; i++) {
        m->id[i] = l->id[i];
        m->mode[i] = (uint8_t)km_detect(l->s[i], l->ns[i]);
        m->last[i] = l->s[i][l->ns[i] - 1];
        m->have[i] = 1;
    }
}

/* a saved knob: mode << 16 | channel << 8 | CC (km_pack / km_unpack, for the settings store) */
static inline int32_t km_pack(const km_map *m, int i) { return (int32_t)m->mode[i] << 16 | m->id[i]; }
static inline void km_unpack(km_map *m, int i, int32_t v)
{
    m->id[i] = (uint16_t)(v & 0x0FFF); m->mode[i] = (uint8_t)((v >> 16) & 3); m->have[i] = 0;
}

/* a knob's value as detents: + up, - down; 0 for none (an absolute knob's first message only sets where it is) */
static inline int km_delta(km_map *m, int i, int v)
{
    v &= 127;
    switch (m->mode[i]) {
    case KM_REL2C: return v < 64 ? v : v - 128;
    case KM_REL64: return v - 64;
    default:
        if (!m->have[i]) { m->have[i] = 1; m->last[i] = (uint8_t)v; return 0; }
        const int d = v - m->last[i];
        m->last[i] = (uint8_t)v;
        return d;
    }
}
#endif
