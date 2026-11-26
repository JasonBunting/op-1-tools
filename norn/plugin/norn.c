/* Norn: Mutable Instruments Plaits' synthesis models for the OP-1 under Hundekraft. Blue chooses the model: its
 * range is cut into four fixed zones so a saved sound keeps its model as models are added.
 *   blue   : the model: 1 waveshaper (ws_dsp.h), 2 FM (fm_dsp.h), 3 formant (gr_dsp.h), 4 wave terrain (wt_dsp.h)
 *   green  : HARMONICS  (waveshaper: the curve; FM: the frequency ratio; formant: the second formant's interval;
 *                        wave terrain: the terrain)
 *   white  : TIMBRE     (waveshaper: the wavefolder; FM: the modulation index; formant: the formant frequency;
 *                        wave terrain: the path's radius)
 *   orange : MORPH      (waveshaper: triangle to ramp; FM: feedback, frequency left of centre, phase right;
 *                        formant: the carrier's shape; wave terrain: the path's centre)
 * No screen yet: the OP-1 shows its bars for the four knobs.
 * Hundekraft 3.3.2. Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see
 * LICENSE and NOTICE). Plaits is Copyright 2016 Emilie Gillet, MIT (see NOTICE). */
#include "op1_plugin.h"
#include "ws_dsp.h"
#include "fm_dsp.h"
#include "gr_dsp.h"
#include "wt_dsp.h"
#include "norn_level.h"

enum { PL_WS, PL_FM, PL_GR, PL_WT };

typedef struct {
    ws_voice ws;
    fm_voice fm;
    gr_voice gr;
    wt_voice wt;
    uint8_t model;
} pl_voice;

static pl_voice voices[OP1_VOICES];

/* blue's four zones -> a model, in enum order */
static int pl_model(int32_t blue)
{
    return blue <= 0 ? PL_WS : blue >= 32767 ? PL_WT : (blue * 4) >> 15;
}

static void pl_note_on(void *self, const int16_t *params)
{
    int i = op1_voice_index(self);
    if (i < 0 || i >= OP1_VOICES) return;
    voices[i].model = (uint8_t)pl_model(params[0]);
    ws_voice_note(&voices[i].ws);
    fm_voice_note(&voices[i].fm);
    gr_voice_note(&voices[i].gr);
    wt_voice_note(&voices[i].wt);
}

static void pl_note_off(void *self) { (void)self; }   /* T2's release */

static void pl_render_cb(void *self, int16_t *out, const int16_t *params)
{
    int i = op1_voice_index(self);
    if (i < 0 || i >= OP1_VOICES) { for (int j = 0; j < OP1_BLOCK; j++) out[j] = 0; return; }
    pl_voice *v = &voices[i];
    const int32_t *inc = OP1_PITCH_INC;
    const int32_t gain = (NORN_GAIN * op1_voice_vel_gain(self)) >> 15;
    const int model = pl_model(params[0]);
    if (model != v->model) {                          /* blue moved during the note: the new model starts fresh */
        v->model = (uint8_t)model;
        ws_voice_note(&v->ws);
        fm_voice_note(&v->fm);
        gr_voice_note(&v->gr);
        wt_voice_note(&v->wt);
    }
    if (model == PL_WS) {
        ws_knobs k;
        ws_knobs_make(&k, params[1], params[2], params[3], (uint32_t)inc[0]);
        ws_render(&v->ws, &k, inc, OP1_BLOCK, gain, out);
    } else if (model == PL_GR) {
        gr_knobs k;
        gr_knobs_make(&k, params[1], params[2], params[3], (uint32_t)inc[0]);
        gr_render(&v->gr, &k, inc, OP1_BLOCK, gain, out, op1_sin_tab());
    } else if (model == PL_WT) {
        wt_knobs k;
        wt_knobs_make(&k, params[1], params[2], params[3], (uint32_t)inc[OP1_BLOCK - 1]);
        wt_render(&v->wt, &k, inc, OP1_BLOCK, gain, out, op1_sin_tab());
    } else {
        fm_knobs k;
        fm_knobs_make(&k, params[1], params[2], params[3], (uint32_t)inc[OP1_BLOCK - 1]);
        fm_render(&v->fm, &k, inc, OP1_BLOCK, gain, out, op1_sin_tab());   /* the core's sine, in L1 */
    }
}

const op1_engine_t ENGINE_norn = { pl_note_on, pl_render_cb, pl_note_off,
                                    { OP1_PRESS_NONE, OP1_PRESS_NONE, OP1_PRESS_NONE, OP1_PRESS_NONE } };
