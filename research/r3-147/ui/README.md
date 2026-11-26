# Rotary effect UI concepts

Generated with the built-in image-generation tool on 2026-11-25. These are visual proposals, not production screen assets. Jason's direction is one naive rotating horn viewed from above at roughly 45 degrees and one microphone moving to show proximity. No cabinet or second rotor. The supplied original OP-1 screenshots establish the outline style, charcoal background, blue/green/lavender/pink palette and tall lettering.

- [Option A — the little mechanism](option-a-mechanism.png): a larger physical horn and microphone with sparse supporting marks.
- [Option B — the orbit](option-b-orbit.png): a compact horn in an elliptical geometric rotation diagram.

Both retain the four control meanings from the technical brief; SPEED labels the motion selector and MIC labels microphone perspective. Display values are illustrative. Jason selected A's corner treatment and the short scoop variation, with the horn above-left and the microphone below-right in the empty center. The mic travels diagonally and no green proximity dashes are wanted. Final artwork uses flat native colors and uniform strokes, without the generated previews' slight texture. Native text/font compatibility and animation aliasing need verification on the OP-1.

## Interactive vector prototype

The current design is implemented in `rotary-preview.html`, an inline interactive preview, and [rotary-screen.svg](rotary-screen.svg), a standalone vector frame. All geometry and lettering are constructed paths; there are no image textures, external fonts, libraries or network requests. The SVG has a 320 by 160 viewBox. The corner treatment follows the selected composition using custom stroke lettering, rather than tracing generated pixels or reproducing the firmware font.

The Drive number now uses the same 13-unit letter height and stroke treatment as the other text readouts, replacing the initial 40-unit number at Jason's request. All four parameter labels already share the same height.

Play rotates the horn; Pause freezes it for inspection. Dragging Horn angle pauses playback and selects a pose. Speed offers slow, fast and brake, with visual acceleration and coast-down. Mic distance moves the mic along a diagonal away from the axis. Drive and Mix update the corner readouts. Actual screen size reduces the preview to 320 CSS pixels wide. This is a silent visual prototype, not the DSP effect.

The horn is a rounded rectangular loft in 3D, rotated about a vertical spindle and projected from a camera elevated 45 degrees. Face visibility and the mouth rim change with angle. The microphone remains outside the horn's complete rotation envelope at every distance setting. Its body is a simple facing illustration, aimed along the projected radial direction. There is no connecting line or dotted proximity guide.

### Source and regeneration

| File | Purpose |
| --- | --- |
| `prototype/rotor-geometry.js` | Portable geometry, projection, visibility, color palette and vector lettering. Shared by the preview and static SVG export. |
| `prototype/preview.js` | Playback, visual speed ramps, local controls and optional conversation-state persistence. |
| `prototype/preview-template.html` | Preview layout and controls. |
| `prototype/build-preview.cjs` | Builds the self-contained preview and SVG. |
| `prototype/check-geometry.cjs` | Checks a full rotation, corner stability, screen bounds and conservative mic clearance. |

From the repository root:

```powershell
node research/r3-147/ui/prototype/build-preview.cjs
node research/r3-147/ui/prototype/check-geometry.cjs
```

### Validation

Geometry checks cover 360 poses. The horn's maximum radius is 34.50 design units; a conservative lower bound on the nearest mic geometry is 78 units from the same axis, leaving at least 43.50 units of radial clearance. Projected horn bounds across all poses are x 105.50 to 174.50 and y 28.23 to 87.22, clear of the corner labels.

The rendered preview was checked in Chrome, including Play/Pause, angle scrubbing, mic distance, Drive, Mix, brake coast-down and a 360-pixel browser viewport. Front, side and rear poses were visually inspected. The check reported no script exceptions or horizontal overflow. The initial view is paused; playback requires an explicit click. No OP-1 execution or screen-cost benchmark has been performed.

For the firmware port, reuse the geometry and composition, but measure the cost of a suitable low-polygon or precomputed representation. The browser implementation regenerates its scene for clarity and is not an optimized firmware renderer. The single horn is a UI symbol for the effect; it does not require removing the independently modeled bass rotor from the DSP design.

## Compact horn variations

Three edits of A retain its corner layout while shortening the horn and placing the microphone farther from the axis:

- [1 — Short scoop](a1-short-scoop.png): closest to A, with a broad rounded rectangular mouth.
- [2 — Rounded elbow](a2-rounded-elbow.png): a curved body and oval bell opening.
- [3 — Faceted horn](a3-faceted-horn.png): simple straight edges and a square mouth.

These are static concepts, not verified rotation models. In implementation, compute the maximum horizontal radius of every horn vertex around the axis, including mouth corners. Place the nearest part of the microphone outside that radius plus a visible clearance at its nearest setting. Project both objects from the same approximately 45-degree elevated camera. Rotate the horn geometry around the vertical spindle and update face visibility; do not rotate the entire 2D sprite in the screen plane. The microphone moves along a radial line and remains aimed toward the axis. Its visual angle and travel can be finalized after selecting a horn shape.

[Final generation prompts for all three variations](compact-horn-prompts.md).

