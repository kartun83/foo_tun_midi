//
//  ClapRenderer.cpp
//  foo_tun_midi (CLAP-enabled / "Full" build only)
//
//  Port of the standalone CLAP host spike into an IMidiRenderer. See
//  ClapRenderer.h and docs/ARCHITECTURE.md for the host-contract findings this
//  encodes (buffer every declared port incl. inputs; query note dialect).
//

#include "ClapRenderer.h"
#include "../Core/SMFInfo.h"
#include "../fb2k_sdk.h"   // console::*

#include <clap/clap.h>
#include <dlfcn.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace foo_midi {

namespace {

// --- host callbacks -------------------------------------------------------
// Bare host: advertise no extensions. Enough to boot + render the instruments
// the spike validated (Tekno, OsTIrus). Single-threaded offline use.
const void* host_get_extension(const clap_host_t*, const char*) { return nullptr; }
void host_request_restart(const clap_host_t*) {}
void host_request_process(const clap_host_t*) {}

// A scheduled MIDI event mapped to an absolute output frame.
struct SchedEvent {
    long long frame;
    uint8_t status, d1, d2;
};

// Per-block input event queue handed to the plugin.
struct EventStore {
    std::vector<clap_event_note_t> notes;
    std::vector<clap_event_midi_t> midis;
    std::vector<const clap_event_header_t*> order;
};
uint32_t in_size(const clap_input_events_t* l) {
    return (uint32_t)((EventStore*)l->ctx)->order.size();
}
const clap_event_header_t* in_get(const clap_input_events_t* l, uint32_t i) {
    return ((EventStore*)l->ctx)->order[i];
}
bool out_try_push(const clap_output_events_t*, const clap_event_header_t*) { return true; }

} // namespace

struct ClapRenderer::Impl {
    // library / plugin
    void* dl = nullptr;
    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_t* plugin = nullptr;
    clap_host_t host{};
    std::atomic<bool> callbackRequested{false};

    // config
    int sampleRate = 48000;
    bool sendMidi = true;         // MIDI vs CLAP note dialect
    static constexpr uint32_t kMaxBlock = 4096;

    // bus buffers (one channel buffer per declared channel; port views over them)
    std::vector<std::vector<float>> inStore, outStore;
    std::vector<std::vector<float*>> inPtrs, outPtrs;
    std::vector<clap_audio_buffer_t> inBufs, outBufs;
    float* capL = nullptr;
    float* capR = nullptr;

    // schedule + playback cursors
    std::vector<SchedEvent> sched;
    size_t evtIdx = 0;
    long long pos = 0;            // absolute output frame
    long long nominalFrames = 0;  // parsed song length in frames (0 = unknown)
    int silenceRun = 0;
    bool started = false;
    bool finished = false;

    ~Impl() { teardown(); }

    void teardown() {
        if (plugin) {
            if (started) { plugin->stop_processing(plugin); started = false; }
            plugin->deactivate(plugin);
            plugin->destroy(plugin);
            plugin = nullptr;
        }
        if (entry) { entry->deinit(); entry = nullptr; }
        if (dl) { dlclose(dl); dl = nullptr; }
    }

    // Allocate a real, zeroable buffer for every channel of every declared port.
    void buildBuses(const std::vector<uint32_t>& portCh,
                    std::vector<std::vector<float>>& store,
                    std::vector<std::vector<float*>>& ptrs,
                    std::vector<clap_audio_buffer_t>& bufs) {
        bufs.resize(portCh.size());
        ptrs.resize(portCh.size());
        size_t total = 0; for (uint32_t c : portCh) total += c;
        store.assign(total, std::vector<float>(kMaxBlock, 0.f));
        size_t idx = 0;
        for (size_t p = 0; p < portCh.size(); ++p) {
            ptrs[p].resize(portCh[p]);
            for (uint32_t c = 0; c < portCh[p]; ++c) ptrs[p][c] = store[idx++].data();
            bufs[p] = clap_audio_buffer_t{};
            bufs[p].data32 = ptrs[p].data();
            bufs[p].channel_count = portCh[p];
        }
    }
};

ClapRenderer::ClapRenderer() : m_impl(std::make_unique<Impl>()) {}
ClapRenderer::~ClapRenderer() = default;

int ClapRenderer::sampleRate() const { return m_impl->sampleRate; }

