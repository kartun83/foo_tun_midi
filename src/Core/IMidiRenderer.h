//
//  IMidiRenderer.h
//  foo_tun_midi
//
//  Backend-agnostic rendering interface. A renderer turns a parsed MIDI file
//  into interleaved-stereo float audio via a pull model: construct + init the
//  concrete backend, then repeatedly render() blocks until it returns 0.
//
//  Deliberately minimal. Concepts that only make sense for one backend
//  (percussion routing, the FluidSynth engine cache, CLAP plugin paths) stay
//  inside that backend and out of this interface. Seeking is a *capability* a
//  backend declares via supportsSeek() rather than a method every backend must
//  meaningfully implement — MidiInput reports the input as non-seekable when a
//  backend returns false, so foobar2000 just disables the seekbar.
//

#pragma once

#include <cstddef>

namespace foo_midi {

class IMidiRenderer {
public:
    virtual ~IMidiRenderer() = default;

    // All backends produce interleaved stereo.
    static constexpr int kChannels = 2;

    // The rate this backend renders at (Hz). foobar2000 resamples to the device.
    virtual int sampleRate() const = 0;

    // Whether seek() does anything meaningful. When false, the input is reported
    // non-seekable and seek() is a no-op.
    virtual bool supportsSeek() const = 0;

    // Jump to an absolute position in seconds. No-op if !supportsSeek().
    virtual void seek(double seconds) = 0;

    // Render up to `frames` stereo frames into `out` (must hold 2*frames floats).
    // Returns frames produced; 0 once the song and its (trimmed) tail are done.
    virtual int render(float* out, int frames) = 0;
};

} // namespace foo_midi
