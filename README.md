# op-1-tools

Plugins and companion apps for the original OP-1 running [Hundekraft](https://hundekraft.github.io/op1-workshop/).

| Project | What it is | Status | Get it |
| --- | --- | --- | --- |
| [OP-1 Mirror](mirror/README.md) | Shows the OP-1's screen live on Windows or an Android phone: a plugin plus two viewer apps | Works, v0.7 | [Releases](https://github.com/JasonBunting/op-1-tools/releases) |
| [Pattern+](patternplus/README.md) | A sequencer plugin like stock Pattern, plus pages, chains and saving | In progress, 0.3.3 | [Build from source](patternplus/README.md) |

Shared as-is, in the hope it helps. It is a personal project and not actively supported: issues and pull requests
may sit unanswered, so fork freely.

## Requirements

1. An original OP-1 running Hundekraft 3.2.3. The mirror plugin also runs on 3.1.4.
2. To build from source:
   1. the .NET 8 SDK or later, for the mirror's viewers (the Android one also needs .NET 10 and the MAUI workload);
   2. for the plugins, WSL (Ubuntu 24.04) with a `bfin-elf` toolchain and the Hundekraft 3.2.3 source (for its SDK
      headers and packer).

## Layout

Each project is a folder of its own, so you can take just the piece you want:

| Path | What |
| --- | --- |
| `mirror/` | OP-1 Mirror: `plugin/` (the OP-1 side), `windows/` and `mobile/` viewers, `docs/`, `release.sh`. |
| `patternplus/` | Pattern+: `plugin/`, `docs/`, and `HARDWARE-TEST.md`, the controls and device tests. |
| `research/` | Throwaway probes; see [Research tools](#research-tools). |
| `tools/` | Shared scripts: the toolchain build, the plugin packer and `build.sh`. |
| `op-1-tools.slnx` | Every C# project, for Visual Studio or `dotnet build`. |

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
