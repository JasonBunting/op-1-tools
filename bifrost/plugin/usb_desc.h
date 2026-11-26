/* Bifrost: the USB parts with no hardware in them, so the host test can run them: finding a USB-MIDI streaming
 * interface and its IN endpoint in a configuration descriptor, a device's name from a string descriptor, and a
 * USB-MIDI bulk packet as a receive-ring word.
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#ifndef USB_DESC_H
#define USB_DESC_H
#include <stdint.h>

typedef struct {
    uint8_t cfg;        /* bConfigurationValue */
    uint8_t ep;         /* the MIDI IN endpoint's number (1..15), 0: none found */
    uint8_t ep_type;    /* 2 bulk, 3 interrupt */
    uint16_t maxp;      /* its wMaxPacketSize (11 bits) */
    uint8_t iface;      /* the MIDI streaming interface's number */
    uint8_t n_iface;    /* interface descriptors seen */
} ud_midi;

/* Walks a whole configuration descriptor (cfg, len bytes). 1 when a MIDI streaming interface (class 1, subclass 3)
 * with an IN bulk or interrupt endpoint was found, else 0. A short or broken descriptor stops the walk. */
static inline int ud_find_midi(const uint8_t *cfg, int len, ud_midi *m)
{
    m->cfg = 0; m->ep = 0; m->ep_type = 0; m->maxp = 0; m->iface = 0; m->n_iface = 0;
    if (len < 9 || cfg[1] != 2) return 0;
    m->cfg = cfg[5];
    int in_midi = 0;
    for (int p = 0; p + 2 <= len;) {
        int dl = cfg[p], dt = cfg[p + 1];
        if (dl < 2 || p + dl > len) break;
        if (dt == 4 && dl >= 9) {                                  /* interface */
            m->n_iface++;
            in_midi = cfg[p + 5] == 1 && cfg[p + 6] == 3;
            if (in_midi && !m->ep) m->iface = cfg[p + 2];
        } else if (dt == 5 && dl >= 7 && in_midi && !m->ep) {      /* an endpoint of the MIDI interface */
            int addr = cfg[p + 2], type = cfg[p + 3] & 3;
            if ((addr & 0x80) && (type == 2 || type == 3)) {
                m->ep = (uint8_t)(addr & 15);
                m->ep_type = (uint8_t)type;
                m->maxp = (uint16_t)((cfg[p + 4] | cfg[p + 5] << 8) & 0x7FF);
            }
        }
        p += dl;
    }
    return m->ep != 0;
}

/* A string descriptor (UTF-16LE) as printable ASCII for a pop-up: other characters become '?', leading and trailing
 * spaces go, at most max - 1 characters. -> the length (0: no usable name). */
static inline int ud_string_ascii(const uint8_t *d, int len, char *out, int max)
{
    int n = 0;
    if (max <= 0) return 0;
    if (len >= 2 && d[1] == 3) {
        int dl = d[0] < len ? d[0] : len;
        for (int i = 2; i + 1 < dl && n < max - 1; i += 2) {
            unsigned c = d[i] | (unsigned)d[i + 1] << 8;
            if (c == ' ' && n == 0) continue;
            out[n++] = (char)(c >= 0x20 && c < 0x7F ? c : '?');
        }
    }
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = 0;
    return n;
}

/* One 4-byte USB-MIDI event packet as a receive-ring word: little-endian, cable 0 (the stock parser ignores the
 * cable; Hundekraft's raw-MIDI hook skips cable 15, which marks its own posted bytes). 0: nothing to queue. */
static inline uint32_t ud_ring_word(const uint8_t *pk)
{
    if (!(pk[0] & 15)) return 0;
    return (uint32_t)(pk[0] & 15) | (uint32_t)pk[1] << 8 | (uint32_t)pk[2] << 16 | (uint32_t)pk[3] << 24;
}
#endif
