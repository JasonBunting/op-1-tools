/* Bifrost: USB host on the original OP-1 (R246 firmware under Hundekraft). How it works and where each fact comes
 * from: research/usbhost/README.md and research/usbprobe/README.md.
 *
 * The port's 5 V comes from the OP-1's power controller, not the USB controller: bit 0 of R246's power-output byte,
 * sent as the command {1, byte} to device 9 through R246's command routine, as R246 sends bit 1 of the same byte.
 * The USB controller never sees that 5 V as a host, so it raises VBUS errors that mean nothing here.
 * The order that keeps the controller the host: session off, power off until VBUS has fallen, power on, wait for VBUS
 * valid, then the session. Started with the 5 V already up, it takes the B side.
 * Host mode runs polled: the controller's interrupt enables are off, so R246's USB handler stays out.
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#pragma GCC optimize("no-tree-loop-distribute-patterns")          /* the firmware has no memcpy / memset */
#include "bf_usb.h"

/* ---- the BF52x USB controller, 16-bit registers ---- */
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
#define POWER_SOFTCON 0x40
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
#define HOST_EP       1              /* our RX endpoint for the device's MIDI IN */

/* ---- R246 ---- */
#define USB_PLUGGED_COUNT (*(volatile uint32_t *)0x018B62A8)   /* the power code counts "session on" to 50, then charges */
#define RX_RING  ((volatile uint32_t *)0x01AB0510)             /* USB-MIDI receive ring (kennel/kn_midi.c) */
#define RX_WR    (*(volatile uint32_t *)0x01AB1510)
#define RX_RD    (*(volatile uint32_t *)0x01AB1514)
#define PWR_OUT  (*(volatile uint8_t *)0x01DE4F79)             /* power-output byte; bit 0: the USB port's 5 V */
#define PWR_SEND ((int (*)(int, int, const void *, int))0x01A69024)   /* (device 9, 2 bytes, {1, byte}, 0) */

#define CYC_MS 400000u                                         /* 400 MHz */
static inline uint32_t cycles(void) { uint32_t c; __asm__ volatile("%0 = CYCLES;" : "=d"(c)); return c; }
static inline void ssync(void) { __asm__ volatile("ssync;" : : : "memory"); }
static inline uint32_t irq_off(void) { uint32_t m; __asm__ volatile("cli %0;" : "=d"(m)); return m; }
static inline void irq_on(uint32_t m) { __asm__ volatile("sti %0;" : : "d"(m)); }

static uint8_t port_on, pwr_msg[2], mps0 = 8;
static uint16_t sv_power, sv_global, sv_test, sv_txe, sv_rxe, sv_usbe, sv_index, sv_vplen;
static uint32_t dropped;

void bfu_keep_charger_off(void) { USB_PLUGGED_COUNT = 0; }
uint16_t bfu_devctl(void) { return U_DEVCTL; }
int bfu_port_on(void) { return port_on; }
uint32_t bfu_dropped(void) { return dropped; }

void bfu_wait_ms(uint32_t ms)
{
    const uint32_t t = cycles();
    while (cycles() - t < ms * CYC_MS) if (port_on) bfu_keep_charger_off();
}

uint16_t bfu_intr(void)
{
    const uint16_t v = U_INTRUSB;                              /* write-1-to-clear */
    if (v) U_INTRUSB = v;
    return v;
}

static void clear_pending(void)
{
    uint16_t v;
    v = U_INTRUSB; U_INTRUSB = v;
    v = U_INTRTX;  U_INTRTX = v;
    v = U_INTRRX;  U_INTRRX = v;
}

static void port_power(int on)
{
    uint8_t v = PWR_OUT;
    v = (uint8_t)(on ? v | 1 : v & ~1);
    PWR_OUT = v;
    pwr_msg[0] = 1; pwr_msg[1] = v;
    PWR_SEND(9, 2, pwr_msg, 0);
    port_on = (uint8_t)on;
}

