# Norn UI handoff: Three Keepers

Date: 2026-11-26  
Target: original OP-1, Hundekraft; current repository prototype 0.4.1  
Project: `D:\src\op-1-tools\norn`

## Objective

Implement the approved **Three Keepers** screen for Norn. Three blue Norns hold green, white, and orange threads that form a continuous, parameter-responsive weave. Four compact parameter readouts sit across the top. The artwork should feel at home beside the original OP-1's illustrated synth and effect screens.

This is a UI task for the existing four-model synth. Preserve its sound, parameter order, model boundaries, defaults, preset behavior, and T2/T3/T4 integration. The rename from Pleat to Norn has already happened; use the current plugin identity and build target.

## Authoritative references

Paths below are relative to `D:\src\op-1-tools` unless absolute.

| Reference | Purpose |
| --- | --- |
| `norn/design/Norn-Comparison.html` | Open in a browser. **Three keepers**, with blue figures, is the approved visual direction and default view. |
| `norn/design/norn-weave-comparison.html` | Editable mockup source: palette, layout, hand anchors, `point()` geometry, crossing detection, and interaction timing. |
| `norn/plugin/norn.c` and `norn/plugin/plugin.json` | Current model selection, engine callbacks, identity, defaults, and build inputs. |
| `norn/plugin/{ws,fm,gr,wt}_dsp.h` | Actual parameter meanings and pitch-dependent behavior. These override simplified browser labels and animations. |
| `norn/README.md`, `norn/docs/platform-contract.md`, `norn/HARDWARE-TEST.md` | Current behavior, platform constraints, and device validation. Parts of the platform contract retain historical estimates; use current source and measurements. |
| `ratatoskr/plugin/ratatoskr.c` and its artwork/build inputs | Existing working custom screen, parameter access, text rendering, cached artwork, and bounded line rendering. |
| `D:\src\OP-1-Related\teenage engineering ARTWORK` | User-supplied original OP-1 SVG aesthetic references. Read-only. |
| `D:\src\hundekraft-source` and `D:\src\op1-workshop` | Additional read-only SDK/firmware references, where available. |

The older `Norn-Pleat-Mockup-v2.html` and its embedded handoff are historical. They describe the superseded left panel and a reserved FM slot. Do not use them as the implementation specification.

The comparison still contains a legacy `PLEAT` idle footer and a **Shared weave** alternative. Neither belongs in the final plugin. Use **NORN** where a name is useful; omit redundant branding if space is needed. Browser controls and the comparison selector are not device UI.

## Approved design decisions

1. Use **Three Keepers**, not a user-selectable choice between the two concepts.
2. Preserve the recognizable rendered figures: profiles, hoods, braid, hands, and robe silhouettes. Render all three in blue. Their original ribbon colors and shapes are expendable.
3. The top figure holds the green Harmonics thread, the middle holds white Timbre, and the bottom holds orange Morph.
4. Blue selects the synthesis model for the whole ensemble. Blue figures visually balance the other three encoder colors; no additional figure or blue thread is needed.
5. All three sound parameters can influence the entire weave. A figure's assigned control identifies her role; it does not limit changes to her individual thread.
6. Put readings and position indicators across the top, in physical encoder order. Give the lower screen's horizontal space to the figures and weave.
7. Every visible thread is generated continuously from its hand onward. **No static ribbon tail joined to an animated extension**, and no visible change of width, color, or curvature at an artificial join.
8. Parameter positions must remain unambiguous while the artwork moves: minimum, midpoint, and maximum are visible on each continuous control's track.
9. Preserve the satisfying parameter-dependent weave behavior demonstrated by the comparison. It is an illustration of the settings, not a waveform or spectral measurement.

## Layout and palette

Design in **320 × 160 logical coordinates**. The browser uses a 960 × 480 backing canvas with a 3× transform; do not treat that as the device resolution.

| Element | Mockup reference geometry |
| --- | --- |
| Parameter columns | Start at x = 7, 87, 167, 247; 80-pixel spacing. |
| Label / value baselines | y = 9 / 20. |
| Continuous tracks | y = 26, length 60; endpoint and midpoint ticks. |
| Model indicator | Four discrete segments, 16-pixel spacing, 12-pixel segment length. |
| Figure placement | Source-space origin at (8, 38), uniform scale `121 / 329`. Original embedded image is 274 × 329. |
| Top hand / green thread | Approximately (79.72, 73.31). |
| Middle hand / white thread | Approximately (84.13, 104.57). |
| Bottom hand / orange thread | Approximately (87.44, 136.20). |
| Thread right extent | x = 310. |
| Footer | Browser baseline y = 152; optional and nonessential. |

