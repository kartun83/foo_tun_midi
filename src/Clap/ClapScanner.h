//
//  ClapScanner.h
//  foo_tun_midi (CLAP-enabled / "Full" build only)
//
//  Enumerates installed CLAP *instrument* plugins from the standard macOS
//  locations so the preferences pane can offer a dropdown instead of a manual
//  file path. Only each plugin's descriptor is read (name / id / features) —
//  the plugin is never instantiated during a scan, so a heavyweight plugin
//  (e.g. a DSP-emulation synth) can't boot or crash the host while scanning.
//
//  The result is cached in memory and persisted (via MidiConfig) so the list is
//  instant on later launches; a rescan is on demand only.
//

#pragma once

#include <string>
#include <vector>

namespace foo_midi {

struct ClapPluginEntry {
    std::string name;   // descriptor display name
    std::string path;   // .clap bundle path
    std::string id;     // plugin id within the bundle (bundles can host several)
};

// Installed CLAP instruments, sorted by name. `forceRescan` re-walks the
// filesystem and re-persists; otherwise a persisted/in-memory cache is returned
// (scanning the filesystem once if neither exists yet).
const std::vector<ClapPluginEntry>& clapInstruments(bool forceRescan = false);

} // namespace foo_midi
