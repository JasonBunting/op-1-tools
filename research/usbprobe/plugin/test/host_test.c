/* usbprobe host test: descriptor parsing and ring words (usb_desc.h).
 * gcc -Wall -I research/usbprobe/plugin research/usbprobe/plugin/test/host_test.c -o /tmp/up && /tmp/up */
#include <stdio.h>
#include "usb_desc.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* a class-compliant keyboard: audio control interface, then MIDI streaming with jacks, bulk OUT 0x02 and IN 0x81 */
static const unsigned char kbd[] = {
    9, 2, 101, 0, 2, 1, 0, 0x80, 50,                       /* config 1, 101 bytes, 2 interfaces */
    9, 4, 0, 0, 0, 1, 1, 0, 0,                             /* interface 0: audio control */
    9, 0x24, 1, 0, 1, 9, 0, 1, 1,                          /* class-specific AC header */
    9, 4, 1, 0, 2, 1, 3, 0, 0,                             /* interface 1: MIDI streaming, 2 endpoints */
    7, 0x24, 1, 0, 1, 65, 0,                               /* MS header */
    6, 0x24, 2, 1, 1, 0,                                   /* in jack embedded */
    6, 0x24, 2, 2, 2, 0,                                   /* in jack external */
    9, 0x24, 3, 1, 3, 1, 2, 1, 0,                          /* out jack embedded */
    9, 0x24, 3, 2, 4, 1, 1, 1, 0,                          /* out jack external */
    9, 5, 0x02, 2, 64, 0, 0, 0, 0,                         /* bulk OUT 2 (audio-style 9-byte endpoint) */
    5, 0x25, 1, 1, 1,                                      /* class-specific MS endpoint */
    9, 5, 0x81, 2, 64, 0, 0, 0, 0,                         /* bulk IN 1 */
    5, 0x25, 1, 1, 3,
};

int main(void)
{
    ud_midi m;
    CHECK(ud_find_midi(kbd, sizeof kbd, &m) == 1);
    CHECK(m.cfg == 1 && m.ep == 1 && m.ep_type == 2 && m.maxp == 64 && m.iface == 1 && m.n_iface == 2);
    CHECK(m.classes[0] == 0x11 && m.classes[1] == 0x13);

    /* a mouse: HID only, no MIDI */
    static const unsigned char mouse[] = { 9, 2, 34, 0, 1, 1, 0, 0xA0, 50, 9, 4, 0, 0, 1, 3, 1, 2, 0,
                                           9, 0x21, 0x11, 1, 0, 1, 0x22, 52, 0, 7, 5, 0x81, 3, 4, 0, 10 };
    CHECK(ud_find_midi(mouse, sizeof mouse, &m) == 0);
    CHECK(m.n_iface == 1 && m.classes[0] == 0x31);

    /* truncated: the walk stops, nothing found, no read past the end */
    CHECK(ud_find_midi(kbd, 60, &m) == 0);
    CHECK(ud_find_midi(kbd, 5, &m) == 0);
    static const unsigned char zero_len[] = { 9, 2, 20, 0, 1, 1, 0, 0x80, 50, 0, 4, 0, 0, 0, 1, 3, 0, 0, 0, 0 };
    CHECK(ud_find_midi(zero_len, sizeof zero_len, &m) == 0);

    /* ring words */
    const unsigned char on[4] = { 0x09, 0x90, 60, 100 }, cable2[4] = { 0x29, 0x91, 64, 1 }, pad[4] = { 0, 0, 0, 0 };
    CHECK(ud_ring_word(on) == 0x643C9009u);
    CHECK(ud_ring_word(cable2) == 0x01409109u);
    CHECK(ud_ring_word(pad) == 0);

    printf(fails ? "%d failed\n" : "all passed\n", fails);
    return fails != 0;
}
