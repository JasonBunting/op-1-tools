/* Host test of Pattern+'s pages, grid editing and playback.
 *   gcc -Wall -I patternplus/plugin patternplus/plugin/test/host_test.c patternplus/plugin/pp_player.c patternplus/plugin/pp_store.c -o /tmp/pp_test && /tmp/pp_test
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#include <stdio.h>
#include <string.h>
#include "pp_player.h"

static int failures, ons, offs, sounding, last_key, last_transpose, next_id, tick_of_last_on, now;
static int open_ids[1024];

int pp_out_on(int mode, int key, int transpose, int vel)
{
    (void)mode; (void)vel;
    int id = next_id++ % 1024;
    open_ids[id] = 1;
    ons++; sounding++; last_key = key; last_transpose = transpose; tick_of_last_on = now;
    return id;
}
void pp_out_off(int id)
{
    if (!open_ids[id]) { printf("FAIL: note-off for an id that is not sounding: %d\n", id); failures++; }
    open_ids[id] = 0;
    offs++; sounding--;
}
static void check(const char *what, int got, int want)
{
    if (got != want) { printf("FAIL: %s: got %d, want %d\n", what, got, want); failures++; }
}
static void fresh(void)
{
    memset(pp_bank, 0, sizeof pp_bank);
    memset(open_ids, 0, sizeof open_ids);
    ons = offs = sounding = last_key = last_transpose = next_id = tick_of_last_on = now = 0;
    pp_init(0);
    pp_init(1);
}
static void ticks(int mode, int n) { while (n--) { now++; pp_tick(mode); } }
static pp_page_t *page(int mode, int i) { return &pp_bank[mode].pages[i]; }

/* a save text with its check digits made right again, so a test reaches the check it means to */
static void fix_sum(char *s, int len)
{
    unsigned sum = 0;
    for (int i = 0; i < len - 4; i++) sum += (unsigned char)s[i];
    for (int i = 3; i >= 0; i--, sum >>= 4) s[len - 4 + i] = "0123456789ABCDEF"[sum & 15];
}
static void fill_all(int mode)
{
    pp_bank_t *b = &pp_bank[mode];
    while (b->count < PP_PAGES) pp_page_next(mode);
    for (int i = 0; i < PP_PAGES; i++) {
        pp_page_t *g = &b->pages[i];
        for (int c = 0; c < PP_COLS; c++) g->notes[c] = (0xFFFFFFu >> (i + c % 8)) | 1u << 23;
        g->start = (uint8_t)i; g->len = (uint8_t)(PP_COLS - i); g->swing = (uint8_t)(PP_SWING_MIN + i * 3); g->dir = (uint8_t)(i % PP_DIRS);
    }
    for (int i = 0; i < PP_CHAIN_MAX; i++) b->chain[i] = (uint8_t)(i % PP_PAGES);
    b->chain_len = PP_CHAIN_MAX;
    b->sel = 5; b->pitch_key = 19;
}

/* the columns visited for n steps, as a page's notes (one distinct key per column) play them */
static void expect_order(const char *what, const int *want, int n)
{
    for (int c = 0; c < PP_COLS; c++) page(0, pp_bank[0].sel)->notes[c] = 1u << c;
    pp_set_hold(0, 1);
    now++; pp_tick(0);
    for (int i = 0; i < n; i++) {
        check(what, last_key, want[i]);
        int before = ons;
        while (ons == before && now < 100000) ticks(0, 1);
    }
    pp_set_hold(0, 0);
}

