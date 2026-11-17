# Mouth

A synth engine for the original OP-1 running Hundekraft 3.3: a voice that sings vowels and babbles made-up words.
Each note is a vocal-fold pulse shaped by five formants (the resonances that make "ee" sound like "ee"). The formant
frequencies come from [CHIMERA](https://github.com/jamesondh/chimera-vocal-synth)'s vocal-tract model, for a man, a
woman and a child. Six voices; every voice wanders slightly in pitch on its own, so chords sound like a small choir.
There are no real consonants: talk mode makes words that sound like words but aren't.

The [manual](manual/manual.html) covers playing it; this README is the short version, plus building and testing.

## Install

1. **The engine:** in the workshop, add `build/mouth.knl` under **Your plugins** and send it to the OP-1.
2. **The sounds (optional, recommended):** the eight presets in `presets/mouth/`, several with an LFO already set up.
   1. On the OP-1, hold **Shift** and press **COM**, then press **T3**. The OP-1 shows up on the computer as a disk.
   2. On the computer, copy the `presets/mouth` folder into the OP-1 disk's `synth` folder, so the disk has
      `synth/mouth/babble.aif` and the rest (the same layout as Hundekraft's preset collections).
   3. Eject the disk. The OP-1 restarts by itself, loads the new sounds, and waits for a key press.

   If the OP-1 says **Failed to create synth patch** for each sound while it syncs, it is full: it holds a limited
   number of user synth sounds, and it deletes the ones it can't take. On Hundekraft this limit was reached with about
   220 sounds on the disk, well under the 300 Hundekraft's preset collection mentions. Delete preset folders you don't
   use from the disk's `synth` folder, copy the `mouth` folder again, and eject. An existing sound with the same file
   name may keep its old settings.

## Controls

Blue, green and white each have a second page: press the knob until it clicks to switch, press again to switch back.
Page 2 knobs move one step per click. The bottom row of the screen always shows which page each knob is on.

| Knob | Page 1 | Page 2 (press) |
| --- | --- | --- |
| Blue | **vowel**: ee, ih, ay, eh, a, ah, aw, oh, oo, gliding between them | **talk**: off, or syllables at 1/2, 1/4, 1/4t, 1/8, 1/8t, 1/16, 1/16t, 1/32 of the OP-1 tempo |
| Green | **body**: giant, man, woman, child, tiny (moves the formants, not the pitch) | **word**: "key" (each key says its own word) or word 1–16 (every key says the same one) |
| White | **breath**: more air; fully right whispers | **wild**: 0–100%. Low mumbles around the blue vowel with soft edges; high jumps between vowels with sharp "b", "m", "w", "y"-like edges and an uneven rhythm |
| Orange | **vibrato**: the left half sets its depth (up to a semitone, fading in after each note starts); the right half speeds it into a warble | none |

Talk follows the OP-1's tempo (the BPM setting or MIDI clock) and starts with each note, so notes from the sequencer
land their syllables on the beat. When you let go of a key, talk stops making new syllables and the note fades on its
last vowel. Every vowel and body plays at about the same loudness.

## Screen

1. **Top:** a mouth: lips, upper teeth and a tongue. Wide and smiling for "ee", open with the tongue low for "ah",
   puckered for "oh" and "oo"; bigger for a bigger body. It follows the first sounding voice: talk syllables press the
   lips shut ("b", "m"), pucker them ("w") or smile ("y"); breath parts the lips; the vibrato makes it wobble. With no
   key held, it follows the knobs.
2. **Middle:** a strip from "ee" to "oo". The blue mark is where blue points (an LFO on blue moves it); a white dot
   shows each sounding voice's vowel.
3. **Bottom:** each knob's name and value in its colour. A knob on page 2 shows its page-2 name and a second line.

## Making it talk with the OP-1's LFOs

The stock LFOs can turn Mouth's knobs for you, which makes it sound like it is talking even with talk off. On a synth
sound, press **T4** to switch the LFO on or off, and **Shift + T4** to choose its type. Each LFO's own screen uses the
four knobs as follows (from TE's OP-1 guide):

1. **Value** (the one the presets use): blue speed, green amount, white destination, orange parameter. Pick the synth
   as the destination and blue as the parameter: the vowel sweeps back and forth ("wah-wah", or slow sighing at low
   speeds). Point it at green instead and the voice grows and shrinks between bodies.
2. **Element**: blue picks what drives it: the g-force sensor (tilt the OP-1), an external input (the radio, line in
   or the mic), the synth's envelope, or the synth's level. Green amount, white destination, orange parameter. With
   the envelope as the source and blue as the parameter, every note opens its vowel as it starts, like "wow". With
   the mic, your own voice moves Mouth's vowel.
3. **Random**: blue amount, green speed, white destination, orange attack/decay. It moves every knob of the
   destination at once (vowel, body, breath and vibrato): use a small amount for a restless voice.
4. **Tremolo**: blue speed, green pitch, white volume, orange envelope. Pitch and volume only; Mouth's own vibrato
   usually does this better.

Tip: the blue mark on Mouth's screen shows where the LFO has pushed the vowel, so you can see it working. Combining
both is the most word-like: talk on (page 2) for syllables, and a slow Value LFO on blue so the words drift.

## Presets

| Sound | What it does |
| --- | --- |
| babble | The default voice talking at 1/8, with a slow vowel sweep from the LFO |
| choir ah | No talk; play chords for a small choir |
| giant monk | A huge, dark "oh", an octave down |
| chatterbox | A tiny voice talking fast and wild |
| whisper talk | Breath only, saying word 3 on every key |
| robot words | No vibrato or breath, choppy: a machine trying to talk |
| talking wah | No talk; a wide, fast LFO on the vowel does the talking |
| warble diva | A fast warble on long, slow syllables |

The LFO settings in the presets are written by number (Hundekraft's own presets use the same ones for "synth, blue
knob"); their speed and depth are set by ear on a PC, so `HARDWARE-TEST.md` checks them on the OP-1.

## Testing on the OP-1

See [HARDWARE-TEST.md](HARDWARE-TEST.md): every step says exactly what to press and what you should see or hear.

## For developers

| File | What |
| --- | --- |
| `plugin/mouth.c` | The engine (knobs, voices, level, page 2) and its screen. |
| `plugin/mouth.svg` | The screen's SVG: empty, since `mouth.c` draws everything. |
| `plugin/fm_dsp.h` | The pulse, the five resonators, vibrato, drift and talk. Integer only. |
| `plugin/fm_tables.h` | Formants for nine vowels and three bodies, generated by `plugin/gen_tables.py`. |
| `plugin/fm_levels.h` | The loudness evening per vowel and body, generated by `plugin/test/level_fit.c`. |
| `plugin/presets.py` | Writes the presets in `presets/mouth/`. |
| `plugin/test/host_test.c` | Host test: coefficients, stability, loudness, talk (tempo, words, release), voice stealing; writes WAVs. |
| `manual/` | The manual (`manual.html`, its style and key images; screenshots go in `img/`). |
| `HARDWARE-TEST.md` | The device test. |

Build with `tools/build.sh mouth` (makes `build/mouth.knl`). Host test and level fit, in WSL from the repository root
(WAVs go to `build/`):

```bash
gcc -O2 -Wall -I mouth/plugin mouth/plugin/test/host_test.c -lm -o /tmp/fm_test && /tmp/fm_test build
```

```bash
gcc -O2 -Wall -I mouth/plugin mouth/plugin/test/level_fit.c -lm -o /tmp/fm_fit && /tmp/fm_fit mouth/plugin/fm_levels.h
```

Run the level fit twice after any change to the sound (the second pass refines), then the host test. Regenerate the
presets with `python mouth/plugin/presets.py`, and the formant table from CHIMERA's
`crates/chimera/src/phonetics/vowelTargets.generated.json` with `python mouth/plugin/gen_tables.py <that file>`.
