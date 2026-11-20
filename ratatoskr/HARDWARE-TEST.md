# Ratatoskr: hardware test

Every step says what to do, then what you should see or hear. If what happens is different, write down the step
number and what happened, and keep going unless the OP-1 froze. At the end there's a short list of things to report.

You need: the OP-1 on Hundekraft 3.3.1, the computer with the workshop open, and `build/ratatoskr.knl`. About 20
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

## 2. Pick Ratatoskr

1. Press the **synth key**, then **sound key 8** (or any sound you don't mind changing).
2. Hold **Shift** and press **T1**. Turn **blue** until **ratatoskr** is highlighted, then press **T1**.
   - Expect: a new screen. If the sound has a sample already (from an earlier sampler recording), it shows near the
     top as white and grey columns; otherwise the screen says **record a sample first**. At the bottom, four labels in
     blue, green, white and orange: **start** with a time in seconds, **size 100ms**, **speed +0.25x**,
     **density 2.0x**.
   - If anything overlaps, is cut off or is missing, take a photo.

## 3. Record a sample into it

Record with Ratatoskr selected. Recording in the sampler and then switching to Ratatoskr can bring back an older
sample saved on the OP-1 instead of the new one.

1. Press the **input key** (top right, with the microphone symbol).
2. Hold any keyboard key and sing, speak or play something with a melody into the mic for about five seconds, then
   let go of the key.
   - Expect: the screen stays on Ratatoskr. The columns near the top change to the shape of your recording; the
     middle half is white. Under them, a green bracket under the white part, a blue arrow on it, and a thin white line
     through the columns.

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
2. **Start (blue):** set white to about +0.25x again, then turn blue from left to right.
   - Expect: the white part of the columns and the green bracket slide along the recording, keeping their length
     until the bracket reaches the recording's end, where it gets shorter; the sound changes to that part.
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
   - Expect: the bracket's left end stays where it is the whole time; only its right end moves. Fully left (0%), the
     bracket shrinks to a point at its left end and the playhead stops moving even with speed up. Fully right (100%),
     the bracket runs from its left end to the recording's end, and the playhead travels all of it, wrapping from the
     end back to the left end.
3. **Drift:** press **white** (label **drift**). Turn it to 100% and hold a key for twenty seconds.
   - Expect: the white line wobbles around the blue arrow, up to a quarter of the bracket either way; the arrow itself
     keeps moving steadily. At 0% the white line sits on the arrow again. Press white again.
4. **Sample browser:** press **orange**.
   - Expect: the core's sample browser opens, listing the sampler presets. Pick one and press **white** to load it.
     The columns change to the new sample and Ratatoskr plays it. (Close the browser the way the core says.)

## 7. Shift settings (new in 0.2.0)

Make sure no knob is on page 2 (no second line above any label), and set white's speed to about **+0.50x**. Hold a
middle key through each step.

1. Look at the top line of the screen.
   - Expect: grey text reading **jumps 0%  turns 0%  wrap  chaos 0%**.
2. **Jumps:** hold **Shift** and turn **blue** right, one click at a time, to **jumps 100%**. Let go of Shift.
   - Expect: the top line counts up in steps of 10%. At 100% the blue arrow leaps to a new place in the bracket now
     and then (roughly every two seconds) and keeps moving forwards from there. Turning blue without Shift still moves
     the start.
3. **Turns:** set jumps back to **0%** (Shift + blue left), then hold **Shift** and turn **green** to **turns 100%**.
   - Expect: the arrow now and then reverses, roughly once a second, and the sound plays backwards for a while.
4. **Edges:** set turns back to **0%**. Hold **Shift** and turn **white** one click right.
   - Expect: the top line changes from **wrap** to **bounce**. When the arrow reaches the bracket's right end it
     turns round and runs back to the left end, then forwards again: ping-pong. One click left goes back to **wrap**,
     where it jumps from the right end back to the left end instead.
5. **Chaos:** set jumps to **100%**, then hold **Shift** and turn **orange** to **chaos 100%**. Listen for thirty
   seconds.
   - Expect: the jumps no longer come evenly. There are quiet spells of several seconds with no jumps, then flurries
     of several close together.
6. **Shift doesn't change the pages:** with Shift settings changed, check the four page-1 labels and values at the
   bottom.
   - Expect: they are unchanged by the Shift turns.

## 8. Saving

1. With Ratatoskr set up how you like it, including some Shift settings, switch to **sound key 1** and back to
   **sound key 8**.
   - Expect: all seven knob settings (four page 1, three page 2) and the four Shift settings on the top line are as you
     left them.
2. Turn the OP-1 off and on again, then select **sound key 8**.
   - Expect: Ratatoskr is still the engine, the recording is still there, and all the settings are kept, including
     the top line.

## 9. Load (optional, skip if short on time; passed on 0.1.x)

1. Set density fully right, size fully right, and hold six keys at once for ten seconds.
   - Expect: no crackles, dropouts or a frozen screen. If it crackles, note which settings and how many keys.

## 10. Sharper screen lines (0.2.2)

1. With a sample recorded (section 3), hold a key and look closely at the screen.
   - Expect: the waveform columns, the green region bracket, the blue playhead arrow and the white read line look crisp:
     no soft, doubled or half-lit rows. The thin white read line may look slightly bolder than before.
2. Turn green (width) slowly from one end to the other.
   - Expect: the bracket's ends move a pixel at a time and stay sharp; nothing flickers.

## Report

1. Any step that didn't match, with its number.
2. Photos of the screen if it looked wrong.
3. How the slow speeds (0.1x to 0.5x) sound: smooth, or rough and phasey? This decides whether smoother stretching is
   worth adding.
4. Anything about the controls that felt backwards or hard to find.
