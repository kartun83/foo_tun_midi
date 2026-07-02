# Changelog

All notable changes to foo_tun_midi will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [0.5.0] - 2026-07-02

### Added

- **Headless CLAP preset switching (Full build).** A hosted CLAP instrument's
  presets can now be selected without opening the plugin's GUI, via the CLAP
  `preset-discovery` + `preset-load` extensions. Preferences gained a **Preset**
  dropdown next to the plugin picker; a **Presets** button loads the selected
  plugin's preset list on demand. Works for plugins that ship a preset-discovery
  provider (e.g. Baby Audio Tekno); plugins without one (e.g. Vital) silently
  keep their default patch. The choice is applied when the plugin is
  instantiated and persists across launches.

### Fixed / Changed

- **Reduced the CLAP plugin scanner's memory cost.** Diagnosed a
  macOS out-of-memory: because macOS never unloads an Obj-C/JUCE plugin image on
  `dlclose`, each scan of the installed plugins permanently raised foobar2000's
  memory (~200 MB for a large plugin folder), and repeated Rescans compounded it.
  The scanner now `dlopen`s with `RTLD_LAZY` (descriptor read only) and the
  preferences hint warns that Rescan/Presets raise memory until restart. Playback
  never scans. A full fix (out-of-process scanning) is tracked in
  docs/ARCHITECTURE.md.

## [0.4.0] - 2026-07-01

### Added

- **CLAP instrument hosting (new "Full" build).** A second rendering backend
  hosts a single CLAP instrument plugin and plays a MIDI file's notes through it
  — for previewing a pattern into a synth or drum machine. Two components are
  now built from one source tree: **`foo_tun_midi`** (FluidSynth only, unchanged)
  and **`foo_tun_midi_clap`** (adds the CLAP engine). Install one or the other,
  not both. The Full build's preferences add an **Engine** picker (FluidSynth /
  CLAP) and a **plugin dropdown** auto-populated by scanning the installed CLAP
  instruments (cached; a Rescan button re-walks after installing new plugins).
  CLAP-backed files are non-seekable (the seekbar is disabled) and use the
  plugin's own sound, so percussion/SoundFont settings don't apply to them.
  Note: plugins that JIT-compile at runtime (the DSP56300 emulations —
  NodalRed2x, OsTIrus, Osirus, Vavra) crash foobar2000, which lacks the macOS
  JIT entitlement; non-JIT plugins (Tekno, Vital, u-he, …) work. See
  docs/ARCHITECTURE.md.

### Changed

- **Rendering is now behind an `IMidiRenderer` interface**, with FluidSynth as
  one implementation and CLAP as the other. No behavior change for FluidSynth
  playback. `Scripts/build.sh` gained `--variant lite|full|both`, `--package`
  (build a `.fb2k-component`), and now refuses `--install` with `--variant both`.

## [0.3.0] - 2026-07-01

### Added

- **Auto-detect drum files.** Percussion handling is now a three-way choice —
  **Off** (General MIDI), **Auto**, or **Always** — replacing the on/off toggle.
  Auto forces the drum kit only for files that look like drum patterns misrouted
  off channel 10 (notes on a melodic channel, no program change, in the GM drum
  range), so a mixed library plays correctly without touching the setting. An
  existing "force on" setting migrates to Always; new installs default to Auto.
- **Metadata as tags.** The SMF's sequence name (as title), tempo (BPM), time
  signature, key signature, instrument names, and copyright are parsed and
  exposed as track fields. Text is normalised to valid UTF-8 (old Latin-1 files
  included).
- **Configurable sample rate.** The render rate (44100 / 48000 / 88200 /
  96000 Hz) is now a preference instead of being hardcoded to 44100. foobar2000
  still resamples to the output device, so matching your device rate avoids an
  extra resample.

### Fixed

- **Trailing silence trimmed.** FluidSynth's player reports end-of-song up to
  ~2 s late, and the renderer used to wait for that and then add a fixed 2 s
  tail — appending up to ~4 s of dead air to every track. Playback now ends once
  the sound has actually decayed (past the parsed song length), so the rendered
  length matches the reported length far more closely while still preserving
  reverb/release tails.

### Notes

- Seeking was reviewed and confirmed correct: FluidSynth 2.x replays program,
  bank and controller state on seek (skipping only note-ons), so instruments are
  right after a jump. No change needed.

## [0.2.1] - 2026-07-01

### Fixed

- **Force-percussion now works with drum SoundFonts whose kit isn't at program 0.**
  It previously hardcoded bank 128 / program 0; SoundFonts that place their kit at
  another program (e.g. a TR-909 kit at program 24) rendered silence. The kit
  program is now discovered from the SoundFont (program 0 preferred, else the
  lowest present) and logged.

### Added

- **Descriptive console logging** to explain SoundFonts that produce no sound.
  When a SoundFont loads, the console (**View → Console**) now reports its preset
  count, which melodic banks it contains, and how many percussion kits (bank 128)
  it has. Two silent-mismatch cases are called out explicitly:
  - force-percussion is ON but the SoundFont has no percussion presets, and
  - the SoundFont is drum-only (bank 128) but force-percussion is off.
- A per-track warning when a track renders complete **silence**, hinting that the
  SoundFont likely lacks the instrument(s) the file requests (catches missing
  individual programs that the load-time summary can't).
- SoundFont load failures now log the offending path.

## [0.2.0] - 2026-07-01

### Changed

- **Much faster playback start.** The SoundFont is now loaded once and the
  FluidSynth engine is reused across tracks instead of being rebuilt per track.
  Previously every play reloaded the SoundFont (~4 s for a compressed `.sf3`);
  now only the first play after a SoundFont change pays that cost, and
  subsequent track starts are effectively instant (measured ~0 ms vs ~3.9 s).
- The engine is also **preloaded in the background at startup and whenever the
  SoundFont / percussion setting changes**, so even the first play is instant
  once the preload finishes.

### Technical

- New `FluidEngine` (a loaded synth keyed by soundfont/samplerate/percussion) and
  `FluidEngineCache` (process-wide, load-once, reuse-when-idle, async preload,
  temporary-engine fallback for gapless overlap). Between tracks the synth is
  reset (`fluid_synth_system_reset`) and drum routing re-applied.

## [0.1.0] - 2026-07-01

### Added

- Initial release.
- `input` decoder for Standard MIDI Files (`.mid` / `.midi` / `.kar` / `.smf`).
- SMF parser with tempo-map integration for accurate track length and seeking
  (PPQ and SMPTE divisions, running status, meta/sysex skipping).
- Offline rendering through FluidSynth and a user-selected SoundFont
  (interleaved-stereo float, pull model, ~2 s release/reverb tail).
- Preferences pane (**Input → MIDI Player**):
  - Choose the SoundFont (`.sf2` / `.sf3`) with a file picker; live
    found/not-found status.
  - **Force all channels to the drum kit** — audition drum-pattern MIDI whose
    notes sit on channel 1 instead of GM channel 10.
- Settings persist via `fb2k::configStore`.
- Standalone build tooling: `Scripts/bootstrap_sdk.sh` fetches and builds the
  foobar2000 SDK; `Scripts/build.sh` builds and installs the component.

### Known issues

- Reported track length (from the SMF tempo map) can be shorter than the audible
  render because of the reverb/release tail; the seekbar length may not exactly
  match playback length.
