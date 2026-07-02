//
//  ClapPresets.cpp
//  foo_tun_midi (CLAP-enabled / "Full" build only)
//
//  Preset-discovery host: drives clap_preset_discovery_factory to enumerate a
//  plugin's presets without instantiating it. See ClapPresets.h.
//

#include "ClapPresets.h"

#include <clap/clap.h>
#include <clap/factory/preset-discovery.h>
#include <dlfcn.h>

#include <algorithm>
#include <filesystem>

namespace foo_midi {

namespace fs = std::filesystem;

namespace {

// State threaded through the indexer + metadata-receiver callbacks.
struct Crawl {
    std::string wantPluginId;                 // filter presets to this plugin (empty = keep all)
    size_t maxPresets = 5000;

    // Declared by the provider during init():
    std::vector<std::string> extensions;      // preset file extensions (no dot)
    struct Loc { uint32_t kind; std::string path; };
    std::vector<Loc> locations;

    // Accumulated presets. `curPluginIds` collects add_plugin_id() calls for the
    // preset currently being described (between begin_preset calls).
    std::vector<ClapPreset> out;
    std::vector<std::vector<std::string>> pluginIds;
    std::string curLocation;                  // location being crawled (for FILE presets)
    uint32_t    curKind = 0;
};

// Derive a friendly display name: some plugins (e.g. Tekno) report the full file
// path as the preset name, so prefer the file stem; fall back to load_key.
std::string friendlyName(const char* name, const char* loadKey, const std::string& location) {
    std::string n = name ? name : "";
    if (!n.empty()) {
        // If it looks like a path, take the stem.
        if (n.find('/') != std::string::npos) return fs::path(n).stem().string();
        return n;
    }
    if (!location.empty()) return fs::path(location).stem().string();
    if (loadKey && loadKey[0]) return loadKey;
    return "(unnamed)";
}

// ---- metadata receiver callbacks ----
bool rcv_begin(const clap_preset_discovery_metadata_receiver_t* r, const char* name, const char* load_key) {
    Crawl* c = (Crawl*)r->receiver_data;
    if (c->out.size() >= c->maxPresets) return false;   // stop the provider
    ClapPreset p;
    p.name = friendlyName(name, load_key, c->curLocation);
    p.locationKind = c->curKind;
    p.location = c->curLocation;
    p.loadKey = load_key ? load_key : "";
    c->out.push_back(std::move(p));
    c->pluginIds.emplace_back();
    return true;
}
void rcv_add_plugin_id(const clap_preset_discovery_metadata_receiver_t* r, const clap_universal_plugin_id_t* id) {
    Crawl* c = (Crawl*)r->receiver_data;
    if (!c->pluginIds.empty() && id && id->id) c->pluginIds.back().push_back(id->id);
}
void rcv_on_error(const clap_preset_discovery_metadata_receiver_t*, int32_t, const char*) {}
void rcv_set_soundpack(const clap_preset_discovery_metadata_receiver_t*, const char*) {}
void rcv_set_flags(const clap_preset_discovery_metadata_receiver_t*, uint32_t) {}
void rcv_add_creator(const clap_preset_discovery_metadata_receiver_t*, const char*) {}
void rcv_set_desc(const clap_preset_discovery_metadata_receiver_t*, const char*) {}
void rcv_set_ts(const clap_preset_discovery_metadata_receiver_t*, clap_timestamp, clap_timestamp) {}
void rcv_add_feature(const clap_preset_discovery_metadata_receiver_t*, const char*) {}
void rcv_add_extra(const clap_preset_discovery_metadata_receiver_t*, const char*, const char*) {}

// ---- indexer callbacks (provider declares its locations/filetypes here) ----
bool idx_filetype(const clap_preset_discovery_indexer_t* ix, const clap_preset_discovery_filetype_t* ft) {
    Crawl* c = (Crawl*)ix->indexer_data;
    if (ft && ft->file_extension && ft->file_extension[0]) c->extensions.push_back(ft->file_extension);
    return true;
}
bool idx_location(const clap_preset_discovery_indexer_t* ix, const clap_preset_discovery_location_t* loc) {
    Crawl* c = (Crawl*)ix->indexer_data;
    if (loc) c->locations.push_back({ loc->kind, loc->location ? loc->location : "" });
    return true;
}
bool idx_soundpack(const clap_preset_discovery_indexer_t*, const clap_preset_discovery_soundpack_t*) { return true; }
const void* idx_get_ext(const clap_preset_discovery_indexer_t*, const char*) { return nullptr; }

const clap_preset_discovery_factory_t* getPresetFactory(const clap_plugin_entry_t* entry) {
    auto* f = (const clap_preset_discovery_factory_t*)entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID);
    if (!f) f = (const clap_preset_discovery_factory_t*)entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT);
    return f;
}

std::string bundleBinary(const std::string& bundlePath) {
    std::string name = bundlePath;
    while (!name.empty() && name.back() == '/') name.pop_back();
    std::string leaf = fs::path(name).stem().string();  // strip .clap
    return name + "/Contents/MacOS/" + leaf;
}

} // namespace

