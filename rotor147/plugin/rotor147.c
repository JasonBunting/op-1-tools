/* rotor147 (working name): a rotary speaker effect for the OP-1 running Hundekraft, inspired by the Motion Sound R3-147.
 *   blue   : motion - brake, slow, fast (three zones);         push: speed (both rotors x0.25 .. x4)
 *   green  : mics - close and pronounced to distant, blended;  push: acceleration (ramp times x5 .. x0.1)
 *   white  : drive - clean to growling, level compensated;     push: contour (high-cut before the drive)
 *   orange : mix - dry to fully rotary
 * A push toggles the knob's second page (words 5..7, saved with the sound); Shift + turn stays the core's fine turn.
 * Synth and drum positions run the mono callback (one side's microphones); the master runs render_stereo (the cabinet
 * fed from (L + R) / 2, stereo microphones). The DSP is rc_dsp.h, shared with the host test.
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#include "op1_plugin.h"
#include "rc_dsp.h"

static rc_state st[3];                           /* one per FX position: 0 synth, 1 drum, 2 master */

static void rc_init_cb(int pos) { rc_init(&st[pos % 3]); }

static int32_t rc_word(const int16_t *p, int i) { return p[i] < 0 ? 0 : p[i]; }   /* page-2 words, 0..32767 */

static void rc_knobs(rc_state *s, const int16_t *p)
{
    const int32_t q[7] = { op1_fx_param_q15(p, 0), op1_fx_param_q15(p, 1), op1_fx_param_q15(p, 2), op1_fx_param_q15(p, 3),
                           rc_word(p, 5), rc_word(p, 6), rc_word(p, 7) };
    rc_control(s, q);
}

static void rc_render_cb(int pos, int16_t *buf, const int16_t *p)
{
    rc_state *s = &st[pos % 3];
    rc_knobs(s, p);
    rc_render_mono(s, buf, OP1_SINE_TABLE);
}

static void rc_render_stereo_cb(int pos, int16_t *l, int16_t *r, const int16_t *p)
{
    rc_state *s = &st[pos % 3];
    rc_knobs(s, p);
    rc_render_stereo(s, l, r, OP1_SINE_TABLE);
}

const op1_fx_stereo_t FX_rotor147 = {
    { rc_init_cb, rc_render_cb, { OP1_PRESS_PAGE, OP1_PRESS_PAGE, OP1_PRESS_PAGE, OP1_PRESS_NONE } },
    rc_render_stereo_cb,
};

/* ======================= screen (GUI thread) =======================
 * rotor147.svg is empty; everything is drawn here, as in Ratatoskr:
 *   the left column: a row per knob, its name and value beside a strip in its colour; a knob on page 2 shows its
 *   page-2 name and value and a second strip;
 *   the two rotors, read live from the shown position's state: the horn (a bar with a flared mouth) and the bass drum
 *   (a circle with its scoop), each turned to its angle, with its speed in Hz below. */
#include "op1_synth.h"
#include "seq/seq_text.h"   /* sq_linemin: whole pixels on the running panel's grid */
#define Q(v) ((v) << 10)
#define TS 12                           /* values */
#define TS_LAB 8                        /* labels */
#define COL_X 17
static const int16_t row_base[4] = { 21, 54, 87, 120 };   /* each row's label baseline; its value 5 px below */
static const uint8_t c_blue[4] = { 0x4D, 0x9E, 0xFF, 0xFF }, c_green[4] = { 0x00, 0xED, 0x95, 0xFF },
                     c_white[4] = { 0xFF, 0xFF, 0xFF, 0xFF }, c_orange[4] = { 0xFF, 0x8C, 0x3A, 0xFF },
                     c_grey[4] = { 0x8A, 0x8C, 0x8E, 0xFF }, c_dim[4] = { 0x4A, 0x4C, 0x4E, 0xFF };
