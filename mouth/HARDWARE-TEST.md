# Mouth: hardware test

Every step says what to do, then what you should see or hear. If what happens is different, write down the step
number and what happened, and keep going unless the OP-1 froze. At the end there's a short list of things to report.

You need: the OP-1 on Hundekraft 3.3.2, the computer with the workshop open, `build/mouth.knl`, and the
`mouth/presets` folder. About 30 minutes.

Words used here:

1. **Synth key:** the key with the blue wave symbol (synth mode).
2. **Sound keys 1–8:** the keys that pick one of the eight synth sounds.
3. **T1, T2, T3, T4:** the four keys for the engine, envelope, effect and LFO pages.
4. **Press a knob:** push the knob down until it clicks. **Turn one click:** turn it until you feel one notch.
5. **A middle key:** any white key near the middle of the keyboard, at the normal octave.

## 1. Install

1. **Remove the old version.** In the workshop, under **Your plugins**, remove **formant** if it's there. (Sounds
   that used formant will play the stock sound until you pick Mouth for them.)
2. **Add Mouth.** Under **Your plugins**, add `build/mouth.knl`. Press **Review changes**, then **Send to OP-1**.
   - Expect: the OP-1 restarts, and the workshop says the plugin is installed.

## 2. Pick Mouth for a sound

