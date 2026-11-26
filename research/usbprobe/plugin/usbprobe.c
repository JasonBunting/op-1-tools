/* usbprobe 0.1.5: a throwaway synth engine that tries USB host mode, for a class-compliant MIDI keyboard plugged into
 * the OP-1 through a mini-USB OTG adapter (research/usbhost/README.md). It makes no sound itself: once a keyboard is
 * running, pick any other sound and play it from the keyboard. The work runs in the engine's main-loop service, so it
 * goes on whatever sound is current.
 *   blue push  : start host mode / stop it and give the USB port back to the computer
 *   green push : clear the counters
 * Host mode is polled (the controller's interrupts are off while it runs) and the MIDI goes into R246's USB-MIDI receive
 * ring, so the stock parser plays it as MIDI from a computer. While the OP-1 powers the keyboard, R246's "USB plugged"
 * counter (0x018B62A8) is kept at 0 so the charging code doesn't start.
 * Hundekraft 3.3.2. Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission. */
#include "op1_plugin.h"
#include "op1_synth.h"
#include "op1_feature.h"
#include "usb_desc.h"

/* ---- the BF52x USB controller (Mentor MUSB), 16-bit registers ---- */
#define UR(off)       (*(volatile uint16_t *)(0xFFC03800u + (off)))
#define U_FADDR       UR(0x00)
#define U_POWER       UR(0x04)
#define U_INTRTX      UR(0x08)
#define U_INTRRX      UR(0x0C)
#define U_INTRTXE     UR(0x10)
#define U_INTRRXE     UR(0x14)
#define U_INTRUSB     UR(0x18)
#define U_INTRUSBE    UR(0x1C)
#define U_INDEX       UR(0x24)
#define U_TESTMODE    UR(0x28)
#define U_GLOBAL      UR(0x30)
#define U_CSR0        UR(0x44)       /* INDEX 0 */
#define U_RXMAXP      UR(0x48)
#define U_RXCSR       UR(0x4C)
#define U_COUNT       UR(0x50)       /* COUNT0 / RXCOUNT */
#define U_NAKLIMIT0   UR(0x58)
#define U_RXTYPE      UR(0x5C)
#define U_RXINTERVAL  UR(0x60)
#define U_FIFO(n)     UR(0x80 + 8 * (n))
#define U_DEVCTL      UR(0x100)
#define U_VPLEN       UR(0x14C)

#define POWER_RESET   0x08
#define POWER_HSMODE  0x10
#define POWER_SOFTCON 0x40
#define INT_CONN      0x10
#define INT_DISCON    0x20
#define INT_VBUSERR   0x80
#define DC_SESSION    0x01
#define DC_HOSTMODE   0x04
#define DC_VBUS       0x18
#define DC_LSDEV      0x20
#define DC_FSDEV      0x40
#define DC_BDEVICE    0x80
#define CSR0_RXRDY    0x01
#define CSR0_TXRDY    0x02
#define CSR0_STALL    0x04
#define CSR0_SETUP    0x08
#define CSR0_ERROR    0x10
#define CSR0_REQPKT   0x20
#define CSR0_STATUS   0x40
#define CSR0_FLUSH    0x100
#define RX_RXRDY      0x01
#define RX_ERROR      0x04
#define RX_NAKTO      0x08
#define RX_REQPKT     0x20
#define RX_STALL      0x40
#define RX_CLRTOG     0x80
#define HOST_EP       1              /* our RX endpoint for the keyboard's MIDI IN */

/* ---- R246 ---- */
#define USB_PLUGGED_COUNT (*(volatile uint32_t *)0x018B62A8)   /* power code counts "session on" to 50, then charges */
#define RX_RING  ((volatile uint32_t *)0x01AB0510)             /* USB-MIDI receive ring (kennel/kn_midi.c) */
#define RX_WR    (*(volatile uint32_t *)0x01AB1510)
#define RX_RD    (*(volatile uint32_t *)0x01AB1514)
/* the USB port's 5 V: bit 0 of R246's power-output byte, sent to the power controller (device 9) as {1, byte} through
 * R246's command routine, the way R246 sends bit 1 of the same byte (0x01A11410) and Opus switches its port power */
