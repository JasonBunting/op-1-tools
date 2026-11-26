# plocctrl turns off blue's page 2 in every other engine

For: Nicolas Franck Pauly (PLOC, https://github.com/nicolasfpauly/ploc) and Hundekraft (who bundle PLOCCTRL in the core's
`content/`).

**Fixed upstream in Hundekraft 3.3.2** (2026-11-21 kennel change in `plocctrl.c`: `key_sync` gated on `p6_linked`, plus
`test_plocctrl_latch.c`). Not sent; PLOC's own repo got that fix instead as https://github.com/nicolasfpauly/ploc/pull/1.

## Symptom

On Hundekraft 3.3.1 with PLOCCTRL 0.6.1 installed (it is bundled), any synth engine whose blue encoder push switches a
page 2 (`OP1_PRESS_PAGE`) can't keep blue on page 2. Press blue and page 2 shows; let go and it switches back. Green,
white and red are not affected. Seen in Hundekraft's `granular` and in two third-party engines. Holding Shift while
letting go keeps it.

## Evidence

A probe engine (an `OP1_PRESS_PAGE` press on every knob, logging each push down from its `engine_input` hook and each
change of `op1_eng_page[k]` once a frame, with times):

| Knob | Events after one press and release |
| --- | --- |
| Blue | push down; page 2 on 17.7 ms later; page 2 off 207 ms later, with no second push |
| Green | push down; page 2 on 11.7 ms later; stays on |
| White, red | push down, page 2 on; off only at the next push |

With `plocctrl` switched off (the inventory's switched-off list) and the OP-1 restarted, blue's page 2 stays on, in the
probe and in the engines.

## Cause

`plugins/plocctrl/plocctrl.c` registers `.key_sync = key_sync`. The core calls every feature's `key_sync` on every key
event (`kn_features_key_sync`, `op1_feature.h`: "every key event (KeyHandler thread)"), whatever sound is shown:

```c
static void key_sync(void) {if(!p5_shift_held())op1_eng_page[0]=0;}
```

It has no `p6_linked(...)` guard, unlike `input()` and the `OP1_EV_SERVICE` branch, so the release of the blue push
(itself a key event) clears blue's page-2 flag for whatever engine is shown.

A smaller thing in the same file: `event()` calls `clear_edit()` (clearing `op1_eng_page[0]` and `[1]`) on every
`OP1_EV_MODE`, so leaving synth mode and coming back also drops blue's and green's page 2 for other engines. The core
itself only resets the flags on a slot or engine change. Not part of the patch below.

## Fix

Guard `key_sync` like the rest of the file (`plocctrl-key-sync.patch` beside this file, against the Hundekraft 3.3.1 tree; in the PLOC
repository the file is `source/plugins/plocctrl/plocctrl.c`):

```diff
-static void key_sync(void) {if(!p5_shift_held())op1_eng_page[0]=0;}
+/* key_sync runs on every key event, whatever sound is shown: only touch the shared page flags for a linked PLOC. */
+static void key_sync(void) {if(p6_linked(op1_synth_knobs(p4_slot()))&&!p5_shift_held())op1_eng_page[0]=0;}
```

The patched file compiles with the kennel flags (`bfin-elf-gcc -O2 -mcpu=bf524 ...`). The fix is not yet tried on a
device; switching PLOCCTRL off, which removes the same write, was.