1. Press the **synth key**, then **sound key 8** (or any sound you don't mind changing).
2. Hold **Shift** and press **T1**. A list of engines appears.
3. Turn **blue** until **mouth** is highlighted, then press **T1**.
   - Expect: a new screen. At the top, a drawn mouth: white lips, a grey line of upper teeth, a red tongue. In the middle, a grey line with small ticks and the
     words "ee", "ah", "oo" under it, and a short blue mark above it. At the bottom, four labels in blue, green,
     white and orange: **vowel ah**, **body man** (or woman), **breath** with a percentage, **vibrato** with a
     percentage.
   - If the screen looks wrong (words overlapping, cut off, or missing), take a photo.

## 3. The first sound

1. Hold a middle key for about three seconds, then let go.
   - Expect: a voice babbling made-up syllables, about four a second, mostly around "ah". The mouth on screen opens
     and closes with the syllables (pressing shut, puckering or smiling on some of them), and a white dot hops left and right along the grey line.
   - When you let go: the babbling stops, and the voice fades out on one vowel. It should not keep babbling while it
     fades.

## 4. Talk speed (blue's page 2)

1. Press **blue**.
   - Expect: the blue label changes from "vowel" to **talk**, with **1/8** under it, and a second blue line appears
     above the blue line at the bottom.
2. Hold a middle key. Turn **blue** left one click at a time until it reads **off**.
   - Expect: each click is slower babbling (1/4t, 1/4, 1/2), then at **off** a steady "ah" with no syllables, and the
     mouth stops moving.
3. Still holding the key, turn **blue** right one click at a time until it reads **1/32**.
   - Expect: each click is faster babbling: 1/2, 1/4, 1/4t, 1/8, 1/8t, 1/16, 1/16t, 1/32. At 1/32 it's a fast
     chatter.
4. Turn it back to **1/8**. Press **blue** again.
   - Expect: the label goes back to **vowel**, and the second blue line disappears.

## 5. Talk follows the tempo

1. Press the tempo (metronome) key and set the tempo to **60** BPM with the knob it shows. Go back to the synth
   (synth key). Hold a middle key.
   - Expect: slow babbling, about two syllables a second.
2. Set the tempo to **120** BPM the same way, go back, and hold the key again.
   - Expect: twice as fast, about four a second.
3. Optional: start the sequencer playing a pattern on this sound. Expect the syllables to land in time with the notes.

## 6. Words (green's page 2)

1. Press **green**.
   - Expect: the green label reads **word**, with **key** under it.
2. Play the same middle key four times, holding each for about one second.
   - Expect: the same "word" (the same pattern of syllables) every time.
   - Then hold the same key for about four seconds.
   - Expect: a few syllables, a short silence, then the same syllables again, over and over. Some syllables start
     with a puff of air ("p"-like) or a short hiss ("s"-like).
3. Play the key next to it, the same way.
   - Expect: a different word.
4. Turn **green** right one click.
   - Expect: it reads **1**.
5. Play the two keys again.
   - Expect: now both keys say the same word (at their own pitches).
6. Turn **green** left until it reads **key** again. Press **green** to go back.

## 7. Wild (white's page 2)

1. Press **white**.
   - Expect: the white label reads **wild** with **50%** under it.
2. Hold a middle key. Turn **white** left, one click at a time, to **0%**.
   - Expect: the syllables get gentler and stay near "ah"; it sounds like mumbling.
3. Turn **white** right to **100%**.
   - Expect: the syllables jump between very different vowels, with sharper starts ("b", "p", "m", "s", "w", "y"-like), and the
     rhythm sometimes skips a beat.
4. Turn it back to **50%**. Press **white** to go back.

## 8. Page 1 knobs

Turn blue's page 2 (talk) to **off** first (step 4), so you hear one steady vowel. Hold a middle key for each.

1. **Blue (vowel):** turn slowly from fully left to fully right.
   - Expect: "ee" slides through "eh", "ah", "oh" to "oo", with no clicks. The label reads ee, ih, ay, eh, a, ah, aw,
     oh, oo along the way, and the mouth changes from wide and smiling ("ee") to open with the tongue showing ("ah") to
     small and puckered ("oo"). Every vowel about
     equally loud.
2. **Green (body):** turn from fully left to fully right.
   - Expect: the same note goes from a huge, dark voice to a tiny, bright one. The label reads giant, man, woman,
     child, tiny, and the mouth gets smaller (it fills a good part of the top of the screen at "giant"). About equally loud all the way.
3. **White (breath):** turn to fully right.
   - Expect: a whisper with only a faint pitch, and the lips slightly parted. Turn it back to about 10%.
4. **Orange (vibrato):** fully left, then halfway, then fully right.
   - Expect: fully left, no vibrato (just a slight natural wander). Halfway: a wide vibrato that starts within a
     quarter second, and the mouth wobbles with it. Fully right: a fast warble, and the label reads **warble**
     instead of vibrato.

Turn talk back on (blue's page 2 to **1/8**) when done.

## 9. Chords, voice stealing and range

1. Hold six keys at once (both hands).
   - Expect: a small choir, every voice babbling on its own. No distortion or crackle. Several white dots on the
     strip.
2. Keep holding five of them and play a seventh key a few times quickly.
   - Expect: no clicks when the new notes start.
3. Turn talk **off**. Play the lowest and highest keys, shifting the octave down and up as far as it goes.
   - Expect: a voice at every key: no silence, squeals or sudden loud notes at either end.

## 10. CPU

1. Hold **Shift** and press **Help** to open the clock screen, then press **T1**: the resource monitor.
2. Turn **blue** until the bottom line starts with **2/6 synth**.
3. Go back to the synth (synth key), turn talk to **1/32** and wild to **100%**. Return to the monitor (Shift + Help,
   T1). Hold six keys.
   - Write down the number after "synth".
4. Turn **blue** on the monitor until it shows **5/6 pk**. Press any knob to restart it, hold the six keys for a few
   seconds, and write down the number after "pk".
   - Expect: both well under 100. If the OP-1 shows "mouth muted: too slow" or "audio overload" at any time, write
     down when.

## 11. Presets and the LFO

1. **Copy the presets.** Hold **Shift** and press **COM**, then press **T3** (disk mode). On the computer, copy the
   `mouth/presets/mouth` folder into the OP-1 disk's `synth` folder (so the disk has `synth/mouth/babble.aif` and the
   rest). Eject the OP-1 disk.
   - Expect: the OP-1 restarts by itself, shows "Created synth patch." for each sound, and waits for a key press.
     Press any key.
   - If it shows **Failed to create synth patch** instead, the OP-1 is full of user sounds (it deletes the ones it
     can't take). Go back to disk mode, delete a preset folder you don't use from `synth`, copy the `mouth` folder
     again, and eject.
2. **Open a preset.** Press the **synth key**, then hold **Shift** and press a **sound key** (say 7).
   - Expect: a list of sounds. Turn **blue** to the **mouth** folder (it may be listed by engine or by folder name),
     and **green** to pick a sound in it. Write down which key you pressed to load it.
3. Load **babble** and hold a middle key.
   - Expect: babbling, and the **blue mark on the strip moves slowly by itself** (that's the LFO). The T4 LFO
     indicator shows it's on.
4. Load **talking wah**, hold a key.
   - Expect: no syllables, but the vowel sweeps quickly back and forth ("wah-wah-wah") and the blue mark swings
     widely.
   - If the blue mark doesn't move in 3 or 4, the LFO isn't reaching blue: press **Shift + T4**, write down the LFO
     type shown, and what its four values on screen are.
5. Play each of the other presets briefly: **choir ah** (hold a chord), **giant monk**, **chatterbox**, **whisper
   talk**, **robot words**, **warble diva**.
   - Expect: each sounds like its description in the README. Note any that are too quiet, too loud, or not what the
     name suggests.
6. **Press T4** on babble.
   - Expect: the LFO switches off; the blue mark stops moving. Press **T4** again to switch it back on.

## 12. Keeping settings

1. On sound 8 (from section 2), set talk to **1/16**, word to **3** and wild to **80%**.
2. Switch the OP-1 off and on again. Press the synth key and sound key 8.
   - Expect: Mouth is still the engine, and pressing blue, green and white shows 1/16, 3 and 80%.

## 13. Pitch and low end (0.5.1)

1. Turn talk **off** (blue's page 2), set blue to **ah** and orange fully left. Open your tuner app and hold middle C
   at octave **+0**.
   - Expect: the tuner reads **C** (it used to read G5).
2. Try **ee** and **oo** on the same key.
   - Expect: C each time.
3. Shift the octave to **+1** and **+2** and play a few notes on **ah**.
   - Expect: still a voice with some body, not thin and squeaky. At +2 it gets close to a pure, flute-like tone; that's
     how real high voices behave.
4. Repeat section 10 (CPU) once: hold six keys with talk at **1/32** and wild at **100%**, and write down "synth" and
   "pk" again. The new low end costs a little; "pk" should still be well under 100.

## 14. Sharper screen lines (0.5.2)

1. Play and hold a key on **ah**, so the mouth opens, and look closely at the screen.
   - Expect: the lips, the vowel strip and the line under each knob's value look crisp: no soft, doubled or half-lit
     rows. The lips are a touch bolder than before.
2. Turn blue slowly from **ee** to **oo** while holding the key.
   - Expect: the mouth still moves smoothly, without jumping or shimmering.

## What to report

1. Any step where something was different (step number and what happened), and any photos of the screen.
2. Section 10's two numbers ("synth" and "pk"), and section 13's.
3. Section 11: which key loaded a preset, and whether the blue mark moved in 3 and 4.
4. Anything that didn't sound fun, too loud or too quiet.