/* ---- device mode <-> host mode ---- */
void bfu_enter(void)
{
    sv_power = U_POWER; sv_global = U_GLOBAL; sv_test = U_TESTMODE; sv_index = U_INDEX; sv_vplen = U_VPLEN;
    sv_txe = U_INTRTXE; sv_rxe = U_INTRRXE; sv_usbe = U_INTRUSBE;
    U_INTRTXE = 0; U_INTRRXE = 0; U_INTRUSBE = 0;
    ssync();
    clear_pending();
    U_POWER = (uint16_t)(sv_power & ~POWER_SOFTCON);           /* off the computer's bus */
    U_GLOBAL = (uint16_t)(sv_global | 0x0001 | (1u << (7 + HOST_EP)));   /* enabled, our RX endpoint on */
    U_FADDR = 0;
    U_VPLEN = 1;                                               /* as Opus */
    U_DEVCTL = BFU_SESSION;                /* unpowered: a VBUS error, or the A side, means an OTG adapter is in */
    ssync();
}

void bfu_leave(void)
{
    U_INDEX = HOST_EP; U_RXCSR = 0;
    U_INDEX = 0; U_CSR0 = 0;
    U_DEVCTL = 0;
    ssync();
    if (port_on) port_power(0);
    bfu_wait_ms(20);
    U_FADDR = 0;
    U_POWER = sv_power; U_GLOBAL = sv_global; U_TESTMODE = sv_test; U_VPLEN = sv_vplen;
    clear_pending();
    U_INTRTXE = sv_txe; U_INTRRXE = sv_rxe; U_INTRUSBE = sv_usbe;
    U_INDEX = sv_index;
    ssync();
}

int bfu_powered_start(void)
{
    U_DEVCTL = 0; ssync();
    if (port_on) port_power(0);
    uint32_t t = cycles();
    while ((U_DEVCTL & BFU_VBUS) && cycles() - t < 500 * CYC_MS) bfu_keep_charger_off();
    bfu_wait_ms(30);
    port_power(1);
    t = cycles();
    while ((U_DEVCTL & BFU_VBUS) != BFU_VBUS && cycles() - t < 500 * CYC_MS) bfu_keep_charger_off();
    const int ok = (U_DEVCTL & BFU_VBUS) == BFU_VBUS;
    clear_pending();
    U_DEVCTL = BFU_SESSION; ssync();
    return ok;
}

/* ---- endpoint 0 ---- */
static int wait0(int rx)
{
    const uint32_t t = cycles();
    for (;;) {
        const uint16_t c = U_CSR0;
        if (c & CSR0_STALL) { U_CSR0 = 0; return -BFU_E_STALL; }
        if (c & CSR0_ERROR) { U_CSR0 = 0; return -BFU_E_ERROR; }
        if (rx ? (c & CSR0_RXRDY) : !(c & CSR0_TXRDY)) return 0;
        if (cycles() - t > 100 * CYC_MS) { U_CSR0 = CSR0_FLUSH; U_CSR0 = 0; return -BFU_E_TIMEOUT; }
        bfu_keep_charger_off();
    }
}

static void fifo_read(int ep, uint8_t *buf, int n, int room)
{
    for (int i = 0; i < n; i += 2) {
        const uint16_t w = U_FIFO(ep);
        if (i < room) buf[i] = (uint8_t)w;
        if (i + 1 < n && i + 1 < room) buf[i + 1] = (uint8_t)(w >> 8);
    }
}

/* one control transfer; IN data into buf (up to len bytes) -> bytes read, or -BFU_E_* */
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
            const int n = U_COUNT & 0x7F;
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

static void read_name(bfu_dev *d, int index)
{
    uint8_t s[64];
    d->name[0] = 0;
    if (!index) return;
    int r = ctrl(0x80, 6, 0x0300, 0, 4, s);                    /* string 0: the first language */
    const uint16_t lang = r >= 4 ? (uint16_t)(s[2] | s[3] << 8) : 0x0409;
    r = ctrl(0x80, 6, (uint16_t)(0x0300 | index), lang, sizeof s, s);
    if (r > 0) ud_string_ascii(s, r, d->name, sizeof d->name);
}

