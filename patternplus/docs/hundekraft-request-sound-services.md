# Request notes: reading and applying a whole sound

Background for a possible feature request to the Hundekraft maintainer, for
[issue #5](https://github.com/JasonBunting/op-1-tools/issues/5) (saving a sound with a Pattern+ collection). Checked
against the hundekraft 3.2.3 source (`ext/kennel/synth_sdk`) on 2026-11-08, not on the device. Design notes:
[sound-recall-design-notes.md](sound-recall-design-notes.md).

## What we want

1. Save a Pattern+ collection, optionally with the synth or drum sound it was made with, edits included.
2. Load it later and get the same sound back, even if the slot's preset has been changed since.

## What a plugin needs

1. **Read a slot's whole sound** as something it can store in a file: engine, knobs, envelope, effect, LFO.
2. **Apply a stored sound to a slot**, engine change included, with the same checks and saving as loading a preset.
3. **For sampled sounds**, a way to save and restore the audio as well, or to know reliably which audio a sound
   uses.

## Findings

1. **Apply needs a patch object nobody hands out.** `op1_sound_apply(int *patch)` (`op1_feature_sound.h`) is
   exported, but a plugin has no service that gives it a slot's patch object.
2. **No read service.** Drum slots have `OP1_DRUM_PATCH_JSON(s)` in `op1_feature_drum.h`; synth slots have no
   published equivalent. Either way, reading the JSON means calling jsoncpp at firmware addresses.
3. **The randomiser does it by fixed address** (`plugins/randomiser/rn_fw.c`): the synth patch objects at
   `0x01DDD7E8 + slot * 0x38`, then voices reset (`0x01A862D4`), `patch_set` (`0x01A052DC`), `op1_sound_apply`, and
   the patch's save job (`0x01A00D90`), after waiting for the background saver and a fade. Our probe
   (`research/soundprobe`) copies that sequence. The plugin guide asks authors to request a missing service rather
   than reach into firmware, hence this request.
4. **Snapshots don't cover it.** There's no snapshot load, and `op1_snapshot_save` only finishes a hold-to-save that a
   feature held back in `OP1_EV_SAVE_HOLD`.
5. **Samples live outside the patch JSON.** The stock background saver writes the audio
   (`op1_feature_sampler.h`), so copying the JSON alone may not restore a sampled sound.

## The request, drafted

> Could kennel offer services for reading and applying a whole sound? For example `op1_sound_get(kind, slot, buf,
> len)`, returning a synth or drum slot's patch JSON as text, and `op1_sound_set(kind, slot, text)`, applying it as
> loading a preset does: voices reset, validation (with the usual fallback for a missing engine or effect, keeping its
> name), the save job, and a screen refresh, on the main loop. The use: a sequencer plugin saves a collection with the
> sound it was made with, and restores both later. Today a plugin can do this only by copying the randomiser's fixed
> addresses. For sampled sounds and kits, is there a supported way to save and restore the audio too, or to tell
> which file a sound uses?

A follow-up question: would the core rather export the patch object (`op1_sound_patch(kind, slot)`) and keep the JSON
handling with the plugin?

## Status

Not sent. Waiting on the probe's device results, so the request can say what already works.
