# Architecture — foo_tun_midi

A minimal foobar2000 `input` decoder that turns Standard MIDI Files into audio
via FluidSynth. Two concerns are deliberately separated: **parsing** the SMF for
metadata/seeking, and **rendering** it to PCM.

## Components

| File | Responsibility |
|------|----------------|
| `src/Integration/MidiInput.cpp` | The `input` service. Slurps the file, parses it, drives the renderer, emits `audio_chunk`s. |
| `src/Integration/Main.cpp` | Component registration / about box. |
| `src/Core/SMFInfo.{h,cpp}` | SMF parser → tempo map, duration, tick↔seconds. |
| `src/Core/FluidSynthRenderer.{h,cpp}` | Per-track playback: hosts a `fluid_player` on a borrowed engine, pulls float blocks. |
| `src/Core/FluidEngine.{h,cpp}` | Loaded synth+SoundFont keyed by (soundfont, samplerate, percussion) + a process-wide cache (load-once, reuse, async preload). |
| `src/Core/MidiPreload.h` | Warms the engine cache from the current config (startup / on prefs change). |
| `src/Core/MidiConfig.h` | `fb2k::configStore` wrapper (SoundFont path, percussion flag). |
| `src/UI/MidiPreferences.{h,mm}` | Cocoa preferences pane (Input → MIDI Player). |
| `src/version.h` | Version single-source-of-truth (parsed by the project generator). |
| `src/branding.h` | About-box macro/attribution. |
| `src/PreferencesCommon.h` | Vendored Cocoa prefs helpers (from the JendaT suite, MIT). |

### Decoder pattern

`input_midi : input_stubs`, registered with
`input_singletrack_factory_t<input_midi>` — the lightweight single-track input
pattern from the SDK's `input_impl.h` (modeled on `foo_sample/input_raw.cpp`),
*not* the heavier `input_entry_v2`. One playable item per file, no subsongs.

### Rendering model

FluidSynth runs **without an audio driver**; the player is advanced purely by our
`fluid_synth_write_float()` calls (offline pull rendering). `decode_run` asks for
`kBlockFrames` (4096) interleaved-stereo frames each call and hands them to
`audio_chunk::set_data_32`. After the player reports `FLUID_PLAYER_DONE` we keep
rendering until the active voice count hits zero or a ~2 s tail cap elapses, so
reverb/long releases aren't cut off.

Seeking maps seconds → tick via the tempo map (`SMFInfo::secondsToTick`), calls
`fluid_player_seek`, then `fluid_synth_all_sounds_off` to kill notes hanging
across the jump.

### SoundFont caching (startup latency)

Loading a SoundFont dominates start-up cost: a compressed `.sf3` (OGG samples)
takes ~4 s to load because FluidSynth decompresses every sample; an uncompressed
`.sf2` of similar size loads in ~20 ms. So the synth is **not** rebuilt per
track. `FluidEngineCache` keeps one live `FluidEngine` keyed by
(soundfont, samplerate, percussion):

- `acquire(key)` returns the cached engine when it matches and is idle (waiting
  on an in-flight preload of the same key), else builds one. If the slot is busy
  with a *different* in-use engine — foobar can briefly open two decoders during
  a gapless transition — it returns an uncached temporary, since a FluidSynth
  synth can't host two players at once.
- `release()` marks the cached engine idle again; between tracks the synth is
  `fluid_synth_system_reset`'d and drum routing re-applied, so no state leaks.
- `preload(key)` builds the engine on a background thread. `MidiPreload.h` calls
  it at component startup (`initquit::on_init`) and whenever the SoundFont or
  percussion preference changes, so the first play is instant too (once the
  preload finishes). Missing files (unmounted volume) are skipped.

Net effect: first play after a SoundFont change pays the load once; every later
track start is ~0 ms.

## Percussion routing (important)

**Finding:** drum-pattern libraries commonly put GM-drum-map notes (36 kick,
38 snare, 42/44/46 hats) on **MIDI channel 1 with no program change** — they
assume a DAW drum rack supplies the mapping. Under General MIDI only channel 10
is percussion, so FluidSynth (correctly) renders channel 1 with its default
patch: Acoustic Grand Piano. The files *sound like piano*, and nothing is wrong.

