# Bifrost

Plug a USB MIDI keyboard straight into the original OP-1 and play it, no computer in between. A Hundekraft feature
plugin for Hundekraft 3.3.2. Named for the rainbow bridge between the worlds: it carries your notes from the keyboard
to the OP-1.

You need a **mini-USB OTG adapter** (or an OTG cable): mini-USB plug on one end, full-size USB socket on the other. It
has to be a real OTG one, which connects the plug's ID pin to ground; many sold as "OTG" are plain plugs that look the
same and do nothing. The cable that came with the OP-1 is not one.

Status: 0.1.0, built from [research/usbprobe](../research/usbprobe/README.md), which played an AKAI keyboard on the
OP-1. Host tests pass; Bifrost itself is not yet tried on the OP-1. See [HARDWARE-TEST.md](HARDWARE-TEST.md).

## What it does

1. **It starts by itself.** While no computer is plugged in, Bifrost checks every 5 s whether an OTG adapter is in.
   When one is, it switches the USB port's 5 V on and waits for a device.
2. **A MIDI device just plays.** Any class-compliant USB MIDI keyboard or controller. A pop-up shows its name
   ("MPK mini 3 connected"), and the OP-1 plays it as it plays MIDI from a computer: velocity, the OP-1's MIDI channel
   and everything Hundekraft does with MIDI.
3. **Unplug and replug the keyboard** whenever you like ("... unplugged"). The 5 V stays on while the adapter is in.
4. **Take the adapter out** and the USB port goes back to normal for the computer, the workshop, disk mode and Mirror.
5. **A device that isn't MIDI** (a flash drive, a mouse) gets "not a midi device" and is left alone.
6. **Turn it off** on the settings page: **Shift** + **Help**, then **T3**, then Shift + turn blue to **BIFROST**, and
   set **USB KEYBOARD** off. Off, the USB port is exactly as stock.

Limits: notes go one way, keyboard to OP-1 (nothing is sent back to the keyboard); one device at a time, no hubs; while
a device is plugged in the OP-1 powers it from its battery and doesn't charge.

## How it works

The facts, and where each came from, are in [research/usbhost](../research/usbhost/README.md) and the usbprobe
history in [research/usbprobe](../research/usbprobe/README.md). In short:

1. The OTG adapter's grounded ID pin lets the OP-1's USB controller (the BF52x's Mentor MUSB) be the host.
2. The port's 5 V comes from the OP-1's power controller: bit 0 of R246's power-output byte (`0x01DE4F79`), sent as the
   command `{1, byte}` to device 9 through `0x01A69024`, as R246 sends bit 1 of the same byte.
3. The USB controller never sees that 5 V as a host, so it raises VBUS errors that mean nothing here; and started with
   the 5 V already up, it takes the device side. The order that works: session off, power off until VBUS has fallen,
   power on, wait for VBUS valid, then the session.
4. Host mode runs polled with the controller's interrupt enables off, so R246's USB handler stays out. R246's
   "USB plugged" counter (`0x018B62A8`) is kept at 0 so its charging code doesn't start.
5. The keyboard's USB-MIDI packets go into R246's own USB-MIDI receive ring (`0x01AB0510`), so the stock parser plays
   them as MIDI from a computer.

| File | What |
| --- | --- |
| `plugin/bifrost.c` | The feature: the states (idle, probe, start, waiting for a device, running), the settings row, the pop-ups. |
| `plugin/bf_usb.c`, `bf_usb.h` | The USB controller as a host, the port's 5 V, enumeration, MIDI into the receive ring. |
| `plugin/usb_desc.h` | Descriptor parsing, device names and ring words, with no hardware in them. |
| `plugin/test/host_test.c` | Host test for `usb_desc.h`. |
| `manual/manual.html` | The manual. |
| `HARDWARE-TEST.md` | The device test. |

## Build and test

```bash
tools/build.sh bifrost      # build/bifrost.knl
gcc -Wall -Wextra -I bifrost/plugin bifrost/plugin/test/host_test.c -o /tmp/bf && /tmp/bf
```

Install `build/bifrost.knl` from the workshop's **Your plugins** page.
