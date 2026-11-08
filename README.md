# op-1-tools

Plugins and companion apps for the original OP-1 running [Hundekraft](https://hundekraft.github.io/op1-workshop/).

| Project | What it is | Status | Get it |
| --- | --- | --- | --- |
| [OP-1 Mirror](#op-1-mirror) | Shows the OP-1's screen live on Windows or an Android phone: a plugin plus two viewer apps | Works, v0.7 | [Releases](../../releases) |
| [Pattern+](#pattern) | A sequencer plugin like stock Pattern, plus pages, chains and saving | In progress, 0.3.3 | [Build from source](#pattern) |

Shared as-is, in the hope it helps. It is a personal project and not actively supported: issues and pull requests
may sit unanswered, so fork freely.

## Requirements

1. An original OP-1 running Hundekraft 3.2.3. The mirror plugin also runs on 3.1.4.
2. For the mirror: Windows 10/11 (64-bit) or an Android 8+ phone with USB-C.
3. To build from source:
   1. the .NET 8 SDK or later, for the viewers;
   2. for the plugins, WSL (Ubuntu 24.04) with a `bfin-elf` toolchain and the Hundekraft 3.2.3 source (for its SDK
      headers and packer). See [Build a plugin](#build-a-plugin).

## OP-1 Mirror

Mirror the OP-1's screen live on a computer or phone, so you can read detailed UIs (Hundekraft plugins, synth
screens) on a big display while you twist knobs on the machine.

It is a "dumb display": a small plugin on the OP-1 sends the screen's pixels over USB MIDI, and the viewer just draws
them. Nothing about the OP-1's state is interpreted, so every screen, stock or custom, mirrors the same way.

```
OP-1 (Hundekraft)                               Windows PC
  mirror.knl ── changed 8x8 tiles as SysEx ──►  Op1Mirror (WPF)
             ◄── settings + keep-alive as CCs ──
```

Status: works on Windows and Android with a real OP-1 running Hundekraft 3.1.4 or 3.2.3: about 40 fps on the OP-1
side, no lost messages, no audible effect on audio.

No need to build anything: the [Releases](../../releases) page has `mirror.knl`, `Op1Mirror.exe` (Windows, .NET
included) and `op1-mirror.apk` (Android).

### Use it

1. Install the plugin: open the Hundekraft workshop, go to **Your plugins**, drop in `mirror.knl`, then
   **Review changes → Send to OP-1** and press **T1** on the OP-1 to confirm.
2. **Close the browser completely.** Windows MIDI ports are single-client; while the workshop tab is open, the
   viewer can't connect (it says the OP-1 is busy).
3. Run the viewer: `Op1Mirror.exe` from a release, or from source:
   ```bash
   dotnet run --project desktop/Op1Mirror
   ```
   It finds the OP-1's MIDI ports by name, reconnects if the cable is pulled, and the OP-1 starts streaming on its
   own. Nothing needs switching on the device. When the viewer closes, the plugin goes quiet within ~3 seconds.

#### Viewer controls

Both viewers remember their settings between launches; on Windows that includes the window's size and place, kept in
`%LOCALAPPDATA%\Op1Mirror\settings.json`. Delete that file to go back to the defaults.

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

#### On an Android phone

Same plugin, different viewer: `mobile/Op1Mirror.Mobile` (.NET MAUI, Android 8+ with USB host support). From a
release, install `op1-mirror.apk` (allow installs from unknown sources) and skip to step 3. From source:

1. Put the machine's SDK paths in `mobile/Directory.Build.local.props` (Git ignores it):
   ```xml
   <Project>
     <PropertyGroup>
       <AndroidSdkDirectory>C:\Program Files (x86)\Android\android-sdk</AndroidSdkDirectory>
       <JavaSdkDirectory>C:\Users\you\AppData\Local\Microsoft\OpenJDK\17</JavaSdkDirectory>
     </PropertyGroup>
   </Project>
   ```
2. With the phone plugged into the PC (USB debugging on), build and install:
   ```bash
   dotnet build mobile/Op1Mirror.Mobile -t:Run -f net10.0-android
   ```
3. Unplug the phone from the PC and plug the OP-1 into the phone with a USB-C OTG cable or adapter. The app finds
   it on its own. Tap the screen for the controls; they match the Windows viewer.

### How it works

1. **On the OP-1:** the plugin's Gfx overlay hook notes which framebuffer was just drawn (RGB565, portrait, 512-byte
   stride). Each main-loop pass (~123/s) hashes some 8x8 tiles, and sends the changed ones as raw, RLE or palette
   tiles within a byte budget, packed in SysEx (`F0 7D 'O' 'M' …`). In whole-frame mode it first copies one finished
   frame into a buffer the Hundekraft core grants it, sends all of that frame's changes, then marks the frame done.
2. **From the PC:** control changes on MIDI channel 16 keep the stream alive (CC 118) and set budget (119),
   scan (120), mode (121) and refresh (122). Without a viewer the plugin sends nothing.
3. **In the viewer:** tiles are written into a copy of the framebuffer, which is rotated into the OP-1's landscape view.

### Test without the OP-1

`host_test.c` needs a 256x400 24-bit BMP of a screen and the SDK headers:

```bash
gcc -O2 -I<hundekraft-3.2.3-source>/ext/kennel/synth_sdk firmware/mirror/test/host_test.c -o /tmp/host_test
/tmp/host_test frame.bmp mirror.syx
```

Then decode the recording with the viewer:

```bash
dotnet run --project desktop/Op1Mirror -- --replay mirror.syx replay.bmp
```

It writes the picture plus `replay.bmp.txt` (messages, lost, bytes, frames) and exits non-zero on losses.

### Notes

1. The plugin calls an unexported firmware routine at a fixed address. It is built against Hundekraft 3.2.3 and also
   runs on 3.1.4; a later core may need checking.
2. The viewer sends controller messages 118–122 on MIDI channel 16. On Hundekraft 3.2.3 and later, leave the COM
   screen's PARAM CH off or on another channel: on channel 16, message 118 would keep setting the tape input level.

## Pattern+

A sequencer type for Hundekraft 3.2.3 that looks and plays like the OP-1's stock Pattern sequencer, and adds:

1. **Pages:** more than one pattern per mode, switched with Shift + blue and green pushes.
2. **Chains:** play pages in an order you set, such as `A > B > C > C > D`.
3. **Saving:** your work comes back after a power off, and there are 8 named save slots per mode.

Status: in progress. The grid, pages and chains work on the device. Saving is newer and has had less testing, so
don't trust it with a good idea yet. There is no release; build it from source.

1. Build it (see [Build a plugin](#build-a-plugin)):
   ```bash
   bash tools/pack-plugin.sh <hundekraft-3.2.3-source>/ext/kennel firmware/patternplus build/patternplus.knl
   ```
2. Install `build/patternplus.knl` from the workshop's **Your plugins**, as for the mirror.
3. Pick it as the sequencer type on the OP-1, where it shows as `PATTERNPLUS`.

[HARDWARE-TEST.md](firmware/patternplus/HARDWARE-TEST.md) has every control, which ones differ from stock, and the
device tests. The design is in [docs/pattern-plus-poc-spec.md](docs/pattern-plus-poc-spec.md).

Host test of the logic (Linux/WSL):

```bash
gcc -Wall -I firmware/patternplus firmware/patternplus/test/host_test.c firmware/patternplus/pp_player.c firmware/patternplus/pp_store.c -o /tmp/pp_test && /tmp/pp_test
```

## Build a plugin

In WSL, once, build the toolchain:

```bash
tr -d '\r' < tools/build-bfin-toolchain.sh > /tmp/build-bfin.sh && bash /tmp/build-bfin.sh
```

Then, from the repository root, pack any plugin folder (pass the `ext/kennel` folder of the Hundekraft 3.2.3 source).
The output file must be named after the plugin's id:

```bash
bash tools/pack-plugin.sh <hundekraft-3.2.3-source>/ext/kennel firmware/mirror build/mirror.knl
```

From Git Bash on Windows, wrap it as
`MSYS_NO_PATHCONV=1 wsl.exe -d Ubuntu-24.04 --cd /mnt/d/src/op-1-tools -- bash tools/pack-plugin.sh …`.

## Repository layout

| Path | What |
| --- | --- |
| `firmware/mirror/` | The mirror plugin (C). `mirror.c` documents the wire protocol at the top. |
| `firmware/mirror/test/host_test.c` | Runs the plugin on Linux/WSL against a fake framebuffer and records its SysEx. |
| `firmware/patternplus/` | The Pattern+ sequencer plugin (C), its host test and its couch sheet. |
| `desktop/Op1Mirror/` | The Windows viewer (C#, WPF, [DryWetMIDI](https://github.com/melanchall/drywetmidi)). |
| `mobile/Op1Mirror.Mobile/` | The Android viewer (C#, .NET MAUI, SkiaSharp); shares the Windows viewer's decoder. |
| `tools/build-bfin-toolchain.sh` | Builds binutils 2.42 + GCC 13.3.0 for `bfin-elf` into `~/bfin` (WSL). |
| `tools/pack-plugin.sh` | Packs a plugin folder into a `.knl` with Hundekraft's packer. |
| `tools/release.sh` | Builds the mirror's three release files and, with `--publish`, the GitHub release. |
| `docs/` | How the framebuffer was found, the Pattern+ spec, and research on stock Pattern. |

## Make a release

Releases cover the mirror only, for now. From Git Bash, on a clean commit:

1. Once: make an Android signing key and keep it, and its password, somewhere safe outside the repository. Every
   later version of the app must be signed with the same key, or phones refuse the update.
   ```bash
   keytool -genkeypair -keystore op1mirror.keystore -alias op1mirror -keyalg RSA -keysize 2048 -validity 36500
   ```
   (`keytool` is in the JDK's `bin` folder.)
2. Set `HUNDEKRAFT_KENNEL` (WSL path of the Hundekraft source's `ext/kennel`), `OP1_KEYSTORE` and
   `OP1_KEYSTORE_PASS`, then build into `build/release/<tag>/`:
   ```bash
   tools/release.sh
   ```
   The tag defaults to `v` plus the mirror plugin's version. Add `--allow-dirty` for a trial build.
3. Tag and push the commit, then publish (or run the `gh` command it prints):
   ```bash
   tools/release.sh --publish
   ```

## An iPhone viewer?

Nobody has made one, but the code is the easy part. The Android viewer is .NET MAUI, and MAUI builds for iOS too: the
decoder, screen, controls and settings carry over, and `Platforms/iOS` is already scaffolded. What's missing is an
iOS `IMidiLink` on CoreMIDI, which handles the OP-1 as a standard USB MIDI device with no permission dance; probably a
day or two of work. Plug the OP-1 into a USB-C iPhone directly, or into an older one with Apple's Lightning to USB
camera adapter.

The hard part is Apple's process: building needs a Mac with Xcode, a free account's installs expire after 7 days, and
sharing it means a paid developer account plus TestFlight or App Store review. With no iPhone or Mac here, it's open to
anyone who has both: fork freely.

## License

1. **The plugins** (`firmware/mirror/`, `firmware/patternplus/`, and so their `.knl` files): GPL-3.0-or-later with
   the OP-1 firmware permission, the same terms as the Hundekraft SDK they are built with. See each folder's LICENSE
   and NOTICE, for example [the mirror's](firmware/mirror/LICENSE).
2. **Everything else** (the viewers, tools and docs): MIT, see [LICENSE](LICENSE).

No Teenage Engineering firmware or device data is included in this repository or needed beyond what is already on
your OP-1.

## Research tools

`firmware/rastprobe/` and `desktop/RastProbe.Listener/` are a throwaway probe, built against the Hundekraft 3.1.4 SDK,
that was used to find the OP-1's framebuffer for the mirror. It isn't meant for everyday use.
