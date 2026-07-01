# Changelog

All notable changes to foo_tun_midi will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

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
