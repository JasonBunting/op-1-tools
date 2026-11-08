# op-1-tools

Plugins and companion apps for the original OP-1 running Hundekraft. See README.md for what each project is.

## Layout: one folder per project

The repository is organized by project, so anyone can take one piece without the rest. Keep it that way:

1. Anything about one project goes in that project's folder:
   1. `mirror/`: `plugin/`, `windows/`, `mobile/`, `docs/`, `release.sh`, `README.md`
   2. `patternplus/`: `plugin/`, `docs/`, `HARDWARE-TEST.md`, `README.md`
   3. `research/<tool>/`: throwaway probes, such as `rastprobe/` (`plugin/`, `listener/`)
2. Only shared things live at the root: `README.md` (an index of the projects), `LICENSE`, `CLAUDE.md`, `.env.example`,
   `op-1-tools.slnx` and `tools/` (scripts every project uses). Don't create root-level `docs/`, `firmware/`,
   `desktop/` or `mobile/` folders, or loose notes at the root.
3. A new plugin or project gets a new top-level folder with the same shape:
   1. `<name>/plugin/` with `plugin.json`, `LICENSE`, `NOTICE` (GPL-3.0-or-later with the OP-1 firmware permission,
      as in the existing plugins), and `test/` for host tests;
   2. `<name>/README.md`;
   3. a row in the root README's project table.
   `tools/build.sh` finds `*/plugin/` and `research/*/plugin/` folders on its own.
4. List new docs in the owning project's README table.
5. Personal files (device backups, videos, downloads, the Hundekraft source) stay outside the repository.

## Build

1. `tools/build.sh [project...]` packs plugins into `build/<id>.knl`, from Git Bash or WSL. It reads
   `HUNDEKRAFT_KENNEL` (the WSL path of the Hundekraft source's `ext/kennel`) from `.env`, which Git ignores; see
   `.env.example`.
2. `dotnet build op-1-tools.slnx` builds every C# project.
3. Each project's README has its host tests and, where there is one, its release steps. Release tags are per
   project: `mirror-v<version>`, `patternplus-v<version>`.

## Hundekraft core

Every plugin, doc, build path and release script follows the core installed on the OP-1 (3.2.3 at the time of
writing). On a core upgrade, rebuild every plugin and update every reference in one pass. The official plugin guide is
https://hundekraft.github.io/op1-workshop/llms.txt.
