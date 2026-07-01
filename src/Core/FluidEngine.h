//
//  FluidEngine.h
//  foo_tun_midi
//
//  A FluidEngine is a FluidSynth synth with a SoundFont already loaded, keyed by
//  (soundfont, sample rate, percussion mode). Loading a compressed .sf3 costs
//  seconds, so engines are cached and reused across tracks by FluidEngineCache,
//  and can be preloaded in the background (at startup / on prefs change) so the
//  first playback doesn't pay the load either.
//

#pragma once

#include <cstdint>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// Match FluidSynth's own tags (fluidsynth/types.h) so these forward declarations
// don't conflict with the real header.
typedef struct _fluid_hashtable_t fluid_settings_t;
typedef struct _fluid_synth_t fluid_synth_t;
typedef struct _fluid_player_t fluid_player_t;

namespace foo_midi {

struct EngineKey {
    std::string soundfont;
    int sampleRate = 44100;
    bool forcePercussion = false;

    bool operator==(const EngineKey& o) const {
        return sampleRate == o.sampleRate &&
               forcePercussion == o.forcePercussion &&
               soundfont == o.soundfont;
    }
    bool operator!=(const EngineKey& o) const { return !(*this == o); }
};

// A loaded synth + soundfont. One player at a time is hosted on its synth.
class FluidEngine {
public:
    // Build a synth and load the SoundFont. Returns nullptr on any failure.
    static std::shared_ptr<FluidEngine> create(const EngineKey& key);
    ~FluidEngine();

    FluidEngine(const FluidEngine&) = delete;
    FluidEngine& operator=(const FluidEngine&) = delete;

    fluid_synth_t* synth() const { return m_synth; }
    const EngineKey& key() const { return m_key; }

    // Return the synth to a clean slate for a new track: clears controllers,
    // programs and ringing voices, then re-applies drum routing if configured.
    void resetForNewTrack();

private:
    FluidEngine() = default;
    bool load(const EngineKey& key);

    EngineKey m_key;
    fluid_settings_t* m_settings = nullptr;
    fluid_synth_t* m_synth = nullptr;
    int m_sfontId = -1;
    // Percussion program to select on bank 128 when forcing drums. Many drum
    // SoundFonts put their kit at a program number other than 0 (e.g. a TR-909
    // kit at program 24), so we can't assume 0. -1 = no percussion preset.
    int m_drumProgram = 0;
};

// Process-wide cache of a single most-recently-used engine.
class FluidEngineCache {
public:
    static FluidEngineCache& instance();

    // Get an engine for `key`. Reuses the cached engine when it matches and is
    // idle (waiting for an in-flight preload of the same key); otherwise builds
    // a fresh one. If the cache slot is busy with a different in-use engine
    // (e.g. a gapless overlap), the returned engine is a temporary that is not
    // cached. Returns nullptr only if the SoundFont can't be loaded.
    std::shared_ptr<FluidEngine> acquire(const EngineKey& key);

    // Mark the cached engine idle again (no-op for temporaries).
    void release(const std::shared_ptr<FluidEngine>& engine);

    // Kick off a background load so a later acquire(key) is instant. Cheap no-op
    // if the key is already loaded or loading.
    void preload(const EngineKey& key);

    // Drop the cached engine (called at shutdown).
    void clear();

private:
    FluidEngineCache() = default;

    enum class Status { Empty, Loading, Ready };

    std::mutex m_mtx;
    std::condition_variable m_cv;
    Status m_status = Status::Empty;
    EngineKey m_key;
    std::shared_ptr<FluidEngine> m_engine;
    bool m_inUse = false;
};

} // namespace foo_midi
