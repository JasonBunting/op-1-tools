# Ratatoskr design spec

**Target:** Original Teenage Engineering OP‑1 running Hundekraft  
**Status:** Approved screen design for implementation and on-device validation

Ratatoskr plays a recorded sample as a stream of overlapping grains. The waveform is the trunk of Yggdrasil. The squirrel is the playhead, the eagle marks the crown, and the dragon marks the roots. Turning the blue encoder puts the squirrel somewhere in the recording. Turning the orange encoder changes how he travels.

## Main controls

Press a key to hear the sample. Use C4 as the root note, matching Hundekraft’s Granular convention. The keyboard transposes the sound within each grain independently of how fast the squirrel moves. At startup, the four encoders control the following parameters. The ranges are starting points for hardware and listening tests. [Hundekraft manual](https://hundekraft.github.io/op1-workshop/manual.html#granular)

| Encoder | Control | Range and default | Behavior |
|---|---|---|---|
| Blue | **Position** | 0–100% of the sample, default 50% | Places the squirrel immediately and sets the starting point for the next phrase. Display source time in seconds. |
| Green | **Size** | 12–250 ms, logarithmic, default 50 ms | Sets how long a grain plays in output time. |
| White | **Scatter** | 0–500 ms to either side, default 0 ms | Chooses an offset independently for each grain. Give the lower end of the knob travel finer resolution. |
| Orange | **Motion** | Hold, Forward, Reverse, Bounce, Wander, Jump. Default Hold. | Picks how the source position changes. Show the mode name as the value. |

Start with a **Hann** envelope and **2× overlap per active note**. With a sample loaded, holding a single note should produce a steady tone or repeating texture at the selected spot. Use the existing Hundekraft synth note, envelope, and sampler interfaces as the integration point. Keep the white encoder push available for sample browsing if the plugin interface supports reusing it. [Original OP‑1 synth guide](https://teenage.engineering/guides/op-1/original/synthesizer-mode) · [Hundekraft manual](https://hundekraft.github.io/op1-workshop/manual.html)

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

## Approved screen layout

The left column shows the four encoder readouts. The central waveform forms the tree. The eagle is the approved head profile, the dragon is the approved **Swept horns** profile, and the squirrel uses the two-tone outline from the sketch. The right side shows the current grain envelope. Design in the original OP‑1’s **320 × 160 logical units**. Use the matching SDK’s panel scaling and coordinate helpers for native drawing. [Teenage Engineering OP‑1 manual](https://teenage.engineering/_img/54b7f9bf8681400300255cab_original.pdf)

| Element | Logical placement and appearance |
|---|---|
| Background | Black, `#000000`. |
| Left column | Label baselines at y = 36, 65, 94, 123, x = 17. Value baselines 11 units below. Draw a 2 × 17 strip at x = 10 and 8 units above each label baseline. |
| Parameter text | `POSITION`, `SIZE`, `SCATTER`, `MOTION`. Use muted labels `#A4AFA7` sized near 7.6 logical units and values near 11.5. Match the reference baselines and bounds using native text. |
| Encoder colors | Position blue `#51A2FF`, Size green `#22E495`, Scatter white `#EDF0EB`, Motion orange `#FA9745`. Use these for values and strips. |
| Waveform | Center x = 156. Crown y = 23 and roots y = 139. Source time increases upward. For normalized source time `t`, use `round(139 - 116 * t)`. Use one-logical-unit bars `#83D8C8` and a centerline `#39796A`. |
| Eagle and dragon | At full Range, the top-left anchors are `(143, 4)` and `(145, 141)`. Reuse the existing Range endpoints. Position the eagle 19 units above the upper endpoint and the dragon 2 units below the lower endpoint on the same axis. |
| Squirrel | Sprite origin at `(187, playY - 14)`. Draw the orange line at `(180, playY)` with width 6 and height 1, using `#EEA570`. Use the live, shared playhead for `playY`. |
| Grain marks | Keep the current live grain display. Place marks at actual grain source centers after boundary handling. Combine launches that map to the same display row. Reference mark color is `#D6EBE1`. |
| Shape pane | On the right, place the `SHAPE` label at `(237, 58)` and the shape name at `(237, 109)`, measured at the text baseline. Use `#83B197` and `#B9E6C1` respectively. The axes run from `(237, 68)` to `(237, 93)` to `(304, 93)` in `#264D39`. Plot the active window in `#8DDCA6` across x = 237–304, with a 20-unit peak height and a reference stroke width of 1.35 logical units. |

Draw the branches and roots behind the bars. For the reference frame, the four polylines are `[(156,46),(134,46),(117,39)]`, `[(156,86),(175,69),(196,69),(211,60)]`, `[(156,144),(142,144),(127,152)]` and `[(156,144),(168,146),(186,152)]`. Use `#204B37` and a 1.2-unit reference stroke. Where SVG art and generated marks share screen space, preserve legibility with draw order or disjoint bounds.

The blue readout shows selected Position while the encoder is being edited and live playhead time otherwise. Display source position in seconds, Size and Scatter in milliseconds, and the active Motion by name. Show `NO SAMPLE` in the waveform area for an empty slot. After a successful sample change, rebuild the waveform and place the squirrel at the stored Position.

### Shape display and editing

Show the shape plot during normal playing, with **Size** visible in the green row. Green push enters or exits Shape editing. In Shape editing, emphasize the right pane with the green encoder color and update the name and curve as the encoder turns. The right pane supplies the Shape readout and the left row continues to show Size. In this screen update, reuse the current encoder and Shift state so each gesture still targets the same parameter.

### Native integration

Use the existing `ratatoskr.c` screen callbacks and the installed core’s matching Hundekraft/kennel SDK. The companion `op-1-tools/research/plugin-ui-design.md` identifies the current SVG asset pipeline and exact source locations.

1. Put the SVG from **Appendix A** into the plug-in’s existing screen asset and retain the current manifest registration. The matching SDK packs the artwork into the `.knl`. [Hundekraft plug-in guide](https://hundekraft.github.io/op1-workshop/manual.html#make-your-own-plugin)
2. In `rt_screen_init()`, resolve `eagle`, `dragon` and `squirrel` each time the SVG document initializes. Keep native text object allocation separate from those document-owned handles. Use the existing text wrappers and respect each helper’s coordinate units.
3. In `rt_screen_update()`, use a coherent snapshot of the existing engine/display state. Update art transforms from the live playhead and Range mapping, call `op1_screen_draw()`, and draw the generated waveform, grain marks, labels and shape curve with the current raster helpers. Keep document mutation in the graphics callback and use the existing engine state for all readouts. Convert logical coordinates to Q10 where the SDK expects it.
4. Start with ordinary native drawing and automatic frame skipping. Bound every path and label, reuse text objects, and rebuild the waveform peak bins when the sample changes. Use the native shape/window data for the graph with a bounded number of segments. Revisit caching only if measurements show a specific hotspot.
5. Run the matching SDK’s SVG checker, build the existing plug-in and compare a native capture using **Appendix B**. Exercise both grain-shape page states, narrow Range, absent and short samples, modulation, document reload and playback under load on the original OP‑1. Match the approved geometry and spacing while allowing for the firmware’s fonts, RGB565 color quantization and rasterizer.

## Grain shape and the Hann window

A **window** is a volume curve applied to one grain. A **Hann** window starts at silence, rises smoothly to its peak halfway through, and falls smoothly back to silence. For normalized time `t` from 0 to 1, its gain is `0.5 - 0.5 * cos(2π * t)`. The fades avoid an abrupt level change at the edges of a cut. A host ADSR applies to the held note as a whole. The grain window repeats on each of the little pieces that make up that note. [SciPy Hann definition](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.windows.hann.html) · [SuperCollider GrainBuf](https://doc.sccode.org/Classes/GrainBuf.html)

| Shape | Curve | What to listen for |
|---|---|---|
| **Hann** | Rounded, even rise and fall | Smooth sustained textures |
| **Triangle** | Straight rise and fall with a pointed peak | A sharper pulse at the same size and overlap |
| **Pluck** | Fast rise to the peak, long fall | A more pronounced front edge |
| **Swell** | Long rise to the peak, fast fall | A reverse-like lift while the source plays forward |

For the first Shape prototype, use a symmetric triangle for Triangle and piecewise raised-cosine ramps with the peak at 15% or 85% for Pluck and Swell. All four curves start and end at zero. Compare them at matched listening levels before settling on any per-shape gain correction.

## Additional parameters

| Parameter | Prototype range | Why add it | Proposed control |
|---|---|---|---|
| **Speed** | 0.05–2× in Forward, Reverse, and Bounce. 0.1–4 updates/sec in Wander and Jump. Use the existing motion presets as defaults. | Makes traversal adjustable without changing note pitch. | Orange push toggles Motion and Speed. In Hold, display a dash for Speed and preserve its stored values. |
| **Shape** | Hann, Triangle, Pluck, Swell. Default Hann. | Adds attack and decay choices at the grain level. | Green push switches between Size and Shape editing. The right pane shows and highlights Shape. |
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
- At the same Size and Overlap, change Hann to Pluck or Swell. Grain source positions and cadence stay consistent while the attacks change. The tree stays visible, the right-hand name and curve reflect the selected shape, and green push toggles the Shape focus indicator.

## Appendix A — Approved SVG artwork

This SVG contains the three approved sprite groups in their full-range reference positions. The eagle and dragon use filled logical-pixel regions and the squirrel uses two cubic contours plus an eye. Group transforms supply the reference positions. Use each existing node’s live position when rendering. The asset is 7,999 bytes, within the guide’s 8 KiB target. Its geometry was checked in a host renderer. Run the matching SDK checker and verify the native rendering before packaging.

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="320" height="160" viewBox="0 0 320 160">
  <g id="eagle" transform="matrix(1 0 0 1 143 4)">
  <path fill="#e7efde" d="M12 1L20 1L20 4L12 4Z"/>
  <path fill="#e7efde" d="M9 2L12 2L12 6L9 6Z"/>
  <path fill="#e7efde" d="M20 2L22 2L22 4L20 4Z"/>
  <path fill="#e7efde" d="M8 3L9 3L9 7L8 7Z"/>
  <path fill="#65745f" d="M22 3L23 3L23 4L22 4Z"/>
  <path fill="#e7efde" d="M7 4L8 4L8 7L7 7Z"/>
  <path fill="#e7efde" d="M12 4L19 4L19 5L12 5Z"/>
  <path fill="#65745f" d="M19 4L22 4L22 5L19 5Z"/>
  <path fill="#e2bd76" d="M22 4L28 4L28 8L22 8Z"/>
  <path fill="#e7efde" d="M6 5L7 5L7 8L6 8Z"/>
  <path fill="#e7efde" d="M12 5L14 5L14 12L12 12Z"/>
  <path fill="#65745f" d="M14 5L19 5L19 6L14 6Z"/>
  <path fill="#0b1110" d="M19 5L21 5L21 6L19 6Z"/>
  <path fill="#e2bd76" d="M21 5L22 5L22 8L21 8Z"/>
  <path fill="#e2bd76" d="M28 5L29 5L29 11L28 11Z"/>
  <path fill="#e7efde" d="M5 6L6 6L6 9L5 9Z"/>
  <path fill="#aeb9a4" d="M9 6L10 6L10 7L9 7Z"/>
  <path fill="#e7efde" d="M10 6L12 6L12 12L10 12Z"/>
  <path fill="#e7efde" d="M14 6L20 6L20 11L14 11Z"/>
  <path fill="#e2bd76" d="M20 6L21 6L21 7L20 7Z"/>
  <path fill="#e2bd76" d="M29 6L30 6L30 11L29 11Z"/>
  <path fill="#aeb9a4" d="M7 7L9 7L9 10L7 10Z"/>
  <path fill="#e7efde" d="M9 7L10 7L10 9L9 9Z"/>
  <path fill="#e7efde" d="M20 7L21 7L21 10L20 10Z"/>
  <path fill="#e2bd76" d="M30 7L31 7L31 9L30 9Z"/>
  <path fill="#e7efde" d="M4 8L5 8L5 10L4 10Z"/>
  <path fill="#aeb9a4" d="M6 8L7 8L7 11L6 11Z"/>
  <path fill="#e7efde" d="M21 8L22 8L22 9L21 9Z"/>
  <path fill="#e2bd76" d="M22 8L24 8L24 9L22 9Z"/>
  <path fill="#e2bd76" d="M27 8L28 8L28 9L27 9Z"/>
  <path fill="#e7efde" d="M3 9L4 9L4 13L3 13Z"/>
  <path fill="#aeb9a4" d="M5 9L6 9L6 11L5 11Z"/>
  <path fill="#aeb9a4" d="M9 9L10 9L10 10L9 10Z"/>
  <path fill="#e7efde" d="M2 10L3 10L3 13L2 13Z"/>
  <path fill="#aeb9a4" d="M4 10L5 10L5 11L4 11Z"/>
  <path fill="#aeb9a4" d="M7 10L8 10L8 11L7 11Z"/>
  <path fill="#e7efde" d="M8 10L10 10L10 13L8 13Z"/>
  <path fill="#e2bd76" d="M27 10L28 10L28 12L27 12Z"/>
  <path fill="#e7efde" d="M1 11L2 11L2 12L1 12Z"/>
  <path fill="#e7efde" d="M4 11L7 11L7 12L4 12Z"/>
  <path fill="#65745f" d="M7 11L8 11L8 12L7 12Z"/>
  <path fill="#65745f" d="M14 11L15 11L15 15L14 15Z"/>
  <path fill="#e7efde" d="M15 11L19 11L19 12L15 12Z"/>
  <path fill="#65745f" d="M19 11L21 11L21 12L19 12Z"/>
  <path fill="#65745f" d="M1 12L2 12L2 14L1 14Z"/>
  <path fill="#e7efde" d="M4 12L5 12L5 13L4 13Z"/>
  <path fill="#65745f" d="M5 12L7 12L7 14L5 14Z"/>
  <path fill="#e7efde" d="M7 12L8 12L8 14L7 14Z"/>
  <path fill="#65745f" d="M10 12L11 12L11 15L10 15Z"/>
  <path fill="#e7efde" d="M11 12L13 12L13 13L11 13Z"/>
  <path fill="#65745f" d="M13 12L14 12L14 14L13 14Z"/>
  <path fill="#65745f" d="M15 12L16 12L16 14L15 14Z"/>
  <path fill="#e7efde" d="M16 12L17 12L17 13L16 13Z"/>
  <path fill="#65745f" d="M17 12L20 12L20 13L17 13Z"/>
  <path fill="#65745f" d="M0 13L1 13L1 14L0 14Z"/>
  <path fill="#65745f" d="M4 13L5 13L5 15L4 15Z"/>
  <path fill="#65745f" d="M8 13L10 13L10 15L8 15Z"/>
  <path fill="#65745f" d="M11 13L12 13L12 14L11 14Z"/>
  <path fill="#65745f" d="M16 13L18 13L18 14L16 14Z"/>
  <path fill="#65745f" d="M3 14L4 14L4 15L3 15Z"/>
  <path fill="#65745f" d="M9 15L10 15L10 16L9 16Z"/>
  </g>
  <g id="dragon" transform="matrix(1 0 0 1 145 141)">
  <path fill="#a2d1a7" d="M9 0L11 0L11 1L9 1Z"/>
  <path fill="#a2d1a7" d="M2 1L3 1L3 2L2 2Z"/>
  <path fill="#a2d1a7" d="M10 1L17 1L17 2L10 2Z"/>
  <path fill="#a2d1a7" d="M3 2L9 2L9 3L3 3Z"/>
  <path fill="#a2d1a7" d="M13 2L19 2L19 3L13 3Z"/>
  <path fill="#a2d1a7" d="M4 3L11 3L11 4L4 4Z"/>
  <path fill="#a2d1a7" d="M15 3L20 3L20 4L15 4Z"/>
  <path fill="#a2d1a7" d="M7 4L15 4L15 5L7 5Z"/>
  <path fill="#6bbe8c" d="M15 4L16 4L16 6L15 6Z"/>
  <path fill="#a2d1a7" d="M16 4L20 4L20 5L16 5Z"/>
  <path fill="#6bbe8c" d="M20 4L24 4L24 5L20 5Z"/>
  <path fill="#a2d1a7" d="M9 5L14 5L14 6L9 6Z"/>
  <path fill="#6bbe8c" d="M14 5L15 5L15 6L14 6Z"/>
  <path fill="#6bbe8c" d="M16 5L17 5L17 6L16 6Z"/>
  <path fill="#4d9369" d="M17 5L22 5L22 6L17 6Z"/>
  <path fill="#6bbe8c" d="M22 5L25 5L25 6L22 6Z"/>
  <path fill="#6bbe8c" d="M9 6L10 6L10 12L9 12Z"/>
  <path fill="#a2d1a7" d="M10 6L12 6L12 7L10 7Z"/>
  <path fill="#6bbe8c" d="M12 6L13 6L13 7L12 7Z"/>
  <path fill="#4d9369" d="M13 6L20 6L20 8L13 8Z"/>
  <path fill="#e2bd76" d="M20 6L22 6L22 7L20 7Z"/>
  <path fill="#4d9369" d="M22 6L23 6L23 7L22 7Z"/>
  <path fill="#6bbe8c" d="M23 6L31 6L31 8L23 8Z"/>
  <path fill="#6bbe8c" d="M8 7L9 7L9 13L8 13Z"/>
  <path fill="#6bbe8c" d="M10 7L11 7L11 8L10 8Z"/>
  <path fill="#4d9369" d="M11 7L13 7L13 10L11 10Z"/>
  <path fill="#4d9369" d="M20 7L22 7L22 8L20 8Z"/>
  <path fill="#6bbe8c" d="M22 7L23 7L23 13L22 13Z"/>
  <path fill="#0b1110" d="M31 7L33 7L33 8L31 8Z"/>
  <path fill="#6bbe8c" d="M7 8L8 8L8 13L7 13Z"/>
  <path fill="#4d9369" d="M10 8L11 8L11 11L10 11Z"/>
  <path fill="#4d9369" d="M13 8L16 8L16 9L13 9Z"/>
  <path fill="#6bbe8c" d="M16 8L22 8L22 11L16 11Z"/>
  <path fill="#6bbe8c" d="M23 8L24 8L24 9L23 9Z"/>
  <path fill="#376f51" d="M24 8L26 8L26 9L24 9Z"/>
  <path fill="#6bbe8c" d="M26 8L34 8L34 9L26 9Z"/>
  <path fill="#6bbe8c" d="M5 9L7 9L7 13L5 13Z"/>
  <path fill="#4d9369" d="M13 9L15 9L15 10L13 10Z"/>
  <path fill="#6bbe8c" d="M15 9L16 9L16 11L15 11Z"/>
  <path fill="#376f51" d="M23 9L24 9L24 10L23 10Z"/>
  <path fill="#376f51" d="M25 9L27 9L27 10L25 10Z"/>
  <path fill="#6bbe8c" d="M27 9L34 9L34 10L27 10Z"/>
  <path fill="#376f51" d="M3 10L4 10L4 11L3 11Z"/>
  <path fill="#6bbe8c" d="M4 10L5 10L5 13L4 13Z"/>
  <path fill="#4d9369" d="M11 10L12 10L12 11L11 11Z"/>
  <path fill="#6bbe8c" d="M12 10L15 10L15 11L12 11Z"/>
  <path fill="#6bbe8c" d="M23 10L24 10L24 14L23 14Z"/>
  <path fill="#376f51" d="M24 10L25 10L25 11L24 11Z"/>
  <path fill="#376f51" d="M26 10L29 10L29 11L26 11Z"/>
  <path fill="#a2d1a7" d="M29 10L30 10L30 11L29 11Z"/>
  <path fill="#376f51" d="M30 10L33 10L33 11L30 11Z"/>
  <path fill="#376f51" d="M2 11L3 11L3 14L2 14Z"/>
  <path fill="#6bbe8c" d="M3 11L4 11L4 12L3 12Z"/>
  <path fill="#6bbe8c" d="M10 11L12 11L12 12L10 12Z"/>
  <path fill="#376f51" d="M12 11L18 11L18 12L12 12Z"/>
  <path fill="#6bbe8c" d="M18 11L22 11L22 12L18 12Z"/>
  <path fill="#6bbe8c" d="M24 11L25 11L25 14L24 14Z"/>
  <path fill="#376f51" d="M25 11L26 11L26 12L25 12Z"/>
  <path fill="#376f51" d="M1 12L2 12L2 13L1 13Z"/>
  <path fill="#376f51" d="M3 12L4 12L4 14L3 14Z"/>
  <path fill="#376f51" d="M9 12L12 12L12 13L9 13Z"/>
  <path fill="#376f51" d="M19 12L20 12L20 13L19 13Z"/>
  <path fill="#6bbe8c" d="M20 12L22 12L22 13L20 13Z"/>
  <path fill="#6bbe8c" d="M25 12L31 12L31 13L25 13Z"/>
  <path fill="#376f51" d="M4 13L9 13L9 14L4 14Z"/>
  <path fill="#376f51" d="M21 13L23 13L23 14L21 14Z"/>
  <path fill="#6bbe8c" d="M25 13L28 13L28 14L25 14Z"/>
  <path fill="#376f51" d="M28 13L30 13L30 14L28 14Z"/>
  </g>
  <g id="squirrel" transform="matrix(1 0 0 1 187 108)">
  <path fill="#a26343" d="M 20.064 9.6 C 17.376 6.336 19.392 3.648 22.848 2.784 C 26.496 2.016 31.968 5.472 34.656 9.408 C 36.576 12.384 36.288 15.072 34.08 17.28 C 33.024 18.528 31.584 19.488 30.048 20.448 L 23.04 20.448 C 21.216 20.448 21.984 19.392 23.424 18.048 C 25.152 16.32 26.976 14.304 26.112 12.192 C 25.344 10.176 22.464 10.848 21.792 12.48 C 21.312 13.344 20.832 12.192 20.064 9.6 Z"/>
  <path fill="#eea570" d="M 0.384 12.96 L 5.184 9.312 L 4.128 1.92 C 4.064 1.536 4.224 1.44 4.608 1.632 L 9.12 4.032 L 11.424 0.48 C 11.616 0.16 11.808 0.224 12 0.672 L 13.92 5.952 C 14.592 7.392 16.704 8.736 18.24 11.808 C 21.12 16.32 18.432 19.392 13.632 20.736 L 3.36 20.736 C 2.976 20.672 2.88 20.48 3.072 20.16 L 5.472 15.84 C 5.536 15.648 5.44 15.488 5.184 15.36 L 0.48 13.344 C 0.16 13.216 0.128 13.088 0.384 12.96 Z"/>
  <path fill="#0b1110" d="M 9.0048 9.984 C 9.0048 10.5937 8.4546 11.088 7.776 11.088 C 7.0974 11.088 6.5472 10.5937 6.5472 9.984 C 6.5472 9.3743 7.0974 8.88 7.776 8.88 C 8.4546 8.88 9.0048 9.3743 9.0048 9.984 Z"/>
  </g>
</svg>
```

## Appendix B — Reference-frame fixture

The approved frame shows Range at 100%, `POSITION 0.32 S`, `SIZE 113 MS`, `SCATTER 313 MS`, `MOTION JUMP`, and the right-hand `HANN` pane in its normal state. The visual fixture places the marker at y = 122 and the squirrel at `(187, 108)`. It uses the following waveform bars from the screenshot. Their purpose is a reproducible visual comparison. The plug-in derives the live bars from the loaded sample.

Each pair is `[fraction down from the crown, logical half-width]`. Set y to `round(23 + 116 * fraction)` and round the half-width to the nearest integer. Draw the first bar in `#427A6F` and the rest in `#83D8C8`. The four reference grain marks use fractions .69, .71, .88 and .95 with offsets left of the centerline of 28, 35, 29 and 30 respectively. Their widths, extending farther to the left, are 7, 5, 5 and 4.

```json
[
  [0,15.5],[0.028,15.5],[0.05,10.4],[0.081,7.1],[0.103,3.7],[0.124,3],[0.156,5.9],[0.177,9.7],
  [0.209,3.7],[0.23,5],[0.251,5.4],[0.283,4.7],[0.304,12.6],[0.336,18.1],[0.357,4.7],[0.379,5.4],
  [0.41,5.4],[0.432,6.7],[0.463,14.3],[0.485,16.8],[0.506,15.5],[0.538,14.7],[0.559,5.4],
  [0.591,10.4],[0.612,15.5],[0.644,21.8],[0.665,20.5],[0.686,13.8],[0.718,13.8],[0.739,17.6],
  [0.771,20.5],[0.792,7.6],[0.814,9.7],[0.846,18.1],[0.867,21.8],[0.899,3.4],[0.92,3.4],
  [0.941,3.4],[0.973,4.2],[0.994,4.2]
]
```