#define PWR_OUT  (*(volatile uint8_t *)0x01DE4F79)
#define PWR_SEND ((int (*)(int, int, const void *, int))0x01A69024)

static inline uint32_t cycles(void) { uint32_t c; __asm__ volatile("%0 = CYCLES;" : "=d"(c)); return c; }
static inline void ssync(void) { __asm__ volatile("ssync;" : : : "memory"); }
static inline uint32_t irq_off(void) { uint32_t m; __asm__ volatile("cli %0;" : "=d"(m)); return m; }
static inline void irq_on(uint32_t m) { __asm__ volatile("sti %0;" : : "d"(m)); }
#define CYC_MS 400000u                                         /* 400 MHz */

enum { S_IDLE, S_SESSION, S_WAITCONN, S_RUN, S_FAILED, S_STOPPED };
enum { E_NONE, E_STALL, E_ERROR, E_TIMEOUT, E_NOMIDI, E_COMPUTER, E_NOSESSION, E_VBUSERR, E_UNPLUGGED, E_BADDESC, E_BDEVICE };
enum { P_NONE, P_DEV8, P_ADDR, P_DEV18, P_CFG9, P_CFG, P_SETCFG, P_EP };

static volatile uint8_t state, err, phase, req_toggle, req_clear;
static volatile uint16_t dc_seen, power_seen, intr_seen, last_csr, dc_at_err, vplen_was;
static volatile uint8_t vbus_errs;
static volatile uint16_t vid, pid, mps0, speed;              /* speed: 0 full, 1 low, 2 high */
static volatile uint32_t packets, dropped, rx_errors, polls;
static volatile uint8_t last_pk[4][4];
static ud_midi midi;
static uint32_t t0;

/* what host mode changes, to give back */
static uint16_t sv_power, sv_global, sv_test, sv_txe, sv_rxe, sv_usbe, sv_index;

static void keep_charger_off(void) { USB_PLUGGED_COUNT = 0; }

static volatile uint8_t port_on;
static uint8_t pwr_msg[2];
static void port_power(int on)
{
    uint8_t v = PWR_OUT;
    v = (uint8_t)(on ? v | 1 : v & ~1);
    PWR_OUT = v;
    pwr_msg[0] = 1; pwr_msg[1] = v;
    PWR_SEND(9, 2, pwr_msg, 0);
    port_on = (uint8_t)on;
}

static void wait_ms(uint32_t ms)
{
    const uint32_t t = cycles();
    while (cycles() - t < ms * CYC_MS) keep_charger_off();
}

static uint16_t intr_pending;                                   /* seen and not yet handled (a connect during S_SESSION) */
static uint16_t intr_usb(void)                                 /* write-1-to-clear; remembered for the screen */
{
    uint16_t v = U_INTRUSB;
    if (v) { U_INTRUSB = v; intr_seen |= v; intr_pending |= v; }
    return intr_pending;
}

static void clear_pending(void)
{
    uint16_t v;
    v = U_INTRUSB; U_INTRUSB = v;
    v = U_INTRTX;  U_INTRTX = v;
    v = U_INTRRX;  U_INTRRX = v;
}

