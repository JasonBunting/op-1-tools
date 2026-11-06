/* See pp_player.h. Keys arrive on the KeyHandler thread and ticks on the sequencer thread, so every path that ends
 * playback releases its notes itself, and pp_tick also releases anything left sounding after playback ended.
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#pragma GCC optimize("no-tree-loop-distribute-patterns")      /* the firmware has no memcpy / memset */
#include "pp_player.h"

pp_bank_t pp_bank[2];

static pp_bank_t *bank(int mode) { return &pp_bank[mode & 1]; }
static pp_page_t *sel_page(pp_bank_t *b) { return &b->pages[b->sel]; }
static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int is_running(const pp_bank_t *b) { return b->hold || b->down; }

static void page_defaults(pp_page_t *p)
{
    p->start = 0;
    p->len = PP_COLS;
    p->swing = PP_SWING_NONE;
    p->dir = PP_DIR_FORWARD;
}
void pp_init(int mode)
{
    pp_bank_t *b = bank(mode);
    if (b->init) return;
    page_defaults(&b->pages[0]);
    b->count = 1;
    b->pitch_key = PP_ROOT_KEY;
    b->follow = 1;
    b->init = 1;
}
int pp_running(int mode) { return is_running(bank(mode)); }

static void silence(pp_bank_t *b)
{
    if (!b->sounding) return;
    b->sounding = 0;
    for (int k = 0; k < PP_KEYS; k++) {
        int id = b->ids[k] - 1;
        b->ids[k] = 0;                      /* cleared first: a preempting thread cannot release it twice */
        if (id >= 0) pp_out_off(id);
    }
}

/* the first step of the page that plays next */
static void place_first(pp_bank_t *b)
{
    const pp_page_t *p = &b->pages[b->play_page];
    b->pos = p->dir == PP_DIR_REVERSE ? (uint8_t)(p->start + p->len - 1) : p->start;
    b->back = 0;
}
static void begin(pp_bank_t *b)
{
    silence(b);
    b->follow = 1;
    b->chain_pos = 0;
    if (b->chain_len) b->sel = b->chain[0];
    b->play_page = b->sel;
    b->elapsed = 0;
    b->started = 0;
    place_first(b);
}
/* The end of a page's cycle: the only place the playing page changes. */
static void loop_point(pp_bank_t *b)
{
    int len = b->chain_len;
    if (len) {
        int next = b->chain_pos + 1;
        if (next >= len) next = 0;
        b->chain_pos = (int8_t)next;
        b->play_page = b->chain[next];
        if (b->follow) b->sel = b->play_page;
    } else {
        b->chain_pos = 0;
        b->play_page = b->sel;
    }
    place_first(b);
}
static void advance(pp_bank_t *b)
{
    const pp_page_t *p = &b->pages[b->play_page];
    int s = p->start, e = s + p->len, at = b->pos;
    if (at < s || at >= e) { loop_point(b); return; }       /* the section moved away from the playhead */
    if (p->dir == PP_DIR_REVERSE) {
        if (at - 1 < s) loop_point(b); else b->pos = (uint8_t)(at - 1);
    } else if (p->dir == PP_DIR_CYCLE) {
        /* there and back, ends once each: s .. e-1 .. s+1; arriving back at s is the end of the cycle */
        if (!b->back && at + 1 < e) { b->pos = (uint8_t)(at + 1); return; }
        b->back = 1;
        if (at - 1 <= s) loop_point(b); else b->pos = (uint8_t)(at - 1);
    } else {
        if (at + 1 >= e) loop_point(b); else b->pos = (uint8_t)(at + 1);
    }
}

void pp_tick(int mode)
{
    pp_bank_t *b = bank(mode);
    /* Ticks only reach the current mode's player: a note left in the other mode would never get its note-off. */
    silence(bank(!(mode & 1)));
    if (!b->init || !is_running(b)) { silence(b); return; }
    if (b->started) {
        b->elapsed++;
        if (b->elapsed >= b->step_len) {        /* as stock: notes last the whole step, released just before the next */
            silence(b);
            b->elapsed = 0;
            b->started = 0;
            advance(b);
        }
    }
    if (!b->started) {
        const pp_page_t *p = &b->pages[b->play_page];
        int at = b->pos & (PP_COLS - 1);
        uint32_t notes = p->notes[at];
        for (int k = 0; notes && k < PP_KEYS; k++, notes >>= 1) {
            if (!(notes & 1)) continue;
            int id = pp_out_on(mode, k, b->transpose, PP_VELOCITY);
            if (id >= 0) { b->ids[k] = (int16_t)(id + 1); b->sounding = 1; }
        }
        int first = PP_PAIR_TICKS * p->swing / 100;         /* swing moves the middle of each pair of steps, either way */
        b->step_len = (uint8_t)(at & 1 ? PP_PAIR_TICKS - first : first);
        b->started = 1;
    }
}