bool ClapRenderer::init(const std::string& pluginPath, const std::string& pluginId,
                        int sampleRate, const SMFInfo& smf) {
    Impl& d = *m_impl;
    d.sampleRate = sampleRate > 0 ? sampleRate : 48000;

    // Resolve the binary inside the .clap bundle.
    std::string bin = pluginPath;
    if (pluginPath.size() > 5 && pluginPath.substr(pluginPath.size() - 5) == ".clap") {
        std::string name = pluginPath;
        while (!name.empty() && name.back() == '/') name.pop_back();
        auto slash = name.find_last_of('/');
        std::string leaf = (slash == std::string::npos) ? name : name.substr(slash + 1);
        leaf = leaf.substr(0, leaf.size() - 5);
        bin = name + "/Contents/MacOS/" + leaf;
    }

    d.dl = dlopen(bin.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!d.dl) { console::error("foo_tun_midi: CLAP dlopen failed"); return false; }

    d.entry = (const clap_plugin_entry_t*)dlsym(d.dl, "clap_entry");
    if (!d.entry) { console::error("foo_tun_midi: no clap_entry in bundle"); d.teardown(); return false; }
    if (!d.entry->init(pluginPath.c_str())) {
        console::error("foo_tun_midi: clap entry init failed"); d.teardown(); return false;
    }

    auto* factory = (const clap_plugin_factory_t*)d.entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    if (!factory || factory->get_plugin_count(factory) == 0) {
        console::error("foo_tun_midi: CLAP bundle has no plugins"); d.teardown(); return false;
    }
    // Pick the requested plugin by id (bundles can host several); default to the
    // first when no id is configured or it isn't found.
    const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, 0);
    if (!pluginId.empty()) {
        uint32_t n = factory->get_plugin_count(factory);
        for (uint32_t i = 0; i < n; ++i) {
            const clap_plugin_descriptor_t* cand = factory->get_plugin_descriptor(factory, i);
            if (cand && cand->id && pluginId == cand->id) { desc = cand; break; }
        }
    }
    if (!desc) { d.teardown(); return false; }

    d.host.clap_version = CLAP_VERSION;
    d.host.host_data = &d;
    d.host.name = "foo_tun_midi";
    d.host.vendor = "kartun83";
    d.host.url = "https://github.com/kartun83/foo_tun_midi";
    d.host.version = "0.4.0";
    d.host.get_extension = host_get_extension;
    d.host.request_restart = host_request_restart;
    d.host.request_process = host_request_process;
    d.host.request_callback = [](const clap_host_t* h) {
        ((Impl*)h->host_data)->callbackRequested = true;
    };

    d.plugin = factory->create_plugin(factory, &d.host, desc->id);
    if (!d.plugin || !d.plugin->init(d.plugin)) {
        console::error("foo_tun_midi: CLAP create/init failed"); d.teardown(); return false;
    }

    // Mirror the plugin's exact bus layout (inputs included — JUCE-wrapped
    // plugins crash otherwise).
    std::vector<uint32_t> inPortCh, outPortCh;
    auto* ap = (const clap_plugin_audio_ports_t*)d.plugin->get_extension(d.plugin, CLAP_EXT_AUDIO_PORTS);
    if (ap) {
        uint32_t nin = ap->count(d.plugin, true), nout = ap->count(d.plugin, false);
        for (uint32_t i = 0; i < nin; ++i) {
            clap_audio_port_info_t info{};
            inPortCh.push_back(ap->get(d.plugin, i, true, &info) ? info.channel_count : 2);
        }
        for (uint32_t i = 0; i < nout; ++i) {
            clap_audio_port_info_t info{};
            outPortCh.push_back(ap->get(d.plugin, i, false, &info) ? info.channel_count : 2);
        }
    }
    if (outPortCh.empty()) outPortCh.push_back(2);

    // Note dialect: send MIDI unless the plugin's input note port supports CLAP
    // and not MIDI.
    d.sendMidi = true;
    auto* np = (const clap_plugin_note_ports_t*)d.plugin->get_extension(d.plugin, CLAP_EXT_NOTE_PORTS);
    if (np && np->count(d.plugin, true) > 0) {
        clap_note_port_info_t info{};
        if (np->get(d.plugin, 0, true, &info)) {
            bool clapOk = info.supported_dialects & CLAP_NOTE_DIALECT_CLAP;
            bool midiOk = info.supported_dialects & CLAP_NOTE_DIALECT_MIDI;
            if (clapOk && !midiOk) d.sendMidi = false;
        }
    }

    d.buildBuses(inPortCh, d.inStore, d.inPtrs, d.inBufs);
    d.buildBuses(outPortCh, d.outStore, d.outPtrs, d.outBufs);
    d.capL = d.outPtrs.empty() ? nullptr : d.outPtrs[0][0];
    d.capR = (!d.outPtrs.empty() && outPortCh[0] > 1) ? d.outPtrs[0][1] : d.capL;

    if (!d.plugin->activate(d.plugin, (double)d.sampleRate, 1, Impl::kMaxBlock)) {
        console::error("foo_tun_midi: CLAP activate failed"); d.teardown(); return false;
    }
    if (!d.plugin->start_processing(d.plugin)) {
        console::error("foo_tun_midi: CLAP start_processing failed"); d.teardown(); return false;
    }
    d.started = true;

    // Schedule events: map each retained channel event's tick to an output frame.
    d.sched.reserve(smf.events.size());
    for (const auto& e : smf.events) {
        long long frame = (long long)(smf.tickToSeconds(e.tick) * d.sampleRate + 0.5);
        d.sched.push_back({ frame, e.status, e.d1, e.d2 });
    }
    d.nominalFrames = smf.durationSeconds > 0
        ? (long long)(smf.durationSeconds * d.sampleRate) : 0;
    return true;
}