/* ---- device mode <-> host mode ---- */
static void host_enter(void)
{
    sv_power = U_POWER; sv_global = U_GLOBAL; sv_test = U_TESTMODE; sv_index = U_INDEX;
    sv_txe = U_INTRTXE; sv_rxe = U_INTRRXE; sv_usbe = U_INTRUSBE;
    U_INTRTXE = 0; U_INTRRXE = 0; U_INTRUSBE = 0;              /* R246's USB interrupt stays out: we poll */
    ssync();
    clear_pending();
    U_POWER = (uint16_t)(sv_power & ~POWER_SOFTCON);           /* off the computer's bus */
    U_GLOBAL = (uint16_t)(sv_global | 0x0001 | (1u << (7 + HOST_EP)));   /* enabled, our RX endpoint on */
    U_FADDR = 0;
    vplen_was = U_VPLEN;
    U_VPLEN = 1;                                               /* as Opus does before its session */
    U_DEVCTL = DC_SESSION;               /* a first try with no power: a VBUS error then means an OTG adapter (Opus) */
    ssync();
}

static void host_leave(void)
{
    U_INDEX = HOST_EP; U_RXCSR = 0;
    U_INDEX = 0; U_CSR0 = 0;
    U_DEVCTL = 0;                                              /* session off */
    ssync();
    if (port_on) port_power(0);                                /* the port's 5 V off */
    wait_ms(20);
    U_FADDR = 0;
    U_POWER = sv_power; U_GLOBAL = sv_global; U_TESTMODE = sv_test;
    U_VPLEN = vplen_was;
    clear_pending();
    U_INTRTXE = sv_txe; U_INTRRXE = sv_rxe; U_INTRUSBE = sv_usbe;
    U_INDEX = sv_index;
    ssync();
}

/* ---- endpoint 0 ---- */
static int wait0(int rx)
{
    const uint32_t t = cycles();
    for (;;) {
        uint16_t c = U_CSR0;
        last_csr = c;
        if (c & CSR0_STALL) { U_CSR0 = 0; return -E_STALL; }
        if (c & CSR0_ERROR) { U_CSR0 = 0; return -E_ERROR; }
        if (rx ? (c & CSR0_RXRDY) : !(c & CSR0_TXRDY)) return 0;
        if (cycles() - t > 100 * CYC_MS) { U_CSR0 = CSR0_FLUSH; U_CSR0 = 0; return -E_TIMEOUT; }
        keep_charger_off();
    }
}

static void fifo_read(int ep, uint8_t *buf, int n, int room)
{
    for (int i = 0; i < n; i += 2) {
        uint16_t w = U_FIFO(ep);
        if (i < room) buf[i] = (uint8_t)w;
        if (i + 1 < n && i + 1 < room) buf[i + 1] = (uint8_t)(w >> 8);
    }
}

/* one control transfer; IN data into buf (up to len bytes). -> bytes read, or -E_* */
static int ctrl(uint8_t rt, uint8_t rq, uint16_t val, uint16_t idx, uint16_t len, uint8_t *buf)
{
    const uint8_t s[8] = { rt, rq, (uint8_t)val, (uint8_t)(val >> 8), (uint8_t)idx, (uint8_t)(idx >> 8),
                           (uint8_t)len, (uint8_t)(len >> 8) };
    int r, got = 0;
    U_INDEX = 0;
    for (int i = 0; i < 8; i += 2) U_FIFO(0) = (uint16_t)(s[i] | s[i + 1] << 8);
    U_CSR0 = CSR0_SETUP | CSR0_TXRDY;
    if ((r = wait0(0)) < 0) return r;
    if (len && (rt & 0x80)) {
        for (;;) {
            U_CSR0 = CSR0_REQPKT;
            if ((r = wait0(1)) < 0) return r;
            int n = U_COUNT & 0x7F;
            fifo_read(0, buf + got, n, len - got);
            U_CSR0 = 0;
            got += n < len - got ? n : len - got;
            if (n < mps0 || got >= len) break;
        }
        U_CSR0 = CSR0_STATUS | CSR0_TXRDY;                     /* OUT status */
        if ((r = wait0(0)) < 0) return r;
    } else {
        U_CSR0 = CSR0_STATUS | CSR0_REQPKT;                    /* IN status */
        if ((r = wait0(1)) < 0) return r;
    }
    U_CSR0 = 0;
    return got;
}

