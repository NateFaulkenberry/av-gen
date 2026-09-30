#include "sonic/notes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <fstream>
#include <iterator>
#include <map>

namespace avgen::sonic {

// ---- the track ---------------------------------------------------------------------------------------------------

double NoteTrack::endSeconds() const {
    double end = 0.0;
    for (const NoteEvent& n : notes) {
        end = std::max(end, n.end());
    }
    return end;
}

void NoteTrack::finish() {
    std::stable_sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
        return a.start != b.start ? a.start < b.start : a.key < b.key;
    });
    longest = 0.0;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        notes[i].id = static_cast<std::uint32_t>(i);
        longest = std::max(longest, notes[i].duration);
    }
}

namespace {

struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t pos = 0;

    [[nodiscard]] bool has(std::size_t n) const { return pos + n <= bytes.size(); }
    std::uint32_t be(int n) {
        std::uint32_t v = 0;
        for (int i = 0; i < n; ++i) {
            v = (v << 8u) | bytes[pos++];
        }
        return v;
    }
    bool vlq(std::uint32_t& out) {
        out = 0;
        for (int i = 0; i < 4; ++i) {
            if (!has(1)) {
                return false;
            }
            const std::uint8_t b = bytes[pos++];
            out = (out << 7u) | (b & 0x7Fu);
            if ((b & 0x80u) == 0) {
                return true;
            }
        }
        return false;
    }
};

struct RawNote {
    std::uint64_t onTick = 0;
    std::uint64_t offTick = 0;
    std::uint8_t channel = 0;
    std::uint8_t key = 0;
    std::uint8_t velocity = 0;
};

} // namespace

