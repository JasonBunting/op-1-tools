# R3-147 rotary effect technical handoff

Prepared for Jason and Claude on 2026-11-25. Status: proposed implementation brief; no rotary plugin has been built or benchmarked yet.

UI direction and prototype: [screen design, interactive vector source and validation](ui/README.md). Jason selected A's corner treatment and the short scoop horn, with the horn above-left and one microphone below-right in the open center. The mic moves diagonally to indicate proximity. No cabinet or green proximity dashes. The browser prototype demonstrates rotation and clearance; firmware screen work remains a later milestone.

Build a standalone effect for the original OP-1 running Hundekraft, inspired by the Motion Sound R3-147. The first deliverable is a playable rotary prototype with listening examples and measured target performance. Prioritize convincing motion, separate horn and bass inertia, and a useful driven sound. Exact replication requires reference recordings or measurements from a physical R3-147.

## Start here

1. Read the repository's `AGENTS.md`, `CLAUDE.md`, and this brief. Preserve existing uncommitted work. The working tree already contains unrelated changes.
2. Verify the installed Hundekraft version and matching SDK. `CLAUDE.md` currently identifies 3.3.2; older README references elsewhere are not sufficient evidence of the device version.
3. Implement the host DSP and reproducible audio renders described below, then package a minimal effect and measure its Blackfin cost. Do this before custom artwork or secondary parameter pages.
4. Present the sound examples, implementation, measurements, and remaining device checks to Jason. Host tests, emulation, and hardware results must be reported separately.

The research brief lives here until implementation starts. Proposed working project and plugin ID: `rotor147`. This is a development name, not an approved final product name. Check for collisions before using it, and keep the ID stable once Jason saves sounds with it.

## What makes the reference distinctive

The R3-147 combines a tube preamp, a real rotating treble horn captured by four microphones, and an electronic bass rotor simulation. The two microphone pairs offer close/distant blending. Upper and lower rotors have independent speed and acceleration/deceleration controls. These are the features to translate into DSP.

Craig Anderton's February 1998 hands-on review describes the bass simulation as **analog**, a 12AX7 preamp, mono input, stereo output, and a pre-tube high-cut contour control. Avoid calling the original bass section digital. The manufacturer advertised the R3-147 as new in July 1996; a unit manufactured around 2000 is consistent with an earlier design.

Source references:

- [Motion Sound advertisement in Electronic Musician, July 1996, printed page 52](https://www.worldradiohistory.com/Archive-All-Music/Electronic-Musician/1996/Electronic-Musician-1996-07.pdf).
- [Craig Anderton's hands-on review in EQ, February 1998, printed page 128](https://www.worldradiohistory.com/Archive-All-Audio/EQ-Magazine/EQ-1998-02.pdf).

The topology and controls above are documented. The crossover frequency, modulation depths, exact speeds, filter responses, microphone geometry, and tube transfer curve below are engineering starting points, not measured R3-147 specifications.

## Verified local foundation

The following paths are reference material. Do not modify either reference repository to build this plugin.

| Reference | Use |
| --- | --- |
| `D:\src\hundekraft-source\PLUGIN_GUIDE.md` | Standalone plugin workflow, descriptor contracts, compatibility and licensing. |
| `D:\src\hundekraft-source\ext\kennel\synth_sdk\op1_plugin.h` | FX sample rate, block size, stereo descriptor, parameter normalization and per-position state. |
| `D:\src\hundekraft-source\ext\kennel\synth_sdk\template\fx\myfx.c` and `plugin.json` | Minimal FX entry points and manifest. |
| `D:\src\hundekraft-source\ext\kennel\synth_sdk\plugins\rackverb\rv_quad.c` | `p_leslie`: one-pole band split, horn delay modulation per microphone, horn/bass amplitude modulation. Its bass path is AM-only. |
| `D:\src\hundekraft-source\ext\kennel\synth_sdk\plugins\pew\pew.c` and `pew_macro.h` | Existing musical rotary behavior and brake/slow/fast mapping; this is a synth, not an arbitrary-input FX implementation. |
| `D:\src\hundekraft-source\ext\kennel\synth_sdk\plugins\juni_chorus\` | Stereo FX packaging, fixed-point moving delays, mono behavior, and documented cost tradeoffs. |
| `D:\src\hundekraft-source\ext\kennel\synth_sdk\kennel\kn_exports.c` | Exact services exported by the local source snapshot. |
| `D:\src\op1-workshop\manual.html` | Installation and user-facing firmware behavior. |
| `D:\src\op-1-tools\tools\build.sh` and `tools\pack-plugin.sh` | Existing standalone build and staging flow. |

CodeMap is configured for the repository's .NET projects. The rotary DSP and SDK references are C and assembly; use ordinary file tools for these. If changing any .NET code, follow the repository's CodeMap session instructions.

## Scope of the first prototype

Implement a two-band rotary effect with independent rotor phases and speeds, smooth brake/slow/fast transitions, horn Doppler and directivity, subtler bass motion, pre-rotary drive, mic perspective, and dry/wet mix. Support synth, drum, and master positions with independent state.

Use the stock parameter presentation initially. Defer custom animation, MIDI speed control, selectable cabinet models, separately adjustable microphone channels, room reverb, and exhaustive hardware control replication. Secondary controls can follow after the DSP is useful and fits the budget.

No core patches, firmware rebuild, or changes to existing plugins should be needed. Produce an ordinary `.knl` with matching SDK support.

## Routing and SDK contract

The SDK defines FX blocks as 64 signed 16-bit samples at 44,100 Hz. A block lasts approximately 1.451 ms. Synth voice rendering uses a different rate; do not copy voice-rate constants from Pew into this effect.

- Use `op1_fx_stereo_t` with `stereo: true` in the manifest. Supply `init`, mono `render`, and `render_stereo` callbacks.
- Positions 0 and 1 are synth and drum: the firmware calls the mono callback. Position 2 is master: it supplies independent left and right buffers to the stereo callback.
- Normalize primary knob words with `op1_fx_param_q15`; do not assume the raw words span 0 to 32767.
- Keep all mutable DSP state separate for each position. Clear filters and delay history on `init`. Check scratch alignment and bounds against `OP1_FX_SCRATCH_BYTES` if using the position's scratch buffer.
- The audio callback uses integer DSP, bounded work, and no allocation or libc calls. Avoid floating point and per-sample division.

For the first prototype, the master wet path uses `mono = (left + right) / 2`, with widened arithmetic, feeding one modeled cabinet and producing stereo microphone outputs. Preserve the original left and right signals separately in the dry path. This intentionally models a mono-input cabinet: at full wet, the original stereo image is replaced by the simulated cabinet image. Opposite-polarity input can cancel at the mono sum; document and test this behavior.

At synth/drum positions, render one microphone perspective with its near/far blend. Do not automatically average two opposing microphone outputs: that can erase the pitch movement. A mono insert therefore need not equal the fold-down of the stereo master version. Separately check that the master output remains musically usable when folded to mono.

Mix 0 must return the original input samples exactly, including stereo channels. Mix 100 percent must contain no intentional dry feed. Continue advancing phases, delay history and speed ramps while mix is zero so reintroducing the effect is smooth. Firmware bypass behavior is separate and must be observed on target.

## Proposed DSP

Conceptual wet signal path:

```text
input or master mono sum
  -> fixed contour filtering -> drive -> band split
       low  -> bass modulation ---------------------------+
       high -> horn delay, level and brightness per mic --+-> mic/rotor mix -> wet output
original dry input -------------------------------------------------------> dry/wet mix
```

### Drive and band split

Use a modest fixed pre-drive high-cut contour and a bounded soft saturation curve with output compensation. Drive zero should be effectively clean. A table or piecewise polynomial is suitable; document its input range, intermediate widths and headroom. Compare driven high notes for aliasing before deciding whether oversampling is worth its cost. Oversampling is not required for milestone one.

Start the split near 800 Hz as a tuning assumption. A complementary one-pole split (`high = input - low`) is a cheap starting point consistent with the existing Rackverb implementation. Preserve headroom for the subtraction. A steeper crossover is an optional later improvement if the simple version sounds muddy; it needs its own phase and recombination checks.

### Rotor motion

Maintain separate horn and bass phase accumulators, actual speeds, and target speeds. Use unsigned phase wrap intentionally. Changing mode changes target speed without resetting phase or filter history.

Starting values for listening and profiling:

| Parameter | Horn | Bass |
| --- | --- | --- |
| Slow rotation | 0.8 Hz | 0.7 Hz |
| Fast rotation | 6.7 Hz | 5.5 Hz |
| Slow to fast transition | 1.0 s | 3.0 s |
| Fast to slow transition | 1.5 s | 4.0 s |
| Fast to stopped transition | 2.0 s | 5.0 s |

These values are chosen to make the different inertia audible; they are not a factory preset. Use a bounded linear speed ramp initially, with duration defined as the time from the current speed to the new target. A transition interrupted by another command starts from the current actual speed. Update speed at block boundaries and interpolate within the block if needed. Accumulate phase using the evolving speed rather than jumping to the target increment.

At brake, reach zero speed and hold the final phase. Audio still passes through the stationary cabinet model. Do not mute at brake or force the horn back to a fixed angle. Test several stop angles for excessive attenuation and uneven stereo levels.

### Horn and microphones

Implement Doppler as fractional reads from a short delay line. Derive delay, amplitude, and brightness from a common rotor angle and microphone geometry so their relationship remains coherent. The delay slope produces the pitch change; do not add a second unrelated vibrato on top.

Start with a 2 ms mean delay and approximately 0.2 to 0.4 ms peak delay excursion for the close perspective. These are adjustable development constants. Keep every read safely behind the write head, including interpolation neighbors. Choose and document whether processing is sample-by-sample or block-based; a tap shorter than one block cannot use a read-whole-block-before-write algorithm without additional handling.

The close perspective should have stronger level and brightness variation. Use a smooth directional gain with a nonzero floor, plus inexpensive angle-dependent high-frequency attenuation. Keep coefficients in a stable range and interpolate changes. Begin with linear fractional interpolation, then evaluate higher-order interpolation only if listening or spectra justify it.

Represent the four microphones as near/far taps for each stereo side, sharing horn delay history where possible. Begin with a left/right angular separation around 90 degrees, with reduced modulation/directivity and a small additional delay for the distant perspective. The physical rack's distant pair is still inside the enclosure: do not turn it into a long room reverb. Assign each path a documented phase and gain relationship.

Green blends near to far with bounded, normalized gains. Test correlated blending for loudness changes and cancellation. If four fully independent brightness filters exceed the cost budget, try shared filtering or a cheaper approximation, record the compromise, and compare renders before keeping it.

### Bass rotor

Use an independent bass phase and speed, moderate amplitude modulation, and subtler delay modulation than the horn. This extends the existing Rackverb bass path toward the reference's adjustable AM/FM character. Preserve bass weight; use a shallower gain excursion than the horn and avoid deep periodic nulls. A first bring-up may use AM alone, but identify it as a temporary simplification in the listening report.

### Output and control smoothing

Use bounded gains, adequate internal headroom, and final saturation to int16. Avoid accidental clipping at crossover recombination or mic blending. Prefer a linear dry/wet crossfade initially; delayed wet and dry are correlated, so an equal-power law is not automatically appropriate. Check intermediate mixes for comb filtering and unexpected gain jumps.

Smooth mic perspective, drive gain and mix over roughly 10 to 30 ms, independently of rotor inertia. Keep exact settled dry and wet endpoints. State smoothing must not introduce DC offsets or persistent low-level oscillation.

## Controls

| Knob | Primary control | Prototype default |
| --- | --- | --- |
| Blue | Motion: brake, slow, fast in three ordered zones | Slow |
| Green | Mics: close and pronounced to distant and blended | 25 percent toward distant |
| White | Drive: clean to growling, with level compensation | Clean |
| Orange | Mix: dry to fully rotary | Fully wet |

Use normalized blue thresholds 10923 and 21845 for the initial three zones. Smooth the resulting motor speeds, not the mode selector. If the verified target core supports the SDK's three-step FX encoding, it can provide one detent per mode; preserve the same saved parameter meaning.

Candidate secondary controls are rotor balance, ramp duration, and contour. Do not implement them by assuming four independent extra words: the SDK's ordinary FX page mechanism only provides second-page words for blue, green and white. Newer shift/push services have their own packed-word rules. Re-read the matching header and exports before adding either mechanism. Defaults and preset round trips require tests once secondary controls are introduced.

## Performance and memory

The first performance target is at most roughly 10 to 12 percent of a block for one stereo instance in the matching firmware's calibrated cost model. This is a provisional design budget, not a guarantee of whole-system headroom. The local JUNI chorus README reports 22,978 executed packets and 10.98 percent under its model; those figures cannot be transferred directly to a different harness or treated as hardware timing.

Measure steady rendering, initialization, mode changes, parameter sweeps, silence, and worst-case driven input. Check mono and stereo separately, then concurrent instances with a demanding synth, drum activity and tape playback. Include screen cost when a custom screen is eventually added.

The referenced `harness/plugin_cost.py`, `harness/cost_check.py`, and `harness/op1_host.h` were not found in the inspected source snapshot. Locate a compatible local emulator/harness or obtain the matching tooling before claiming a CPU percentage. Desktop execution time is not a Blackfin benchmark. If target tooling is unavailable, finish host correctness and renders, report CPU fit as unverified, and give Jason a concrete hardware test using the firmware's resource monitor.

Keep state compact and report bytes per position, total static storage, scratch usage and maximum stack requirements. Short rotary delays should not need large buffers. Avoid per-sample 64-bit arithmetic where a proven 32-bit formulation suffices; use wider setup calculations when needed and check generated target code for expensive helpers.

## Implementation layout and build

When implementation begins, create the usual top-level project layout:

```text
rotor147/
  README.md
  HARDWARE-TEST.md
  plugin/
    plugin.json
    LICENSE
    NOTICE
    rotor147.c
    rotor147_dsp.h
    test/
      host_test.c
```

Keep portable integer DSP independent of firmware addresses. The adapter handles normalized controls, positions and SDK callbacks; the host test invokes the same DSP. Do not create a separate approximate test implementation. Add files as needed rather than treating these proposed filenames as an API.

Use `kind: "fx"`, `stereo: true`, `loadable: true`, `order: 0` or omitted, `claims: []`, a matching descriptor symbol, and an explicit author/version in the manifest. Use `op1_fx_stereo_t` in the source. Copy licensing terms and preserve attribution from any reused implementation: plugins here use GPL-3.0-or-later with the OP-1 firmware permission. Do not copy another plugin's cost metadata as this plugin's measurement.

From Git Bash or WSL at the repository root, after the project exists:

```bash
tools/build.sh rotor147
```

Expected output: `build/rotor147.knl`. The existing build wrapper stages the plugin under its manifest ID. Use the matching `HUNDEKRAFT_KENNEL` configuration without printing private environment contents. Add the new project to the root README and document its actual host test command. Run `python tools/compile_commands.py` after adding source files. Custom artwork and the HTML manual belong in the later usability milestone.

## Validation and acceptance

### Host correctness

Use meaningful signal and state tests, with assertions and deterministic output:

- Silence, impulse, constant input, full-scale positive/negative edges, sine sweeps and sustained tones: no overflow, instability, stale history, or out-of-bounds delay reads. Use host sanitizers where available.
- Mix zero is bit-exact dry in mono and stereo, with no left/right swap or crosstalk in the dry path. Full wet has no dry contribution.
- Measure actual rotation rates and transition durations against the configured values, including interrupted transitions and brake. Inspect phase continuity rather than merely checking the requested target.
- Settle brake at different phases; verify finite output and useful remaining level. Verify silence decays without persistent fixed-point limit cycles.
- Run three independent positions with different inputs/settings and prove state isolation. Reinitializing one must not reset the others.
- Check intermediate mic and mix settings, stereo fold-down, left-only/right-only input, and the documented opposite-polarity master-input cancellation.
- Sweep controls during sustained audio. Inspect boundary discontinuities and listen for clicks. Compare driven spectra and input/output levels at quiet, typical and loud inputs.

### Listening examples

Render both raw output for level/headroom checks and clearly labeled level-matched comparisons for listening. Use the actual integer DSP. Include a simple generated organ-like source and sustained synth chord; use a user-provided or appropriately licensed voice sample when available. Do not block the prototype solely on obtaining a vocal recording.

For each source, include dry, slow clean, fast clean, slow-to-fast-to-brake, near/far comparison, and driven rotary. Make the transition example long enough to hear the entire bass coast-down. Supply stereo WAVs and a mono fold-down example, settings, source provenance, and a reproducible render command. Keep generated audio under `build/` unless intentionally distributing selected examples.

Jason's listening decision should address: Does it feel like rotating sound? Are the two inertias audible? Does slow mode have useful movement? Does fast mode retain pitch/body? Does drive add useful character? Does it still work in mono?

### Target and hardware

Package with the matching Blackfin toolchain, inspect compiler/linker diagnostics, and measure with a compatible target harness where available. Record the model/version and units for any cost result. A successful build does not establish CPU fit or audio quality.

Provide installation through the existing workshop flow and a hardware test sheet covering all three positions, simultaneous instances, synth/drum activity with tape playback, parameter sweeps, switching FX, saving/reloading settings, cold restart, stereo headphones, mono playback, and resource-monitor peaks. Record any overload, muting or glitch. Hardware results remain pending until someone performs them.

## Milestones and handoff to Jason

1. **Sound and feasibility:** implement integer DSP, host tests and audio renders; build the minimal `.knl`; obtain target cost measurements or clearly identify the missing tooling. Deliver source, commands, WAVs, settings and an honest validation report.
2. **Voicing:** use Jason's listening feedback to adjust crossover, speed ramps, mic behavior, drive and levels. Compare with R3-147 recordings if they become available. Keep every numerical adjustment identified as measured or chosen by ear.
3. **Usability and release:** finalize name/ID, add secondary controls only where useful, create an inexpensive screen that follows actual rotor motion, document controls, complete device testing and package the release.

The immediate task for Claude is milestone one. Make routine implementation choices within this brief, record departures that affect sound or compatibility, and deliver something Jason can hear before investing in the final visual design.