static const uint8_t *const col4[4] = { c_blue, c_green, c_white, c_orange };
enum { TX_LAB, TX_VAL = 4, TX_HORN = 8, TX_BASS, TX_HHZ, TX_BHZ, TX_N };
static uint32_t tx[TX_N][OP1_TEXT_SIZE / 4];
static char shown[TX_N][24];
static uint8_t tx_made;
#define HORN_X 170
#define BASS_X 272
#define ROT_Y  60
#define HORN_R 32
#define BASS_R 26
/* contour: the high-cut's frequency in 10 Hz at each RC_CONTOUR point (1 kHz .. 16 kHz in equal octave steps) */
static const uint16_t RC_CONTOUR_HZ10[32] = {
    100, 109, 120, 131, 143, 156, 171, 187, 205, 224, 245, 267, 292, 320, 350, 383,
    418, 457, 500, 547, 598, 654, 715, 782, 856, 936, 1023, 1119, 1223, 1338, 1463, 1600 };
static const char *const motion_name[3] = { "brake", "slow", "fast" };

static int32_t on_x(int32_t x) { return x < -Q(4) ? -Q(4) : (x > Q(324) ? Q(324) : x); }   /* never far off the panel */
static int32_t on_y(int32_t y) { return y < -Q(4) ? -Q(4) : (y > Q(164) ? Q(164) : y); }
static void ln(void *r, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w, const uint8_t *c)
{
    x0 = on_x(x0); x1 = on_x(x1); y0 = on_y(y0); y1 = on_y(y1);
    if (x0 == x1 && y0 == y1) y1 += 256;
    sq_linemin(r, x0, y0, x1, y1, w, c, 1);
}
static void curve(void *r, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w, const uint8_t *c)   /* smooth */
{
    op1_pt a = { on_x(x0), on_y(y0) }, b = { on_x(x1), on_y(y1) };
    op1_raster_line(r, &a, &b, w, c);
}
static void txt(void *r, int i, const char *s)
{
    static const int32_t ident[9] = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 };
    int same = 1;
    for (int j = 0; j < 24; j++) { if (shown[i][j] != s[j]) { same = 0; break; } if (!s[j]) break; }
    if (!same) { op1_text_set(tx[i], s); int j = 0; for (; j < 23 && s[j]; j++) shown[i][j] = s[j]; shown[i][j] = 0; }
    op1_text_draw(tx[i], r, ident);
}
static char *put(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *num(char *p, int32_t x) { char t[10]; int n = 0; do t[n++] = (char)('0' + x % 10); while ((x /= 10)); while (n) *p++ = t[--n]; *p = 0; return p; }
static char *dec(char *p, int32_t x, int d)                /* x in 1/10^d: 125, 2 -> "1.25" */
{
    const int32_t s = d == 1 ? 10 : 100, f = x % s;
    p = num(p, x / s); *p++ = '.';
    if (d == 2 && f < 10) *p++ = '0';
    return num(p, f);
}
static char *pct(char *p, int32_t q15) { return put(num(p, (q15 * 100 + 16383) >> 15), "%"); }
static int32_t hz100(int32_t sp) { return (int32_t)(((uint32_t)sp * 441u + 214748u) / 429497u); }   /* RC_HZ -> 1/100 Hz */
/* a point at radius rq (Q10) from (cx, cy) along angle ph: 0 points down, at the listener; mirrored so that the left
 * microphones (negative angles) are on the left */
static void at(uint32_t ph, int32_t cx, int32_t cy, int32_t rq, int32_t *x, int32_t *y)
{
    const int32_t c = rc_cos(OP1_SINE_TABLE, ph), s = rc_cos(OP1_SINE_TABLE, ph - 0x40000000u);
    *x = Q(cx) - (rq >> 5) * s / 1024;                 /* rq < 2^16, s Q15: no overflow */
    *y = Q(cy) + (rq >> 5) * c / 1024;
}

static void rc_screen_init(void *scr) { (void)scr; }

static void rc_screen_update(void *scr, int dt)
{
    const int pos = op1_fx_screen_pos() % 3;
    int16_t kl[4];
    op1_fx_knobs_live(kl);
    const int16_t *w = op1_fx_knobs(pos);
    if (!tx_made) {
        for (int i = 0; i < 4; i++) {                   /* y is the text's top: 6 px above an 8's baseline */
            op1_text_ctor(tx[TX_LAB + i], Q(COL_X), row_base[i] - 6, TS_LAB, col4[i]);
            op1_text_ctor(tx[TX_VAL + i], Q(COL_X), row_base[i] + 5, TS, col4[i]);
        }
        op1_text_ctor_a(tx[TX_HORN], Q(HORN_X), ROT_Y + HORN_R + 10, TS_LAB, c_grey, 1);
        op1_text_ctor_a(tx[TX_BASS], Q(BASS_X), ROT_Y + HORN_R + 10, TS_LAB, c_grey, 1);
        op1_text_ctor_a(tx[TX_HHZ], Q(HORN_X), ROT_Y + HORN_R + 21, TS, c_white, 1);
        op1_text_ctor_a(tx[TX_BHZ], Q(BASS_X), ROT_Y + HORN_R + 21, TS, c_white, 1);
        tx_made = 1;
    }
    op1_screen_draw(scr, dt);
    void *r = op1_raster();

    /* the rotors: one 32-bit read each of what the audio thread last wrote */
    const rc_state *s = &st[pos];
    const uint32_t hph = s->rh.ph, bph = s->rb.ph;
    const int32_t hsp = s->rh.sp, bsp = s->rb.sp;
    int32_t x0, y0, x1, y1, x2, y2;
    for (uint32_t i = 0; i < 24; i++) {                 /* the bass drum's rim */
        at(i * 178956971u, BASS_X, ROT_Y, Q(BASS_R), &x0, &y0);
        at((i + 1) * 178956971u, BASS_X, ROT_Y, Q(BASS_R), &x1, &y1);
        curve(r, x0, y0, x1, y1, 0x400, c_dim);
    }
    at(bph, BASS_X, ROT_Y, Q(BASS_R - 2), &x0, &y0);    /* its scoop: a spoke with an arrow head at the rim */
    at(bph - 0x0C000000u, BASS_X, ROT_Y, Q(BASS_R - 9), &x1, &y1);
    at(bph + 0x0C000000u, BASS_X, ROT_Y, Q(BASS_R - 9), &x2, &y2);
    curve(r, Q(BASS_X), Q(ROT_Y), x0, y0, 0x800, c_grey);
    curve(r, x1, y1, x0, y0, 0x800, c_grey);
    curve(r, x2, y2, x0, y0, 0x800, c_grey);
    at(hph, HORN_X, ROT_Y, Q(HORN_R), &x0, &y0);        /* the horn: the mouth end, the dummy end opposite, shorter */
    at(hph + 0x80000000u, HORN_X, ROT_Y, Q(HORN_R - 12), &x1, &y1);
    curve(r, x1, y1, x0, y0, 0x800, c_white);
    at(hph - 0x0A000000u, HORN_X, ROT_Y, Q(HORN_R + 1), &x1, &y1);   /* the flare across the mouth */
    at(hph + 0x0A000000u, HORN_X, ROT_Y, Q(HORN_R + 1), &x2, &y2);
    curve(r, x1, y1, x2, y2, 0x800, c_white);
    /* the microphones (green, the mics knob's colour): the horn's close pair just outside its sweep, the distant pair
     * further out, the bass's one each side; the synth and drum positions use the left ones only, the master all. A
     * microphone sits where the rotor faces it; its mark grows with what it hears: its share of the mics blend (close
     * or distant; the bass always) times how directly the rotor faces it, so a horn stopped facing away shows short
     * marks, and the sound is quieter */
    const int sides = pos == OP1_FX_MASTER ? 2 : 1;
    const int32_t blend = s->blend;                     /* 0 close .. 32767 distant */
    for (int k = 0; k < sides; k++) {
        for (int f = 0; f < 2; f++) {
            const rc_mic *m = &RC_MICS[2 * k + f];
            const int32_t face = (32767 + rc_cos(OP1_SINE_TABLE, hph + m->angle)) >> 1;   /* 0 away .. 32767 facing */
            const int32_t share = f ? blend : 32767 - blend;
            const int32_t r0 = HORN_R + (f ? 10 : 4), len = Q(2) + 9 * (((share >> 8) * (face >> 8)) >> 4);   /* 2 .. 11 px */
            at(0u - m->angle, HORN_X, ROT_Y, Q(r0), &x0, &y0);
            at(0u - m->angle, HORN_X, ROT_Y, Q(r0) + len, &x1, &y1);
            curve(r, x0, y0, x1, y1, 0x800, c_green);
        }
        const rc_bassmic *b = &RC_BASSMICS[k];
        const int32_t face = (32767 + rc_cos(OP1_SINE_TABLE, bph + b->angle)) >> 1;
        const int32_t len = Q(2) + 9 * (face >> 5);
        at(0u - b->angle, BASS_X, ROT_Y, Q(BASS_R + 4), &x0, &y0);
        at(0u - b->angle, BASS_X, ROT_Y, Q(BASS_R + 4) + len, &x1, &y1);
        curve(r, x0, y0, x1, y1, 0x800, c_green);
    }
    char v[24];
    txt(r, TX_HORN, "horn");
    txt(r, TX_BASS, "bass");
    put(dec(v, hz100(hsp), 2), " Hz"); txt(r, TX_HHZ, v);
    put(dec(v, hz100(bsp), 2), " Hz"); txt(r, TX_BHZ, v);

    /* the left column: 2 x 22 px strips; a second one marks a knob on page 2 */
    for (int i = 0; i < 4; i++) {
        const int32_t ya = Q(row_base[i] - 8), yb = Q(row_base[i] + 14);
        ln(r, Q(11), ya, Q(11), yb, 0x800, col4[i]);
        if (i < 3 && op1_fx_page[pos][i]) ln(r, Q(5), ya, Q(5), yb, 0x800, col4[i]);
    }
    const int32_t p5 = rc_word(w, 5), p6 = rc_word(w, 6), p7 = rc_word(w, 7);
    if (op1_fx_page[pos][0]) { txt(r, TX_LAB, "speed"); dec(put(v, "x"), (rc_tab(RC_SPEED, p5) * 100 + 2048) >> 12, 2); }
    else { txt(r, TX_LAB, "motion"); put(v, motion_name[rc_mode(kl[0])]); }
    txt(r, TX_VAL, v);
    if (op1_fx_page[pos][1]) { txt(r, TX_LAB + 1, "spin-up"); put(dec(v, (rc_tab(RC_ACCEL, p6) * 100 + 2048) >> 12, 2), " s"); }
    else { txt(r, TX_LAB + 1, "mics"); pct(v, kl[1]); }
    txt(r, TX_VAL + 1, v);
    if (op1_fx_page[pos][2]) {
        txt(r, TX_LAB + 2, "contour");
        if (p7 >= RC_OPEN) put(v, "open");
        else {
            const int32_t i = p7 >> 10, j = i < 31 ? i + 1 : 31;
            const int32_t h = RC_CONTOUR_HZ10[i] + (((RC_CONTOUR_HZ10[j] - RC_CONTOUR_HZ10[i]) * (p7 & 1023)) >> 10);
            put(dec(v, (h + 5) / 10, 1), " kHz");
        }
    } else { txt(r, TX_LAB + 2, "drive"); pct(v, kl[2]); }
    txt(r, TX_VAL + 2, v);
    txt(r, TX_LAB + 3, "mix");
    pct(v, kl[3]);
    txt(r, TX_VAL + 3, v);
}

const op1_screen_t SCREEN_rotor147 = { rc_screen_init, rc_screen_update };
