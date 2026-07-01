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
                              const uint8_t* midiData, size_t midiSize,
                              double nominalSeconds) {
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

    m_renderedFrames = 0;
    m_nominalFrames = nominalSeconds > 0 ? (long long)(nominalSeconds * m_sampleRate) : 0;
    m_silenceRun = 0;
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

    // Loudest sample in this block: feeds both the silence trim and the
    // whole-track silence warning.
    float blockPeak = 0.0f;
    for (int i = 0, n = frames * kChannels; i < n; ++i) {
        float a = std::fabs(out[i]);
        if (a > blockPeak) blockPeak = a;
    }
    if (blockPeak > m_peak) m_peak = blockPeak;
    m_renderedFrames += frames;

    // Once we're past the parsed song end (or the player has actually consumed
    // all events), stop as soon as the sound has died out. FluidSynth reports
    // DONE up to ~2 s late, so we don't wait for it: a short run of silence, or
    // no active voices, ends the stream and trims the trailing dead air.
    static constexpr float kSilence = 1.0e-4f;
    bool pastEnd = (m_nominalFrames > 0 && m_renderedFrames >= m_nominalFrames) ||
                   fluid_player_get_status(m_player) == FLUID_PLAYER_DONE;
    if (pastEnd) {
        const long long endSilence = m_sampleRate / 4;   // 0.25 s of quiet
        const long long maxTail    = (long long)m_sampleRate * 4; // hard cap
        m_silenceRun = blockPeak < kSilence ? m_silenceRun + frames : 0;
        long long overrun = m_nominalFrames > 0 ? m_renderedFrames - m_nominalFrames : 0;
        if (fluid_synth_get_active_voice_count(syn) == 0 ||
            m_silenceRun >= endSilence ||
            overrun >= maxTail) {
            m_finished = true;
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

void FluidSynthRenderer::seek(uint32_t tick, double seconds) {
    fluid_synth_t* syn = synth();
    if (!syn || !m_player) return;
    fluid_player_seek(m_player, (int)tick);
    // Kill any notes left hanging across the jump.
    fluid_synth_all_sounds_off(syn, -1);
    m_finished = false;
    // Keep the tail bookkeeping tied to the new song position so the end-of-song
    // trim still triggers at the right place after a seek.
    m_renderedFrames = seconds > 0 ? (long long)(seconds * m_sampleRate) : 0;
    m_silenceRun = 0;
}

} // namespace foo_midi
