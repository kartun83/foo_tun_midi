//
//  FluidSynthRenderer.cpp
//  foo_jl_midi_mac
//

#include "FluidSynthRenderer.h"
#include <fluidsynth.h>

namespace foo_midi {

FluidSynthRenderer::~FluidSynthRenderer() {
    teardown();
}

void FluidSynthRenderer::teardown() {
    // Player must be deleted before the synth.
    if (m_player) { delete_fluid_player(m_player); m_player = nullptr; }
    if (m_synth)  { delete_fluid_synth(m_synth);   m_synth = nullptr; }
    if (m_settings) { delete_fluid_settings(m_settings); m_settings = nullptr; }
    m_sfontId = -1;
}

bool FluidSynthRenderer::init(const char* soundfontPath,
                              const uint8_t* midiData, size_t midiSize,
                              int sampleRate, bool forcePercussion) {
    teardown();
    m_sampleRate = sampleRate > 0 ? sampleRate : 44100;
    m_finished = false;

    m_settings = new_fluid_settings();
    if (!m_settings) return false;
    fluid_settings_setnum(m_settings, "synth.sample-rate", (double)m_sampleRate);
    fluid_settings_setint(m_settings, "synth.midi-channels", 16);
    // No separate audio driver/timer: the player is advanced by our
    // fluid_synth_write_* calls (offline pull rendering).

    m_synth = new_fluid_synth(m_settings);
    if (!m_synth) { teardown(); return false; }

    m_sfontId = fluid_synth_sfload(m_synth, soundfontPath, 1 /* reset presets */);
    if (m_sfontId == FLUID_FAILED) { teardown(); return false; }

    if (forcePercussion) {
        // Pin every channel to the percussion bank (128) and load the standard
        // kit, then mark the channel as a drum channel so any program changes
        // the file might send stay within the drum bank.
        for (int ch = 0; ch < 16; ++ch) {
            fluid_synth_set_channel_type(m_synth, ch, CHANNEL_TYPE_DRUM);
            fluid_synth_bank_select(m_synth, ch, 128);
            fluid_synth_program_change(m_synth, ch, 0);
        }
    }

    m_midi.assign(midiData, midiData + midiSize);
    m_player = new_fluid_player(m_synth);
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
    if (m_finished || !m_synth || !m_player || frames <= 0) return 0;

    // Interleaved stereo: left at offset 0 stride 2, right at offset 1 stride 2.
    if (fluid_synth_write_float(m_synth, frames,
                                out, 0, kChannels,
                                out, 1, kChannels) != FLUID_OK) {
        m_finished = true;
        return 0;
    }

    if (fluid_player_get_status(m_player) == FLUID_PLAYER_DONE) {
        // Player has consumed all events; render the tail until voices die out.
        if (fluid_synth_get_active_voice_count(m_synth) == 0) {
            m_finished = true;
        } else {
            m_tailFramesRemaining -= frames;
            if (m_tailFramesRemaining <= 0) m_finished = true;
        }
    }
    return frames;
}

void FluidSynthRenderer::seek(uint32_t tick) {
    if (!m_synth || !m_player) return;
    fluid_player_seek(m_player, (int)tick);
    // Kill any notes left hanging across the jump.
    fluid_synth_all_sounds_off(m_synth, -1);
    m_finished = false;
    m_tailFramesRemaining = m_sampleRate * 2;
}

} // namespace foo_midi
