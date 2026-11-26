# usbprobe: hardware test (0.1.5)

Can the OP-1 power your AKAI keyboard and play from it, with no computer? About 10 minutes.

You need: the OP-1 on Hundekraft 3.3.2, the computer with the workshop open, `build/usbprobe.knl`, the AKAI keyboard
with its USB cable, and a **mini-USB OTG adapter** (or an OTG cable: mini-USB on one end, a full-size USB socket on the
other). It has to be a real OTG adapter; a plain mini-USB-to-USB-socket plug looks the same and won't work.

If anything goes wrong at any point, switch the OP-1 off and on again: that puts the USB port back to normal.

## 1. Install

1. In the workshop, under **Your plugins**, add (or replace usbprobe with) `build/usbprobe.knl`. Press **Review changes**, then **Send to OP-1**.
   The OP-1 restarts.
2. Unplug the USB cable from the OP-1.

## 2. Open the probe

1. Press the **synth key** (blue wave symbol, top left), then a **sound key** you don't mind changing.
2. Hold **Shift** and press **T1**. Turn **blue** until **usbprobe** is highlighted, then press **T1**.
   - Expect: a grey line at the top, then a green line "push blue to start host mode", then rows of numbers.

## 3. Start host mode, no keyboard yet

1. Plug the OTG adapter into the OP-1, with nothing plugged into the adapter.
2. Push the **blue** knob.
   - Expect: the green line says **host mode: plug the keyboard in**, and the next row starts `devctl 05` and ends `5v on`. A `vbuserr` count going up is
     expected (the OP-1 never reports the port's power to its own USB controller).
   - If a red line appears instead, write down the `devctl` row and the `step ... vbuserr ... at ... vplen` row (or take
     a photo), then switch the OP-1 off and on.

## 4. Plug in the keyboard

1. Plug the AKAI's USB cable into the adapter.
   - Expect: the AKAI lights up (the OP-1 is powering it).
   - Expect, within a second or two: the green line says **running: play the keyboard**, a row shows `vid` and `pid`
     with numbers, and a row starts `midi in ep`.
2. Play a few keys on the AKAI.
   - Expect: `packets` counts up, and the bottom row shows short hex codes (like `903c64`) for the last notes.
3. Take a photo of the screen (`devctl` should now read `45`).

## 5. Hear it

1. Hold **Shift** and press **T1**, pick another engine (any sound you like), press **T1**.
2. Play the AKAI.
   - Expect: the OP-1 plays the notes, louder for harder hits.
   - If nothing sounds: on the AKAI, set its MIDI channel to 1 and try again.

## 6. Stop

1. Go back to the usbprobe sound (Shift + T1, pick **usbprobe**), and push **blue**.
   - Expect: **stopped: back to normal usb**, and the AKAI goes dark.
2. Unplug the adapter, plug the computer back in, and check the workshop still sees the OP-1, and that the OP-1
   charges (the battery icon on Shift + COM, T4, shows it charging).

## What to send back

The photos from steps 3 and 4, and whether step 5 made sound. If a red line appeared, a photo of it and which step.
