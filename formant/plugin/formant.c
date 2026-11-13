/* formant: a vowel synth engine for the OP-1 under Hundekraft. A vocal-fold pulse through five formant resonators
 * whose frequencies come from CHIMERA's vocal-tract model (fm_tables.h); the DSP is fm_dsp.h.
 *   blue   : vowel, "ee" (left) .. "ah" .. "oo" (right)       press: TALK rate, 10 steps: off, 1.5 .. 10 a second
 *   green  : body, bigger than a man .. woman .. smaller than a child   press: TALK spread, 0 .. 100% in 11 steps
 *   white  : breath (fully right whispers)                    press: TALK chop, 0 .. 100% in 11 steps
 *   orange : vibrato depth, then speed
 * Page 2 uses word 7, so there is no hidden preset trim.
 * Hundekraft 3.2.3. Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see
 * LICENSE and NOTICE). */
#include "op1_plugin.h"
#include "fm_dsp.h"

#define FM_GAIN 1638                     /* one voice at middle C: rms 1700 .. 5200 of 32767 by vowel (host test) */

static fm_voice voices[OP1_VOICES];

/* page 2 in steps, one per encoder detent (OP1_PRESS_PAGE_STEPS: step i's word is (2i+1) * 32768 / (2n), and an unset
 * word, 0, is step 0) */
#define FM_RATE_STEPS  10                /* off, then 9 rates */
#define FM_PCT_STEPS   11                /* 0, 10 .. 100% */
static int fm_step(int32_t word, int n) { int s = (int)((word < 0 ? 0 : word) * n >> 15); return s >= n ? n - 1 : s; }
/* The talk knobs from page 2. Spread and chop never go fully to zero while talk is on, so talk is always audible
 * (a slot set up before page 2 existed has every page-2 word at 0). */
static void fm_page2(const int16_t *w, fm_talk_knobs *tk, int *rate_step, int *spread_pct, int *chop_pct)
{
    int r = fm_step(w[OP1_ENG_PAGE_WORD(0)], FM_RATE_STEPS);
    int sp = fm_step(w[OP1_ENG_PAGE_WORD(1)], FM_PCT_STEPS) * 10, ch = fm_step(w[OP1_ENG_PAGE_WORD(2)], FM_PCT_STEPS) * 10;
    tk->rate = r ? FM_TALK_OFF + (r - 1) * (32767 - FM_TALK_OFF) / (FM_RATE_STEPS - 2) : 0;
    tk->spread = 4915 + sp * 27852 / 100;                              /* 15% .. 100% of the strip */
    tk->cons = 6554 + ch * 26213 / 100;                                /* 20% .. 100% */
    if (rate_step) *rate_step = r;
    if (spread_pct) *spread_pct = sp;
    if (chop_pct) *chop_pct = ch;
}

static void fm_note_on(void *self, const int16_t *params)
{
    (void)params;
    int i = op1_voice_index(self);
    if (i >= 0 && i < OP1_VOICES) fm_voice_reset(&voices[i]);
}

static void fm_render_cb(void *self, int16_t *out, const int16_t *params)
{
    int i = op1_voice_index(self);
    if (i < 0 || i >= OP1_VOICES) { for (int j = 0; j < OP1_BLOCK; j++) out[j] = 0; return; }
    fm_voice *v = &voices[i];
    const int32_t *inc = OP1_PITCH_INC;
    fm_talk_knobs tk;
    fm_page2(params, &tk, 0, 0, 0);
    int32_t vowel, closure;
    int32_t level = fm_talk(v, params[0], &tk, (uint32_t)inc[0], &vowel, &closure);
    v->show_vowel = vowel; v->show_closure = closure;
    fm_knobs k;
    fm_knobs_make(&k, vowel, params[1], params[2], params[3]);
    fm_close(&k, closure);
    int32_t gain = (FM_GAIN * op1_voice_vel_gain(self)) >> 15;
    gain = (gain * level) >> 15;
    fm_render(v, &k, inc, OP1_BLOCK, gain, out, OP1_SINE_TABLE);
}

const op1_engine_t ENGINE_formant = { fm_note_on, fm_render_cb, 0,
                                      { OP1_PRESS_PAGE_STEPS(FM_RATE_STEPS), OP1_PRESS_PAGE_STEPS(FM_PCT_STEPS),
                                        OP1_PRESS_PAGE_STEPS(FM_PCT_STEPS), OP1_PRESS_NONE } };

/* ======================= screen (GUI thread) =======================
 * The core only gives an engine its page 2 while the engine's own screen is shown, hence this screen (layout after
 * Hundekraft's dddd). formant.svg is empty; everything is drawn here:
 *   a mouth: wide for "ee", round for "oo", open for "ah", sized by the body (giant biggest), closing on each talk
 *            syllable (the first sounding voice, or the knobs when nothing sounds);
 *   a vowel strip, "ee" .. "oo": blue's place (a blue mark) and every sounding voice's vowel (a dot);
 *   the bottom row: each knob's name and value; a knob on page 2 shows its page-2 name and a doubled line. */
