//
//  MidiPreload.h
//  foo_tun_midi
//
//  Helper to warm the FluidEngineCache with the currently-configured SoundFont
//  so the first playback doesn't pay the (multi-second, for .sf3) load. Called
//  at component startup and whenever the relevant preferences change.
//

#pragma once

#include "FluidEngine.h"
#include "MidiConfig.h"

#include <fstream>

namespace foo_midi {

inline EngineKey currentEngineKey() {
    EngineKey key;
    key.soundfont = midi_config::soundFontPath();
    key.sampleRate = 44100;
    key.forcePercussion = midi_config::forcePercussion();
    return key;
}

inline void preloadCurrentSoundFont() {
    EngineKey key = currentEngineKey();
    if (key.soundfont.empty()) return;
    // Skip if the file isn't reachable (e.g. an unmounted external volume) so we
    // don't spawn a doomed load or spam errors.
    std::ifstream probe(key.soundfont, std::ios::binary);
    if (!probe.good()) return;
    FluidEngineCache::instance().preload(key);
}

} // namespace foo_midi
