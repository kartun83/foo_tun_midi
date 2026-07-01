//
//  FluidSynthRenderer.h
//  foo_jl_midi_mac
//
//  Thin wrapper around FluidSynth for offline block rendering of an in-memory
//  SMF through a SoundFont. Pull model: create, then repeatedly render()
//  interleaved-stereo float blocks until it returns 0 (end of stream + tail).
//

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

// Match FluidSynth's own tags (see fluidsynth/types.h) so these forward
// declarations don't conflict with the real header.
typedef struct _fluid_hashtable_t fluid_settings_t;
typedef struct _fluid_synth_t fluid_synth_t;
typedef struct _fluid_player_t fluid_player_t;

namespace foo_midi {

class FluidSynthRenderer {
public:
    FluidSynthRenderer() = default;
    ~FluidSynthRenderer();

    FluidSynthRenderer(const FluidSynthRenderer&) = delete;
    FluidSynthRenderer& operator=(const FluidSynthRenderer&) = delete;

    // Load soundfont + MIDI and start the player. Returns false on any failure
    // (missing/invalid soundfont, bad MIDI). `midiData` is copied internally.
    //
    // forcePercussion: route ALL 16 channels to the drum bank. Drum-pattern
    // libraries often put GM-drum-map notes on channel 1 with no program change
    // (they expect a DAW drum rack), so they'd otherwise render as piano.
    bool init(const char* soundfontPath,
              const uint8_t* midiData, size_t midiSize,
              int sampleRate,
              bool forcePercussion = false);

    int sampleRate() const { return m_sampleRate; }
    static constexpr int kChannels = 2;

    // Render up to `frames` stereo frames into `out` (must hold 2*frames floats).
    // Returns frames produced; 0 once the song and its release tail are done.
    int render(float* out, int frames);

    // Seek to an absolute tick (from SMFInfo::secondsToTick).
    void seek(uint32_t tick);

private:
    void teardown();

    fluid_settings_t* m_settings = nullptr;
    fluid_synth_t* m_synth = nullptr;
    fluid_player_t* m_player = nullptr;
    int m_sfontId = -1;
    int m_sampleRate = 44100;

    std::vector<uint8_t> m_midi;   // kept alive for the player's lifetime
    bool m_finished = false;
    int m_tailFramesRemaining = 0; // release/reverb tail after player reports done
};

} // namespace foo_midi
