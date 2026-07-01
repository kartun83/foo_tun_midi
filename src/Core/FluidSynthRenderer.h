//
//  FluidSynthRenderer.h
//  foo_tun_midi
//
//  Per-track playback over a (usually cached) FluidEngine. Owns only the
//  fluid_player and the in-memory MIDI; the synth + loaded SoundFont are
//  borrowed from FluidEngineCache so repeated track starts don't reload the
//  SoundFont. Pull model: create, then repeatedly render() interleaved-stereo
//  float blocks until it returns 0 (end of stream + tail).
//

#pragma once

#include "IMidiRenderer.h"
#include "FluidEngine.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace foo_midi {

struct SMFInfo;

class FluidSynthRenderer : public IMidiRenderer {
public:
    FluidSynthRenderer() = default;
    ~FluidSynthRenderer() override;

    FluidSynthRenderer(const FluidSynthRenderer&) = delete;
    FluidSynthRenderer& operator=(const FluidSynthRenderer&) = delete;

    // Acquire an engine for `key` (from the cache when possible) and start the
    // player on the in-memory MIDI. Returns false if the SoundFont can't load
    // or the MIDI is rejected. `midiData` is copied internally. `smf` supplies
    // the parsed song length (to bound the release tail) and the tempo map
    // (to convert seek positions in seconds to ticks); it must outlive this.
    bool init(const EngineKey& key, const uint8_t* midiData, size_t midiSize,
              const SMFInfo& smf);

    int sampleRate() const override { return m_sampleRate; }
    bool supportsSeek() const override { return true; }

    // Render up to `frames` stereo frames into `out` (must hold 2*frames floats).
    // Returns frames produced; 0 once the song and its (trimmed) tail are done.
    int render(float* out, int frames) override;

    // Seek to an absolute position in seconds (mapped to a tick via the tempo map).
    void seek(double seconds) override;

private:
    void teardown();
    fluid_synth_t* synth() const;
    void warnIfSilent();   // log a hint once if the whole track was inaudible

    std::shared_ptr<FluidEngine> m_engine;  // borrowed from the cache
    fluid_player_t* m_player = nullptr;
    const SMFInfo* m_smf = nullptr;         // for seek (tempo map); not owned
    int m_sampleRate = 44100;

    std::vector<uint8_t> m_midi;   // kept alive for the player's lifetime
    bool m_finished = false;

    // Tail handling. Past the nominal song end (or once the player reports done)
    // we keep rendering only while audio is still ringing, then stop after a
    // short run of silence — FluidSynth's player reports DONE ~2 s late, so
    // waiting for it would append seconds of dead air to every track.
    long long m_renderedFrames = 0;
    long long m_nominalFrames = 0; // parsed song length in frames (0 = unknown)
    int m_silenceRun = 0;          // consecutive silent frames past the end

    // Silence detection: if a whole track renders inaudibly we log a hint once,
    // to explain SoundFonts that "produce no sound" (e.g. a missing program).
    float m_peak = 0.0f;
    bool m_warnedSilent = false;
};

} // namespace foo_midi
