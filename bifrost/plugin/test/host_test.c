/* Bifrost host test: descriptor parsing, device names and ring words (usb_desc.h); the knob map (knobmap.h).
 * gcc -Wall -Wextra -I bifrost/plugin bifrost/plugin/test/host_test.c -o /tmp/bf && /tmp/bf */
#include <stdio.h>
#include <string.h>
#include "usb_desc.h"
#include "knobmap.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* a class-compliant keyboard: audio control interface, then MIDI streaming with jacks, bulk OUT 0x02 and IN 0x81 */
static const unsigned char kbd[] = {
    9, 2, 101, 0, 2, 1, 0, 0x80, 50,
    9, 4, 0, 0, 0, 1, 1, 0, 0,
    9, 0x24, 1, 0, 1, 9, 0, 1, 1,
    9, 4, 1, 0, 2, 1, 3, 0, 0,
    7, 0x24, 1, 0, 1, 65, 0,
    6, 0x24, 2, 1, 1, 0,
    6, 0x24, 2, 2, 2, 0,
    9, 0x24, 3, 1, 3, 1, 2, 1, 0,
    9, 0x24, 3, 2, 4, 1, 1, 1, 0,
    9, 5, 0x02, 2, 64, 0, 0, 0, 0,
    5, 0x25, 1, 1, 1,
    9, 5, 0x81, 2, 64, 0, 0, 0, 0,
    5, 0x25, 1, 1, 3,
};

/* a composite controller: HID first, then MIDI with an interrupt IN endpoint */
static const unsigned char comp[] = {
    9, 2, 59, 0, 2, 2, 0, 0x80, 50,
    9, 4, 0, 0, 1, 3, 0, 0, 0,
    9, 0x21, 0x11, 1, 0, 1, 0x22, 40, 0,
    7, 5, 0x83, 3, 8, 0, 10,
    9, 4, 1, 0, 1, 1, 3, 0, 0,
    7, 5, 0x82, 3, 32, 0, 1,
};

static void name_of(const char *ascii, unsigned char *d, int *len)
{
    int n = (int)strlen(ascii);
    d[0] = (unsigned char)(2 + 2 * n); d[1] = 3;
    for (int i = 0; i < n; i++) { d[2 + 2 * i] = (unsigned char)ascii[i]; d[3 + 2 * i] = 0; }
    *len = 2 + 2 * n;
}