static uint8_t cfg_buf[512];

static int enumerate(void)
{
    uint8_t d[18];
    int r;
    mps0 = 8;
    U_FADDR = 0;
    U_NAKLIMIT0 = 0;
    phase = P_DEV8;
    if ((r = ctrl(0x80, 6, 0x0100, 0, 8, d)) < 0) return r;
    if (r < 8) return -E_BADDESC;
    mps0 = d[7] == 16 || d[7] == 32 || d[7] == 64 ? d[7] : 8;
    phase = P_ADDR;
    if ((r = ctrl(0x00, 5, 1, 0, 0, 0)) < 0) return r;
    wait_ms(10);
    U_FADDR = 1;
    phase = P_DEV18;
    if ((r = ctrl(0x80, 6, 0x0100, 0, 18, d)) < 0) return r;
    if (r < 18) return -E_BADDESC;
    vid = (uint16_t)(d[8] | d[9] << 8); pid = (uint16_t)(d[10] | d[11] << 8);
    phase = P_CFG9;
    if ((r = ctrl(0x80, 6, 0x0200, 0, 9, cfg_buf)) < 0) return r;
    if (r < 9) return -E_BADDESC;
    int total = cfg_buf[2] | cfg_buf[3] << 8;
    if (total > (int)sizeof cfg_buf) total = sizeof cfg_buf;
    phase = P_CFG;
    if ((r = ctrl(0x80, 6, 0x0200, 0, (uint16_t)total, cfg_buf)) < 0) return r;
    if (!ud_find_midi(cfg_buf, r, &midi)) return -E_NOMIDI;
    phase = P_SETCFG;
    if ((r = ctrl(0x00, 9, midi.cfg, 0, 0, 0)) < 0) return r;
    phase = P_EP;
    U_INDEX = HOST_EP;
    U_RXMAXP = midi.maxp ? midi.maxp : 64;
    U_RXTYPE = (uint16_t)(midi.ep_type << 4 | midi.ep);
    U_RXINTERVAL = midi.ep_type == 3 ? 1 : 0;                  /* bulk: no NAK limit, the request waits for data */
    U_RXCSR = RX_CLRTOG;
    U_RXCSR = RX_REQPKT;
    return 0;
}

static void push_packet(const uint8_t *pk)
{
    uint32_t w = ud_ring_word(pk);
    if (!w) return;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 4; j++) last_pk[i][j] = last_pk[i + 1][j];
    for (int j = 0; j < 4; j++) last_pk[3][j] = pk[j];
    uint32_t m = irq_off(), wr = RX_WR, rd = RX_RD;
    int ok = wr - rd < 1023u;
    if (ok) { RX_RING[wr & 1023u] = w; ssync(); RX_WR = wr + 1u; ssync(); }
    irq_on(m);
    if (ok) packets++; else dropped++;
}

static void poll_midi(void)
{
    uint8_t buf[64];
    polls++;
    U_INDEX = HOST_EP;
    uint16_t c = U_RXCSR;
    if (c & RX_STALL) { rx_errors++; U_RXCSR = RX_CLRTOG; U_RXCSR = RX_REQPKT; return; }
    if (c & (RX_ERROR | RX_NAKTO)) { rx_errors++; U_RXCSR = 0; U_RXCSR = RX_REQPKT; return; }
    if (!(c & RX_RXRDY)) return;
    int n = U_COUNT & 0x1FFF;
    fifo_read(HOST_EP, buf, n, sizeof buf);
    U_RXCSR = 0;
    U_RXCSR = RX_REQPKT;
    if (n > (int)sizeof buf) n = sizeof buf;
    for (int i = 0; i + 4 <= n; i += 4) push_packet(buf + i);
}

static void fail(int e)
{
    err = (uint8_t)e;
    host_leave();
    state = S_FAILED;
}

static volatile uint8_t restarts;

