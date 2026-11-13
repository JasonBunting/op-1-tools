/* tractprobe: a throwaway synth engine that finds how big a physically modelled vocal tract the OP-1 can run.
 * The DSP is tp_dsp.h (a Kelly-Lochbaum tube and a glottal pulse). Read its cost on Hundekraft's resource monitor.
 *   blue   : tube sections 12, 24, 44, 64, 96 at left, quarter, half, three-quarters, right (cost; also the tract length, 3.9 mm a section)
 *   green  : tongue position, back .. front
 *   white  : tongue height, open .. closed
 *   orange : breath noise
 * Hundekraft 3.2.3. Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission. */
#include "op1_plugin.h"
#include "tp_dsp.h"

#define TP_GAIN 8192                     /* one voice at middle C: rms ~2500 of 32767 (host test) */

typedef struct {                         /* one per voice, max 0x24 bytes */
    uint32_t vtable;                     /* firmware-owned: never write */
    tp_voice v;
} tp_obj;

static tp_tube tubes[OP1_VOICES];
static tp_shape shape = { 0, -1, { 0 } };
static const uint8_t steps[5] = { 12, 24, 44, 64, 96 };      /* blue left, 1/4, 1/2, 3/4, right */

static void tp_note_on(void *self, const int16_t *params)
{
    (void)params;
    int i = op1_voice_index(self);
    if (i < 0 || i >= OP1_VOICES) return;
    tp_voice_reset(&((tp_obj *)self)->v, &tubes[i]);
}

static void tp_render_cb(void *self, int16_t *out, const int16_t *params)
{
    int i = op1_voice_index(self);
    if (i < 0 || i >= OP1_VOICES) { for (int j = 0; j < OP1_BLOCK; j++) out[j] = 0; return; }
    int32_t step = (params[0] * 5) >> 15;                                /* 0..4 */
    step = step < 0 ? 0 : (step > 4 ? 4 : step);
    int32_t pos = params[1] & 0x7FE0, height = params[2] & 0x7FE0;     /* ignore the lowest bits' jitter */
    int32_t key = (int32_t)(((uint32_t)step << 28) ^ ((uint32_t)pos << 13) ^ (uint32_t)height);
    if (key != shape.key) { tp_shape_make(&shape, steps[step], pos, height, OP1_SINE_TABLE); shape.key = key; }
    int32_t gain = (TP_GAIN * op1_voice_vel_gain(self)) >> 15;
    gain = (gain * op1_preset_trim(params)) >> 13;
    tp_render(&((tp_obj *)self)->v, &tubes[i], &shape, OP1_PITCH_INC, OP1_BLOCK, params[3], gain, out, OP1_SINE_TABLE);
}

const op1_engine_t ENGINE_tractprobe = { tp_note_on, tp_render_cb, 0,
                                         { OP1_PRESS_NONE, OP1_PRESS_NONE, OP1_PRESS_NONE, OP1_PRESS_NONE } };
