/* Pattern+: a stock-Pattern-style grid sequencer with multiple pages, as a Hundekraft 3.2.3 sequencer type.
 * This build has the grid, pages, chains and saving (patternplus/docs/pattern-plus-poc-spec.md).
 * Screen layout and colors measured from a capture of stock Pattern on the device; the artwork is drawn here.
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#pragma GCC optimize("no-tree-loop-distribute-patterns")      /* the firmware has no memcpy / memset */
#include "op1_feature_seq.h"
#include "op1_synth.h"
#include "seq/seq_text.h"
#include "pp_player.h"
#include "op1_feature.h"

#define PP_NAME       "patternplus"
#define PP_SCREEN_ID  0x2D                                /* finger's slot, shared by every kennel sequencer */
#define PP_MODE()     (*(volatile int *)0x01DE4C40 == 1)
#define PP_SHIFT()    (*(volatile uint8_t *)0x01DD7F78)
#define PP_SCREEN()   (*(volatile int *)0x018B65C8)
#define PP_SLOT(m)    (*(volatile int *)((m) ? 0xFF8016D8 : 0xFF8016D4) & 7)
#define PP_OCTAVE(m)  (((const int *)((m) ? 0x01DFE500 : 0x01DFE4E0))[PP_SLOT(m)])
/* SeqIds::name entry, which the core patches to jump to kn_seq.c's eu_seq_name: the mode's sequencer name for type 3 */
#define PP_SEQ_NAME   ((const char *(*)(int))0x01A422EC)
#define PP_KEY_LEFT   0x24
#define PP_KEY_RIGHT  0x25
#define PP_KEY_HELP   0x00
enum { PP_BLUE, PP_GREEN, PP_WHITE, PP_ORANGE };
#define PP_FLASH_MS   1000

static volatile uint8_t pp_arrow_taken[2];                /* its release is ours too, wherever the press went on to */
static int32_t pp_acc[2][4];
static void pp_changed(void);
static volatile int16_t pp_swing_flash, pp_section_flash, pp_dir_flash, pp_full_flash;   /* ms left to show brightly */

int pp_out_on(int mode, int key, int transpose, int vel)
{
    /* As stock R246 Pattern (0x01A08D24): a synth note is key + transpose; a drum's key picks the sound only and its
     * pitch is just the transposition. Since kennel 3.2.3 (2026-11-07) op1_seq_note_on takes a drum's pitch as
     * note - (key + 0x35) - drum octave * 12, so both modes send the key's own note; older cores play drums flat. */
    return op1_seq_note_on(mode, key + 0x35 + PP_OCTAVE(mode) * 12 + transpose, key, vel);
}
void pp_out_off(int id) { op1_seq_note_off(id); }

/* ---- player (vtable layout: op1_feature_seq.h) ---- */
static int pp_nop(void *self) { (void)self; return 0; }
static int pp_pkey_down(void *self, int key)
{
    (void)self;
    if (key < 0 || key > 23) return 0;
    int m = PP_MODE(), shown = PP_SCREEN() == PP_SCREEN_ID;
    if (shown) {                                          /* as stock: the key sounds and the arrow moves */
        if (PP_SHIFT() && !pp_bank[m].view) { pp_note_entry(m, key); pp_changed(); }   /* never onto a grid out of sight */
        else pp_set_pitch(m, key);
        return 0;
    }
    if (PP_SHIFT()) return 0;                             /* elsewhere Shift + key stays stock */
    if (!pp_has_notes(m)) return 0;                       /* as stock: with nothing to play, the key just sounds */
    pp_key_down(m, key);                                  /* synth, drum, tape, mixer: play, transposed */
    return 1;
}
static void pp_pkey_up(void *self, int key)               /* both modes: the mode can change while a key is held */
{
    (void)self;
    pp_key_up(0, key);
    pp_key_up(1, key);
}
/* ---- saving: the current work follows every change into op1.db (sq_store, a background thread, as stock's sequencers
 * save). Named collections are files, one per slot, written and read only on the main loop. ---- */
#define PP_SLOTS     8
#define PP_NAME_MAX  24
#define PP_DIR       "/yaffs2/user/patternplus"
/* An auto-save that can't be read is never written over with an empty grid (Hundekraft's plugin guide): its text is
 * kept and written back unchanged until the main loop has copied it to unreadable-s.txt / unreadable-d.txt. Then saving
 * resumes. Loading a slot or clearing on purpose also lets it go. */