int bfu_enumerate(bfu_dev *d)
{
    uint8_t dd[18];
    int r;
    bfu_wait_ms(100);                                          /* the device powers up */
    U_POWER = (uint16_t)(U_POWER | POWER_RESET);
    bfu_wait_ms(50);
    U_POWER = (uint16_t)(U_POWER & ~POWER_RESET);
    bfu_wait_ms(20);
    mps0 = 8;
    U_FADDR = 0;
    U_NAKLIMIT0 = 0;
    if ((r = ctrl(0x80, 6, 0x0100, 0, 8, dd)) < 0) return r;  /* device descriptor, first 8 bytes */
    if (r < 8) return -BFU_E_BADDESC;
    mps0 = dd[7] == 16 || dd[7] == 32 || dd[7] == 64 ? dd[7] : 8;
    if ((r = ctrl(0x00, 5, 1, 0, 0, 0)) < 0) return r;        /* SET_ADDRESS 1 */
    bfu_wait_ms(10);
    U_FADDR = 1;
    if ((r = ctrl(0x80, 6, 0x0100, 0, 18, dd)) < 0) return r;
    if (r < 18) return -BFU_E_BADDESC;
    d->vid = (uint16_t)(dd[8] | dd[9] << 8); d->pid = (uint16_t)(dd[10] | dd[11] << 8);
    if ((r = ctrl(0x80, 6, 0x0200, 0, 9, cfg_buf)) < 0) return r;
    if (r < 9) return -BFU_E_BADDESC;
    int total = cfg_buf[2] | cfg_buf[3] << 8;
    if (total > (int)sizeof cfg_buf) total = sizeof cfg_buf;
    if ((r = ctrl(0x80, 6, 0x0200, 0, (uint16_t)total, cfg_buf)) < 0) return r;
    read_name(d, dd[15]);                                      /* iProduct; a failure only leaves the name empty */
    if (!ud_find_midi(cfg_buf, r, &d->midi)) return -BFU_E_NOMIDI;
    if ((r = ctrl(0x00, 9, d->midi.cfg, 0, 0, 0)) < 0) return r;   /* SET_CONFIGURATION */
    U_INDEX = HOST_EP;
    U_RXMAXP = d->midi.maxp ? d->midi.maxp : 64;
    U_RXTYPE = (uint16_t)(d->midi.ep_type << 4 | d->midi.ep);
    U_RXINTERVAL = d->midi.ep_type == 3 ? 1 : 0;               /* bulk: no NAK limit, the request waits for data */
    U_RXCSR = RX_CLRTOG;
    U_RXCSR = RX_REQPKT;
    return BFU_OK;
}

static int push_packet(const uint8_t *pk)
{
    const uint32_t w = ud_ring_word(pk);
    if (!w) return 0;
    const uint32_t m = irq_off(), wr = RX_WR, rd = RX_RD;
    const int ok = wr - rd < 1023u;                            /* one slot always stays empty */
    if (ok) { RX_RING[wr & 1023u] = w; ssync(); RX_WR = wr + 1u; ssync(); }
    irq_on(m);
    if (!ok) dropped++;
    return ok;
}

int bfu_poll_midi(void)
{
    uint8_t buf[64];
    U_INDEX = HOST_EP;
    const uint16_t c = U_RXCSR;
    if (c & RX_STALL) { U_RXCSR = RX_CLRTOG; U_RXCSR = RX_REQPKT; return -1; }
    if (c & (RX_ERROR | RX_NAKTO)) { U_RXCSR = 0; U_RXCSR = RX_REQPKT; return -1; }
    if (!(c & RX_RXRDY)) return 0;
    int n = U_COUNT & 0x1FFF;
    fifo_read(HOST_EP, buf, n, sizeof buf);
    U_RXCSR = 0;
    U_RXCSR = RX_REQPKT;
    if (n > (int)sizeof buf) n = sizeof buf;
    int q = 0;
    for (int i = 0; i + 4 <= n; i += 4) q += push_packet(buf + i);
    return q;
}
