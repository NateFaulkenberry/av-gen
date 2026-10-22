#include "signals/conditioned_signal.hpp"

#include "params/serialization.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <utility>

namespace avgen::signals {

using nlohmann::json;

namespace {

constexpr std::array<std::pair<std::string_view, ConditionedSignalSource::Combine>, 4> kCombines{{
    {"firstActive", ConditionedSignalSource::Combine::FirstActive},
    {"sum", ConditionedSignalSource::Combine::Sum},
    {"mean", ConditionedSignalSource::Combine::Mean},
    {"max", ConditionedSignalSource::Combine::Max},
}};

Result<float> finiteNumber(const json& j, const std::string& where) {
    if (!j.is_number()) {
        return fail("{} must be a number", where);
    }
    const auto v = j.get<double>();
    if (!std::isfinite(v)) {
        return fail("{} must be finite", where);
    }
    return static_cast<float>(v);
}

// `<prefix>.active` for a source `<prefix>.<feature>`: the activity flag a stem or a live input source publishes
// (ADR-1520/1521). Empty when the name has no dot.
std::string activeOf(const std::string& source) {
    const auto dot = source.rfind('.');
    return dot == std::string::npos || dot == 0 ? std::string() : source.substr(0, dot) + ".active";
}

} // namespace

const char* combineName(ConditionedSignalSource::Combine c) {
    for (const auto& [name, value] : kCombines) {
        if (value == c) {
            return name.data();
        }
    }
    return "firstActive";
}

ConditionedSignalSource::ConditionedSignalSource(std::string name)
    : Source(std::move(name)) {}

void ConditionedSignalSource::attach(SignalBus& bus, params::ParameterSet& params) {
    static_cast<void>(params);
    output_ = bus.declare("signal." + name_, 0.0f, 1.0f, event_);
    bus.setEventKind(output_, event_); // a reload may have switched the mode
    for (Input& in : inputs_) {
        in.id = kInvalidSignal;
        in.depthId = kInvalidSignal;
        in.gateId = kInvalidSignal;
        in.gateResolved = false;
    }
    resolve(bus);
}

// Inputs resolve lazily (as an envelope's trigger does): a producer may declare its signal after this source attached
// -- a stem loaded later, a live input source mapped in the session.
void ConditionedSignalSource::resolve(SignalBus& bus) {
    for (Input& in : inputs_) {
        if (in.constant) {
            continue;
        }
        if (in.id == kInvalidSignal) {
            if (const auto id = bus.find(in.source)) {
                in.id = *id;
            }
        }
        if (!in.depthSource.empty() && in.depthId == kInvalidSignal) {
            if (const auto id = bus.find(in.depthSource)) {
                in.depthId = *id;
            }
        }
        if (in.gateId == kInvalidSignal) {
            const std::string gate = in.gate.empty() ? activeOf(in.source) : in.gate;
            if (!gate.empty()) {
                if (const auto id = bus.find(gate)) {
                    in.gateId = *id;
                }
            }
        }
    }
}

void ConditionedSignalSource::reset() {
    for (Input& in : inputs_) {
        in.state = {};
        in.value = 0.0f;
        in.event = false;
        in.active = false;
    }
    outputState_ = {};
    value_ = 0.0f;
    selected_ = -1;
}

void ConditionedSignalSource::update(SignalBus& bus, const SourceContext& context) {
    if (output_ == kInvalidSignal) {
        return;
    }
    resolve(bus);
    const double dt = context.time.deltaTime;
    for (Input& in : inputs_) {
        if (in.constant) {
            // A constant through its chain (a pass-through unless one is given), always active.
            in.value = in.chain.process(*in.constant, false, dt, in.state) * in.amount;
            in.event = false;
            in.active = true;
            continue;
        }
        // A source the bus does not carry, or a depth signal it lacks with no default: silent and inactive, as an
        // unresolved route is (ADR-1570's `optional` shape).
        const bool scaled = !in.depthSource.empty();
        if (in.id == kInvalidSignal || in.id >= bus.size() ||
            (scaled && in.depthId == kInvalidSignal && !in.depthDefault)) {
            in.value = 0.0f;
            in.event = false;
            in.active = false;
            continue;
        }
        // Exactly `Modulator::applyRoutesWhere`'s front half (an `add` onto 0 at depth d writes y x d).
        float x = bus.value(in.id);
        if (in.bipolar) {
            x = x * 2.0f - 1.0f;
        }
        in.event = bus.event(in.id);
        float y = in.chain.process(x, in.event, dt, in.state) * in.amount;
        if (scaled) {
            const float depthValue = in.depthId != kInvalidSignal ? bus.value(in.depthId) : *in.depthDefault;
            y *= in.depthMin + (in.depthMax - in.depthMin) * depthValue;
        }
        in.value = y;
        // A declared gate the bus does not carry means the thing it gates is absent: inactive. The implied
        // `<prefix>.active`, when the bus has none, means the source has no notion of activity: active.
        in.active = in.gateId != kInvalidSignal ? bus.value(in.gateId) > 0.5f : in.gate.empty();
    }

    float v = 0.0f;
    bool event = false;
    selected_ = -1;
    switch (combine_) {
    case Combine::FirstActive:
        for (std::size_t i = 0; i < inputs_.size(); ++i) {
            if (inputs_[i].active) {
                selected_ = static_cast<int>(i);
                v = inputs_[i].value;
                event = inputs_[i].event;
                break;
            }
        }
        break;
    case Combine::Sum:
    case Combine::Mean:
    case Combine::Max: {
        int n = 0;
        for (const Input& in : inputs_) {
            if (!in.active) {
                continue;
            }
            v = combine_ == Combine::Max ? (n == 0 ? in.value : std::max(v, in.value)) : v + in.value;
            event = event || in.event;
            ++n;
        }
        if (combine_ == Combine::Mean && n > 0) {
            v /= static_cast<float>(n);
        }
        break;
    }
    }
    if (outputChain_) {
        v = outputChain_->process(v, event, dt, outputState_);
    }
    value_ = v;
    if (event_) {
        bus.setEvent(output_, event, v);
    } else {
        bus.set(output_, v);
    }
}

json ConditionedSignalSource::settingsToJson() const {
    json inputs = json::array();
    for (const Input& in : inputs_) {
        json e = json::object();
        if (in.constant) {
            e["constant"] = *in.constant;
        } else {
            e["source"] = in.source;
        }
        if (in.bipolar) {
            e["polarity"] = "bipolar";
        }
        if (in.amount != 1.0f) {
            e["amount"] = in.amount;
        }
        const json chain = params::chainToJson(in.chain);
        if (chain != params::chainToJson(params::ProcessorChain{})) {
            e["chain"] = chain;
        }
        if (!in.depthSource.empty()) {
            e["depthSource"] = in.depthSource;
            e["depthMin"] = in.depthMin;
            e["depthMax"] = in.depthMax;
            if (in.depthDefault) {
                e["depthDefault"] = *in.depthDefault;
            }
        }
        if (!in.gate.empty()) {
            e["gate"] = in.gate;
        }
        inputs.push_back(std::move(e));
    }
    json j{{"combine", combineName(combine_)}, {"inputs", std::move(inputs)}};
    if (outputChain_) {
        j["chain"] = params::chainToJson(*outputChain_);
    }
    if (event_) {
        j["event"] = true;
    }
    return j;
}

Result<void> ConditionedSignalSource::settingsFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("signal '{}': settings must be an object", name_);
    }
    Combine combine = Combine::FirstActive;
    if (const auto c = j.find("combine"); c != j.end()) {
        if (!c->is_string()) {
            return fail("signal '{}': 'combine' must be a string", name_);
        }
        const auto it = std::find_if(kCombines.begin(), kCombines.end(),
                                     [&](const auto& e) { return e.first == c->get<std::string>(); });
        if (it == kCombines.end()) {
            return fail("signal '{}': 'combine' must be firstActive, sum, mean or max, got '{}'", name_,
                        c->get<std::string>());
        }
        combine = it->second;
    }
    const auto list = j.find("inputs");
    if (list == j.end() || !list->is_array() || list->empty()) {
        return fail("signal '{}': 'inputs' must be a non-empty array", name_);
    }
    std::vector<Input> inputs;
    for (std::size_t i = 0; i < list->size(); ++i) {
        const json& e = (*list)[i];
        const std::string where = fmt::format("signal '{}' inputs[{}]", name_, i);
        if (!e.is_object()) {
            return fail("{} must be an object", where);
        }
        Input in;
        const auto source = e.find("source");
        const auto constant = e.find("constant");
        if ((source == e.end()) == (constant == e.end())) {
            return fail("{} needs exactly one of 'source' and 'constant'", where);
        }
        if (source != e.end()) {
            if (!source->is_string() || source->get<std::string>().empty()) {
                return fail("{}: 'source' must be a non-empty string", where);
            }
            in.source = source->get<std::string>();
        } else {
            auto v = finiteNumber(*constant, where + ".constant");
            if (!v) {
                return std::unexpected(v.error());
            }
            in.constant = *v;
        }
        if (const auto p = e.find("polarity"); p != e.end()) {
            if (!p->is_string() || (*p != "unipolar" && *p != "bipolar")) {
                return fail("{}: 'polarity' must be unipolar or bipolar", where);
            }
            in.bipolar = *p == "bipolar";
        }
        const auto number = [&](const char* key, float& out) -> Result<void> {
            if (const auto it = e.find(key); it != e.end()) {
                auto v = finiteNumber(*it, where + "." + key);
                if (!v) {
                    return std::unexpected(v.error());
                }
                out = *v;
            }
            return {};
        };
        if (auto r = number("amount", in.amount); !r) {
            return r;
        }
        if (const auto c = e.find("chain"); c != e.end()) {
            auto chain = params::chainFromJson(*c);
            if (!chain) {
                return fail("{}: {}", where, chain.error().message);
            }
            in.chain = *chain;
        }
        if (const auto d = e.find("depthSource"); d != e.end()) {
            if (!d->is_string()) {
                return fail("{}: 'depthSource' must be a string", where);
            }
            in.depthSource = d->get<std::string>();
        }
        if (auto r = number("depthMin", in.depthMin); !r) {
            return r;
        }
        if (auto r = number("depthMax", in.depthMax); !r) {
            return r;
        }
        if (const auto d = e.find("depthDefault"); d != e.end()) {
            auto v = finiteNumber(*d, where + ".depthDefault");
            if (!v) {
                return std::unexpected(v.error());
            }
            in.depthDefault = *v;
        }
        if (const auto g = e.find("gate"); g != e.end()) {
            if (!g->is_string()) {
                return fail("{}: 'gate' must be a string", where);
            }
            in.gate = g->get<std::string>();
        }
        inputs.push_back(std::move(in));
    }
    std::optional<params::ProcessorChain> output;
    if (const auto c = j.find("chain"); c != j.end()) {
        auto chain = params::chainFromJson(*c);
        if (!chain) {
            return fail("signal '{}' chain: {}", name_, chain.error().message);
        }
        output = *chain;
    }
    bool event = false;
    if (const auto ev = j.find("event"); ev != j.end()) {
        if (!ev->is_boolean()) {
            return fail("signal '{}': 'event' must be true or false", name_);
        }
        event = ev->get<bool>();
    }
    combine_ = combine;
    inputs_ = std::move(inputs);
    outputChain_ = output;
    event_ = event;
    reset();
    return {};
}

} // namespace avgen::signals