int main(void)
{
    ud_midi m;
    CHECK(ud_find_midi(kbd, sizeof kbd, &m) == 1);
    CHECK(m.cfg == 1 && m.ep == 1 && m.ep_type == 2 && m.maxp == 64 && m.iface == 1 && m.n_iface == 2);
    CHECK(ud_find_midi(comp, sizeof comp, &m) == 1);
    CHECK(m.cfg == 2 && m.ep == 2 && m.ep_type == 3 && m.maxp == 32 && m.iface == 1);

    static const unsigned char mouse[] = { 9, 2, 34, 0, 1, 1, 0, 0xA0, 50, 9, 4, 0, 0, 1, 3, 1, 2, 0,
                                           9, 0x21, 0x11, 1, 0, 1, 0x22, 52, 0, 7, 5, 0x81, 3, 4, 0, 10 };
    CHECK(ud_find_midi(mouse, sizeof mouse, &m) == 0 && m.n_iface == 1);
    CHECK(ud_find_midi(kbd, 60, &m) == 0);
    CHECK(ud_find_midi(kbd, 5, &m) == 0);
    static const unsigned char zero_len[] = { 9, 2, 20, 0, 1, 1, 0, 0x80, 50, 0, 4, 0, 0, 0, 1, 3, 0, 0, 0, 0 };
    CHECK(ud_find_midi(zero_len, sizeof zero_len, &m) == 0);

    unsigned char d[128]; char out[25]; int len;
    name_of("  MPK mini 3 ", d, &len);
    CHECK(ud_string_ascii(d, len, out, sizeof out) == 10 && !strcmp(out, "MPK mini 3"));
    name_of("A very long product name indeed, longer than room", d, &len);
    CHECK(ud_string_ascii(d, len, out, sizeof out) == 24 && strlen(out) == 24);
    d[0] = 6; d[1] = 3; d[2] = 0xE9; d[3] = 0; d[4] = 'x'; d[5] = 0x20;     /* e-acute and a non-Latin character */
    CHECK(ud_string_ascii(d, 6, out, sizeof out) == 2 && !strcmp(out, "??"));
    d[1] = 2;                                                                /* not a string descriptor */
    CHECK(ud_string_ascii(d, 6, out, sizeof out) == 0 && out[0] == 0);
    name_of("Odd", d, &len);
    CHECK(ud_string_ascii(d, 5, out, sizeof out) == 1 && !strcmp(out, "O"));   /* truncated transfer */

    const unsigned char on[4] = { 0x09, 0x90, 60, 100 }, cable2[4] = { 0x29, 0x91, 64, 1 }, pad[4] = { 0, 0, 0, 0 };
    CHECK(ud_ring_word(on) == 0x643C9009u);
    CHECK(ud_ring_word(cable2) == 0x01409109u);
    CHECK(ud_ring_word(pad) == 0);

    /* knob kinds from how they turn */
    const uint8_t abs_up[] = { 60, 61, 62, 64, 66 }, abs_slow[] = { 64, 65, 66, 67 };
    const uint8_t r2c_up[] = { 1, 1, 1, 2, 1 }, r2c_dn[] = { 127, 127, 126, 127 }, r64[] = { 65, 65, 66, 65 }, r64d[] = { 63, 63, 63 };
    CHECK(km_detect(abs_up, 5) == KM_ABS && km_detect(abs_slow, 4) == KM_ABS);
    CHECK(km_detect(r2c_up, 5) == KM_REL2C && km_detect(r2c_dn, 4) == KM_REL2C);
    CHECK(km_detect(r64, 4) == KM_REL64 && km_detect(r64d, 3) == KM_REL64);
    CHECK(km_detect(r2c_up, 2) == KM_ABS);                                   /* too few to tell */

    /* learning eight knobs, two kinds */
    km_learn l = { 0 }; km_map km = { 0 };
    int news = 0;
    for (int k = 0; k < 8; k++) {
        const int cc = 70 + k;
        for (int t = 0; t < 4; t++) {
            const int v = k < 4 ? 40 + t : 1;                                /* top row absolute, bottom relative */
            if (km_learn_cc(&l, 0, cc, v) >= 0) news++;
            if (k < 7) CHECK(!km_learn_full(&l));
        }
    }
    CHECK(news == 8 && km_learn_full(&l));
    km_learn_finish(&l, &km);
    CHECK(km.n == 8 && km.mode[0] == KM_ABS && km.mode[4] == KM_REL2C);
    CHECK(km_find(&km, 0, 73) == 3 && km_find(&km, 0, 77) == 7 && km_find(&km, 1, 70) < 0 && km_find(&km, 0, 69) < 0);
    CHECK(km_learn_cc(&l, 0, 99, 1) < 0);                                      /* a ninth CC is ignored */

    /* deltas: absolute from the last position, relative as steps */
    CHECK(km_delta(&km, 0, 45) == 2);                                          /* learned last 43 */
    CHECK(km_delta(&km, 0, 40) == -5);
    CHECK(km_delta(&km, 4, 3) == 3 && km_delta(&km, 4, 125) == -3);
    km.mode[5] = KM_REL64;
    CHECK(km_delta(&km, 5, 66) == 2 && km_delta(&km, 5, 61) == -3);

    /* saved and loaded: an absolute knob's first message after a restart only sets where it is */
    km_map km2 = { 0 };
    for (int i = 0; i < 8; i++) { km_unpack(&km2, i, km_pack(&km, i)); km2.n = (uint8_t)(i + 1); }
    CHECK(km2.id[3] == km.id[3] && km2.mode[4] == KM_REL2C && km2.mode[5] == KM_REL64);
    CHECK(km_delta(&km2, 0, 100) == 0 && km_delta(&km2, 0, 103) == 3);
    km_map ch9 = { 0 };
    km_learn l9 = { 0 };
    km_learn_cc(&l9, 9, 21, 64); km_learn_finish(&l9, &ch9);
    CHECK(km_find(&ch9, 9, 21) == 0 && (km_pack(&ch9, 0) & 0x0FFF) == (9 << 8 | 21));

    printf(fails ? "%d failed\n" : "all passed\n", fails);
    return fails != 0;
}
