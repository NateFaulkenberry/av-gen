#include "signals/stem_signals.hpp"

#include <algorithm>

namespace avgen::signals {

namespace {

// name suffix, min, max, event, label suffix -- the one table both `declare` and `names` read.
struct Field {
    const char* suffix;
    float minValue;
    float maxValue;
    bool event;
    const char* label;
};
constexpr std::array<Field, 19> kFields{{
    {"active", 0.0f, 1.0f, false, "has audio now (0 = no data: outside the stem, absent or disconnected)"},
    {"rms", 0.0f, 1.0f, false, "loudness (RMS, unsmoothed)"},
    {"peak", 0.0f, 1.0f, false, "peak"},
    {"level", 0.0f, 1.0f, false, "level (against its own loudness)"},
    {"onset", 0.0f, 1.0f, true, "onset (any attack)"},
    {"onsetStrength", 0.0f, 4.0f, false, "onset strength"},
    {"bass", 0.0f, 1.0f, false, "bass band"},
    {"lowMid", 0.0f, 1.0f, false, "low-mid band"},
    {"mid", 0.0f, 1.0f, false, "mid band"},
    {"highMid", 0.0f, 1.0f, false, "high-mid band"},
    {"treble", 0.0f, 1.0f, false, "treble band"},
    {"centroid", 0.0f, 1.0f, false, "brightness (spectral centroid)"},
    {"width", 0.0f, 2.0f, false, "stereo width (0 for mono)"},
    {"kick", 0.0f, 1.0f, true, "kick (hits tier)"},
    {"snare", 0.0f, 1.0f, true, "snare / clap (hits tier)"},
    {"hat", 0.0f, 1.0f, true, "hats (hits tier)"},
    // ADR-1604: the register of the latest onset (low/mid/high shares of its attack), held until the next
    {"onsetLow", 0.0f, 1.0f, false, "onset register: low share (30-250 Hz) of the latest attack"},
    {"onsetMid", 0.0f, 1.0f, false, "onset register: mid share (250 Hz-4 kHz) of the latest attack"},
    {"onsetHigh", 0.0f, 1.0f, false, "onset register: high share (4-16 kHz) of the latest attack"},
}};

std::array<SignalId*, 19> slots(StemSignals& s) {
    return {&s.active, &s.rms,      &s.peak,     &s.level,    &s.onset,    &s.onsetStrength,
            &s.bands[0], &s.bands[1], &s.bands[2], &s.bands[3], &s.bands[4], &s.centroid,
            &s.width,    &s.kick,     &s.snare,    &s.hat,      &s.onsetRegister[0], &s.onsetRegister[1],
            &s.onsetRegister[2]};
}

} // namespace

std::vector<std::string> StemSignals::names(const std::string& key) {
    std::vector<std::string> out;
    out.reserve(kFields.size());
    for (const Field& f : kFields) {
        out.push_back("stem." + key + "." + f.suffix);
    }
    return out;
}

StemSignals StemSignals::declare(SignalBus& bus, const std::string& key, const std::string& label) {
    StemSignals s;
    s.key = key;
    const auto targets = slots(s);
    for (std::size_t i = 0; i < kFields.size(); ++i) {
        const Field& f = kFields[i];
        *targets[i] = bus.declare("stem." + key + "." + f.suffix, f.minValue, f.maxValue, f.event);
    }
    s.relabel(bus, label);
    return s;
}

void StemSignals::relabel(SignalBus& bus, const std::string& label) const {
    auto copy = *this;
    const auto targets = slots(copy);
    for (std::size_t i = 0; i < kFields.size(); ++i) {
        bus.setLabel(*targets[i], (label.empty() ? key : label) + ": " + kFields[i].label);
    }
}

std::vector<SignalId> StemSignals::ids() const {
    auto copy = *this;
    std::vector<SignalId> out;
    for (SignalId* id : slots(copy)) {
        out.push_back(*id);
    }
    return out;
}

void StemSignals::publish(SignalBus& bus, const analysis::StemFrame& f, bool events) const {
    bus.set(active, 1.0f);
    bus.set(rms, f.rms);
    bus.set(peak, f.peak);
    bus.set(level, f.level);
    bus.set(onsetStrength, f.onsetStrength);
    for (std::size_t b = 0; b < bands.size(); ++b) {
        bus.set(bands[b], f.bands[b]);
    }
    bus.set(centroid, f.centroid);
    bus.set(width, f.stereo ? f.width : 0.0f);
    for (std::size_t b = 0; b < onsetRegister.size(); ++b) {
        bus.set(onsetRegister[b], f.onsetRegister[b]); // ADR-1604
    }
    // An event fires with its strength, floored so a quiet one still moves an event route (as ADR-898's band onsets did).
    bus.setEvent(onset, events && f.onset, std::clamp(f.onsetStrength * 0.5f, 0.05f, 1.0f));
    bus.setEvent(kick, events && f.kick, std::max(0.05f, f.kickStrength));
    bus.setEvent(snare, events && f.snare, std::max(0.05f, f.snareStrength));
    bus.setEvent(hat, events && f.hat, std::max(0.05f, f.hatStrength));
}

void StemSignals::publishInactive(SignalBus& bus) const {
    auto copy = *this;
    for (SignalId* id : slots(copy)) {
        if (bus.info(*id).isEvent) {
            bus.setEvent(*id, false);
        } else {
            bus.set(*id, 0.0f);
        }
    }
}

void StemSignals::clearEvents(SignalBus& bus) const {
    for (const SignalId id : {onset, kick, snare, hat}) {
        bus.setEvent(id, false);
    }
}

void StemSignals::publishTrack(SignalBus& bus, const analysis::StemTrack* track, double offset, double from,
                               double to) const {
    if (track == nullptr || track->frames().empty()) {
        publishInactive(bus);
        return;
    }
    const double local = to - offset;
    if (!(local >= 0.0) || local >= track->durationSeconds()) {
        publishInactive(bus);
        return;
    }
    const auto& frames = track->frames();
    const auto after = std::upper_bound(frames.begin(), frames.end(), local,
                                        [](double t, const analysis::StemFrame& f) { return t < f.timeSeconds; });
    // Before the first window's centre (the stem's first 21 ms) the first frame stands: the stem has begun.
    analysis::StemFrame f = after == frames.begin() ? frames.front() : *std::prev(after);
    f.onset = f.kick = f.snare = f.hat = false;
    f.kickStrength = f.snareStrength = f.hatStrength = 0.0f;
    float batchStrength = 0.0f;
    if (from >= 0.0 && from < to) {
        const double localFrom = from - offset;
        auto it = std::upper_bound(frames.begin(), frames.end(), localFrom,
                                   [](double t, const analysis::StemFrame& g) { return t < g.timeSeconds; });
        for (; it != after; ++it) {
            if (it->onset) {
                f.onset = true;
                batchStrength = std::max(batchStrength, it->onsetStrength);
            }
            if (it->kick) {
                f.kick = true;
                f.kickStrength = std::max(f.kickStrength, it->kickStrength);
            }
            if (it->snare) {
                f.snare = true;
                f.snareStrength = std::max(f.snareStrength, it->snareStrength);
            }
            if (it->hat) {
                f.hat = true;
                f.hatStrength = std::max(f.hatStrength, it->hatStrength);
            }
        }
    }
    if (f.onset) {
        f.onsetStrength = std::max(f.onsetStrength, batchStrength);
    }
    publish(bus, f);
}

} // namespace avgen::signals
