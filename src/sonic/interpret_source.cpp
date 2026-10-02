#include "sonic/interpret_source.hpp"

#include "core/log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::sonic {

namespace {

constexpr std::array<const char*, 5> kCombineNames{"mean", "sum", "product", "max", "min"};

params::Parameter<float>* addParam(params::ParameterSet& params, std::string path, float value, float lo, float hi,
                                   float softLo, float softHi) {
    return &params.add(params::ParamDesc<float>{.path = std::move(path),
                                                .defaultValue = value,
                                                .hardMin = lo,
                                                .hardMax = hi,
                                                .softMin = softLo,
                                                .softMax = softHi});
}

void drop(params::ParameterSet& params, params::Parameter<float>*& p) {
    if (p != nullptr) {
        params.remove(p->path());
        p = nullptr;
    }
}

} // namespace

const char* combineName(Combine c) {
    return kCombineNames[static_cast<std::size_t>(c)];
}

InterpretSource::InterpretSource(std::string name) : Source(std::move(name)) {}

void InterpretSource::attach(signals::SignalBus& bus, params::ParameterSet& params) {
    const std::string prefix = parameterPrefix();
    for (Group& g : groups_) {
        g.sharpnessParam = addParam(params, prefix + "group/" + g.name + "/sharpness", g.sharpness, 0.1f, 32.0f, 0.5f, 8.0f);
    }
    for (Mapping& m : mappings_) {
        m.output = bus.declare("visual." + m.name, 0.0f, 1.0f);
        const std::string base = prefix + m.name + "/";
        m.biasParam = addParam(params, base + "bias", m.bias, -64.0f, 64.0f, -1.0f, 1.0f); // ADR-1062: wide hard ranges
        m.gainParam = addParam(params, base + "gain", m.gain, -256.0f, 256.0f, 0.0f, 4.0f); // for narrow register bumps
        m.curveParam = addParam(params, base + "curve", m.curve, 0.05f, 16.0f, 0.25f, 4.0f);
        for (std::size_t i = 0; i < m.inputs.size(); ++i) {
            Input& in = m.inputs[i];
            in.weightParam = addParam(params, base + "in" + std::to_string(i + 1), in.weight, 0.0f, 100.0f, 0.0f, 4.0f);
            in.id = signals::kInvalidSignal;
        }
    }
    values_.assign(mappings_.size(), 0.0f);
}

void InterpretSource::detach(params::ParameterSet& params) {
    for (Group& g : groups_) {
        drop(params, g.sharpnessParam);
    }
    for (Mapping& m : mappings_) {
        drop(params, m.biasParam);
        drop(params, m.gainParam);
        drop(params, m.curveParam);
        for (Input& in : m.inputs) {
            drop(params, in.weightParam);
        }
    }
}

void InterpretSource::update(signals::SignalBus& bus, const signals::SourceContext& context) {
    sample(bus, context);
}

float InterpretSource::combine(Combine mode, const std::vector<std::pair<float, float>>& weighted) {
    if (weighted.empty()) {
        return 0.0f;
    }
    switch (mode) {
    case Combine::Mean: {
        double sum = 0.0;
        double w = 0.0;
        for (const auto& [x, weight] : weighted) {
            sum += static_cast<double>(x) * weight;
            w += weight;
        }
        return w > 0.0 ? static_cast<float>(sum / w) : 0.0f;
    }
    case Combine::Sum: {
        double sum = 0.0;
        for (const auto& [x, weight] : weighted) {
            sum += static_cast<double>(x) * weight;
        }
        return static_cast<float>(sum);
    }
    case Combine::Product: {
        double p = 1.0;
        for (const auto& [x, weight] : weighted) {
            if (weight <= 0.0f) {
                continue;
            }
            p *= std::pow(std::clamp(static_cast<double>(x), 0.0, 1.0), static_cast<double>(weight));
        }
        return static_cast<float>(p);
    }
    case Combine::Max: {
        float m = -1e30f;
        for (const auto& [x, weight] : weighted) {
            m = std::max(m, x * weight);
        }
        return m;
    }
    case Combine::Min: {
        float m = 1e30f;
        for (const auto& [x, weight] : weighted) {
            m = std::min(m, x * weight);
        }
        return m;
    }
    }
    return 0.0f;
}

float InterpretSource::shape(float v, float bias, float gain, float curve) {
    const float x = std::clamp(bias + gain * v, 0.0f, 1.0f);
    return curve == 1.0f ? x : std::pow(x, std::max(curve, 1e-3f));
}

void InterpretSource::compete(std::vector<float>& values, float sharpness) {
    double sum = 0.0;
    for (float& v : values) {
        v = std::pow(std::max(v, 0.0f), std::max(sharpness, 1e-3f));
        sum += v;
    }
    if (sum <= 1e-12) {
        std::fill(values.begin(), values.end(), 0.0f);
        return;
    }
    for (float& v : values) {
        v = static_cast<float>(v / sum);
    }
}

