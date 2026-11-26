# Norn: hardware test (0.4.0)

0.4.0 adds the fourth model, wave terrain, in blue's last quarter. The other three models are unchanged. A short pass:
play it, then read off two CPU numbers.

You need: the OP-1 on Hundekraft 3.3.2, the computer with the workshop open, and `build/norn.knl` (now 68 KB). About
10 minutes.

Words used here:

1. **Synth key:** the key with the blue wave symbol (synth mode).
2. **T1, T3:** the engine and effect page keys.
3. **A middle key:** any white key near the middle of the keyboard, at the normal octave.

## 1. Install

1. In the workshop, under **Your plugins**, add `build/norn.knl`. Press **Review changes**, then **Send to OP-1**.
   - Expect: the OP-1 restarts, and the workshop says the plugin is installed.
2. Press the **synth key**, the sound key you use for Norn, then **T1**. Turn **blue** fully right.
   - Expect: a new sound, not FM: that's wave terrain.

## 2. Play it

Hold a middle key through each step.

1. Turn **green** slowly from fully left to fully right.
   - Expect: the tone changes character several times as it morphs through eight terrains; the last three (green
     past about two thirds) are buzzier and more digital. No silences or crackles.
2. Turn **white** from fully left to fully right.
   - Expect: soft and nearly pure at the left, richer and brighter towards the right.
3. Turn **orange** from fully left to fully right.
   - Expect: the tone shifts as the circle slides across the terrain; softer near both ends.
4. Play freely across the keyboard, chords included.
   - Expect: in tune, no clicks when notes start. The highest notes get softer and simpler; that's how Plaits does it.

## 3. CPU

1. Set the heaviest setting: green a little past the middle (about 60%), white fully right, orange in the middle.
2. Hold **Shift** and press **Help**, then press **T1**: the resource monitor. Turn **blue** until the bottom line
   starts with **2/6 synth**. Hold six keys and write down the number after "synth".
3. Turn **blue** on the monitor until it shows **5/6 pk**. Press any knob to restart it, hold the six keys for a few
   seconds, and write down the number after "pk".
   - Expect: if the OP-1 shows "norn muted: too slow" or "audio overload", write down when.

## What to report

1. The two numbers from section 3.
2. Anything that sounded broken (crackles, silence, clicks) and roughly where the knobs were.
3. Whether you liked it.
