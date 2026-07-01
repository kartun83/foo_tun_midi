# Changelog

All notable changes to foo_tun_midi will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

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
