#include "sonic/signal_events.hpp"

#include "signals/signal_bus.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::sonic {

namespace {

bool startsWith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

bool sonicFamily(std::string_view name) {
    return startsWith(name, "sonic.") || startsWith(name, "response.") || startsWith(name, "notes.") ||
           startsWith(name, "timbre.");
}

} // namespace

bool SignalEventDeriver::derivable(std::string_view name) {
    return name == "audio.onset" || name == "audio.beat" || name == "audio.onsetLow" || name == "audio.onsetMid" ||
           name == "audio.onsetHigh" || sonicFamily(name);
}

void SignalEventDeriver::clear() {
    key_ = 0;
    walked_ = false;
    sonic_.clear();
    sonicEventNames_.clear();
}

std::optional<std::vector<world::TriggerOnset>> SignalEventDeriver::derive(std::string_view name,
                                                                           const analysis::AnalysisTrack* track,
                                                                           const SonicSetup* setup) {
    if (startsWith(name, "audio.")) {
        if (track == nullptr || track->empty()) {
            return std::nullopt;
        }
        std::vector<world::TriggerOnset> out;
        for (const analysis::AnalysisFrame& f : track->frames()) {
            // The strengths `AudioSignals::publish` gives each event.
            if (name == "audio.onset" && f.onset) {
                out.push_back({f.timeSeconds, std::min(1.0f, f.onsetStrength * 0.5f)});
            } else if (name == "audio.beat" && f.beat) {
                out.push_back({f.timeSeconds, 1.0f});
            } else if (name == "audio.onsetLow" && f.lowOnset) {
                out.push_back({f.timeSeconds, std::max(0.05f, f.lowOnsetStrength)});
            } else if (name == "audio.onsetMid" && f.midOnset) {
                out.push_back({f.timeSeconds, std::max(0.05f, f.midOnsetStrength)});
            } else if (name == "audio.onsetHigh" && f.highOnset) {
                out.push_back({f.timeSeconds, std::max(0.05f, f.highOnsetStrength)});
            }
        }
        if (name != "audio.onset" && name != "audio.beat" && name != "audio.onsetLow" && name != "audio.onsetMid" &&
            name != "audio.onsetHigh") {
            return std::nullopt;
        }
        return out;
    }
    if (!sonicFamily(name) || setup == nullptr) {
        return std::nullopt;
    }
    std::uint64_t key = setup->key();
    key ^= reinterpret_cast<std::uintptr_t>(track) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
    key ^= (track != nullptr ? track->frames().size() : 0) + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
    if (!walked_ || key != key_) {
        sonic_.clear();
        sonicEventNames_.clear();
        walkSonic(track, *setup);
        key_ = key;
        walked_ = true;
    }
    if (std::find(sonicEventNames_.begin(), sonicEventNames_.end(), name) == sonicEventNames_.end()) {
        return std::nullopt; // not an event of the Sonic runtime (a continuous signal, or a misspelling)
    }
    if (const auto it = sonic_.find(name); it != sonic_.end()) {
        return it->second;
    }
    return std::vector<world::TriggerOnset>{};
}

void SignalEventDeriver::walkSonic(const analysis::AnalysisTrack* track, const SonicSetup& setup) {
    signals::SignalBus bus;
    SonicRuntime runtime;
    runtime.declare(bus);
    std::vector<signals::SignalId> events;
    for (signals::SignalId id = 0; id < bus.size(); ++id) {
        if (bus.info(id).isEvent) {
            events.push_back(id);
            sonicEventNames_.push_back(bus.info(id).name);
        }
    }
    const auto harvest = [&](double t) {
        for (const signals::SignalId id : events) {
            if (bus.event(id)) {
                sonic_[bus.info(id).name].push_back({t, bus.value(id)});
            }
        }
        bus.clearEvents();
    };
    const bool haveTimbre = track != nullptr && !track->empty() && setup.timbre.size() == track->frames().size();
    if (haveTimbre) {
        const auto& frames = track->frames();
        const double hop =
            static_cast<double>(track->config().hopSize) / static_cast<double>(track->config().sampleRate);
        for (std::size_t i = 0; i < frames.size(); ++i) {
            runtime.step(setup, setup.timbre[i], hop);
            runtime.publish(&setup, bus, frames[i].timeSeconds);
            harvest(frames[i].timeSeconds);
        }
        return;
    }
    // No audio: the notes alone, published on a 100 Hz grid to a second past the last note.
    const double end = setup.notes.notes.empty() ? 0.0 : setup.notes.notes.back().start + setup.notes.longest + 1.0;
    const auto steps = static_cast<std::size_t>(std::ceil(end * 100.0));
    for (std::size_t i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) / 100.0;
        runtime.publish(&setup, bus, t);
        harvest(t);
    }
}

} // namespace avgen::sonic
