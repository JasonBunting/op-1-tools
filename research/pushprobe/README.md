# pushprobe

A throwaway synth engine to find out why blue's page 2 flips back when the knob is let go (Ratatoskr and Hundekraft's
granular both do it; green and orange don't). It logs every knob push and release the OP-1 reports, with the time
between them, so it can tell a bouncing knob switch from a press copied in software.

Build with `tools/build.sh pushprobe`, which makes `build/pushprobe.knl`. It makes no sound and the pushes do nothing
else.

History: 0.1.0 never saw a release (the core sends a push's release only to whoever claimed the press, and 0.1.0
didn't claim them). 0.1.1 saw green and orange report every press twice, blue and white once, with no timing to tell
why. 0.2.0 adds the timing.

## Device test

1. In the workshop, under **Your plugins**, replace pushprobe with `build/pushprobe.knl`. Press **Review changes**,
   then **Send to OP-1**. The OP-1 restarts.
2. Press the **synth key** (blue wave symbol, top left), then a **sound key** you don't mind changing.
3. Hold **Shift** and press **T1**. Turn **blue** until **pushprobe** is highlighted, then press **T1**.
   - Expect: a grey line at the top, then two rows per knob: its name with `downs 0  ups 0` in its colour, and an
     empty white row under it.
4. Press each knob once, firmly, and let go: **blue**, **green**, **white**, **orange**.
5. Take a photo or a Mirror screenshot of the whole screen.
6. Do it again with any filter or other effect switched off, and with no LFO on the sound, and take another.

## Reading it

Each white row lists that knob's last four events, oldest first: `d` (down) or `u` (up), each followed by the
milliseconds since that knob's event before it (`-` for the first).

1. **`d -  u 120.5`:** one clean press, held about 120 ms.
2. **`d -  u 95.0  d 3.2  u 1.8`:** a second press a few ms after letting go: the switch bounced. That's hardware.
3. **`d -  u 95.0  d 0.0  u 0.0`:** a copy arriving at the same instant: software is delivering the press twice.

Afterwards, pick your usual engine again with Shift + T1, and remove pushprobe from **Your plugins** if you like.

## 0.3.0: watching page 2

0.2.0 showed blue's push is clean (and pushguard fixed the other knobs' chatter), yet blue's page 2 still flips back.
0.3.0 lets the core handle the pushes as usual (every knob's press switches a page 2, as in Ratatoskr) and logs, per
knob: `d` when a push goes down, `+` when its page 2 turns on, `-` when it turns off, each with the ms since the event
before. The top row of each knob shows its page now, the pushes and the flips.

1. Pick **pushprobe** (Shift + T1), press **blue** once, and let go. Take a photo.
2. Do the same with **green**.

Reading it:

1. **`d -  + 16.0`** and page 2 stays: healthy (the flag is seen once a screen frame, so `+` comes a few ms after `d`).
2. **`d  +  -`** with the `-` arriving as you let go and no second `d`: something other than the push turns page 2 off.
3. **downs 1, flips 0:** the flag flipped twice within one frame (on and straight back), or never flipped.

## Result

Blue's page 2 was turned off by PLOCCTRL (bundled with Hundekraft): its `key_sync`, which the core calls on every key
event, clears `op1_eng_page[0]` unless Shift is held, with no check that a PLOC sound is shown. Switching PLOCCTRL off
fixed it on the device. `upstream/` holds the report (`plocctrl-report.md`) and a one-line patch.