/* Opus's powered start (its states 2..4): session off, port power off until VBUS has fallen, power on, wait for VBUS
 * valid, then the session. Started with VBUS already up, the controller takes the B side; this order keeps it host.
 * Main loop, blocking up to about a second. -> 1 when VBUS came up */
static int powered_start(void)
{
    U_DEVCTL = 0; ssync();
    if (port_on) port_power(0);
    uint32_t t = cycles();
    while ((U_DEVCTL & DC_VBUS) && cycles() - t < 500 * CYC_MS) keep_charger_off();
    wait_ms(30);
    port_power(1);
    t = cycles();
    while ((U_DEVCTL & DC_VBUS) != DC_VBUS && cycles() - t < 500 * CYC_MS) keep_charger_off();
    const int ok = (U_DEVCTL & DC_VBUS) == DC_VBUS;
    intr_pending = 0;
    clear_pending();
    U_DEVCTL = DC_SESSION; ssync();
    t0 = cycles();
    return ok;
}

/* main loop, every pass */
static void step(void)
{
    if (req_clear) { req_clear = 0; packets = dropped = rx_errors = polls = 0; intr_seen = 0; }
    if (req_toggle) {
        req_toggle = 0;
        if (state == S_SESSION || state == S_WAITCONN || state == S_RUN) { host_leave(); state = S_STOPPED; return; }
        err = E_NONE; phase = P_NONE; vbus_errs = 0; port_on = 0; restarts = 0; dc_at_err = 0; vid = pid = 0; midi.ep = 0; midi.n_iface = 0; intr_seen = intr_pending = 0;
        dc_seen = U_DEVCTL;
        if (dc_seen & DC_VBUS) { err = E_COMPUTER; state = S_FAILED; return; }   /* something already powers the bus */
        host_enter();
        t0 = cycles();
        state = S_SESSION;
        return;
    }
    if (state == S_IDLE || state == S_FAILED || state == S_STOPPED) return;
    keep_charger_off();
    const uint16_t dc = U_DEVCTL;
    dc_seen = dc; power_seen = U_POWER;
    const uint16_t iu = intr_usb();
    if (iu & INT_VBUSERR) {                                    /* VBUS dipped (unpowered try, or a keyboard's inrush) */
        intr_pending &= (uint16_t)~INT_VBUSERR;
        dc_at_err = dc;
        if (vbus_errs < 255) vbus_errs++;
    }
    if (port_on && state != S_SESSION && (!(dc & DC_SESSION) || (dc & DC_BDEVICE))) {   /* lost host: start over */
        if (++restarts > 5) { fail(E_NOSESSION); return; }
        powered_start();
        state = S_SESSION;
        return;
    }
    switch (state) {
    case S_SESSION:
        if (!port_on) {          /* no power yet: an A-device try (VBUS error, or session + A-device) means an adapter */
            if (vbus_errs || ((dc & (DC_BDEVICE | DC_SESSION)) == DC_SESSION && cycles() - t0 > 40 * CYC_MS)) {
                if (!powered_start()) fail(E_VBUSERR);         /* the port's 5 V never came up */
                break;
            }
            if (!(dc & DC_SESSION) && cycles() - t0 < 1000 * CYC_MS) { U_DEVCTL = DC_SESSION; ssync(); }
            if (cycles() - t0 > 2000 * CYC_MS) fail((dc & (DC_BDEVICE | DC_SESSION)) == (DC_BDEVICE | DC_SESSION) ? E_BDEVICE : E_NOSESSION);
            break;
        }
        if ((dc & (DC_HOSTMODE | DC_SESSION)) == (DC_HOSTMODE | DC_SESSION)) { restarts = 0; state = S_WAITCONN; break; }
        if (cycles() - t0 > 2000 * CYC_MS) {                   /* no host mode within 2 s (Opus's state 4) */
            if (++restarts > 5) { fail((dc & DC_BDEVICE) ? E_BDEVICE : E_NOSESSION); break; }
            powered_start();
        }
        break;
    case S_WAITCONN:                                           /* until a device connects (or blue again) */
        if ((iu & INT_CONN) || (dc & (DC_FSDEV | DC_LSDEV))) {    /* Opus watches the speed bits, not the interrupt */
            intr_pending &= (uint16_t)~(INT_CONN | INT_DISCON);
            wait_ms(100);                                      /* the device powers up */
            U_POWER = (uint16_t)(U_POWER | POWER_RESET);
            wait_ms(50);
            U_POWER = (uint16_t)(U_POWER & ~POWER_RESET);
            wait_ms(20);
            speed = (U_POWER & POWER_HSMODE) ? 2 : (dc & DC_LSDEV) ? 1 : 0;
            int r = enumerate();
            if (r < 0) { fail(-r); break; }
            state = S_RUN;
        }
        break;
    case S_RUN:
        if (iu & INT_DISCON) { fail(E_UNPLUGGED); break; }
        poll_midi();
        break;
    }
}

