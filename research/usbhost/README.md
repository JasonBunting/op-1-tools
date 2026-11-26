# USB host on the OP-1: research notes

Goal: a class-compliant USB MIDI keyboard plugged straight into the OP-1 on Hundekraft, as Opus 2 beta 5 does.
These are our own notes on how the hardware and the stock firmware work. Nothing of Opus's (code, listings, binaries)
belongs in this repository; the rebuilt Opus firmware and its disassembly stay in `D:\src\OP-1-Related\opus-fw`.

## The hardware: no modification

1. A real mini-USB OTG adapter grounds the ID pin. With that, the BF52x's USB controller (Mentor MUSB, registers
   at `0xFFC03800`) can be the A-device and drive VBUS itself, powering one device from the OP-1's battery. Opus's
   release notes: one device at a time, no hub, no charging while a device is in, and many cheap "OTG" adapters are
   plain plugs that don't ground ID.
2. The stock firmware never uses host mode, but the board is wired for it.

## Starting host mode (the textbook MUSB A-device start)

1. Save the device-mode registers you'll want back (`POWER` 0x3804, `GLOBAL_CTL` 0x3830, `TESTMODE` 0x3828) and
   the interrupt enables (`INTRTXE` 0x3810, `INTRRXE` 0x3814, `INTRUSBE` 0x381C); write the enables to 0 so the
   stock device-mode interrupt handler stays out of it, and poll instead.
2. Clear `POWER` bit 6 (SOFTCONN): the OP-1 leaves the computer's bus.
3. Write `VPLEN` (0x394C). The value Opus uses is one it saved earlier; still to find out (open question 2).
4. Write `OTG_DEV_CTL` (0x3900) = 1 (SESSION).
5. Poll `OTG_DEV_CTL`: `& 0x81 == 0x01` means session on and A-device (host); `& 0x18 == 0x18` means VBUS valid.
6. Reset the bus, then enumerate on endpoint 0 the usual MUSB host way: `INDEX` (0x3824) = 0, the 8-byte setup
   packet into the EP0 FIFO (0x3880), `CSR0` (0x3844) = SETUPPKT | TXPKTRDY (0x0A), wait, then the IN data stage
   (REQPKT 0x20) and the status stage (0x60 / 0x40). Linux's `drivers/usb/musb` and ADI's BF52x hardware manual cover
   all of it.
7. Find the device's USB-MIDI streaming interface (class 1, subclass 3) and its bulk IN endpoint, set the
   configuration, then poll that endpoint for 4-byte USB-MIDI event packets.

## Getting the notes into the OP-1: the stock USB MIDI ring

The stock firmware already queues USB MIDI from the computer in a ring that one routine drains:

| Address (R246) | What |
|---|---|
| `0x01AB0510` | ring: 1024 x 32-bit USB-MIDI event packets |
| `0x01AB1510` | write index (base + 0x1000), mask 0x3FF |
| `0x01AB1514` | read index (base + 0x1004) |
| `0x019E31A4` | the reader: drains the ring and dispatches each packet by its code index (byte 0 & 0x0F) through the jump table at `0x018E45EC` |
| `0x019E0D18` | USB MIDI send (`TX_ENQ` in Hundekraft's `kn_core.c` and in our Mirror) |

So a keyboard's packets only need appending to the ring, with interrupts off, the cable number (byte 0's high
nibble) cleared, and the write index moved on. After that, velocity, channels and everything else play as MIDI from a
computer does.

## Could Hundekraft do it?

Probably, and maybe even from a plugin. Everything above is register pokes, polling and one ring in stock RAM; no
interrupt hook is needed if host mode runs polled. The parts to decide:

1. Where the polling runs: a plugin's per-block callback, or a small core hook (an upstream request to Hundekraft).
2. Switching back to device mode on unplug, so the computer, disk mode and Mirror keep working.
3. Charging: the battery/charger code has to stay off while the OP-1 powers the device.

The first try is [../usbprobe/](../usbprobe/README.md): host mode from a plugin's main-loop service.

## Open questions

1. Does anything in the stock firmware fight the session (its USB task, the charger code reading `OTG_DEV_CTL`)?
2. The `VPLEN` value, and whether a PHY or clock change is needed beyond what device mode already set up.
3. How a mid-session unplug is detected and undone.
4. Whether host mode works from inside a plugin without a core hook.