int main(void)
{
    /* --- entry: the cursor moves on with each note, wraps, and notes stack if you come back --- */
    fresh();
    pp_note_entry(0, 3);
    pp_note_entry(0, 4);
    check("first note in column 0", page(0, 0)->notes[0], 1 << 3);
    check("second note in column 1", page(0, 0)->notes[1], 1 << 4);
    check("cursor moved on", pp_bank[0].cursor, 2);
    pp_move_cursor(0, -2);
    pp_note_entry(0, 9);
    check("notes stack in a column", page(0, 0)->notes[0], 1 << 3 | 1 << 9);
    pp_move_cursor(0, -3);
    check("cursor wraps", pp_bank[0].cursor, 14);
    check("no line 16: wrap stays in 0..15", pp_bank[0].cursor < PP_COLS, 1);

    /* --- erase clears what the cursor leaves --- */
    fresh();
    pp_note_entry(0, 3);
    pp_note_entry(0, 3);
    pp_note_entry(0, 3);
    pp_move_cursor(0, -3);
    pp_erase_move(0, 2);
    check("erased 0", page(0, 0)->notes[0], 0);
    check("erased 1", page(0, 0)->notes[1], 0);
    check("kept 2", page(0, 0)->notes[2], 1 << 3);

    /* --- pages: A then blank B, A intact; only a deliberate step creates; limit --- */
    fresh();
    pp_note_entry(0, 3);
    check("next creates", pp_page_next(0), 1);
    check("on B", pp_bank[0].sel, 1);
    check("B starts blank", page(0, 1)->notes[0], 0);
    check("B has defaults", page(0, 1)->len, PP_COLS);
    pp_note_entry(0, 5);
    pp_page_prev(0);
    check("A intact", page(0, 0)->notes[0], 1 << 3);
    pp_page_prev(0);
    check("previous on A stays on A", pp_bank[0].sel, 0);
    check("moving to an existing page creates nothing", pp_page_next(0), 0);
    check("still two pages", pp_bank[0].count, 2);
    while (pp_bank[0].count < PP_PAGES) pp_page_next(0);
    while (pp_bank[0].sel + 1 < PP_PAGES) pp_page_next(0);
    check("at the limit", pp_page_next(0), -1);
    check("limit changes nothing", pp_bank[0].sel, PP_PAGES - 1);
    check("limit keeps the count", pp_bank[0].count, PP_PAGES);
    check("modes are separate", pp_bank[1].count, 1);

    /* --- section, rotation inside it only --- */
    fresh();
    pp_add_length(0, -6);
    check("length", page(0, 0)->len, 10);
    pp_move_section(0, 3);
    check("section moved", page(0, 0)->start, 3);
    pp_move_section(0, 99);
    check("section clamps", page(0, 0)->start, 6);
    pp_add_length(0, 99);
    check("length clamps to the grid", page(0, 0)->start + page(0, 0)->len, PP_COLS);
    pp_add_length(0, -99);
    check("length at least 1", page(0, 0)->len, 1);
    fresh();
    page(0, 0)->start = 2; page(0, 0)->len = 3;
    page(0, 0)->notes[0] = 1; page(0, 0)->notes[2] = 2; page(0, 0)->notes[3] = 4; page(0, 0)->notes[4] = 8;
    page(0, 0)->notes[5] = 16;
    pp_rotate(0, 1);
    check("outside the section stays", page(0, 0)->notes[0], 1);
    check("outside the section stays", page(0, 0)->notes[5], 16);
    check("wrapped", page(0, 0)->notes[2], 8);
    check("moved", page(0, 0)->notes[3], 2);
    pp_rotate(0, -1);
    check("rotate back", page(0, 0)->notes[2], 2);

    /* --- direction order --- */
    fresh();
    { static const int w[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0, 1 };
      expect_order("forward", w, 18); }
    fresh();
    page(0, 0)->start = 2; page(0, 0)->len = 3;
    { static const int w[] = { 2, 3, 4, 2, 3 }; expect_order("forward section", w, 5); }
    fresh();
    page(0, 0)->start = 2; page(0, 0)->len = 3; page(0, 0)->dir = PP_DIR_REVERSE;
    { static const int w[] = { 4, 3, 2, 4, 3 }; expect_order("reverse", w, 5); }
    fresh();
    page(0, 0)->start = 2; page(0, 0)->len = 4; page(0, 0)->dir = PP_DIR_CYCLE;
    { static const int w[] = { 2, 3, 4, 5, 4, 3, 2, 3, 4 }; expect_order("cycle", w, 9); }
    fresh();
    page(0, 0)->start = 2; page(0, 0)->len = 2; page(0, 0)->dir = PP_DIR_CYCLE;
    { static const int w[] = { 2, 3, 2, 3 }; expect_order("cycle of two", w, 4); }
    fresh();
    page(0, 0)->start = 7; page(0, 0)->len = 1; page(0, 0)->dir = PP_DIR_CYCLE;
    { static const int w[] = { 7, 7, 7 }; expect_order("cycle of one", w, 3); }

    /* --- swing: pairs still add up, so the beat holds --- */
    fresh();
    page(0, 0)->swing = 62;
    for (int c = 0; c < PP_COLS; c++) page(0, 0)->notes[c] = 1;
    pp_set_hold(0, 1);
    ticks(0, 1);
    int t0 = tick_of_last_on;
    { int n = ons; while (ons == n) ticks(0, 1); }
    check("long step at 62%", tick_of_last_on - t0, 29);
    { int n = ons; while (ons == n) ticks(0, 1); }
    check("pair length unchanged by swing", tick_of_last_on - t0, PP_PAIR_TICKS);

    /* --- swing below 50: a short first step still releases, and the pair still adds up --- */
    fresh();
    check("no swing by default", page(0, 0)->swing, PP_SWING_NONE);
    pp_add_swing(0, -99);
    check("swing minimum", page(0, 0)->swing, 20);
    pp_add_swing(0, 99);
    check("swing maximum", page(0, 0)->swing, 80);
    page(0, 0)->swing = 20;
    for (int c = 0; c < PP_COLS; c++) page(0, 0)->notes[c] = 1;
    pp_set_hold(0, 1);
    ticks(0, 1);
    t0 = tick_of_last_on;
    { int n = ons; while (ons == n) { ticks(0, 1); check("one note at a time", sounding <= 1, 1); } }
    check("short step at 20%", tick_of_last_on - t0, 9);
    { int n = ons; while (ons == n) ticks(0, 1); }
    check("pair length at 20%", tick_of_last_on - t0, PP_PAIR_TICKS);

    /* --- notes pair; a chord lasts its whole step and releases together, as stock --- */
    fresh();
    page(0, 0)->notes[0] = 1 << 2 | 1 << 5 | 1 << 9;
    pp_set_hold(0, 1);
    ticks(0, 1);
    check("chord", sounding, 3);
    ticks(0, PP_PAIR_TICKS / 2 - 1);
    check("chord lasts to the end of the step", sounding, 3);
    ticks(0, 1);
    check("next step ends the chord", sounding, 0);
    check("all released", offs, 3);

    /* --- transposition, no restart under HOLD, held key without HOLD --- */
    fresh();
    for (int c = 0; c < PP_COLS; c++) page(0, 0)->notes[c] = 1;
    pp_key_down(0, PP_ROOT_KEY);
    ticks(0, 1);
    check("root key plays as written", last_transpose, 0);
    ticks(0, 24);
    int pos = pp_bank[0].pos;
    pp_key_down(0, PP_ROOT_KEY + 2);
    check("second key doesn't restart", pp_bank[0].pos, pos);
    ticks(0, 24);
    check("second key transposes", last_transpose, 2);
    pp_key_up(0, PP_ROOT_KEY);
    check("still running with one key down", pp_running(0), 1);
    pp_key_up(0, PP_ROOT_KEY + 2);
    check("stopped", pp_running(0), 0);
    check("release silences", sounding, 0);
    { int n = ons; ticks(0, 200); check("nothing after release", ons, n); }

    /* --- no duplicate first step when restarting --- */
    fresh();
    for (int c = 0; c < PP_COLS; c++) page(0, 0)->notes[c] = 1;
    pp_key_down(0, 7);
    ticks(0, 1);
    pp_key_up(0, 7);
    pp_key_down(0, 7);
    ticks(0, 1);
    check("one note per start", ons, 2);
    check("position restarted", pp_bank[0].pos, 0);

    /* --- running: cursor frozen, notes go to the playhead without moving anything --- */
    fresh();
    pp_set_hold(0, 1);
    ticks(0, 1 + 24 * 3);
    check("playhead", pp_bank[0].pos, 3);
    pp_move_cursor(0, 5);
    check("cursor frozen", pp_bank[0].cursor, 0);
    pp_note_entry(0, 11);
    check("live entry at the playhead", page(0, 0)->notes[3], 1 << 11);
    check("live entry leaves the cursor", pp_bank[0].cursor, 0);

    /* --- live edit: Shift + arrow frees the cursor from the playhead; playback carries on --- */
    pp_live_move(0, 1);
    check("live edit on", pp_bank[0].live, 1);
    check("cursor starts from the playhead", pp_bank[0].cursor, 4);
    check("playhead unmoved", pp_bank[0].pos, 3);
    pp_move_cursor(0, 2);
    check("plain arrows move it now", pp_bank[0].cursor, 6);
    check("notes go to the cursor", pp_edit_col(0), 6);
    pp_note_entry(0, 5);
    check("entered at the cursor", page(0, 0)->notes[6], 1 << 5);
    check("cursor moves on", pp_bank[0].cursor, 7);
    pp_erase_move(0, -1);
    pp_erase_move(0, -1);
    check("erase works at the cursor", page(0, 0)->notes[6], 0);
    ticks(0, 24);
    check("still playing", pp_bank[0].pos, 4);
    check("cursor stays put", pp_bank[0].cursor, 5);
    pp_live_move(0, -1);
    check("Shift + arrow again just moves", pp_bank[0].cursor, 4);
    pp_resume_follow(0);
    check("resume ends live edit", pp_bank[0].live, 0);
    check("back on the playhead", pp_edit_col(0), 4);
    pp_live_move(0, 1);
    pp_set_hold(0, 0);
    pp_set_hold(0, 1);
    check("restarting ends live edit", pp_bank[0].live, 0);
    pp_set_hold(0, 0);
    pp_live_move(0, 1);
    check("stopped: no live edit", pp_bank[0].live, 0);
    /* with a chain, the grid stays on the page while the chain goes on, and the cursor is still free when it returns */
    fresh();
    pp_page_next(0); pp_page_prev(0);
    page(0, 0)->len = 2; page(0, 1)->len = 2;
    pp_bank[0].chain[0] = 0; pp_bank[0].chain[1] = 1; pp_bank[0].chain_len = 2;
    pp_set_hold(0, 1);
    ticks(0, 1);
    pp_live_move(0, 1);
    check("chain: cursor on A's second step", pp_bank[0].cursor, 1);
    check("chain: stops following", pp_bank[0].follow, 0);
    ticks(0, 48);
    check("chain: B plays", pp_bank[0].play_page, 1);
    check("chain: grid stays on A", pp_bank[0].sel, 0);
    ticks(0, 48);
    check("chain: A plays again", pp_bank[0].play_page, 0);
    check("chain: cursor still free", pp_edit_col(0), 1);
    pp_resume_follow(0);
    check("chain: following again", pp_bank[0].follow, 1);
    pp_set_hold(0, 0);
    ticks(0, 1);

    /* --- page change while playing waits for the end of the page --- */
    fresh();
    page(0, 0)->len = 2;
    pp_page_next(0);
    page(0, 1)->notes[0] = 1 << 20;
    pp_page_prev(0);
    page(0, 0)->notes[0] = 1 << 1;
    page(0, 0)->notes[1] = 1 << 2;
    pp_set_hold(0, 1);
    ticks(0, 1);
    check("A first", last_key, 1);
    pp_page_next(0);
    check("still playing A", pp_bank[0].play_page, 0);
    ticks(0, 24);
    check("A finishes", last_key, 2);
    ticks(0, 24);
    check("B after A's end", last_key, 20);
    check("playing B", pp_bank[0].play_page, 1);

    /* --- mode change, stop and rewind leave nothing sounding; HOLD survives a stop --- */
    fresh();
    page(0, 0)->notes[0] = 1;
    pp_set_hold(0, 1);
    ticks(0, 1);
    check("synth sounding", sounding, 1);
    ticks(1, 1);
    check("ticks in drum release the synth note", sounding, 0);

    fresh();
    pp_key_down(1, 5);
    pp_key_up(0, 5);
    check("the other mode's release changes nothing", pp_running(1), 1);
    pp_key_up(1, 5);
    check("key released", pp_running(1), 0);

    fresh();
    for (int c = 0; c < PP_COLS; c++) page(0, 0)->notes[c] = 1;
    pp_set_hold(0, 1);
    ticks(0, 1 + 24);
    check("sounding before stop", sounding, 1);
    pp_silence_all(0);
    check("stop silences", sounding, 0);
    check("stop turns HOLD off", pp_bank[0].hold, 0);
    check("stop rewinds", pp_bank[0].pos, 0);
    pp_key_down(0, 19);
    pp_set_hold(0, 1);
    check("transposed while playing", pp_bank[0].transpose, 12);
    pp_silence_all(0);
    check("stop clears the transposition", pp_bank[0].transpose, 0);
    pp_key_down(0, 19);
    pp_key_up(0, 19);
    check("letting go clears it", pp_bank[0].transpose, 0);
    pp_key_down(0, 19);
    pp_set_hold(0, 1);
    pp_key_up(0, 19);
    check("HOLD keeps it", pp_bank[0].transpose, 12);
    pp_set_hold(0, 0);
    check("HOLD off clears it", pp_bank[0].transpose, 0);
    check("has notes", pp_has_notes(0), 1);
    page(0, 0)->start = 4; page(0, 0)->len = 2;
    for (int c = 0; c < PP_COLS; c++) page(0, 0)->notes[c] = c == 4 || c == 5 ? 0 : 1;
    check("notes outside the section don't count", pp_has_notes(0), 0);
    page(0, 0)->start = 0; page(0, 0)->len = PP_COLS;
    pp_rewind(0);
    check("rewind silences", sounding, 0);
    pp_set_hold(0, 0);
    ticks(0, 200);
    check("every note released", offs, ons);
    check("nothing left sounding", sounding, 0);

    /* --- chain: A > B > B > C, two steps per page, twice around; both Bs complete --- */
    fresh();
    pp_page_next(0); pp_page_next(0);
    for (int i = 0; i < 3; i++) { page(0, i)->len = 2; page(0, i)->notes[0] = 1u << i; page(0, i)->notes[1] = 1u << (i + 8); }
    pp_view_toggle(0);
    check("view shows the chain", pp_bank[0].view, 1);
    { static const int8_t want[] = { 0, 1, 1, 2 };
      pp_chain_pick(0, -9);
      for (int i = 0; i < 4; i++) { pp_chain_pick(0, want[i] - pp_bank[0].chain_pick); check("insert", pp_chain_insert(0), 0); }
      check("chain length", pp_bank[0].chain_len, 4);
      pp_set_hold(0, 1);
      ticks(0, 1);
      for (int lap = 0; lap < 2; lap++)
          for (int i = 0; i < 4; i++) {
              check("chain plays the occurrence", pp_bank[0].chain_pos, i);
              check("first step's note", last_key, want[i]);
              check("grid follows", pp_bank[0].sel, want[i]);
              ticks(0, 24);
              check("second step's note", last_key, want[i] + 8);
              ticks(0, 24);
          }
    }
    /* switching views doesn't disturb playback */
    { int pos = pp_bank[0].pos, at = pp_bank[0].chain_pos, el = pp_bank[0].elapsed;
      pp_view_toggle(0); pp_view_toggle(0);
      check("view keeps pos", pp_bank[0].pos, pos); check("view keeps occurrence", pp_bank[0].chain_pos, at); check("view keeps timing", pp_bank[0].elapsed, el); }
    /* manual navigation suspends following; the cursor is free on a page that isn't playing; resume returns */
    pp_view_toggle(0);
    while (pp_bank[0].play_page != 0) ticks(0, 1);
    pp_page_next(0);
    check("following suspended", pp_bank[0].follow, 0);
    check("shows the chosen page", pp_bank[0].sel, 1);
    { int cur = pp_bank[0].cursor;
      pp_move_cursor(0, 1);
      check("cursor free off the playhead", pp_bank[0].cursor, cur + 1); }
    while (pp_bank[0].play_page != 2) ticks(0, 1);
    check("stays while suspended", pp_bank[0].sel, 1);
    pp_resume_follow(0);
    check("resume shows the playing page", pp_bank[0].sel, 2);
    check("following resumed", pp_bank[0].follow, 1);
    pp_view_toggle(0);
    /* removing the occurrence playing: it finishes, and its successor plays */
    pp_set_hold(0, 0); ticks(0, 1); pp_set_hold(0, 1); ticks(0, 1);
    check("restart from the first entry", pp_bank[0].chain_pos, 0);
    pp_bank[0].chain_cursor = 0;
    pp_chain_remove(0);
    check("removed", pp_bank[0].chain_len, 3);
    check("still the page that was playing", pp_bank[0].play_page, 0);
    ticks(0, 48);
    check("then the successor", pp_bank[0].chain_pos, 0);
    check("which is B", pp_bank[0].play_page, 1);
    /* inserting before the occurrence playing keeps it the one playing */
    pp_bank[0].chain_cursor = 0; pp_bank[0].chain_pick = 2;
    pp_chain_insert(0);
    check("occurrence moved along", pp_bank[0].chain_pos, 1);
    check("same page", pp_bank[0].chain[1], 1);
    /* limits */
    pp_set_hold(0, 0); ticks(0, 1);
    pp_bank[0].chain_cursor = pp_bank[0].chain_len;
    while (pp_bank[0].chain_len < PP_CHAIN_MAX) check("fill", pp_chain_insert(0), 0);
    check("full", pp_chain_insert(0), -1);
    check("cursor stays on the chain", pp_bank[0].chain_cursor < PP_CHAIN_MAX, 1);
    pp_bank[0].chain_cursor = PP_CHAIN_MAX;
    pp_chain_move(0, 1);
    check("cursor stops at the last slot", pp_bank[0].chain_cursor, PP_CHAIN_MAX - 1);
    pp_chain_pick(0, -99);
    check("pick stops at A", pp_bank[0].chain_pick, 0);
    pp_chain_pick(0, 99);
    check("pick stops at the last page", pp_bank[0].chain_pick, pp_bank[0].count - 1);
    /* emptying the chain goes back to looping the selected page, with nothing left sounding */
    pp_set_hold(0, 1); ticks(0, 5);
    pp_bank[0].chain_cursor = 0;
    while (pp_bank[0].chain_len) pp_chain_remove(0);
    pp_chain_remove(0);
    pp_bank[0].sel = 2;
    ticks(0, 100);
    check("no chain: the selected page", pp_bank[0].play_page, 2);
    pp_set_hold(0, 0);
    ticks(0, 1);
    check("chain: every note released", offs, ons);

    /* --- #8: the cursor walks the chain A > B > A > C by section ends; D, not in it, wraps on itself --- */
    fresh();
    for (int i = 0; i < 3; i++) pp_page_next(0);
    page(0, 0)->start = 2; page(0, 0)->len = 3;             /* A: steps 2..4 */
    page(0, 1)->len = 2;                                    /* B: 0..1 */
    page(0, 2)->start = 4; page(0, 2)->len = 4;             /* C: 4..7 */
    { static const uint8_t c[] = { 0, 1, 0, 2 };
      for (int i = 0; i < 4; i++) pp_bank[0].chain[i] = c[i];
      pp_bank[0].chain_len = 4; }
    pp_bank[0].sel = 0; pp_bank[0].cursor = 2;
    pp_move_cursor(0, 2);
    check("within A", pp_bank[0].cursor, 4); check("still A", pp_bank[0].sel, 0);
    pp_move_cursor(0, 1);
    check("on to B", pp_bank[0].sel, 1); check("B's first step", pp_bank[0].cursor, 0); check("occurrence 1", pp_bank[0].edit_occ, 1);
    pp_move_cursor(0, 2);
    check("on to the second A", pp_bank[0].sel, 0); check("A's first step", pp_bank[0].cursor, 2); check("occurrence 2", pp_bank[0].edit_occ, 2);
    pp_move_cursor(0, 3);
    check("then C, not B again", pp_bank[0].sel, 2); check("C's first step", pp_bank[0].cursor, 4);
    pp_move_cursor(0, 4);
    check("wraps to A", pp_bank[0].sel, 0); check("first occurrence", pp_bank[0].edit_occ, 0); check("at A's start", pp_bank[0].cursor, 2);
    pp_move_cursor(0, -1);
    check("back wraps to C", pp_bank[0].sel, 2); check("C's last step", pp_bank[0].cursor, 7);
    pp_move_cursor(0, -4);
    check("back to the second A", pp_bank[0].sel, 0); check("its last step", pp_bank[0].cursor, 4); check("occurrence 2 again", pp_bank[0].edit_occ, 2);
    /* the single-page key: wraps on the page */
    pp_set_solo(0, 1);
    pp_move_cursor(0, 1);
    check("solo stays on A", pp_bank[0].sel, 0); check("solo moves in the grid", pp_bank[0].cursor, 5);
    pp_set_solo(0, 0);
    pp_move_cursor(0, -1);
    pp_move_cursor(0, 1);
    check("released: chain again", pp_bank[0].sel, 2);
    pp_bank[0].sel = 3; pp_bank[0].cursor = 15;
    pp_move_cursor(0, 1);
    check("D wraps on itself", pp_bank[0].sel, 3); check("D's column 0", pp_bank[0].cursor, 0);

    /* --- #8: the single-page key loops one page under HOLD; releasing it mid-loop goes on along the chain --- */
    fresh();
    pp_page_next(0); pp_page_next(0);
    for (int i = 0; i < 3; i++) { page(0, i)->len = 2; page(0, i)->notes[0] = 1u << i; }
    for (int i = 0; i < 3; i++) pp_bank[0].chain[i] = (uint8_t)i;
    pp_bank[0].chain_len = 3;
    pp_set_hold(0, 1);
    ticks(0, 1);
    check("chain starts at A", pp_bank[0].play_page, 0);
    pp_set_solo(0, 1);
    for (int lap = 0; lap < 3; lap++) { ticks(0, 48); check("solo loops A", pp_bank[0].play_page, 0); check("A's note", last_key, 0); }
    ticks(0, 24);                                           /* mid-loop */
    pp_set_solo(0, 0);
    check("finishes A first", pp_bank[0].play_page, 0);
    ticks(0, 24);
    check("then B", pp_bank[0].play_page, 1); check("B's note", last_key, 1);
    check("grid follows", pp_bank[0].sel, 1);
    pp_set_hold(0, 0); ticks(0, 1);
    /* starting with the key held loops the page shown, then the chain goes on after it */
    pp_bank[0].sel = 1;
    pp_set_solo(0, 1);
    pp_set_hold(0, 1);
    ticks(0, 1);
    check("starts on the page shown", pp_bank[0].play_page, 1);
    ticks(0, 96);
    check("still B", pp_bank[0].play_page, 1);
    pp_set_solo(0, 0);
    ticks(0, 48);
    check("then C", pp_bank[0].play_page, 2);
    pp_set_hold(0, 0);
    ticks(0, 1);
    check("#8: every note released", offs, ons);

    /* --- Shift + a note already there erases just that note --- */
    fresh();
    pp_note_entry(0, 3);
    check("entry moves the cursor", pp_bank[0].cursor, 1);
    pp_move_cursor(0, -1);
    pp_note_entry(0, 5);
    check("stacks", page(0, 0)->notes[0], 1 << 3 | 1 << 5);
    pp_move_cursor(0, -1);
    pp_note_entry(0, 3);
    check("erases only that note", page(0, 0)->notes[0], 1 << 5);
    check("erasing keeps the cursor", pp_bank[0].cursor, 0);
    pp_note_entry(0, 3);
    check("enters it again", page(0, 0)->notes[0], 1 << 5 | 1 << 3);
    /* while playing: at the playhead; the erased note still gets its note-off */
    fresh();
    page(0, 0)->notes[0] = 1 << 4;
    pp_set_hold(0, 1);
    ticks(0, 1);
    check("note sounding", sounding, 1);
    pp_note_entry(0, 4);
    check("erased at the playhead", page(0, 0)->notes[0], 0);
    pp_note_entry(0, 6);
    check("added at the playhead", page(0, 0)->notes[0], 1 << 6);
    ticks(0, 24);
    check("erased note released", sounding, 0);
    { int n = ons; ticks(0, 24 * 15 - 1); check("nothing else plays", ons, n);
      ticks(0, 1); check("added note plays next time", last_key, 6); }
    pp_set_hold(0, 0);
    ticks(0, 1);
    check("erase: every note released", offs, ons);

    /* --- saving: everything comes back, in the mode it belongs to --- */
    { static char text[PP_STORE_MAX + 8], again[PP_STORE_MAX + 8], bad[PP_STORE_MAX + 8];
      static pp_bank_t saved;
      fresh();
      fill_all(0);
      int n = pp_serialize(0, text, sizeof text);
      check("largest save fits", n, PP_STORE_MAX);
      check("too small a buffer refuses", pp_serialize(0, text, n - 1), 0);
      n = pp_serialize(0, text, sizeof text);
      memcpy(&saved, &pp_bank[0], sizeof saved);
      check("reads in another mode", pp_parse(1, text, n), 0);
      check("pages back", pp_bank[1].count, PP_PAGES);
      check("selected page back", pp_bank[1].sel, 5);
      check("pitch arrow back", pp_bank[1].pitch_key, 19);
      check("chain back", pp_bank[1].chain_len, PP_CHAIN_MAX);
      for (int i = 0; i < PP_CHAIN_MAX; i++) check("chain entry", pp_bank[1].chain[i], saved.chain[i]);
      check("same text again", pp_serialize(1, again, sizeof again), n);
      check("identical", memcmp(text, again, n), 0);
      for (int i = 0; i < PP_PAGES; i++) {
          check("start", pp_bank[1].pages[i].start, saved.pages[i].start);
          check("len", pp_bank[1].pages[i].len, saved.pages[i].len);
          check("swing", pp_bank[1].pages[i].swing, saved.pages[i].swing);
          check("dir", pp_bank[1].pages[i].dir, saved.pages[i].dir);
          for (int c = 0; c < PP_COLS; c++) check("notes", (int)pp_bank[1].pages[i].notes[c], (int)saved.pages[i].notes[c]);
      }
      /* a smaller collection leaves nothing of the larger one behind */
      fresh();
      pp_note_entry(0, 2);
      int small = pp_serialize(0, again, sizeof again);
      fill_all(1);
      check("small reads", pp_parse(1, again, small), 0);
      check("one page", pp_bank[1].count, 1);
      check("no chain", pp_bank[1].chain_len, 0);
      check("its note", (int)pp_bank[1].pages[0].notes[0], 1 << 2);
      check("later columns empty", (int)pp_bank[1].pages[0].notes[1], 0);
      check("later pages empty", (int)pp_bank[1].pages[1].notes[0], 0);
      pp_page_next(1);
      check("next page is blank", (int)pp_bank[1].pages[1].len, PP_COLS);
      /* bad saves are refused and change nothing */
      fresh();
      fill_all(0);
      n = pp_serialize(0, text, sizeof text);
      fresh();
      pp_note_entry(0, 9);
      memcpy(&saved, &pp_bank[0], sizeof saved);
      #define REFUSED(what) do { check(what, pp_parse(0, bad, n), -1); check(what " keeps the work", memcmp(&saved, &pp_bank[0], sizeof saved), 0); } while (0)
      check("null", pp_parse(0, 0, 0), -1);
      check("empty", pp_parse(0, text, 0), -1);
      check("truncated", pp_parse(0, text, n - 1), -1);
      check("too long", pp_parse(0, text, n + 1), -1);
      memcpy(bad, text, n); bad[0] = 'X'; fix_sum(bad, n); REFUSED("wrong format");
      memcpy(bad, text, n); bad[2] = '2'; fix_sum(bad, n); REFUSED("newer version");
      memcpy(bad, text, n); bad[n - 1] = bad[n - 1] == '0' ? '1' : '0'; REFUSED("bad check");
      memcpy(bad, text, n); bad[40] = bad[40] == '0' ? '1' : '0'; REFUSED("one changed character");
      memcpy(bad, text, n); bad[3] = '1'; bad[4] = '1'; fix_sum(bad, n); REFUSED("too many pages");
      memcpy(bad, text, n); bad[5] = '1'; bad[6] = '0'; fix_sum(bad, n); REFUSED("selected page doesn't exist");
      memcpy(bad, text, n); bad[7] = '1'; bad[8] = '8'; fix_sum(bad, n); REFUSED("bad pitch");
      memcpy(bad, text, n); bad[11] = '1'; bad[12] = '0'; fix_sum(bad, n); REFUSED("chain names a missing page");
      { int page0 = 11 + PP_CHAIN_MAX * 2;
        memcpy(bad, text, n); bad[page0 + 2] = '0'; bad[page0 + 3] = '0'; fix_sum(bad, n); REFUSED("zero length");
        memcpy(bad, text, n); bad[page0 + 2] = '1'; bad[page0 + 3] = '1'; fix_sum(bad, n); REFUSED("section past the end");
        memcpy(bad, text, n); bad[page0 + 4] = '0'; bad[page0 + 5] = '0'; fix_sum(bad, n); REFUSED("bad swing");
        memcpy(bad, text, n); bad[page0 + 6] = '0'; bad[page0 + 7] = '3'; fix_sum(bad, n); REFUSED("bad direction");
        memcpy(bad, text, n); bad[page0 + 8] = 'g'; fix_sum(bad, n); REFUSED("not hex"); }
      /* loading while playing stops cleanly */
      fresh();
      page(0, 0)->notes[0] = 1;
      n = pp_serialize(0, text, sizeof text);
      pp_set_hold(0, 1);
      ticks(0, 1);
      check("playing before load", sounding, 1);
      check("loads", pp_parse(0, text, n), 0);
      check("load silences", sounding, 0);
      check("load stops", pp_running(0), 0);
      ticks(0, 50);
      check("nothing plays after load", sounding, 0);
      check("load: every note released", offs, ons);
      /* new */
      fill_all(0);
      pp_clear(0);
      check("new: one page", pp_bank[0].count, 1);
      check("new: no chain", pp_bank[0].chain_len, 0);
      check("new: blank", (int)pp_bank[0].pages[0].notes[0], 0);
      check("new: settings", pp_bank[0].pages[0].len, PP_COLS);
      /* the views cycle and leave playback alone */
      fresh();
      pp_view_toggle(0); check("chain view", pp_bank[0].view, PP_VIEW_CHAIN);
      pp_view_toggle(0); check("saved view", pp_bank[0].view, PP_VIEW_SAVED);
      pp_view_toggle(0); check("back to the grid", pp_bank[0].view, PP_VIEW_GRID);
    }

    /* --- page copy, cut, paste and undo --- */
    {
        pp_bank_t *b = &pp_bank[1];
        fresh();
        check("paste with nothing copied", pp_page_paste(1), PP_OP_EMPTY);
        check("undo with nothing done", pp_undo(1), PP_OP_EMPTY);
        pp_page_next(1); pp_page_next(1); pp_page_next(1);       /* A B C D */
        for (int i = 0; i < 4; i++) { page(1, i)->notes[0] = 1u << i; page(1, i)->swing = (uint8_t)(30 + i); }
        page(1, 2)->len = 8; page(1, 2)->dir = PP_DIR_REVERSE;
        /* copy C, paste over A: everything about the page comes along, and C keeps its own */
        b->sel = 2;
        check("copy", pp_page_copy(1), PP_OP_OK);
        b->sel = 0;
        check("paste", pp_page_paste(1), PP_OP_OK);
        check("pasted notes", (int)page(1, 0)->notes[0], 1 << 2);
        check("pasted swing", page(1, 0)->swing, 32);
        check("pasted length", page(1, 0)->len, 8);
        check("pasted direction", page(1, 0)->dir, PP_DIR_REVERSE);
        check("C still there", (int)page(1, 2)->notes[0], 1 << 2);
        check("drum clipboard only", pp_page_paste(0), PP_OP_EMPTY);
        check("undo paste", pp_undo(1), PP_OP_OK);
        check("A back", (int)page(1, 0)->notes[0], 1);
        check("A's swing back", page(1, 0)->swing, 30);
        /* paste as a new page */
        check("paste new", pp_page_paste_new(1), PP_OP_OK);
        check("paste new: count", b->count, 5);
        check("paste new: selected", b->sel, 4);
        check("paste new: notes", (int)page(1, 4)->notes[0], 1 << 2);
        check("undo paste new", pp_undo(1), PP_OP_OK);
        check("undo paste new: count", b->count, 4);
        /* cut D and paste it over B: a move. Pages stay put and the chain, about positions, never changes */
        { static const uint8_t c[] = { 0, 1, 2, 3, 2, 1, 0 }; for (int i = 0; i < 7; i++) b->chain[i] = c[i]; b->chain_len = 7; }
        b->sel = 3;
        check("cut D", pp_page_cut(1), PP_OP_OK);
        check("cut: D blank", pp_page_blank(1, 3), 1);
        check("cut: D's settings reset", page(1, 3)->swing, PP_SWING_NONE);
        check("cut: still four pages", b->count, 4);
        check("cut: chain untouched", b->chain_len, 7);
        check("cut: D still in the chain", b->chain[3], 3);
        b->sel = 1;
        check("paste over B", pp_page_paste(1), PP_OP_OK);
        check("B has D's pattern", (int)page(1, 1)->notes[0], 1 << 3);
        check("B has D's swing", page(1, 1)->swing, 33);
        check("C unmoved", (int)page(1, 2)->notes[0], 1 << 2);
        check("chain still untouched", b->chain[1] << 12 | b->chain[2] << 8 | b->chain[3] << 4 | b->chain[5], 0x1231);
        check("paste again", pp_page_paste(1), PP_OP_OK);
        /* undo: the second paste, the first, then the cut */
        check("undo paste again", pp_undo(1), PP_OP_OK);
        check("undo paste", pp_undo(1), PP_OP_OK);
        check("B's own pattern back", (int)page(1, 1)->notes[0], 1 << 1);
        check("undo cut", pp_undo(1), PP_OP_OK);
        check("D's pattern back", (int)page(1, 3)->notes[0], 1 << 3);
        check("undo cut leaves the clipboard", pp_page_paste(1), PP_OP_OK);
        check("undo that", pp_undo(1), PP_OP_OK);
        /* every step so far has been undone */
        check("nothing older", pp_undo(1), PP_OP_EMPTY);
        /* three levels: the fourth step forgets the first */
        for (int i = 0; i < 4; i++) pp_page_paste(1);
        check("3 undos", pp_undo(1) | pp_undo(1) | pp_undo(1), PP_OP_OK);
        check("no 4th", pp_undo(1), PP_OP_EMPTY);
        /* undoing a paste-new the chain has used since: the page stays, blank, so the chain still points at a page */
        b->chain_len = 0;
        pp_page_paste_new(1);
        check("new page E", b->sel, 4);
        b->chain[0] = 4; b->chain_len = 1;
        pp_undo(1);
        check("E stays for the chain", b->count, 5);
        check("E blank", pp_page_blank(1, 4), 1);
        check("chain kept", b->chain[0], 4);
        /* the only page: cut leaves it blank */
        fresh();
        page(1, 0)->notes[3] = 5; page(1, 0)->len = 4;
        pp_page_cut(1);
        check("only page: one page", b->count, 1);
        check("only page: blank", pp_page_blank(1, 0), 1);
        check("only page: settings", page(1, 0)->len, PP_COLS);
        check("only page: undo", pp_undo(1), PP_OP_OK);
        check("only page: notes back", (int)page(1, 0)->notes[3], 5);
        /* nothing while playing */
        page(1, 0)->notes[0] = 1;
        pp_page_copy(1);
        pp_set_hold(1, 1);
        check("no copy while playing", pp_page_copy(1), PP_OP_RUNNING);
        check("no paste while playing", pp_page_paste(1), PP_OP_RUNNING);
        check("no paste new while playing", pp_page_paste_new(1), PP_OP_RUNNING);
        check("no cut while playing", pp_page_cut(1), PP_OP_RUNNING);
        check("no undo while playing", pp_undo(1), PP_OP_RUNNING);
        pp_set_hold(1, 0);
        /* full: no new page */
        while (b->count < PP_PAGES) pp_page_next(1);
        check("paste new when full", pp_page_paste_new(1), PP_OP_FULL);
        /* a clear or a load forgets the clipboard and undo; a save does not */
        { char t[PP_STORE_MAX + 1]; check("save", pp_serialize(1, t, sizeof t) > 0, 1); }
        check("save keeps the clipboard", pp_page_paste(1), PP_OP_OK);
        pp_clear(1);
        check("clear forgets the clipboard", pp_page_paste(1), PP_OP_EMPTY);
        check("clear forgets undo", pp_undo(1), PP_OP_EMPTY);
    }

    /* --- ticks before the player is set up --- */
    memset(pp_bank, 0, sizeof pp_bank);
    pp_tick(0);
    pp_rewind(0);
    pp_silence_all(0);

    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures != 0;
}
