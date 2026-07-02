//
//  RendererFactory.cpp
//  foo_tun_midi
//

#include "RendererFactory.h"
#include "FluidSynthRenderer.h"
#include "SMFInfo.h"
#include "MidiConfig.h"
#include "../fb2k_sdk.h"   // console::*

#include <string>

#if FOO_TUN_MIDI_CLAP
#include "../Clap/ClapRenderer.h"
#endif

namespace foo_midi {

namespace {

// Build + init the FluidSynth backend from the SoundFont/percussion/rate prefs.
std::unique_ptr<IMidiRenderer> makeFluidSynth(const uint8_t* midiData,
                                              size_t midiSize,
                                              const SMFInfo& smf) {
    EngineKey key;
    key.soundfont = midi_config::soundFontPath();
    key.sampleRate = midi_config::sampleRate();

    // Effective percussion decision: Always forces every file; Auto forces only
    // files that look like drum patterns misrouted off channel 10.
    int mode = midi_config::percussionMode();
    key.forcePercussion = (mode == midi_config::kPercAlways) ||
        (mode == midi_config::kPercAuto && smf.looksLikePercussionMisrouted());

    auto r = std::make_unique<FluidSynthRenderer>();
    if (!r->init(key, midiData, midiSize, smf)) {
        std::string msg =
            "foo_tun_midi: failed to initialize FluidSynth. Check that the "
            "SoundFont exists and is a valid .sf2/.sf3 (set it in "
            "Preferences > Input > MIDI Player): ";
        msg += key.soundfont;
        console::error(msg.c_str());
        return nullptr;
    }
    return r;
}

} // namespace

std::unique_ptr<IMidiRenderer> createRenderer(const uint8_t* midiData,
                                              size_t midiSize,
                                              const SMFInfo& smf) {
#if FOO_TUN_MIDI_CLAP
    if (midi_config::engine() == midi_config::kEngineClap) {
        std::string plugin = midi_config::clapPluginPath();
        if (plugin.empty()) {
            console::error("foo_tun_midi: CLAP engine selected but no plugin is "
                           "configured (Preferences > Input > MIDI Player). "
                           "Falling back to FluidSynth.");
        } else {
            auto r = std::make_unique<ClapRenderer>();
            midi_config::ClapPresetRef preset = midi_config::clapPreset();
            if (preset.valid)
                r->setPreset(preset.locationKind, preset.location, preset.loadKey);
            if (r->init(plugin, midi_config::clapPluginId(),
                        midi_config::sampleRate(), smf)) return r;
            std::string msg = "foo_tun_midi: failed to load CLAP plugin, falling "
                              "back to FluidSynth: ";
            msg += plugin;
            console::error(msg.c_str());
        }
    }
#endif
    return makeFluidSynth(midiData, midiSize, smf);
}

} // namespace foo_midi
