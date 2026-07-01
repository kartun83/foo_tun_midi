# foo_tun_midi

A native macOS [foobar2000](https://www.foobar2000.org/mac) v2 component that
plays **Standard MIDI Files** (`.mid` / `.midi` / `.kar` / `.smf`) by rendering
them through [FluidSynth](https://www.fluidsynth.org/) and a SoundFont.

Built for one job: **quickly auditioning a MIDI library** — good enough to judge
a phrase or groove before importing selections into a DAW.

**[Changelog](CHANGELOG.md)** · **[Architecture](docs/ARCHITECTURE.md)** · **[Acknowledgments](ACKNOWLEDGMENTS.md)**

## Requirements

- foobar2000 for Mac 2.x (Apple Silicon / arm64), macOS 11.0+
- [FluidSynth](https://www.fluidsynth.org/) — `brew install fluid-synth`
- A SoundFont (`.sf2` / `.sf3`). A solid general-purpose choice is
  [MuseScore_General](https://ftp.osuosl.org/pub/musescore/soundfont/MuseScore_General/).
  The SoundFont is loaded once and reused (and preloaded at startup), so start-up
  latency is a non-issue after the first load — but note an uncompressed `.sf2`
  loads in ~20 ms versus several seconds for a compressed `.sf3`.
- To build from source: Xcode Command Line Tools, Ruby, and
  [`sevenzip`](https://formulae.brew.sh/formula/sevenzip) (`brew install sevenzip`,
  for unpacking the SDK).

## Install (binary)

1. `brew install fluid-synth`
2. Download `foo_tun_midi.fb2k-component` from [Releases](../../releases).
3. Create `~/Library/foobar2000-v2/user-components/foo_tun_midi/` and copy
   `foo_tun_midi.component` into it.
4. Restart foobar2000.
5. **Preferences → Input → MIDI Player** → choose your SoundFont.

## Build from source

```bash
git clone <this repo> foo_tun_midi && cd foo_tun_midi

# 1. Fetch + build the foobar2000 SDK (once). Downloads SDK-2025-03-07 from
#    foobar2000.org into ./SDK-2025-03-07 and builds its static libs.
./Scripts/bootstrap_sdk.sh

# 2. Build the component and install it into foobar2000.
./Scripts/build.sh --install
```

Other build options: `--clean`, `--regenerate` (after adding/removing source
files), `--debug`. Point at an existing SDK checkout with
`export FB2K_SDK_PATH=/path/to/SDK-2025-03-07`.

The SDK and `build/` are git-ignored; only source, scripts, and docs are tracked.

## Configuration

**Preferences → Input → MIDI Player**

| Setting | Purpose |
|---------|---------|
| **SoundFont** | Path to the `.sf2` / `.sf3` used for rendering. Falls back to a built-in default if unset. |
| **Force all channels to the drum kit** | Route every MIDI channel to the percussion bank. |

### Why "force percussion"?

Many drum-pattern libraries store GM-drum-map notes (36 = kick, 38 = snare,
42/44/46 = hi-hats) on **channel 1 with no program change** — they expect you to
drop the file onto a drum instrument in a DAW. Under General MIDI, only channel
10 is percussion, so those files otherwise render as *piano* (channel 1's
default patch). This is correct GM behaviour, not a bug. Enable **Force all
channels to the drum kit** to audition such files as drums; leave it off for
normal multi-instrument MIDI.

To see what a file actually contains:

```bash
python3 Scripts/midinfo.py path/to/file.mid
```

## Scope

**In scope:** SMF playback via FluidSynth + a user-selected SoundFont, for
library preview.

**Not (yet) in scope:** CLAP/VST plugin hosting, MT-32/OPL emulation, exotic
legacy MIDI containers (`.rcp` / `.xmi` / `.hmi` / `.mus`).

## License

MIT — see [LICENSE](LICENSE). This component links FluidSynth (LGPL) and builds
against the foobar2000 SDK; reused work is credited in
[ACKNOWLEDGMENTS.md](ACKNOWLEDGMENTS.md).