**Handling:** the engine's percussion mode (`EngineKey::forcePercussion`). When
set, after loading the SoundFont `resetForNewTrack()` pins every channel to the
drum bank:

```cpp
for (int ch = 0; ch < 16; ++ch) {
    fluid_synth_set_channel_type(m_synth, ch, CHANNEL_TYPE_DRUM);
    fluid_synth_bank_select(m_synth, ch, 128);              // GM percussion bank
    fluid_synth_program_change(m_synth, ch, m_drumProgram); // a kit that exists
}
```

`m_drumProgram` is **discovered from the SoundFont at load time**, not assumed to
be 0: many drum SoundFonts place their only kit at another program number (e.g. a
TR-909 kit at program 24), which would otherwise select a non-existent preset and
render silence. Program 0 is used when present, else the lowest bank-128 program
(`-1`/no-op when the SoundFont has no percussion at all). `set_channel_type(DRUM)`
makes any later program change the file sends stay within bank 128. Exposed as the
"Force all channels to the drum kit" preference.

### Diagnostics

Because SoundFonts vary wildly in what they contain, the engine logs a summary to
the foobar2000 console (**View → Console**) on each load — preset count, melodic
banks, percussion kit count, and (when forcing drums) the chosen kit program — and
warns about the silent-mismatch cases (force-percussion with no drum kit;
drum-only SoundFont with force-percussion off). `FluidSynthRenderer` additionally
logs once if a whole track renders inaudibly, catching per-file missing programs
the load-time summary can't predict.

To inspect a file's channels/programs/notes when triaging "why does this sound
wrong", `Scripts/midinfo.py` (a small standalone SMF event dumper) prints the
channels used, program changes, and note numbers:

```bash
python3 Scripts/midinfo.py path/to/file.mid
```

## Build / linking notes (SDK & toolchain gotchas)

- **Standalone repo.** The foobar2000 SDK is third-party and not vendored;
  `Scripts/bootstrap_sdk.sh` fetches `SDK-2025-03-07` from foobar2000.org into the
  repo root (override with `FB2K_SDK_PATH`) and builds its five static libs.
  `Scripts/generate_xcode_project.rb` reads the version from `src/version.h` and
  resolves the SDK path itself — there is no monorepo dependency.
- **arm64-only.** Homebrew's `fluid-synth` keg is arm64-only, and foobar2000
  runs arm64 on Apple Silicon. `ARCHS = arm64` is set in the generated project.
- FluidSynth is linked via `Scripts/generate_xcode_project.rb`: `-lfluidsynth`
  plus header/lib search paths from `brew --prefix fluid-synth` (env override
  `FLUIDSYNTH_PREFIX`). The dylib's install-name is absolute, so it resolves at
  runtime without bundling.
- **Adding source files requires `--regenerate`** — the Xcode project is
  generated by globbing `src/{Core,UI,Integration}/*.{cpp,h,mm}`. Cocoa is
  already linked, so `.mm` files build without extra project edits.
- **Forward-declaring FluidSynth types:** `fluid_settings_t`'s real tag is
  `struct _fluid_hashtable_t` (see `fluidsynth/types.h`) — declaring it as
  `_fluid_settings_t` causes a typedef-redefinition clash with the real header.
- **This SDK's `exception_io_data` has no `const char*` constructor.** To surface
  a message, `console::error(str.c_str())` then `throw exception_io_data();`.
  There is no `console::errorf`.
- **Config persistence:** use `fb2k::configStore` (as `MidiConfig.h` does), *not*
  `cfg_var` — `cfg_var` does not persist reliably on foobar2000 macOS v2.

## Known issues / future work

- Reported length (from the tempo map) can be shorter than the audible render
  because of the release tail; the seekbar length may not exactly match playback
  length. Worth reconciling player end-of-track vs. our `totalTicks`.
- Phase 2 ideas: auto-detect "looks like a drum file" (single channel, all-
  percussion note range) to route only that file; optional CLAP plugin hosting as
  an alternate renderer.
