# Architecture — foo_tun_midi

A minimal foobar2000 `input` decoder that turns Standard MIDI Files into audio
via FluidSynth. Two concerns are deliberately separated: **parsing** the SMF for
metadata/seeking, and **rendering** it to PCM.

## Components

| File | Responsibility |
|------|----------------|
| `src/Integration/MidiInput.cpp` | The `input` service. Slurps the file, parses it, drives a renderer *through the interface*, emits `audio_chunk`s. Backend-agnostic. |
| `src/Integration/Main.cpp` | Component registration / about box. |
| `src/Core/SMFInfo.{h,cpp}` | SMF parser → tempo map, duration, tick↔seconds. Optionally retains the flattened channel-voice event stream (for the CLAP backend). |
| `src/Core/IMidiRenderer.h` | The rendering interface: `sampleRate` / `supportsSeek` / `seek` / `render` (interleaved-stereo pull model). |
| `src/Core/RendererFactory.{h,cpp}` | Builds the configured backend from preferences (FluidSynth always; CLAP under `FOO_TUN_MIDI_CLAP`). Keeps `MidiInput` backend-agnostic. |
| `src/Core/FluidSynthRenderer.{h,cpp}` | `IMidiRenderer` #1: hosts a `fluid_player` on a borrowed engine, pulls float blocks. Seekable. |
| `src/Core/FluidEngine.{h,cpp}` | Loaded synth+SoundFont keyed by (soundfont, samplerate, percussion) + a process-wide cache (load-once, reuse, async preload). |
| `src/Clap/ClapRenderer.{h,cpp}` | `IMidiRenderer` #2 (Full build only): hosts a CLAP instrument plugin, feeds `SMFInfo`'s events at sample offsets, pulls its main output. Non-seekable. |
| `src/Clap/ClapScanner.{h,cpp}` | Enumerates installed CLAP *instruments* (descriptor-only, cached/persisted) for the preferences dropdown. Full build only. |
| `src/Core/MidiPreload.h` | Warms the FluidSynth engine cache from the current config (startup / on prefs change). |
| `src/Core/MidiConfig.h` | `fb2k::configStore` wrapper (SoundFont path, percussion mode, sample rate, engine, CLAP plugin path). |
| `src/UI/MidiPreferences.{h,mm}` | Cocoa preferences pane (Input → MIDI Player). The engine picker + CLAP path controls appear in the Full build only. |
| `third_party/clap-headers/` | Vendored header-only CLAP SDK (MIT). Used by the Full build only. |
| `src/version.h` | Version single-source-of-truth (parsed by the project generator). |
| `src/branding.h` | About-box macro/attribution. |
| `src/PreferencesCommon.h` | Vendored Cocoa prefs helpers (from the JendaT suite, MIT). |

### Decoder pattern

`input_midi : input_stubs`, registered with
`input_singletrack_factory_t<input_midi>` — the lightweight single-track input
pattern from the SDK's `input_impl.h` (modeled on `foo_sample/input_raw.cpp`),
*not* the heavier `input_entry_v2`. One playable item per file, no subsongs.

### Rendering backends (the `IMidiRenderer` interface)

Rendering is split behind `IMidiRenderer` — a minimal pull interface
(`sampleRate` / `supportsSeek` / `seek` / `render`, all interleaved stereo).
`MidiInput` never names a concrete backend: `RendererFactory::createRenderer`
reads the preferences and returns the right one, and `decode_can_seek` just
reflects `supportsSeek()`. Concepts specific to one backend (percussion routing
and the engine cache for FluidSynth; the plugin path for CLAP) stay inside that
backend, not in the interface. Seeking is a declared *capability*, not a method
every backend must meaningfully implement.

Two backends implement it:

- **`FluidSynthRenderer`** — always present. Seekable. Everything in the
  "Rendering model", "SoundFont caching", and "Percussion routing" sections
  below is FluidSynth-specific.
- **`ClapRenderer`** — compiled into the **Full** build only
  (`FOO_TUN_MIDI_CLAP`; see "Build variants"). Hosts a single CLAP instrument
  plugin and feeds it the SMF's events for single-plugin pattern preview. It is
  **non-seekable** (`supportsSeek()` returns false, so foobar2000 disables the
  seekbar) and does **no** percussion routing — a hosted plugin owns its own
  note→sound mapping, so there is no GM channel-10 ambiguity to resolve. See
  "The CLAP backend" below.

### Rendering model (FluidSynth)

