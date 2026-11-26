/* Bifrost: a class-compliant USB MIDI keyboard or controller plugged straight into the original OP-1 through a
 * mini-USB OTG adapter, playing it as MIDI from a computer does. A Hundekraft feature: it runs on the main loop
 * (OP1_EV_SERVICE) whatever sound is current, and one settings row switches it off.
 *
 *   idle      device mode, as stock. Every 5 s with nothing powering the port it tries an unpowered host session.
 *   probe     a VBUS error (or the A side) within 500 ms means an OTG adapter is in; anything else, back to idle.
 *   start     the powered start (bf_usb.c); host mode within 2 s, or give up.
 *   waitdev   host with the 5 V on, until a device's speed bit (or the connect interrupt) shows.
 *   run       a MIDI device: its packets go into R246's receive ring each pass.
 *   other     a device that isn't MIDI (or wouldn't set up): wait for it to be unplugged.
 * Losing the session or the A side (a keyboard's inrush, the adapter pulled) runs the powered start again; when that
 * doesn't give host mode, the adapter is gone and the port goes back to the computer. Disk mode and the settings row
 * end host mode at once.
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#pragma GCC optimize("no-tree-loop-distribute-patterns")          /* the firmware has no memcpy / memset */
#include "op1_feature.h"
#include "bf_usb.h"

#define FEAT "bifrost"
#define CYC_MS 400000ull

enum { ST_OFF, ST_IDLE, ST_PROBE, ST_START, ST_WAITDEV, ST_RUN, ST_OTHER, ST_BACKOFF };

static uint8_t st = ST_OFF, on_setting = 1, disk, restarts;
static uint64_t t_state, t_boot;
static bfu_dev dev;
static char msg[48];

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
static uint64_t ms_since(uint64_t t) { return (now() - t) / CYC_MS; }
static void go(int s) { st = (uint8_t)s; t_state = now(); }

static char *put(char *p, const char *s, char *end) { while (*s && p < end) *p++ = *s++; *p = 0; return p; }
static void toast2(const char *a, const char *b)
{
    char *p = put(msg, a, msg + sizeof msg - 1);
    put(p, b, msg + sizeof msg - 1);
    op1_toast(msg);
}
static const char *dev_name(void) { return dev.name[0] ? dev.name : "usb device"; }

static int active(void) { return st >= ST_PROBE && st <= ST_OTHER; }
static void stop(int next) { if (active()) bfu_leave(); go(next); }

static void restart(void)                                        /* lost host: the powered start again */
{
    if (++restarts > 3) { stop(ST_IDLE); return; }
    bfu_powered_start();
    go(ST_START);
}

static void service(void)
{
    if (st == ST_OFF) return;
    if (st == ST_IDLE || st == ST_BACKOFF) {
        if (disk || ms_since(t_boot) < 5000 || ms_since(t_state) < (st == ST_IDLE ? 5000u : 30000u)) return;
        if (bfu_devctl() & (BFU_VBUS | BFU_SESSION)) { go(ST_IDLE); return; }   /* a computer, or a session: leave it */
        restarts = 0;
        bfu_enter();
        go(ST_PROBE);
        return;
    }
    bfu_keep_charger_off();                                      /* a session is on: R246 would start charging */
    const uint16_t dc = bfu_devctl(), iu = bfu_intr();
    const int host = (dc & (BFU_SESSION | BFU_BDEVICE)) == BFU_SESSION;
    switch (st) {
    case ST_PROBE:
        if ((iu & BFU_INT_VBUSERR) || (host && ms_since(t_state) > 40)) {
            if (dc & BFU_VBUS) { stop(ST_IDLE); break; }       /* power from outside: not an adapter */
            if (!bfu_powered_start()) { op1_toast("bifrost: no 5v on the usb port"); stop(ST_BACKOFF); break; }
            go(ST_START);
        } else if (ms_since(t_state) > 500) stop(ST_IDLE);     /* no adapter */
        break;
    case ST_START:
        if (host && (dc & BFU_HOSTMODE)) { go(ST_WAITDEV); break; }
        if (ms_since(t_state) > 2000) stop(ST_IDLE);           /* no host mode: the adapter is gone */
        break;
    case ST_WAITDEV:
        if (!host) { restart(); break; }
        if ((iu & BFU_INT_CONN) || (dc & (BFU_FSDEV | BFU_LSDEV))) {
            dev.name[0] = 0;
            const int r = bfu_enumerate(&dev);
            if (r == BFU_OK) { restarts = 0; toast2(dev_name(), " connected"); go(ST_RUN); }
            else if (!(bfu_devctl() & BFU_SESSION)) restart();  /* the session dropped while setting it up */
            else { toast2(dev_name(), r == -BFU_E_NOMIDI ? ": not a midi device" : ": usb setup failed"); go(ST_OTHER); }
        }
        break;
    case ST_RUN:
    case ST_OTHER:
        if ((iu & BFU_INT_DISCON) || (host && !(dc & (BFU_FSDEV | BFU_LSDEV)))) {   /* unplugged */
            if (st == ST_RUN) toast2(dev_name(), " unplugged");
            go(ST_WAITDEV);
            break;
        }
        if (!host) { restart(); break; }
        if (st == ST_RUN) bfu_poll_midi();
        break;
    }
}

static void read_settings(void)
{
    on_setting = op1_setting_value(FEAT, "on") != 0;
    if (!on_setting) stop(ST_OFF);
    else if (st == ST_OFF) go(ST_IDLE);
}

static void event(int id, int32_t arg)
{
    switch (id) {
    case OP1_EV_BOOT_DONE: t_boot = now(); read_settings(); break;
    case OP1_EV_SETTING: read_settings(); break;
    case OP1_EV_DISK:                                            /* arg 1: disk mode starts (the computer gets the port) */
        disk = arg != 0;
        if (disk && active()) stop(ST_IDLE);
        break;
    case OP1_EV_SERVICE: service(); break;
    }
}

const op1_feature_t FEATURE_bifrost = { .api = OP1_FEATURE_API, .priority = 1000, .name = FEAT, .event = event };
