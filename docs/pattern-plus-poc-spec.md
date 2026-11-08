# Pattern+ proof of concept

Planning handoff for Claude Code. Based on issue #1 and the owner's design discussion, November 5, 2026.

Issue: https://github.com/JasonBunting/op-1-tools/issues/1

## Objective

Build a playable, separate Hundekraft sequencer plugin for the original OP-1. Preserve the built-in Pattern sequencer's grid-editing experience, add multiple editable pages, and arrange those pages into looping chains. Reach a hardware POC quickly so the owner can evaluate the feel and iterate.

Do not modify the built-in Pattern sequencer. Do not target OP-1 Field. The existing Windows/Android mirror viewers are not part of this implementation.

## Confirmed interaction requirements

- Pattern+ is its own entry in the general sequencer selector.
- The pattern editor should look and work like the stock Pattern editor. Notes must be entered and edited on its grid using the familiar controls.
- A dedicated key combination navigates directly between pages, without opening a management menu.
- Moving forward selects the next existing page. Moving beyond the last existing page creates and selects a blank page. Returning to a page preserves its edits.
- Identify pages clearly as A, B, C, D, etc. The proposed metadata location is the lower-right area below the playback-direction indicator, itself below HOLD. Verify the available space on the real device before finalizing placement.
- A different key combination switches to a chain editor/viewer.
- Display the chain as an ordered list, for example `A > B > B > C > D`.
- During playback, the user can watch either the grid view, which follows the playing page, or the chain view, which highlights the current chain position. Consecutive occurrences of B must be distinguishable.
- Support saving and loading patterns and their chains. Use Hundekraft's shared text-entry screen for collection names if named saving is included in the first hardware build.

## Data model

- **Page:** one Pattern-style grid and its playback parameters.
- **Chain:** an ordered list of references to pages; repetitions are allowed.
- **Collection:** the pages and chain saved together.
- **Editor state:** selected page, selected chain position, and active view.
- **Playback state:** playing page, chain position, and position within the page, kept separate from editor selection.

Default interpretation: both B entries reference the same page, so editing B changes both occurrences. This is a proposed implementation default, not an independently confirmed requirement.

Use stable page identifiers and a versioned storage representation. Do not persist raw C structs or pointers. Validate counts, lengths, and page references before accepting saved data.

## POC scope and provisional choices

The following choices keep implementation moving; expose them clearly in the handoff report and expect iteration:

- Start with a bounded page bank and bounded chain. Choose small practical limits after checking memory; support at least the four-page example and repeated chain entries. Four pages is not a permanent product requirement.
- At the page limit, indicate that no further page can be created; never overwrite an existing page silently.
- Provide previous/next navigation; previous on A remains on A.
- A new collection starts with empty page A. Playing without an explicit chain loops the selected page.
- Chain editing needs only append/select a page, remove an occurrence, and move the chain cursor. Reordering can initially use remove and insert.
- Loop the chain and transition at the end of each page's configured playback cycle. Exact interaction with direction, section length, swing, and note gates needs a defined timing contract before implementing transitions.
- Switching grid/chain views must not restart playback.
- Default to following playback in grid view. Manual page navigation during playback should suspend following so editing is possible; define a simple way to resume following. This behavior is provisional and should be tried on hardware.
- Begin with one chain per collection. Multiple chains per collection can follow later.
- Prefer restoring the last collection and selected page on reopen. New/Open/Save can live in a secondary menu; a startup chooser is not essential to the first hardware test.

Do not silently claim full stock parity if a behavior is deferred. Identify any temporary omissions explicitly before hardware evaluation.

## Controls and visual fidelity

Do a short stock-Pattern control inventory before assigning shortcuts. Preserve note entry, erasure, cursor movement, swing, note rotation, length/section selection, HOLD, direction, and applicable live-edit behavior.

Shift + encoder turns already have stock editing meanings. Shift + encoder presses are candidates, not approved final mappings. Check the SDK's actual event routing and conflicts with global Hundekraft shortcuts. Choose explicit previous/next page and chain-view controls and document them in a compact control table.

Recreate the familiar grid layout using available drawing facilities. Verify labels, colors, cursor, playhead, HOLD, direction, and the added page indicator against the owner's device. Do not assume that the stock screen implementation is reusable through the plugin API.

## Existing technical foundation

The Hundekraft source is kept outside the repository, at `../OP-1-Related/hundekraft-3.2.3-source/`. Relevant references:

