/* Pattern+: pages, grid editing and playback, free of firmware addresses so a host test can run it.
 * Modelled on stock Pattern as observed on the device: 16 columns; notes go on lines 0..15 and line 16 only closes the
 * bar. Each column holds any set of the 24 keyboard keys.
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#ifndef PP_PLAYER_H
#define PP_PLAYER_H
#include <stdint.h>

#define PP_COLS       16
#define PP_KEYS       24
#define PP_PAGES      8
#define PP_CHAIN_MAX  32
#define PP_STORE_MAX  911                   /* the longest save text: 8 pages and a full chain */
enum { PP_VIEW_GRID, PP_VIEW_CHAIN, PP_VIEW_SAVED, PP_VIEWS };
#define PP_PAIR_TICKS 48                    /* two 1/16 steps at 96 ticks per beat: swing divides this */
#define PP_VELOCITY   127                   /* stock R246 Pattern plays every note at 0x7F */
#define PP_SWING_MIN  20                    /* stock's range, read from the device */
#define PP_SWING_MAX  80
#define PP_SWING_NONE 50
#define PP_ROOT_KEY   7                     /* the lower C: holding it plays the page at its written pitch */
enum { PP_DIR_FORWARD, PP_DIR_CYCLE, PP_DIR_REVERSE, PP_DIRS };

typedef struct {
    volatile uint32_t notes[PP_COLS];       /* bit per key */
    volatile uint8_t start, len, swing, dir;
} pp_page_t;

#define PP_UNDO 3                           /* page copy / paste / delete steps that can be taken back, per mode */
typedef struct {                            /* what a page operation can change */
    pp_page_t pages[PP_PAGES];
    uint8_t chain[PP_CHAIN_MAX];
    uint8_t count, sel, chain_len;
} pp_snap_t;

typedef struct {
    pp_page_t pages[PP_PAGES];
    volatile uint8_t init, count, sel, cursor, pitch_key, hold;
    volatile uint32_t down;                 /* transposing keys held, bit per key */
    volatile int8_t transpose;
    volatile uint8_t play_page, pos, back, started, elapsed, step_len, sounding;
    int16_t ids[PP_KEYS];                   /* note id + 1, per key */
    volatile uint8_t chain[PP_CHAIN_MAX];   /* page indexes; repeats share one page. Empty: the selected page loops */
    volatile uint8_t chain_len, chain_cursor, chain_pick, view, follow;
    volatile int8_t chain_pos;              /* the occurrence playing; -1 just after removing the first while it played */
    pp_page_t clip;                         /* the copied page; not saved, so it lasts until a load, a clear or power off */
    uint8_t has_clip, undo_n;
    pp_snap_t undo[PP_UNDO];                /* oldest first */
} pp_bank_t;

extern pp_bank_t pp_bank[2];                /* 0 synth, 1 drum; .bss is zero until pp_init */

/* provided by the glue (or the host test) */
int pp_out_on(int mode, int key, int transpose, int vel);   /* -> note id, or < 0 */
void pp_out_off(int id);

void pp_init(int mode);
int pp_running(int mode);
int pp_has_notes(int mode);                 /* would playing produce any note */
int pp_edit_col(int mode);                  /* where a note goes: the playhead while it runs on this page, else the cursor */

/* editing (KeyHandler thread) */
void pp_note_entry(int mode, int key);      /* at the playhead, or at the cursor, which then moves on. A note already there is erased instead */
void pp_set_pitch(int mode, int key);       /* the keyboard-position arrow */
void pp_move_cursor(int mode, int delta);   /* stopped only */
void pp_erase_move(int mode, int delta);    /* stopped only: clears each column the cursor leaves */
void pp_add_swing(int mode, int delta);
void pp_rotate(int mode, int delta);        /* within the section, wrapping */
void pp_add_length(int mode, int delta);
void pp_move_section(int mode, int delta);
void pp_add_dir(int mode, int delta);
void pp_page_prev(int mode);
int pp_page_next(int mode);                 /* 0 moved, 1 created a blank page, -1 at the limit (nothing changed) */

/* page operations: stopped only. Paste, paste-new and delete can be undone; a new one past PP_UNDO forgets the oldest */
enum { PP_OP_OK = 0, PP_OP_RUNNING = -1, PP_OP_EMPTY = -2, PP_OP_FULL = -3 };
int pp_page_copy(int mode);                 /* the selected page, all its settings too, to this mode's clipboard */
int pp_page_paste(int mode);                /* the clipboard over the selected page. EMPTY: nothing copied */
int pp_page_paste_new(int mode);            /* the clipboard as a new last page, which is then selected */
int pp_page_delete(int mode);               /* later pages move down a letter and the chain follows them; entries of
                                               the deleted page go. The only page is blanked instead */
int pp_undo(int mode);                      /* EMPTY: nothing to undo */

/* chain */
void pp_view_toggle(int mode);              /* grid, chain, saved, grid: playback is not touched */
void pp_chain_move(int mode, int delta);    /* over the entries and the free slot after them */
void pp_chain_pick(int mode, int delta);    /* the page to add; on an entry, also changes that entry */
int pp_chain_insert(int mode);              /* the picked page, before the cursor. 0, or -1 when full */
void pp_chain_remove(int mode);             /* the entry at the cursor */
void pp_resume_follow(int mode);            /* the grid goes back to showing the playing page */

/* saving (pp_store.c): text that can be stored anywhere; nothing here touches the database or files */
void pp_clear(int mode);                    /* stop this mode and start over with one empty page; forgets the clipboard
                                               and undo too (loading clears first) */
int pp_serialize(int mode, char *out, int cap);     /* -> length, 0 if it doesn't fit; no terminator */
int pp_parse(int mode, const char *s, int len);     /* 0 replaced this mode's work (stopped); -1 refused, nothing changed */

/* playback */
void pp_key_down(int mode, int key);        /* start, or change the transposition */
void pp_key_up(int mode, int key);          /* safe for a key this mode does not hold */
void pp_set_hold(int mode, int on);
void pp_tick(int mode);                     /* mode: the one receiving ticks */
void pp_rewind(int mode);                   /* silence; a running part starts again from its first step */
void pp_silence_all(int mode);              /* as stock's stop: silence, release keys, HOLD off, no transposition */
#endif
