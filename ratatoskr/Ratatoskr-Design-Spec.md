# Ratatoskr design spec

**Target:** Original Teenage Engineering OP‑1 running Hundekraft  
**Status:** Design proposal for an on-device prototype

Ratatoskr plays a recorded sample as a stream of overlapping grains. The waveform is the trunk of Yggdrasil. The squirrel is the playhead, the eagle marks the crown, and the dragon marks the roots. Turning the blue encoder puts the squirrel somewhere in the recording. Turning the orange encoder changes how he travels.

## Main controls

Press a key to hear the sample. Use C4 as the root note, matching Hundekraft’s Granular convention. The keyboard transposes the sound within each grain independently of how fast the squirrel moves. At startup, the four encoders control the following parameters. The ranges are starting points for hardware and listening tests. [Hundekraft manual](https://hundekraft.github.io/op1-workshop/manual.html#granular)

| Encoder | Control | Range and default | Behavior |
|---|---|---|---|
| Blue | **Position** | 0–100% of the sample, default 50% | Places the squirrel immediately and sets the starting point for the next phrase. Display source time in seconds. |
| Green | **Size** | 12–250 ms, logarithmic, default 50 ms | Sets how long a grain plays in output time. |
| White | **Scatter** | 0–500 ms to either side, default 0 ms | Chooses an offset independently for each grain. Give the lower end of the knob travel finer resolution. |
| Orange | **Motion** | Hold, Forward, Reverse, Bounce, Wander, Jump. Default Hold. | Picks how the source position changes. Show the mode name as the value. |

Use a **Hann** envelope and **2× overlap per active note** for the four-control build. With a sample loaded, holding a single note should produce a steady tone or repeating texture at the selected spot. Use the existing Hundekraft synth note, envelope, and sampler interfaces as the integration point. Keep the white encoder push available for sample browsing if the plugin interface supports reusing it. [Original OP‑1 synth guide](https://teenage.engineering/guides/op-1/original/synthesizer-mode) · [Hundekraft manual](https://hundekraft.github.io/op1-workshop/manual.html)

## Playhead behavior

Sample time runs from the roots at the bottom to the crown at the top. All held notes share one playhead, and each note has its own grain pitch. When the first key of a new phrase is pressed, start at the stored Position. Additional keys join the current playhead. Freeze transport when the last key is released and let the host voice envelope finish the release. A change to Motion takes effect from the current playhead. A turn of the blue encoder resets the playhead to the newly selected Position. Grain parameters are captured at launch so already playing grains finish with their original settings.

| Mode | Behavior | Starting rate |
|---|---|---|
| **Hold** | Stay at the current position. Grain launches continue while a key is held. | Stationary |
| **Forward** | Climb toward the eagle, then wrap to the roots. | 0.25 source seconds per real second |
| **Reverse** | Descend toward the dragon, then wrap to the crown. | 0.25 source seconds per real second |
| **Bounce** | Reverse travel direction at each endpoint. Start upward when entering the mode, or downward if already at the crown. | 0.25 source seconds per real second |
| **Wander** | Pick a nearby target, move smoothly toward it, then pick another. Select targets within ±5% of the travel span around the current position, intersected with that span. | One target per second |
| **Jump** | Pick a new position uniformly within the travel span. Hold it until the next jump. | One jump per second |

Forward and Reverse describe the direction in which the squirrel samples the trunk. Each grain itself reads forward at the rate set by the keyboard. **Grain Reverse** would be a separate effect.

## Screen layout

Use the original OP‑1’s **320 × 160** display. Give the tree most of the screen and reserve the bottom row for `POS`, `SIZE`, `SCAT`, and `MOTION`, colored blue, green, white, and orange. Keep the waveform centered horizontally as a vertical strip. Compute a peak envelope over each vertical time bin when the sample changes so short transients stay visible. Draw an unobstructed central strip for the waveform and leave room beside it for the squirrel. [Teenage Engineering OP‑1 manual](https://teenage.engineering/_img/54b7f9bf8681400300255cab_original.pdf)

- **Squirrel** at the live playhead height, facing up or down during travel and perched in Hold. Draw it after grain marks so it stays legible.
- **Grain flashes** at the actual source centers of newly launched grains, after boundary handling. Show them briefly beside the waveform. Combine launches that fall in the same display row during a frame. In Hold with Scatter up, the squirrel stays still while flashes appear above and below him.
- **Eagle and dragon** near the upper and lower travel endpoints. With the optional Range control, move both endpoint markers with the travel span. Clamp the art within the usable display area.
- **Empty slot** shows `NO SAMPLE` where the waveform would be. After a successful recording or load, rebuild the waveform and place the squirrel at the saved Position in the new sample.

The blue readout shows the selected Position while the knob is being edited. At other times it shows live playhead time. This makes a moving playhead easy to follow and lets the player see exactly what start position they’re setting.

## Grain shape and the Hann window

A **window** is a volume curve applied to one grain. A **Hann** window starts at silence, rises smoothly to its peak halfway through, and falls smoothly back to silence. For normalized time `t` from 0 to 1, its gain is `0.5 - 0.5 * cos(2π * t)`. The fades avoid an abrupt level change at the edges of a cut. A host ADSR applies to the held note as a whole. The grain window repeats on each of the little pieces that make up that note. [SciPy Hann definition](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.windows.hann.html) · [SuperCollider GrainBuf](https://doc.sccode.org/Classes/GrainBuf.html)

| Shape | Curve | What to listen for |
|---|---|---|
| **Hann** | Rounded, even rise and fall | Smooth sustained textures |
| **Triangle** | Straight rise and fall with a pointed peak | A sharper pulse at the same size and overlap |
| **Pluck** | Fast rise to the peak, long fall | A more pronounced front edge |
| **Swell** | Long rise to the peak, fast fall | A reverse-like lift while the source plays forward |

For the first Shape prototype, use a symmetric triangle for Triangle and piecewise raised-cosine ramps with the peak at 15% or 85% for Pluck and Swell. All four curves start and end at zero. Compare them at matched listening levels before settling on any per-shape gain correction.

If Shape is exposed, **push green to toggle between Size and Shape**. In Shape mode the green footer reads `SHAPE` and the name of the current curve. Show a small left-to-right curve beside the tree while Shape is open, with time running horizontally and gain vertically. Push green again to return to Size. This keeps the source waveform vertical and the curve recognizable as a separate object. Existing Hundekraft Granular already uses the green push for Shape. [Hundekraft Granular controls](https://hundekraft.github.io/op1-workshop/manual.html#granular)

## Additional parameters

| Parameter | Prototype range | Why add it | Proposed control |
|---|---|---|---|
| **Speed** | 0.05–2× in Forward, Reverse, and Bounce. 0.1–4 updates/sec in Wander and Jump. Use the existing motion presets as defaults. | Makes traversal adjustable without changing note pitch. | Orange push toggles Motion and Speed. In Hold, display a dash for Speed and preserve its stored values. |
| **Shape** | Hann, Triangle, Pluck, Swell. Default Hann. | Adds attack and decay choices at the grain level. | Green push. |
| **Range** | 5–100% of the sample. Default 100%. | Lets the squirrel explore a word, chord, or short passage. | Blue push toggles Position and Range. |
| **Overlap** | 0.5–4×, default 2×. | Moves between distinct pulses and continuous textures. | Try after auditioning the fixed setting. |
| **Grain Reverse** | 0–100% probability, default 0%. | Mixes forward and backward grain reads while the squirrel follows the same route. | Candidate after the main controls feel right. |

For a seven-parameter version, add **Speed, Shape, and Range** to the four main controls. If rhythmic control is more valuable during testing, put Overlap in the Range slot. Range restricts the squirrel’s route. It is a window of the selected width around Position, shifted inward if needed to fit the sample. When the range changes, clamp the live position to it. Scatter may still send a grain outside that route, within the loaded recording.

## Playback implementation

1. On launch, read the live playhead from the transport and derive the grain’s read rate from the note, including sample-rate conversion if the host supplies a source rate that differs from the output rate.
2. Convert requested grain duration to an output frame count. Shorten the grain if its source span at that pitch would exceed the available recording. Bounds must include interpolation taps. For a buffer too short to window and interpolate safely, skip the launch.
3. Determine the valid interval for grain centers from the source span. Intersect it with the Scatter interval and choose a center uniformly within the intersection. If the intervals do not meet, use the nearest valid center. Schedule the flash from that same result.
4. Read the buffer with linear interpolation, multiply by a precomputed window, and sum active grains with headroom. For nominal duration `D` seconds, launch each note’s grains every `D / overlap` seconds. At 50 ms and 2× overlap, that is 40 launches/sec per note. Use the requested duration for scheduling when a short recording forces a shorter actual grain so loading a very short sample cannot spike launch rates.
5. Keep active grains in a fixed-size pool and avoid allocation in the audio callback. If the pool is full, skip the new launch and let current grains finish. Use launch information already collected by the renderer to update grain flashes outside the audio callback. Tune the voice budget and maximum overlap against the original OP‑1 under sustained chords.

## On-device checks

- On a spoken sample, compare **Hold + Scatter** with **Wander + zero Scatter**. In the first case the squirrel stays still and the flashes spread out. In the second case they follow the squirrel, with any endpoint adjustment shown at the actual grain center.
- Hold C4 and an octave above it. Both notes follow the same route at the same speed and sound an octave apart on a pitched source. New notes during a phrase join the live position.
- Switch among Hold, Reverse, and Bounce while playing. Motion changes at the current position. Bounce changes direction at each marker.
- Try the shortest available recording at both sample endpoints and across the keyboard. Verify bounded sample reads, clean transitions, and no audio dropouts under the configured voice budget.
- At the same Size and Overlap, change Hann to Pluck or Swell. Grain source positions and cadence stay consistent while the attacks change. The tree stays visible and the green label and curve reflect the selected shape.