static void up_service(int ev, int32_t arg)
{
    (void)arg;
    if (ev == OP1_EV_SERVICE) step();
}

/* ---- the engine: silent ---- */
static void up_note_on(void *self, const int16_t *params) { (void)self; (void)params; }
static void up_render(void *self, int16_t *out, const int16_t *params)
{
    (void)self; (void)params;
    for (int i = 0; i < OP1_BLOCK; i++) out[i] = 0;
}

/* KeyHandler: blue push toggles host mode, green push clears the counts; turns are left alone */
static int up_input(int page, int ev, int a, int b)
{
    if (page != OP1_ENG_T1 || ev != OP1_ENG_PUSH || a < 0 || a > 1) return 0;
    if (b) { if (a == 0) req_toggle = 1; else req_clear = 1; }
    return 1;
}

const op1_engine_ext2_t ENGINE_usbprobe = {
    { { up_note_on, up_render, 0, { OP1_PRESS_NONE, OP1_PRESS_NONE, OP1_PRESS_NONE, OP1_PRESS_NONE } }, 0, 0, 0 },
    up_input,
    0,
    up_service,
};

/* ---- screen (GUI thread) ---- */
#define Q(v) ((v) << 10)
#define ROWS 8
#define RED_ROW ROWS                                           /* a second status row object, in red */
static const uint8_t c_white[4] = { 0xFF, 0xFF, 0xFF, 0xFF }, c_grey[4] = { 0x8A, 0x8C, 0x8E, 0xFF },
                     c_green[4] = { 0x00, 0xED, 0x95, 0xFF }, c_red[4] = { 0xFF, 0x3A, 0x5D, 0xFF };
static uint32_t tx[ROWS + 1][OP1_TEXT_SIZE / 4];
static char shown[ROWS + 1][48];
static uint8_t made;

