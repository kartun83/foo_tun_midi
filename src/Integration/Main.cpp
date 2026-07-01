//
//  Main.cpp
//  foo_tun_midi
//
//  Component registration.
//

#include "../fb2k_sdk.h"
#include "../branding.h"
#include "../version.h"

TUN_COMPONENT_ABOUT(
    "MIDI Player",
    MIDI_VERSION,
    "Plays Standard MIDI Files (.mid/.midi/.kar/.smf) by rendering them\n"
    "through FluidSynth and a SoundFont. Intended for quickly auditioning\n"
    "a MIDI library.\n\n"
    "Requires FluidSynth installed via Homebrew (brew install fluid-synth)."
);

VALIDATE_COMPONENT_FILENAME("foo_tun_midi.component");
