/* Ratatoskr: a grain cloud for the OP-1 under Hundekraft, read around a playhead that scurries through the sampler
 * slot's recording (named for the squirrel that runs up and down the world tree). The DSP is rt_dsp.h.
 * Record a sound with the sampler engine on a synth slot, then pick ratatoskr on the same slot.
 *   blue   : position, the middle of the region the playhead roams        press: SPRAY, grains scattered around it
 *   green  : grain size, 10 ms .. 500 ms                                    press: WIDTH, the region, 0 .. the whole
 *   white  : speed: frozen in the middle, forwards right, backwards left,   press: WANDER: drift, jumps and turns
 *            up to 4x (1x three quarters of the way out)
 *   red    : density, 0.5x .. 4.4x overlap                                  press: the sampler's sample browser
 * Keys play the cloud at their pitch (C4 is the recording's own). Page 2 uses word 7, so there is no hidden trim.
 * Hundekraft 3.3. Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see
 * LICENSE and NOTICE). */
#include "op1_plugin.h"
#include "rt_dsp.h"

static rt_voice voices[OP1_VOICES];
static rt_roam roam;
static int32_t rec_len;                  /* the slot's recording, worked out at each note */
static int last_idx = OP1_VOICES;        /* the voice rendered last: a lower or equal one starts a new block */
static int32_t read_now;                 /* the playhead's read point this block */

static void rt_knobs_of(const int16_t *p, rt_knobs *k)
{
    k->position = p[0]; k->size = p[1]; k->speed = p[2]; k->density = p[3];
    k->spray = p[OP1_ENG_PAGE_WORD(0)]; k->width = p[OP1_ENG_PAGE_WORD(1)]; k->wander = p[OP1_ENG_PAGE_WORD(2)];
}

/* the recording's length: the end of the last peak bucket above a small threshold (as Hundekraft's granular), four
 * buckets at a time while they are all zero */
static int32_t rt_recorded(const uint8_t *sb)
{
    const int16_t *pk = op1_sample_peaks(sb);
    int i = OP1_PEAK_BUCKETS - 1;
    if (!((uintptr_t)pk & 3)) {
        while (i >= 0 && (i & 3) != 3) { if (pk[i] > 96) goto found; i--; }
        const uint32_t *w = (const uint32_t *)(const void *)pk;
        while (i >= 3 && !(w[(i - 3) >> 1] | w[(i - 1) >> 1])) i -= 4;
    }
    for (; i >= 0; i--) if (pk[i] > 96) goto found;
    return 0;
found:
    {
        int32_t l = (i + 1) * 256;
        return l > OP1_SAMPLE_MAX ? OP1_SAMPLE_MAX : l;
    }
}

static void rt_note_on(void *self, const int16_t *params)
{
    (void)params;
    int i = op1_voice_index(self);
    if (i < 0 || i >= OP1_VOICES) return;
    const uint8_t *sb = op1_sample_buffer(op1_current_slot());
    rec_len = sb ? rt_recorded(sb) : 0;
    rt_voice_note(&voices[i], i);
}

static void rt_render_cb(void *self, int16_t *out, const int16_t *params)
{
    int i = op1_voice_index(self);
    if (i < 0 || i >= OP1_VOICES) { for (int j = 0; j < OP1_BLOCK; j++) out[j] = 0; return; }
    const uint8_t *sb = op1_sample_buffer(op1_current_slot());
    const int16_t *data = sb ? op1_sample_data(sb) : 0;
    rt_knobs k;
    rt_knobs_of(params, &k);
    if (i <= last_idx && data && rec_len >= RT_MIN_LEN) read_now = rt_roam_step(&roam, rec_len, &k);
    last_idx = i;
    int32_t gain = (op1_voice_vel_gain(self) * 9741) >> 13;     /* +1.5 dB, as granular's level pass */
    rt_voice_render(&voices[i], data, rec_len, read_now, &k, rt_rate((uint32_t)OP1_PITCH_INC[0]), gain, out,
                    OP1_SINE_TABLE);
}

const op1_engine_t ENGINE_ratatoskr = { rt_note_on, rt_render_cb, 0,
                                        { OP1_PRESS_PAGE, OP1_PRESS_PAGE, OP1_PRESS_PAGE, OP1_PRESS_BROWSE } };

