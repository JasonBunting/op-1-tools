/* pushprobe 0.3: a throwaway synth engine that watches the core's page-2 flags (op1_eng_page) against the encoder
 * pushes. Every knob's press is OP1_PRESS_PAGE, as in Ratatoskr, and the input hook only notes each push down and passes
 * it on, so the core flips the page as usual. The screen notes each time a flag turns on ("+") or off ("-"). Each knob
 * gets two rows: its page now, the downs and the flips counted; then its last few events, each with the milliseconds
 * since that knob's event before it. A healthy press: "d", then "+" (or "-") a frame later; a release shows nothing.
 * A flag that turns back with no "d" before it is being changed by something other than the push.
 * (0.1, 0.2 took the pushes to time downs and ups; they found the switch chatter that research/pushguard fixes.)
 * Flips are seen once a screen frame, so two flips inside one frame are missed (the downs still show).
 * Time: CYCLES at 400 MHz (32 bits: a gap over 10.7 s shows as ">9s"). Turn any knob to clear.
 * Hundekraft 3.3.1 (engine_input: kennel cores from 2026-11-11). Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later
 * with the OP-1 firmware permission. */
#include "op1_plugin.h"
#include "op1_synth.h"

#define EVS 5
static volatile uint16_t downs[4], flips[4];
static volatile uint32_t last_t[4];              /* CYCLES at each knob's last event */
static volatile uint8_t any[4];
static volatile char ev_c[4][EVS];               /* the last events, oldest first: 'd', '+', '-', or 0 */
static volatile uint32_t ev_ms10[4][EVS];        /* tenths of a ms since that knob's event before; ~0: the first */
static uint8_t was[4], was_known;                /* the flags last frame (GUI thread) */

static inline uint32_t cycles(void) { uint32_t c; __asm__ volatile("%0 = CYCLES;" : "=d"(c)); return c; }

static void pp_note_on(void *self, const int16_t *params) { (void)self; (void)params; }
static void pp_render(void *self, int16_t *out, const int16_t *params)
{
    (void)self; (void)params;
    for (int i = 0; i < OP1_BLOCK; i++) out[i] = 0;
}

static void note(int a, char c)                  /* KeyHandler (downs) and GUI (flips): a stray race only blurs a row */
{
    const uint32_t t = cycles();
    const uint32_t d = any[a] ? (t - last_t[a]) / 40000u : 0xFFFFFFFFu;    /* 40000 cycles = 0.1 ms */
    last_t[a] = t; any[a] = 1;
    for (int i = 0; i < EVS - 1; i++) { ev_c[a][i] = ev_c[a][i + 1]; ev_ms10[a][i] = ev_ms10[a][i + 1]; }
    ev_c[a][EVS - 1] = c;
    ev_ms10[a][EVS - 1] = d;
}

static void clear(void)
{
    for (int k = 0; k < 4; k++) {
        downs[k] = flips[k] = 0; any[k] = 0;
        for (int i = 0; i < EVS; i++) ev_c[k][i] = 0;
    }
}

/* KeyHandler thread: every encoder turn and push on this engine's T1 page; nothing is taken */
static int pp_input(int page, int ev, int a, int b)
{
    if (page != OP1_ENG_T1 || a < 0 || a > 3) return 0;
    if (ev == OP1_ENG_TURN) { clear(); return 0; }
    if (ev == OP1_ENG_PUSH && b) { downs[a]++; note(a, 'd'); }
    return 0;
}

const op1_engine_ext2_t ENGINE_pushprobe = {
    { { pp_note_on, pp_render, 0, { OP1_PRESS_PAGE, OP1_PRESS_PAGE, OP1_PRESS_PAGE, OP1_PRESS_PAGE } }, 0, 0, 0 },
    pp_input,
    0,
    0,
};

/* ---- screen (GUI thread) ---- */
#define Q(v) ((v) << 10)
static const uint8_t c_blue[4] = { 0x4D, 0x9E, 0xFF, 0xFF }, c_green[4] = { 0x00, 0xED, 0x95, 0xFF },
                     c_white[4] = { 0xFF, 0xFF, 0xFF, 0xFF }, c_red[4] = { 0xFF, 0x3A, 0x5D, 0xFF },
                     c_grey[4] = { 0x8A, 0x8C, 0x8E, 0xFF };
static const uint8_t *const col4[4] = { c_blue, c_green, c_white, c_red };
static uint32_t tx[9][OP1_TEXT_SIZE / 4];
static char shown[9][48];
static uint8_t made;

static void txt(void *r, int i, const char *s)
{
    static const int32_t ident[9] = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 };
    int same = 1;
    for (int j = 0; j < 48; j++) { if (shown[i][j] != s[j]) { same = 0; break; } if (!s[j]) break; }
    if (!same) { op1_text_set(tx[i], s); int j = 0; for (; j < 47 && s[j]; j++) shown[i][j] = s[j]; shown[i][j] = 0; }
    op1_text_draw(tx[i], r, ident);
}
static char *put(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *num(char *p, uint32_t x) { char t[10]; int n = 0; do t[n++] = (char)('0' + x % 10); while ((x /= 10)); while (n) *p++ = t[--n]; *p = 0; return p; }

static void pp_screen_init(void *scr) { (void)scr; }

static void pp_screen_update(void *scr, int dt)
{
    static const char *const name[4] = { "blue", "green", "white", "orange" };
    for (int k = 0; k < 4; k++) {                /* the page flags, once a frame */
        const uint8_t p = op1_eng_page[k] != 0;
        if (was_known && p != was[k]) { flips[k]++; note(k, p ? '+' : '-'); }
        was[k] = p;
    }
    was_known = 1;
    if (!made) {
        op1_text_ctor(tx[8], Q(8), 4, 12, c_grey);
        for (int k = 0; k < 4; k++) {
            op1_text_ctor(tx[k], Q(8), 20 + 34 * k, 12, col4[k]);
            op1_text_ctor(tx[4 + k], Q(8), 33 + 34 * k, 12, c_white);
        }
        made = 1;
    }
    op1_screen_draw(scr, dt);
    void *r = op1_raster();
    txt(r, 8, "d push  + page 2 on  - off; turn to clear");
    for (int k = 0; k < 4; k++) {
        char s[48], *q = s;
        q = put(q, name[k]);
        q = put(num(put(q, "  page "), was[k] ? 2 : 1), "  downs ");
        q = put(num(q, downs[k]), "  flips ");
        num(q, flips[k]);
        txt(r, k, s);
        q = s; *q = 0;
        for (int i = 0; i < EVS; i++) {
            const char c = ev_c[k][i];
            const uint32_t m = ev_ms10[k][i];
            if (!c) continue;
            *q++ = c; *q++ = ' ';
            if (m == 0xFFFFFFFFu) q = put(q, "-");
            else if (m > 99999) q = put(q, ">9s");
            else { q = num(q, m / 10); *q++ = '.'; q = num(q, m % 10); }
            q = put(q, "  ");
        }
        txt(r, 4 + k, s);
    }
}

const op1_screen_t SCREEN_pushprobe = { pp_screen_init, pp_screen_update };
