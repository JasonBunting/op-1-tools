#!/usr/bin/env python3
"""Mouth: factory sounds as OP-1 patch files.
    python mouth/plugin/presets.py [OUT_DIR]     -> OUT_DIR/*.aif (default: mouth/presets/mouth/)

Install: copy the mouth/ folder (named after the engine, the .aif files directly inside, as Hundekraft's preset
collections do) into the OP-1 disk's synth/ folder in disk mode (see the README).

An OP-1 synth patch is an AIFC file (16-bit mono, 44.1 kHz, 'sowt') with an APPL chunk holding "op-1" + JSON and an
empty SSND chunk. A saved sound finds its engine by the saved type name, "mouth"; knobs[4] holds 0x5300 (a file
plugin without a fixed number). Every value stays inside the ranges the factory presets use: outside them the
firmware imports the file as a plain sample. The format and ranges follow Hundekraft's VOX presets.py (kennel SDK,
GPL-3.0-or-later with the OP-1 firmware permission).

Knobs (mouth.c): blue vowel, green body, white breath, orange vibrato/warble; page-2 words: 5 TALK speed (9 steps:
off, 1/2, 1/4, 1/4t, 1/8, 1/8t, 1/16, 1/16t, 1/32), 6 WORD (17 steps: the key, 1 .. 16), 7 WILD (11 steps: 0 .. 100%).

LFO: the stock Value LFO, [speed, amount, destination, parameter]. Destination 2000 is the synth engine and
parameter 2000 its blue knob (Hundekraft's tools/presets/polish.py uses the same words), so it sweeps the vowel:
the "talking" sound. Speed and amount are by ear; check them on the OP-1 (HARDWARE-TEST.md).
Copyright (C) 2026 Jason Bunting. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE).
"""
import json
import math
import struct
import sys
from pathlib import Path

RANGE_ADSR = [(64, 16320), (64, 16320), (0, 32767), (64, 16320)]
RANGE_FX = {
    "delay": [(3392, 11200), (4969, 32767), (0, 16384), (1332, 32767)],
    "spring": [(1344, 16448), (9920, 16448), (0, 16384), (2047, 26120)],
}
RANGE_LFO = {"value": [(0, 12776), (-32767, 32767), (1024, 11264), (1024, 15360)]}
SELECTOR = 0x5300
SPEEDS = ["off", "1/2", "1/4", "1/4t", "1/8", "1/8t", "1/16", "1/16t", "1/32"]
SYNTH, BLUE = 2000, 2000                                 # Value LFO destination: the synth engine, its blue knob


def step_word(i, n):
    """the middle of step i of a page-2 word read as n steps (mouth.c fm_step: (word * n) >> 15)"""
    return ((2 * i + 1) * 32768) // (2 * n)


VOWEL = {"ee": 0, "ih": 4096, "ay": 8192, "eh": 12288, "a": 16384, "ah": 20480, "aw": 24576, "oh": 28672, "oo": 32767}
BODY = {"giant": 0, "man": 5461, "woman": 16384, "child": 27307, "tiny": 32767}