/* ======================= screen (GUI thread) =======================
 * ratatoskr.svg is empty; everything is drawn here:
 *   the recording as a row of columns (its peaks), the region as a green bracket under it, the playhead as a blue
 *   arrow, the read point (the playhead with wander's drift) as a white line, and every sounding grain as a red tick
 *   below; "record a sample first" when the slot has none;
 *   the bottom row: each knob's name and value; a knob on page 2 shows its page-2 name and a doubled line. */
#include "op1_synth.h"
#define Q(v) ((v) << 10)
#define TS 12
static const uint8_t c_blue[4] = { 0x4D, 0x9E, 0xFF, 0xFF }, c_green[4] = { 0x00, 0xED, 0x95, 0xFF },
                     c_white[4] = { 0xFF, 0xFF, 0xFF, 0xFF }, c_red[4] = { 0xFF, 0x3A, 0x5D, 0xFF },
                     c_grey[4] = { 0x6F, 0x71, 0x73, 0xFF };
static const uint8_t *const col4[4] = { c_blue, c_green, c_white, c_red };
enum { TX_LAB, TX_VAL = 4, TX_MSG = 8, TX_N };
static uint32_t tx[TX_N][OP1_TEXT_SIZE / 4];
static char shown[TX_N][24];
static uint8_t tx_made;
static uint32_t seen_blk[OP1_VOICES];

static void ln(void *r, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w, const uint8_t *c)
{
    if (x0 == x1 && y0 == y1) y1 += 256;
    op1_pt a = { x0, y0 }, b = { x1, y1 };
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
    int32_t s = d == 1 ? 10 : 100;
    p = num(p, x / s); *p++ = '.';
    int32_t f = x % s;
    if (d == 2 && f < 10) *p++ = '0';
    return num(p, f);
}
static void pct(char *p, int32_t v) { put(num(p, (rt_clamp(v, 0, 32767) * 100 + 16383) >> 15), "%"); }
static void secs(char *p, int32_t samples44)               /* "120ms", "1.25s" */
{
    int32_t ms = (samples44 * 10 + 220) / 441;
    if (ms < 1000) put(num(p, ms), "ms"); else put(dec(p, ms / 10, 2), "s");
}

#define BAR_X0 8
#define BAR_X1 312
#define BAR_N  76                            /* a column every 4 px */
#define BAR_Y  40
#define BAR_H  20
#define BRK_Y  66
#define GRN_Y  76
static int32_t x_of(int32_t s, int32_t len)  /* source sample -> Q10 x */
{
    if (len <= 0) return Q(BAR_X0);
    int32_t f = (rt_clamp(s, 0, len) << 11) / len;          /* 0 .. 2048; len < 2^19, so the shift fits */
    return Q(BAR_X0) + f * ((BAR_X1 - BAR_X0) << 9);
}

static void rt_screen_init(void *scr) { (void)scr; }

