# Trigger guard: guard the encoder pushes too

For: Hundekraft. A patch to `plugins/trigguard` (3.3.1) with a host test, tested on the device as a renamed copy.

## Symptom

With a worn encoder, one push of the knob sometimes registers as two. On an engine whose press switches a page 2
(`OP1_PRESS_PAGE`), the page then seems not to switch, or switches back. In our case orange did it most, and every knob
now and then.

## Evidence

A probe engine took every encoder push through its `engine_input` hook and timed each down and up with `CYCLES`. One
press of orange:

| Event | ms since the event before |
| --- | --- |
| down | (first) |
| up | 0.2 |
| down | 0.3 |
| up | 88.2 |

That's contact chatter as the switch closes, under a millisecond. It varies from press to press. The other three knobs
also doubled now and then.

## Cause

The core flips an engine's page 2 on every encoder down (`dispatcher.c`, `op1_eng_page[...] ^= 1`), so one chatter
flips the page twice. The trigger guard, which handles exactly this for the note keys, lets encoder pushes
(`0x3B`–`0x3E`) through untouched.

## Change

`trigguard-knobs.patch` (against the 3.3.1 tree):

1. The four encoder pushes become guarded keys 25–28, after the 24 note keys.
2. They follow the guard's existing rule with the release held back: a release, then a press of the same knob inside
   the window, drops both. A page flips on the down, so holding the up for the window costs nothing you can feel.
3. Two new settings: **KNOB GUARD** (`knobs`, on by default) and **KNOB MS** (`kms`, 1–60 ms, 15 by default). The
   chatter measured is under 1 ms; a deliberate second push is over 100 ms.
4. **WINDOWS ALL** and **HOLD RELEASE** stay the note keys' only. Everything for the note keys is unchanged.
5. Dropped pushes count in their own `kg_dropped`, next to `tg_dropped`.
6. plugin.json: version 2, the two settings, and the TRIG. GUARD help mentions KNOB GUARD.

## Testing

1. **Host test** (`knob_guard_test.c`, nine cases, all passing against the patched `trigguard.c`):
   1. The recorded orange chatter comes out as one push.
   2. A bounce on release is one push.
   3. Two deliberate pushes 150 ms apart are two.
   4. The held release goes out when the window has passed, not before.
   5. KNOB GUARD off passes the chatter as it came.
   6. A 1 ms window still catches the 0.3 ms chatter.
   7. The knobs keep their own rule with HOLD RELEASE off and WINDOWS ALL on.
   8. A note key's bounce is dropped as before.
   9. Other keys (T1) pass untouched.
2. **Blackfin build:** the patched file compiles with the plugin flags (`bfin-elf-gcc -O2 -mcpu=bf524 ...`).
3. **On the device:** the same code, renamed `pushguard` and installed instead of the trigger guard. With KNOB GUARD on,
   the probe counted one down and one up per press on every knob over dozens of presses. With it off, the doubled
   presses came back (orange: `up 0.0, down 0.1`).
