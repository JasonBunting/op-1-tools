/* Bifrost knob map (bf_knobs.h). How a knob's message becomes what a hand does:
 *   1. the message's CC is one of the learned knobs (knobmap.h): knob 1-4 wants its encoder's page 1, 5-8 page 2;
 *   2. its value becomes detents (a position's change, or a relative knob's step);
 *   3. on a plugin engine's main page (screen 0x14, where page 2 exists), if the encoder shows the other page, Bifrost
 *      clicks it (encoder push 0x3B + k through op1_key_push) and waits for the page flag (op1_eng_page) to follow;
 *   4. then it turns the encoder: ticks into R246's encoder accumulators, as Hundekraft's MIDI encoder CCs do
 *      (kennel/kn_ccmap.c: 0x01DD5250, 2 ticks a detent, then KeyHandler::signal 0x019C2F04).
 * A click that doesn't flip the page within 150 ms means that knob has no page 2 on this sound (its push may be a
 * latch, freeze for one): Bifrost clicks again at once, which undoes a latch, and turns page 1 from then on, until
 * the sound changes. Anywhere but an engine's main page both rows simply turn the encoders.
 * Learning (the LEARN KNOBS row): each new CC is the next knob, its first values tell absolute from relative
 * (knobmap.h km_detect); after the eighth, or when the row is switched off, the knobs are saved and the row goes off.
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#pragma GCC optimize("no-tree-loop-distribute-patterns")          /* the firmware has no memcpy / memset */
#include "op1_plugin.h"
#include "op1_feature.h"
#include "op1_keyfilter.h"
#include "knobmap.h"
#include "bf_knobs.h"

#define FEAT "bifrost"
#define CYC_MS 400000u

/* ---- R246 ---- */
#define ENC_TICKS  ((volatile int32_t *)0x01DD5250)            /* encoder accumulators, blue..orange */
#define KEY_WAKE   ((void (*)(void))0x019C2F04)                /* KeyHandler::signal */
#define CUR_SCREEN (*(volatile int *)0x018B65C8)
#define ENG_SCREEN 0x14                                        /* a plugin engine's main page (iter's screen) */
#define SYNTH_SLOT (*(volatile int *)0xFF8016D4)
#define ENG_WORD4(s) (((volatile int16_t *)(0xFF8013C4 + (s) * 16))[4])   /* the sound's engine selector */
#define ENC_PUSH   0x3B                                        /* key codes of the encoder pushes, blue..orange */

static inline uint32_t cycles(void) { uint32_t c; __asm__ volatile("%0 = CYCLES;" : "=d"(c)); return c; }
static inline uint32_t irq_off(void) { uint32_t m; __asm__ volatile("cli %0;" : "=d"(m)); return m; }
static inline void irq_on(uint32_t m) { __asm__ volatile("sti %0;" : : "d"(m)); }

static km_map map;
static km_learn learn;
static uint8_t map_on, learning;
static char msg[40];

/* per encoder: detents waiting for their page */
static int16_t pend[4];
static int8_t want[4] = { -1, -1, -1, -1 };
static uint8_t clicked[4];
static uint32_t click_t[4];
static int32_t no_page2[4] = { -1, -1, -1, -1 };              /* the sound (slot << 16 | selector) without one */

static int32_t sound_key(void) { const int s = SYNTH_SLOT & 7; return s << 16 | (uint16_t)ENG_WORD4(s); }

static void turn(int k, int detents)
{
    if (!detents) return;
    const uint32_t m = irq_off();
    ENC_TICKS[k] += detents * 2;
    irq_on(m);
    KEY_WAKE();
}

static void click(int k) { op1_key_push(ENC_PUSH + k, 1); op1_key_push(ENC_PUSH + k, 0); }

static void done(int k) { pend[k] = 0; want[k] = -1; clicked[k] = 0; }

