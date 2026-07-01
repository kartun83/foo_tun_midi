//
//  ClapRenderer.h
//  foo_tun_midi (CLAP-enabled / "Full" build only — FOO_TUN_MIDI_CLAP)
//
//  IMidiRenderer backed by a hosted CLAP instrument plugin. Feeds the parsed
//  SMF's channel-voice events into the plugin at their sample offsets and pulls
//  the plugin's main stereo output (offline, block by block). Single-plugin
//  preview — not multi-timbral GM; there is no seeking (supportsSeek() == false)
//  and no percussion routing (the plugin owns its own note→sound mapping).
//
//  The host model here is the one proven by the standalone spike: load every
//  declared audio port (inputs included, or JUCE-wrapped plugins crash), and
//  send MIDI-dialect note events unless the plugin's note port prefers CLAP.
//
//  CLAP details are hidden behind a PIMPL so this header (and RendererFactory)
//  needn't see <clap/clap.h>.
//

#pragma once

#include "../Core/IMidiRenderer.h"

#include <memory>
#include <string>

namespace foo_midi {

struct SMFInfo;

class ClapRenderer : public IMidiRenderer {
public:
    ClapRenderer();
    ~ClapRenderer() override;

    ClapRenderer(const ClapRenderer&) = delete;
    ClapRenderer& operator=(const ClapRenderer&) = delete;

    // Load + activate a plugin from the .clap bundle at `pluginPath` at
    // `sampleRate` and schedule the parsed events from `smf` (which must have
    // been parsed with keepEvents=true). `pluginId` selects which plugin in the
    // bundle to host (empty = the first). Returns false if it can't be loaded.
    bool init(const std::string& pluginPath, const std::string& pluginId,
              int sampleRate, const SMFInfo& smf);

    int sampleRate() const override;
    bool supportsSeek() const override { return false; }
    void seek(double /*seconds*/) override {}   // non-seekable by design
    int render(float* out, int frames) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace foo_midi
