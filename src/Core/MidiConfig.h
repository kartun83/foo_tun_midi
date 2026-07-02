//
//  MidiConfig.h
//  foo_jl_midi_mac
//
//  Persistent configuration via fb2k::configStore. On macOS v2, cfg_var does
//  NOT persist reliably — configStore is the supported API (see the wave_seekbar
//  component's notes). Shared between the decoder (MidiInput.cpp) and the
//  preferences pane (MidiPreferences.mm).
//

#pragma once

#include "../fb2k_sdk.h"
#include <cstdlib>
#include <string>

namespace midi_config {

static const char* const kConfigPrefix = "foo_jl_midi.";

// Config keys.
static const char* const kKeySoundFontPath = "soundfont_path";
static const char* const kKeyForcePercussion = "force_percussion";   // legacy bool
static const char* const kKeyPercussionMode = "percussion_mode";     // 0/1/2 below
static const char* const kKeySampleRate = "sample_rate";             // render Hz
static const char* const kKeyEngine = "engine";                      // 0/1 below
static const char* const kKeyClapPluginPath = "clap_plugin_path";    // .clap bundle
static const char* const kKeyClapPluginId = "clap_plugin_id";        // plugin within it
static const char* const kKeyClapPluginList = "clap_plugin_list";    // cached scan
static const char* const kKeyClapPreset = "clap_preset";             // "kind\tloc\tkey"
static const char* const kKeyClapPresetName = "clap_preset_name";    // display label

// Rendering backend. Only the CLAP-enabled ("Full") build exposes a choice;
// the FluidSynth-only build ignores this and always renders with FluidSynth.
enum Engine {
    kEngineFluidSynth = 0,
    kEngineClap = 1,
};

// Percussion handling mode.
enum PercussionMode {
    kPercOff = 0,     // never force: honour General MIDI (drums only on channel 10)
    kPercAuto = 1,    // force only when a file looks like misrouted drums
    kPercAlways = 2,  // always force every channel to the drum kit
};

// Fallback used when the user hasn't chosen a SoundFont yet. Full General MIDI
// bank (incl. the drum kit on channel 10), lives on the external drive.
static const char* const kDefaultSoundFont =
    "/Volumes/external_wd/Download/midi/sf2/MuseScore_General.sf3";

inline std::string getConfigString(const char* key, const char* defaultVal) {
    try {
        auto store = fb2k::configStore::get();
        pfc::string8 fullKey;
        fullKey << kConfigPrefix << key;
        fb2k::stringRef val = store->getConfigString(fullKey.c_str(), defaultVal);
        return val->c_str();
    } catch (...) {
        return defaultVal ? defaultVal : "";
    }
}

inline void setConfigString(const char* key, const char* value) {
    try {
        auto store = fb2k::configStore::get();
        pfc::string8 fullKey;
        fullKey << kConfigPrefix << key;
        store->setConfigString(fullKey.c_str(), value);
    } catch (...) {
        // best effort; nothing actionable if config store is unavailable
    }
}

inline int64_t getConfigInt(const char* key, int64_t defaultVal) {
    try {
        auto store = fb2k::configStore::get();
        pfc::string8 fullKey;
        fullKey << kConfigPrefix << key;
        return store->getConfigInt(fullKey.c_str(), defaultVal);
    } catch (...) {
        return defaultVal;
    }
}

inline void setConfigInt(const char* key, int64_t value) {
    try {
        auto store = fb2k::configStore::get();
        pfc::string8 fullKey;
        fullKey << kConfigPrefix << key;
        store->setConfigInt(fullKey.c_str(), value);
    } catch (...) {
        // best effort
    }
}

// Percussion mode, migrating the legacy boolean: an existing "force on" user
// maps to Always, everyone else defaults to Auto.
inline int percussionMode() {
    int64_t m = getConfigInt(kKeyPercussionMode, -1);
    if (m < 0) return getConfigInt(kKeyForcePercussion, 0) != 0 ? kPercAlways : kPercAuto;
    if (m < kPercOff || m > kPercAlways) return kPercAuto;
    return (int)m;
}

// Render sample rate (Hz). FluidSynth renders at this rate and the decoder
// declares it to foobar; the output chain resamples to the device if needed.
// Defaults to 44100; only a handful of standard rates are offered/accepted.
static const int kDefaultSampleRate = 44100;

inline int sampleRate() {
    int64_t sr = getConfigInt(kKeySampleRate, kDefaultSampleRate);
    switch (sr) {
        case 44100: case 48000: case 88200: case 96000:
            return (int)sr;
        default:
            return kDefaultSampleRate;
    }
}

// Resolved SoundFont path the decoder should load: the user's choice, or the
// bundled default if unset/empty.
inline std::string soundFontPath() {
    std::string p = getConfigString(kKeySoundFontPath, kDefaultSoundFont);
    if (p.empty()) p = kDefaultSoundFont;
    return p;
}

// Selected rendering backend (validated). Meaningful only in the CLAP-enabled
// build; the FluidSynth-only build treats everything as kEngineFluidSynth.
inline int engine() {
    int64_t e = getConfigInt(kKeyEngine, kEngineFluidSynth);
    return (e == kEngineClap) ? kEngineClap : kEngineFluidSynth;
}

// Path to the .clap bundle the CLAP backend should host (empty if unset).
inline std::string clapPluginPath() {
    return getConfigString(kKeyClapPluginPath, "");
}

// Which plugin inside that bundle to instantiate (a bundle can host several).
// Empty selects the first plugin in the bundle.
inline std::string clapPluginId() {
    return getConfigString(kKeyClapPluginId, "");
}

// A CLAP preset the hosted plugin should load on init (headless preset switch).
// Persisted as "kind\tlocation\tloadKey"; `valid` is false when unset.
struct ClapPresetRef {
    bool valid = false;
    uint32_t locationKind = 0;   // clap_preset_discovery_location_kind
    std::string location;        // file path (FILE kind) or empty
    std::string loadKey;         // container key; may be empty
};

inline ClapPresetRef clapPreset() {
    ClapPresetRef ref;
    std::string s = getConfigString(kKeyClapPreset, "");
    if (s.empty()) return ref;
    size_t t1 = s.find('\t');
    if (t1 == std::string::npos) return ref;
    size_t t2 = s.find('\t', t1 + 1);
    if (t2 == std::string::npos) return ref;
    ref.locationKind = (uint32_t)strtoul(s.substr(0, t1).c_str(), nullptr, 10);
    ref.location = s.substr(t1 + 1, t2 - t1 - 1);
    ref.loadKey = s.substr(t2 + 1);
    ref.valid = true;
    return ref;
}

} // namespace midi_config