void bfk_service(void)
{
    for (int k = 0; k < 4; k++) {
        if (want[k] < 0) continue;
        if (CUR_SCREEN != ENG_SCREEN) { turn(k, pend[k]); done(k); continue; }   /* no pages here */
        const int page = op1_eng_page[k] != 0;
        if (page == want[k] || (want[k] == 1 && no_page2[k] == sound_key())) {
            turn(k, pend[k]); done(k); continue;
        }
        if (!clicked[k]) { click(k); clicked[k] = 1; click_t[k] = cycles(); continue; }
        if (cycles() - click_t[k] > 150 * CYC_MS) {             /* the click flipped nothing: no page 2 here */
            click(k);                                           /* undo it (a latch goes back) */
            no_page2[k] = sound_key();
            turn(k, pend[k]); done(k);
        }
    }
}

static void put_num(char *p, const char *a, int n, const char *b)
{
    while (*a) *p++ = *a++;
    if (n >= 10) *p++ = (char)('0' + n / 10);
    *p++ = (char)('0' + n % 10);
    while (*b) *p++ = *b++;
    *p = 0;
}

static void save(void)
{
    static const char *const key[KM_KNOBS] = { "k0", "k1", "k2", "k3", "k4", "k5", "k6", "k7" };
    op1_setting_set(FEAT, "kn", map.n);
    for (int i = 0; i < map.n; i++) op1_setting_set(FEAT, key[i], km_pack(&map, i));
}

static void learn_end(void)
{
    learning = 0;
    if (learn.n) {
        km_learn_finish(&learn, &map);
        save();
        put_num(msg, "", learn.n, learn.n == 1 ? " knob learned" : " knobs learned");
        op1_toast(msg);
    }
    op1_setting_set(FEAT, "learn", 0);                          /* the row switches itself off */
}

static const char *const where[KM_KNOBS] = { "top row, left", "top row, 2nd", "top row, 3rd", "top row, right",
                                             "bottom row, left", "bottom row, 2nd", "bottom row, 3rd", "bottom row, right" };

int bfk_take(const uint8_t *pk)
{
    if ((pk[0] & 15) != 0xB || (pk[1] & 0xF0) != 0xB0) return 0;   /* control changes only */
    const int ch = pk[1] & 15, cc = pk[2] & 127, v = pk[3] & 127;
    if (learning) {
        const int i = km_learn_cc(&learn, ch, cc, v);
        if (km_learn_full(&learn)) learn_end();
        else if (i >= 0 && i + 1 < KM_KNOBS) {
            char *p = msg;
            put_num(p, "knob ", i + 1, " ok, now ");
            while (*p) p++;
            for (const char *s = where[i + 1]; *s; s++) *p++ = *s;
            *p = 0;
            op1_toast(msg);
        }
        return 1;                                               /* while learning, knobs move nothing */
    }
    if (!map_on) return 0;
    const int i = km_find(&map, ch, cc);
    if (i < 0) return 0;
    const int d = km_delta(&map, i, v), k = i & 3, w = i >= 4;
    if (want[k] >= 0 && want[k] != w) done(k);                  /* turned the other row before this one landed */
    want[k] = (int8_t)w;
    pend[k] = (int16_t)(pend[k] + d);
    return 1;
}

void bfk_settings(void)
{
    map_on = op1_setting_value(FEAT, "knobs") != 0;
    const int l = op1_setting_value(FEAT, "learn") != 0;
    if (l && !learning) {
        learning = 1;
        learn.n = 0;
        op1_toast("learn: turn the top row's left knob");
    } else if (!l && learning) learn_end();
}

void bfk_load(void)
{
    static const char *const key[KM_KNOBS] = { "k0", "k1", "k2", "k3", "k4", "k5", "k6", "k7" };
    int n = op1_setting_get(FEAT, "kn", 0);
    map.n = 0;
    for (int i = 0; i < n && i < KM_KNOBS; i++) {
        const int32_t v = op1_setting_get(FEAT, key[i], -1);
        if (v < 0) break;
        km_unpack(&map, i, v);
        map.n = (uint8_t)(i + 1);
    }
    learning = 0;
    if (op1_setting_value(FEAT, "learn")) op1_setting_set(FEAT, "learn", 0);   /* never left on across a restart */
    bfk_settings();
}
