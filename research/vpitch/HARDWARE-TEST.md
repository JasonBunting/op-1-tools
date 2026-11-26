# Per-voice pitch test core: hardware test

For issue 14 (chord glide as an effect on any synth). This test core proves one thing: can the core bend each of the
six synth voices on its own, on every synth, while the note is held?

The core change lives in `D:\src\hundekraft-vpitch` (branch `vpitch` of `hundekraft-source`, uncommitted):
`ext/kennel/synth_sdk/kennel/kn_vpitch.c`. Firmware files are in `D:\src\OP-1-Related\vpitch-test\` (not in this
repository: they contain TE's firmware).

## What the test core does

The OP-1 plays each note on one of six voices (numbered 0 to 5). In this test core:

1. Voices 0, 2 and 4 play at normal pitch.
2. Voices 1, 3 and 5 loop every 4 seconds: 1 s at normal pitch, a 1 s slide up one octave, 1 s held an octave up, a
   1 s slide back down.

So a held note either stays put or keeps sliding up and down an octave, depending on which voice it landed on. You
can't choose the voice, so each step below presses a key several times to hear both.

**This is not the chord glide.** There's no chord-to-chord movement here, and it isn't meant to sound musical. The
odd voices sliding up and down an octave on their own, in a loop, is the whole test: it shows the core can move one
voice's pitch while the note is held. The plug-in that glides chords comes after this works.

## Setup for every step

1. Turn the LFO off (T4 page in synth mode: set it to none / off). An LFO that moves pitch would hide or mimic the
   slide.
2. FX off is cleaner (a delay or reverb tail blurs the slide) but doesn't change the result.
3. Use a sound that holds while you hold the key (high sustain on the T2 envelope page). Iter's default already does.

## Before you start

1. Back up: on the OP-1, hold Shift and press COM, then press T3 for disk mode. Copy the TAPE, ALBUM, DRUM and SYNTH
   folders to the computer, then eject.
2. This build has Hundekraft's small default plugin set (wavetable, taffy, pew, elg, washer, snowglobe, file, scale,
   naming, undo), not your workshop picks. Your own imported plugins (Mouth, Ratatoskr and so on) may survive the
   install. Going back is a normal workshop install (last section).

## 1. Install

1. Switch the OP-1 off. Hold COM and switch it on. The firmware update menu appears.
2. Press T1 to choose "Upload firmware". Connect USB; the OP-1 appears as a disk.
3. Copy `hundekraft-3.3.1-vpitch-test.op1` to that disk, then eject it.
4. When the screen says "Please restart your OP-1", switch it off and on.
5. Expected: the boot screen shows "hundekraft" and its version, then the tape screen. Write down anything else.

## 2. A stock synth: String

1. Press the synth key. Hold Shift and press T1. Turn the blue encoder to **string** and press T1 to load it.
2. Press and hold the lowest F key for 10 seconds, then let go.
3. Expected: the note either holds one pitch the whole time, or slides up an octave, holds, slides down, and repeats
   every 4 seconds. Either is fine. What matters:
   1. a sliding note slides smoothly, with no clicks, stutters or restarts of the attack;
   2. no other notes or noise appear.
4. Repeat step 2 five more times (same key). Expected: some presses hold still and some slide. If all six presses
   behave the same, write down which (all still, or all sliding).
5. Hold three keys together (F, A, C) for 10 seconds. Expected: some of the three slide while the others hold still;
   the still ones stay in tune with each other.

## 3. Every other synth

For each synth in the list (Shift + T1 in synth mode, turn the blue encoder, T1 loads it), do step 2.2 to 2.4 above:
hold a key for about 10 seconds, six times. Write down for each synth: **slides** (some presses slide smoothly),
**never slides**, or **problem** (clicks, wrong pitch, noise, freeze), with a short note.

1. fm
2. digital
3. dr wave
4. cluster
5. pulse
6. sampler (load any sample; hold a key long enough to hear it, or use a looping sample)
7. phase
8. dna
9. dimension
10. voltage
11. one Hundekraft synth from this build: **wavetable**
12. one of your own plugins if it survived the install: **Mouth** or **Ratatoskr**

A synth whose notes fade quickly may end before the slide starts (1 s in). If so, raise its sustain on the envelope
page (T2) and try again, and note that you did.

## 4. Load (optional)

1. Load **dimension** (usually one of the heaviest stock synths). Hold six keys at once for 10 seconds.
2. Expected: the sound stays clean; no crackle that isn't there on the normal firmware.

## 5. If something goes wrong

1. If the OP-1 hangs at boot: switch off, hold COM and switch on; the update menu is always there. Install your
   normal workshop build or the local baseline (next item).
2. Optional control: `hundekraft-3.3.1-local-baseline.op1` is plain Hundekraft 3.3.1 built on this computer with
   the same toolchain, without the pitch change. If the test core has a problem that has nothing to do with sliding
   (crash, odd behaviour elsewhere), install the baseline and check whether the problem stays. If it stays, it's the
   local toolchain, not the pitch change.

## 6. Going back

Open the workshop, connect the OP-1, press Review changes and send your normal build; or install the workshop's
firmware file through the update menu as in section 1.

## Results

(fill in)