Result<NoteTrack> NoteTrack::fromMidiBytes(std::span<const std::uint8_t> bytes) {
    Reader r{bytes};
    if (!r.has(14) || std::string(reinterpret_cast<const char*>(bytes.data()), 4) != "MThd") {
        return fail("not a Standard MIDI File (no MThd header)");
    }
    r.pos = 4;
    const std::uint32_t headerLength = r.be(4);
    if (headerLength < 6 || !r.has(headerLength)) {
        return fail("MIDI header is truncated");
    }
    const std::uint32_t format = r.be(2);
    const std::uint32_t trackCount = r.be(2);
    const std::uint32_t division = r.be(2);
    r.pos = 8 + headerLength;
    if (format > 1) {
        return fail("MIDI format {} is not supported (0 and 1 are)", format);
    }
    if ((division & 0x8000u) != 0 || division == 0) {
        return fail("SMPTE-timed MIDI files are not supported");
    }
    std::map<std::uint64_t, std::uint32_t> tempo; // tick -> microseconds per quarter
    std::vector<RawNote> raw;
    for (std::uint32_t t = 0; t < trackCount && r.has(8); ++t) {
        const bool isTrack = std::string(reinterpret_cast<const char*>(bytes.data() + r.pos), 4) == "MTrk";
        r.pos += 4;
        const std::uint32_t length = r.be(4);
        if (!r.has(length)) {
            return fail("MIDI track {} is truncated", t);
        }
        const std::size_t end = r.pos + length;
        if (!isTrack) {
            r.pos = end; // an unknown chunk: skipped, as the standard says
            continue;
        }
        std::uint64_t tick = 0;
        std::uint8_t running = 0;
        std::map<std::uint16_t, std::deque<std::pair<std::uint64_t, std::uint8_t>>> open; // (channel<<8|key)
        while (r.pos < end) {
            std::uint32_t delta = 0;
            if (!r.vlq(delta)) {
                return fail("bad delta time in MIDI track {}", t);
            }
            tick += delta;
            if (!r.has(1)) {
                break;
            }
            std::uint8_t status = bytes[r.pos];
            if (status & 0x80u) {
                ++r.pos;
            } else if (running != 0) {
                status = running;
            } else {
                return fail("MIDI data byte with no running status in track {}", t);
            }
            if (status == 0xFF) {
                if (!r.has(1)) {
                    break;
                }
                const std::uint8_t type = bytes[r.pos++];
                std::uint32_t len = 0;
                if (!r.vlq(len) || !r.has(len)) {
                    return fail("bad meta event in MIDI track {}", t);
                }
                if (type == 0x51 && len == 3) {
                    tempo[tick] = r.be(3);
                } else {
                    r.pos += len;
                }
                if (type == 0x2F) {
                    break;
                }
                continue;
            }
            if (status == 0xF0 || status == 0xF7) {
                std::uint32_t len = 0;
                if (!r.vlq(len) || !r.has(len)) {
                    return fail("bad sysex in MIDI track {}", t);
                }
                r.pos += len;
                continue;
            }
            running = status;
            const std::uint8_t kind = status & 0xF0u;
            const std::uint8_t channel = status & 0x0Fu;
            const int dataBytes = (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
            if (!r.has(static_cast<std::size_t>(dataBytes))) {
                break;
            }
            const std::uint8_t d1 = bytes[r.pos] & 0x7Fu;
            const std::uint8_t d2 = dataBytes == 2 ? (bytes[r.pos + 1] & 0x7Fu) : 0;
            r.pos += static_cast<std::size_t>(dataBytes);
            const auto slot = static_cast<std::uint16_t>((channel << 8u) | d1);
            if (kind == 0x90 && d2 > 0) {
                open[slot].emplace_back(tick, d2);
            } else if (kind == 0x80 || (kind == 0x90 && d2 == 0)) {
                auto it = open.find(slot);
                if (it != open.end() && !it->second.empty()) {
                    const auto [onTick, velocity] = it->second.front();
                    it->second.pop_front();
                    raw.push_back({onTick, tick, channel, d1, velocity});
                }
            }
        }
        // Notes never released end with their track.
        for (auto& [slot, queue] : open) {
            for (const auto& [onTick, velocity] : queue) {
                raw.push_back({onTick, tick, static_cast<std::uint8_t>(slot >> 8u),
                               static_cast<std::uint8_t>(slot & 0xFFu), velocity});
            }
        }
        r.pos = end;
    }
    // The tempo map: ticks to seconds, piecewise linear.
    if (tempo.empty() || tempo.begin()->first != 0) {
        tempo.emplace(0, 500000u);
    }
    struct Segment {
        std::uint64_t tick;
        double seconds;
        double secondsPerTick;
    };
    std::vector<Segment> segments;
    double seconds = 0.0;
    std::uint64_t lastTick = 0;
    double lastRate = 0.0;
    for (const auto& [tick, microsPerQuarter] : tempo) {
        if (!segments.empty()) {
            seconds += static_cast<double>(tick - lastTick) * lastRate;
        }
        lastRate = static_cast<double>(microsPerQuarter) * 1e-6 / static_cast<double>(division);
        segments.push_back({tick, seconds, lastRate});
        lastTick = tick;
    }
    const auto toSeconds = [&](std::uint64_t tick) {
        auto it = std::upper_bound(segments.begin(), segments.end(), tick,
                                   [](std::uint64_t v, const Segment& s) { return v < s.tick; });
        const Segment& s = *std::prev(it);
        return s.seconds + static_cast<double>(tick - s.tick) * s.secondsPerTick;
    };
    NoteTrack track;
    track.notes.reserve(raw.size());
    for (const RawNote& n : raw) {
        NoteEvent e;
        e.start = toSeconds(n.onTick);
        e.duration = std::max(0.0, toSeconds(n.offTick) - e.start);
        e.velocity = static_cast<float>(n.velocity) / 127.0f;
        e.key = n.key;
        e.pitch = static_cast<float>(n.key);
        e.channel = n.channel;
        track.notes.push_back(e);
    }
    track.finish();
    return track;
}

Result<NoteTrack> NoteTrack::fromMidiFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return fail("cannot open MIDI file '{}'", path.string());
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto track = fromMidiBytes(bytes);
    if (!track) {
        return fail("'{}': {}", path.filename().string(), track.error().message);
    }
    return track;
}

// ---- the context -------------------------------------------------------------------------------------------------

namespace {

// Onsets closer than `chordSeconds` are one onset: a chord, strummed or not.
struct Group {
    double start = 0.0;
    double end = 0.0;    // the latest end of its notes
    float pitch = 0.0f;  // mean pitch
    int size = 0;
    std::size_t first = 0; // note index range [first, last)
    std::size_t last = 0;
};
constexpr std::size_t kMaxGroups = 96;

std::size_t firstStartingAfter(const NoteTrack& track, double seconds) {
    return static_cast<std::size_t>(
        std::upper_bound(track.notes.begin(), track.notes.end(), seconds,
                         [](double s, const NoteEvent& n) { return s < n.start; }) -
        track.notes.begin());
}

std::size_t firstStartingAtOrAfter(const NoteTrack& track, double seconds) {
    return static_cast<std::size_t>(
        std::lower_bound(track.notes.begin(), track.notes.end(), seconds,
                         [](const NoteEvent& n, double s) { return n.start < s; }) -
        track.notes.begin());
}

// The dissonance of an interval class (semitones mod 12, folded to 0..6): minor second and tritone the most,
// fifth and fourth the least. A conventional ranking, not a measurement (RESEARCH.md §4.3).
constexpr std::array<float, 7> kIntervalTension{0.0f, 1.0f, 0.6f, 0.2f, 0.15f, 0.05f, 0.8f};

} // namespace

