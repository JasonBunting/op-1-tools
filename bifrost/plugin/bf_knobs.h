/* Bifrost knob map: a MIDI controller's knobs turn the OP-1's four encoders, the first four on page 1 and the next four
 * on page 2, clicking an encoder to the right page first. Main loop only.
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#ifndef BF_KNOBS_H
#define BF_KNOBS_H
#include <stdint.h>

void bfk_load(void);                /* boot: the saved knobs and the settings rows */
void bfk_settings(void);            /* a settings row changed */
int bfk_take(const uint8_t *pk);    /* a USB-MIDI packet from the device: 1 when it was a knob (kept from the OP-1) */
void bfk_service(void);             /* every pass: clicks and turns waiting for their page */
#endif
