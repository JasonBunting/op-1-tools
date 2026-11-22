/* pushguard: Hundekraft's trigger guard (synth_sdk/plugins/trigguard/trigguard.c, 3.3.1) with the four encoder pushes
 * guarded too, as a prototype of a fix to send upstream. Everything for the note keys is trigguard's, unchanged.
 *
 * The problem (research/pushprobe): a worn encoder push switch chatters. One press arrived as down, up 0.2 ms later,
 * down 0.3 ms after that, then the real up 88 ms later. The core flips an engine's page 2 on every encoder down
 * (dispatcher.c, op1_eng_page ^= 1), so a chatter flips the page twice (it seems not to switch) or flips it back on
 * release (it seems to need holding). trigguard guards only the 24 note keys; encoder pushes (0x3B..0x3E) pass.
 *
 * The change: the encoder pushes are four more guarded keys, always with the release held back (a page flips on the
 * down, so holding the up costs nothing), with their own switch ("knobs", KNOB GUARD) and window ("kms", KNOB MS,
 * 1..60, default 15: the chatter is under a millisecond, a deliberate second push is over 100 ms). WINDOWS ALL stays
 * for the note keys only. Settings "on", "ms", "windows", "release" are trigguard's.
 *
 * Only one key filter can run: install this instead of trigguard, not beside it (whichever loads last would win).
 *
 * trigguard's rule, for each guarded key, window W:
 *   HOLD RELEASE on: a release is held back for W. A press of the same key inside W drops both (the key just stays
 *   down); with no press the held release is delivered when W has passed. A second press with no release between (a
 *   stuck-scan glitch) is dropped.
 *   HOLD RELEASE off (note keys only): releases pass at once; a press within W of the key's last release is dropped,
 *   and so is the release that follows it.
 * Delivery of a held release: on the KeyHandler thread, in time order, before the event being handled, through
 * `deliver`. When no key follows, the main loop (OP1_EV_SERVICE) pushes a private wake event (code 0x3F, pressed
 * TG_TOKEN) into R246's queue (op1_key_push), and the guard delivers the due releases when it arrives.
 * Time: CYCLES2:CYCLES (400 MHz), 64 bits.
 * trigguard: Copyright (C) 2026 Hundekraft. Changes: Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1
 * firmware permission. */
#pragma GCC optimize("no-tree-loop-distribute-patterns")          /* the firmware has no memcpy / memset */
#include "op1_feature.h"
#include "op1_keyfilter.h"

#define CYC_MS     400000u                                       /* core cycles per ms (400 MHz) */
#define TG_MS_DEFAULT 50
#define TG_MS_MIN  10
#define TG_MS_MAX  300
#define KG_MS_DEFAULT 15                                         /* encoder pushes */
#define KG_MS_MIN  1
#define KG_MS_MAX  60
#define TG_TOKEN_CODE 0x3F                                       /* the main loop's wake event: 0x3F | 0xA6 << 8 */
#define TG_TOKEN   0xA6                                          /* (kennel's MIDI param key is 0xA5) */
#define NOTES      24
#define KEYS       28                                            /* 24 note keys, then the four encoder pushes */
#define FEAT       "pushguard"

#if defined(OP1_HOST) || defined(PG_HOST)
uint64_t tg_host_cycles;                                         /* test/host_test.c sets the clock */
static uint64_t now(void) { return tg_host_cycles; }
#else
static uint64_t now(void)
{
    uint32_t lo, hi, hi2;
    do {                                                         /* CYCLES2 changes when CYCLES wraps */
        __asm__ volatile("%0 = CYCLES2;" : "=d"(hi));
        __asm__ volatile("%0 = CYCLES;" : "=d"(lo));
        __asm__ volatile("%0 = CYCLES2;" : "=d"(hi2));
    } while (hi != hi2);
    return (uint64_t)hi << 32 | lo;
}
#endif

/* code -> guarded key index + 1 (0: not guarded): the note keys from key_is_note_key, then the encoder pushes */
static const uint8_t key_idx[0x40] = {
    [0x0D] = 1, [0x0E] = 2, [0x0F] = 3,
    [0x16] = 4, [0x17] = 5, [0x18] = 6, [0x19] = 7, [0x1A] = 8, [0x1B] = 9, [0x1C] = 10, [0x1D] = 11, [0x1E] = 12,
    [0x1F] = 13, [0x20] = 14,
    [0x27] = 15, [0x28] = 16, [0x29] = 17, [0x2A] = 18,
    [0x30] = 19, [0x31] = 20, [0x32] = 21, [0x33] = 22, [0x34] = 23, [0x35] = 24,
    [0x3B] = 25, [0x3C] = 26, [0x3D] = 27, [0x3E] = 28 };
static uint8_t key_code[KEYS];                                   /* index -> code */

/* settings (written on the main loop, read on the KeyHandler) */
static volatile uint8_t tg_on = 1, tg_rel = 1, tg_all, kg_on = 1;
static volatile uint16_t tg_ms = TG_MS_DEFAULT, kg_ms = KG_MS_DEFAULT;

/* per guarded key (KeyHandler thread) */
static uint8_t down[KEYS];                                       /* consumers saw the press and not yet its release */
static uint8_t held[KEYS];                                       /* a release is held back until due[] */
static uint8_t swallow[KEYS];                                    /* drop the next release (its press was dropped) */
static uint64_t due[KEYS], last_up[KEYS];                        /* last_up: the last release consumers saw */
static uint64_t any_up;                                          /* WINDOWS ALL: when the last note-key release came */
static uint8_t any_up_set;