static void txt(void *r, int i, const char *s)
{
    static const int32_t ident[9] = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 };
    int same = 1;
    for (int j = 0; j < 48; j++) { if (shown[i][j] != s[j]) { same = 0; break; } if (!s[j]) break; }
    if (!same) { op1_text_set(tx[i], s); int j = 0; for (; j < 47 && s[j]; j++) shown[i][j] = s[j]; shown[i][j] = 0; }
    op1_text_draw(tx[i], r, ident);
}
static char *put(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *num(char *p, uint32_t x) { char t[10]; int n = 0; do t[n++] = (char)('0' + x % 10); while ((x /= 10)); while (n) *p++ = t[--n]; *p = 0; return p; }
static char *hex(char *p, uint32_t x, int digits)
{
    for (int i = digits - 1; i >= 0; i--) { int d = (x >> (4 * i)) & 15; *p++ = (char)(d < 10 ? '0' + d : 'a' + d - 10); }
    *p = 0; return p;
}

static const char *state_text(void)
{
    static const char *const errs[] = { "", "the keyboard said no (stall)", "usb error", "no answer (timeout)",
        "no usb-midi in this device", "unplug the computer cable first", "no host mode: otg adapter?",
        "the port's 5v never came up", "the keyboard was unplugged", "a broken descriptor",
        "stayed a device: not an otg cable" };
    switch (state) {
    case S_IDLE: return "push blue to start host mode";
    case S_SESSION: return "starting host mode";
    case S_WAITCONN: return "host mode: plug the keyboard in";
    case S_RUN: return "running: play the keyboard";
    case S_STOPPED: return "stopped: back to normal usb";
    default: return err < sizeof errs / sizeof errs[0] ? errs[err] : "failed";
    }
}

static void up_screen_init(void *scr) { (void)scr; }

static void up_screen_update(void *scr, int dt)
{
    static const char *const phases[] = { "-", "device (8)", "address", "device", "config (9)", "config", "set config",
                                          "endpoint" };
    static const char *const speeds[] = { "full", "low", "high" };
    if (!made) {
        op1_text_ctor(tx[0], Q(8), 6, 12, c_grey);
        for (int i = 1; i < ROWS; i++) op1_text_ctor(tx[i], Q(8), 6 + 19 * i, 12, i == 1 ? c_green : c_white);
        op1_text_ctor(tx[RED_ROW], Q(8), 6 + 19, 12, c_red);
        made = 1;
    }
    op1_screen_draw(scr, dt);
    void *r = op1_raster();
    char s[72], *q;                               /* rows stay under 48 in practice; room for the worst case */
    txt(r, 0, "usbprobe 0.1.5  blue: start/stop  green: clear");
    txt(r, state == S_FAILED ? RED_ROW : 1, state_text());
    q = put(s, "devctl "); q = hex(q, dc_seen, 2); q = put(q, "  power "); q = hex(q, power_seen, 2);
    q = put(q, "  intr "); q = hex(q, intr_seen, 2); put(q, port_on ? "  5v on" : "  5v off");
    txt(r, 2, s);
    q = put(s, "step "); q = put(q, phases[phase < 8 ? phase : 0]); q = put(q, " vbuserr "); q = num(q, vbus_errs);
    q = put(q, " at "); q = hex(q, dc_at_err, 2); q = put(q, " restarts "); num(q, restarts);   /* fits 48 chars at worst */
    txt(r, 3, s);
    q = put(s, "vid "); q = hex(q, vid, 4); q = put(q, "  pid "); q = hex(q, pid, 4);
    q = put(q, "  "); q = put(q, speeds[speed < 3 ? speed : 0]); q = put(q, "  ep0 "); num(q, mps0);
    txt(r, 4, s);
    if (midi.ep) {
        q = put(s, "midi in ep "); q = num(q, midi.ep); q = put(q, midi.ep_type == 3 ? " int " : " bulk ");
        q = num(q, midi.maxp); q = put(q, "  config "); num(q, midi.cfg);
    } else {
        q = put(s, "interfaces "); q = num(q, midi.n_iface);
        for (int i = 0; i < midi.n_iface && i < 4; i++) { q = put(q, " "); q = hex(q, midi.classes[i], 2); }
    }
    txt(r, 5, s);
    q = put(s, "packets "); q = num(q, packets); q = put(q, "  dropped "); q = num(q, dropped);
    q = put(q, "  errors "); num(q, rx_errors);
    txt(r, 6, s);
    q = s; *q = 0;
    for (int i = 0; i < 4; i++) {
        if (!(last_pk[i][0] | last_pk[i][1])) continue;
        for (int j = 1; j < 4; j++) q = hex(q, last_pk[i][j], 2);
        q = put(q, " ");
    }
    txt(r, 7, s[0] ? s : "no midi yet");
}

const op1_screen_t SCREEN_usbprobe = { up_screen_init, up_screen_update };
