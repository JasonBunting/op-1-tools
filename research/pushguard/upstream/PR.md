# Pull request for Hundekraft/op1-workshop

Branch `contrib/trigguard-knob-guard` in the worktree `D:\src\OP-1-Related\op1-workshop-trigguard-pr` (from your fork's
`main`, which matches upstream's 781fac3). Files: `source/updates/trigguard-knob-guard/` (README.md, UPDATE.json,
the patch).

## Commit message

```
Provide trigguard knob guard source update for Hundekraft 3.3.2
```

## Title

Trigger guard: guard the encoder pushes too (source update for 3.3.2)

## Description

A worn encoder can register one push as two: contact chatter under a millisecond (measured on orange: down, up 0.2 ms,
down 0.3 ms, up 88 ms). The core flips an engine's page 2 on every encoder down, so a press-page engine's page seems not
to switch, or switches back. The trigger guard already handles this for the note keys, but lets the encoder pushes
(`0x3B`–`0x3E`) through.

This PR adds a readable source patch, exact-base hashes and apply instructions under
`source/updates/trigguard-knob-guard/`. It applies to `hundekraft-3.3.2-source.tar.gz` (SHA-256
`eabeb668bc577e10633400ada35ff5ac8be6ef1318796c0318f03a878990a97f`). The four pushes become guarded keys with their own
switch and window: **KNOB GUARD** (on by default) and **KNOB MS** (1–60 ms, 15 by default), with the release held back
for the window so a page still flips on the down. The note keys' behaviour and settings are unchanged; trigguard's
plugin.json goes to version 2.

Validation: clean patch application and file hashes on the published 3.3.2 archive; a new host test
(`plugins/trigguard/test/knob_guard_test.c`, nine cases including the recorded chatter, all passing); Blackfin object
compilation with the plugin flags. On the device, the same code as a renamed loadable copy on 3.3.1 (trigguard.c is
identical in 3.3.2) took every knob to one down and one up per press over dozens of presses; with KNOB GUARD off the
doubles came back. A full Hundekraft build of the patched source is not verified.

**This PR does not replace any `.knl`, source archive or release manifest.** Please apply the patch in your source
build, rebuild, and publish through your normal release process.