void pp_key_down(int mode, int key)
{
    pp_bank_t *b = bank(mode);
    if (key < 0 || key >= PP_KEYS) return;
    pp_init(mode);
    b->transpose = (int8_t)(key - PP_ROOT_KEY);
    if (!is_running(b)) begin(b);
    b->down |= 1u << key;
}
void pp_key_up(int mode, int key)
{
    pp_bank_t *b = bank(mode);
    if (key < 0 || key >= PP_KEYS || !(b->down >> key & 1)) return;
    b->down &= ~(1u << key);
    if (!is_running(b)) silence(b);
}
void pp_set_hold(int mode, int on)
{
    pp_bank_t *b = bank(mode);
    pp_init(mode);
    if (on && !is_running(b)) begin(b);
    b->hold = (uint8_t)(on != 0);
    if (!is_running(b)) silence(b);
}
void pp_rewind(int mode)
{
    pp_bank_t *b = bank(mode);
    if (b->init) begin(b);
}
void pp_silence_all(int mode)
{
    pp_bank_t *b = bank(mode);
    b->down = 0;
    silence(b);
    b->elapsed = 0;
    b->started = 0;
    if (b->init) place_first(b);
}

/* ---- editing ---- */
/* While the playhead is on the page shown, editing happens at the playhead; otherwise the cursor is free. */
static int on_playhead(const pp_bank_t *b) { return is_running(b) && b->play_page == b->sel; }
int pp_edit_col(int mode)
{
    pp_bank_t *b = bank(mode);
    return on_playhead(b) ? b->pos : b->cursor;
}
void pp_set_pitch(int mode, int key)
{
    pp_init(mode);
    if (key >= 0 && key < PP_KEYS) bank(mode)->pitch_key = (uint8_t)key;
}
void pp_note_entry(int mode, int key)
{
    pp_bank_t *b = bank(mode);
    if (key < 0 || key >= PP_KEYS) return;
    pp_init(mode);
    b->pitch_key = (uint8_t)key;
    volatile uint32_t *col = &sel_page(b)->notes[pp_edit_col(mode)];
    if (*col >> key & 1) { *col &= ~(1u << key); return; }  /* already there: erase it, and the cursor stays */
    *col |= 1u << key;
    if (!on_playhead(b)) b->cursor = (uint8_t)((b->cursor + 1) % PP_COLS);
}
void pp_move_cursor(int mode, int delta)
{
    pp_bank_t *b = bank(mode);
    pp_init(mode);
    if (on_playhead(b)) return;
    b->cursor = (uint8_t)((b->cursor + delta % PP_COLS + PP_COLS) % PP_COLS);
}
void pp_erase_move(int mode, int delta)
{
    pp_bank_t *b = bank(mode);
    pp_init(mode);
    if (on_playhead(b)) return;
    int step = delta < 0 ? -1 : 1;
    for (int i = delta < 0 ? -delta : delta; i > 0; i--) {
        sel_page(b)->notes[b->cursor] = 0;
        b->cursor = (uint8_t)((b->cursor + step + PP_COLS) % PP_COLS);
    }
}
void pp_add_swing(int mode, int delta)
{
    pp_init(mode);
    pp_page_t *p = sel_page(bank(mode));
    p->swing = (uint8_t)clamp(p->swing + delta, PP_SWING_MIN, PP_SWING_MAX);
}
void pp_rotate(int mode, int delta)
{
    pp_init(mode);
    pp_page_t *p = sel_page(bank(mode));
    int s = p->start, n = p->len, step = delta < 0 ? -1 : 1;
    for (int i = delta < 0 ? -delta : delta; i > 0; i--) {
        if (step > 0) {
            uint32_t last = p->notes[s + n - 1];
            for (int c = s + n - 1; c > s; c--) p->notes[c] = p->notes[c - 1];
            p->notes[s] = last;
        } else {
            uint32_t first = p->notes[s];
            for (int c = s; c < s + n - 1; c++) p->notes[c] = p->notes[c + 1];
            p->notes[s + n - 1] = first;
        }
    }
}
void pp_add_length(int mode, int delta)
{
    pp_init(mode);
    pp_page_t *p = sel_page(bank(mode));
    p->len = (uint8_t)clamp(p->len + delta, 1, PP_COLS - p->start);
}
void pp_move_section(int mode, int delta)
{
    pp_init(mode);
    pp_page_t *p = sel_page(bank(mode));
    p->start = (uint8_t)clamp(p->start + delta, 0, PP_COLS - p->len);
}
void pp_add_dir(int mode, int delta)
{
    pp_init(mode);
    pp_page_t *p = sel_page(bank(mode));
    p->dir = (uint8_t)clamp(p->dir + delta, 0, PP_DIRS - 1);
}
void pp_page_prev(int mode)
{
    pp_bank_t *b = bank(mode);
    pp_init(mode);
    if (is_running(b) && b->chain_len) b->follow = 0;
    if (b->sel) b->sel--;
}
int pp_page_next(int mode)
{
    pp_bank_t *b = bank(mode);
    pp_init(mode);
    if (is_running(b) && b->chain_len) b->follow = 0;
    if (b->sel + 1 < b->count) { b->sel++; return 0; }
    if (b->count >= PP_PAGES) return -1;
    page_defaults(&b->pages[b->count]);     /* its notes are still zero: pages are never removed yet */
    b->count++;
    b->sel++;
    return 1;
}

