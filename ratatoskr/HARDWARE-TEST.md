# Ratatoskr: hardware test

Every step says what to do, then what you should see or hear. If what happens is different, write down the step
number and what happened, and keep going unless the OP-1 froze. At the end there's a short list of things to report.

You need: the OP-1 on Hundekraft 3.3, the computer with the workshop open, and `build/ratatoskr.knl`. About 20
minutes.

Words used here:

1. **Synth key:** the key with the blue wave symbol (synth mode).
2. **Sound keys 1–8:** the keys that pick one of the eight synth sounds.
3. **Press a knob:** push the knob down until it clicks.
4. **A middle key:** any white key near the middle of the keyboard, at the normal octave.

## 1. Install

1. In the workshop, under **Your plugins**, add `build/ratatoskr.knl`. Press **Review changes**, then **Send to
   OP-1**.
   - Expect: the OP-1 restarts, and the workshop says the plugin is installed.

## 2. Record a sample

1. Press the **synth key**, then **sound key 8** (or any sound you don't mind changing).
2. Hold **Shift** and press **T1**. Turn **blue** until **sampler** is highlighted, then press **T1**.
3. Press the **input key** (top right, with the microphone symbol) so the sampler is ready to record from the mic.
4. Hold any keyboard key and sing, speak or play something with a melody into the mic for about five seconds, then
   let go of the key.
   - Expect: the sampler shows the recorded waveform, and keys play it back.

## 3. Pick Ratatoskr

1. Hold **Shift** and press **T1**. Turn **blue** until **ratatoskr** is highlighted, then press **T1**.
   - Expect: a new screen. Near the top, the recording as white and grey columns; the middle half is white. A green
     bracket under the white part, a blue arrow on it, and a thin white line through the columns. At the bottom, four
     labels in blue, green, white and orange: **position** with a time in seconds, **size 100ms**, **speed +0.25x**,
     **density 2.0x**.
   - If the screen says **record a sample first**, the recording didn't take: go back to step 2.
   - If anything overlaps, is cut off or is missing, take a photo.

## 4. The first sound

1. Hold a middle key for ten seconds, then let go.
   - Expect: a soft, smeared version of your recording, slowly moving forward through it at about a quarter of its
     speed, at its own pitch on C4. The blue arrow and the white line creep to the right; red ticks flicker under the
     bracket while the key is held. Letting go fades the sound with the stock envelope.
2. Play a few different keys, one at a time.
   - Expect: higher keys sound higher, lower keys lower. Each new note carries on from where the last one left off.
3. Hold a chord of three keys.
   - Expect: three pitches of the same cloud, and three rows of red ticks.

## 5. Page 1 knobs

Hold a middle key through each step (or set the sequencer to hold notes).

1. **Speed (white):** turn white fully left, then to the middle, then fully right.
   - Expect: fully left, the playhead runs backwards fast (the arrow moves left, the sound plays backwards). In the
     middle, the label says **frozen** and the arrow stops: a held, unchanging texture. Fully right, it rushes forward
     at **+4.00x**. At three quarters right it shows about **+1.00x** and moves at the recording's own speed.
2. **Position (blue):** set white to about +0.25x again, then turn blue from left to right.
   - Expect: the white part of the columns and the green bracket slide along the recording; the sound changes to that
     part.
3. **Size (green):** turn it fully left, then fully right.
   - Expect: fully left (10ms), a buzzy, grainy sound. Fully right (500ms), long smooth swells where words or notes
     are recognisable.
4. **Density (orange):** turn it fully left, then fully right.
   - Expect: fully left (0.5x), separate grains with gaps between them. Fully right (4.4x), a thick, smooth cloud.
     The volume should stay roughly even; it should not distort.

## 6. Page 2 knobs

1. **Spray:** press **blue**. The blue label changes to **spray** and a second line appears above it. Turn blue fully
   right, listen, then fully left. Press blue again to go back.
   - Expect: fully right, grains come from all over (up to 1.5s around the playhead): a scattered, shimmering cloud.
     Fully left, they all come from one spot.
2. **Width:** press **green** (label **width**). Turn it fully left, then fully right. Press green again.
   - Expect: fully left (0%), the bracket shrinks to a point and the playhead stops moving even with speed up. Fully
     right (100%), the bracket covers the whole recording and the playhead travels the whole of it, wrapping from the
     end back to the start.
3. **Wander:** press **white** (label **wander**). Turn it to 100% and hold a key for twenty seconds.
   - Expect: the white line wobbles around the arrow, and the arrow now and then jumps to a new place or turns round.
     The sound drifts and jumps around the recording. At 0% it moves steadily again. Press white again.
4. **Sample browser:** press **orange**.
   - Expect: the core's sample browser opens, listing the sampler presets. Pick one and press **white** to load it.
     The columns change to the new sample and Ratatoskr plays it. (Close the browser the way the core says.)

## 7. Saving

1. With Ratatoskr set up how you like it, switch to **sound key 1** and back to **sound key 8**.
   - Expect: all seven settings (four page 1, three page 2) are as you left them.
2. Turn the OP-1 off and on again, then select **sound key 8**.
   - Expect: Ratatoskr is still the engine, the recording is still there, and the settings are kept.

## 8. Load (optional, skip if short on time)

1. Set density fully right, size fully right, and hold six keys at once for ten seconds.
   - Expect: no crackles, dropouts or a frozen screen. If it crackles, note which settings and how many keys.

## Report

1. Any step that didn't match, with its number.
2. Photos of the screen if it looked wrong.
3. How the slow speeds (0.1x to 0.5x) sound: smooth, or rough and phasey? This decides whether smoother stretching is
   worth adding.
4. Anything about the controls that felt backwards or hard to find.
