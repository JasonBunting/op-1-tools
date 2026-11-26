# Chord glide: design draft

Issue 14. Working name: **glide**. A note-pipeline feature that, on a chord change, slides the voices already sounding
into the new chord instead of letting the new notes strike. It works on any synth through the per-voice pitch core
hook (`kn_vpitch.c`, proven on the device 2026-11-23: iter, string, fm, dimension).

Behaviour comes from Brickglide (`brickchord/ambient.py`, MIT, Michael Tietjen): voice leading, glide paths, moves,
timing. Mono output, no pan.

## 1. Pieces

1. **Core (our kennel patch):** turn the test loop into an API and export it (`kennel/kn_exports.c`):
   1. `op1_voice_pitch_set(voice, cents)`: the voice's offset from its note. The core ramps from the last value to
      the new one across the 128 samples of the next block, so per-block updates don't step (no zipper).
   2. `op1_voice_state(voice, *note)`: whether a synth voice is sounding and its note. R246 keeps a sounding flag
      per voice (`0xFF80105C`) and the target pitch (`0xFF8012D4`, set by VoiceCtl's note-on `0x01A84414`); addresses
      from op1-sdk, to be checked in our R246 disassembly.
   3. Offsets clear on a voice's note-on, so a plain note never inherits an old offset.
2. **Plug-in (`glide/plugin/`, a feature):**
   1. note pipeline stage after chord mode (priority 250: scale 100, chord 200), so hands, chord mode and sequencers
      all feed it;
   2. `tick` (audio thread, every block) moves each voice along its path and calls `op1_voice_pitch_set`;
   3. a clock page for the controls and the pitch-trail screen.

## 2. Behaviour

1. **Grouping:** note-ons within a short window (about 30 ms) count as one chord. Chord mode sends its notes
   together; hands are looser.
2. **First chord:** passes through as normal notes. The plug-in records which voice each note landed on (reading
   the voice state right after passing the note on).
3. **Next chord:** the new note-ons are held back. Brickglide's voice leading maps sounding voices to the new tones
   (lowest to the root, the rest to the nearest unused tone above the voice below). Each voice glides from where it
   is now: no new note-on, no attack.
4. **More tones than voices sounding:** the extra tones play as new notes (they strike). A later version could
   start them on a neighbouring voice's pitch and glide them out (Bloom style).
5. **Fewer tones:** the extra voices double chord tones (Brickglide keeps every voice), or are released (a setting).
6. **Mid-glide change:** each voice starts the new glide from its current pitch: no jump.
7. **Releasing keys:** while any key is held, releases are swallowed (the voices now play the new chord, not the keys
   you hold). When the last key comes up: normal release, or **Hold** (drone until the next chord or a stop press).
8. **Range:** a voice stays within ±36 semitones of the note it struck. Voice leading keeps moves small, so that's
   rarely reached; past it, that voice restrikes.
9. **Poly play mode only.** In mono, legato or unison, notes pass through untouched.

## 3. Controls (first version)

1. **Time:** glide length, 0.1 s to 60 s.
2. **Path:** Smooth, Linear, Ease, Swell, Bounce, Late, Snap, Wobble, Semitones, Scale steps.
3. **Move:** none, Bloom, Cascade up, Cascade down, Spiral, Swoop, Scatter, Converge, Fall.
4. **Timing:** Together, By distance, Bass lags, Bass leads, Loose.
5. Later: Scrub (an encoder plays the glide by hand), Hold, scenes, drift / vibrato.

## 4. Screen

Each voice as a line of its pitch over time against guides for the scale, with its destination marked (Brickglide's
trail view). Stock-OP-1 look.

## 5. Known limits

1. Timbre that follows the note (a filter that tracks the key, a sample's zones) stays at the struck note while the
   pitch moves. That depends on the synth.
2. VoiceCtl steals a voice when a 7th note arrives; the plug-in must notice and drop that voice from its record.
3. The plug-in needs our patched core. On a stock Hundekraft core it should refuse to load, not misbehave.

## 6. Questions for you

1. **Release:** when you let go of every key, normal release or Hold (drone) by default?
2. **Extra voices:** fewer tones than voices: double chord tones, or release the extras?
3. **Sources:** glide notes from sequencers and MIDI too, or only keys and chord mode?
4. **Controls:** which T key for the page (T4 is chord mode's)?
5. **Name:** keep "glide", or something else?
