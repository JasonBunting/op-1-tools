/* Ratatoskr: a grain cloud for the OP-1 under Hundekraft, read around a playhead (the squirrel) that runs up and down
 * the sampler slot's recording, drawn as the trunk of Yggdrasil. The DSP is rt_dsp.h.
 * Record into the sound with the input key while ratatoskr is selected.
 *   blue   : POSITION, where the squirrel starts                     press: RANGE, 5 .. 100% of the recording around it
 *   green  : SIZE, 12 .. 250 ms                                      press: SHAPE, hann / triangle / pluck / swell
 *   white  : SCATTER, 0 .. 500 ms either side                        press: nothing (no sample browser: too easy to load by accident)
 *   orange : MOTION, hold / forward / reverse / bounce / wander / jump  press: SPEED, 0.05 .. 2x travel, or 0.1 .. 4
 *                                                                           targets or jumps a second
 * Shift + green (the engine's record, shown while Shift is held): OVERLAP, 2x for a new sound, 0.5 .. 4x.
 * The first key after all keys are up starts the squirrel at the position; more keys join it where it is; it stops
 * when the last key goes up. Keys play the grains at their pitch (C4 is the recording's own).
 * Hundekraft 3.3.1. Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see
 * LICENSE and NOTICE). */
#include "op1_plugin.h"
#include "rt_dsp.h"

static rt_voice voices[OP1_VOICES];
static rt_roam roam;
static uint8_t roam_ready;
static rt_shapes shapes;
static volatile uint8_t shapes_ready;
static rt_flashes flashes;
static int32_t rec_len;                  /* the slot's recording, worked out at each note */
static int last_idx = OP1_VOICES;        /* the voice rendered last: a lower or equal one starts a new block */
static int32_t read_now;                 /* the playhead's read point this block */
static volatile uint32_t gblk;           /* audio blocks so far */
static volatile uint8_t held;            /* a bit per voice whose key is down */
static volatile uint8_t restart;         /* the first key of a phrase: start at the position */
static uint32_t seen[OP1_VOICES];        /* the block each voice last rendered in */

/* Shift + green lives in the engine's record (saved with the sound) as a step: 0 (a new sound) is 2x. Index 4, clear of
 * the 0.2 records (jumps, turns, edges, chaos), which now mean nothing. */
#define REC_OVERLAP 4
static uint32_t rec_owner;
static uint32_t rt_owner(void) { if (!rec_owner) rec_owner = op1_engine_rec_owner("ratatoskr"); return rec_owner; }

static void rt_knobs_of(const int16_t *p, const int16_t *rec, rt_knobs *k)
{
    k->position = p[0]; k->size = p[1]; k->scatter = p[2]; k->motion = p[3];
    k->range = p[OP1_ENG_PAGE_WORD(0)]; k->shape = p[OP1_ENG_PAGE_WORD(1)]; k->speed = p[OP1_ENG_PAGE_WORD(3)];
    k->overlap = rec ? rt_clamp(rec[REC_OVERLAP], 0, RT_OVL_STEPS - 1) : 0;
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
    if (!shapes_ready) { rt_shapes_init(shapes, OP1_SINE_TABLE); shapes_ready = 1; }
    const uint8_t *sb = op1_sample_buffer(op1_current_slot());
    rec_len = sb ? rt_recorded(sb) : 0;
    if (!held) restart = 1;
    held |= (uint8_t)(1u << i);
    seen[i] = gblk;
    rt_voice_note(&voices[i], i);
}

static void rt_note_off(void *self)
{
    int i = op1_voice_index(self);
    if (i >= 0 && i < OP1_VOICES) held &= (uint8_t)~(1u << i);
}

