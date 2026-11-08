# Pattern+

A sequencer type for Hundekraft 3.2.3 that looks and plays like the OP-1's stock Pattern sequencer, and adds:

1. **Pages:** more than one pattern per mode, switched with Shift + blue and green pushes.
2. **Chains:** play pages in an order you set, such as `A > B > C > C > D`.
3. **Saving:** your work comes back after a power off, and there are 8 named save slots per mode.

Status: in progress. The grid, pages and chains work on the device. Saving is newer and has had less testing, so
don't trust it with a good idea yet. There is no release; build it from source.

| Path | What |
| --- | --- |
| `plugin/` | The plugin (C): screen and input in `patternplus.c`, playback in `pp_player.c`, saving in `pp_store.c`. |
| `plugin/test/host_test.c` | Host test of pages, grid editing and playback. |
| [HARDWARE-TEST.md](HARDWARE-TEST.md) | Every control, which ones differ from stock, and the device tests. |
| [docs/pattern-plus-poc-spec.md](docs/pattern-plus-poc-spec.md) | The design. |
| `docs/Original_OP-1_Pattern_Sequencer_Technical_Research.md` | Research on the stock Pattern sequencer. |
| [docs/hundekraft-request-background-drums.md](docs/hundekraft-request-background-drums.md) | Notes for a Hundekraft feature request: keeping a drum pattern playing on the Synth screen. |

## Build and install

Set up the build once as described in [Build](../README.md#build) in the main README. Then, from the repository root:

1. Build it:
   ```bash
   tools/build.sh patternplus
   ```
2. Install `build/patternplus.knl`: open the Hundekraft workshop, go to **Your plugins**, add it, then review and
   send it to the OP-1.
3. Pick it as the sequencer type on the OP-1, where it shows as `PATTERNPLUS`.

Host test of the logic, in WSL:

```bash
gcc -Wall -I patternplus/plugin patternplus/plugin/test/host_test.c patternplus/plugin/pp_player.c patternplus/plugin/pp_store.c -o /tmp/pp_test && /tmp/pp_test
```

## License

GPL-3.0-or-later with the OP-1 firmware permission, the same terms as the Hundekraft SDK it is built with; see
[LICENSE](plugin/LICENSE) and [NOTICE](plugin/NOTICE).