These are a reference composition, not permission to shrink text until it is unreadable. Use the SDK's actual font metrics and inspect at native device size. Ratatoskr notes that some panels hide rows below roughly y = 147: keep essential hands, controls, and motion inside the confirmed visible area. Cropping robe bottoms is acceptable; losing a hand or value is not.

| Role | Mockup color |
| --- | --- |
| Background | `#000000` |
| Blue: model and figures | `#709CFF` |
| Green: Harmonics | `#62E6A4` |
| White: Timbre, values, crossing accents | `#EEE9DC` |
| Orange: Morph | `#FF9859` |
| Track / ticks | `#30343B` / `#727782` |
| Secondary text | `#878C94` |

The mockup multiplies the original ivory figure image by blue, so its antialiased pixels are slightly darker than the nominal blue. Treat the displayed result as the visual reference; exact per-pixel RGB reproduction is not required on the device. Use flat color, black negative space, deliberate thin lines, and compact lettering. Avoid glow, gradients, shadows, or ornamental UI panels.

## Figures and continuous thread attachment

The current browser preserves the figures using an **embedded PNG**, contour clipping (`figureMask`, `tailMask`), and a cached blue tint. It is not finished vector artwork. Some isolated sleeve strokes are visible in the reference; review them during asset cleanup so they do not look like leftover ribbon fragments.

Prepare clean figure-only artwork for the device, preferably through the repository's existing SVG/compiled-art pipeline. Retain the approved likeness; do not replace the figures with generic icons. A supported preprocessed bitmap is also acceptable if it preserves the look and fits the renderer and memory budget. Asset cleanup must remove all painted thread remnants without erasing fingers or robe contours.

Keep each hand anchor in the same coordinate system and transform as its figure. The entire thread is dynamic, with a smooth departure from that anchor. Draw the thread beneath the fingers, using a small foreground hand layer if necessary. Static figures are fine; smooth continuity does not require animating their poses.

The browser draws threads, clips the figure image above them, and adds a small fingertip stroke. On hardware, choose the equivalent layering that works with the existing screen cache. Do not let an opaque figure background rectangle cover the live weave. Do not copy browser clipping, PNG decoding, or tinting into the audio path.

## Controls and displayed values

Blue retains the current four fixed quarters in `pl_model()` within `norn.c`:

| Stored blue value | Model | Compact label |
| --- | --- | --- |
| 0–8191 | Waveshaper | SHAPER |
| 8192–16383 | Two-operator FM | 2-OP FM |
| 16384–24575 | Formant | FORMANT |
| 24576–32767 | Wave Terrain | TERRAIN |

Use the existing selector logic, including its boundary handling. The browser dropdown is only a convenient test control; it does not redefine the device's parameter storage. Wave Terrain is implemented, not a placeholder.

| Model | Green / Harmonics | White / Timbre | Orange / Morph |
| --- | --- | --- | --- |
| Waveshaper | **CURVE:** neutral near center, different bending on either side | **FOLD:** folding amount | **SLOPE:** triangle toward ramp |
| Two-op FM | **RATIO:** stepped oscillator ratio | **DEPTH:** modulation amount | **FEEDBACK:** frequency feedback to the left, phase feedback to the right, neutral at center |
| Formant | **INTERVAL:** second formant relative to the first; the DSP also changes fundamental contribution | **FORMANT:** first formant frequency | **WIDTH:** carrier pulse width, constrained on high notes |
| Wave Terrain | **TERRAIN:** morph through eight terrains | **RADIUS:** circular path size, reduced on high notes | **OFFSET:** path center, with radius shrinking toward edges |

Initially show normalized knob percentages rather than inventing physical units. Preserve model-specific labels. The tracks always increase left to right; each has a solid base-setting marker and clear endpoints and center.

For FM orange, the browser displays negative and positive normalized amounts, with **OFF** at 50%. On device, determine OFF from the DSP's actual zero-feedback region after integer quantization, not from a rounded percentage string. A rounded 50% must not incorrectly imply zero. Likewise, do not label ratios, frequencies, or terrain numbers as exact physical values unless derived from the same mapping as the DSP.