static void rt_render_cb(void *self, int16_t *out, const int16_t *params)
{
    static rt_launch launch;
    int i = op1_voice_index(self);
    if (i < 0 || i >= OP1_VOICES) { for (int j = 0; j < OP1_BLOCK; j++) out[j] = 0; return; }
    const uint8_t *sb = op1_sample_buffer(op1_current_slot());
    const int16_t *data = sb ? op1_sample_data(sb) : 0;
    if (i <= last_idx) {                                         /* a new block: the playhead and the launch settings */
        gblk++;
        uint8_t h = held;                                        /* a voice taken without a key-up (a steal, a kill) */
        for (int v = 0; v < OP1_VOICES; v++) if ((h >> v & 1) && gblk - seen[v] > 8) h &= (uint8_t)~(1u << v);
        held = h;
        rt_knobs k;
        rt_knobs_of(params, op1_engine_record(op1_current_slot(), rt_owner()), &k);
        if (!shapes_ready) { rt_shapes_init(shapes, OP1_SINE_TABLE); shapes_ready = 1; }
        rt_launch_of(&k, shapes, &launch);
        if (!roam_ready) { rt_roam_init(&roam); roam_ready = 1; }
        if (data && rec_len >= RT_MIN_LEN && (h || restart)) {   /* it stops when the last key is up */
            read_now = rt_roam_step(&roam, rec_len, &k, restart);
            restart = 0;
        }
    }
    last_idx = i;
    seen[i] = gblk;
    int32_t gain = (op1_voice_vel_gain(self) * 9741) >> 13;     /* +1.5 dB, as granular's level pass */
    rt_voice_render(&voices[i], data, rec_len, read_now, &launch, rt_rate((uint32_t)OP1_PITCH_INC[0]), gain, out,
                    &flashes, gblk);
}

const op1_engine_t ENGINE_ratatoskr = { rt_note_on, rt_render_cb, rt_note_off,
                                        { OP1_PRESS_PAGE, OP1_PRESS_PAGE, OP1_PRESS_NONE, OP1_PRESS_PAGE } };

/* ======================= screen (GUI thread) =======================
 * The layout is Ratatoskr-Design-Spec.md's (with its 2026-11-21 decisions). ratatoskr.svg holds the eagle (crown) and
 * the dragon (roots), which never move and are drawn through the core's cached static prefix, and the squirrel, which
 * this places. Drawn here:
 *   the trunk: the recording from the roots (its start) up to the crown (its end), a row per slice, as wide as its
 *   loudest moment; the route (the range) bright, the rest dim;
 *   the squirrel at the playhead, pointing at the trunk, tilted up or down while it travels;
 *   a white flash left of the trunk at each grain just launched; "no sample" with nothing recorded;
 *   with green on page 2, the grain's shape as a curve on the right;
 *   the left column: a row per knob, its name and value beside a strip in its colour; a knob on page 2 shows its
 *   page-2 name and a second strip; while Shift is held, green shows the overlap. */
#include "op1_synth.h"
#include "seq/seq_text.h"   /* sq_linemin: whole pixels on the running panel's grid (SDK docs/screen-art.md "Pixel grid") */
#define Q(v) ((v) << 10)
#define TS 12                           /* values */
#define TS_LAB 8                        /* labels */
#define COL_X 17                        /* the left column's text */
static const int16_t row_base[4] = { 21, 54, 87, 120 };   /* each row's label baseline; its value 5 px below */
static const uint8_t c_blue[4] = { 0x4D, 0x9E, 0xFF, 0xFF }, c_green[4] = { 0x00, 0xED, 0x95, 0xFF },
                     c_white[4] = { 0xFF, 0xFF, 0xFF, 0xFF }, c_orange[4] = { 0xFF, 0x8C, 0x3A, 0xFF },
                     c_grey[4] = { 0x8A, 0x8C, 0x8E, 0xFF }, c_bark[4] = { 0x8A, 0xE0, 0xCC, 0xFF },
                     c_bark_d[4] = { 0x3A, 0x74, 0x68, 0xFF }, c_axis[4] = { 0x3E, 0x7A, 0x5A, 0xFF },
                     c_nose[4] = { 0xF2, 0xA0, 0x6A, 0xFF };
