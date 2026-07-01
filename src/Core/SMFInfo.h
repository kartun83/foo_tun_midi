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
#include <string>
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

    // Flattened channel-voice event stream (note on/off, CC, program, pitch,
    // pressure), across all tracks, sorted ascending by absolute tick. Populated
    // only when parseSMF(..., keepEvents=true) — the CLAP backend feeds these to
    // a hosted instrument. Empty otherwise (FluidSynth parses the SMF itself).
    struct ChannelEvent {
        uint32_t tick;   // absolute tick
        uint8_t status;  // full status byte incl. channel (0x80..0xEF)
        uint8_t d1;      // data byte 1
        uint8_t d2;      // data byte 2 (0 for 1-byte messages)
    };
    std::vector<ChannelEvent> events;

    // Tempo map, sorted ascending by tick. Always has an entry at tick 0.
    struct TempoEntry {
        uint32_t tick;
        uint32_t usPerQuarter;   // microseconds per quarter note
        double secondsAtTick;    // absolute seconds at `tick`
    };
    std::vector<TempoEntry> tempoMap;

    // --- Metadata (best-effort; first occurrence wins for single-valued ones) ---
    double initialBpm = 0;                   // tempo at tick 0
    int timeSigNum = 0, timeSigDen = 0;      // FF 58 (0/0 if absent)
    std::string keySignature;                // FF 59 rendered, e.g. "A minor"
    std::string sequenceName;                // first FF 03 (track/sequence name)
    std::vector<std::string> instrumentNames;// FF 04, de-duplicated
    std::string copyright;                   // FF 02

    // --- Signals for drum-file auto-detection ---
    uint32_t noteCount = 0;          // note-on events (velocity > 0)
    uint32_t drumRangeNoteCount = 0; // of those, notes in the GM drum range on
                                     // a non-drum channel
    bool usesDrumChannel = false;    // any note on channel 10 (index 9)
    bool hasProgramChange = false;   // any program-change event anywhere

    // Heuristic: the file appears to be a drum pattern whose notes were placed
    // on a melodic channel with no program change (common in DAW drum-rack
    // exports), so it would render as piano under General MIDI. Such files
    // benefit from forcing percussion; normal GM files do not match.
    bool looksLikePercussionMisrouted() const {
        return noteCount > 0 && !usesDrumChannel && !hasProgramChange &&
               drumRangeNoteCount * 100 >= noteCount * 80;
    }

    double tickToSeconds(uint32_t tick) const;
    uint32_t secondsToTick(double seconds) const;
};

// Parse an in-memory SMF. On failure returns SMFInfo{ .valid = false }.
// When keepEvents is true, the flattened channel-voice event stream is retained
// in SMFInfo::events (for the CLAP backend); it is discarded otherwise.
SMFInfo parseSMF(const uint8_t* data, size_t size, bool keepEvents = false);

} // namespace foo_midi