static char pp_kept[2][PP_STORE_MAX];
static volatile int16_t pp_kept_len[2];
static volatile uint8_t pp_kept_tried[2];
static int pp_store_ser(int pos, char *out, int n)        /* saver thread */
{
    int len = pp_kept_len[pos & 1], o = 0;
    if (!len) return pp_serialize(pos, out, n);
    for (int i = 0; i < len && o + 2 <= n; i++) {          /* a quote is doubled, as SQL wants inside '...' */
        char c = pp_kept[pos & 1][i];
        if (c == '\'') out[o++] = c;
        out[o++] = c;
    }
    return o;
}
static int pp_store_parse(int pos, const char *s, int len)
{
    int r = pp_parse(pos, s, len);
    if (r) {
        for (int i = 0; i < len && i < PP_STORE_MAX; i++) pp_kept[pos & 1][i] = s[i];
        pp_kept_len[pos & 1] = (int16_t)(len < PP_STORE_MAX ? len : PP_STORE_MAX);
        op1_toast("pattern+ save unreadable, copy kept");
    }
    return r;
}
static char pp_sql[2048];                                 /* the INSERT around the longest save text */
static sq_store_t pp_store = { .name = "patternplus", .ser = pp_store_ser, .parse = pp_store_parse,
                               .sql = pp_sql, .cap = sizeof pp_sql, .maxlen = PP_STORE_MAX + 1 };
static void pp_changed(void) { sq_store_dirty(&pp_store); }
static void pp_let_go(int mode) { pp_kept_len[mode & 1] = 0; }   /* the user replaced the work on purpose */

enum { PP_JOB_NONE, PP_JOB_SCAN, PP_JOB_LOAD };
enum { PP_ASK_NONE, PP_ASK_LOAD, PP_ASK_NEW, PP_ASK_PASTE };
static char pp_slot_name[2][PP_SLOTS][PP_NAME_MAX + 1];   /* upper case; empty: nothing in the slot */
static char pp_file_buf[PP_NAME_MAX + 1 + PP_STORE_MAX];
static int8_t pp_loaded[2] = { -1, -1 };
static volatile uint8_t pp_slot_at[2], pp_job, pp_job_mode, pp_job_slot, pp_ask, pp_ask_mode, pp_save_mode, pp_save_slot;
static const char *volatile pp_msg;
static volatile int16_t pp_msg_ms;
#define PP_MSG_MS 3000
static void pp_say(const char *m) { pp_msg = m; pp_msg_ms = PP_MSG_MS; }
/* A question waits for green (yes) or orange (no) and belongs to the mode that asked it. */
static void pp_ask_for(int mode, int ask, const char *m) { pp_ask = (uint8_t)ask; pp_ask_mode = (uint8_t)(mode & 1); pp_say(m); }
/* "<before><page letter><after>": one buffer, as only one message shows at a time */
static char pp_msg_buf[40];
static const char *pp_page_msg(const char *before, int page, const char *after)
{
    char *e = sq_cat(pp_msg_buf, before);
    *e++ = (char)('A' + page);
    *e = 0;
    sq_cat(e, after);
    return pp_msg_buf;
}

