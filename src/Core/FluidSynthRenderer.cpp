//
//  FluidSynthRenderer.cpp
//  foo_tun_midi
//

#include "FluidSynthRenderer.h"
#include "../fb2k_sdk.h"   // console::*
#include <fluidsynth.h>

#include <cmath>
#include <string>

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
    m_peak = 0.0f;
    m_warnedSilent = false;
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

    // Track the loudest sample seen so we can flag a track that renders silence.
    for (int i = 0, n = frames * kChannels; i < n; ++i) {
        float a = std::fabs(out[i]);
        if (a > m_peak) m_peak = a;
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

    if (m_finished) warnIfSilent();
    return frames;
}

void FluidSynthRenderer::warnIfSilent() {
    if (m_warnedSilent) return;
    m_warnedSilent = true;
    if (m_peak >= 1.0e-4f) return;   // clearly audible; nothing to report

    std::string sf = m_engine ? m_engine->key().soundfont : std::string();
    if (auto slash = sf.find_last_of('/'); slash != std::string::npos)
        sf = sf.substr(slash + 1);

    std::string msg =
        "foo_tun_midi: this track rendered SILENCE with SoundFont '" + sf +
        "'. Likely the SoundFont lacks the instrument(s) the file asks for. "
        "For drum-pattern files, enable 'Force all channels to the drum kit' in "
        "Preferences > Input > MIDI Player; otherwise try a different SoundFont.";
    console::error(msg.c_str());
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