void InterpretSource::sample(signals::SignalBus& bus, const signals::SourceContext&) const {
    if (values_.size() != mappings_.size()) {
        return; // not attached
    }
    for (std::size_t mi = 0; mi < mappings_.size(); ++mi) {
        const Mapping& m = mappings_[mi];
        scratch_.clear();
        for (const Input& in : m.inputs) {
            if (in.id == signals::kInvalidSignal || in.id >= bus.size()) {
                if (const auto id = bus.find(in.signal)) {
                    in.id = *id;
                } else {
                    continue; // absent: left out, not read as zero (a missing producer is not silence)
                }
            }
            float x = bus.value(in.id);
            if (in.invert) {
                x = 1.0f - x;
            }
            scratch_.emplace_back(x, in.weightParam != nullptr ? in.weightParam->value() : in.weight);
        }
        const float raw = combine(m.combine, scratch_);
        values_[mi] = shape(raw, m.biasParam ? m.biasParam->value() : m.bias, m.gainParam ? m.gainParam->value() : m.gain,
                            m.curveParam ? m.curveParam->value() : m.curve);
    }
    for (std::size_t gi = 0; gi < groups_.size(); ++gi) {
        groupScratch_.clear();
        for (const Mapping& m : mappings_) {
            if (m.groupIndex == static_cast<int>(gi)) {
                groupScratch_.push_back(values_[static_cast<std::size_t>(&m - mappings_.data())]);
            }
        }
        const Group& g = groups_[gi];
        compete(groupScratch_, g.sharpnessParam ? g.sharpnessParam->value() : g.sharpness);
        std::size_t k = 0;
        for (std::size_t mi = 0; mi < mappings_.size(); ++mi) {
            if (mappings_[mi].groupIndex == static_cast<int>(gi)) {
                values_[mi] = groupScratch_[k++];
            }
        }
    }
    for (std::size_t mi = 0; mi < mappings_.size(); ++mi) {
        if (mappings_[mi].output != signals::kInvalidSignal) {
            bus.set(mappings_[mi].output, values_[mi]);
        }
    }
}

nlohmann::json InterpretSource::settingsToJson() const {
    nlohmann::json maps = nlohmann::json::array();
    for (const Mapping& m : mappings_) {
        nlohmann::json inputs = nlohmann::json::array();
        for (const Input& in : m.inputs) {
            nlohmann::json i = {{"signal", in.signal}, {"weight", in.weight}};
            if (in.invert) {
                i["invert"] = true;
            }
            inputs.push_back(std::move(i));
        }
        nlohmann::json j = {{"name", m.name}, {"combine", combineName(m.combine)}, {"inputs", std::move(inputs)},
                            {"bias", m.bias}, {"gain", m.gain}, {"curve", m.curve}};
        if (!m.group.empty()) {
            j["group"] = m.group;
        }
        maps.push_back(std::move(j));
    }
    nlohmann::json groups = nlohmann::json::object();
    for (const Group& g : groups_) {
        groups[g.name] = {{"sharpness", g.sharpness}};
    }
    return {{"mappings", std::move(maps)}, {"groups", std::move(groups)}};
}

Result<void> InterpretSource::settingsFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("interpret '{}': settings must be an object", name_);
    }
    std::vector<Mapping> maps;
    std::vector<Group> groups;
    if (const auto g = j.find("groups"); g != j.end()) {
        if (!g->is_object()) {
            return fail("interpret '{}': 'groups' must be an object", name_);
        }
        for (const auto& [gname, body] : g->items()) {
            Group group;
            group.name = gname;
            group.sharpness = body.is_object() ? body.value("sharpness", 1.0f) : 1.0f;
            groups.push_back(std::move(group));
        }
    }
    const auto m = j.find("mappings");
    if (m == j.end() || !m->is_array()) {
        return fail("interpret '{}': needs a 'mappings' array", name_);
    }
    for (const auto& entry : *m) {
        Mapping map;
        map.name = entry.value("name", std::string{});
        if (map.name.empty()) {
            return fail("interpret '{}': every mapping needs a name", name_);
        }
        for (const Mapping& other : maps) {
            if (other.name == map.name) {
                return fail("interpret '{}': two mappings are named '{}'", name_, map.name);
            }
        }
        const std::string mode = entry.value("combine", std::string("mean"));
        const auto found = std::find(kCombineNames.begin(), kCombineNames.end(), mode);
        if (found == kCombineNames.end()) {
            return fail("interpret '{}': mapping '{}' has an unknown combine '{}'", name_, map.name, mode);
        }
        map.combine = static_cast<Combine>(found - kCombineNames.begin());
        map.bias = entry.value("bias", 0.0f);
        map.gain = entry.value("gain", 1.0f);
        map.curve = entry.value("curve", 1.0f);
        map.group = entry.value("group", std::string{});
        if (!map.group.empty()) {
            auto it = std::find_if(groups.begin(), groups.end(), [&](const Group& g) { return g.name == map.group; });
            if (it == groups.end()) {
                groups.push_back(Group{map.group, 1.0f, nullptr});
                it = groups.end() - 1;
            }
            map.groupIndex = static_cast<int>(it - groups.begin());
        }
        const auto inputs = entry.find("inputs");
        if (inputs == entry.end() || !inputs->is_array() || inputs->empty()) {
            return fail("interpret '{}': mapping '{}' needs a non-empty 'inputs' array", name_, map.name);
        }
        for (const auto& in : *inputs) {
            Input input;
            if (in.is_string()) {
                input.signal = in.get<std::string>();
            } else {
                input.signal = in.value("signal", std::string{});
                input.weight = in.value("weight", 1.0f);
                input.invert = in.value("invert", false);
            }
            if (input.signal.empty()) {
                return fail("interpret '{}': mapping '{}' has an input with no signal", name_, map.name);
            }
            map.inputs.push_back(std::move(input));
        }
        maps.push_back(std::move(map));
    }
    mappings_ = std::move(maps);
    groups_ = std::move(groups);
    values_.clear();
    return {};
}

std::vector<std::string> InterpretSource::outputs() const {
    std::vector<std::string> out;
    out.reserve(mappings_.size());
    for (const Mapping& m : mappings_) {
        out.push_back("visual." + m.name);
    }
    return out;
}

} // namespace avgen::sonic
