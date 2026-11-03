# op-1-tools

Mirror the original OP-1's screen live on a computer, so you can read detailed UIs (Hundekraft plugins, synth
screens) on a big display while you twist knobs on the machine.

It is a "dumb display": a small plugin on the OP-1 sends the screen's pixels over USB MIDI, and the viewer just draws
them. Nothing about the OP-1's state is interpreted, so every screen, stock or custom, mirrors the same way.

```
OP-1 (Hundekraft 3.1.4)                         Windows PC
  mirror.knl ── changed 8x8 tiles as SysEx ──►  Op1Mirror (WPF)
             ◄── settings + keep-alive as CCs ──
```

Status: works on Windows with a real OP-1 running Hundekraft 3.1.4: about 40 fps on the OP-1 side, no lost messages,
no audible effect on audio. Android is next.

## Requirements

1. An original OP-1 running [Hundekraft](https://hundekraft.github.io/op1-workshop/) 3.1.4 (the plugin uses its
   feature-plugin runtime and addresses specific to that build).
2. Windows with the .NET 8 SDK or later.
3. To build the plugin yourself: WSL (Ubuntu 24.04) with a `bfin-elf` toolchain and the Hundekraft 3.1.4 source
   (for its SDK headers and packer).

## Use it

1. Install the plugin: open the Hundekraft workshop, go to **Your plugins**, drop in `mirror.knl`, then
   **Review changes → Send to OP-1** and press **T1** on the OP-1 to confirm.
2. **Close the browser completely.** Windows MIDI ports are single-client; while the workshop tab is open, the
   viewer can't connect (it says the OP-1 is busy).
3. Run the viewer:
   ```bash
   dotnet run --project desktop/Op1Mirror
   ```
   It finds the OP-1's MIDI ports by name, reconnects if the cable is pulled, and the OP-1 starts streaming on its
   own. Nothing needs switching on the device. When the viewer closes, the plugin goes quiet within ~3 seconds.

### Viewer controls

| Control | What it does |
| --- | --- |
| Budget (Auto) | Bytes the OP-1 may send per main-loop pass. Auto creeps it up and backs off on losses or a slowing OP-1. |
| Scan | Tiles checked for changes per pass. Higher picks up changes sooner. |
| Refresh | Tiles resent per pass regardless, so anything lost heals by itself. |
| Whole frames | Show only complete frames: no tile patchwork on busy screens (e.g. Strings), at fewer updates per second. |
| Palette / RLE tiles | Tile encodings the plugin may use; it picks the smallest per tile. Leave both on. |
| Whole-number zoom | Scale by exact multiples so every OP-1 pixel is an even block. |
| F11 / Esc | Fullscreen (hides the controls) / leave it. |
| T | Always on top. |

## Repository layout

| Path | What |
| --- | --- |
| `firmware/mirror/` | The OP-1 plugin (C). `mirror.c` documents the wire protocol at the top. |
| `firmware/mirror/test/host_test.c` | Runs the plugin on Linux/WSL against a fake framebuffer and records its SysEx. |
| `desktop/Op1Mirror/` | The Windows viewer (C#, WPF, [DryWetMIDI](https://github.com/melanchall/drywetmidi)). |
| `tools/build-bfin-toolchain.sh` | Builds binutils 2.42 + GCC 13.3.0 for `bfin-elf` into `~/bfin` (WSL). |
| `tools/pack-plugin.sh` | Packs a plugin folder into a `.knl` with Hundekraft's packer. |
| `docs/audit-001-framebuffer.md` | How the framebuffer was found and measured. |

## Build the plugin

In WSL, once:

```bash
tr -d '\r' < tools/build-bfin-toolchain.sh > /tmp/build-bfin.sh && bash /tmp/build-bfin.sh
```

Then, from the repository root (pass the `ext/kennel` folder of the Hundekraft 3.1.4 source):

```bash
bash tools/pack-plugin.sh <hundekraft-3.1.4-source>/ext/kennel firmware/mirror build/mirror.knl
```

From Git Bash on Windows, wrap it as
`MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu-24.04 --cd /mnt/d/src/op-1-tools -- bash tools/pack-plugin.sh …`.

### Test without the OP-1

`host_test.c` needs a 256x400 24-bit BMP of a screen and the SDK headers:

```bash
gcc -O2 -I<hundekraft-3.1.4-source>/ext/kennel/synth_sdk firmware/mirror/test/host_test.c -o /tmp/host_test
/tmp/host_test frame.bmp mirror.syx
```

Then decode the recording with the viewer:

```bash
dotnet run --project desktop/Op1Mirror -- --replay mirror.syx replay.bmp
```

It writes the picture plus `replay.bmp.txt` (messages, lost, bytes, frames) and exits non-zero on losses.

## How it works

1. **On the OP-1:** the plugin's Gfx overlay hook notes which framebuffer was just drawn (RGB565, portrait, 512-byte
   stride). Each main-loop pass (~123/s) hashes some 8x8 tiles, and sends the changed ones as raw, RLE or palette
   tiles within a byte budget, packed in SysEx (`F0 7D 'O' 'M' …`). In whole-frame mode it first copies one finished
   frame into a buffer the Hundekraft core grants it, sends all of that frame's changes, then marks the frame done.
2. **From the PC:** control changes on MIDI channel 16 keep the stream alive (CC 118) and set budget (119),
   scan (120), mode (121) and refresh (122). Without a viewer the plugin sends nothing.
3. **In the viewer:** tiles are written into a copy of the framebuffer, which is rotated into the OP-1's landscape view.

## Notes

1. The plugin calls an unexported firmware routine at a fixed address, so it only works on Hundekraft 3.1.4 as
   built from its release.
2. No Teenage Engineering firmware or device data belongs in this repository.

## License

MIT, see [LICENSE](LICENSE).