- `ext/kennel/synth_sdk/op1_feature_seq.h`: custom sequencer registration, player contract, input callbacks, screen registration, MIDI-start callback, and note-on/off services.
- `ext/kennel/synth_sdk/seq/dots.c` and `seq/euclid.c`: existing sequencer implementations, multiple patterns, and persistence examples.
- `ext/kennel/synth_sdk/seq/seq_text.h`: storage helpers referenced by the sequencer API.
- `ext/kennel/synth_sdk/op1_feature.h`: shared text-entry API, settings, and file services.
- `ext/kennel/synth_sdk/kennel/kn_exports.c`: exports available to loadable plugins.
- Repository `firmware/mirror/` and `tools/pack-plugin.sh`: existing local plugin packaging conventions.

These paths are relative to `hundekraft-3.2.3-source/` except the last item. The source tree is locally available but untracked; do not assume it exists in a fresh clone or commit it wholesale.

The inspected SDK provides `op1_text_entry()` as a shared modal dialog with completion callback and a configurable limit up to 24 characters. The preset-naming feature uses a smaller limit. Pattern+ should consume the returned text and implement its own collection persistence; text entry does not save a collection for us.

The API describes per-mode synth/drum sequencer selection and 96 ticks per beat. Verify behavior against the target 3.2.3 build. Keep synth/drum state separate unless a different behavior is explicitly agreed.

Useful documentation:

- Original Pattern controls: https://teenage.engineering/guides/op-1/original/sequencers
- Hundekraft naming UI and screenshot: https://hundekraft.github.io/op1-workshop/manual.html ("Name your sounds")

## First technical checkpoint

Before implementing the full editor, prove that a standalone `.knl` can register a new sequencer on the installed 3.2.3 firmware, show a custom screen, receive the needed inputs, play/stop notes correctly, and restore a small saved value.

Verify packaging and browser registration, required assets, available exports, memory limits, persistence context, and callback timing. Source declarations are evidence of an intended interface, not proof that an arbitrary external plugin works on the installed build. If core changes appear necessary, report the specific blocker before expanding scope.

Keep timing-sensitive playback separate from file/database writes and text entry. Pair emitted notes with note-offs; handle stop, reset, mode changes, and page transitions without stuck voices. Reuse proven SDK patterns for drawing, storage, and event dispatch.

## Suggested delivery order

1. Minimal standalone sequencer integration checkpoint.
2. Familiar editable grid, page indicator, and direct previous/next/create navigation.
3. Chain editing, looping playback, grid following, and chain-position highlighting.
4. Persistence and recovery. An initial hardware milestone may restore one collection automatically; named New/Open/Save completes the broader POC save/load workflow.
5. Hardware feedback and control/layout revisions.

Each playable milestone is useful. Do not postpone the first device test for a polished browser, advanced management, or a generalized framework.

## Acceptance scenarios

- Enter notes on A using the familiar Pattern workflow. Advance to blank B, enter different notes, then return to A and verify its notes are intact.
- Navigate among existing pages without accidentally creating or clearing them. Advance beyond the last page to create exactly one new page per deliberate navigation action.
- Build `A > B > B > C > D`. Hear the expected order and two complete occurrences of B.
- In chain view, see the active occurrence advance correctly. In grid view, see each corresponding page and its playhead.
- Switch views during playback without resetting timing. Exercise manual navigation while playing and verify the documented follow behavior.
- Stop, restart, change modes, and exercise supported sync/reset behavior without stuck notes or duplicate first steps.
- Save, restart the device, and recover the expected pages and chain. For named collections, save two distinct collections and load each correctly.
- Reject invalid saved data without a crash or silent overwrite. Handle capacity and save failures visibly.
- Confirm the new page label is legible and does not obscure stock editing information on the real screen.

Use host-side checks for chain traversal, page boundaries, and serialization where practical. Hardware validation is required for input feel, timing, rendering, and persistence; distinguish tested behavior from assumptions.

## Implementation handoff expectations

Read applicable repository instructions and preserve unrelated local changes. Keep Pattern+ source in a dedicated directory, tentatively `firmware/patternplus/`. Follow existing licensing requirements for SDK-derived code. Do not publish, flash the device, or change the installed firmware without explicit authorization.

Deliver the plugin artifact, build instructions, exact control mapping, supported features, known limitations, and a short hardware test checklist. Keep progress reports concise. This document is a planning artifact; its creation does not authorize coding in the current planning chat.