FluidSynth runs **without an audio driver**; the player is advanced purely by our
`fluid_synth_write_float()` calls (offline pull rendering). `decode_run` asks for
`kBlockFrames` (4096) interleaved-stereo frames each call and hands them to
`audio_chunk::set_data_32` at the configured render rate. That rate
(`midi_config::sampleRate()`, default 44100; also 48000/88200/96000) is baked
into the `EngineKey`, so FluidSynth renders natively at it and the decoder
declares it via `get_info` — foobar2000's output chain resamples to the device
if they differ. There's no per-input "device rate" callback in this SDK; a
decoder simply declares whatever rate it produces and the pipeline adapts.

**End-of-song / tail.** FluidSynth's player reports `FLUID_PLAYER_DONE` up to ~2 s
*after* the last event (measured), so waiting for it and then adding a fixed tail
appended several seconds of dead air to every track. Instead, once we pass the
parsed song length (`SMFInfo::durationSeconds`, converted to frames and handed to
the renderer) *or* the player reports done, we keep rendering only while sound is
still present and stop after ~0.25 s of continuous silence (peak `< 1e-4`), when
the active voice count reaches zero, or at a 4 s hard cap. This trims trailing
silence while preserving reverb/release decay. The parsed length itself is
accurate — it matches FluidSynth's own `fluid_player_get_total_ticks`.

Seeking maps seconds → tick via the tempo map (`SMFInfo::secondsToTick`), calls
`fluid_player_seek`, then `fluid_synth_all_sounds_off` to kill notes hanging
across the jump. FluidSynth 2.x replays program/bank/controller events up to the
seek point (skipping note-ons), so instrument state is correct after a jump — no
manual reconstruction needed.

### Metadata

`SMFInfo` also collects, in the same single pass, the sequence name (→ `title`),
initial tempo (BPM), time signature, key signature, instrument names, and
copyright, surfaced through `get_info`. Meta text is normalised to valid UTF-8
(well-formed UTF-8 passes through; stray high bytes are treated as Latin-1) so
foobar's tag API accepts old files.

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
makes any later program change the file sends stay within bank 128.

**When to force** is a three-way preference (`midi_config::PercussionMode`):
*Off* / *Auto* / *Always*. The decision is made per file in `decode_initialize`
and baked into the `EngineKey` (so forced and non-forced share of the same
SoundFont are distinct cache entries). *Auto* consults
`SMFInfo::looksLikePercussionMisrouted()` — true when the file has notes but none
on channel 10, no program change anywhere, and ≥80 % of notes fall in the GM drum
range (35–81). That catches DAW drum-rack exports (drums on channel 1) while
leaving real General MIDI and files that already use channel 10 alone. General
MIDI / GS / XG bank-select and SysEx resets are handled by FluidSynth's player
during normal playback, so no separate SysEx parser is needed; *Auto* covers the
one case FluidSynth can't infer (drums on the wrong channel).

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

## The CLAP backend (Full build)

`ClapRenderer` (in `src/Clap/`) is the offline CLAP host proven by a standalone
spike, adapted to the `IMidiRenderer` pull model. It hosts **one** CLAP
instrument plugin for previewing a pattern — not a multi-timbral GM arrangement.

