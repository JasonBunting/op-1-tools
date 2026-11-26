# usbprobe

A throwaway synth engine that tries USB host mode on the OP-1, so a class-compliant MIDI keyboard plugged in through a
mini-USB OTG adapter plays it. How it works and where the facts come from: [../usbhost/README.md](../usbhost/README.md).

Build with `tools/build.sh usbprobe`, which makes `build/usbprobe.knl`. The device test is
[HARDWARE-TEST.md](HARDWARE-TEST.md).

1. It makes no sound itself. Its page shows what the USB controller and the keyboard do; its work runs on every
   main-loop pass whatever sound is current, so once the keyboard runs, any other sound plays from it.
2. Blue push starts host mode, and again stops it and gives the port back to the computer. Green push clears the
   counters.
3. While host mode runs, the computer, disk mode and Mirror can't use the USB port. Stop it first, or restart the
   OP-1.

Host test (descriptor parsing and receive-ring words):

```bash
gcc -Wall -I research/usbprobe/plugin research/usbprobe/plugin/test/host_test.c -o /tmp/up && /tmp/up
```

## 0.1.0

First try. Unknowns it should answer: does the session start and power the keyboard (`devctl 1d`: session, host,
VBUS valid; `5d` with a full-speed device on it), does the keyboard connect and enumerate (its vid and pid, a MIDI IN endpoint), and do its notes arrive
(`packets` counting up) and play.

## 0.1.1

0.1.0 with the OTG cable and nothing on it: red "the device draws too much power", which was the controller's VBUS
error flag, not a real load. The OTG cable did make the OP-1 the host. 0.1.1 sets the session again on a VBUS error
while the session is starting (up to three times, as Linux's musb driver does), writes VPLEN = 1 before the session as
Opus does, words the error as "no power on the port", and shows the VBUS error count, `devctl` at the last error and
the VPLEN it found.

## 0.1.2

0.1.1 still went red with a VBUS error. Opus's code shows why: as host, the OP-1's USB controller never sees the
port's power (the VBUS bits stay 0 and VBUS errors fire), and Opus ignores that. Its test is the session and A-device
bits held for 40 ms with no outside power on the port (outside power means a computer, and it backs out). 0.1.2 does
the same: it counts VBUS errors without failing on them, keeps the session set, and expects `devctl 05` (`45` with a
full-speed keyboard). "Stayed a device: not an otg cable" now names the stock-cable case.

## 0.1.3

0.1.2: host mode, but the AKAI stayed dark: the USB controller doesn't power the port on the OP-1. Opus does it through
R246's power controller: bit 0 of the power-output byte at `0x01DE4F79`, sent as the 2-byte command `{1, byte}` to
device 9 through `0x01A69024` (R246 sends bit 1 of the same byte the same way at `0x01A11410`). Opus's order, which
0.1.3 follows: a first session with no power (a VBUS error there means an OTG adapter), then power on, 150 ms, the
session again; power off when host mode ends. The status row shows `5v on` / `5v off`.

## 0.1.4

0.1.3: the AKAI lit up (the port's 5 V works), but the probe kept waiting for the keyboard: it waited for the
controller's connect interrupt. Opus watches DEV_CTL's speed bits instead (0x40 full speed, 0x20 low speed) and starts
the bus reset when one comes on; 0.1.4 takes either.

## 0.1.5

0.1.4: the AKAI lit, then `devctl 99` (session, VBUS valid, but the B side) after one VBUS error: the keyboard's
inrush dipped the 5 V, the error ended the session, and setting the session again with our own 5 V already on the line
made the controller take the B side. 0.1.5 follows Opus's states 2..4 for every start and restart: session off, power
off until VBUS falls, power on, wait for VBUS valid, session, then wait (2 s) for the host-mode bit. A lost session or
the B side restarts that (up to 5 times, shown as `restarts`).