void pp_clear(int mode)
{
    pp_bank_t *b = bank(mode);
    pp_init(mode);
    b->hold = 0;
    b->down = 0;
    silence(b);
    b->chain_len = 0;
    b->chain_cursor = 0;
    b->chain_pick = 0;
    b->chain_pos = 0;
    b->sel = 0;
    b->play_page = 0;
    b->count = 1;
    b->cursor = 0;
    b->pitch_key = PP_ROOT_KEY;
    b->transpose = 0;
    b->follow = 1;
    b->elapsed = 0;
    b->started = 0;
    for (int i = 0; i < PP_PAGES; i++) {
        for (int c = 0; c < PP_COLS; c++) b->pages[i].notes[c] = 0;
        page_defaults(&b->pages[i]);
    }
    place_first(b);
}

/* ---- chain: keys run on another thread than ticks, so the tick may see an edit half done. Every entry is always a
 * page that exists and pages are never removed, so the worst it can do is pick a neighbouring occurrence once. ---- */
void pp_view_toggle(int mode)
{
    pp_bank_t *b = bank(mode);
    pp_init(mode);
    b->view = (uint8_t)((b->view + 1) % PP_VIEWS);
    if (b->view == PP_VIEW_CHAIN) b->chain_pick = b->chain_cursor < b->chain_len ? b->chain[b->chain_cursor] : b->sel;
}
void pp_chain_move(int mode, int delta)
{
    pp_bank_t *b = bank(mode);
    pp_init(mode);
    int last = b->chain_len < PP_CHAIN_MAX ? b->chain_len : PP_CHAIN_MAX - 1;
    b->chain_cursor = (uint8_t)clamp(b->chain_cursor + delta, 0, last);
    if (b->chain_cursor < b->chain_len) b->chain_pick = b->chain[b->chain_cursor];
}
void pp_chain_pick(int mode, int delta)
{
    pp_bank_t *b = bank(mode);
    pp_init(mode);
    b->chain_pick = (uint8_t)clamp(b->chain_pick + delta, 0, b->count - 1);
    if (b->chain_cursor < b->chain_len) b->chain[b->chain_cursor] = b->chain_pick;
}
int pp_chain_insert(int mode)
{
    pp_bank_t *b = bank(mode);
    pp_init(mode);
    int len = b->chain_len, at = b->chain_cursor;
    if (len >= PP_CHAIN_MAX) return -1;
    for (int i = len; i > at; i--) b->chain[i] = b->chain[i - 1];
    b->chain[at] = b->chain_pick;
    if (len && at <= b->chain_pos) b->chain_pos++;          /* the occurrence playing moved along */
    b->chain_len = (uint8_t)(len + 1);
    if (at + 1 < PP_CHAIN_MAX) b->chain_cursor = (uint8_t)(at + 1);
    return 0;
}
void pp_chain_remove(int mode)
{
    pp_bank_t *b = bank(mode);
    pp_init(mode);
    int len = b->chain_len, at = b->chain_cursor;
    if (at >= len) return;
    for (int i = at; i + 1 < len; i++) b->chain[i] = b->chain[i + 1];
    if (at <= b->chain_pos) b->chain_pos--;                 /* removing the one playing: it finishes, then its successor */
    b->chain_len = (uint8_t)(len - 1);
    if (at < b->chain_len) b->chain_pick = b->chain[at];
}
void pp_resume_follow(int mode)
{
    pp_bank_t *b = bank(mode);
    pp_init(mode);
    b->follow = 1;
    if (is_running(b) && b->chain_len) b->sel = b->play_page;
}
