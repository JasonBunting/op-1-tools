# Sound recall: design notes

Working notes for [issue #5](https://github.com/JasonBunting/op-1-tools/issues/5): saving the synth or drum sound, with
its settings, alongside a Pattern+ collection. These are notes from a discussion that is still going; nothing here
is decided unless it says **Agreed**. Checked against the Hundekraft 3.2.3 source on 2026-11-08, not on the device.

## Where things stand today

1. Named collections are files, 8 slots per mode: `/yaffs2/user/patternplus/s1.txt`..`s8.txt` (synth) and
   `d1.txt`..`d8.txt` (drum), written and read on the main loop (`plugin/patternplus.c`, `pp_save_done`,
   `pp_do_load`).
2. Each file is a name line followed by PP1 text (`plugin/pp_store.c`): pages, notes, playback settings, pitch key,
   the selected page and the chain, plus a checksum. Nothing about the sound.
3. The current work also follows every change into `op1.db` through `sq_store`. That is separate from named
   collections and out of scope here.

## What Hundekraft offers

1. **No existing convention.** No bundled sequencer (c64arp, euclid, trail) saves a sound with its data.
2. **A synth sound is a small JSON record** (`ext/kennel/docs/preset-identity.md`): `type` (the engine's name),
   `knobs[8]`, `adsr[8]`, `fx_type`, `fx_params[8]`, `fx_active`, `lfo_type`, `lfo_params[8]`, `lfo_active`. That
   covers everything the issue lists as "edited state".
3. **Reading and applying a whole sound has a precedent.** The randomiser (`synth_sdk/plugins/randomiser/rn_fw.c`)
   reads a slot's live words and writes a full sound back: voices reset, `patch_set`, `op1_sound_apply`, then a save
   job. It reaches the patch objects and several firmware functions by fixed address, not through SDK services. The
   plugin guide prefers SDK services and asks for a missing service to be requested rather than patched in.
4. **Snapshot save** (`op1_snapshot_save` in `op1_feature.h`) saves a slot as a named preset under
   `preset_<kind>/snapshot/`. There is no matching load service.
5. **User files** under `/yaffs2/user/` (`op1_file_*`), already used for collections.
6. **Rules from the plugin guide that apply:**
   1. Persistent writes happen on the main loop, never in the audio thread.
   2. Check that the slot and engine are still the same before committing a write that had to wait.
   3. A missing engine or effect falls back to Iter or filter but keeps its saved name, so reinstalling restores it.
      Don't rewrite missing names to stock ones.
   4. Don't overwrite a payload you can't decode with defaults.
7. **Not yet looked at:** how drum kits and sampler sounds are stored (drum+, `kn_drum.c`, the AIFF kit format), and
   what a kit's edits consist of.

## Why stock OP-1 keeps them apart

1. **Documented:** synth and drum modes each have their own sequencer memory
   ([guide: sequencers](https://teenage.engineering/guides/op-1/original/sequencers)), and presets live in separate
   snapshot folders ([guide: synthesizer mode](https://teenage.engineering/guides/op-1/original/synthesizer-mode)).
   No source found that explains why.
2. **Our guesses, unsourced:**
   1. A sequence belongs to a mode, not to a sound, so you can change sounds while it plays. That's a big part of how
      people play the OP-1 live.
   2. Sequencers are performance tools and sounds are design objects; TE keeps the layers apart.
   3. A snapshot file is only a sound, which keeps browsing and sharing simple.
3. Other gear has the same split and the same pitfall: Roland's JD-08 warns that a pattern can sound different if its
   patch changes after saving
   ([Roland support](https://support.roland.com/hc/en-us/articles/4411755353755-JD-08-The-patterns-when-reloaded-sound-different-when-played-back-than-they-did-before-they-were-saved)).

## Direction

1. **Agreed:** saving the sound with a collection is optional.
2. **Agreed:** a sound coupled to a collection can be decoupled later.
3. **Proposed:** keep the sound in a companion file next to the collection (for example `s1.snd` beside `s1.txt`)
   instead of inside the PP1 text. The PP1 format doesn't change, existing collections load as before, and coupling
   or decoupling only adds or removes the companion.
4. **Proposed:** each collection keeps its own copy of the sound. Shared copies would bring back the issue's
   mutable-slot problem: editing one would change every collection that uses it.
5. **Proposed load choices:** pattern only (the default when there's no sound), pattern and sound, and sound only.

## Open questions

1. Can a plugin read and apply a whole sound through SDK services, or does it need a new core service? If the latter,
   draft a request like `hundekraft-request-background-drums.md`.
2. Drum kits and sampler sounds: embed the audio, or store a reference with a check that it hasn't changed? How big is
   a kit?
3. Saving: ask every time, or a setting such as "always include sound"? The OP-1 has few buttons to spare.
4. Decoupling: keep the companion file (so it can be relinked), or delete it? Keeping seems the safer default.
5. Loading a sound: which slot does it go into (the one it came from, or the current one), and does it overwrite
   that slot's saved preset or only the live sound until you save?
6. What happens when the saved engine, effect or LFO isn't installed? The core's fallback (Iter, keep the name) may
   be enough.
7. Does the sound need saving with every page or chain, or once per collection? Once per collection is the
   assumption so far.
