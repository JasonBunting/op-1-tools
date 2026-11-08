# Audit 001 — Can a plugin mirror the screen as pixels?

Hundekraft 3.0.3 source (sha256 `2f745e38…b1d7`, from hundekraft.github.io), plus op1emu and op1r-src at their GitHub HEADs, 2026-10-29.

## Verdict

Very likely **yes, as an ordinary feature plugin, without changing the Kennel core.** The framebuffer is reachable from a per-frame hook. The weak spot is the USB transmit path for large SysEx messages: it works, but it is not a documented plugin API.

## Findings

1. **Per-frame hook.** `op1_feature_t.overlay(void *raster, int32_t screen)` runs on the Gfx thread every frame, after all screens are drawn (`kennel/kn_core.c` `kn_screens_update`). A plugin can read the finished frame there. `screen` gives the screen ID for free.
2. **Raster = native RGB565 framebuffer.** `op1_raster()` (fixed address `0x01A3B490`, `op1_plugin.h:219`) returns a struct whose word 0 is a `uint16_t*` RGB565 buffer. `kn_artcache.c` already reads and writes that buffer directly and RLE-encodes it.
3. **Geometry: portrait panel, rotated for display (mostly resolved).** The art cache checks `r[1]==198, r[2]==512, r[3]==400` and walks the buffer as width 198, height 400, stride 256 (in pixels). op1emu's screenshots of the panel are "rotated to the landscape orientation the GUI shows (400x200)". So the native framebuffer is portrait, about 200×400, and the viewer must rotate it. "320×160" in the old spec is wrong. Still to confirm: the 198-vs-200 width, the rotation direction, and what `r[2]==512` is.
4. **Missed layers.** The text-entry box and toasts are drawn *after* feature overlays, so a mirror would miss them for that one frame. That's minor; worst case they show one frame late.
5. **An RLE codec exists.** `kn_artcache_encode/decode` (32-bit runs: count<<16 | colour) is GPL source we can reuse as-is. OP-1 screens are mostly black, so compression should be large.
6. **MIDI out: the exported API is too small.** `op1_midi_send` (exported) sends only a single 1–3-byte message, and it rejects SysEx continuation packets. Multi-packet SysEx needs the firmware's `usbmidi_tx_enqueue` at `0x019E0D18`, which is what `devupd/du_fw.c` `send_sysex` calls. Plugins already call fixed addresses everywhere (`op1_plugin.h`), so this is consistent with house style, but it's unexported and its queue depth is unknown.
7. **MIDI in works.** `midi_raw` sees every inbound USB-MIDI packet, but its docs say "not SysEx". The devupd path parses SysEx from the RX ring. For the host→OP-1 direction (hello/resync), plain CCs or notes on a spare channel may be simpler than SysEx.
8. **SysEx size limits.** OP-1 *input* stops on SysEx over 96 bytes (`DU_USB_MAX_MSG`). The firmware's SysEx buffer has no length check (`DU_MAX_MSG` 256, "stay well below"). Output limits are unknown, so plan on many small messages.
9. **Namespace.** Devupd uses `F0 00 20 76 7D …`. Ours must differ, e.g. another sub-ID under `00 20 76`, or the non-commercial `7D` ID.
10. **Memory.** `op1_alloc` gives boot-lifetime arena memory with no free. A 100 KB previous-frame copy for dirty-region diffing competes with the art cache's 192 KB and plugin headroom, so check the arena.
11. **op1emu is a full test bench.** It emulates the PPI/DMA display path (RGB565 rows), has a `FrameTap` that captures complete frames, and exposes the OP-1's USB (audio + MIDI) over **USB/IP** on TCP 3240. In principle a plugin, our SysEx stream and a PC viewer can all be tested without hardware. On Windows that needs a USB/IP client such as usbip-win. It runs from your own NAND image or `op1_246.op1`.
12. **op1r-src matters less now.** It's the lineage of Kennel's screen and harness code, which the "dumb display" approach doesn't need. Its `op1push.py` is still a reference for host-side SysEx.

## Measured on hardware (rastprobe 0.2, Hundekraft 3.1.4, 2026-11-02)

1. **Proven end-to-end:** a plugin's main-loop service streamed the framebuffer over USB MIDI SysEx to a Windows listener, and the image reassembled correctly (Settings page, `research/rastprobe/plugin`, `research/rastprobe/listener`).
2. **Raster descriptor:** `r[0]` buffer, `r[1]` width, `r[2]` stride in bytes, `r[3]` height. The Settings page reads 188 × 376, stride 512. `r[4..7]` repeat the buffer and size, plus `r[5]=0x010A0804` (unknown). Size varies by screen; 198 × 400 is another panel.
3. **Double buffered:** `r[0]` alternates between `0x02000000` and `0x03000000`.
4. **Orientation:** stored portrait (the visible area is the top-left `r[1]` × `r[3]`). Rotate 90° clockwise for the OP-1's landscape view: landscape (x, y) = buffer (y, H−1−x).
5. **Rates:** about 29 frames/s drawn and about 123 main-loop passes/s. Two 66-byte SysEx messages per pass (about 16 KB/s) gave zero losses with no visible effect on the OP-1.

## Bandwidth sketch (unverified)

A raw frame is 320×160×2 ≈ 100 KB. A typical mostly-black screen at a few thousand runs is about 10–20 KB, ×8/7 for 7-bit SysEx and ×4/3 for USB-MIDI packets. Full-speed USB MIDI realistically moves tens of KB/s or more, depending on how fast the firmware drains its TX queue. That's enough for a few full frames a second, and much more using dirty regions only.

## Open questions → next steps

1. Raster geometry and orientation (item 3): dump `op1_raster()` words and one frame in op1emu.
2. TX queue: depth, drain rate and overflow behaviour of `0x019E0D18`. Does it block, drop, or corrupt?
3. CPU cost of diff + RLE on the Gfx thread per frame, and whether throttling to 5–10 fps is needed.
4. A minimal plugin: an `overlay` that sends a checksum of the frame every N frames. Then the full frame.
5. Windows side: receive SysEx (DryWetMIDI / Windows MIDI Services) and paint the 565 buffer.
