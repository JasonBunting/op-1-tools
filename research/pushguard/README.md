# pushguard

A prototype fix for knob pushes that register twice. It's a copy of Hundekraft's trigger guard
(`synth_sdk/plugins/trigguard/trigguard.c`, 3.3.1) that also guards the four encoder pushes, to prove the fix on the
device before sending it to Hundekraft.

**The problem,** measured with [pushprobe](../pushprobe/README.md): a worn knob switch chatters. One press of orange
arrived as down, up 0.2 ms later, down again 0.3 ms after that, then the real up 88 ms later. The core flips an
engine's page 2 on every knob down, so a chatter flips it twice (it seems not to switch) or flips it back on release
(it seems to need holding down). The trigger guard filters bounces on the 24 note keys only; knob pushes pass straight
through.

**The change:** the four knob pushes become four more guarded keys, with the release held back for a short window, so a
quick up-down inside it is dropped. They get their own switch and window on the settings page. The note-key guard is
unchanged.

Build with `tools/build.sh pushguard` (makes `build/pushguard.knl`). Host test, in WSL from the repository root:

```bash
gcc -O2 -Wall -DPG_HOST -I /mnt/d/src/hundekraft-source/ext/kennel/synth_sdk -I research/pushguard/plugin research/pushguard/plugin/test/host_test.c -o /tmp/pg_test && /tmp/pg_test
```

## Settings (on the OP-1's settings page, under PUSH GUARD)

1. **TRIG. GUARD, WINDOW MS, WINDOWS, HOLD RELEASE:** the trigger guard's own four, for the note keys, unchanged.
2. **KNOB GUARD:** on or off, for the four knob pushes. On by default.
3. **KNOB MS:** the knob pushes' window, 1–60 ms, 15 by default. The chatter measured so far is under 1 ms; a deliberate
   second push is over 100 ms.

## Device test

### 1. Is the trigger guard installed?

Only one key guard can run at a time, so pushguard replaces the trigger guard rather than running beside it.

1. Hold **Shift** and press **Help**.
2. Press **T3**. The settings page opens: a title at the top, then four rows.
3. Hold **Shift** and turn **blue** one click at a time. Each click shows another page; read each title.
   - If one page is titled **TRIGGER GUARD**, the trigger guard is installed. Note whether its **TRIG. GUARD** row says
     on or off.
   - If none is, it isn't installed.

### 2. Install pushguard

1. In the workshop, under **Your plugins**:
   1. If the trigger guard (**trigguard**) is listed there, remove it.
   2. Add `build/pushguard.knl`.
2. Press **Review changes**, then **Send to OP-1**. The OP-1 restarts.
3. Open the settings page again (**Shift** + **Help**, then **T3**) and Shift + turn blue to the page titled **PUSH GUARD**.
   - Expect: **KNOB GUARD** on and **KNOB MS** 15, among the trigger guard's usual rows.

### 3. Check with pushprobe

1. On a synth sound, pick **pushprobe** (Shift + T1).
2. Press each knob ten times, at different speeds. Take a Mirror screenshot.
   - Expect: downs and ups equal to your presses on every knob, and no gaps under a few ms in the white rows.
3. On the **PUSH GUARD** settings page, turn **KNOB GUARD** off and repeat.
   - Expect: the doubled presses come back now and then. That shows the guard is what fixed them.
4. Turn **KNOB GUARD** back on.

### 4. Check where it matters

1. Pick **ratatoskr** (or Hundekraft's **granular**) and press **blue** once, about twenty times in a row.
   - Expect: each press switches between page 1 and page 2 and it stays where you left it, every time.
2. If a press still sometimes fails, raise **KNOB MS** (try 30) and try again; note the value that works.

## Report

1. Whether the trigger guard was installed, and on or off.
2. The pushprobe screenshots with KNOB GUARD on and off.
3. Whether blue's page 2 now stays, and the KNOB MS value used.
4. Whether pushes feel any slower. They shouldn't: a page flips on the press, and only the release waits.

## Upstream

`upstream/` holds the same change as a patch to Hundekraft's own trigger guard (`trigguard-knobs.patch`), its host test
(`knob_guard_test.c`, passing against the patched file) and the report to send (`trigguard-report.md`).