/* for the main loop */
static volatile uint8_t tg_pending;                              /* a held release waits */
static volatile uint32_t tg_due32;                               /* the earliest due time, low 32 bits of the clock */
static volatile uint8_t tg_token;                                /* a wake event is in the queue */
volatile uint32_t tg_dropped;                                    /* note-key events dropped since boot */
volatile uint32_t kg_dropped;                                    /* encoder-push events dropped since boot */

static int is_knob(int i) { return i >= NOTES; }
static int guard_on(int i) { return is_knob(i) ? kg_on : tg_on; }
static int hold_rel(int i) { return is_knob(i) ? 1 : tg_rel; }
static uint64_t window(int i) { return (uint64_t)(is_knob(i) ? kg_ms : tg_ms) * CYC_MS; }
static void dropped(int i, int n) { if (is_knob(i)) kg_dropped += (uint32_t)n; else tg_dropped += (uint32_t)n; }

static void publish(void)                                        /* the earliest held release for the main loop */
{
    int any = 0;
    uint64_t m = 0;
    for (int i = 0; i < KEYS; i++) if (held[i] && (!any || due[i] < m)) { m = due[i]; any = 1; }
    tg_due32 = (uint32_t)m;
    tg_pending = (uint8_t)any;
}

/* due held releases (all of a key's when its guard or its release hold is off), in time order */
static void flush(uint64_t t, op1_key_deliver deliver)
{
    if (!tg_pending) return;
    for (;;) {
        int k = -1;
        for (int i = 0; i < KEYS; i++) {
            const int all = !guard_on(i) || !hold_rel(i);
            if (held[i] && (all || due[i] <= t) && (k < 0 || due[i] < due[k])) k = i;
        }
        if (k < 0) break;
        held[k] = 0; down[k] = 0; last_up[k] = t;
        deliver(key_code[k], 0);
    }
    publish();
}

/* the key filter (KeyHandler): every key event */
static void tg_key(int code, int pressed, int marked, op1_key_deliver deliver)
{
    if (!key_code[0]) for (int c = 0; c < 0x40; c++) if (key_idx[c]) key_code[key_idx[c] - 1] = (uint8_t)c;
    if (code == TG_TOKEN_CODE && pressed == TG_TOKEN) {          /* our own wake event: never passed on */
        tg_token = 0;
        flush(now(), deliver);
        return;
    }
    const uint64_t t = now();
    flush(t, deliver);                                           /* releases that came due go first */
    tg_token = 0;                                                /* a wake event lost to a queue reset never blocks the next */
    const int i = code >= 0 && code < 0x40 ? key_idx[code] - 1 : -1;
    if (marked || i < 0) { deliver(code, pressed); return; }
    pressed = pressed != 0;
    if (!pressed && swallow[i]) { swallow[i] = 0; dropped(i, 1); return; }
    if (!guard_on(i)) {
        down[i] = (uint8_t)pressed;
        if (!pressed) last_up[i] = t;
        deliver(code, pressed);
        return;
    }
    if (pressed) {
        if (held[i]) { held[i] = 0; publish(); dropped(i, 2); return; }           /* bounce: the key stays down */
        if (down[i]) { dropped(i, 1); return; }                                   /* a second press, no release */
        if (!hold_rel(i) && last_up[i] && t - last_up[i] < window(i)) {           /* bounce, releases not held */
            swallow[i] = 1; dropped(i, 1); return;
        }
        if (!is_knob(i) && tg_all && any_up_set && t - any_up < window(i)) {      /* ALL: inside the shared window */
            swallow[i] = 1; dropped(i, 1); return;
        }
        down[i] = 1;
        deliver(code, 1);
        return;
    }
    if (held[i]) { dropped(i, 1); return; }                      /* a second release */
    if (!is_knob(i)) { any_up = t; any_up_set = 1; }             /* a real note release: the shared window starts */
    if (down[i] && hold_rel(i)) { held[i] = 1; due[i] = t + window(i); publish(); return; }
    down[i] = 0; last_up[i] = t;                                 /* not held (or its press came before we knew): pass */
    deliver(code, 0);
}

/* main loop: wake the KeyHandler when a held release is due (at once when a guard or the hold went off) */
static void tg_service(void)
{
    if (!tg_pending || tg_token) return;
    if (tg_on && tg_rel && kg_on && (int32_t)((uint32_t)now() - tg_due32) < 0) return;
    tg_token = 1;
    if (!op1_key_push(TG_TOKEN_CODE, TG_TOKEN)) tg_token = 0;   /* queue full: the next pass tries again */
}

static void tg_read_settings(void)
{
    tg_on = op1_setting_value(FEAT, "on") != 0;
    const int32_t ms = op1_setting_value(FEAT, "ms");
    tg_ms = (uint16_t)(ms < TG_MS_MIN ? TG_MS_MIN : ms > TG_MS_MAX ? TG_MS_MAX : ms);
    tg_all = op1_setting_value(FEAT, "windows") == 1;
    tg_rel = op1_setting_value(FEAT, "release") != 0;
    kg_on = op1_setting_value(FEAT, "knobs") != 0;
    const int32_t kms = op1_setting_value(FEAT, "kms");
    kg_ms = (uint16_t)(kms < KG_MS_MIN ? KG_MS_MIN : kms > KG_MS_MAX ? KG_MS_MAX : kms);
}

static void tg_event(int id, int32_t arg)
{
    (void)arg;
    if (id == OP1_EV_BOOT_DONE) {
        tg_read_settings();
        op1_key_filter.fn = tg_key;
    } else if (id == OP1_EV_SETTING)
        tg_read_settings();
    else if (id == OP1_EV_SERVICE)
        tg_service();
}

const op1_feature_t FEATURE_pushguard = { .api = OP1_FEATURE_API, .priority = 1000, .name = FEAT, .event = tg_event };
