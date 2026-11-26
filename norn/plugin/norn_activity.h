/* Note activity, from the engine (norn.c, the audio side) to the screen (norn_screen.c, the GUI thread): plain bytes
 * and words, each written by one side only.
 * Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#ifndef NORN_ACTIVITY_H
#define NORN_ACTIVITY_H
#include <stdint.h>

extern volatile uint8_t nr_held;                 /* bit i: voice i's key is down (note on .. note off) */
extern volatile uint8_t nr_seq;                  /* counts note-ons: a new or retriggered note */
extern volatile uint32_t nr_seen[8];             /* CPU cycles at each voice's last render: a voice no longer
                                                  * rendered (engine changed, notes cut) stops counting as held */
#endif
