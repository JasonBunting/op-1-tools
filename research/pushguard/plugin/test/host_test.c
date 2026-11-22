/* Host test of pushguard.c: the encoder pushes (the recorded chatter, a bounce on release, two deliberate pushes,
 * the guard off, a shorter window), and the note keys unchanged from trigguard. Times in tenths of a ms.
 *   gcc -O2 -Wall -DPG_HOST -I <synth_sdk> -I research/pushguard/plugin research/pushguard/plugin/test/host_test.c \
 *       -o /tmp/pg_test && /tmp/pg_test
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "op1_feature.h"
#include "op1_keyfilter.h"

op1_key_filter_t op1_key_filter;
static int32_t set_on = 1, set_ms = 50, set_windows = 0, set_release = 1, set_knobs = 1, set_kms = 15;
int32_t op1_setting_value(const char *feature, const char *id)
{
    if (strcmp(feature, "pushguard")) return 0;
    if (!strcmp(id, "on")) return set_on;
    if (!strcmp(id, "ms")) return set_ms;
    if (!strcmp(id, "windows")) return set_windows;
    if (!strcmp(id, "release")) return set_release;
    if (!strcmp(id, "knobs")) return set_knobs;
    if (!strcmp(id, "kms")) return set_kms;
    return 0;
}
static int pushed;                                   /* wake events waiting in the "queue" */
int op1_key_push(int code, int down) { (void)code; (void)down; pushed++; return 1; }

#include "../pushguard.c"

#define BLUE 0x3B
#define ORANGE 0x3E
#define NOTE 0x16
static char out[256];
static int nout, failures;
static void deliver(int code, int pressed)
{
    nout += snprintf(out + nout, sizeof out - (size_t)nout, "%s%c%02X", nout ? " " : "", pressed ? 'd' : 'u', code);
}
static void at(uint32_t tenths) { tg_host_cycles = (uint64_t)tenths * (CYC_MS / 10); }
static void ev(uint32_t tenths, int code, int pressed) { at(tenths); tg_key(code, pressed, 0, deliver); }
static void service(uint32_t tenths)                 /* a main-loop pass, then its wake event if it pushed one */
{
    at(tenths);
    pushed = 0;
    tg_event(OP1_EV_SERVICE, 0);
    if (pushed) tg_key(TG_TOKEN_CODE, TG_TOKEN, 1, deliver);
}
static void reset(void)
{
    memset(down, 0, sizeof down); memset(held, 0, sizeof held); memset(swallow, 0, sizeof swallow);
    memset(due, 0, sizeof due); memset(last_up, 0, sizeof last_up);
    any_up_set = 0; tg_pending = 0; tg_token = 0;
    nout = 0; out[0] = 0;
    tg_read_settings();
}
static void expect(const char *want, const char *what)
{
    if (strcmp(out, want)) { printf("FAIL: %s\n  want: %s\n  got:  %s\n", what, want, out); failures++; }
}

int main(void)
{
    tg_event(OP1_EV_BOOT_DONE, 0);
    if (op1_key_filter.fn != tg_key) { printf("FAIL: filter not installed\n"); failures++; }

    /* the chatter recorded on orange (research/pushprobe): d, u +0.2 ms, d +0.3 ms, u +88.2 ms */
    reset();
    ev(0, ORANGE, 1); ev(2, ORANGE, 0); ev(5, ORANGE, 1); ev(887, ORANGE, 0);
    service(900); service(1037); service(1038);
    expect("d3E u3E", "orange's recorded chatter is one push");

    /* a bounce on release: d, u at 120 ms, d +0.4 ms, u +0.3 ms */
    reset();
    ev(0, BLUE, 1); ev(1200, BLUE, 0); ev(1204, BLUE, 1); ev(1207, BLUE, 0);
    service(1300); service(1358);
    expect("d3B u3B", "a bounce on release is one push");

    /* two deliberate pushes 150 ms apart both count */
    reset();
    ev(0, BLUE, 1); ev(800, BLUE, 0); ev(2300, BLUE, 1); ev(3100, BLUE, 0); service(3260);
    expect("d3B u3B d3B u3B", "two deliberate pushes are two");

    /* the held release goes out when the window has passed, not before */
    reset();
    ev(0, BLUE, 1); ev(800, BLUE, 0); service(900);
    expect("d3B", "the release is held inside the window");
    service(951);
    expect("d3B u3B", "the release goes out after 15 ms");

    /* knob guard off: the chatter passes as it came */
    set_knobs = 0; reset();
    ev(0, ORANGE, 1); ev(2, ORANGE, 0); ev(5, ORANGE, 1); ev(887, ORANGE, 0);
    expect("d3E u3E d3E u3E", "knob guard off passes everything");
    set_knobs = 1;

    /* a 1 ms window still catches the 0.3 ms chatter */
    set_kms = 1; reset();
    ev(0, ORANGE, 1); ev(2, ORANGE, 0); ev(5, ORANGE, 1); ev(887, ORANGE, 0); service(898);
    expect("d3E u3E", "1 ms window catches sub-millisecond chatter");
    set_kms = 15;

    /* the knobs don't use the note keys' settings: release hold off and WINDOWS ALL leave them alone */
    set_release = 0; set_windows = 1; reset();
    ev(0, NOTE, 1); ev(500, NOTE, 0);                  /* a note release: passes at once, starts the shared window */
    ev(510, BLUE, 1); ev(1200, BLUE, 0); ev(1203, BLUE, 1); ev(1205, BLUE, 0); service(1400);
    expect("d16 u16 d3B u3B", "knobs keep their own rule beside the notes' settings");
    set_release = 1; set_windows = 0;

    /* note keys as trigguard: a bounce inside 50 ms is dropped, the note continues */
    reset();
    ev(0, NOTE, 1); ev(1000, NOTE, 0); ev(1100, NOTE, 1); ev(2000, NOTE, 0); service(2400); service(2501);
    expect("d16 u16", "a note key's bounce is dropped as in trigguard");

    /* other keys pass untouched */
    reset();
    ev(0, 0x06, 1); ev(1, 0x06, 0); ev(2, 0x06, 1);
    expect("d06 u06 d06", "T1 passes untouched");

    if (failures) { printf("%d failed\n", failures); return 1; }
    printf("all passed (dropped: notes %u, knobs %u)\n", tg_dropped, kg_dropped);
    return 0;
}
