/* Bifrost: the OP-1's USB controller (the BF52x's Mentor MUSB) as a host, and the port's 5 V. Main loop only.
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#ifndef BF_USB_H
#define BF_USB_H
#include <stdint.h>
#include "usb_desc.h"

/* DEV_CTL bits */
#define BFU_SESSION   0x01
#define BFU_HOSTMODE  0x04
#define BFU_VBUS      0x18
#define BFU_LSDEV     0x20
#define BFU_FSDEV     0x40
#define BFU_BDEVICE   0x80
/* INTRUSB bits */
#define BFU_INT_CONN    0x10
#define BFU_INT_DISCON  0x20
#define BFU_INT_VBUSERR 0x80

enum { BFU_OK, BFU_E_STALL, BFU_E_ERROR, BFU_E_TIMEOUT, BFU_E_NOMIDI, BFU_E_BADDESC };

typedef struct {
    uint16_t vid, pid;
    ud_midi midi;
    char name[25];          /* the product string, ASCII; "" when it has none */
} bfu_dev;

uint16_t bfu_devctl(void);
uint16_t bfu_intr(void);            /* the INTRUSB bits seen since the last call (cleared in the controller) */
int bfu_port_on(void);

void bfu_enter(void);               /* device-mode registers saved, R246's USB interrupts off, an unpowered session */
void bfu_leave(void);               /* session off, the port's 5 V off, everything given back */
int bfu_powered_start(void);        /* the order that keeps the controller the host; 1 when VBUS came up */
void bfu_keep_charger_off(void);    /* while the OP-1 powers the port: R246 must not start charging from it */
void bfu_wait_ms(uint32_t ms);

int bfu_enumerate(bfu_dev *d);      /* bus reset, then address, descriptors, configuration, the MIDI endpoint; -BFU_E_* */
int bfu_poll_midi(void);            /* MIDI packets into R246's receive ring -> packets queued, -1 on an error */
uint32_t bfu_dropped(void);         /* packets the full ring refused */
#endif
