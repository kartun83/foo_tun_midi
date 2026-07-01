//
//  MidiInput.cpp
//  foo_jl_midi_mac
//
//  foobar2000 input decoder for Standard MIDI Files, rendered through
//  FluidSynth + a SoundFont. Single-track input (one playable item per file).
//
//  Phase 1: the SoundFont path is hardcoded (see kDefaultSoundFont). A Cocoa
//  preferences pane will replace this later.
//

#include "../fb2k_sdk.h"
#include "../Core/SMFInfo.h"
#include "../Core/IMidiRenderer.h"
#include "../Core/RendererFactory.h"
#include "../Core/MidiConfig.h"

#include <memory>
#include <string>
#include <vector>

namespace {

using namespace foo_midi;

enum { kBlockFrames = 4096 };

class input_midi : public input_stubs {
public:
    void open(service_ptr_t<file> p_filehint, const char* p_path,
              t_input_open_reason p_reason, abort_callback& p_abort) {
        if (p_reason == input_open_info_write) throw exception_tagging_unsupported();

        m_file = p_filehint;
        input_open_file_helper(m_file, p_path, p_reason, p_abort);

        // Slurp the whole file; SMF files are small and FluidSynth wants it in memory.
        t_filesize size64 = m_file->get_size_ex(p_abort);
        size_t size = (size_t)size64;
        m_data.resize(size);
        size_t total = 0;
        while (total < size) {
            size_t got = m_file->read(m_data.data() + total, size - total, p_abort);
            if (got == 0) break;
            total += got;
        }
        m_data.resize(total);

        // The CLAP-enabled build retains the channel-voice event stream so a
        // hosted plugin can be fed; the FluidSynth-only build discards it
        // (FluidSynth parses the SMF itself).
#if FOO_TUN_MIDI_CLAP
        constexpr bool kKeepEvents = true;
#else
        constexpr bool kKeepEvents = false;
#endif
        m_smf = parseSMF(m_data.data(), m_data.size(), kKeepEvents);
        if (!m_smf.valid || m_data.empty()) throw exception_io_data();
    }

    void get_info(file_info& p_info, abort_callback& p_abort) {
        if (m_smf.valid && m_smf.durationSeconds > 0) {
            p_info.set_length(m_smf.durationSeconds);
        }
        p_info.info_set_int("samplerate", midi_config::sampleRate());
        p_info.info_set_int("channels", IMidiRenderer::kChannels);
        p_info.info_set("encoding", "synthesized");
        p_info.info_set("codec", "MIDI");
        if (!m_smf.valid) return;

        p_info.info_set_int("midi_format", m_smf.format);
        p_info.info_set_int("midi_tracks", m_smf.numTracks);

        // Musical metadata parsed from the SMF.
        if (m_smf.initialBpm > 0)
            p_info.info_set_int("midi_tempo", (t_int64)(m_smf.initialBpm + 0.5));
        if (m_smf.timeSigNum > 0) {
            pfc::string_formatter ts;
            ts << m_smf.timeSigNum << "/" << m_smf.timeSigDen;
            p_info.info_set("midi_time_signature", ts);
        }
        if (!m_smf.keySignature.empty())
            p_info.info_set("midi_key", m_smf.keySignature.c_str());
        if (!m_smf.instrumentNames.empty()) {
            pfc::string_formatter instr;
            for (const auto& n : m_smf.instrumentNames) {
                if (!instr.is_empty()) instr << ", ";
                instr << n.c_str();
            }
            p_info.info_set("midi_instruments", instr);
        }

        // Tag-style fields (shown as track metadata). Only set when present so we
        // don't blank out foobar's filename-derived title.
        if (!m_smf.sequenceName.empty())
            p_info.meta_set("title", m_smf.sequenceName.c_str());
        if (!m_smf.copyright.empty())
            p_info.meta_set("copyright", m_smf.copyright.c_str());
    }

    t_filestats2 get_stats2(unsigned f, abort_callback& a) { return m_file->get_stats2_(f, a); }
    t_filestats get_file_stats(abort_callback& a) { return m_file->get_stats(a); }

    void decode_initialize(unsigned /*p_flags*/, abort_callback& /*p_abort*/) {
        // The factory picks the backend (FluidSynth, or CLAP in the Full build)
        // and builds its key/config from preferences, keeping this decoder
        // backend-agnostic. It logs the specific reason on failure.
        m_renderer = createRenderer(m_data.data(), m_data.size(), m_smf);
        if (!m_renderer) throw exception_io_data();
        m_block.resize((size_t)kBlockFrames * IMidiRenderer::kChannels);
    }

    bool decode_run(audio_chunk& p_chunk, abort_callback& /*p_abort*/) {
        if (!m_renderer) return false;
        int frames = m_renderer->render(m_block.data(), kBlockFrames);
        if (frames <= 0) return false;
        p_chunk.set_data_32(m_block.data(), (size_t)frames,
                            IMidiRenderer::kChannels, m_renderer->sampleRate());
        return true;
    }

    void decode_seek(double p_seconds, abort_callback& /*p_abort*/) {
        if (m_renderer) m_renderer->seek(p_seconds);
    }

    // Reflect the active backend's capability: FluidSynth seeks; CLAP does not,
    // so foobar2000 disables the seekbar for CLAP-backed files.
    bool decode_can_seek() {
        return m_smf.valid && (!m_renderer || m_renderer->supportsSeek());
    }

    void retag(const file_info&, abort_callback&) { throw exception_tagging_unsupported(); }
    void remove_tags(abort_callback&) { throw exception_tagging_unsupported(); }

    static bool g_is_our_content_type(const char* p_type) {
        return stricmp_utf8(p_type, "audio/midi") == 0 ||
               stricmp_utf8(p_type, "audio/x-midi") == 0;
    }
    static bool g_is_our_path(const char* /*p_path*/, const char* p_ext) {
        return stricmp_utf8(p_ext, "mid") == 0 ||
               stricmp_utf8(p_ext, "midi") == 0 ||
               stricmp_utf8(p_ext, "kar") == 0 ||
               stricmp_utf8(p_ext, "smf") == 0;
    }
    static const char* g_get_name() { return "MIDI Player (FluidSynth)"; }
    static GUID g_get_guid() {
        // Unique to this component. Regenerate if you fork.
        static const GUID guid =
            { 0x7a3f1e64, 0x2b9c, 0x4d81, { 0xa5, 0x0e, 0x9c, 0x71, 0x3d, 0x62, 0x8f, 0x14 } };
        return guid;
    }

private:
    service_ptr_t<file> m_file;
    std::vector<uint8_t> m_data;
    SMFInfo m_smf;
    std::unique_ptr<IMidiRenderer> m_renderer;
    std::vector<float> m_block;
};

static input_singletrack_factory_t<input_midi> g_input_midi_factory;

DECLARE_FILE_TYPE("MIDI files", "*.MID;*.MIDI;*.KAR;*.SMF");

} // namespace
