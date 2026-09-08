#include "signals/signal_bus.hpp"

#include <utility>

namespace avgen::signals {

SignalId SignalBus::declare(std::string name, float minValue, float maxValue, bool isEvent) {
    if (const auto it = index_.find(name); it != index_.end()) {
        return it->second;
    }
    const auto id = static_cast<SignalId>(infos_.size());
    infos_.push_back(SignalInfo{name, minValue, maxValue, isEvent});
    values_.push_back(0.0f);
    events_.push_back(0);
    index_.emplace(std::move(name), id);
    return id;
}

std::optional<SignalId> SignalBus::find(std::string_view name) const {
    const auto it = index_.find(std::string(name));
    if (it == index_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void SignalBus::set(SignalId id, float value) {
    if (id < values_.size()) {
        values_[id] = value;
    }
}

void SignalBus::setEvent(SignalId id, bool fired, float strength) {
    if (id < values_.size()) {
        values_[id] = fired ? strength : 0.0f;
        events_[id] = fired ? 1 : 0;
    }
}

void SignalBus::clearEvents() {
    for (std::size_t i = 0; i < events_.size(); ++i) {
        if (events_[i] != 0) {
            events_[i] = 0;
            values_[i] = 0.0f;
        }
    }
}

} // namespace avgen::signals
