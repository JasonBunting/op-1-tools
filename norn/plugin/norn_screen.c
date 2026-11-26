/* Norn's screen, "Three Keepers" (norn/design/Claude-UI-Handoff.md): three blue figures (norn.svg, the cached static
 * part) hold the green, white and orange threads; four readouts across the top in encoder order. Drawn here every
 * frame, on the GUI thread:
 *   the top strip: MODEL with four segments, and green / white / orange with their label, value and a track (ends,
 *   centre, the saved setting solid, the live one hollow while T4 moves it);
 *   the weave (norn_weave.h): each thread from its hand, shaped by the live knobs, its crossings marked;
 *   a ring and letter at the hand of a knob just turned, fading over 1.3 s;
 *   while notes play, a highlight running along each thread, fading 0.65 s after the last release.
 * Blue picks the labels and the weave's pattern from its saved setting; when T4 moves blue into another model, that
 * model's segment is underlined (the sound follows the live value).
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#include "op1_synth.h"
#include "seq/seq_text.h"       /* sq_linemin: whole pixels on the panel's grid (SDK docs/screen-art.md "Pixel grid") */
#include "norn_weave.h"
#include "norn_activity.h"

#define Q(v) ((int32_t)(v) << 10)
#define TS_LAB 8
#define TS_VAL 12
#define TRACK_Y 31                      /* below the values: a 12's glyphs reach about 13 px below its top */
#define LAB_TOP 2
#define VAL_TOP 13

static const uint8_t c_blue[4] = { 0x70, 0x9C, 0xFF, 0xFF }, c_green[4] = { 0x62, 0xE6, 0xA4, 0xFF },
                     c_white[4] = { 0xEE, 0xE9, 0xDC, 0xFF }, c_orange[4] = { 0xFF, 0x98, 0x59, 0xFF },
                     c_track[4] = { 0x30, 0x34, 0x3B, 0xFF }, c_tick[4] = { 0x72, 0x77, 0x82, 0xFF },
                     c_seg[4] = { 0x27, 0x30, 0x3B, 0xFF }, c_black[4] = { 0, 0, 0, 0xFF };
static const uint8_t *const col4[4] = { c_blue, c_green, c_white, c_orange };
/* lowercase: the stock font draws lowercase letters as capitals; its uppercase codes are icons (SDK
 * docs/feature-hooks.md) */
static const char *const model_name[4] = { "shaper", "2-op fm", "formant", "terrain" };
static const char *const knob_name[4][3] = { { "curve", "fold", "slope" }, { "ratio", "depth", "feedback" },
                                             { "interval", "formant", "width" }, { "terrain", "radius", "offset" } };

enum { TX_LAB, TX_VAL = 4, TX_KEEP = 8, TX_N = 11 };
static uint32_t tx[TX_N][OP1_TEXT_SIZE / 4];
static char shown[TX_N][16];
static uint8_t tx_made;

static uint32_t now_ms, last_cyc, cyc_rem;              /* the screen's clock, from the CPU's cycle counter */
static int16_t was[3] = { -1, -1, -1 };                 /* saved green / white / orange last frame */
static int8_t accent = -1;                              /* the keeper whose knob was just turned */
static uint32_t accent_at, note_at, release_at;
static uint8_t was_seq, was_active, have_release;

/* the blue knob's model: norn.c's pl_model */
static int nr_model(int32_t blue) { return blue <= 0 ? 0 : blue >= 32767 ? 3 : (blue * 4) >> 15; }

