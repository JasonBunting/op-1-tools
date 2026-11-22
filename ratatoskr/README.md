# Ratatoskr

A synth engine for the original OP-1 running Hundekraft 3.3.1: overlapping grains read from the sampler's recording,
around a playhead that moves through it. Named for the squirrel that runs up and down the world tree: on the screen the
recording is Yggdrasil's trunk, the squirrel is the playhead, the eagle marks the crown and the dragon the roots.

Blue puts the squirrel somewhere in the recording; orange picks how it travels (hold, forward, reverse, bounce, wander,
jump). Slow travel stretches the sound out without changing its pitch. Each key plays the grains at its own pitch: C4
is the recording's own.

The idea comes from [GrainBrick](https://github.com/ETRNLIAM/GrainBrick), a granular instrument for the TrimUI Brick.
The 0.3 design follows [Ratatoskr-Design-Spec.md](Ratatoskr-Design-Spec.md). Hundekraft's own `granular`, `clouds` and
`hail` cover plain granular playback; Ratatoskr's difference is the moving playhead. See
[Ratatoskr or Hundekraft's granular?](#ratatoskr-or-hundekrafts-granular).

Status: 0.3.3, a redesign of 0.2 (a single motion knob instead of speed, drift, jumps, turns and chaos; grain shapes; a
tree screen). Host tests pass; not yet tried on the OP-1. The last 0.2 build is `build/versions/ratatoskr-0.2.2.knl`.
The [manual](manual/manual.html) still describes 0.2 and needs rewriting once 0.3 has been played.

## Ratatoskr or Hundekraft's granular?

Both play grains from the sound's own recording (up to 6 s, recorded with the input key while the engine is selected),
and in both C4 plays the recording at its own pitch. They differ in what moves:

| | Hundekraft's `granular` | Ratatoskr |
| --- | --- | --- |
| Where grains come from | One fixed point, until you turn it or an LFO does | A playhead that holds, travels, bounces, wanders or jumps through a range |
| Time-stretch and reverse | No | Yes, from forward, reverse and bounce at 0.05x–2x |
| Keys | Each key separate | One shared playhead: chords move together; a new phrase starts at the position |
| Grain shape | Six | Four: hann, triangle, pluck, swell |
| Pitch offset, sample gain | Page 2 | None |
| Grain size | About 10–370 ms | 12–250 ms |
| Overlap | 0.5x–4x | 0.5x–4x (Shift + green) |
| CPU | Lighter: hand-written Blackfin assembly | Plain C |
| Screen | Animated waves, one per knob | The recording as a tree, the squirrel, grain flashes |

## Install and first sound

1. In the workshop, add `build/ratatoskr.knl` under **Your plugins** and send it to the OP-1.
2. On a synth sound, pick **ratatoskr** (Shift + T1).
3. Record straight into it: press the **input key** (top right, microphone symbol), hold any keyboard key while you
   sing, speak or play into the mic, and let go. The sound stays on Ratatoskr.
4. Hold a key.

Record with Ratatoskr selected rather than in the sampler before switching: when a sound switches to Ratatoskr, the
core loads the sound's sample from the OP-1's disk, which can be an older recording than the one you just made.
Ratatoskr has no sample browser (a second press of the knob loaded a sample, too easily by accident). To use a saved sample, load it into the sound with the sampler engine first.

## Controls

Blue, green and orange each have a second page: press the knob until it clicks to switch, press again to switch back.
A doubled line over a label shows the knob is on page 2.

| Knob | Page 1 | Page 2 (press) |
| --- | --- | --- |
| Blue | **pos**: where the squirrel starts, 0–100% of the recording, shown in seconds; while playing it shows where the squirrel is | **range**: 5–100% of the recording around the position, where the squirrel may travel; slid inward at the ends to keep its size |
| Green | **size**: grain length, 12–250 ms, logarithmic | **shape**: hann, triangle, pluck, swell, with the curve drawn on the right |
| White | **scat**: scatter, each grain up to 0–500 ms either side of the squirrel | press: nothing |
| Orange | **motion**: hold, forward, reverse, bounce, wander, jump | **speed**: 0.05x–2x for forward, reverse and bounce (0.25x in the middle); 0.1–4 a second for wander and jump (1 in the middle) |

**Shift + green: overlap**, 0.5x–4x, shown on the green label while Shift is held. A new sound starts at 2x; turning
right goes 2.5x, 3x, 3.5x, 4x, then 0.5x, 1x, 1.5x. The order wraps because the core gives every new sound step 0 of a
Shift setting, and Ratatoskr can't take Shift turns itself (the core refuses the engine input hook on engines that use
a sample).

The motions:

1. **Hold:** the squirrel stays put; grains keep coming from that spot.
2. **Forward:** it climbs to the top of the range and starts again at the bottom.
3. **Reverse:** it climbs down and starts again at the top.
4. **Bounce:** it turns round at each end.
5. **Wander:** it glides to a nearby spot (within 5% of the range), then picks another.
6. **Jump:** it leaps to a random spot in the range and stays there until the next leap.

The playhead is shared by every key. The first key after all keys are up starts it at the position; more keys join it
where it is; it stops when the last key goes up. Turning blue moves it to the new position at once, and changing the
motion carries on from where it is. Grains keep the size and shape they were launched with.

## Screen

1. **The tree:** the recording runs up the trunk from the roots (its start) to the crown (its end); each row is as wide
   as that slice's loudest moment. The range is bright, the rest dim. The eagle sits in the crown and the dragon at the roots.
2. **The squirrel:** at the playhead, right of the trunk with a line pointing at it; tilted up or down while it
   travels.
3. **Grain flashes:** white dashes left of the trunk where grains were just launched.
4. **The shape:** with green on page 2, the grain's curve on the right.
5. **Bottom:** each knob's name and value in its colour.

With nothing recorded on the sound, the trunk is missing, the screen says **no sample** and the keys are silent.

## Testing on the OP-1

See [HARDWARE-TEST.md](HARDWARE-TEST.md).

## For developers

| File | What |
| --- | --- |
| `plugin/ratatoskr.c` | The OP-1 glue (voices, phrases, the shared playhead, page 2, the overlap record) and the screen. |
| `plugin/rt_dsp.h` | The controls' laws, the route, the motions, the grain shapes and the grains. Integer only, no OP-1 calls. |
| `plugin/ratatoskr.svg` | The eagle, dragon and squirrel that `ratatoskr.c` places. |
| `plugin/test/host_test.c` | Host test: the laws, the route, every motion, phrases, the shapes, grains inside the recording, pitch, loudness; writes WAVs. |
| `manual/` | The manual (`manual.html`, its style and key images; screenshots go in `img/`). Still 0.2. |
| `HARDWARE-TEST.md` | The device test. |
| `Ratatoskr-Design-Spec.md` | The 0.3 design. |

Build with `tools/build.sh ratatoskr` (makes `build/ratatoskr.knl`). Host test, in WSL from the repository root (WAVs
go to `build/`):

```bash
gcc -O2 -Wall -I ratatoskr/plugin ratatoskr/plugin/test/host_test.c -lm -o /tmp/rt_test && /tmp/rt_test build
```

The playhead moves once per audio block: the voice loop renders voices in order, so a voice whose number is not above
the last one rendered starts a new block. A voice whose key-up never came (a steal) is dropped from the held keys once
it stops rendering. Each voice has eight grains, read with linear interpolation through a 256-step window table; a
grain that would run past the recording at a high key is shortened, and the launch rate stays that of the set size.

## Later, maybe

1. Grain reverse (a chance per grain of reading backwards), from the spec.
2. A light WSOLA-style alignment for smoother stretching, if slow speeds sound rough on the device.
3. Matched loudness between the shapes, after listening.