int ClapRenderer::render(float* out, int frames) {
    Impl& d = *m_impl;
    if (d.finished || !d.plugin || frames <= 0) return 0;
    if (frames > (int)Impl::kMaxBlock) frames = (int)Impl::kMaxBlock;

    // Events whose frame lands in [pos, pos+frames): convert to plugin events.
    EventStore store;
    while (d.evtIdx < d.sched.size() && d.sched[d.evtIdx].frame < d.pos + frames) {
        const SchedEvent& e = d.sched[d.evtIdx++];
        uint32_t t = (uint32_t)std::max<long long>(0, e.frame - d.pos);
        uint8_t hi = e.status & 0xF0;
        if (d.sendMidi) {
            clap_event_midi_t m{};
            m.header.size = sizeof(m);
            m.header.time = t;
            m.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            m.header.type = CLAP_EVENT_MIDI;
            m.port_index = 0;
            m.data[0] = e.status; m.data[1] = e.d1; m.data[2] = e.d2;
            store.midis.push_back(m);
        } else if (hi == 0x90 || hi == 0x80) {  // CLAP dialect: notes only
            clap_event_note_t n{};
            n.header.size = sizeof(n);
            n.header.time = t;
            n.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            n.header.type = (hi == 0x90 && e.d2 > 0) ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF;
            n.note_id = -1;
            n.port_index = 0;
            n.channel = e.status & 0x0F;
            n.key = e.d1;
            n.velocity = e.d2 / 127.0;
            store.notes.push_back(n);
        }
    }
    for (auto& e : store.notes) store.order.push_back(&e.header);
    for (auto& e : store.midis) store.order.push_back(&e.header);

    clap_input_events_t inEv{};   inEv.ctx = &store; inEv.size = in_size; inEv.get = in_get;
    clap_output_events_t outEv{}; outEv.ctx = nullptr; outEv.try_push = out_try_push;

    for (auto& ch : d.inStore)  std::fill(ch.begin(), ch.begin() + frames, 0.f);
    for (auto& ch : d.outStore) std::fill(ch.begin(), ch.begin() + frames, 0.f);

    clap_process_t proc{};
    proc.steady_time = d.pos;
    proc.frames_count = (uint32_t)frames;
    proc.transport = nullptr;
    proc.audio_inputs = d.inBufs.empty() ? nullptr : d.inBufs.data();
    proc.audio_inputs_count = (uint32_t)d.inBufs.size();
    proc.audio_outputs = d.outBufs.empty() ? nullptr : d.outBufs.data();
    proc.audio_outputs_count = (uint32_t)d.outBufs.size();
    proc.in_events = &inEv;
    proc.out_events = &outEv;

    clap_process_status st = d.plugin->process(d.plugin, &proc);
    if (st == CLAP_PROCESS_ERROR) { d.finished = true; return 0; }
    if (d.callbackRequested.exchange(false)) d.plugin->on_main_thread(d.plugin);

    // Interleave output port 0 (first two channels) and track loudness.
    float blockPeak = 0.f;
    for (int i = 0; i < frames; ++i) {
        float l = d.capL ? d.capL[i] : 0.f;
        float r = d.capR ? d.capR[i] : l;
        out[i * 2] = l;
        out[i * 2 + 1] = r;
        float a = std::fabs(l), b = std::fabs(r);
        if (a > blockPeak) blockPeak = a;
        if (b > blockPeak) blockPeak = b;
    }
    d.pos += frames;

    // End-of-song: once all events are sent and we're past the parsed length,
    // stop after the sound decays (mirrors the FluidSynth tail trim).
    static constexpr float kSilence = 1.0e-4f;
    bool eventsDone = d.evtIdx >= d.sched.size();
    bool pastEnd = eventsDone && (d.nominalFrames == 0 || d.pos >= d.nominalFrames);
    if (pastEnd) {
        const long long endSilence = d.sampleRate / 4;                 // 0.25 s
        const long long maxTail    = (long long)d.sampleRate * 4;      // hard cap
        d.silenceRun = blockPeak < kSilence ? d.silenceRun + frames : 0;
        long long overrun = d.nominalFrames > 0 ? d.pos - d.nominalFrames : d.silenceRun;
        if (d.silenceRun >= endSilence || overrun >= maxTail) d.finished = true;
    }
    return frames;
}

} // namespace foo_midi
