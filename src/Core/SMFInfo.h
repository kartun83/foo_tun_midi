//
//  SMFInfo.h
//  foo_jl_midi_mac
//
//  Minimal Standard MIDI File parser: extracts format/division, builds a
//  tempo map, and computes total duration. Enough for playlist info and
//  time<->tick conversion for seeking. FluidSynth does the actual note
//  parsing/rendering; this only needs the timing skeleton.
//

#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace foo_midi {

struct SMFInfo {
    bool valid = false;
    int format = 0;         // 0, 1, or 2
    int numTracks = 0;
    int rawDivision = 0;    // MThd division word as-is

    bool isSMPTE = false;
    int ppq = 0;                    // ticks per quarter note (PPQ mode)
    double smpteTicksPerSecond = 0; // (SMPTE mode)

    uint32_t totalTicks = 0;        // end tick of the longest track
    double durationSeconds = 0;

    // Tempo map, sorted ascending by tick. Always has an entry at tick 0.
    struct TempoEntry {
        uint32_t tick;
        uint32_t usPerQuarter;   // microseconds per quarter note
        double secondsAtTick;    // absolute seconds at `tick`
    };
    std::vector<TempoEntry> tempoMap;

    double tickToSeconds(uint32_t tick) const;
    uint32_t secondsToTick(double seconds) const;
};

// Parse an in-memory SMF. On failure returns SMFInfo{ .valid = false }.
SMFInfo parseSMF(const uint8_t* data, size_t size);

} // namespace foo_midi