## Weave behavior

Use `point()` in the editable mockup as the reference for the established appearance. Both comparison concepts use this same geometry. Reimplement it efficiently in the UI renderer; it is not new synthesis code.

| Model | Visual behavior to retain |
| --- | --- |
| Waveshaper | Green changes bending; white introduces more folds/detail; orange changes the weave's lean/asymmetry. |
| FM | Green changes repetition; white changes excursion and modulation detail; orange produces broad warping on the left and finer ripples on the right. At center the feedback-specific warping disappears, while the basic weave remains. |
| Formant | Green changes separation between strands; white changes the fine pattern; orange changes line thickness modestly. |
| Wave Terrain | Green changes the pattern associated with terrain; white changes excursion/detail; orange shifts its shape. This is a visual analogy, not an actual terrain plot or calculated oscillator path. |

In the reference, each path blends from its fixed hand to a right endpoint using `u²(3−2u)`. The deformation is multiplied by `sin²(pi*u)`, keeping the departure and endpoints smooth. Right endpoint y values are 120, 108, and 96 for top, middle, and bottom threads. Preserve this continuity even if sampling or formulas are simplified.

Reference thread width is about 0.85 logical pixels; Formant adds up to 0.45. Choose crisp device-supported widths after inspecting the actual rasterizer. Match the overall weave before tuning microscopic details.

Small ivory diamonds mark **actual intersections of the thread centerlines**. They are not parameter handles, voices, notes, or signal-routing nodes. Detect crossings at common screen x coordinates because the hands have different x positions. Deduplicate close intersections and cap the marker count. Keep diamonds subtle so they do not obscure the threads. The browser's black underlay and small ivory diamond are the visual reference.

## Motion and feedback

Separate three causes of visual change:

| Cause | Behavior |
| --- | --- |
| Encoder adjustment | Update the geometry and top readout. For green/white/orange, briefly accent the corresponding hand/thread in that control's color. The mockup uses a small hand ring and H/T/M label fading over about 1.3 seconds. |
| Note activity | Send small highlights outward along the threads. A held note may add slight motion to the weave; release fades the highlights while retaining the setting-dependent geometry. |
| Actual T4 modulation | Change the geometry using the real effective parameter values, while keeping base-setting markers stable. |

The figures remain blue during these accents. Detect user adjustments separately from continuous modulation so the hand highlight does not fire every LFO frame. Blue updates the model indication and the ensemble's pattern; it has no individual keeper.

The browser loops note highlights with a small strand-to-strand offset, adds roughly one pixel of drift, and fades release over 0.65 seconds. Those timings are aesthetic references, not measurements of Norn's envelope. Do not claim that the fade measures envelope amplitude unless a real envelope signal is used.

For polyphony, use a bounded aggregate activity indication or a documented representative voice. Do not create an unbounded trail or one complete weave per voice. Handle retriggering, voice stealing, all-notes-off, and page changes so the screen cannot remain stuck in a held-note state. A timer may advance a highlight after a genuine event; it must not invent note events or substitute for actual note-state detection.

## LFO: use the existing OP-1 section

**Norn has no additional plugin LFO.** T4 is the modulation source. The browser's **Preview OP-1 modulation** button synthesizes an orange-only oscillation for demonstration; it is not a requested routing change or a new device control.

The platform contract says modulated parameters arrive at the engine callbacks. Ratatoskr also demonstrates `op1_synth_knobs()` and `op1_synth_knobs_live()` in its GUI code. Verify their precise current semantics before choosing the sources for stored values, encoder movement, and effective modulation; do not infer these from the names.

- Solid track marker: base/saved setting, unaffected by LFO animation.
- Optional hollow marker: actual effective value, when safely available.
- Weave: effective values when available; otherwise base values.
- If effective values differ by voice, explicitly choose a representative voice or aggregate display rule. Do not suggest a single marker is an exact reading for every voice.
- Respect actual routing, including the possibility that blue/model is modulated. Do not silently change how the engine responds. A separate effective model indication is preferable to relabeling one model's controls while drawing another's pattern.
- If no reliable UI-safe effective value exists, omit the hollow marker in the first pass. Keep the existing T4 audio behavior intact and document the visual limitation.

## Implementation phases

