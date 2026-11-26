# Ratatoskr: hardware test (0.3.8)

Every step says what to do, then what you should see or hear. If what happens is different, write down the step
number and what happened, and keep going unless the OP-1 froze. At the end there's a short list of things to report.

You need: the OP-1 on Hundekraft 3.3.2, the computer with the workshop open, and `build/ratatoskr.knl`. About 25
minutes. 0.3.0 is a redesign: the knobs mean different things from 0.2, so a sound saved with 0.2 will sound different.
To go back, `build/versions/ratatoskr-0.2.2.knl` is the last 0.2.

Words used here:

1. **Synth key:** the key with the blue wave symbol (synth mode).
2. **Sound keys 1–8:** the keys that pick one of the eight synth sounds.
3. **Press a knob:** push the knob down until it clicks.
4. **A middle key:** any white key near the middle of the keyboard, at the normal octave.

## 1. Install

1. In the workshop, under **Your plugins**, replace ratatoskr with `build/ratatoskr.knl`. Press **Review changes**,
   then **Send to OP-1**.
   - Expect: the OP-1 restarts, and the workshop says the plugin is installed.

## 2. Pick Ratatoskr

1. Press the **synth key**, then **sound key 8** (or any sound you don't mind changing).
2. Hold **Shift** and press **T1**. Turn **blue** until **ratatoskr** is highlighted, then press **T1**.
   - Expect: a tree in the middle of the screen, almost the full height: an eagle's head at the top, a green horned
     dragon at the roots. If the sound has a sample, the trunk is made of thin horizontal lines (the recording, from
     the roots up), with an orange and brown squirrel to its right, halfway up, pointing at the trunk. With no sample,
     the trunk and squirrel are missing and the screen says **no sample**. Down the left side, four rows in blue,
     green, white and orange, each with a small name over a larger value and a short coloured bar beside it:
     **position** (seconds), **size 50 ms**, **scatter 0 ms**, **motion hold**.
   - Take a photo of the screen either way. Note anything that overlaps, is cut off, or is hard to see (especially the
     dark parts of the trunk).

## 3. Record a sample into it

1. Press the **input key** (top right, with the microphone symbol).
2. Hold any keyboard key and sing, speak or play something with a melody into the mic for about five seconds, then
   let go of the key.
   - Expect: the screen stays on Ratatoskr. The trunk changes to the shape of your recording: wide rows where it was
     loud, narrow where it was quiet. The squirrel sits halfway up.

## 4. The first sound (hold)

1. Hold a middle key for five seconds, then let go.
   - Expect: a steady, repeating texture of the moment halfway through your recording, at its own pitch on C4. White
     dashes flicker left of the trunk, level with the squirrel. The squirrel doesn't move.
2. Hold C4 and the C an octave above together.
   - Expect: the same texture an octave apart.

## 5. Page 1 knobs

Hold a middle key through each step.

1. **Position (blue):** turn blue slowly from left to right.
   - Expect: the squirrel climbs from the roots to the crown, and the sound changes to that part of the recording. The
     blue value shows the position in seconds while you turn.
2. **Size (green):** turn it fully left, then fully right, then back to **50 ms**.
   - Expect: fully left (**12 ms**), a buzzy, grainy sound. Fully right (**250 ms**), longer, smoother pieces where
     words or notes are recognisable.
3. **Scatter (white):** turn it right to about **200 ms**.
   - Expect: the white dashes spread above and below the squirrel while the squirrel stays still; the sound becomes a
     blur of nearby moments. Fully right is **500 ms**. Turn it back to **0 ms**.
4. **Motion (orange):** turn it one zone at a time through all six names, holding a key for about five seconds on
   each.
   - **hold:** the squirrel stays put.
   - **forward:** the squirrel climbs slowly (a quarter of the recording's speed), tilted up; at the top of the range it starts
     again at the bottom. The sound plays forward, stretched out, at the same pitch.
   - **reverse:** it climbs down, tilted down, and starts again at the top.
   - **bounce:** it climbs to the top of the range, turns round, climbs down to the bottom, and so on.
   - **wander:** it drifts a little up and down around where it was, changing aim about once a second.
   - **jump:** it leaps to a new spot about once a second and stays there until the next leap.
5. **Phrases:** set **forward**. Hold a key for five seconds, let go, and look at the squirrel; then press the key
   again.
   - Expect: when you let go, the squirrel stops where it is. When you press again, it starts again from the position
     (halfway, unless you turned blue). While one key is held, pressing a second key does not move it back.
6. **Live position:** still on **forward**, hold a key and watch the blue value.
   - Expect: it counts up with the squirrel. While you turn blue it shows the position you're setting instead, and
     about a second after you stop turning it goes back to following the squirrel.

## 6. Page 2 knobs

1. **Range:** press **blue**. The label changes to **range 100%** and a second bar appears beside it. Turn it left to
   about **20%**, with motion on **bounce**, and hold a key.
   - Expect: only the part of the trunk the squirrel travels stays bright; the rest goes dim. The squirrel bounces
     between the ends of the bright part. Turn blue back to page 1 (press
     it), then turn it fully left or fully right: the range slides inward to stay the same size at the ends of the
     recording. Press blue, set **100%**, press blue again.
2. **Shape:** press **green** (label **shape hann**). A curve appears on the right under **grain shape**.
   - Turn it through **hann**, **triangle**, **pluck**, **swell** with a key held and size around 150 ms.
   - Expect: the curve changes: a rounded hill, a pointed peak, a fast rise with a long fall, a long rise with a fast
     fall. Pluck sounds more attacked, swell more like a backwards swell. The squirrel and the dashes keep the same
     rhythm. Press green again: the curve goes away.
3. **Speed:** press **orange** (label **speed**).
   - On **hold** it shows **-**.
   - On **forward**, **reverse** or **bounce** it shows a speed: **0.25x** in the middle, **0.05x** fully left, **2.00x**
     fully right. The squirrel moves that fast.
   - On **wander** or **jump** it shows how often a second: **1.0/s** in the middle, **0.1/s** fully left, **4.0/s**
     fully right.
   - Press orange again.
4. **White press:** press **white**.
   - Expect: nothing happens; no sample browser opens.

## 7. Overlap (Shift + green)

1. Hold **Shift**.
   - Expect: the green label changes to **overlap 2x** while Shift is held.
2. Still holding Shift, turn **green** right one click at a time, holding a middle key in between.
   - Expect: **2.5x, 3x, 3.5x, 4x**, then round to **0.5x, 1x, 1.5x** and it stops. The sound gets smoother and denser up
     to 4x; at 0.5x the grains have gaps between them. The volume stays roughly even. Turning left goes back. Let go
     of Shift: the label goes back to **size**.

## 8. Saving

1. Set every knob away from its default (including page 2 and the overlap). Switch to **sound key 1** and back to
   **sound key 8**.
   - Expect: all seven knob settings and the overlap are as you left them.
2. Turn the OP-1 off and on again, then select **sound key 8**.
   - Expect: Ratatoskr is still the engine, the recording is still there, and all the settings are kept.

## 9. Load

1. Set size fully right, overlap **4x**, motion **forward**, and hold six keys at once for ten seconds.
   - Expect: no crackles, dropouts or a frozen screen. If it crackles, note which settings and how many keys.

## Report

1. Any step that didn't match, with its number.
2. Photos of the screen: the tree, the squirrel tilted while moving, and the shape curve.
3. Whether the dim part of the trunk are visible enough.
4. How the slow speeds (0.05x to 0.25x) sound: smooth, or rough and phasey?
5. Anything about the controls that felt backwards or hard to find.
