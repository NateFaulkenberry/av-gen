#pragma once

// Registry of named control signals (ADR-011). Producers (audio analysis, clock, later LFOs and
// MIDI) write values once per frame; the Modulator reads them. Names are hierarchical
// ("audio.bass"). Ids are stable for the life of the bus; lookups by name happen at bind time.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace avgen::signals {

using SignalId = std::uint32_t;
constexpr SignalId kInvalidSignal = 0xFFFFFFFFu;

struct SignalInfo {
    std::string name;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    bool isEvent = false; // events are pulses: value = strength for one frame, event flag set
    // What the signal is, in words an artist would use ("kick (low-band onset)"), shown beside the
    // name wherever a route's source is picked. Empty when the name says it already.
    std::string label;
};

class SignalBus {
public:
    // Idempotent: declaring an existing name returns its id (range/event flags are not changed).
    SignalId declare(std::string name, float minValue = 0.0f, float maxValue = 1.0f, bool isEvent = false);
    // Also records that `name` was asked for, hit or miss (see `sought`).
    [[nodiscard]] std::optional<SignalId> find(std::string_view name) const;
    // ADR-951: whether anything has ever looked `name` up with `find`. Every consumer of a bus signal
    // -- a route's source or depth, a reaction, a behaviour, staging, a scene state, a source's
    // trigger, the route panel -- reaches its value through `find`, so a producer whose signal is
    // expensive can skip computing one nobody has asked for. Asked-for is sticky: once sought, a name
    // stays sought for the life of the bus. A miss counts, because a consumer bound before the
    // producer first declares its signal has still asked for it. Main thread only, like the bus.
    [[nodiscard]] bool sought(std::string_view name) const;
    // Re-states whether a declared signal is an event -- for a producer whose kind changed after the
    // name was first declared (a timeline source switched to event mode, ADR-900).
    void setEventKind(SignalId id, bool isEvent);

    // Gives a declared signal its readable label (see `SignalInfo::label`).
    void setLabel(SignalId id, std::string label);

    void set(SignalId id, float value);
    // Sets an event signal: fired => value = strength and event flag; else value 0.
    void setEvent(SignalId id, bool fired, float strength = 1.0f);

    [[nodiscard]] float value(SignalId id) const { return values_[id]; }
    [[nodiscard]] bool event(SignalId id) const { return events_[id] != 0; }
    [[nodiscard]] const SignalInfo& info(SignalId id) const { return infos_[id]; }
    [[nodiscard]] std::size_t size() const { return infos_.size(); }
    [[nodiscard]] const std::vector<SignalInfo>& infos() const { return infos_; }

    // Clears all event flags (and event values). Call once per frame after evaluation.
    void clearEvents();

private:
    std::vector<SignalInfo> infos_;
    std::vector<float> values_;
    std::vector<std::uint8_t> events_;
    std::unordered_map<std::string, SignalId> index_;
    mutable std::unordered_set<std::string> sought_; // ADR-951: every name `find` has been asked for
};

} // namespace avgen::signals
