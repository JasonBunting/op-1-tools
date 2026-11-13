# tractprobe

A throwaway synth engine that measures how much of a physically modelled voice the OP-1 can run, as groundwork for
a vocal synth inspired by [CHIMERA](https://github.com/jamesondh/chimera-vocal-synth). It is a tube of sections (a
Kelly-Lochbaum vocal tract) driven by a simple vocal-fold pulse. More sections cost more CPU and also make the
tract longer: 44 sections is about an adult male's 17 cm.

Build with `tools/build.sh tractprobe`, which makes `build/tractprobe.knl`. It needs Hundekraft 3.2.3.

| File | What |
| --- | --- |
| `plugin/tractprobe.c` | The engine: knobs, voices, gain. |
| `plugin/tp_dsp.h` | The tube and the vocal-fold pulse, integer only. |
| `plugin/test/host_test.c` | Host test: every knob setting stays stable; writes WAVs to listen to. |

Host test, in WSL from the repository root (the WAVs go to `build/`):

```bash
gcc -O2 -Wall -I research/tractprobe/plugin research/tractprobe/plugin/test/host_test.c -lm -o /tmp/tp_test && /tmp/tp_test build
```

## Controls

1. **Blue:** tube sections, in five steps: 12 (fully left), 24 (a quarter), 44 (halfway), 64 (three-quarters),
   96 (fully right). Turning right costs more CPU and makes the voice deeper (a longer throat).
2. **Green:** tongue position, back (left) to front (right).
3. **White:** tongue height, open (left) to closed (right). Green right with white right is roughly "ee"; both left
   is roughly "ah"; green left with white right is roughly "oo".
4. **Orange:** breath noise.

## Device test

Before starting: in the workshop, make sure the **sysmon** (resource monitor) plugin is installed alongside
tractprobe. Install tractprobe through the workshop as usual.

1. **It plays.**
   1. Press the synth key, pick an empty-ish slot, then hold Shift and press T1 and choose **tractprobe**.
   2. Play a key in the middle of the keyboard. Expect a buzzy, vowel-like "ah".
   3. Turn green and white slowly while holding a key. Expect the vowel to change (towards "ee" and "oo").
   4. Turn orange right. Expect more breath hiss.
2. **Open the monitor.** Hold Shift and press Help to open the clock screen, then press T1. Turn blue until the line
   at the bottom starts with **2/6 synth**. The number after "synth" is the percent of the CPU the synth voices use.
   You can keep playing keys while this screen is open.
3. **Measure.** Go back to the synth screen (synth key) and set blue fully left. Return to the monitor (Shift +
   Help, T1) and note the "synth" number in two cases:
   1. one key held down;
   2. six keys held down (a chord with both hands).

   Repeat with blue at a quarter, halfway, three-quarters and fully right: five blue positions, two numbers each.
   Each position is a different tube size, so the sound should get deeper as blue goes right.
4. **Peak.** On the monitor, turn blue to **5/6 pk**. Press any knob to restart the peak, hold the six-key chord
   with blue (on the synth screen) fully right for a few seconds, and note the "pk" number.
5. **Overload.** If at any point the OP-1 shows "tractprobe muted: too slow" or "audio overload", note the blue
   position and how many keys were held. That's the ceiling, and it's a useful result, not a failure.

If the OP-1 freezes or restarts, note the step, then remove tractprobe in the workshop.

## Results

Not yet tried on the device.
