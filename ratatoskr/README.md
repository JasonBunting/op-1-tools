# Ratatoskr

A synth engine for the original OP-1 running Hundekraft 3.3: a cloud of grains read from the sampler's recording,
around a playhead that scurries through it. Named for the squirrel that runs up and down the world tree.

Record something with the stock sampler engine, switch the same sound to Ratatoskr, and the recording becomes a
texture. The playhead moves through a region of the recording at any speed from frozen to 4x, forwards or backwards,
so slow speeds stretch the sound out without lowering its pitch. With **wander** up it drifts, jumps and turns round
by itself. Each key plays the cloud at its own pitch: C4 is the recording's own.

The idea comes from [GrainBrick](https://github.com/ETRNLIAM/GrainBrick), a granular instrument for the TrimUI Brick.
Hundekraft's own `granular`, `clouds` and `hail` cover plain granular playback; Ratatoskr's difference is the moving
playhead.

Status: 0.1, not yet tried on the OP-1. There is no manual yet.

## Install and first sound

1. In the workshop, add `build/ratatoskr.knl` under **Your plugins** and send it to the OP-1.
2. On a synth sound, pick the **sampler** engine (Shift + T1) and record a few seconds.
3. Pick **ratatoskr** for the same sound (Shift + T1). Hold a key.

## Controls

Blue, green and white each have a second page: press the knob until it clicks to switch, press again to switch back.
The bottom row of the screen shows which page each knob is on.

| Knob | Page 1 | Page 2 (press) |
| --- | --- | --- |
| Blue | **position**: the middle of the region the playhead roams | **spray**: grains scattered up to 1.5 s around the playhead |
| Green | **size**: grain length, 10 ms to 500 ms | **width**: the region, from nothing (the playhead sits on the position) to the whole recording |
| White | **speed**: frozen in the middle, forwards to the right, backwards to the left, up to 4x; 1x is three quarters of the way out | **wander**: 0–100%: drift around the playhead, plus jumps (up to about 0.6 a second) and turns (up to about 0.8 a second) |
| Orange | **density**: grains overlapping 0.5x to 4.4x | press: the sampler's sample browser, to load a different sample into the sound |

The playhead is shared by all the keys you hold and only moves while something plays, so the next note carries on
where the last one left off. At the region's ends it wraps round. A new sound starts at the middle of the recording,
with half of it as the region, slowly moving forwards (0.25x), 100 ms grains at 2x density and a little spray.

## Screen

1. **Top:** the recording as columns; the region's part is white, the rest grey. A white line is where the grains are
   read from right now.
2. **Below it:** a green bracket marks the region, a blue arrow the playhead, and red ticks the grains that are
   sounding (one row per voice).
3. **Bottom:** each knob's name and value. A knob on page 2 shows its page-2 name and a second line.

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
