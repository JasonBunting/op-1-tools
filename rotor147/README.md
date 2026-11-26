# rotor147 (working name)

A rotary speaker effect for the original OP-1 running Hundekraft 3.3.2, inspired by the Motion Sound R3-147: a horn
and a bass rotor with their own speeds and inertia, a driven preamp, and close or distant microphones. Not a Motion
Sound product. `rotor147` is a development name and plugin ID, not a final name; keep the ID once sounds are saved with
it.

**Status: milestone 2 (voicing), 0.2.2.** 0.1.0 passed its first device test (FX 11% on the resource monitor). 0.2.0
added a second page on blue, green and white, more bass throb and a wider master image; 0.2.1 added its own screen so
page 2 can be seen; 0.2.2 shows the microphones and makes contour open the default. Host tests pass, not yet on the OP-1 (see [Validation](#validation)). The design brief is
[research/r3-147/README.md](../research/r3-147/README.md).

## Controls

Push blue, green or white to switch that knob to its second page; push again to come back. Shift + turn is the core's
fine adjustment, as on any knob.

The screen (drawn in code, no art yet): on the left a row per knob in its colour with its name and value; a knob on page
2 shows its page-2 name and value (**speed** as a multiplier, **spin-up** as the horn's slow-to-fast time, **contour**
in kHz) and a second stripe. On the right the horn and the bass drum turn with the live rotors, each with its speed in
Hz, and the microphones are green marks around them (the left ones on a synth or the drums, all four on the master). A
mark grows with what its microphone hears: its share of the mics blend times how directly the rotor faces it, so a
rotor braked facing away shows short marks, and sounds quieter.

| Knob | Page 1 | Page 2 (push) |
| --- | --- | --- |
| Blue | **Motion** in three zones: brake (first third), slow, fast (last third). The rotors speed up and slow down at their own pace; the bass takes longest. Default slow | **Speed:** both rotors' slow and fast speeds, ×0.25 (left) to ×4 (right); centre is the normal speed. Default centre |
| Green | **Mics:** close and pronounced (left) to distant and blended (right). Default 25% | **Acceleration:** how quickly the rotors change speed: every ramp ×5 slower (left) to ×0.1, nearly instant (right). Default centre |
| White | **Drive:** clean (left) to growling (right), with level make-up. Default clean | **Contour:** a high-cut before the drive, 1 kHz (left) to open (right). Default open (fully right; the last half-step of the knob is open, the input untouched) |
| Orange | **Mix:** dry (left) to fully rotary (right). Default fully rotary | none |

The R3-147 has its own speed and acceleration controls for each rotor and a contour before the tube; here speed and
acceleration move both rotors together (they keep their ratio), and both reach well past a real cabinet. Sounds saved
with 0.1.0 have no page-2 values: if one plays very slowly and dark, set the three page-2 knobs to their centres
(contour fully right, open).

On a synth or the drums the effect is mono: one side's close and distant microphones. As the master effect it's stereo:
the cabinet is fed from (L + R) / 2 and heard through a left and a right microphone pair, so at full mix the original
stereo image is replaced by the cabinet's. A signal that is opposite in the two channels cancels at that sum.

## How it works

Everything is in [plugin/rc_dsp.h](plugin/rc_dsp.h); its header comment has the signal path. In short: the contour
high-cut, drive (gain, a soft knee, a little asymmetry, make-up), a 10 Hz DC block, then a 4th-order Linkwitz-Riley
split at 800 Hz. The highs go to the horn: four microphone taps on one short delay line, each with a delay, a level
and a brightness that follow the horn's angle (the delay's slope is the Doppler pitch shift). The lows go to the bass
rotor: one tap per side with a gentler delay swing and level swing. All integer maths, no division in the audio loop.

### Starting values

Chosen by ear and reasoning, not measured from an R3-147.

| | Horn | Bass |
| --- | --- | --- |
| Slow / fast (speed ×1) | 0.8 / 6.7 Hz | 0.7 / 5.5 Hz |
| Slow to fast, fast to slow, to brake (acceleration ×1) | 1.0, 1.5, 2.0 s | 3.0, 4.0, 5.0 s |
| Close mics | ±60°, 2.0 ms ± 0.3 ms, level 0.44–1.0, brightness 3 kHz pole to open | ±75°, 2.0 ms ± 0.1 ms, level 0.5–1.0 |
| Distant mics | ±90°, 2.15 ms ± 0.2 ms, level 0.70–0.94, 6 kHz pole to near open | 2.15 ms ± 0.07 ms, level 0.75–1.0 |

The wet signal gets +1.9 dB to make up for the rotors' average level loss. 0.1.0 had the close horn mics at ±45°,
the distant at ±60°, the bass at ±45° with level 0.7–1.0 (close) and 0.85–1.0 (distant), and no make-up.

A ramp takes the listed time from wherever the speed is, so an interrupted change restarts the clock from the current
speed. Speeding up uses the slow-to-fast time (also brake to slow), slowing the fast-to-slow time, stopping the brake
time. Turning page-2 speed while the rotors spin is a new target, reached at the acceleration's pace. When the effect is first loaded the rotors start at the knob's speed
without a ramp.

### Changes from the brief

1. **Crossover:** a 4th-order Linkwitz-Riley split instead of the one-pole split. With the one-pole split, the horn
   and bass stopping at different angles cancelled each other near 1 kHz, down to −21 dB on one side at brake; with
   this split the worst brake level is −7 dB. It costs three state-variable filters a sample.
2. **Distant mics are 0.15 ms further away, not 0.5 ms.** 0.5 ms put the blend's first comb notch at 1 kHz.
3. **Drive make-up** holds a −12 dBFS signal at its level at any drive; quieter input gets louder with drive, hotter
   input quieter (table in the code).
4. **The bass has delay modulation as well as level modulation** from the start (no AM-only stage).
5. **0.2.0: wider mics, deeper bass throb.** Your call for more stereo separation on the master and more throb. A
   bigger bass pitch swing (0.2 ms) was tried and dropped: it dug a −15 dB hole near 1 kHz at one brake angle.

## Validation

Reported separately, as the brief asks.

1. **Host tests (done, all pass)**, with address and undefined-behaviour sanitizers and a bounds check on every delay
   read:
   1. silence stays exact zeros; after loud noise the output returns to exact zeros (no fixed-point limit cycles);
   2. impulse, full-scale DC both ways, full-scale square edges and noise at every motion, mic and drive corner, mono
      and stereo: no faults;
   3. mix 0 is bit-exact dry in mono and stereo (no swap, no crosstalk) and returns to bit-exact after a sweep; full
      wet has no dry feed;
   4. rotor rates 6.7000 / 5.5000 Hz fast, 0.8000 / 0.7000 Hz slow; ramp times within 1.3% of the table, including an
      interrupted ramp; the phase never jumps;
   5. page 2: speed ×0.25 / ×1 / ×4 gives 0.2 / 0.8 / 3.2 Hz slow and 1.68 / 6.70 / 26.80 Hz fast on the horn, the bass
      in step; acceleration ×5 / ×1 / ×0.1 gives slow-to-fast in 4.99 / 1.00 / 0.10 s (horn) and 15.1 / 3.0 / 0.30 s
      (bass); fastest speed with the quickest ramps and contour swept on full-scale noise: no faults;
   6. brake at eight stop angles: lowest level −7.8 dB, widest left/right difference 6.1 dB (1 kHz and 200 Hz tones);
   7. three positions keep separate state; re-initialising one leaves the others bit-identical;
   8. master: opposite-polarity input cancels (documented), left-only input reaches both microphone pairs;
   9. sweeping each knob and each page-2 setting during an organ chord makes no click spikes; mid-way mix settings
      comb-filter a little, as dry and delayed wet always will;
   10. mics 0–100% changes loudness by 1.3 dB; the mono fold-down of the master is within 0.3 dB of the stereo
       channels;
   11. drive 0 passes samples up to half scale unchanged.
2. **Listening renders (done):** `build/rotor147/`, made by the host test from the integer DSP. `raw/` is the output
   as is, `matched/` is level-matched to the dry source, and `settings.txt` lists every file's settings and level.
   Sources are generated (an organ and a synth chord); no vocal sample yet. Files 7 to 11 show page 2: speed ×4 and
   ×0.25, quick and slow acceleration, a dark contour.
3. **Target build (done):** `build/rotor147.knl` 16.0 KB with the screen (code 12.6 KB, data 5.8 KB: state 3.5 KB for
   the three positions, 1,172 bytes each, and the screen's text objects), no warnings; the only imports are the core's
   FX-page flags and knob reader, for the screen. Stack: about 200 bytes in the master callback
   (0.1.0 measured). The audio loop has no helper calls; the only divisions are the SDK's knob scaling (four a block)
   and one per rotor when its target speed or acceleration changes.
4. **CPU cost (no emulator count):** the cost harness the brief names (`harness/tier2`, the bcore emulator) isn't in the
   Hundekraft source snapshot or on this machine, so there's no Blackfin cycle count. The target is about 10–12% of a
   block for the stereo master; the OP-1's resource monitor showed 11% for 0.1.0 (item 5). See
   [HARDWARE-TEST.md](HARDWARE-TEST.md).
5. **Hardware (2026-11-25, 0.1.0):** loads and sounds right on a synth, the drums and the master. Resource monitor
   with tape, synth chord and drums playing: FX 11%, peak 52%. Saving and restart passed. 0.2.2: pending.

## Files

| Path | What |
| --- | --- |
| `plugin/rc_dsp.h` | The DSP: rotors, drive, crossover, horn and bass taps, mix. Shared by the plugin and the host test. |
| `plugin/rotor147.c` | The SDK adapter: knobs, positions, the mono and stereo callbacks, and the screen. |
| `plugin/rotor147.svg` | The screen's SVG, empty for now: everything is drawn in code. |
| `plugin/test/host_test.c` | Host tests and the listening renders. |
| `HARDWARE-TEST.md` | The device test. |

## Build and test

Build with `tools/build.sh rotor147` (makes `build/rotor147.knl`). Host test and renders, in WSL from the repository
root:

```bash
mkdir -p build/rotor147/raw build/rotor147/matched
gcc -O2 -Wall -fsanitize=address,undefined -DRC_CHECK -I rotor147/plugin rotor147/plugin/test/host_test.c -lm -o /tmp/rc_test && /tmp/rc_test build
```
