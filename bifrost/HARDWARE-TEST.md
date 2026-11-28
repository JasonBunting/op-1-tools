# Bifrost: hardware test (0.2.0)

Does Bifrost start on its own, play the AKAI, survive unplugging, give the USB port back, and do the AKAI's knobs turn
the OP-1's encoders? About 25 minutes. 0.1.0 already passed steps 2 and 3; step 7 is new in 0.2.0.

You need: the OP-1 on Hundekraft 3.3.2, the computer with the workshop open, `build/bifrost.knl`, the AKAI keyboard with
its USB cable, and the mini-USB OTG cable.

If anything goes wrong at any point, switch the OP-1 off and on again: that puts the USB port back to normal.

## 1. Install

1. In the workshop, under **Your plugins**, add `build/bifrost.knl` (or replace the older Bifrost with it). Remove **usbprobe** if it's there (the two would
   fight over the USB port). Press **Review changes**, then **Send to OP-1**. The OP-1 restarts.
2. Hold **Shift** and press **Help**, then press **T3**. Hold **Shift** and turn **blue** one click at a time until the
   page titled **BIFROST** shows.
   - Expect: three rows: **USB KEYBOARD** on, **KNOB MAP** on, **LEARN KNOBS** off.
3. Press the **synth key** and pick any sound you like.
4. Unplug the computer's USB cable from the OP-1.

## 2. Plug in, play

1. Plug the OTG cable into the OP-1, then the AKAI into the OTG cable.
   - Expect, within about 5 seconds: the AKAI lights up, then a pop-up says the AKAI's name and "connected".
2. Play a few keys and chords on the AKAI, soft and hard.
   - Expect: the OP-1 plays them, louder for harder hits.
3. Switch to a different sound on the OP-1 and play again.
   - Expect: it still plays.

## 3. Unplug and replug the keyboard

1. Unplug the AKAI from the OTG cable (leave the OTG cable in the OP-1).
   - Expect: a pop-up with the AKAI's name and "unplugged".
2. Plug the AKAI back in.
   - Expect: it lights up and "connected" shows again within a second or two. Play to check.

## 4. Order the other way round

1. Unplug the OTG cable from the OP-1, wait 10 seconds, then plug the AKAI into the OTG cable first and the OTG cable
   into the OP-1 last.
   - Expect: within about 5 seconds, "connected", and it plays.

## 5. Back to the computer

1. Unplug the OTG cable from the OP-1, wait 5 seconds, and plug the computer's USB cable back in.
   - Expect: the workshop sees the OP-1 as usual. (If it doesn't, unplug the cable and plug it in again, and tell me.)
2. Check the battery icon shows charging.

## 6. The off switch

1. With the computer unplugged: open the **BIFROST** settings page (step 1.2) and turn **USB KEYBOARD** off.
2. Plug in the OTG cable and the AKAI.
   - Expect: the AKAI stays dark and nothing pops up.
3. Turn **USB KEYBOARD** back on.
   - Expect: within about 5 seconds the AKAI lights up and "connected" shows.

## 7. The AKAI's knobs

You need a plugin engine with a page 2 on its knobs, such as Ratatoskr (orange's push gives SPEED) or Mouth.

1. Plug in the OTG cable and the AKAI, and wait for "connected".
2. Open the **BIFROST** settings page (step 1.2) and turn **LEARN KNOBS** on.
   - Expect: a pop-up asking for the top row's left knob.
3. Turn each of the AKAI's eight knobs a little, one at a time: the top row left to right, then the bottom row left to
   right.
   - Expect: after each knob, a pop-up naming the next one; after the eighth, "8 knobs learned", and **LEARN KNOBS**
     back off.
4. Press the **synth key**, pick a Ratatoskr sound, and press **T1** so its knobs show.
5. Turn each top-row knob on the AKAI a little.
   - Expect: blue, green, white and orange change on the OP-1's screen, as if you turned the OP-1's encoders.
6. Turn the AKAI's bottom-row right knob.
   - Expect: orange switches to its page 2 (SPEED) by itself, then changes.
7. Turn the top-row right knob again.
   - Expect: orange switches back to page 1, then changes.
8. Change to another sound and back, then turn a knob.
   - Expect: the value moves from where it is; it doesn't jump.
9. Press **T2** (the envelope) and turn the top-row knobs.
   - Expect: they change attack, decay, sustain and release, as the OP-1's encoders would.
10. Switch the OP-1 off and on, plug everything back in, and turn a knob.
    - Expect: the knobs still work (Bifrost remembered them).

## What to send back

Which steps did what was expected, and for any that didn't: what you saw (the pop-up text, whether the AKAI lit up).