### 1. Establish the screen and assets

**Goal:** reproduce the approved composition with readable live base controls.

Follow Ratatoskr's screen registration, build, text, and asset conventions. Produce clean blue figure artwork with stable hand anchors. Add the top readouts, four model segments, and three position tracks. Render a static parameter-dependent weave first. Use the current Norn defaults rather than the browser demo's starting settings.

**Exit:** all four models and min/center/max values display correctly at native size; fingers and thread origins align; loading presets and switching T1/T2/T3/T4 still work.

### 2. Add continuous weave interaction

**Goal:** make encoder changes feel like the approved mockup.

Port the bounded geometry, crossing accents, and keeper highlights. Ensure neutral FM feedback has no feedback-specific distortion and both extremes are visually distinct. Keep all line endpoints and intermediate vertices within safe screen bounds.

**Exit:** continuous sweeps do not jump, expose static ribbon remnants, crowd the readouts, or leave stale pixels. Any smoothing is visual only and does not alter DSP parameters.

### 3. Add real activity and modulation feedback

**Goal:** connect motion to actual OP-1 behavior.

Use existing safe note/activity and parameter snapshots where possible. Add bounded note highlights and effective-value feedback only where the source is trustworthy. Keep browser controls out of the device screen.

**Exit:** notes, releases, retriggers, steals, and T4 modulation behave predictably; no invented LFO, stuck activity, or misleading base markers.

### 4. Validate on hardware and prepare the handoff result

**Goal:** confirm the visual design fits the device without degrading audio.

Use `tools/build.sh norn` and the existing tests in `norn/README.md`. Measure CPU/peak behavior before and after with six voices and representative effects/tape use. Current README reports approximately 31% synth for Waveshaper, 24% FM, 25% Formant, and 31% Wave Terrain; treat these as context, not a replacement for a same-device baseline.

Keep rendering on the GUI thread. No drawing, asset processing, allocation, or floating-point geometry in the audio callback. If activity snapshots require audio-side changes, make them fixed-size and cheap. Reuse cached static art where layer ordering permits. Prefer integer geometry or lookup tables and a fixed vertex budget; do not copy the browser's repeated trigonometry and allocation pattern. Browser animation is capped near 30 fps, not a hardware requirement.

Ratatoskr explicitly guards line bounds because extreme coordinates can exhaust the rasterizer. Apply equivalent protection to every curve, highlight, crossing marker, and transformed figure. If performance is tight, reduce geometry samples or redraw rate before compromising audio or control readability.

**Exit:** provide device screenshots, build/test results, measured resource changes, and any unverified behavior or departures from this specification.

## Acceptance checklist

- [ ] Three recognizable blue figures; green, white, orange threads in top-to-bottom order.
- [ ] Four compact top readouts; no restored left-side parameter panel.
- [ ] Continuous generated threads start at the fingers, with consistent layering and no painted stubs.
- [ ] All four actual models work, including Wave Terrain; no reserved FM slot or Pleat branding.
- [ ] Min/center/max and base position are legible for each continuous parameter, including during modulation.
- [ ] FM left feedback, zero feedback, and right feedback are distinguishable and truthfully labeled.
- [ ] Crossings are marked only where threads intersect; motion remains bounded.
- [ ] A changed control highlights its assigned keeper without restricting its influence to one thread.
- [ ] T4 remains the only LFO section; any effective marker reflects real data.
- [ ] Pitch-dependent Formant and Wave Terrain visuals are not presented as exact measurements unless implemented from real voice data.
- [ ] No stuck notes/activity, ghost pixels, clipping of essential content, or incorrect page/preset behavior.
- [ ] Native-device text and artwork are readable; browser supersampling is not treated as proof of hardware quality.
- [ ] Existing audio tests pass relative to their documented baseline; CPU/peak and asset costs are reported.

## Suggested first instruction to Claude

> Implement the Norn custom screen from `norn/design/Claude-UI-Handoff.md`, using the blue-figure **Three keepers** view in `norn/design/Norn-Comparison.html` as the visual reference. Start with clean figure-only artwork, the top control strip, and continuous parameter-driven threads from the hands. Follow the working Ratatoskr screen/build conventions. Preserve the existing four-model DSP and OP-1 envelope/effects/LFO behavior. Then add real activity and modulation feedback as specified, and report hardware checks separately from browser checks.
