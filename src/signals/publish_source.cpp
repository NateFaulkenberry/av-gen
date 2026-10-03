#include "signals/publish_source.hpp"

#include "params/parameter_set.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace avgen::signals {

PublishSource::PublishSource(std::string name) : Source(std::move(name)) {}

std::string PublishSource::signalName(const std::string& path) {
    std::string out = path;
    std::replace(out.begin(), out.end(), '/', '.');
    return out;
}

void PublishSource::attach(SignalBus& bus, params::ParameterSet& params) {
    params_ = &params;
    for (Entry& e : entries_) {
        const std::string base = signalName(e.path);
        const params::IParameter* p = params.find(e.path);
        const std::size_t n = p != nullptr ? std::max<std::size_t>(p->componentCount(), 1) : 1;
        e.ids.clear();
        for (std::size_t c = 0; c < n; ++c) {
            const float lo = p != nullptr ? p->hardMin(c) : 0.0f;
            const float hi = p != nullptr ? p->hardMax(c) : 1.0f;
            const SignalId id = bus.declare(c == 0 ? base : base + "." + std::to_string(c), lo, hi);
            if (c == 0) {
                bus.setLabel(id, "the parameter " + e.path + ", as modulated (one frame late)");
            }
            e.ids.push_back(id);
        }
    }
}

void PublishSource::detach(params::ParameterSet& /*params*/) {
    params_ = nullptr;
}

void PublishSource::update(SignalBus& bus, const SourceContext& /*context*/) {
    if (params_ == nullptr) {
        return;
    }
    for (const Entry& e : entries_) {
        const params::IParameter* p = params_->find(e.path);
        for (std::size_t c = 0; c < e.ids.size(); ++c) {
            bus.set(e.ids[c], p != nullptr && c < p->componentCount() ? p->finalComponent(c) : 0.0f);
        }
    }
}

nlohmann::json PublishSource::settingsToJson() const {
    nlohmann::json list = nlohmann::json::array();
    for (const Entry& e : entries_) {
        list.push_back(e.path);
    }
    return {{"parameters", list}};
}

Result<void> PublishSource::settingsFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("a publish source's settings must be an object ({{\"parameters\": [...]}})");
    }
    std::vector<Entry> parsed;
    if (const auto list = j.find("parameters"); list != j.end()) {
        if (!list->is_array()) {
            return fail("a publish source's 'parameters' must be a list of parameter paths");
        }
        for (const auto& item : *list) {
            if (!item.is_string() || item.get<std::string>().empty()) {
                return fail("a publish source's parameters must be paths ('fx/heroGlow/gain')");
            }
            parsed.push_back(Entry{item.get<std::string>(), {}});
        }
    }
    entries_ = std::move(parsed);
    return {};
}

std::vector<std::string> PublishSource::outputs() const {
    std::vector<std::string> out;
    for (const Entry& e : entries_) {
        out.push_back(signalName(e.path));
    }
    return out;
}

} // namespace avgen::signals
