//
//  ClapPresets.h
//  foo_tun_midi (CLAP-enabled / "Full" build only)
//
//  Headless preset switching for a hosted CLAP instrument. A plugin that ships a
//  preset-discovery provider lets the host enumerate its presets *without*
//  instantiating the plugin, and load one via the preset-load extension — so we
//  can offer a preset dropdown even though we never show the plugin's GUI.
//
//  Not all plugins cooperate: many expose presets only through their own UI. For
//  those, discoverClapPresets() returns supported=false and the feature is a
//  silent no-op (the plugin plays its default patch).
//

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace foo_midi {

// One host-loadable preset. To load it, call the plugin's preset-load
// from_location(locationKind, location, loadKey).
struct ClapPreset {
    std::string name;          // friendly display name (derived from file/preset name)
    uint32_t    locationKind;  // clap_preset_discovery_location_kind (0=file, 1=plugin)
    std::string location;      // file path (FILE kind) or empty (PLUGIN kind)
    std::string loadKey;       // container key; may be empty
};

struct ClapPresetList {
    bool supported = false;             // plugin exposes a preset-discovery factory
    std::vector<ClapPreset> presets;    // sorted by name, deduplicated
};

// Enumerate the presets the plugin `pluginId` inside the .clap bundle at
// `bundlePath` exposes, via its preset-discovery factory. Reads preset metadata
// only — the plugin's DSP is never instantiated. `maxPresets` caps the crawl for
// plugins with very large libraries. Returns supported=false when the plugin
// ships no discovery provider.
ClapPresetList discoverClapPresets(const std::string& bundlePath,
                                   const std::string& pluginId,
                                   size_t maxPresets = 5000);

} // namespace foo_midi