static const uint8_t *const col4[4] = { c_blue, c_green, c_white, c_orange };
enum { TX_LAB, TX_VAL = 4, TX_MSG = 8, TX_SHAPE, TX_TIME, TX_N };
static uint32_t tx[TX_N][OP1_TEXT_SIZE / 4];
static char shown[TX_N][48];
static uint8_t tx_made;
static void *n_squirrel;
static int16_t was_pos = -1;             /* the position knob last frame, and frames left showing it rather than */
static int16_t pos_frames;               /* the live playhead */
static int32_t was_read = -1;
static int8_t tilt;                      /* -1 down, 0 perched, 1 up */
static int8_t still_frames;

/* every line is kept on the screen: a point far off it makes AGG allocate until it crashes the OP-1 ("agg.cpp alloc
 * failed, too many blocks"), which a scaling slip in 0.1.0 did */
static int32_t on_x(int32_t x) { return rt_clamp(x, -Q(4), Q(324)); }
static int32_t on_y(int32_t y) { return rt_clamp(y, -Q(4), Q(164)); }
static void ln(void *r, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w, const uint8_t *c)
{
    x0 = on_x(x0); x1 = on_x(x1); y0 = on_y(y0); y1 = on_y(y1);
    if (x0 == x1 && y0 == y1) y1 += 256;
    sq_linemin(r, x0, y0, x1, y1, w, c, 1);   /* whole pixels, straight lines on the panel's grid: sharp on every panel */
}
static void txt(void *r, int i, const char *s)
{
    static const int32_t ident[9] = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 };
    int same = 1;
    for (int j = 0; j < 48; j++) { if (shown[i][j] != s[j]) { same = 0; break; } if (!s[j]) break; }
    if (!same) { op1_text_set(tx[i], s); int j = 0; for (; j < 47 && s[j]; j++) shown[i][j] = s[j]; shown[i][j] = 0; }
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
static void secs(char *p, int32_t samples44) { put(dec(p, (samples44 * 10 + 2205) / 4410, 2), " s"); }
static void ms44(char *p, int32_t samples44) { put(num(p, (samples44 * 10 + 220) / 441), " ms"); }
/* a node's matrix (row vectors [a b 0; c d 0; tx ty 1], Q10): turned by sin / cos (Q15) about its local point
 * (0, py), which lands on (x, y) Q10 */
static void place(void *node, int32_t x, int32_t y, int32_t py, int32_t s15, int32_t c15)
{
    if (!node) return;
    int32_t c = c15 >> 5, s = s15 >> 5;
    int32_t m[9] = { c, s, 0, -s, c, 0, x + py * s, y - py * c, 1024 };
    op1_node_set_transform(node, m);
}

#define TRUNK_X 156
#define ROOT_Y  123                     /* the recording's start (the dragon below it, 125 .. 146: some panels hide the rows below ~147) */
#define CROWN_Y 23                      /* its end */
#define ROWS    40                      /* a row about every 3 px */
#define SQ_X    187                     /* the squirrel's left edge; its nose 14 px above its origin's row */
#define SQ_NOSE 14
#define HALF_W  22                      /* the widest row's half width */
static int32_t y_of(int32_t s, int32_t len)  /* source sample -> Q10 y */
{
    if (len <= 0) return Q(ROOT_Y);
    int32_t f = (rt_clamp(s, 0, len) << 11) / len;          /* 0 .. 2048; len < 2^19, so the shift fits */
    return Q(ROOT_Y) - f * ((ROOT_Y - CROWN_Y) >> 1);       /* Q10: f / 2048 of the height, times 1024 */
}

static void rt_screen_init(void *scr)
{
    op1_engine_shift_record(&ENGINE_ratatoskr, 1, RT_OVL_STEPS, REC_OVERLAP);
    n_squirrel = op1_find_node(scr, "squirrel");
}

static const char *const motion_name[RT_MOTIONS] = { "hold", "forward", "reverse", "bounce", "wander", "jump" };
static const char *const shape_name[RT_SHAPES] = { "hann", "triangle", "pluck", "swell" };