#include "op1_synth.h"
#define Q(v) ((v) << 10)
#define TS 12
static const uint8_t c_blue[4] = { 0x4D, 0x9E, 0xFF, 0xFF }, c_green[4] = { 0x00, 0xED, 0x95, 0xFF },
                     c_white[4] = { 0xFF, 0xFF, 0xFF, 0xFF }, c_red[4] = { 0xFF, 0x3A, 0x5D, 0xFF },
                     c_grey[4] = { 0x6F, 0x71, 0x73, 0xFF };
static const uint8_t *const col4[4] = { c_blue, c_green, c_white, c_red };
enum { TX_LAB, TX_VAL = 4, TX_EE = 8, TX_AH, TX_OO, TX_N };
static uint32_t tx[TX_N][OP1_TEXT_SIZE / 4];
static char shown[TX_N][14];
static uint8_t tx_made;
static int32_t seen_blk[OP1_VOICES];

static void ln(void *r, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w, const uint8_t *c)
{
    if (x0 == x1 && y0 == y1) return;
    op1_pt a = { x0, y0 }, b = { x1, y1 };
    op1_raster_line(r, &a, &b, w, c);
}
static void txt(void *r, int i, const char *s)
{
    static const int32_t ident[9] = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 };
    int same = 1;
    for (int j = 0; j < 14; j++) { if (shown[i][j] != s[j]) { same = 0; break; } if (!s[j]) break; }
    if (!same) { op1_text_set(tx[i], s); int j = 0; for (; j < 13 && s[j]; j++) shown[i][j] = s[j]; shown[i][j] = 0; }
    op1_text_draw(tx[i], r, ident);
}
static char *put(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *num(char *p, int x) { char t[6]; int n = 0; do t[n++] = (char)('0' + x % 10); while ((x /= 10)); while (n) *p++ = t[--n]; *p = 0; return p; }
static void pct(char *p, int32_t v) { put(num(p, (v * 100 + 16383) >> 15), "%"); }
/* an ellipse of n segments around (cx, cy) Q10, radii rx, ry Q10 (under 64 px, so the products fit 32 bits) */
static void ellipse(void *r, int32_t cx, int32_t cy, int32_t rx, int32_t ry, int n, int32_t w, const uint8_t *c)
{
    int32_t px = cx + rx, py = cy;
    for (int i = 1; i <= n; i++) {
        int32_t a = 8192 * i / n;
        int32_t x = cx + ((rx * OP1_SINE_TABLE[(a + 2048) & 8191]) >> 15);
        int32_t y = cy + ((ry * OP1_SINE_TABLE[a & 8191]) >> 15);
        ln(r, px, py, x, y, w, c);
        px = x; py = y;
    }
}
static int32_t clampi(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : (x > hi ? hi : x); }

#define STRIP_X0 40
#define STRIP_X1 280
#define STRIP_Y  88
static int32_t strip_x(int32_t vowel15) { return Q(STRIP_X0) + ((vowel15 * (STRIP_X1 - STRIP_X0)) >> 5); }

static void fm_screen_init(void *scr) { (void)scr; }

static void fm_screen_update(void *scr, int dt)
{
    int slot = OP1_SCREEN_SLOT & 7;
    int16_t k[4];
    op1_synth_knobs_live(slot, k);
    const int16_t *w = op1_synth_knobs(slot);
    if (!tx_made) {
        for (int i = 0; i < 4; i++) {
            op1_text_ctor(tx[TX_LAB + i], Q(8 + 78 * i), 114, TS, col4[i]);
            op1_text_ctor(tx[TX_VAL + i], Q(8 + 78 * i), 127, TS, c_white);
        }
        op1_text_ctor(tx[TX_EE], Q(STRIP_X0 - 8), STRIP_Y + 4, TS, c_grey);
        op1_text_ctor(tx[TX_AH], Q(STRIP_X0 + (STRIP_X1 - STRIP_X0) * 5 / 8 - 8), STRIP_Y + 4, TS, c_grey);
        op1_text_ctor(tx[TX_OO], Q(STRIP_X1 - 8), STRIP_Y + 4, TS, c_grey);
        tx_made = 1;
    }
    op1_screen_draw(scr, dt);
    void *r = op1_raster();

    /* which voices are sounding: their block count moved since the last frame */
    int lead = -1;
    uint8_t live[OP1_VOICES];
    for (int i = 0; i < OP1_VOICES; i++) {
        int32_t b = voices[i].blk;
        live[i] = b != seen_blk[i];
        seen_blk[i] = b;
        if (live[i] && lead < 0) lead = i;
    }

    /* the mouth: its shape comes from the vowel alone (a man's formants: F2 sets the width, F1 the opening), its size
     * from the body (a giant's is the biggest, a tiny voice's the smallest); a talk syllable closes it */
    int32_t vowel = k[0], closure = 0;
    if (lead >= 0 && voices[lead].hz[0]) { vowel = clampi(voices[lead].show_vowel, 0, 32767); closure = voices[lead].show_closure; }
    fm_knobs kk;
    fm_knobs_make(&kk, vowel, 5461, 0, 0);                 /* green 5461: the man, exactly */
    int32_t f1 = kk.hz[0] >> 8, f2 = kk.hz[1] >> 8;
    int32_t bp = k[1] * FM_BODIES - 16384;                 /* -0.5 .. 2.5 bodies, Q15, as fm_knobs_make */
    int32_t size = 1280 - (bp + 16384) * 717 / 98304;      /* Q10: 1.25 (giant) .. 0.55 (tiny) */
    int32_t rx = ((Q(14) + clampi((f2 - 800) * 36 / 1500, 0, 36) * 1024) * size) >> 10;   /* under 63 px */
    int32_t ry = ((Q(3) + clampi((f1 - 280) * 22 / 480, 0, 22) * 1024) * size) >> 10;
    ry -= ((ry - Q(1)) * closure) >> 15;                  /* Q10 px * Q15: below 2^31 */
    ellipse(r, Q(160), Q(44), rx, ry, 24, 0x800, c_white);
    ln(r, Q(160) - rx + Q(4), Q(44), Q(160) + rx - Q(4), Q(44), 0x400, c_grey);   /* where the lips meet */

    /* the vowel strip */
    ln(r, Q(STRIP_X0), Q(STRIP_Y), Q(STRIP_X1), Q(STRIP_Y), 0x400, c_grey);
    for (int i = 0; i < FM_VOWELS; i++) {
        int32_t x = Q(STRIP_X0) + Q(STRIP_X1 - STRIP_X0) * i / (FM_VOWELS - 1);
        ln(r, x, Q(STRIP_Y - 2), x, Q(STRIP_Y + 2), 0x400, c_grey);
    }
    int32_t bx = strip_x(k[0]);
    ln(r, bx, Q(STRIP_Y - 9), bx, Q(STRIP_Y - 1), 0xC00, c_blue);
    for (int i = 0; i < OP1_VOICES; i++)
        if (live[i]) ellipse(r, strip_x(clampi(voices[i].show_vowel, 0, 32767)), Q(STRIP_Y - 5), Q(2), Q(2), 6, 0x800, c_white);
    txt(r, TX_EE, "ee"); txt(r, TX_AH, "ah"); txt(r, TX_OO, "oo");

    /* the bottom row; a doubled line marks a knob on page 2 */
    for (int i = 0; i < 4; i++) {
        ln(r, Q(8 + 78 * i), Q(110), Q(78 + 78 * i), Q(110), 0x800, col4[i]);
        if (i < 3 && op1_eng_page[i]) ln(r, Q(8 + 78 * i), Q(106), Q(78 + 78 * i), Q(106), 0x800, col4[i]);
    }
    char v[16];
    static const char *const vname[FM_VOWELS] = { "ee", "ih", "ay", "eh", "a", "ah", "aw", "oh", "oo" };
    fm_talk_knobs tk;
    int rstep, sp, ch;
    fm_page2(w, &tk, &rstep, &sp, &ch);
    if (op1_eng_page[0]) {
        txt(r, TX_LAB, "talk");
        if (!rstep) put(v, "off");
        else { int32_t base = 1378 / (3 + ((tk.rate * 17) >> 15)); put(num(v, (689 * 10 / base + 5) / 10), "/s"); }
    } else { txt(r, TX_LAB, "vowel"); put(v, vname[clampi((k[0] * (FM_VOWELS - 1) + 16384) >> 15, 0, FM_VOWELS - 1)]); }
    txt(r, TX_VAL, v);
    if (op1_eng_page[1]) { txt(r, TX_LAB + 1, "spread"); put(num(v, sp), "%"); }
    else {
        int32_t bp = k[1] * FM_BODIES - 16384;                         /* as fm_knobs_make: -0.5 .. 2.5 bodies */
        txt(r, TX_LAB + 1, "body");
        put(v, bp < -8192 ? "giant" : bp < 16384 ? "man" : bp < 49152 ? "woman" : bp < 73728 ? "child" : "tiny");
    }
    txt(r, TX_VAL + 1, v);
    if (op1_eng_page[2]) { txt(r, TX_LAB + 2, "chop"); put(num(v, ch), "%"); }
    else { txt(r, TX_LAB + 2, "breath"); pct(v, k[2]); }
    txt(r, TX_VAL + 2, v);
    if (k[3] > 16384) { txt(r, TX_LAB + 3, "warble"); pct(v, (k[3] - 16384) * 2); }
    else { txt(r, TX_LAB + 3, "vibrato"); pct(v, k[3] * 2); }
    txt(r, TX_VAL + 3, v);
}

const op1_screen_t SCREEN_formant = { fm_screen_init, fm_screen_update };
