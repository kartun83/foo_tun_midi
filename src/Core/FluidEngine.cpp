//
//  FluidEngine.cpp
//  foo_tun_midi
//

#include "FluidEngine.h"
#include "../fb2k_sdk.h"   // console::*
#include <fluidsynth.h>

#include <set>
#include <string>

namespace foo_midi {

namespace {

// Walk the loaded SoundFont's presets and log a human-readable summary to the
// foobar2000 console, so "why is this SoundFont silent?" is answerable without a
// debugger. The common silent cases are a bank mismatch: force-percussion pins
// every channel to bank 128, so a SoundFont with no percussion presets plays
// nothing; conversely a drum-only SoundFont is silent for melodic MIDI.
// Returns the percussion program number to select on bank 128 when forcing
// drums (0 preferred, else the lowest present, -1 if the SoundFont has none).
int logSoundFontContents(fluid_synth_t* synth, int sfontId,
                         const std::string& path, bool forcePercussion) {
    std::string name = path;
    if (auto slash = name.find_last_of('/'); slash != std::string::npos)
        name = name.substr(slash + 1);

    fluid_sfont_t* sf = fluid_synth_get_sfont_by_id(synth, sfontId);
    if (!sf) {
        console::error((std::string("foo_tun_midi: loaded '") + name +
                        "' but could not enumerate its presets.").c_str());
        return 0;
    }

    std::set<int> melodicBanks;   // banks other than 128
    std::set<int> percProgs;      // program numbers present in bank 128
    int presetCount = 0;
    bool hasBank0Prog0 = false;   // GM default melodic patch (piano)
    std::string firstKitName;

    fluid_sfont_iteration_start(sf);
    for (fluid_preset_t* p = fluid_sfont_iteration_next(sf); p;
         p = fluid_sfont_iteration_next(sf)) {
        int bank = fluid_preset_get_banknum(p);
        int prog = fluid_preset_get_num(p);
        ++presetCount;
        if (bank == 128) {
            if (percProgs.empty()) {
                const char* pn = fluid_preset_get_name(p);
                firstKitName = pn ? pn : "";
            }
            percProgs.insert(prog);
        } else {
            melodicBanks.insert(bank);
            if (bank == 0 && prog == 0) hasBank0Prog0 = true;
        }
    }

    // Prefer program 0 when present (the GM standard kit), else the lowest.
    int drumProgram = percProgs.empty() ? -1
                    : (percProgs.count(0) ? 0 : *percProgs.begin());

    std::string banks;
    for (int b : melodicBanks) {
        if (!banks.empty()) banks += ",";
        banks += std::to_string(b);
    }

    std::string summary = "foo_tun_midi: SoundFont '" + name + "' loaded: " +
        std::to_string(presetCount) + " presets; melodic banks: " +
        (banks.empty() ? std::string("(none)") : banks) +
        "; percussion (bank 128): " +
        (percProgs.empty() ? std::string("none")
                           : std::to_string(percProgs.size()) + " kit(s)") +
        (forcePercussion ? "; force-percussion: ON" : "; force-percussion: off");
    console::print(summary.c_str());

    // Actionable warnings for the silent-mismatch cases.
    if (forcePercussion && drumProgram < 0) {
        console::error(("foo_tun_midi: WARNING '" + name +
            "' has NO percussion presets (bank 128), but 'Force all channels to "
            "the drum kit' is ON \xE2\x80\x94 playback will be SILENT. Turn force-"
            "percussion off, or choose a SoundFont that contains a drum kit.").c_str());
    } else if (forcePercussion) {
        // Report which kit we'll use, so a non-zero program (e.g. TR-909 at 24)
        // isn't a mystery.
        console::print(("foo_tun_midi: force-percussion using bank 128 program " +
            std::to_string(drumProgram) +
            (firstKitName.empty() ? std::string() : " ('" + firstKitName + "')")).c_str());
    } else if (melodicBanks.empty() && !percProgs.empty()) {
        console::error(("foo_tun_midi: WARNING '" + name +
            "' contains only percussion presets (bank 128). Melodic MIDI will be "
            "SILENT \xE2\x80\x94 enable 'Force all channels to the drum kit' to audition "
            "drum files with this SoundFont.").c_str());
    } else if (!hasBank0Prog0) {
        console::print(("foo_tun_midi: note '" + name +
            "' has no bank 0 / program 0 preset; General MIDI files that rely on "
            "the default patch may be silent on some channels.").c_str());
    }

    return drumProgram;
}

} // namespace

// ---------------------------------------------------------------- FluidEngine

std::shared_ptr<FluidEngine> FluidEngine::create(const EngineKey& key) {
    // Not std::make_shared: the constructor is private.
    std::shared_ptr<FluidEngine> eng(new FluidEngine());
    if (!eng->load(key)) return nullptr;
    return eng;
}

bool FluidEngine::load(const EngineKey& key) {
    m_key = key;

    m_settings = new_fluid_settings();
    if (!m_settings) return false;
    fluid_settings_setnum(m_settings, "synth.sample-rate", (double)key.sampleRate);
    fluid_settings_setint(m_settings, "synth.midi-channels", 16);

    m_synth = new_fluid_synth(m_settings);
    if (!m_synth) return false;

    m_sfontId = fluid_synth_sfload(m_synth, key.soundfont.c_str(), 1 /* reset presets */);
    if (m_sfontId == FLUID_FAILED) {
        console::error((std::string("foo_tun_midi: failed to load SoundFont: ") +
                        key.soundfont + " (not a valid .sf2/.sf3, or unreadable)").c_str());
        return false;
    }

    m_drumProgram = logSoundFontContents(m_synth, m_sfontId, key.soundfont,
                                         key.forcePercussion);

    resetForNewTrack();
    return true;
}

void FluidEngine::resetForNewTrack() {
    if (!m_synth) return;

    // Reset all channels/controllers/programs and silence ringing voices so a
    // reused synth doesn't carry state (program changes, held notes, reverb
    // tail) from the previous track.
    fluid_synth_system_reset(m_synth);

    if (m_key.forcePercussion && m_drumProgram >= 0) {
        // Pin every channel to the percussion bank (128) and load an actual kit
        // present in this SoundFont (not necessarily program 0 — e.g. a TR-909
        // kit sits at program 24), then mark it a drum channel so later program
        // changes the file sends stay within bank 128.
        for (int ch = 0; ch < 16; ++ch) {
            fluid_synth_set_channel_type(m_synth, ch, CHANNEL_TYPE_DRUM);
            fluid_synth_bank_select(m_synth, ch, 128);
            fluid_synth_program_change(m_synth, ch, m_drumProgram);
        }
    }
}

FluidEngine::~FluidEngine() {
    if (m_synth)    { delete_fluid_synth(m_synth);       m_synth = nullptr; }
    if (m_settings) { delete_fluid_settings(m_settings); m_settings = nullptr; }
}

// ----------------------------------------------------------- FluidEngineCache

FluidEngineCache& FluidEngineCache::instance() {
    static FluidEngineCache s_instance;
    return s_instance;
}

std::shared_ptr<FluidEngine> FluidEngineCache::acquire(const EngineKey& key) {
    std::unique_lock<std::mutex> lk(m_mtx);

    for (;;) {
        if (m_status == Status::Ready && m_key == key && !m_inUse) {
            m_inUse = true;
            auto eng = m_engine;
            lk.unlock();
            eng->resetForNewTrack();
            return eng;
        }
        if (m_status == Status::Loading && m_key == key) {
            m_cv.wait(lk);           // a preload for this key is in flight
            continue;
        }
        break;                       // nothing usable cached: build our own
    }

    if (!m_inUse) {
        // Cache slot is free: build this key and install it as the cached engine.
        m_key = key;
        m_status = Status::Loading;
        m_engine = nullptr;
        lk.unlock();

        auto eng = FluidEngine::create(key);

        lk.lock();
        if (eng && m_key == key) {
            m_engine = eng;
            m_status = Status::Ready;
            m_inUse = true;
            m_cv.notify_all();
            lk.unlock();
            eng->resetForNewTrack();
            return eng;
        }
        // Build failed, or the desired key changed while we loaded.
        if (m_key == key) { m_status = Status::Empty; m_engine = nullptr; }
        m_cv.notify_all();
        lk.unlock();
        return eng;                  // valid (temporary) or nullptr on failure
    }

    // Cache is busy with a different in-use engine (e.g. gapless overlap):
    // build a temporary engine that won't be cached.
    lk.unlock();
    return FluidEngine::create(key);
}

void FluidEngineCache::release(const std::shared_ptr<FluidEngine>& engine) {
    std::lock_guard<std::mutex> lk(m_mtx);
    if (engine && engine == m_engine) {
        m_inUse = false;
        m_cv.notify_all();
    }
    // Temporary engines are freed when the caller drops its shared_ptr.
}

void FluidEngineCache::preload(const EngineKey& key) {
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        if (m_key == key && (m_status == Status::Ready || m_status == Status::Loading))
            return;                  // already loaded or loading
        if (m_inUse)
            return;                  // slot lent out; will load on next acquire
        m_key = key;
        m_status = Status::Loading;
        m_engine = nullptr;
    }

    std::thread([this, key]() {
        auto eng = FluidEngine::create(key);
        std::lock_guard<std::mutex> lk(m_mtx);
        if (m_key == key && m_status == Status::Loading) {
            m_engine = eng;
            m_status = eng ? Status::Ready : Status::Empty;
            m_cv.notify_all();
        }
    }).detach();
}

void FluidEngineCache::clear() {
    std::lock_guard<std::mutex> lk(m_mtx);
    if (m_inUse) return;             // don't yank an engine out from under a track
    m_engine = nullptr;
    m_status = Status::Empty;
}

} // namespace foo_midi
