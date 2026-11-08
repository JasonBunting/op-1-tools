# Request notes: a drum pattern that keeps playing on the Synth screen

Background for a possible feature request to the Hundekraft maintainer. Checked against the hundekraft 3.2.3 source (`ext/kennel/synth_sdk`) on 2026-11-08, not on the device unless stated.

## What we want

1. On the Drum screen, start a Pattern+ drum pattern looping with HOLD.
2. Press the Synth key. The drum loop keeps playing on the drum kit.
3. On the Synth screen, play keys and build a Pattern+ synth pattern over the drums, both in time.

Today the drum pattern stops when you press the Synth key. That was seen on the device with Pattern+ 0.3.3, and stock Pattern seems to do the same.

## What a plugin needs

1. **A clock that keeps running in any mode.** Probably available already; see below.
2. **A way to send notes to the drum kit while another instrument is on screen.** Missing; this is the request.
3. **A way to stay running when the mode changes.** The core stops the player when you leave the mode; see below.

## Findings

1. **Sequencer ticks reach only the current mode's player.** `op1_feature_seq.h`, the player vtable comment: "+0x2c tick (96 per beat, the current mode's player while the sequencer runs)".
2. **A feature tick runs whatever the mode.** `op1_feature.h`: `void (*tick)(const op1_beat_t *beat)` is called on the audio thread every 64-sample block, about 689 times a second. `op1_beat_t` in `op1_plugin.h` gives the beat length (`len`, in 1/16384 samples), the time to the next beat (`next`), and the clock advance per block (`step`). A plugin could count steps from this itself. Unchecked: `follows_tape` says the clock can stand still while the tape is stopped, so it may not run without the tape.
3. **Notes can't be aimed at an instrument.** `op1_seq_note_on(mode, note, key, vel)` (`kennel/kn_seq.c`, `seq_note_on`) uses `mode` only to work out the note number and the drum pitch. It builds an `op1_note` and calls `op1_key_down`, which is R246's VoiceCtl key_down at `0x01A85750` (`kennel/kn_tape.c`). `op1_note` (`op1_feature.h`: note, velocity, send_midi, extra) has no field for the target instrument, so the note plays on the instrument on screen. On the Synth screen, a drum pattern would play the synth.
4. **Leaving the mode stops the player.** Stock R246 Pattern's stop (`0x01A08BC0`) clears running, HOLD and the transposition. Pattern+ does the same in its stop callback since 0.3.2. On the device, pressing the Synth key stops the drum loop, so the core seems to call stop on a mode change. This is inferred from the device, not traced in the firmware.

## The request, drafted

> Could kennel offer a way for a feature to play notes on a specific instrument (the drum kit, or the synth) whatever is on screen, for example `op1_seq_note_on_to(target, note, key, vel)` with a matching note-off? The use: a sequencer keeps a drum pattern looping, timed from the feature `tick`, while the user builds a synth pattern on the Synth screen. A plugin can already keep time from `tick`, but `op1_key_down` always plays the instrument on screen. If R246's voice handling makes this impractical, for polyphony or engine scheduling, knowing that would help too.

A follow-up question: should the core keep calling a player's tick while its mode isn't shown, or should a plugin time itself from the feature `tick`?
