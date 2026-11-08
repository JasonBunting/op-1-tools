# OP-1 Mirror

Mirror the original OP-1's screen live on a computer or phone, so you can read detailed UIs (Hundekraft plugins,
synth screens) on a big display while you twist knobs on the machine.

It is a "dumb display": a small plugin on the OP-1 sends the screen's pixels over USB MIDI, and the viewer just draws
them. Nothing about the OP-1's state is interpreted, so every screen, stock or custom, mirrors the same way.

```
OP-1 (Hundekraft)                               Windows PC
  mirror.knl ── changed 8x8 tiles as SysEx ──►  Op1Mirror (WPF)
             ◄── settings + keep-alive as CCs ──
```

Status: works on Windows and Android with a real OP-1 running Hundekraft 3.1.4 or 3.2.3: about 40 fps on the OP-1
side, no lost messages, no audible effect on audio.

## Requirements

1. An original OP-1 running Hundekraft 3.2.3 or 3.1.4.
2. Windows 10/11 (64-bit) or an Android 8+ phone with USB-C.

No need to build anything: the [Releases](https://github.com/JasonBunting/op-1-tools/releases) page has
`mirror.knl`, `Op1Mirror.exe` (Windows, .NET included) and `op1-mirror.apk` (Android). To build from source, see
[Build](../README.md#build) in the main README. Commands below run from the repository root.

| Path | What |
| --- | --- |
| `plugin/` | The OP-1 plugin (C). `mirror.c` documents the wire protocol at the top. |
| `plugin/test/host_test.c` | Runs the plugin on Linux/WSL against a fake framebuffer and records its SysEx. |
| `windows/` | The Windows viewer (C#, WPF, [DryWetMIDI](https://github.com/melanchall/drywetmidi)). |
| `mobile/` | The Android viewer (C#, .NET MAUI, SkiaSharp); shares the Windows viewer's decoder. |
| `docs/audit-001-framebuffer.md` | How the framebuffer was found and measured. |
| `release.sh` | Builds the three release files and, with `--publish`, the GitHub release. |

## Use it

1. Install the plugin: open the Hundekraft workshop, go to **Your plugins**, drop in `mirror.knl`, then
   **Review changes → Send to OP-1** and press **T1** on the OP-1 to confirm.
2. **Close the browser completely.** Windows MIDI ports are single-client; while the workshop tab is open, the
   viewer can't connect (it says the OP-1 is busy).
3. Run the viewer: `Op1Mirror.exe` from a release, or from source:
   ```bash
   dotnet run --project mirror/windows
   ```
   It finds the OP-1's MIDI ports by name, reconnects if the cable is pulled, and the OP-1 starts streaming on its
   own. Nothing needs switching on the device. When the viewer closes, the plugin goes quiet within ~3 seconds.

### Viewer controls

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

### On an Android phone

Same plugin, different viewer: `mobile/` (.NET MAUI, Android 8+ with USB host support). From a release, install
`op1-mirror.apk` (allow installs from unknown sources) and skip to step 3. From source:

1. Put the machine's SDK paths in `mirror/mobile/Directory.Build.local.props` (Git ignores it):
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
   dotnet build mirror/mobile -t:Run -f net10.0-android
   ```
3. Unplug the phone from the PC and plug the OP-1 into the phone with a USB-C OTG cable or adapter. The app finds
   it on its own. Tap the screen for the controls; they match the Windows viewer.

## How it works

1. **On the OP-1:** the plugin's Gfx overlay hook notes which framebuffer was just drawn (RGB565, portrait, 512-byte
   stride). Each main-loop pass (~123/s) hashes some 8x8 tiles, and sends the changed ones as raw, RLE or palette
   tiles within a byte budget, packed in SysEx (`F0 7D 'O' 'M' …`). In whole-frame mode it first copies one finished
   frame into a buffer the Hundekraft core grants it, sends all of that frame's changes, then marks the frame done.
2. **From the PC:** control changes on MIDI channel 16 keep the stream alive (CC 118) and set budget (119),
   scan (120), mode (121) and refresh (122). Without a viewer the plugin sends nothing.
3. **In the viewer:** tiles are written into a copy of the framebuffer, which is rotated into the OP-1's landscape view.

## Test without the OP-1

`host_test.c` needs a 256x400 24-bit BMP of a screen and the SDK headers. In WSL:

```bash
gcc -O2 -I<hundekraft-3.2.3-source>/ext/kennel/synth_sdk mirror/plugin/test/host_test.c -o /tmp/host_test
/tmp/host_test frame.bmp mirror.syx
```

Then decode the recording with the viewer:

```bash
dotnet run --project mirror/windows -- --replay mirror.syx replay.bmp
```

It writes the picture plus `replay.bmp.txt` (messages, lost, bytes, frames) and exits non-zero on losses.

## Make a release

From Git Bash, on a clean commit:

1. Once: make an Android signing key and keep it, and its password, somewhere safe outside the repository. Every
   later version of the app must be signed with the same key, or phones refuse the update.
   ```bash
   keytool -genkeypair -keystore op1mirror.keystore -alias op1mirror -keyalg RSA -keysize 2048 -validity 36500
   ```
   (`keytool` is in the JDK's `bin` folder.)
2. Set `OP1_KEYSTORE` (in `.env` or the shell) and `OP1_KEYSTORE_PASS` (in the shell only), then build into
   `build/release/<tag>/`:
   ```bash
   mirror/release.sh
   ```
   The tag defaults to `mirror-v` plus the plugin's version (releases up to v0.7 were tagged plain `v0.7`). Add
   `--allow-dirty` for a trial build.
3. Tag and push the commit, then publish (or run the `gh` command it prints):
   ```bash
   mirror/release.sh --publish
   ```

## Notes

1. The plugin calls an unexported firmware routine at a fixed address. It is built against Hundekraft 3.2.3 and also
   runs on 3.1.4; a later core may need checking.
2. The viewer sends controller messages 118–122 on MIDI channel 16. On Hundekraft 3.2.3 and later, leave the COM
   screen's PARAM CH off or on another channel: on channel 16, message 118 would keep setting the tape input level.

## An iPhone viewer?

Nobody has made one, but the code is the easy part. The Android viewer is .NET MAUI, and MAUI builds for iOS too: the
decoder, screen, controls and settings carry over, and `mobile/Platforms/iOS` is already scaffolded. What's missing
is an iOS `IMidiLink` on CoreMIDI, which handles the OP-1 as a standard USB MIDI device with no permission dance;
probably a day or two of work. Plug the OP-1 into a USB-C iPhone directly, or into an older one with Apple's Lightning
to USB camera adapter.

The hard part is Apple's process: building needs a Mac with Xcode, a free account's installs expire after 7 days, and
sharing it means a paid developer account plus TestFlight or App Store review. With no iPhone or Mac here, it's open to
anyone who has both: fork freely.

## License

The plugin (`plugin/`, and so `mirror.knl`) is GPL-3.0-or-later with the OP-1 firmware permission; see
[its LICENSE](plugin/LICENSE) and [NOTICE](plugin/NOTICE). The viewers and docs are MIT, see
[LICENSE](../LICENSE).
