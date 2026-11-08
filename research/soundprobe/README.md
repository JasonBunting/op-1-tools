# soundprobe

A throwaway plugin for [issue #5](https://github.com/JasonBunting/op-1-tools/issues/5). It checks whether a plugin can
copy a whole synth sound and put it back later, including a change of engine. Notes:
[patternplus/docs/sound-recall-design-notes.md](../../patternplus/docs/sound-recall-design-notes.md).

Build with `tools/build.sh soundprobe`, which makes `build/soundprobe.knl`. Install it through the workshop. It needs
Hundekraft 3.2.3: it uses firmware addresses that another core may move.

## Controls (synth pages only)

1. **Shift + scissors:** copy the current sound into memory. A message shows "captured", the engine name and the slot.
2. **Scissors:** put the copied sound into the current slot. A message shows "restored", the engine name and the slot.

The copy is lost when the OP-1 is switched off.

## Device test

Before starting, turn off the randomiser if it's on, so a stray Shift press can't change the sound.

1. **Copy and restore, same engine.**
   1. Press the synth key and pick a sound slot the usual way. Note the engine name on screen and the slot number
      (the messages below say "slot 1"; expect whichever slot you picked).
   2. Play a few keys and remember how it sounds.
   3. Press Shift + scissors. Expect a message: "captured", the engine name, "slot 1".
   4. Turn the blue, green, white and orange knobs a good way each. Play keys; it should sound different.
   5. Press scissors. Expect "restored", the same engine name, "slot 1".
   6. Play keys. Expect the original sound, and the knob positions on screen back where they were.
2. **Engine change.**
   1. With the copy from test 1 still in memory, change the slot's engine to a different one.
   2. Press scissors. Expect "restored" with the original engine's name.
   3. Expect the screen to show the original engine, and keys to play the original sound.
3. **Envelope, effect and LFO.**
   1. Go back to the starting sound. On the envelope, effect and LFO pages, change a setting or two on each, then
      press Shift + scissors.
   2. Change those pages again, and switch the effect and LFO to different types.
   3. Press scissors. Expect every page back as it was when you copied it, effect and LFO types included.
4. **Survives a restart.** After test 3, switch the OP-1 off and on again and pick the same slot. Expect the restored
   sound still there. (This checks that the OP-1 saved the restored sound, not the copy, which is gone.)
5. **Into another slot.** Copy a sound from one slot, pick another slot with a different sound, and press scissors.
   Expect the copied sound in the new slot, and the first slot unchanged.
6. **Optional: a sampler sound.** Copy a synth sampler sound, change the slot to another engine, then press
   scissors. Note whether the sample comes back, comes back silent, or the OP-1 shows an error. Expect this one may
   fail; it tells us how samples are stored.

If the OP-1 freezes or restarts at any step, note which step, then remove the plugin in the workshop.

## Results

Not yet tried on the device.