static void pp_slot_path(char *p, int mode, int slot)
{
    p = sq_cat(p, PP_DIR "/");
    *p++ = mode ? 'd' : 's';
    *p++ = (char)('1' + slot);
    sq_cat(p, ".txt");
}
static void pp_set_name(int mode, int slot, const char *s, int n)
{
    char *d = pp_slot_name[mode][slot];
    int i = 0;
    for (; i < n && i < PP_NAME_MAX; i++) d[i] = s[i] >= 'a' && s[i] <= 'z' ? (char)(s[i] - 32) : s[i];
    d[i] = 0;
}
/* the slot's name line from its file's start; -1 if the file isn't a slot's */
static int pp_name_len(const char *buf, int n)
{
    for (int i = 0; i < n && i <= PP_NAME_MAX; i++) if (buf[i] == '\n') return i ? i : -1;
    return -1;
}
static void pp_do_scan(int mode)
{
    char path[48];
    for (int s = 0; s < PP_SLOTS; s++) {
        pp_slot_path(path, mode, s);
        int fd = op1_file_open(path, OP1_FILE_READ), n = -1;
        if (fd >= 0) { n = op1_file_read(fd, pp_file_buf, PP_NAME_MAX + 1); op1_file_close(fd); }
        int len = n > 0 ? pp_name_len(pp_file_buf, n) : -1;
        pp_set_name(mode, s, pp_file_buf, len > 0 ? len : 0);
    }
}
static void pp_do_load(int mode, int slot)
{
    char path[48];
    pp_slot_path(path, mode, slot);
    int fd = op1_file_open(path, OP1_FILE_READ), n = -1;
    if (fd >= 0) { n = op1_file_read(fd, pp_file_buf, sizeof pp_file_buf); op1_file_close(fd); }
    int len = n > 0 ? pp_name_len(pp_file_buf, n) : -1;
    if (len <= 0 || pp_parse(mode, pp_file_buf + len + 1, n - len - 1)) { pp_say("NOT LOADED  FILE UNREADABLE"); return; }
    pp_let_go(mode);
    pp_loaded[mode] = (int8_t)slot;
    pp_say("LOADED");
    pp_changed();
}
static void pp_save_done(const char *text, int ok)        /* main loop */
{
    if (!ok) return;
    int mode = pp_save_mode, slot = pp_save_slot, n = 0;
    char path[48];
    if (!text || !text[0]) text = "UNTITLED";
    while (text[n] && n < PP_NAME_MAX) { pp_file_buf[n] = text[n]; n++; }
    pp_file_buf[n] = '\n';
    int total = n + 1 + pp_serialize(mode, pp_file_buf + n + 1, PP_STORE_MAX);
    op1_file_mkdir(PP_DIR);
    pp_slot_path(path, mode, slot);
    int fd = op1_file_open(path, OP1_FILE_WRITE | OP1_FILE_CREATE | OP1_FILE_TRUNC), wrote = -1;
    if (fd >= 0) { wrote = op1_file_write(fd, pp_file_buf, total); op1_file_close(fd); }
    if (wrote != total) { pp_say("SAVE FAILED"); pp_job_mode = (uint8_t)mode; pp_job = PP_JOB_SCAN; return; }
    pp_set_name(mode, slot, text, n);
    pp_loaded[mode] = (int8_t)slot;
    pp_say("SAVED");
}
static op1_text_entry_t pp_entry = { "COLLECTION NAME", 0, 0, PP_NAME_MAX, 0, pp_save_done, 0 };
static void pp_keep_unreadable(void)                      /* main loop: once per mode and boot */
{
    for (int m = 0; m < 2; m++) {
        int len = pp_kept_len[m];
        if (!len || pp_kept_tried[m]) continue;
        pp_kept_tried[m] = 1;
        op1_file_mkdir(PP_DIR);
        int fd = op1_file_open(m ? PP_DIR "/unreadable-d.txt" : PP_DIR "/unreadable-s.txt",
                               OP1_FILE_WRITE | OP1_FILE_CREATE | OP1_FILE_TRUNC), wrote = -1;
        if (fd >= 0) { wrote = op1_file_write(fd, pp_kept[m], len); op1_file_close(fd); }
        if (wrote != len) continue;                       /* not copied: the database keeps it until a load or clear */
        pp_let_go(m);
        pp_changed();
    }
}
static void pp_event(int id, int32_t arg)
{
    (void)arg;
    if (id != OP1_EV_SERVICE) return;
    pp_keep_unreadable();
    if (!pp_job) return;
    int job = pp_job;
    if (job == PP_JOB_SCAN) pp_do_scan(pp_job_mode);
    if (job == PP_JOB_LOAD) pp_do_load(pp_job_mode, pp_job_slot);
    pp_job = PP_JOB_NONE;
}
static void pp_slot_move(int mode, int delta)
{
    int at = pp_slot_at[mode] + delta;
    pp_slot_at[mode] = (uint8_t)(at < 0 ? 0 : at >= PP_SLOTS ? PP_SLOTS - 1 : at);
    pp_ask = PP_ASK_NONE;
    pp_msg_ms = 0;
}
static void pp_saved_press(int mode, int knob, int shift)
{
    int slot = pp_slot_at[mode];
    if (pp_ask != PP_ASK_NONE) {                          /* a question is up: green push confirms, orange cancels */
        int ask = pp_ask;
        if (knob == PP_ORANGE) { pp_ask = PP_ASK_NONE; pp_say("CANCELLED"); }
        if (knob != PP_GREEN) return;
        pp_ask = PP_ASK_NONE;
        pp_msg_ms = 0;
        if (ask == PP_ASK_NEW) {
            pp_clear(mode);
            pp_let_go(mode);
            pp_loaded[mode] = -1;
            pp_say("NEW");
            pp_changed();
        } else if (!pp_job) { pp_job_mode = (uint8_t)mode; pp_job_slot = (uint8_t)slot; pp_job = PP_JOB_LOAD; }
        return;
    }
    if (shift) {
        if (knob != PP_GREEN) return;
        pp_ask_for(mode, PP_ASK_NEW, "CLEAR ALL? GREEN YES ORANGE NO");
    } else if (knob == PP_GREEN) {
        if (!pp_slot_name[mode][slot][0]) { pp_say("EMPTY"); return; }
        pp_ask_for(mode, PP_ASK_LOAD, "REPLACE WORK? GREEN YES ORANGE NO");
    } else if (knob == PP_WHITE) {
        pp_ask = PP_ASK_NONE;
        pp_save_mode = (uint8_t)mode;
        pp_save_slot = (uint8_t)slot;
        pp_entry.initial = pp_slot_name[mode][slot][0] ? pp_slot_name[mode][slot] : 0;
        if (op1_text_entry(&pp_entry)) pp_say("SAVE FAILED");
    }
}