MusicalContext contextAt(const NoteTrack& track, double t, const ContextSettings& s) {
    MusicalContext c;
    if (track.empty()) {
        return c;
    }
    const std::size_t end = firstStartingAfter(track, t); // notes [0, end) have begun
    const double tau = std::max(s.tau, 1e-3);
    const auto weight = [&](double start) { return std::exp(-(t - start) / tau); };

    // Sounding notes: begun, not ended. None can have begun before t - longest.
    const std::size_t soundFrom = firstStartingAtOrAfter(track, t - track.longest - 1e-9);
    std::array<float, 128> soundingPitch{};
    int sounding = 0;
    double soundVelocity = 0.0;
    double soundPitch = 0.0;
    float lowest = 1e9f;
    float highest = -1e9f;
    for (std::size_t i = soundFrom; i < end; ++i) {
        const NoteEvent& n = track.notes[i];
        if (n.end() > t) {
            if (sounding < static_cast<int>(soundingPitch.size())) {
                soundingPitch[static_cast<std::size_t>(sounding)] = n.pitch;
            }
            ++sounding;
            soundVelocity += n.velocity;
            soundPitch += n.pitch;
            lowest = std::min(lowest, n.pitch);
            highest = std::max(highest, n.pitch);
        }
    }
    c.active = sounding;

    // Recent onsets, grouped.
    const std::size_t from = firstStartingAfter(track, t - s.window);
    std::array<Group, kMaxGroups> groups{};
    std::size_t groupCount = 0;
    {
        std::size_t i = from;
        // Only the newest kMaxGroups groups fit; older ones carry the least weight anyway.
        while (i < end) {
            Group g;
            g.start = track.notes[i].start;
            g.first = i;
            double pitch = 0.0;
            while (i < end && track.notes[i].start - g.start <= s.chordSeconds) {
                pitch += track.notes[i].pitch;
                g.end = std::max(g.end, track.notes[i].end());
                ++i;
            }
            g.last = i;
            g.size = static_cast<int>(g.last - g.first);
            g.pitch = static_cast<float>(pitch / g.size);
            if (groupCount == kMaxGroups) {
                std::move(groups.begin() + 1, groups.end(), groups.begin());
                --groupCount;
            }
            groups[groupCount++] = g;
        }
    }

    double noteWeight = 0.0;
    double velocity = 0.0;
    double pitch = 0.0;
    double held = 0.0;
    double chordWeight = 0.0;
    for (std::size_t gi = 0; gi < groupCount; ++gi) {
        const Group& g = groups[gi];
        const double w = weight(g.start);
        c.rhythm += static_cast<float>(w);
        for (std::size_t i = g.first; i < g.last; ++i) {
            const NoteEvent& n = track.notes[i];
            noteWeight += w;
            velocity += w * n.velocity;
            pitch += w * n.pitch;
            held += w * std::min(n.duration, t - n.start);
            if (g.size > 1) {
                chordWeight += w;
            }
            if (t - n.start <= s.rangeSeconds) {
                lowest = std::min(lowest, n.pitch);
                highest = std::max(highest, n.pitch);
            }
        }
    }
    c.density = static_cast<float>(noteWeight / tau);
    c.rhythm = static_cast<float>(c.rhythm / tau);
    if (noteWeight > 1e-6) {
        c.velocity = static_cast<float>(velocity / noteWeight);
        c.duration = static_cast<float>(held / noteWeight);
        c.chord = static_cast<float>(chordWeight / noteWeight);
    } else if (sounding > 0) {
        c.velocity = static_cast<float>(soundVelocity / sounding);
    }
    if (sounding > 0) {
        c.pitch = static_cast<float>(soundPitch / sounding);
    } else if (noteWeight > 1e-6) {
        c.pitch = static_cast<float>(pitch / noteWeight);
    } else if (end > 0) {
        c.pitch = track.notes[end - 1].pitch; // the last note ever heard: a pitch centre persists through rests
    }
    if (highest >= lowest) {
        c.lowest = lowest;
        c.highest = highest;
        c.range = highest - lowest;
    }

    // Successive onsets: motion, direction, legato, regularity, repetition.
    double stepWeight = 0.0;
    double absMotion = 0.0;
    double signedMotion = 0.0;
    double legato = 0.0;
    double repeatWeight = 0.0;
    double repeated = 0.0;
    double ioiWeight = 0.0;
    double ioiMean = 0.0;
    double ioiSquare = 0.0;
    int iois = 0;
    for (std::size_t gi = 0; gi < groupCount; ++gi) {
        const Group& g = groups[gi];
        const double w = weight(g.start);
        const int lookback = std::max(1, s.repeatLookback);
        bool repeats = false;
        for (std::size_t back = 1; back <= static_cast<std::size_t>(lookback) && back <= gi && !repeats; ++back) {
            const Group& p = groups[gi - back];
            for (std::size_t i = g.first; i < g.last && !repeats; ++i) {
                for (std::size_t j = p.first; j < p.last; ++j) {
                    if (track.notes[i].key == track.notes[j].key) {
                        repeats = true;
                        break;
                    }
                }
            }
        }
        if (gi == 0) {
            continue;
        }
        const Group& prev = groups[gi - 1];
        const double step = static_cast<double>(g.pitch) - static_cast<double>(prev.pitch);
        stepWeight += w;
        absMotion += w * std::abs(step);
        signedMotion += w * step;
        legato += prev.end > g.start + 1e-6 ? w : 0.0;
        repeatWeight += w;
        repeated += repeats ? w : 0.0;
        const double ioi = g.start - prev.start;
        ioiWeight += w;
        ioiMean += w * ioi;
        ioiSquare += w * ioi * ioi;
        ++iois;
    }
    if (stepWeight > 1e-6) {
        c.motion = static_cast<float>(absMotion / stepWeight);
        c.direction = absMotion > 1e-9 ? static_cast<float>(signedMotion / absMotion) : 0.0f;
        c.legato = static_cast<float>(legato / stepWeight);
        c.repetition = static_cast<float>(repeated / repeatWeight);
    }
    if (iois >= 3 && ioiWeight > 1e-9) {
        const double mean = ioiMean / ioiWeight;
        const double var = std::max(0.0, ioiSquare / ioiWeight - mean * mean);
        c.regularity = mean > 1e-9 ? static_cast<float>(std::clamp(1.0 - std::sqrt(var) / mean, 0.0, 1.0)) : 0.0f;
    }

    // Tension of what is sounding: the mean interval-class dissonance over every pair.
    const int held_ = std::min(sounding, static_cast<int>(soundingPitch.size()));
    if (held_ >= 2) {
        double sum = 0.0;
        int pairs = 0;
        for (int i = 0; i < held_; ++i) {
            for (int j = i + 1; j < held_; ++j) {
                const int semis = static_cast<int>(std::lround(std::abs(soundingPitch[static_cast<std::size_t>(i)] -
                                                                        soundingPitch[static_cast<std::size_t>(j)]))) % 12;
                const int ic = std::min(semis, 12 - semis);
                sum += kIntervalTension[static_cast<std::size_t>(ic)];
                ++pairs;
            }
        }
        c.tension = static_cast<float>(sum / pairs);
    }

    // Phrase activity: how much of the last phraseSeconds had something sounding (a union of intervals).
    {
        const double lo = t - s.phraseSeconds;
        const std::size_t i0 = firstStartingAtOrAfter(track, lo - track.longest - 1e-9);
        double covered = 0.0;
        double reach = lo;
        for (std::size_t i = i0; i < end; ++i) {
            const NoteEvent& n = track.notes[i];
            const double a = std::max(n.start, reach);
            const double b = std::min(n.end(), t);
            if (b > a) {
                covered += b - a;
                reach = b;
            }
        }
        c.phrase = static_cast<float>(std::clamp(covered / std::max(s.phraseSeconds, 1e-3), 0.0, 1.0));
    }
    return c;
}

