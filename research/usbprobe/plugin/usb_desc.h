/* usbprobe: the parts with no hardware in them, so the host test can run them. Descriptor parsing (find a USB-MIDI
 * streaming interface and its IN endpoint) and turning a USB-MIDI bulk packet into receive-ring words.
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission. */
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
    uint8_t classes[4]; /* the first four interfaces' class << 4 | subclass, to show when no MIDI is found */
} ud_midi;

/* Walks a whole configuration descriptor (cfg, len bytes). Returns 1 when a MIDI streaming interface (class 1,
 * subclass 3) with an IN bulk or interrupt endpoint was found, else 0. A short or broken descriptor stops the walk. */
static inline int ud_find_midi(const uint8_t *cfg, int len, ud_midi *m)
{
    m->cfg = 0; m->ep = 0; m->ep_type = 0; m->maxp = 0; m->iface = 0; m->n_iface = 0;
    for (int i = 0; i < 4; i++) m->classes[i] = 0;
    if (len < 9 || cfg[1] != 2) return 0;
    m->cfg = cfg[5];
    int in_midi = 0;
    for (int p = 0; p + 2 <= len;) {
        int dl = cfg[p], dt = cfg[p + 1];
        if (dl < 2 || p + dl > len) break;
        if (dt == 4 && dl >= 9) {                                  /* interface */
            if (m->n_iface < 4) m->classes[m->n_iface] = (uint8_t)((cfg[p + 5] & 15) << 4 | (cfg[p + 6] & 15));
            m->n_iface++;
            in_midi = cfg[p + 5] == 1 && cfg[p + 6] == 3;
            if (in_midi) m->iface = cfg[p + 2];
        } else if (dt == 5 && dl >= 7 && in_midi && !m->ep) {      /* endpoint of the MIDI interface */
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

/* One 4-byte USB-MIDI event packet as a receive-ring word: little-endian, cable 0 (the stock parser ignores the
 * cable, Hundekraft's raw-MIDI hook skips cable 15). 0: nothing to queue (CIN 0, padding). */
static inline uint32_t ud_ring_word(const uint8_t *pk)
{
    if (!(pk[0] & 15)) return 0;
    return (uint32_t)(pk[0] & 15) | (uint32_t)pk[1] << 8 | (uint32_t)pk[2] << 16 | (uint32_t)pk[3] << 24;
}
#endif
