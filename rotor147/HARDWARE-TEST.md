# rotor147: hardware test (0.2.2)

0.1.0 passed (FX 11%). 0.2.0 added a second page on blue, green and white (speed, acceleration, contour), more bass
throb, and a wider stereo image as the master effect. 0.2.1 adds rotor147's own screen, so you can see page 2; 0.2.2 adds the microphones to it and makes contour start fully open. This
pass checks the screen, the new controls, the sound changes, and that the CPU is still about the same.

The screen: on the left, a row per knob in its colour, with its name and value. A knob on page 2 shows its page-2 name
and value and gets a second coloured stripe at the far left. On the right, the horn (a white bar with a flared end) and
the bass drum (a circle with an arrow) turn as the rotors do, with each one's speed in Hz underneath. Green marks around them are the microphones: on a synth or the drums the left
ones, on the master all of them. A mark is long when its microphone hears a lot (the rotor faces it and the mics knob
favours it) and short when it hears little.

Every step says what to do, then what you should see or hear. If what happens is different, write down the step
number and what happened, and keep going unless the OP-1 froze. At the end there's a short list of things to report.

You need: the OP-1 on Hundekraft 3.3.2, the computer with the workshop open, `build/rotor147.knl`, and headphones.
About 20 minutes.

Words used here:

1. **Synth key, mixer key:** the mode keys with the wave and mixer symbols.
2. **T1 to T4:** the four keys under the screen. On a synth sound, **T3** is the effect page.
3. **Blue's thirds:** blue's bar on page 1, in three equal parts: first third **brake**, middle third **slow**, last
   third **fast**.
4. **Push a knob:** press the knob down like a button and let go. On blue, green and white this switches between
   page 1 and page 2 of that knob; push again to come back.
5. **A chord:** hold three white keys near the middle of the keyboard.
6. **Resource monitor:** **Shift + help**, then **T1**. Press **T1** again to close it.

## 1. Install

1. In the workshop, under **Your plugins**, add `build/rotor147.knl`. Press **Review changes**, then **Send to OP-1**.
   - Expect: the OP-1 restarts and the workshop says the plugin is installed.

## 2. The screen and the sound you saved with 0.1.0

1. Press the **synth key** and pick the sound you saved with rotor147 in the last test (fast, mics fully right). Press
   **T3**. Hold **a chord**.
   - Expect: the rotor147 screen: on the left **motion fast**, **mics 100%**, **drive**, **mix**; on the right the horn
     and the drum spinning, with their speeds underneath.
   - Write down which of these you hear: (a) a normal fast swirl, or (b) a very slow, dark, sluggish one, and the two Hz
     numbers.
2. Push **blue**, **green** and **white** once each.
   - Expect: the rows change to **speed**, **spin-up** and **contour**, each with a second stripe at the far left.
   - Write down the three values.
3. Turn them until the screen shows **speed x1.00**, **spin-up 1.00 s** and **contour open**. Push each of the
   three knobs again to return to page 1.
   - Expect: the rows go back to motion, mics and drive, the second stripes go, and you hear a normal fast swirl. The
     horn reads about 6.70 Hz and the bass about 5.50 Hz.
4. Turn green fully left (**mics 0%**). Watch the green marks by the horn while it spins.
   - Expect: two marks on the horn's left (close and distant); only the inner, close one moves, growing as the horn's
     flared end swings past it and shrinking as it turns away. Turning green fully right moves the action to the outer,
     distant mark. Set green back to fully left.
5. Turn blue to the first third (**motion brake**) and hold the chord until the horn stops. Then switch blue to slow
   and back to brake a few times, so it stops in different places.
   - Expect: when the horn stops with its flared end towards the close mark, that mark is long and the sound is
     louder and brighter; stopped facing away, the mark is short and the sound quieter and duller.

## 3. More throb

1. Set blue (page 1) to the middle third (slow), green to fully left (close mics). Hold a low chord, one octave below
   the middle.
   - Expect: the low notes swell and fade about once a second, clearly more than in 0.1.0, without dropping out.
2. Set blue to the last third (fast).
   - Expect: a fast throb in the lows under the horn's warble.

## 4. Page 2: speed

1. With blue on fast, push **blue** (page 2). Turn it slowly to fully right while holding a chord.
   - Expect: speed climbs to **x4.00**, and the horn's number to about 26.8 Hz. The swirl gets faster and faster, past
     a real cabinet, into a buzzy, gargly tremolo at the far right. It follows smoothly, without clicks. (At high
     speeds the drawn rotors may seem to crawl or run backwards, like wagon wheels in a film; the Hz numbers are right.)
2. Turn it slowly to fully left.
   - Expect: **x0.25**, horn about 1.68 Hz. Slower and slower; at the far left, fast mode is only a lazy wobble.
3. Push **blue** to return to page 1, and turn blue into the middle third (**motion slow**).
   - Expect: the horn reads about 0.20 Hz: a very slow drift, one turn about every five seconds, which you can watch
     on the screen.
4. Push **blue**, turn to **x1.00**, push again to return.

## 5. Page 2: acceleration

1. Push **green** (page 2) and turn it fully right (**spin-up 0.10 s**). Push green to return to page 1 (the mics).
2. Switch blue between slow and fast a few times, holding a chord.
   - Expect: the speed changes almost at once, treble and lows both; the Hz numbers jump.
3. Push **green**, turn it fully left (**spin-up 5.00 s**), push again to return. Switch blue from slow to fast.
   - Expect: a long, slow spin-up: the horn's number climbs for about 5 seconds and the bass's for about 15.
4. Push **green**, turn to **1.00 s**, push again to return.

## 6. Page 2: contour

1. Set white (page 1, drive) to about half. Push **white** (page 2) and turn it slowly from fully right to fully left
   while holding a chord.
   - Expect: fully right reads **open** and is the brightest; turning left counts down from 16.0 kHz, gets darker and
     smoother, and the drive gets less fizzy. At fully left (**1.0 kHz**) it's muffled.
2. Turn fully right (**open**), push white to return, and set white (drive) back to fully left (**drive 0%**).

## 7. Wider master image

1. Press the **mixer key**, then **T3**. Choose **rotor147** with **Shift + T3** if it isn't the master effect
   already. Push blue, green and white once each and set **x1.00**, **1.00 s** and **open**, then push each back to
   page 1. Set blue to slow, green to fully right (**mics 100%**).
   - Expect: the same rotor147 screen as on the synth.
2. Play the synth or the tape with headphones on.
   - Expect: the sound sweeps clearly from ear to ear, wider than in 0.1.0.
3. Switch to the OP-1's speaker or take one headphone side off.
   - Expect: still a rotating sound, not thin or hollow.
4. With the tape playing, a synth chord held and the drums playing, open the **resource monitor**.
   - Write down the **FX** number on page 1 and the **PK** numbers. Close the monitor.

## 8. Saving

1. On a synth sound with rotor147, set page 2 of blue to **x2.00**, save the sound by switching the effect off and on
   (**T3** twice), switch to another sound and back.
   - Expect: push blue: still **x2.00**, and faster than normal: the page-2 setting was saved.

## Report

1. Step 2: (a) or (b), the Hz numbers, and the three page-2 values you found.
2. The FX and PK numbers from step 7.
3. Any step that didn't do what it said, with its number, and any clicks, crackles or dropouts.
4. The screen: anything cut off, overlapping, flickering, or not following the knobs?
5. Listening: is the throb enough now? Is the master wide enough? Are the speed and acceleration ranges useful at
   their ends, or too much?
