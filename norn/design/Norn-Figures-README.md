# Norn figure SVG

Use `norn-figures.svg` as the static Three Keepers artwork. This is a hand-authored vector adaptation of the approved image, simplified for the small display; it is not a pixel-exact vectorization. The thread origins retain the mockup's positions.

## Asset contract

- Canvas and viewBox: `0 0 320 160`.
- Three filled paths: `figure-top`, `figure-mid`, `figure-bot`.
- One flat fill: `#709CFF`.
- Transparent background and negative space; no black shapes.
- No strokes, gradients, filters, masks, clipping, embedded images, external resources, or transforms.
- Path commands are absolute `M`, `L`, `C`, and `Z`.
- Standard nonzero fill rule. Facial openings use reverse-wound subpaths. Preserve subpaths and their winding when compiling the artwork.
- Robes end at y = **146.863**. All hands are above that boundary.
- No threads or painted thread tails. The tapered shapes descending from the forearms are robe sleeves, attached to the figures.

| Path | Authored coordinate pairs, including Bézier control points |
| --- | ---: |
| `figure-top` | 119 |
| `figure-mid` | 243 |
| `figure-bot` | 121 |

These counts describe SVG path data, not the number of vertices after a renderer tessellates curves. If the SDK's 256-point limit applies to tessellated output, verify the compiler's result and split the middle figure's components as needed. Do not discard contour points or close separate subpaths together.

## Thread anchors

Coordinates are in the final 320 × 160 viewBox, with no additional transform. Each point is also an explicit vertex of its corresponding hand silhouette. Machine-readable values are in `norn-hand-anchors.json`.

| Anchor | X | Y | Thread |
| --- | ---: | ---: | --- |
| `hand-top` | 79.717 | 73.307 | Green / Harmonics |
| `hand-mid` | 84.131 | 104.568 | White / Timbre |
| `hand-bot` | 87.441 | 136.198 | Orange / Morph |

The SVG contains no anchor circles to remove. Draw the generated threads from these points beneath the figure artwork so the fingers cover the contact point. The empty right-hand canvas is reserved for that live weave.

## Validation and use

Validated as XML and checked against the element, fill, coordinate-count, and vertical-bound constraints. Inspected in a browser enlarged and at 320 × 160. Device compilation and OP-1 rendering are not yet validated.

This supplies the clean figure-only asset requested in `Claude-UI-Handoff.md`; it supersedes that document's instruction to prepare the figure asset. It does not replace the UI mockup or change its control mapping. The browser comparison still uses its original raster figure layer until explicitly updated.

The source figure concept was generated with OpenAI image-generation tools during the project's design process. This SVG was subsequently authored as a simplified vector adaptation for Norn.