ClapPresetList discoverClapPresets(const std::string& bundlePath,
                                   const std::string& pluginId,
                                   size_t maxPresets) {
    ClapPresetList result;

    // RTLD_LAZY: read-only metadata pass, no DSP. Note the plugin's image won't be
    // unloaded on dlclose (Obj-C/JUCE), so this leaves the one selected plugin
    // resident for the session — acceptable for a single user-initiated action.
    void* dl = dlopen(bundleBinary(bundlePath).c_str(), RTLD_LOCAL | RTLD_LAZY);
    if (!dl) return result;

    auto* entry = (const clap_plugin_entry_t*)dlsym(dl, "clap_entry");
    if (!entry || !entry->init(bundlePath.c_str())) { dlclose(dl); return result; }

    const clap_preset_discovery_factory_t* pf = getPresetFactory(entry);
    if (!pf) { entry->deinit(); dlclose(dl); return result; }
    result.supported = true;

    clap_preset_discovery_indexer_t indexer{};
    indexer.clap_version = CLAP_VERSION;
    indexer.name = "foo_tun_midi";
    indexer.vendor = "kartun83";
    indexer.declare_filetype = idx_filetype;
    indexer.declare_location = idx_location;
    indexer.declare_soundpack = idx_soundpack;
    indexer.get_extension = idx_get_ext;

    clap_preset_discovery_metadata_receiver_t rcv{};
    rcv.on_error = rcv_on_error;
    rcv.begin_preset = rcv_begin;
    rcv.add_plugin_id = rcv_add_plugin_id;
    rcv.set_soundpack_id = rcv_set_soundpack;
    rcv.set_flags = rcv_set_flags;
    rcv.add_creator = rcv_add_creator;
    rcv.set_description = rcv_set_desc;
    rcv.set_timestamps = rcv_set_ts;
    rcv.add_feature = rcv_add_feature;
    rcv.add_extra_info = rcv_add_extra;

    uint32_t nprov = pf->count(pf);
    for (uint32_t i = 0; i < nprov; ++i) {
        const clap_preset_discovery_provider_descriptor_t* pd = pf->get_descriptor(pf, i);
        if (!pd) continue;

        Crawl crawl;
        crawl.wantPluginId = pluginId;
        crawl.maxPresets = maxPresets;
        indexer.indexer_data = &crawl;
        rcv.receiver_data = &crawl;

        const clap_preset_discovery_provider_t* prov = pf->create(pf, &indexer, pd->id);
        if (!prov) continue;
        if (!prov->init(prov)) { prov->destroy(prov); continue; }

        for (const auto& loc : crawl.locations) {
            if (crawl.out.size() >= maxPresets) break;
            crawl.curKind = loc.kind;
            if (loc.kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN) {
                crawl.curLocation.clear();
                prov->get_metadata(prov, loc.kind, nullptr, &rcv);
            } else if (loc.kind == CLAP_PRESET_DISCOVERY_LOCATION_FILE) {
                std::error_code ec;
                if (fs::is_directory(loc.path, ec)) {
                    for (fs::recursive_directory_iterator it(loc.path, fs::directory_options::skip_permission_denied, ec), end;
                         !ec && it != end && crawl.out.size() < maxPresets; it.increment(ec)) {
                        if (!it->is_regular_file(ec)) continue;
                        std::string ext = it->path().extension().string();
                        if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
                        bool match = crawl.extensions.empty();
                        for (const auto& want : crawl.extensions) if (want == ext) { match = true; break; }
                        if (!match) continue;
                        crawl.curLocation = it->path().string();
                        prov->get_metadata(prov, loc.kind, crawl.curLocation.c_str(), &rcv);
                    }
                } else if (fs::is_regular_file(loc.path, ec)) {
                    crawl.curLocation = loc.path;                 // preset container file
                    prov->get_metadata(prov, loc.kind, loc.path.c_str(), &rcv);
                }
            }
        }
        prov->destroy(prov);

        // Keep presets that apply to the selected plugin: those that declared no
        // plugin id (apply to all) or explicitly listed it.
        for (size_t k = 0; k < crawl.out.size(); ++k) {
            const auto& ids = crawl.pluginIds[k];
            bool keep = pluginId.empty() || ids.empty() ||
                        std::find(ids.begin(), ids.end(), pluginId) != ids.end();
            if (keep) result.presets.push_back(std::move(crawl.out[k]));
        }
    }

    entry->deinit();
    dlclose(dl);

    // Sort by name, drop exact duplicates (same name+location+loadKey).
    std::sort(result.presets.begin(), result.presets.end(),
              [](const ClapPreset& a, const ClapPreset& b) {
                  if (a.name != b.name) return a.name < b.name;
                  if (a.location != b.location) return a.location < b.location;
                  return a.loadKey < b.loadKey;
              });
    result.presets.erase(
        std::unique(result.presets.begin(), result.presets.end(),
                    [](const ClapPreset& a, const ClapPreset& b) {
                        return a.name == b.name && a.location == b.location && a.loadKey == b.loadKey;
                    }),
        result.presets.end());
    return result;
}

} // namespace foo_midi