/* every line stays on the screen: a point far off it makes AGG allocate until the OP-1 crashes (Ratatoskr 0.1.0) */
static int32_t on_x(int32_t x) { return x < -Q(4) ? -Q(4) : x > Q(324) ? Q(324) : x; }
static int32_t on_y(int32_t y) { return y < -Q(4) ? -Q(4) : y > Q(164) ? Q(164) : y; }
static void ln(void *r, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w, const uint8_t *c)
{
    x0 = on_x(x0); x1 = on_x(x1); y0 = on_y(y0); y1 = on_y(y1);
    if (x0 == x1 && y0 == y1) y1 += 256;
    sq_linemin(r, x0, y0, x1, y1, w, c, 1);             /* straight UI lines: whole pixels, sharp on every panel */
}
static void curve(void *r, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w, const uint8_t *c)
{
    op1_pt a = { on_x(x0), on_y(y0) }, b = { on_x(x1), on_y(y1) };
    op1_raster_line(r, &a, &b, w, c);                   /* curves: smooth, not snapped */
}
/* a diamond of half-diagonal rq (Q10) at x, y: a 45-degree line as wide as it is long */
static void diamond(void *r, int32_t x, int32_t y, int32_t rq, const uint8_t *c)
{
    const int32_t h = rq >> 1;
    curve(r, x - h, y - h, x + h, y + h, (rq * 1810) >> 10, c);   /* width rq sqrt 2, in display px (x 1.25) */
}
static void fade(uint8_t out[4], const uint8_t *c, int32_t k)  /* k Q15: towards black (the background) */
{
    for (int j = 0; j < 3; j++) out[j] = (uint8_t)((c[j] * k) >> 15);
    out[3] = 0xFF;
}
static void txt(void *r, int i, const char *s)
{
    static const int32_t ident[9] = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 };
    int same = 1;
    for (int j = 0; j < 16; j++) { if (shown[i][j] != s[j]) { same = 0; break; } if (!s[j]) break; }
    if (!same) { op1_text_set(tx[i], s); int j = 0; for (; j < 15 && s[j]; j++) shown[i][j] = s[j]; shown[i][j] = 0; }
    op1_text_draw(tx[i], r, ident);
}
static char *num(char *p, int32_t x) { char t[10]; int n = 0; do t[n++] = (char)('0' + x % 10); while ((x /= 10)); while (n) *p++ = t[--n]; *p = 0; return p; }
static int32_t pct(int32_t v) { return (v * 100 + 16383) / 32767; }

/* a knob's value: a percentage, or FM's feedback as -100 .. +100, OFF exactly where fm_dsp.h's feedback is zero
 * (frequency feedback 0.5 fb^2 >> 15 is 0 for |fb| < 256 left of centre, phase feedback 0.25 fb^2 for fb < 362) */
static void value(char *v, int model, int knob, int32_t x)
{
    if (model == 1 && knob == 3) {
        const int32_t fb = 2 * x - 32767;
        if (fb > -256 && fb < 362) { v[0] = 'o'; v[1] = 'f'; v[2] = 'f'; v[3] = 0; return; }
        int32_t p = pct(fb < 0 ? -fb : fb);
        if (p < 1) p = 1;
        *v = fb < 0 ? '-' : '+';
        num(v + 1, p);
        return;
    }
    char *e = num(v, pct(x));
    e[0] = '%'; e[1] = 0;
}

static void clock_tick(void)
{
    const uint32_t c = op1_cycles(), d = c - last_cyc;
    last_cyc = c;
    if (d > 80000000u) return;                          /* the first frame, or back after 0.2 s away: no jump */
    cyc_rem += d;
    now_ms += cyc_rem / 400000u;                        /* the Blackfin runs at 400 MHz (580,525 cycles a block) */
    cyc_rem %= 400000u;
}

static void nr_screen_init(void *scr) { (void)scr; }