NoteEvents eventsBetween(const NoteTrack& track, double from, double to, const ContextSettings& s) {
    NoteEvents e;
    if (track.empty() || !(to > from)) {
        return e;
    }
    const std::size_t first = firstStartingAfter(track, from);
    const std::size_t last = firstStartingAfter(track, to);
    for (std::size_t i = first; i < last; ++i) {
        const NoteEvent& n = track.notes[i];
        e.noteOn = true;
        ++e.onCount;
        e.onVelocity = std::max(e.onVelocity, n.velocity);
        // A phrase starts on an onset after phraseGapSeconds in which nothing sounded.
        bool quiet = true;
        const std::size_t j0 = firstStartingAtOrAfter(track, n.start - s.phraseGapSeconds - track.longest - 1e-9);
        for (std::size_t j = j0; j < track.notes.size() && track.notes[j].start < n.start - s.chordSeconds; ++j) {
            if (track.notes[j].end() > n.start - s.phraseGapSeconds) {
                quiet = false;
                break;
            }
        }
        e.phraseStart = e.phraseStart || quiet;
    }
    const std::size_t o0 = firstStartingAtOrAfter(track, from - track.longest - 1e-9);
    for (std::size_t i = o0; i < last; ++i) {
        const double endAt = track.notes[i].end();
        if (endAt > from && endAt <= to) {
            e.noteOff = true;
            break;
        }
    }
    return e;
}

std::string pitchName(float midi) {
    static constexpr std::array<const char*, 12> kNames{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int n = static_cast<int>(std::lround(midi));
    if (n <= 0) {
        return "-";
    }
    return std::string(kNames[static_cast<std::size_t>(n % 12)]) + std::to_string(n / 12 - 1);
}

} // namespace avgen::sonic
