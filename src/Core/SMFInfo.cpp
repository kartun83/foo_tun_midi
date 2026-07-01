//
//  SMFInfo.cpp
//  foo_jl_midi_mac
//

#include "SMFInfo.h"
#include <algorithm>

namespace foo_midi {

namespace {

// Bounds-checked big-endian readers over a cursor.
struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool ok = true;

    bool has(size_t n) const { return (size_t)(end - p) >= n; }

    uint8_t u8() {
        if (!has(1)) { ok = false; return 0; }
        return *p++;
    }
    uint32_t be(size_t n) {
        uint32_t v = 0;
        for (size_t i = 0; i < n; i++) v = (v << 8) | u8();
        return v;
    }
    // MIDI variable-length quantity.
    uint32_t vlq() {
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) {
            uint8_t b = u8();
            v = (v << 7) | (b & 0x7F);
            if (!(b & 0x80)) break;
        }
        return v;
    }
    void skip(size_t n) {
        if (!has(n)) { ok = false; p = end; return; }
        p += n;
    }
};

struct RawTempo { uint32_t tick; uint32_t us; };

} // namespace

double SMFInfo::tickToSeconds(uint32_t tick) const {
    if (isSMPTE) {
        return smpteTicksPerSecond > 0 ? (double)tick / smpteTicksPerSecond : 0.0;
    }
    if (tempoMap.empty() || ppq <= 0) return 0.0;
    // Last tempo entry at or before `tick`.
    size_t i = 0;
    for (size_t j = 0; j < tempoMap.size(); j++) {
        if (tempoMap[j].tick <= tick) i = j; else break;
    }
    const TempoEntry& e = tempoMap[i];
    double dq = (double)(tick - e.tick) / (double)ppq; // quarter notes
    return e.secondsAtTick + dq * (double)e.usPerQuarter / 1e6;
}

uint32_t SMFInfo::secondsToTick(double seconds) const {
    if (seconds <= 0) return 0;
    if (isSMPTE) {
        return (uint32_t)(seconds * smpteTicksPerSecond + 0.5);
    }
    if (tempoMap.empty() || ppq <= 0) return 0;
    size_t i = 0;
    for (size_t j = 0; j < tempoMap.size(); j++) {
        if (tempoMap[j].secondsAtTick <= seconds) i = j; else break;
    }
    const TempoEntry& e = tempoMap[i];
    double dsec = seconds - e.secondsAtTick;
    double dq = dsec * 1e6 / (double)e.usPerQuarter; // quarter notes
    return e.tick + (uint32_t)(dq * (double)ppq + 0.5);
}

SMFInfo parseSMF(const uint8_t* data, size_t size) {
    SMFInfo out;
    Reader r{ data, data + size };

    // Header chunk: "MThd" <len:4> <format:2> <ntrks:2> <division:2>
    if (r.be(4) != 0x4D546864 /* 'MThd' */) return out;
    uint32_t hlen = r.be(4);
    if (hlen < 6 || !r.has(hlen)) return out;
    const uint8_t* afterHeader = r.p + hlen;
    out.format = (int)r.be(2);
    out.numTracks = (int)r.be(2);
    out.rawDivision = (int)(int16_t)r.be(2);
    r.p = afterHeader; // tolerate oversized header

    if (out.rawDivision > 0) {
        out.ppq = out.rawDivision;
    } else {
        // SMPTE: high byte is negative frames-per-second, low byte ticks/frame.
        int fps = 256 - ((out.rawDivision >> 8) & 0xFF);
        int ticksPerFrame = out.rawDivision & 0xFF;
        out.isSMPTE = true;
        out.smpteTicksPerSecond = (double)fps * (double)ticksPerFrame;
    }

    std::vector<RawTempo> tempos;
    uint32_t maxTick = 0;

    for (int t = 0; t < out.numTracks && r.ok; t++) {
        // Find a track chunk; skip unknown chunk types.
        uint32_t id = r.be(4);
        uint32_t len = r.be(4);
        if (!r.ok || !r.has(len)) break;
        const uint8_t* trackEnd = r.p + len;
        if (id != 0x4D54726B /* 'MTrk' */) { r.p = trackEnd; continue; }

        uint32_t tick = 0;
        uint8_t runningStatus = 0;
        Reader tr{ r.p, trackEnd };
        while (tr.ok && tr.p < tr.end) {
            tick += tr.vlq();
            uint8_t status = tr.u8();
            if (status < 0x80) {
                // Running status: `status` was actually the first data byte.
                if (runningStatus == 0) break;
                tr.p--;              // put the data byte back
                status = runningStatus;
            } else if (status < 0xF0) {
                runningStatus = status;
            }

            if (status == 0xFF) {           // meta event
                uint8_t type = tr.u8();
                uint32_t mlen = tr.vlq();
                if (type == 0x51 && mlen == 3) { // set tempo
                    uint32_t us = tr.be(3);
                    tempos.push_back({ tick, us ? us : 500000 });
                } else {
                    tr.skip(mlen);
                }
            } else if (status == 0xF0 || status == 0xF7) { // sysex
                uint32_t slen = tr.vlq();
                tr.skip(slen);
            } else {
                // Channel voice/mode message: 2 data bytes except program
                // change (0xC) and channel pressure (0xD) which have 1.
                uint8_t hi = status & 0xF0;
                if (hi == 0xC0 || hi == 0xD0) tr.u8();
                else { tr.u8(); tr.u8(); }
            }
        }
        maxTick = std::max(maxTick, tick);
        r.p = trackEnd;
    }

    out.totalTicks = maxTick;

    // Build the tempo map (PPQ only; SMPTE ignores tempo).
    std::sort(tempos.begin(), tempos.end(),
              [](const RawTempo& a, const RawTempo& b) { return a.tick < b.tick; });

    out.tempoMap.push_back({ 0, 500000, 0.0 });
    if (!out.isSMPTE && out.ppq > 0) {
        for (const RawTempo& tp : tempos) {
            if (tp.tick == 0) { out.tempoMap[0].usPerQuarter = tp.us; continue; }
            const SMFInfo::TempoEntry& prev = out.tempoMap.back();
            if (tp.tick == prev.tick) { out.tempoMap.back().usPerQuarter = tp.us; continue; }
            double dq = (double)(tp.tick - prev.tick) / (double)out.ppq;
            double sec = prev.secondsAtTick + dq * (double)prev.usPerQuarter / 1e6;
            out.tempoMap.push_back({ tp.tick, tp.us, sec });
        }
    }

    out.durationSeconds = out.tickToSeconds(out.totalTicks);
    out.valid = true;
    return out;
}

} // namespace foo_midi
