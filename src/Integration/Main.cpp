//
//  Main.cpp
//  foo_tun_midi
//
//  Component registration.
//

#include "../fb2k_sdk.h"
#include "../branding.h"
#include "../version.h"
#include "../Core/FluidEngine.h"
#include "../Core/MidiPreload.h"

TUN_COMPONENT_ABOUT(
    "MIDI Player",
    MIDI_VERSION,
    "Plays Standard MIDI Files (.mid/.midi/.kar/.smf) by rendering them\n"
    "through FluidSynth and a SoundFont. Intended for quickly auditioning\n"
    "a MIDI library.\n\n"
    "Requires FluidSynth installed via Homebrew (brew install fluid-synth)."
);

VALIDATE_COMPONENT_FILENAME("foo_tun_midi.component");

namespace {

// Warm the SoundFont cache at startup so the first track plays without the
// multi-second .sf3 load, and drop it cleanly on shutdown.
class midi_initquit : public initquit {
public:
    void on_init() override { foo_midi::preloadCurrentSoundFont(); }
    void on_quit() override { foo_midi::FluidEngineCache::instance().clear(); }
};

static initquit_factory_t<midi_initquit> g_midi_initquit;

} // namespace
