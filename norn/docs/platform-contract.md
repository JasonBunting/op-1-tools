# Norn: Phase 0 platform contract

2026-11-23. Rings is out of scope for now. Named Norn on 2026-11-26 ("Pleat" was the working name).

Marks: **[S]** established from source, **[R]** established by this repo's shipped plugins, **[I]** inferred,
**[D]** needs a device measurement.

## 1. Versions

1. Mutable Instruments: fork `JasonBunting/mutable-instruments` at `f4cfa76` (2026-10-12). Plaits code is MIT
   (Emilie Gillet), so it can go into a GPL plugin with the notice kept. **[S]**
2. Hundekraft source: `hundekraft-v3.3.2`; synth SDK `VERSION` 30; `OP1_PLUGIN_API` 1. **[S]**
3. Installed core on the OP-1: assumed 3.3.2, the repo's current core. Confirm before the first device build. **[D]**
4. Compiler: `bfin-elf-gcc` 13.3.0 in WSL (`~/bfin/bin`), driven by `tools/pack-plugin.sh`. **[S]**

## 2. Rates and block sizes (the roadmap's "conflict" is resolved)

1. A voice render call gives **128 samples at 88.2 kHz**, decimated 2:1 by the core to 64 frames at 44.1 kHz. **[S]**
   (`op1_plugin.h` `OP1_BLOCK`, `OP1_VOICE_RATE`, measured on R246 2026-11-03), **[R]** (Mouth, Ratatoskr).
2. `OP1_PITCH_INC[voice]` is the phase increment per 88.2 kHz sample (2^32 = one cycle). Pitch comes from there, not
   from note numbers. **[S][R]**
3. 689 render calls a second (44100 / 64). The FX and the summed bus run at 44.1 kHz in 64-sample blocks. **[S]**
4. Plaits assumes 48 kHz (corrected 47872.34) with blocks of 12 to 24. Every frequency, coefficient, smoothing and
   oversampling constant gets rederived for 88.2 kHz and 128-sample blocks. **[S]**
5. Upside: 88.2 kHz is close to Plaits' 2x oversampled rate, so FM's 4x oversampling probably drops to none or
   2x with a cheap decimator. **[I]**

## 3. Voice lifecycle and controls

1. Callbacks: `note_on(self, params)`, `render(self, out[128], params)`, `note_off(self)`. Six voices; the core
   allocates and steals. **[S][R]**
2. Velocity: `op1_voice_vel_gain(self)`, applied once in render. **[S][R]** (Mouth)
3. T2 envelope: the stock envelope runs on top of the engine; `own_envelope` is available but not needed. **[S]**
4. Params: words 0..3 are the four knobs; T4 LFO values arrive in those words. **[S][R]** Page-2 words 5..7
   (white and orange share 7) are available only with a custom screen. **[R]** (Mouth)
5. Model selector: a page-2 stepped word (`OP1_PRESS_PAGE_STEPS(n)`) or blue. Saved model IDs need a permanent
   step-to-model table. **[S][I]**

## 4. Memory

1. The voice object is 32 usable bytes, too small for anything. **[S]** Pattern in our plugins: a static
   `voices[OP1_VOICES]` array indexed by `op1_voice_index(self)`. **[R]**
2. No malloc or libc. BSS is fine (resident RAM); `op1_alloc` is boot-only; the file limit is 1 MiB. **[S]**
3. L1 pool: 8 KiB for the synth position (`op1_l1_claim`), for the hottest tables. **[S]**
4. Upfront estimate: Waveshaper or FM state is under 100 bytes a voice; tables 3–8 KB; well inside the limits. **[I]**

## 5. CPU budget and how to measure it

1. Blackfin at 400 MHz: **580,525 cycles per 64-frame callback** for everything. Hundekraft's rule for the worst
   block: rest 35%, **synth 35%** (about 203k cycles for all six voices), each FX 12%, headroom 6%
   (`ext/kennel/docs/slot-calibration.md`). **[S]**
2. So six voices need about **34k cycles per voice call**, cache stalls included. Hundekraft's modelled counts
   miss stalls by a factor of 1.6 to 2.3, so aim for about **15–20k instructions per voice call (120–150 a sample)**.
   Stock engines declare 3.5k (fmbell) to 25k (drift). **[S][I]**
3. Norn 0.1.0's waveshaper loop is 204 Blackfin instructions, 16 of them 32-bit multiplies, so about 26–33k a voice
   call before stalls. That is over the target, so six voices are unlikely until it's optimised. Options: 16-bit
   multiplies, the tables in L1, or rendering at 44.1 kHz. **[I], [D] to confirm**