- **Event feed.** CLAP has no `fluid_player` equivalent, so the renderer walks
  the SMF's own events. `SMFInfo` gained opt-in capture: `parseSMF(data, size,
  keepEvents=true)` retains the flattened channel-voice stream
  (`SMFInfo::events`, stable-sorted by absolute tick). `MidiInput` requests this
  only in the Full build. `ClapRenderer::init` maps each event's tick to an
  output frame via the tempo map; `render()` emits the events landing in each
  block at their sample offset.
- **Note dialect.** The renderer queries the plugin's note port
  (`supported_dialects`) and sends `clap_event_midi` unless the plugin supports
  only the CLAP note dialect — both spike targets (Baby Audio Tekno, OsTIrus)
  are MIDI-only, and since `SMFInfo` already yields raw MIDI, that is also the
  simpler path. Channel merging is implicit "merge all" (every channel's events
  go to the one plugin).
- **Host contract (load-bearing).** The host allocates a real, zeroed buffer for
  **every declared audio port, inputs included** — JUCE-wrapped CLAP plugins
  build one unified `juce::AudioBuffer` over in+out buses and crash on a null
  input channel (OsTIrus has a stereo input bus; passing none faulted in
  `_platform_memset`). Output is captured from port 0. The host advertises no
  extensions and runs single-threaded (offline).
- **Tail.** Same trim as FluidSynth: past the parsed length (once all events are
  sent) it stops after ~0.25 s of silence or a 4 s cap, preserving release/verb.

- **Plugin discovery.** `ClapScanner` enumerates installed CLAP *instruments*
  from the standard macOS locations (`~/Library/Audio/Plug-Ins/CLAP`,
  `/Library/Audio/Plug-Ins/CLAP`, plus `CLAP_PATH`), recursing into vendor
  subdirs. It reads each bundle's **descriptor only** (name / id / `features`) —
  never instantiating — so scanning is cheap and can't boot or crash a
  heavyweight plugin. Only descriptors carrying the `instrument` feature are
  kept; the list is sorted, cached in memory, and persisted via `MidiConfig`
  (`kKeyClapPluginList`) so the preferences dropdown is instant on later
  launches. A "Rescan" button forces a re-walk.

The engine picker and a **plugin dropdown** (populated from `ClapScanner`) live
in preferences (Full build); the selection is stored via `MidiConfig`
(`kKeyEngine`, and `kKeyClapPluginPath` + `kKeyClapPluginId` — a bundle can host
several plugins, so both the path and the plugin id are recorded).

### Limitation: JIT-compiling plugins crash the host

foobar2000's app is codesigned with the hardened runtime and **does not carry
`com.apple.security.cs.allow-jit`** (its only entitlements are
`disable-library-validation`, `device.audio-input`, `get-task-allow`). A CLAP
plugin that JIT-compiles at runtime therefore traps in
`pthread_jit_write_protect_np` (EXC_BREAKPOINT) the moment it tries to make JIT
memory executable — and, because that fault is on the plugin's own thread, it
takes the whole foobar2000 process down. We cannot catch it from in-process, and
a plugin's JIT use isn't visible in its descriptor, so `ClapScanner` cannot
pre-filter these.

Known affected: the **"The Usual Suspects" DSP56300 emulations** — `NodalRed2x`,
`OsTIrus`, `Osirus`, `Vavra` (`com.theusualsuspects.*`), which JIT-emit a
Motorola DSP core via asmjit. (Ironically `OsTIrus` was a spike target and
worked *there* — the standalone spike binary runs without a hardened runtime.)
Non-JIT plugins are unaffected: Baby Audio Tekno, Vital, the u-he set (Diva,
Hive, Zebra3…), etc. play fine.

Optional user-side workaround (not done by this project — it modifies the user's
foobar2000 install and replaces its Apple signature with an ad-hoc one):

```bash
# quit foobar2000 first
cat > /tmp/fb2k-jit.entitlements <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>com.apple.security.cs.allow-jit</key><true/>
  <key>com.apple.security.cs.allow-unsigned-executable-memory</key><true/>
  <key>com.apple.security.cs.disable-library-validation</key><true/>
  <key>com.apple.security.device.audio-input</key><true/>
  <key>com.apple.security.get-task-allow</key><true/>
</dict></plist>
PLIST
codesign --sign - --force --options runtime \
  --entitlements /tmp/fb2k-jit.entitlements /Applications/foobar2000.app
```

Reversible by reinstalling foobar2000.

## Build variants (Lite / Full)

Two components are produced from one source tree, selected by the `VARIANT` env
var in `Scripts/generate_xcode_project.rb` and driven by
`Scripts/build.sh --variant lite|full|both`:

| Variant | Product | Bundle id | CLAP |
|---------|---------|-----------|------|
| `lite` (default) | `foo_tun_midi` | `com.foobar2000.foo-tun-midi` | FluidSynth only |
| `full` | `foo_tun_midi_clap` | `com.foobar2000.foo-tun-midi-clap` | + hosted CLAP instrument |

The Full variant adds the `src/Clap` group to the build, defines
`FOO_TUN_MIDI_CLAP=1`, and puts `third_party/clap-headers` on the header search
path. The Lite variant **excludes** `src/Clap` from the build entirely (not
merely `#ifdef`'d out) — its binary has no `ClapRenderer` symbols and does not
link `dlopen`, so it structurally cannot host a plugin. The variants get distinct
product names + bundle identifiers; both register an `input` for the same
extensions, so a user installs **one or the other, not both** (`build.sh
--install` refuses `--variant both` for this reason). `build.sh --package` zips
each `.component` into a distributable `.fb2k-component`.

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

- The reported (seekbar) length is the parsed song length; audible reverb/release
  can ring slightly past it. This is expected and now small — the renderer trims
  the trailing silence rather than appending a fixed tail.
- CLAP hosting exists as an alternate renderer in the **Full** build
  (`ClapRenderer`), but is early: no plugin GUI/editor window yet (headless
  offline render only), no preset/state load (the plugin's default patch is what
  plays), and in-app playback hasn't been exercised across a wide range of
  plugins. Non-seekable by design.
- Future ideas: CLAP plugin GUI hosting + preset/state selection; VST3
  hosting; more metadata (lyrics/`.kar`); configurable reverb/chorus levels.
