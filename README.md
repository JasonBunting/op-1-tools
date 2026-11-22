# op-1-tools

Plugins and companion apps for the original OP-1 running [Hundekraft](https://hundekraft.github.io/op1-workshop/).

| Project | What it is | Status | Get it |
| --- | --- | --- | --- |
| [OP-1 Mirror](mirror/README.md) | Shows the OP-1's screen live on Windows or an Android phone: a plugin plus two viewer apps | Works, v0.7.1 | [Releases](https://github.com/JasonBunting/op-1-tools/releases) |
| [Pattern+](patternplus/README.md) | A sequencer plugin like stock Pattern, plus pages, chains and saving | In progress, 0.5.3 | [Build from source](patternplus/README.md) |
| [Mouth](mouth/README.md) | A singing, babbling voice synth: vowels, bodies from giant to tiny, and made-up words | In progress, 0.5.2 | [Build from source](mouth/README.md) |
| [Ratatoskr](ratatoskr/README.md) | Grains from the sampler's recording around a squirrel playhead that holds, travels, bounces, wanders or jumps up a tree | In progress, 0.3.3 | [Build from source](ratatoskr/README.md) |

Shared as-is, in the hope it helps. It is a personal project and not actively supported: issues and pull requests
may sit unanswered, so fork freely.

## Requirements

1. An original OP-1 running Hundekraft 3.3. The mirror plugin also runs on 3.1.4 and 3.2.3.
2. To build from source:
   1. the .NET 8 SDK or later, for the mirror's viewers (the Android one also needs .NET 10 and the MAUI workload);
   2. for the plugins, WSL (Ubuntu 24.04) with a `bfin-elf` toolchain and the Hundekraft 3.3.1 source (for its SDK
      headers and packer).

## Layout

Each project is a folder of its own, so you can take just the piece you want:

| Path | What |
| --- | --- |
| `mirror/` | OP-1 Mirror: `plugin/` (the OP-1 side), `windows/` and `mobile/` viewers, `manual/`, `docs/`, `release.sh`. |
| `patternplus/` | Pattern+: `plugin/`, `manual/`, `docs/`, and `HARDWARE-TEST.md`, the controls and device tests. |
| `mouth/` | Mouth: `plugin/` (engine, formant table, preset generator, host test), `presets/`, `manual/`, `HARDWARE-TEST.md`. |
| `ratatoskr/` | Ratatoskr: `plugin/` (engine, screen, host test), `manual/`, `HARDWARE-TEST.md`. |
| `research/` | Throwaway probes; see [Research tools](#research-tools). |
| `tools/` | Shared scripts: the toolchain build, the plugin packer and `build.sh`. |
| `op-1-tools.slnx` | Every C# project, for Visual Studio or `dotnet build`. |
| `CLAUDE.md` | Layout and build rules for AI coding assistants (and a handy summary for people). |

A plugin always lives in `<project>/plugin/`, with its `plugin.json`, LICENSE and NOTICE, and a `test/` folder for
host tests. A new project gets a top-level folder with the same shape and its own README, and `tools/build.sh` finds
it with no changes.

## Build

Once:

1. In WSL, build the `bfin-elf` toolchain into `~/bfin`:
   ```bash
   tr -d '\r' < tools/build-bfin-toolchain.sh > /tmp/build-bfin.sh && bash /tmp/build-bfin.sh
   ```
2. Copy `.env.example` to `.env` (Git ignores it) and set `HUNDEKRAFT_KENNEL` to the WSL path of the Hundekraft
   source's `ext/kennel`.

Then, from the repository root, in Git Bash or WSL:

```bash
tools/build.sh
```

That packs every plugin into `build/<id>.knl`. Name projects to pack only those, as in `tools/build.sh patternplus`.

For the C# projects:

```bash
dotnet build op-1-tools.slnx
```

Each project's README covers running, testing and releasing it.

## License

1. **The plugins** (every `plugin/` folder, and so their `.knl` files): GPL-3.0-or-later with the OP-1 firmware
   permission, the same terms as the Hundekraft SDK they are built with. See each folder's LICENSE and NOTICE, for
   example [the mirror's](mirror/plugin/LICENSE).
2. **Everything else** (the viewers, tools and docs): MIT, see [LICENSE](LICENSE).

No Teenage Engineering firmware or device data is included in this repository or needed beyond what is already on
your OP-1.

## Research tools

`research/rastprobe/` is a throwaway probe (a plugin and a Windows listener), built against the Hundekraft 3.1.4
SDK, that was used to find the OP-1's framebuffer for the mirror. It isn't meant for everyday use.

`research/tractprobe/` is a throwaway synth engine that measures how big a physically modelled vocal tract the OP-1
can run, as groundwork for a vocal synth. See [its README](research/tractprobe/README.md).

`research/pushprobe/` is a throwaway synth engine that counts the knob pushes the OP-1 reports, to tell a bouncing
knob switch from a firmware problem. See [its README](research/pushprobe/README.md).

`research/pushguard/` is a prototype of a fix for knob pushes that register twice: Hundekraft's trigger guard extended to
the four knob pushes. See [its README](research/pushguard/README.md).

`research/op1inventory/` is a page that lists the plugins installed on the OP-1 and can switch them off, using the workshop's own
USB MIDI code. See [its README](research/op1inventory/README.md).
