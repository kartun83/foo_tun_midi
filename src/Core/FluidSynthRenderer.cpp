//
//  FluidSynthRenderer.cpp
//  foo_tun_midi
//

#include "FluidSynthRenderer.h"
#include <fluidsynth.h>

namespace foo_midi {

FluidSynthRenderer::~FluidSynthRenderer() {
    teardown();
}

fluid_synth_t* FluidSynthRenderer::synth() const {
    return m_engine ? m_engine->synth() : nullptr;
}

void FluidSynthRenderer::teardown() {
    // Delete our player first, then hand the engine back to the cache so its
    // synth (with the SoundFont still loaded) can be reused by the next track.
    if (m_player) { delete_fluid_player(m_player); m_player = nullptr; }
    if (m_engine) {
        FluidEngineCache::instance().release(m_engine);
        m_engine.reset();
    }
    m_finished = false;
}

bool FluidSynthRenderer::init(const EngineKey& key,
                              const uint8_t* midiData, size_t midiSize) {
    teardown();
    m_sampleRate = key.sampleRate > 0 ? key.sampleRate : 44100;
    m_finished = false;

    m_engine = FluidEngineCache::instance().acquire(key);
    if (!m_engine) return false;   // SoundFont failed to load

    m_midi.assign(midiData, midiData + midiSize);
    m_player = new_fluid_player(m_engine->synth());
    if (!m_player) { teardown(); return false; }
    if (fluid_player_add_mem(m_player, m_midi.data(), m_midi.size()) != FLUID_OK) {
        teardown();
        return false;
    }
    if (fluid_player_play(m_player) != FLUID_OK) { teardown(); return false; }

    // ~2s release/reverb tail rendered after the player finishes.
    m_tailFramesRemaining = m_sampleRate * 2;
    return true;
}

int FluidSynthRenderer::render(float* out, int frames) {
    fluid_synth_t* syn = synth();
    if (m_finished || !syn || !m_player || frames <= 0) return 0;

    // Interleaved stereo: left at offset 0 stride 2, right at offset 1 stride 2.
    if (fluid_synth_write_float(syn, frames,
                                out, 0, kChannels,
                                out, 1, kChannels) != FLUID_OK) {
        m_finished = true;
        return 0;
    }

    if (fluid_player_get_status(m_player) == FLUID_PLAYER_DONE) {
        // Player has consumed all events; render the tail until voices die out.
        if (fluid_synth_get_active_voice_count(syn) == 0) {
            m_finished = true;
        } else {
            m_tailFramesRemaining -= frames;
            if (m_tailFramesRemaining <= 0) m_finished = true;
        }
    }
    return frames;
}

void FluidSynthRenderer::seek(uint32_t tick) {
    fluid_synth_t* syn = synth();
    if (!syn || !m_player) return;
    fluid_player_seek(m_player, (int)tick);
    // Kill any notes left hanging across the jump.
    fluid_synth_all_sounds_off(syn, -1);
    m_finished = false;
    m_tailFramesRemaining = m_sampleRate * 2;
}

} // namespace foo_midi
