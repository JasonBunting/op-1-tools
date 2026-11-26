# Bifrost: hardware test (0.1.0)

Does Bifrost start on its own, play the AKAI, survive unplugging, and give the USB port back? About 15 minutes.

You need: the OP-1 on Hundekraft 3.3.2, the computer with the workshop open, `build/bifrost.knl`, the AKAI keyboard with
its USB cable, and the mini-USB OTG cable.

If anything goes wrong at any point, switch the OP-1 off and on again: that puts the USB port back to normal.

## 1. Install

1. In the workshop, under **Your plugins**, add `build/bifrost.knl`. Remove **usbprobe** if it's there (the two would
   fight over the USB port). Press **Review changes**, then **Send to OP-1**. The OP-1 restarts.
2. Hold **Shift** and press **Help**, then press **T3**. Hold **Shift** and turn **blue** one click at a time until the
   page titled **BIFROST** shows.
   - Expect: one row, **USB KEYBOARD**, on.
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

## What to send back

Which steps did what was expected, and for any that didn't: what you saw (the pop-up text, whether the AKAI lit up).
