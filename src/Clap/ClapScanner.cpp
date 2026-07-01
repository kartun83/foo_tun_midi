//
//  ClapScanner.cpp
//  foo_tun_midi (CLAP-enabled / "Full" build only)
//

#include "ClapScanner.h"
#include "../Core/MidiConfig.h"

#include <clap/clap.h>
#include <dlfcn.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace foo_midi {

namespace fs = std::filesystem;

namespace {

std::vector<ClapPluginEntry> g_cache;
bool g_loaded = false;

// Standard macOS CLAP search locations, plus any CLAP_PATH entries.
std::vector<std::string> searchDirs() {
    std::vector<std::string> dirs;
    if (const char* home = getenv("HOME"))
        dirs.push_back(std::string(home) + "/Library/Audio/Plug-Ins/CLAP");
    dirs.push_back("/Library/Audio/Plug-Ins/CLAP");
    if (const char* cp = getenv("CLAP_PATH")) {
        std::string s(cp), cur;
        for (char c : s) {
            if (c == ':') { if (!cur.empty()) dirs.push_back(cur); cur.clear(); }
            else cur.push_back(c);
        }
        if (!cur.empty()) dirs.push_back(cur);
    }
    return dirs;
}

// Read a bundle's descriptors (no instantiation) and append its instruments.
void inspectBundle(const std::string& bundlePath, std::vector<ClapPluginEntry>& out) {
    std::string leaf = fs::path(bundlePath).stem().string();  // name without .clap
    std::string bin = bundlePath + "/Contents/MacOS/" + leaf;

    void* dl = dlopen(bin.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!dl) return;

    auto* entry = (const clap_plugin_entry_t*)dlsym(dl, "clap_entry");
    if (entry && entry->init(bundlePath.c_str())) {
        auto* factory = (const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
        if (factory) {
            uint32_t n = factory->get_plugin_count(factory);
            for (uint32_t i = 0; i < n; ++i) {
                const clap_plugin_descriptor_t* d = factory->get_plugin_descriptor(factory, i);
                if (!d) continue;
                bool instrument = false;
                for (const char* const* f = d->features; f && *f; ++f)
                    if (std::strcmp(*f, CLAP_PLUGIN_FEATURE_INSTRUMENT) == 0) { instrument = true; break; }
                if (!instrument) continue;
                ClapPluginEntry e;
                e.name = (d->name && d->name[0]) ? d->name : leaf;
                e.path = bundlePath;
                e.id = d->id ? d->id : "";
                out.push_back(std::move(e));
            }
        }
        entry->deinit();
    }
    dlclose(dl);
}

void persist() {
    std::string s;
    for (const auto& e : g_cache) {
        s += e.name; s += '\t'; s += e.path; s += '\t'; s += e.id; s += '\n';
    }
    midi_config::setConfigString(midi_config::kKeyClapPluginList, s.c_str());
}

bool loadPersisted() {
    std::string s = midi_config::getConfigString(midi_config::kKeyClapPluginList, "");
    if (s.empty()) return false;
    g_cache.clear();
    size_t start = 0;
    while (start < s.size()) {
        size_t nl = s.find('\n', start);
        std::string line = s.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        start = (nl == std::string::npos) ? s.size() : nl + 1;
        if (line.empty()) continue;
        size_t t1 = line.find('\t');
        size_t t2 = t1 == std::string::npos ? std::string::npos : line.find('\t', t1 + 1);
        if (t1 == std::string::npos || t2 == std::string::npos) continue;
        ClapPluginEntry e;
        e.name = line.substr(0, t1);
        e.path = line.substr(t1 + 1, t2 - t1 - 1);
        e.id = line.substr(t2 + 1);
        g_cache.push_back(std::move(e));
    }
    return !g_cache.empty();
}

void doScan() {
    g_cache.clear();
    for (const auto& dir : searchDirs()) {
        std::error_code ec;
        if (!fs::exists(dir, ec)) continue;
        fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            const fs::path& p = it->path();
            if (p.extension() == ".clap") {
                inspectBundle(p.string(), g_cache);
                it.disable_recursion_pending();   // don't descend into the bundle
            }
        }
    }
    std::sort(g_cache.begin(), g_cache.end(),
              [](const ClapPluginEntry& a, const ClapPluginEntry& b) { return a.name < b.name; });
    persist();
}

} // namespace

const std::vector<ClapPluginEntry>& clapInstruments(bool forceRescan) {
    if (forceRescan) { doScan(); g_loaded = true; return g_cache; }
    if (!g_loaded) {
        if (!loadPersisted()) doScan();
        g_loaded = true;
    }
    return g_cache;
}

} // namespace foo_midi