## Final generation prompt for option A

Use case: ui-mockup. Create one refined 2:1 landscape ORIGINAL OP-1 rotary-effect screen proposal. Attached images are STYLE REFERENCES ONLY: actual original OP-1 effects interfaces. Match their sparse technical illustration aesthetic, uniform fine vector strokes, DARK CHARCOAL #202020 background, periwinkle #6584ff, emerald mint #00d99b, pale lavender-white #dcd5ff, dim plum #493357 and coral pink-red #ff355e. This must look like another member of that exact UI family. Every object is an OUTLINE with background-colored interiors. Absolutely no gradients, shaded surfaces, glow, texture, filled white solids, realistic 3D or pixel/monospaced type. Thin tall condensed uppercase typography as in the reference. Screen artwork only, no device bezel, no design-board title, no watermark. Intended logical resolution 320x160 enlarged, use legible economical geometry. DESIGN A: A naive little physical rotating HORN, floating alone in the central 50 percent of the screen. View down on it at approximately 45 degrees. A short vertical spindle above a small thin elliptical hub/base, and a single bent tapered horn projecting diagonally toward the lower right, ending in a simple rectangular flared open mouth. Its structural contour is pale lavender-white; its tiny hub and spindle are blue. This is a rotating acoustic horn, not a megaphone with a handle and not a circular speaker cone. A short blue elliptical sweep under the base is sufficient to suggest rotation. Keep it mechanically plausible but toy-like, fewer than about 20 total shape edges, no cabinet or motor internals. To the right of the horn, ONE small green microphone drawn as a tilted outline capsule with three parallel grille strokes and a short stalk, pointing toward the horn. A tiny green dashed baseline between its foot and the horn's base indicates proximity travel. The microphone is the only other object. The main illustration is larger and more readable than a tiny icon but has clear negative space all around. Top left label 'SPEED' in blue, below it 'SLOW' in blue about equal size; bottom left label 'DRIVE' in pale lavender and below it a large very thin '12'. Top right label 'MIC' in green, below it 'NEAR' in green; bottom right label 'MIX' in coral-pink with a compact vertical stack of 8 short coral horizontal strokes beneath it as its level display. Side text does not overlap center artwork. No extra parameter readouts. Keep composition calm, open and simple. Match source images' stroke weight, technical cartoon outline language and original OP-1 palette closely. Do not copy their telephone, boxer, spring, car or grid objects. No tube, second rotor, cabinet, chassis, cables, arrows, decorative logos or elaborate dial furniture.

## Final generation prompt for option B

Use case: ui-mockup. Design one new original OP-1 rotary speaker effect screen in the exact restrained visual aesthetic of the attached original OP-1 effects screens. These three images are STYLE REFERENCES: looping geometric delay, perspective wireframe, and simple spring. Produce only a 2:1 wide landscape screen canvas on a completely UNIFORM FLAT #202020 charcoal background, no device or bezel. Strict thin consistent vector strokes, unfilled shapes, crisp tall narrow uppercase lettering. Colors only periwinkle blue #6584ff, mint green #00d99b, pale lavender #dcd5ff, muted dark plum #493357, coral pink #ff355e. Absolutely no gradients, grain, paper, shadows, lighting, depth shading, metallic surfaces, thick white solid objects, UI cards or faux-3D render. CONCEPT B: ROTARY ORBIT. The main graphic is a large clean elliptical blue orbit seen from above at about 45 degrees, centered slightly left of screen center. Inside it is ONE very simple SMALL COMPACT rotating acoustic horn in pale lavender outline, with a little circular hub at the ellipse center and a SHORT tapered bent spout aimed toward lower right ending in a rounded rectangular open mouth. Horn length about half the ellipse's horizontal radius: deliberately short and stubby like a toy, not a long trumpet. Show just enough outline geometry to read its top and one side. Short blue axle goes through the hub. A thin subdued plum ellipse just outside the blue one provides a faint second contour but no grid or radial spokes. The horn rotates about this hub in the future animation. On the right, ONE small mint-green microphone outline capsule, with two grille marks and a simple short stalk/base, aimed toward the horn. A short green straight proximity guide runs from the outer orbit toward the mic and ends in a green dot beneath its foot. No second microphone. This combines naive physical horn and abstract orbital geometry, with plenty of negative space; absolutely no cabinet, bass drum, rack, tube, engine machinery or extra scenery. Composition distinct from a schematic exploded drawing: one dominant oval, tiny horn inside, one mic outside. Use reference-style large thin label 'SPEED' in blue at top left with smaller 'SLOW' directly beneath. Green 'MIC' at top right with a large thin '25' beneath it. Bottom left pale lavender 'DRIVE' with a short horizontal row of four simple lavender level strokes beneath, of which first two are bright and last two dim plum. Bottom right coral 'MIX' with large thin '100' beneath it. Keep all four readouts outside the central ellipse and clear of microphone. Match the original reference's sophisticated economy and hand-drawn technical naivety. Avoid unnecessary labels, logos, title banners, arrows, sound waves or motion blur. This is a clean vector-like static mockup for a 320x160 logical display, presented enlarged.