static void pp_preset(void *self) { (void)self; pp_rewind(PP_MODE()); }
static void pp_ptick(void *self, int counter) { (void)self; (void)counter; pp_tick(PP_MODE()); }
static void pp_pstop(void *self) { (void)self; pp_silence_all(0); pp_silence_all(1); }
static const uint32_t pp_vt[16] = {
    0, 0,  0, (uint32_t)pp_nop,  0, (uint32_t)pp_pkey_down,  0, (uint32_t)pp_pkey_up,
    0, (uint32_t)pp_preset,  0, (uint32_t)pp_ptick,  0, (uint32_t)pp_nop,  0, (uint32_t)pp_pstop };
static struct { const uint32_t *vt; uint32_t pad[7]; } pp_player = { pp_vt, { 0 } };
static void *pp_player_get(void)
{
    pp_init(0);
    pp_init(1);
    sq_store_load(&pp_store);
    return &pp_player;
}

/* ---- input ---- */
static int pp_is_shown(void)
{
    if (PP_SCREEN() != PP_SCREEN_ID) return 0;
    const char *a = PP_SEQ_NAME(3), *b = PP_NAME;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
/* Help, held on the Pattern+ screen, is the single-page key (#8): the cursor and HOLD stay on one page. Shift + help is
 * left to the core. The release is ours wherever the press went on to, and lets go in both modes. */
static volatile uint8_t pp_help_taken;
static int pp_help(int pressed)
{
    if (pressed) {
        if (!pp_is_shown() || PP_SHIFT()) return 0;
        pp_help_taken = 1;
        pp_set_solo(PP_MODE(), 1);
        return 1;
    }
    if (!pp_help_taken) return 0;
    pp_help_taken = 0;
    pp_set_solo(0, 0);
    pp_set_solo(1, 0);
    return 1;
}
/* The core keeps left / right for the keyboard octave on sequencer screens; this filter runs before it, and stock
 * Pattern moves its cursor with them. */
static int pp_input(const op1_input *in)
{
    if (in->type == OP1_IN_KEY && in->code == PP_KEY_HELP) return pp_help(in->pressed);
    if (in->type != OP1_IN_KEY || (in->code != PP_KEY_LEFT && in->code != PP_KEY_RIGHT)) return 0;
    int i = in->code == PP_KEY_RIGHT;
    if (in->pressed) {
        if (!pp_is_shown()) return 0;
        pp_arrow_taken[i] = 1;
        if (pp_bank[PP_MODE()].view == PP_VIEW_SAVED) pp_slot_move(PP_MODE(), i ? 1 : -1);
        else if (pp_bank[PP_MODE()].view) pp_chain_move(PP_MODE(), i ? 1 : -1);
        else if (PP_SHIFT()) pp_live_move(PP_MODE(), i ? 1 : -1);   /* as stock: frees the cursor from the playhead */
        else pp_move_cursor(PP_MODE(), i ? 1 : -1);
    } else {
        if (!pp_arrow_taken[i]) return 0;
        pp_arrow_taken[i] = 0;
    }
    return 1;
}
/* A page is a named grid; its pattern is what lift copies, scissors cuts (leaving the page blank) and drop pastes
 * (Shift + drop: onto a new last page). The chain is never touched. Grid view and stopped only. Only pasting over a
 * page that holds notes asks first; Shift + orange push undoes cut and paste. */
#define PP_KEY_LIFT     0x0A
#define PP_KEY_DROP     0x0B
#define PP_KEY_SCISSORS 0x0C
static void pp_pasted(int mode)
{
    if (pp_page_paste(mode) != PP_OP_OK) return;
    pp_say(pp_page_msg("PASTED ON ", pp_bank[mode].sel, ""));
    pp_changed();
}
static int pp_key_ev(int mode, int code, int pressed, int shift)
{
    if (code < PP_KEY_LIFT || code > PP_KEY_SCISSORS) return 0;
    mode &= 1;
    const pp_bank_t *b = &pp_bank[mode];
    if (!pressed || b->view != PP_VIEW_GRID) return 1;
    pp_ask = PP_ASK_NONE;                                 /* a new operation replaces any question */
    if (pp_running(mode)) { pp_say("STOP FIRST"); return 1; }
    if (code == PP_KEY_LIFT) {
        pp_page_copy(mode);
        pp_say(pp_page_msg("COPIED ", b->sel, ""));
    } else if (code == PP_KEY_SCISSORS) {
        pp_page_cut(mode);
        pp_say(pp_page_msg("CUT ", b->sel, ""));
        pp_changed();
    } else if (!b->has_clip) {
        pp_say("NOTHING COPIED");
    } else if (shift) {
        if (pp_page_paste_new(mode) == PP_OP_FULL) { pp_full_flash = PP_FLASH_MS; return 1; }
        pp_say(pp_page_msg("PASTED ON NEW ", b->sel, ""));
        pp_changed();
    } else if (pp_page_blank(mode, b->sel)) {
        pp_pasted(mode);
    } else {
        pp_ask_for(mode, PP_ASK_PASTE, pp_page_msg("PASTE OVER ", b->sel, "? GREEN YES ORANGE NO"));
    }
    return 1;
}
static void pp_page_answer(int mode, int knob)            /* the paste question is up */
{
    if (knob == PP_ORANGE) { pp_ask = PP_ASK_NONE; pp_say("CANCELLED"); }
    if (knob != PP_GREEN) return;
    pp_ask = PP_ASK_NONE;
    pp_msg_ms = 0;
    if (pp_running(mode)) { pp_say("STOP FIRST"); return; }
    pp_pasted(mode);
}
static void pp_press_ev(int mode, int knob, int shift)
{
    int chain_view = pp_bank[mode & 1].view;
    if (pp_ask && pp_ask_mode != (mode & 1)) { pp_ask = PP_ASK_NONE; pp_msg_ms = 0; }   /* asked in the other mode */
    if (shift && knob == PP_WHITE) {
        pp_view_toggle(mode);
        pp_ask = PP_ASK_NONE;
        pp_msg_ms = 0;
        if (pp_bank[mode & 1].view == PP_VIEW_SAVED && !pp_job) { pp_job_mode = (uint8_t)(mode & 1); pp_job = PP_JOB_SCAN; }
        return;
    }
    if (chain_view == PP_VIEW_SAVED) { pp_saved_press(mode & 1, knob, shift); return; }
    if (pp_ask == PP_ASK_PASTE) { pp_page_answer(mode & 1, knob); return; }
    if (shift) {
        if (knob == PP_ORANGE) {                          /* playing: follow the chain again; stopped: undo */
            if (pp_running(mode)) pp_resume_follow(mode);
            else if (!chain_view) pp_say(pp_undo(mode) == PP_OP_OK ? "UNDONE" : "NOTHING TO UNDO");
        }
        if (chain_view) { pp_changed(); return; }
        if (knob == PP_BLUE) pp_page_prev(mode);
        if (knob == PP_GREEN && pp_page_next(mode) < 0) pp_full_flash = PP_FLASH_MS;
    } else if (chain_view) {
        if (knob == PP_GREEN && pp_chain_insert(mode) < 0) pp_full_flash = PP_FLASH_MS;
        if (knob == PP_WHITE) pp_chain_remove(mode);
    }
    pp_changed();
}
static void pp_turn_ev(int mode, int knob, int delta, int shift)
{
    int32_t *acc = &pp_acc[mode & 1][knob & 3];
    if ((*acc > 0 && delta < 0) || (*acc < 0 && delta > 0)) *acc = 0;
    *acc += delta;
    int st = *acc / 1024;
    if (!st) return;
    *acc -= st * 1024;
    if (pp_bank[mode & 1].view) {                         /* the grid's own settings don't change out of sight */
        if (knob == PP_ORANGE && !shift) pp_set_hold(mode, st > 0);
        if (pp_bank[mode & 1].view == PP_VIEW_SAVED) {
            if (knob == PP_BLUE && !shift) pp_slot_move(mode & 1, st);
            return;
        }
        if (knob == PP_BLUE && !shift) pp_chain_move(mode, st);
        if (knob == PP_GREEN && !shift) pp_chain_pick(mode, st);
        pp_changed();
        return;
    }
    switch (knob) {
    case PP_BLUE:
        if (shift) pp_erase_move(mode, st); else pp_move_cursor(mode, st);
        break;
    case PP_GREEN:
        if (shift) pp_rotate(mode, st);
        else { pp_add_swing(mode, st); pp_swing_flash = PP_FLASH_MS; }
        break;
    case PP_WHITE:
        if (shift) pp_move_section(mode, st); else pp_add_length(mode, st);
        pp_section_flash = PP_FLASH_MS;
        break;
    case PP_ORANGE:
        if (shift) { pp_add_dir(mode, st); pp_dir_flash = PP_FLASH_MS; }
        else pp_set_hold(mode, st > 0);
        break;
    }
    pp_changed();
}
static int pp_shift_wants(int key) { return key >= 0 && key <= 23; }
static void pp_midi_start(void) { pp_rewind(0); pp_rewind(1); }

/* ---- screen: coordinates are the device's 320 x 160, Q10 ---- */
#define PP_GRID_X   54016                                 /* 52.75 */
#define PP_GRID_DX  12096                                 /* 11.8125 per column */
#define PP_GRID_Y   14080                                 /* 13.75 */
#define PP_GRID_HDY 4544                                  /* 4.4375 per half row: black keys sit between the lines */
#define PP_ROWS     14
#define PP_LINE     0x400
static const uint8_t pp_c_minor[4] = { 62, 52, 88, 255 }, pp_c_quarter[4] = { 121, 113, 171, 255 };
static const uint8_t pp_c_horiz[4] = { 84, 70, 110, 255 }, pp_c_dim[4] = { 24, 20, 30, 255 };
static const uint8_t pp_c_dot[4] = { 0, 240, 130, 255 }, pp_c_off[4] = { 128, 128, 140, 255 };
static const uint8_t pp_c_cursor[4] = { 180, 210, 255, 255 }, pp_c_white[4] = { 255, 255, 255, 255 };
static const uint8_t pp_c_swing_dim[4] = { 15, 60, 50, 255 }, pp_c_swing[4] = { 0, 223, 160, 255 };
static const uint8_t pp_c_red_dim[4] = { 69, 39, 52, 255 }, pp_c_red[4] = { 245, 38, 94, 255 };
static const uint8_t pp_c_grey[4] = { 155, 158, 161, 255 };
/* half rows above the F that starts each keyboard octave */
static const uint8_t pp_key_half[12] = { 0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12 };

static int32_t pp_col_x(int col) { return PP_GRID_X + col * PP_GRID_DX; }
static int32_t pp_key_y(int key) { return PP_GRID_Y + (26 - ((key >= 12 ? 14 : 0) + pp_key_half[key >= 12 ? key - 12 : key])) * PP_GRID_HDY; }
static void pp_flash_down(volatile int16_t *f, int dt) { if (*f > 0) *f = (int16_t)(*f > dt ? *f - dt : 0); }

static void pp_draw_grid(void *r, const pp_page_t *p)
{
    int32_t top = PP_GRID_Y, bottom = PP_GRID_Y + (PP_ROWS - 1) * 2 * PP_GRID_HDY;
    int s = p->start, e = s + p->len;
    for (int row = 0; row < PP_ROWS; row++) {
        int32_t y = PP_GRID_Y + row * 2 * PP_GRID_HDY;
        if (s) sq_line(r, pp_col_x(0), y, pp_col_x(s), y, PP_LINE, pp_c_dim);
        if (e < PP_COLS) sq_line(r, pp_col_x(e), y, pp_col_x(PP_COLS), y, PP_LINE, pp_c_dim);
        sq_line(r, pp_col_x(s), y, pp_col_x(e), y, PP_LINE, pp_c_horiz);
    }
    for (int c = 0; c <= PP_COLS; c++) {
        const uint8_t *col = c < s || c > e ? pp_c_dim : c == 0 || c == PP_COLS ? pp_c_horiz : c % 4 == 0 ? pp_c_quarter : pp_c_minor;
        sq_line(r, pp_col_x(c), top, pp_col_x(c), bottom, PP_LINE, col);
    }
}
static void pp_draw_notes(void *r, const pp_page_t *p)
{
    int s = p->start, e = s + p->len;
    for (int c = 0; c < PP_COLS; c++) {
        uint32_t notes = p->notes[c];
        if (!notes) continue;
        const uint8_t *col = c >= s && c < e ? pp_c_dot : pp_c_off;
        for (int k = 0; notes && k < PP_KEYS; k++, notes >>= 1)
            if (notes & 1) sq_dot(r, pp_col_x(c), pp_key_y(k), OP1_Q10(5), col);
    }
}
static void pp_draw_arrow(void *r, int32_t x0, int32_t x1, int32_t y, int32_t head, const uint8_t *c)
{
    int32_t back = x1 > x0 ? x1 - head : x1 + head;
    sq_line(r, x0, y, x1, y, PP_LINE, c);
    sq_line(r, back, y - head, x1, y, PP_LINE, c);
    sq_line(r, back, y + head, x1, y, PP_LINE, c);
}
static void pp_draw_side(void *r, const pp_bank_t *b, const pp_page_t *p, int running)
{
    char t[8], *e;
    e = sq_num(t, p->swing, 1);
    sq_cat(e, "%");
    sq_text(r, OP1_Q10(252), OP1_Q10(16), OP1_Q10(2.7), t, 0x500,
            pp_swing_flash || p->swing != PP_SWING_NONE ? pp_c_swing : pp_c_swing_dim);   /* bright: changing, or any swing at all */

    const uint8_t *hold = b->hold ? pp_c_red : pp_c_red_dim;
    int32_t hx0 = OP1_Q10(257), hx1 = OP1_Q10(295), hy0 = OP1_Q10(49), hy1 = OP1_Q10(67);
    sq_line(r, hx0, hy0, hx1, hy0, PP_LINE, hold);
    sq_line(r, hx1, hy0, hx1, hy1, PP_LINE, hold);
    sq_line(r, hx1, hy1, hx0, hy1, PP_LINE, hold);
    sq_line(r, hx0, hy1, hx0, hy0, PP_LINE, hold);
    sq_text(r, OP1_Q10(263), OP1_Q10(54), OP1_Q10(1.3), "HOLD", 0x400, hold);

    const uint8_t *dir = running || pp_dir_flash ? pp_c_red : pp_c_red_dim;
    int32_t ax0 = OP1_Q10(261), ax1 = OP1_Q10(290), ay = OP1_Q10(87);
    if (p->dir == PP_DIR_REVERSE) pp_draw_arrow(r, ax1, ax0, ay, OP1_Q10(9), dir);
    else if (p->dir == PP_DIR_FORWARD) pp_draw_arrow(r, ax0, ax1, ay, OP1_Q10(9), dir);
    else {
        pp_draw_arrow(r, ax1, ax0, ay - OP1_Q10(5), OP1_Q10(5), dir);
        pp_draw_arrow(r, ax0, ax1, ay + OP1_Q10(5), OP1_Q10(5), dir);
    }

    if (pp_full_flash) {
        sq_text(r, OP1_Q10(258), OP1_Q10(108), OP1_Q10(1.5), "FULL", 0x400, pp_c_red);
        return;
    }
    t[0] = (char)('A' + b->sel);                          /* the page, and the last page there is */
    t[1] = 0;
    sq_text(r, OP1_Q10(258), OP1_Q10(104), OP1_Q10(3), t, 0x600, pp_c_white);
    t[0] = '/';
    t[1] = (char)('A' + b->count - 1);
    t[2] = 0;
    sq_text(r, OP1_Q10(276), OP1_Q10(113), OP1_Q10(1.5), t, 0x400, pp_c_grey);
    if (b->chain_len && b->solo) sq_text(r, OP1_Q10(258), OP1_Q10(128), OP1_Q10(1.3), "PAGE", 0x400, pp_c_white);
    else if (b->chain_len && !b->follow) sq_text(r, OP1_Q10(258), OP1_Q10(128), OP1_Q10(1.3), "FREE", 0x400, pp_c_grey);
}
/* The chain as A > B > B > C: eight to a row. The occurrence playing is boxed, so repeats of a page stay apart. */
#define PP_CELL_X(i) OP1_Q10(24 + ((i) % 8) * 36)
#define PP_CELL_Y(i) OP1_Q10(34 + ((i) / 8) * 26)
static void pp_draw_chain(void *r, const pp_bank_t *b, int running)
{
    char t[2] = { 0, 0 };
    int len = b->chain_len, cur = b->chain_cursor;
    sq_text(r, OP1_Q10(24), OP1_Q10(10), OP1_Q10(1.5), "CHAIN", 0x400, pp_c_grey);
    sq_text(r, OP1_Q10(263), OP1_Q10(10), OP1_Q10(1.3), "HOLD", 0x400, b->hold ? pp_c_red : pp_c_red_dim);
    int playing = running && len ? b->chain_pos : -1;
    for (int i = 0; i < len; i++) {
        int32_t x = PP_CELL_X(i), y = PP_CELL_Y(i);
        t[0] = (char)('A' + b->chain[i]);
        const uint8_t *col = i == playing ? pp_c_dot : pp_page_blank((int)(b - pp_bank), b->chain[i]) ? pp_c_red
                           : i == cur ? pp_c_white : pp_c_grey;    /* red: the page has no pattern on it */
        sq_text(r, x, y, OP1_Q10(2.2), t, 0x500, col);
        if (i == playing) {
            int32_t x0 = x - OP1_Q10(4), x1 = x + OP1_Q10(16), y0 = y - OP1_Q10(4), y1 = y + OP1_Q10(19);
            sq_line(r, x0, y0, x1, y0, PP_LINE, pp_c_dot);
            sq_line(r, x1, y0, x1, y1, PP_LINE, pp_c_dot);
            sq_line(r, x1, y1, x0, y1, PP_LINE, pp_c_dot);
            sq_line(r, x0, y1, x0, y0, PP_LINE, pp_c_dot);
        }
        if (i + 1 < len && (i + 1) % 8) {                 /* the > between entries */
            int32_t cx = x + OP1_Q10(21), cy = y + OP1_Q10(8);
            sq_line(r, cx, cy - OP1_Q10(4), cx + OP1_Q10(5), cy, PP_LINE, pp_c_grey);
            sq_line(r, cx + OP1_Q10(5), cy, cx, cy + OP1_Q10(4), PP_LINE, pp_c_grey);
        }
    }
    if (cur >= len) {                                     /* the free slot, showing the page Green will add */
        t[0] = (char)('A' + b->chain_pick);
        sq_text(r, PP_CELL_X(cur), PP_CELL_Y(cur), OP1_Q10(2.2), t, 0x500, pp_c_quarter);
    }
    sq_line(r, PP_CELL_X(cur) - OP1_Q10(2), PP_CELL_Y(cur) + OP1_Q10(22), PP_CELL_X(cur) + OP1_Q10(14), PP_CELL_Y(cur) + OP1_Q10(22), 0x600, pp_c_cursor);
    if (pp_full_flash) sq_text(r, OP1_Q10(24), OP1_Q10(142), OP1_Q10(1.3), "FULL", 0x400, pp_c_red);
    else if (!len) sq_text(r, OP1_Q10(24), OP1_Q10(142), OP1_Q10(1.3), "NO CHAIN  PAGE LOOPS", 0x400, pp_c_grey);
}
/* The message line under the grid or the slots: a question stays until answered; anything else fades. 1 if drawn */
static int pp_draw_msg(void *r)
{
    const char *m = pp_msg;
    if (!m) return 0;
    if (pp_ask != PP_ASK_NONE) sq_text(r, OP1_Q10(24), OP1_Q10(144), OP1_Q10(1.1), m, 0x300, pp_c_white);
    else if (pp_msg_ms > 0) sq_text(r, OP1_Q10(24), OP1_Q10(144), OP1_Q10(1.3), m, 0x400, pp_c_white);
    else return 0;
    return 1;
}
static void pp_draw_saved(void *r, int m, const pp_bank_t *b)
{
    char t[PP_NAME_MAX + 3];
    sq_text(r, OP1_Q10(24), OP1_Q10(10), OP1_Q10(1.5), "SAVED", 0x400, pp_c_grey);
    sq_text(r, OP1_Q10(263), OP1_Q10(10), OP1_Q10(1.3), "HOLD", 0x400, b->hold ? pp_c_red : pp_c_red_dim);
    for (int s = 0; s < PP_SLOTS; s++) {
        int32_t y = OP1_Q10(28 + s * 14);
        const char *name = pp_slot_name[m][s];
        char *e = t;
        *e++ = (char)('1' + s);
        *e++ = ' ';
        sq_cat(e, name[0] ? name : "EMPTY");
        int at = s == pp_slot_at[m];
        sq_text(r, OP1_Q10(24), y, OP1_Q10(1.3), t, 0x400,
                s == pp_loaded[m] ? pp_c_dot : at ? pp_c_white : name[0] ? pp_c_grey : pp_c_minor);
        if (at) pp_draw_arrow(r, OP1_Q10(8), OP1_Q10(19), y + OP1_Q10(5), OP1_Q10(4), pp_c_cursor);
    }
    if (!pp_draw_msg(r)) sq_text(r, OP1_Q10(24), OP1_Q10(144), OP1_Q10(1.1), "PUSH GREEN LOAD   PUSH WHITE SAVE", 0x300, pp_c_grey);
}
static void pp_scr_init(void *scr) { (void)scr; }
static void pp_scr_update(void *scr, int dt)
{
    op1_screen_draw(scr, dt);
    void *r = op1_raster();
    int m = PP_MODE();
    pp_init(m);
    const pp_bank_t *b = &pp_bank[m];
    const pp_page_t *p = &b->pages[b->sel];
    int running = pp_running(m);
    int32_t top = PP_GRID_Y, bottom = PP_GRID_Y + (PP_ROWS - 1) * 2 * PP_GRID_HDY;

    pp_flash_down(&pp_swing_flash, dt);
    pp_flash_down(&pp_section_flash, dt);
    pp_flash_down(&pp_dir_flash, dt);
    pp_flash_down(&pp_full_flash, dt);
    if (pp_msg_ms > 0 && pp_ask == PP_ASK_NONE) pp_msg_ms = (int16_t)(pp_msg_ms > dt ? pp_msg_ms - dt : 0);  /* a question waits for its answer */
    if (b->view == PP_VIEW_SAVED) { pp_draw_saved(r, m, b); return; }
    if (b->view) { pp_draw_chain(r, b, running); return; }

    pp_draw_grid(r, p);
    if (running || pp_section_flash) {                    /* the section's ends */
        sq_line(r, pp_col_x(p->start), top, pp_col_x(p->start), bottom, PP_LINE, pp_c_grey);
        sq_line(r, pp_col_x(p->start + p->len), top, pp_col_x(p->start + p->len), bottom, PP_LINE, pp_c_white);
    }
    int playhead = running && b->play_page == b->sel;
    if (!playhead || b->live) sq_line(r, pp_col_x(b->cursor), top, pp_col_x(b->cursor), bottom, PP_LINE, pp_c_cursor);
    if (playhead) sq_line(r, pp_col_x(b->pos), top, pp_col_x(b->pos), bottom, PP_LINE, pp_c_white);   /* live edit: both */
    pp_draw_notes(r, p);
    pp_draw_arrow(r, OP1_Q10(30), OP1_Q10(45), pp_key_y(b->pitch_key), OP1_Q10(5), pp_c_white);
    pp_draw_side(r, b, p, running);
    pp_draw_msg(r);
}
const op1_screen_t SCREEN_patternplus = { pp_scr_init, pp_scr_update };

static const op1_seq_type_t pp_type = {
    .name = PP_NAME, .player = pp_player_get, .svg = "patternplus.svg", .screen = &SCREEN_patternplus,
    .key = pp_key_ev, .press = pp_press_ev, .turn = pp_turn_ev, .shift_wants = pp_shift_wants,
    .midi_start = pp_midi_start };
const op1_feature_t FEATURE_patternplus = {
    .api = OP1_FEATURE_API, .priority = 1000, .name = PP_NAME, .seq = &pp_type, .input = pp_input, .event = pp_event };
