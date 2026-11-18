# Ratatoskr

A synth engine for the original OP-1 running Hundekraft 3.3: a cloud of grains read from the sampler's recording,
around a playhead that scurries through it. Named for the squirrel that runs up and down the world tree.

Record something into a sound running Ratatoskr, and the recording becomes a texture. The playhead moves through a region of the recording at any speed from frozen to 4x, forwards or backwards,
so slow speeds stretch the sound out without lowering its pitch. It can drift, jump, turn round and bounce between the
region's ends by itself. Each key plays the cloud at its own pitch: C4 is the recording's own.

The idea comes from [GrainBrick](https://github.com/ETRNLIAM/GrainBrick), a granular instrument for the TrimUI Brick.
Hundekraft's own `granular`, `clouds` and `hail` cover plain granular playback; Ratatoskr's difference is the moving
playhead. See [Ratatoskr or Hundekraft's granular?](#ratatoskr-or-hundekrafts-granular) to pick between them.

Status: 0.2.0. Playing well on the OP-1 so far; saving and the new Shift settings are not yet checked there. The
[manual](manual/manual.html) covers playing it; this README is the short version, plus building and testing.

## Ratatoskr or Hundekraft's granular?

Both are synth engines that play grains from the sound's own recording (up to 6 s, recorded with the input key while
the engine is selected), both open the core's sample browser, and in both C4 plays the recording at its own pitch.
They differ in what moves:

| | Hundekraft's `granular` | Ratatoskr |
| --- | --- | --- |
| Where grains come from | One fixed point (blue), until you turn it or an LFO does | A playhead travelling through a region, frozen to 4x, forwards or backwards; wrap or bounce at the ends |
| Time-stretch and reverse | No | Yes, from the playhead's speed |
| Movement on its own | No | Drift, jumps, turns and chaos |
| Keys | Each key separate | One shared playhead: chords move together, and each note carries on where the last left off |
| Grain shape | Six: hann, gauss, tukey, tri, pluck, swell | Hann only |
| Pitch offset | ±24 semitones (page 2) | None; use the keys and octave |
| Sample gain | 0–4x (page 2), for quiet recordings | None |
| Detune | Spray adds a little random detune per grain | None |
| Grain size | About 10–370 ms | 10–500 ms |
| Density | 0.5x–4x | 0.5x–4.4x |
| Grains per key | 4 | 6 |
| CPU | Lighter: hand-written Blackfin assembly | Plain C; fine in practice (six keys at full density tested) |
| Screen | Animated waves, one per knob | The recording, the region, the playhead and live grains |

Choose `granular` to shape grains from one spot: percussive pluck or swell grains, a pitch or gain page, or the
lightest load. Choose Ratatoskr for textures that travel through a recording, stretch it out or run it backwards, and
keep changing while you hold a note.

## Install and first sound

1. In the workshop, add `build/ratatoskr.knl` under **Your plugins** and send it to the OP-1.
2. On a synth sound, pick **ratatoskr** (Shift + T1).
3. Record straight into it: press the **input key** (top right, microphone symbol), hold any keyboard key while you
   sing, speak or play into the mic, and let go. The sound stays on Ratatoskr.
4. Hold a key.

Record with Ratatoskr selected rather than in the sampler before switching: when a sound switches to Ratatoskr, the
core loads the sound's sample from the OP-1's disk, which can be an older recording than the one you just made.
Orange's press opens the sampler's sample browser, to load a saved sample instead.

## Controls

Blue, green and white each have a second page: press the knob until it clicks to switch, press again to switch back.
The bottom row of the screen shows which page each knob is on.

| Knob | Page 1 | Page 2 (press) | Shift + turn |
| --- | --- | --- | --- |
| Blue | **start**: where the region the playhead roams begins | **spray**: grains scattered up to 1.5 s around the playhead | **jumps**: 0–100%, leaps to random places, up to about 0.6 a second |
| Green | **size**: grain length, 10 ms to 500 ms | **width**: how far the region reaches right of the start, from nothing (the playhead sits on the start) to the whole recording; the start stays put and only the end moves, stopping at the recording's end | **turns**: 0–100%, reversals, up to about 0.8 a second |
| White | **speed**: frozen in the middle, forwards to the right, backwards to the left, up to 4x; 1x is three quarters of the way out | **drift**: 0–100%, the read point wobbling around the playhead, up to a quarter of the region | **edges**: wrap or bounce (ping-pong) at the region's ends |
| Orange | **density**: grains overlapping 0.5x to 4.4x | press: the sampler's sample browser, to load a different sample into the sound | **chaos**: 0–100%, jumps and turns in bursts and lulls (the same rate on average) |

Shift + turn settings move in steps (10% for the percentages), are saved with the sound in the engine's record, and
show across the top of the screen.

The playhead is shared by all the keys you hold and only moves while something plays, so the next note carries on
where the last one left off. At the region's ends it wraps round, or reverses with edges set to bounce. A new sound's region starts a quarter of the way in
and covers half the recording (its middle half), slowly moving forwards (0.25x), 100 ms grains at 2x density and a little spray.

## Screen

1. **Very top:** the Shift + turn settings, such as `jumps 30%  turns 20%  bounce  chaos 50%`.
2. **Top:** the recording as columns; the region's part is white, the rest grey. A white line is where the grains are
   read from right now.
3. **Below it:** a green bracket marks the region, a blue arrow the playhead, and red ticks the grains that are
   sounding (one row per voice).
4. **Bottom:** each knob's name and value. A knob on page 2 shows its page-2 name and a second line.

With nothing recorded on the sound, the screen says "record a sample first" and the keys are silent.

## Testing on the OP-1

See [HARDWARE-TEST.md](HARDWARE-TEST.md).

## For developers

| File | What |
| --- | --- |
| `plugin/ratatoskr.c` | The OP-1 glue (voices, the shared playhead, page 2, the sample browser) and the screen. |
| `plugin/rt_dsp.h` | The playhead and the grains. Integer only, no OP-1 calls. |
| `plugin/ratatoskr.svg` | The screen's SVG: empty, since `ratatoskr.c` draws everything. |
| `plugin/test/host_test.c` | Host test: the knob laws, the playhead, grains inside the recording, pitch, loudness; writes WAVs. |
| `manual/` | The manual (`manual.html`, its style and key images; screenshots go in `img/`). |
| `HARDWARE-TEST.md` | The device test. |

Build with `tools/build.sh ratatoskr` (makes `build/ratatoskr.knl`). Host test, in WSL from the repository root (WAVs
go to `build/`):

```bash
gcc -O2 -Wall -I ratatoskr/plugin ratatoskr/plugin/test/host_test.c -lm -o /tmp/rt_test && /tmp/rt_test build
```

The playhead moves once per audio block: the voice loop renders voices in order, so a voice whose number is not above
the last one rendered starts a new block. Each voice has six grains with Hann windows, read with linear interpolation.
The playhead's region uses 8-sample steps.

## Later, maybe

A light WSOLA-style alignment for smoother stretching, if slow speeds sound rough on the device.