# name, (vowel, body, breath, vibrato), (talk speed, word 0 = key, wild %), ADSR, FX, LFO (speed, amount) on blue, octave
PRESETS = [
    ("babble",       (VOWEL["ah"], 10000, 3000, 12000), ("1/8", 0, 50),   (64, 4000, 30000, 3000),
     ("spring", [5000, 12000, 1500, 5000]), (2500, 9000), 0),
    #   the default sound with a slow vowel sweep under the words
    ("choir ah",     (VOWEL["ah"], BODY["woman"], 6000, 10000), ("off", 0, 0), (3000, 6000, 30000, 6000),
     ("spring", [8000, 14000, 2000, 9000]), (1200, 4000), 0),
    #   no talk: play chords; every voice drifts on its own, so it sounds like several singers
    ("giant monk",   (VOWEL["oh"], BODY["giant"], 2000, 6000), ("off", 0, 0), (2000, 8000, 30000, 9000),
     ("spring", [10000, 15000, 2000, 12000]), (1000, 5000), -1),
    #   a huge, dark "oh", an octave down
    ("chatterbox",   (VOWEL["a"], 30000, 2500, 8000), ("1/16", 0, 80), (64, 3000, 30000, 2000),
     None, (5000, 12000), 0),
    #   a tiny voice talking fast and wild
    ("whisper talk", (VOWEL["ah"], 12000, 30000, 0), ("1/8t", 3, 60), (300, 5000, 28000, 4000),
     ("spring", [5000, 12000, 1500, 5000]), None, 0),
    #   breath only, saying word 3 on every key
    ("robot words",  (VOWEL["eh"], BODY["man"], 0, 0), ("1/16", 0, 100), (64, 2000, 32000, 1500),
     ("delay", [6000, 9000, 2000, 4000]), None, 0),
    #   no vibrato, no breath, choppy: a machine trying to talk
    ("talking wah",  (VOWEL["ah"], BODY["woman"], 3000, 8000), ("off", 0, 0), (64, 4000, 30000, 3000),
     ("delay", [7000, 10000, 2500, 5000]), (7000, 24000), 0),
    #   no talk: the LFO alone sweeps the vowel wide, a "wah-wah" that sounds like talking
    ("warble diva",  (VOWEL["oh"], 22000, 4000, 30000), ("1/4", 0, 30), (1500, 6000, 30000, 6000),
     ("delay", [9000, 14000, 4000, 9000]), (1500, 6000), 0),
    #   a fast warble on long, slow syllables
]


def ieee_extended(x):
    exp = int(math.floor(math.log2(x)))
    return struct.pack(">HQ", exp + 16383, int(x / 2 ** exp * 2 ** 63))


def chunk(cid, body):
    return cid + struct.pack(">I", len(body)) + body + (b"\0" if len(body) & 1 else b"")


def aifc(meta):
    comm = struct.pack(">hIh", 1, 0, 16) + ieee_extended(44100) + b"sowt" + b")Signed integer (little-endian) linear PCM"
    js = json.dumps(meta, separators=(",", ":"), sort_keys=True).encode() + b"\n"
    if len(js) & 1:
        js += b" "
    body = (chunk(b"FVER", bytes.fromhex("a2805140")) + chunk(b"COMM", comm) + chunk(b"APPL", b"op-1" + js)
            + chunk(b"SSND", struct.pack(">II", 0, 0)))
    return b"FORM" + struct.pack(">I", len(body) + 4) + b"AIFC" + body


def check(name, what, values, ranges):
    for i, (v, (lo, hi)) in enumerate(zip(values, ranges)):
        if not lo <= v <= hi:
            raise ValueError(f"{name}: {what}[{i}] = {v} is outside {lo}..{hi}")


def meta(p):
    name, knobs, (speed, word, wild), adsr, fx, lfo, octave = p
    w5, w6, w7 = step_word(SPEEDS.index(speed), 9), step_word(word, 17), step_word(wild // 10, 11)
    check(name, "knobs", list(knobs) + [w5, w6, w7], [(0, 32767)] * 7)
    check(name, "adsr", adsr, RANGE_ADSR)
    if fx:
        check(name, "fx " + fx[0], fx[1], RANGE_FX[fx[0]])
    lfo_params = [lfo[0], lfo[1], SYNTH, BLUE] if lfo else [1200, 0, SYNTH, BLUE]
    check(name, "lfo", lfo_params, RANGE_LFO["value"])
    return {
        "adsr": list(adsr) + [2048, 5568, 4000, 4000],
        "fx_active": fx is not None,
        "fx_params": (fx or ("delay", [8000] * 4))[1] + [0, 0, 0, 0],
        "fx_type": (fx or ("delay", None))[0],
        "knobs": list(knobs) + [SELECTOR, w5, w6, w7],
        "lfo_active": lfo is not None,
        "lfo_params": lfo_params + [0, 0, 0, 0],
        "lfo_type": "value",
        "name": name,
        "octave": octave,
        "synth_version": 2,
        "type": "mouth",
    }


def main():
    if len(sys.argv) > 2:
        sys.exit(__doc__)
    out = Path(sys.argv[1]) if len(sys.argv) == 2 else Path(__file__).resolve().parents[1] / "presets" / "mouth"
    out.mkdir(parents=True, exist_ok=True)
    for p in PRESETS:
        (out / (p[0] + ".aif")).write_bytes(aifc(meta(p)))
    print(f"{len(PRESETS)} presets in {out}")


if __name__ == "__main__":
    main()