4. Measured (2026-11-23, 0.1.0, all knobs right, six voices): synth 33%, pk 66 (74 with an effect); one voice about
   6%. So the waveshaper fits the 35% rule with little margin, and the modelled estimate above was close. **[D]**
   FM (0.2.0) was about 160 instructions a sample in the compiled loop, yet measured synth 41%, pk 75 (87 with an
   effect): its 32-bit multiplies, branches and sine-table reads cost more than the count shows. **[D]** 0.2.1: the
   OP-1's own sine table, which the core keeps in L1 for every plugin (op1_sin_tab), read at the nearest point; every
   multiply 16 x 16 (one cycle); no branches; about 148 instructions a sample. **[I], [D] to confirm**
   Lesson for the next models: count multiplies by width, branches and table placement, not instructions.
   Measured (0.2.1, FM heaviest): synth 24%, pk 60 (66 with an effect). **[D]**
   GCC sometimes compiles a 16 x 16 multiply written in C as a sign extension plus a multi-cycle 32-bit multiply (the
   formant loop did); fm_mul16 now writes the one-cycle `(IS)` multiply in inline assembly. Rare paths (the formant's
   once-a-period restart) go in a noinline function behind `__builtin_expect`, so the per-sample path stays straight.
   Formant (0.3.0): about 170 instructions a sample, all six multiplies 16-bit. **[I]** Measured (Norn 0.4.1, formant
   heaviest): synth 25%, pk 50. **[D]**
   Waveshaper (0.3.1): the same treatment. Its 25 multi-cycle 32-bit multiplies are now 10 one-cycle 16-bit ones (the
   rising and falling slopes as unsigned 16 x 16, `fm_mulu16`); the corners moved to a noinline function; no branches
   in the per-sample path. Output unchanged against Plaits. **[I]** Measured (Norn 0.4.1, heaviest): synth 31%, pk 56
   (from 33% and 66). **[D]**
   Wave terrain (0.4.0): no division per sample after all. Every denominator in the terrain formulas is bounded, so
   each quotient became a 257-point table over its input's range; one terrain evaluation per sample (Plaits: two,
   oversampled). The block renders the path into a buffer, then each of the two terrains in its own branch-free loop
   (42 to 75 instructions), then mixes: about 220 instructions a sample at worst, all multiplies 16-bit. Expected 30 to
   35%. The plugin grows to 68 KB (Plaits' wavetables, 50 KB, read from SDRAM). **[I]** Measured (0.4.0, heaviest setting): synth 31%, pk 59. **[D]**
5. Measuring: on the device, the resource monitor (Shift+Help, T1) shows `synth` and `pk` (peak), as in Mouth's
   HARDWARE-TEST §10. **[S]** `op1_cycles()` reads the `CYCLES` register for counts per block. **[S]**
6. Mouth's §10 pass would give a baseline from another plugin. **[D]**

## 6. The four planned models, from source

| Model | Source | Per sample | Fixed-point risk |
|---|---|---|---|
| Waveshaper | `engine/waveshaping_engine.cc` (137 lines) | 2 PolyBLEP slope oscillators, 4 int16 table reads, 2 Hermite folds, 1 sine | **Low**: tables are already int16; no per-sample division |
| 2-op FM | `engine/fm_engine.cc` (123 lines) | 4x oversample × 3 phase-modulated sines + FIR decimator | **Low**; cost is mostly oversampling, likely to shrink at 88.2 kHz |
| Formant / grainlet | `engine/grain_engine.cc` + `grainlet_oscillator.h`, `z_oscillator.h` | 2 grainlets + Z oscillator + 2 DC blockers | **Medium**: a division per cycle reset (moves to a block reciprocal) |
| Wave Terrain | `engine2/wave_terrain_engine.cc` (235 lines) | oversampled, 2 terrain evaluations each with nested sines and **a division per sample** | **High**: needs a reciprocal table or a reworked terrain set; most expensive |

Shared needs: `NoteToFrequency` (replaced by `OP1_PITCH_INC`), `ParameterInterpolator` (block ramps),
`InterpolateHermite`, `Sine`/`SinePM` (the core's `op1_sin_q` table or our own), `resources.cc` tables (reconverted
to Q15).

## 7. Open items

1. Installed core version on the OP-1. **[D]**
2. Per-voice cycle cost of each model and real headroom under FX + tape. **[D]**
3. OUT only for now; AUX is unused until a later decision.
4. A plugin name: original, with credit to Mutable Instruments.

## 8. First prototype: Waveshaper

1. Smallest dependency set: one `Oscillator` (PolyBLEP), three tables already int16 or easily converted, no per-sample
   division.
2. Everything it needs (pitch increment to slope oscillator, Q15 table interpolation, block-ramped knobs) is
   reused by the other three models, so it builds the shared core.
3. FM is just as easy but overlaps with what Mouth already proved; Waveshaper teaches more that we haven't done.
4. Its knobs map cleanly: Harmonics = shape, Timbre = fold, Morph = slope/asymmetry.
