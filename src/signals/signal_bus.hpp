#pragma once

// Registry of named control signals (ADR-011). Producers (audio analysis, clock, later LFOs and
// MIDI) write values once per frame; the Modulator reads them. Names are hierarchical
// ("audio.bass"). Ids are stable for the life of the bus; lookups by name happen at bind time.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace avgen::signals {

using SignalId = std::uint32_t;
constexpr SignalId kInvalidSignal = 0xFFFFFFFFu;

struct SignalInfo {
    std::string name;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    bool isEvent = false; // events are pulses: value = strength for one frame, event flag set
};

class SignalBus {
public:
    // Idempotent: declaring an existing name returns its id (range/event flags are not changed).
    SignalId declare(std::string name, float minValue = 0.0f, float maxValue = 1.0f, bool isEvent = false);
    [[nodiscard]] std::optional<SignalId> find(std::string_view name) const;

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
};

} // namespace avgen::signals