static void rt_screen_update(void *scr, int dt)
{
    const int slot = OP1_SCREEN_SLOT & 7;
    int16_t kl[4];
    op1_synth_knobs_live(slot, kl);
    const int16_t *w = op1_synth_knobs(slot);
    int16_t p[8];
    for (int i = 0; i < 8; i++) p[i] = i < 4 ? kl[i] : w[i];
    rt_knobs k;
    rt_knobs_of(p, &k);
    if (!tx_made) {
        for (int i = 0; i < 4; i++) {
            op1_text_ctor(tx[TX_LAB + i], Q(8 + 78 * i), 114, TS, col4[i]);
            op1_text_ctor(tx[TX_VAL + i], Q(8 + 78 * i), 127, TS, c_white);
        }
        op1_text_ctor(tx[TX_MSG], Q(90), 44, TS, c_grey);
        tx_made = 1;
    }
    op1_screen_draw(scr, dt);
    void *r = op1_raster();

    const uint8_t *sb = op1_sample_buffer(slot);
    const int32_t len = sb ? rt_recorded(sb) : 0;
    if (len >= RT_MIN_LEN) {
        /* the recording */
        const int16_t *pk = op1_sample_peaks(sb);
        const int nb = (len + 255) >> 8;
        int16_t col[BAR_N];
        int top = 1;
        for (int c = 0; c < BAR_N; c++) {
            int m = 0;
            for (int j = c * nb / BAR_N, e = (c + 1) * nb / BAR_N; j <= e && j < nb; j++) if (pk[j] > m) m = pk[j];
            col[c] = (int16_t)m; if (m > top) top = m;
        }
        int32_t lo, hi;
        rt_region(len, k.position, k.width, &lo, &hi);
        for (int c = 0; c < BAR_N; c++) {
            int32_t x = Q(BAR_X0) + Q(BAR_X1 - BAR_X0) * c / (BAR_N - 1);
            int32_t s = len * c / (BAR_N - 1), h = col[c] * Q(BAR_H) / top;
            ln(r, x, Q(BAR_Y) - h, x, Q(BAR_Y) + h, 0x400, s >= lo && s <= hi ? c_white : c_grey);
        }
        /* the region */
        int32_t xl = x_of(lo, len), xh = x_of(hi, len);
        ln(r, xl, Q(BRK_Y), xh, Q(BRK_Y), 0x600, c_green);
        ln(r, xl, Q(BRK_Y - 4), xl, Q(BRK_Y), 0x600, c_green);
        ln(r, xh, Q(BRK_Y - 4), xh, Q(BRK_Y), 0x600, c_green);
        /* the playhead and the read point */
        int32_t xp = x_of(roam.pos >> 8, len), xr = x_of(roam.read, len);
        if (roam.pos >> 8 < lo || roam.pos >> 8 > hi) xp = x_of(lo, len);   /* not stepped since the region moved */
        ln(r, xp - Q(4), Q(BRK_Y + 6), xp, Q(BRK_Y), 0x500, c_blue);
        ln(r, xp + Q(4), Q(BRK_Y + 6), xp, Q(BRK_Y), 0x500, c_blue);
        ln(r, xr, Q(BAR_Y - BAR_H - 4), xr, Q(BAR_Y + BAR_H + 4), 0x300, c_white);
        /* the grains of the voices sounding now */
        for (int i = 0; i < OP1_VOICES; i++) {
            uint32_t b = voices[i].blocks;
            int live = b != seen_blk[i];
            seen_blk[i] = b;
            if (!live) continue;
            for (int g = 0; g < RT_GRAINS; g++) {
                if (!voices[i].g[g].winc) continue;
                int32_t xg = x_of((int32_t)(voices[i].g[g].pos >> RT_FRAC), len);
                ln(r, xg, Q(GRN_Y + i * 3), xg, Q(GRN_Y + i * 3 + 2), 0x500, c_red);
            }
        }
    } else txt(r, TX_MSG, "record a sample first");

    /* the bottom row; a doubled line marks a knob on page 2 */
    for (int i = 0; i < 4; i++) {
        ln(r, Q(8 + 78 * i), Q(110), Q(78 + 78 * i), Q(110), 0x800, col4[i]);
        if (i < 3 && op1_eng_page[i]) ln(r, Q(8 + 78 * i), Q(106), Q(78 + 78 * i), Q(106), 0x800, col4[i]);
    }
    char v[24];
    if (op1_eng_page[0]) { txt(r, TX_LAB, "spray"); secs(v, rt_spray(k.spray)); }
    else { txt(r, TX_LAB, "position"); secs(v, rt_at(k.position, len)); }
    txt(r, TX_VAL, v);
    if (op1_eng_page[1]) { txt(r, TX_LAB + 1, "width"); pct(v, k.width); }
    else { txt(r, TX_LAB + 1, "size"); secs(v, rt_size(k.size) >> 1); }
    txt(r, TX_VAL + 1, v);
    if (op1_eng_page[2]) { txt(r, TX_LAB + 2, "wander"); pct(v, k.wander); }
    else {
        int32_t s = rt_speed_q16(k.speed);
        txt(r, TX_LAB + 2, "speed");
        if (!s) put(v, "frozen");
        else { char *q = v; *q++ = s < 0 ? '-' : '+'; put(dec(q, ((s < 0 ? -s : s) * 100 + 32768) >> 16, 2), "x"); }
    }
    txt(r, TX_VAL + 2, v);
    txt(r, TX_LAB + 3, "density");
    put(dec(v, rt_ovl8(k.density) * 10 / 8, 1), "x");
    txt(r, TX_VAL + 3, v);
}

const op1_screen_t SCREEN_ratatoskr = { rt_screen_init, rt_screen_update };
