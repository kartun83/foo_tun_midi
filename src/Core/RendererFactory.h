//
//  RendererFactory.h
//  foo_tun_midi
//
//  Builds the configured IMidiRenderer for a parsed MIDI file, keeping the
//  decoder (MidiInput) backend-agnostic. The FluidSynth path is always present;
//  the CLAP path is compiled in only for the CLAP-enabled ("Full") build
//  (FOO_TUN_MIDI_CLAP) and selected via the engine preference.
//

#pragma once

#include "IMidiRenderer.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace foo_midi {

struct SMFInfo;

// Create + initialize the renderer selected by the current preferences.
// `midiData`/`midiSize` is the in-memory SMF; `smf` is its parsed form (tempo
// map, length, and — in the CLAP build — the retained event list). Returns
// nullptr if the selected backend fails to initialize (bad SoundFont, missing
// plugin, etc.); the caller logs and rejects the file.
std::unique_ptr<IMidiRenderer> createRenderer(const uint8_t* midiData,
                                              size_t midiSize,
                                              const SMFInfo& smf);

} // namespace foo_midi