static void rt_screen_update(void *scr, int dt)
{
    const int slot = OP1_SCREEN_SLOT & 7;
    int16_t kl[4];
    op1_synth_knobs_live(slot, kl);
    const int16_t *w = op1_synth_knobs(slot);
    int16_t p[8];
    for (int i = 0; i < 8; i++) p[i] = i < 4 ? kl[i] : w[i];
    rt_knobs k;
    rt_knobs_of(p, op1_engine_record(slot, rt_owner()), &k);
    if (!shapes_ready) { rt_shapes_init(shapes, OP1_SINE_TABLE); shapes_ready = 1; }
    if (!tx_made) {
        for (int i = 0; i < 4; i++) {                   /* y is the text's top: 6 px above an 8's baseline, 9 a 12's */
            op1_text_ctor(tx[TX_LAB + i], Q(COL_X), row_base[i] - 6, TS_LAB, col4[i]);
            op1_text_ctor(tx[TX_VAL + i], Q(COL_X), row_base[i] + 5, TS, col4[i]);   /* 5 px below the label */
        }
        op1_text_ctor(tx[TX_MSG], Q(TRUNK_X - 26), 76, TS, c_grey);
        op1_text_ctor(tx[TX_SHAPE], Q(226), 30, TS, c_grey);
        op1_text_ctor(tx[TX_TIME], Q(278), 84, TS, c_grey);
        tx_made = 1;
    }

    const uint8_t *sb = op1_sample_buffer(slot);
    const int32_t len = sb ? rt_recorded(sb) : 0;
    const int have = len >= RT_MIN_LEN;
    int32_t lo = 0, hi = 0;
    if (have) rt_route(len, k.position, k.range, &lo, &hi);
    /* before the first note the squirrel waits at the position */
    const int32_t read = have ? rt_clamp(roam_ready ? roam.read : rt_at(k.position, len), 0, len - 1) : 0;

    /* the eagle and dragon stay in the crown and at the roots (the SVG's places); the squirrel at the playhead, tilted the
     * way it is going */
    if (have && was_read >= 0 && read != was_read) { tilt = read > was_read ? 1 : -1; still_frames = 0; }
    else if (++still_frames > 6) { tilt = 0; still_frames = 6; }
    was_read = read;
    const int32_t yp = have ? y_of(read, len) : Q(ROOT_Y);
    const int32_t s25 = tilt ? (tilt > 0 ? 6813 : -6813) : 0, c25 = tilt ? 32051 : 32767;   /* 12 degrees */
    if (n_squirrel) {
        op1_node_set_visible(n_squirrel, have);
        place(n_squirrel, Q(SQ_X), yp, SQ_NOSE, s25, c25);
        op1_screen_draw_cached(scr, dt, n_squirrel);     /* the eagle and dragon before it: a static prefix */
    } else op1_screen_draw(scr, dt);
    void *r = op1_raster();

    if (have) {
        /* the trunk */
        const int16_t *pk = op1_sample_peaks(sb);
        const int nb = (len + 255) >> 8;
        int16_t row[ROWS];
        int top = 1;
        for (int c = 0; c < ROWS; c++) {
            int m = 0;
            for (int j = c * nb / ROWS, e = (c + 1) * nb / ROWS; j <= e && j < nb; j++) if (pk[j] > m) m = pk[j];
            row[c] = (int16_t)m; if (m > top) top = m;
        }
        for (int c = 0; c < ROWS; c++) {
            int32_t y = Q(ROOT_Y) - Q(ROOT_Y - CROWN_Y) * c / (ROWS - 1);
            int32_t s = len * c / (ROWS - 1), h = Q(1) + row[c] * Q(HALF_W - 1) / top;
            ln(r, Q(TRUNK_X) - h, y, Q(TRUNK_X) + h, y, 0x400, s >= lo && s <= hi ? c_bark : c_bark_d);
        }
        ln(r, Q(TRUNK_X), Q(CROWN_Y), Q(TRUNK_X), Q(ROOT_Y), 0x400, c_bark_d);
        /* the squirrel's line to the trunk */
        ln(r, Q(SQ_X - 7), yp, Q(SQ_X - 1), yp, 0x400, c_nose);
        /* the grains just launched: the newest longest */
        const uint32_t n = flashes.n, now = gblk;
        for (uint32_t i = 0; i < RT_FLASHES && i < n; i++) {
            uint32_t j = (n - 1 - i) % RT_FLASHES, age = now - flashes.blk[j];
            if (age > 100) continue;                             /* about 0.15 s */
            int32_t yf = y_of(flashes.at[j], len), l = age < 35 ? Q(9) : Q(5);
            ln(r, Q(TRUNK_X - HALF_W - 4) - l, yf, Q(TRUNK_X - HALF_W - 4), yf, 0x400, c_white);
        }
    } else txt(r, TX_MSG, "no sample");

    /* green on page 2: the grain's shape, time to the right, gain up */
    if (op1_eng_page[1]) {
        const int16_t *wv = shapes[rt_shape(k.shape)];
        const int32_t x0 = 226, x1 = 304, yb = 78, yt = 44;
        txt(r, TX_SHAPE, "grain shape");
        txt(r, TX_TIME, "time");
        ln(r, Q(x0), Q(yt - 2), Q(x0), Q(yb), 0x400, c_axis);
        ln(r, Q(x0), Q(yb), Q(x1), Q(yb), 0x400, c_axis);
        int32_t px = Q(x0), py = Q(yb);
        for (int i = 1; i <= 32; i++) {
            int32_t x = Q(x0) + Q(x1 - x0) * i / 32, y = Q(yb) - (wv[i * 8] * (yb - yt)) / 32;
            op1_pt a = { px, py }, b = { x, y };
            op1_raster_line(r, &a, &b, 0x600, c_green);           /* a curve: smooth, not snapped */
            px = x; py = y;
        }
    }

    /* the left column's strips: 2 x 22 px, from 8 above each label's baseline to its value's foot; a second one
     * marks a knob on page 2 */
    for (int i = 0; i < 4; i++) {
        const int32_t y0 = Q(row_base[i] - 8), y1 = Q(row_base[i] + 14);
        ln(r, Q(11), y0, Q(11), y1, 0x800, col4[i]);
        if (i != 2 && op1_eng_page[i]) ln(r, Q(5), y0, Q(5), y1, 0x800, col4[i]);
    }
    char v[24];
    /* blue: the position while it is being turned (and a moment after), else where the squirrel is */
    if (kl[0] != was_pos) { if (was_pos >= 0) pos_frames = 40; was_pos = kl[0]; }
    else if (pos_frames) pos_frames--;
    if (op1_eng_page[0]) {
        txt(r, TX_LAB, "range");
        put(num(v, (rt_range_q15(k.range) * 100 + 16383) >> 15), "%");
    } else {
        txt(r, TX_LAB, "position");
        secs(v, (pos_frames || !have) ? rt_at(k.position, len) : read);
    }
    txt(r, TX_VAL, v);
    if (op1_shift_held()) {
        int32_t o = rt_ovl8(k.overlap);
        txt(r, TX_LAB + 1, "overlap");
        char *q = num(v, o >> 3);
        if (o & 7) { *q++ = '.'; q = num(q, (o & 7) * 10 / 8); }
        put(q, "x");
    } else if (op1_eng_page[1]) { txt(r, TX_LAB + 1, "shape"); put(v, shape_name[rt_shape(k.shape)]); }
    else { txt(r, TX_LAB + 1, "size"); put(num(v, (rt_size(k.size) * 10 + 441) / 882), " ms"); }
    txt(r, TX_VAL + 1, v);
    txt(r, TX_LAB + 2, "scatter");
    ms44(v, rt_scatter(k.scatter));
    txt(r, TX_VAL + 2, v);
    const int mo = rt_motion(k.motion);
    if (op1_eng_page[3]) {
        txt(r, TX_LAB + 3, "speed");
        if (mo == RT_HOLD) put(v, "-");
        else if (mo == RT_WANDER || mo == RT_JUMP) put(dec(v, (rt_events_q16(k.speed) * 10 + 32768) >> 16, 1), "/s");
        else put(dec(v, (rt_travel_q16(k.speed) * 100 + 32768) >> 16, 2), "x");
    } else { txt(r, TX_LAB + 3, "motion"); put(v, motion_name[mo]); }
    txt(r, TX_VAL + 3, v);
}

const op1_screen_t SCREEN_ratatoskr = { rt_screen_init, rt_screen_update };