static void nr_screen_update(void *scr, int dt)
{
    const int slot = OP1_SCREEN_SLOT & 7;
    int16_t live[4];
    op1_synth_knobs_live(slot, live);                   /* with T4's modulation */
    const int16_t *saved = op1_synth_knobs(slot);       /* as set: the solid markers */
    clock_tick();
    if (!tx_made) {
        for (int i = 0; i < 4; i++) {                   /* y is the text's top: 6 px above an 8's baseline, 9 a 12's */
            op1_text_ctor(tx[TX_LAB + i], Q(7 + 80 * i), LAB_TOP, TS_LAB, col4[i]);
            op1_text_ctor(tx[TX_VAL + i], Q(7 + 80 * i), VAL_TOP, TS_VAL, c_white);
        }
        for (int i = 0; i < 3; i++)
            op1_text_ctor(tx[TX_KEEP + i], nw_hand_x[i] - Q(11), (nw_hand_y[i] >> 10) - 4, TS_LAB, col4[i + 1]);
        tx_made = 1;
    }
    op1_screen_draw_cached(scr, dt, 0);                 /* the figures: the whole SVG is static */
    void *r = op1_raster();

    const int model = nr_model(saved[0]), live_model = nr_model(live[0]);

    /* a knob just turned (saved value moved; T4 never moves it): its keeper's accent. Several at once is a preset. */
    int moved = -1, n_moved = 0;
    for (int i = 0; i < 3; i++) {
        if (was[i] >= 0 && saved[i + 1] != was[i]) { moved = i; n_moved++; }
        was[i] = saved[i + 1];
    }
    if (n_moved == 1) { accent = (int8_t)moved; accent_at = now_ms; }

    /* notes: any voice held and still rendering (norn.c) */
    int active = 0;
    const uint32_t c = op1_cycles();
    for (int i = 0; i < OP1_VOICES; i++)
        if ((nr_held >> i & 1) && c - nr_seen[i] < 40000000u) active = 1;   /* rendered in the last 0.1 s */
    if (nr_seq != was_seq) { was_seq = nr_seq; note_at = now_ms; }
    if (was_active && !active) { release_at = now_ms; have_release = 1; }
    was_active = (uint8_t)active;

    /* the top strip */
    char v[16];
    txt(r, TX_LAB, "model");
    txt(r, TX_VAL, model_name[model]);
    for (int j = 0; j < 4; j++) {
        ln(r, Q(7 + 16 * j), Q(TRACK_Y), Q(7 + 16 * j + 12), Q(TRACK_Y), 0x800, j == model ? c_blue : c_seg);
        if (j == live_model && j != model) ln(r, Q(7 + 16 * j), Q(TRACK_Y + 3), Q(7 + 16 * j + 12), Q(TRACK_Y + 3), 0x400, c_blue);
    }
    for (int i = 1; i < 4; i++) {
        const int32_t x = 7 + 80 * i;
        txt(r, TX_LAB + i, knob_name[model][i - 1]);
        value(v, model, i, saved[i]);
        txt(r, TX_VAL + i, v);
        ln(r, Q(x), Q(TRACK_Y), Q(x + 60), Q(TRACK_Y), 0x400, c_track);
        for (int k = 0; k <= 60; k += 30) ln(r, Q(x + k), Q(TRACK_Y - 2), Q(x + k), Q(TRACK_Y + 2), 0x400, c_tick);
        const int32_t mx = Q(x) + ((saved[i] * 60) >> 5);                            /* 60 px x v / 32768, Q10 */
        ln(r, mx, Q(TRACK_Y - 2), mx, Q(TRACK_Y + 3), 0x800, col4[i]);
        if (__builtin_abs(live[i] - saved[i]) > 328) {                               /* T4 moves it: hollow, below */
            const int32_t lx = Q(x) + ((live[i] * 60) >> 5);
            ln(r, lx - Q(1), Q(TRACK_Y + 5), lx + Q(1), Q(TRACK_Y + 5), 0x400, col4[i]);
            ln(r, lx - Q(1), Q(TRACK_Y + 7), lx + Q(1), Q(TRACK_Y + 7), 0x400, col4[i]);
            ln(r, lx - Q(1), Q(TRACK_Y + 5), lx - Q(1), Q(TRACK_Y + 7), 0x400, col4[i]);
            ln(r, lx + Q(1), Q(TRACK_Y + 5), lx + Q(1), Q(TRACK_Y + 7), 0x400, col4[i]);
        }
    }

    /* the weave, from the live knobs */
    static int32_t ys[3][NW_GRID_N];
    nw_in in = { model, live[1], live[2], live[3], now_ms, active };
    nw_threads(OP1_SINE_TABLE, &in, ys);
    const int step = op1_gfx_heavy() ? 2 : 1;           /* fewer segments while the frame governor is busy */
    const int32_t wt = 1088 + (model == 2 ? (live[3] * 576) >> 15 : 0);   /* 0.85 px (formant: + 0.45 x morph) */
    for (int i = 0; i < 3; i++) {
        int32_t px = nw_hand_x[i], py = nw_hand_y[i];
        for (int k = 0; k < NW_GRID_N; k += step) {
            const int kk = k + step >= NW_GRID_N ? NW_GRID_N - 1 : k;
            const int32_t x = Q(NW_GRID_X0 + 2 * kk), y = ys[i][kk];
            curve(r, px, py, x, y, wt, col4[i + 1]);
            px = x; py = y;
            if (kk == NW_GRID_N - 1) break;
        }
        /* the fingertip over the thread's start: it leaves from under the hand */
        curve(r, nw_hand_x[i] - 1741, nw_hand_y[i] - 717, nw_hand_x[i] + 717, nw_hand_y[i] - 1434, 1088, c_blue);
    }
    int32_t cx[NW_CROSS_MAX], cy[NW_CROSS_MAX];
    const int nc = nw_crossings(ys, cx, cy);
    for (int j = 0; j < nc; j++) { diamond(r, cx[j], cy[j], Q(2), c_black); diamond(r, cx[j], cy[j], 1126, c_white); }

    /* a knob just turned: a ring at its keeper's hand and her letter, fading */
    if (accent >= 0) {
        const uint32_t age = now_ms - accent_at;
        if (age >= 1300) accent = -1;
        else {
            uint8_t cc[4];
            fade(cc, col4[accent + 1], 32767 - (int32_t)(age * 32767 / 1300));
            const int32_t hx = nw_hand_x[accent], hy = nw_hand_y[accent];
            int32_t px = hx + 3174, py = hy;                                         /* radius 3.1 px */
            for (int s = 1; s <= 12; s++) {
                const int32_t k = s * 683;                                           /* 1/12 turn, 8192 a turn */
                const int32_t x = hx + ((3174 * OP1_SINE_TABLE[(k + 2048) & 8191]) >> 15);
                const int32_t y = hy + ((3174 * OP1_SINE_TABLE[k & 8191]) >> 15);
                curve(r, px, py, x, y, 896, cc);
                px = x; py = y;
            }
            if (age < 1000) { static const char *const letter[3] = { "h", "t", "m" }; txt(r, TX_KEEP + accent, letter[accent]); }
        }
    }

    /* notes: a highlight running along each thread, 0.45 of its length a second, the threads a little apart */
    int32_t strength = 0;
    if (active) strength = 32767;
    else if (have_release) {
        const uint32_t age = now_ms - release_at;
        if (age < 650) strength = 32767 - (int32_t)(age * 32767 / 650); else have_release = 0;
    }
    if (strength) {
        const uint32_t t = now_ms - note_at;
        for (int i = 0; i < 3; i++) {
            const int32_t u = (int32_t)(((t % 10000u) * 14746u / 1000u + (uint32_t)i * 3932u) & 32767u);  /* .45/s + .12 i */
            uint8_t cc[4], cw[4];
            fade(cc, col4[i + 1], strength);
            fade(cw, c_white, strength);
            const int32_t x = nw_x_at(i, u), y = nw_y(OP1_SINE_TABLE, &in, i, u);
            diamond(r, x, y, Q(2), cc);
            diamond(r, x, y, 820, cw);
        }
    }
}

const op1_screen_t SCREEN_norn = { nr_screen_init, nr_screen_update };
