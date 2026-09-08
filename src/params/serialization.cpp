#include "params/serialization.hpp"

#include "core/log.hpp"

#include <array>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string_view>
#include <utility>

namespace avgen::params {

using nlohmann::json;

namespace {

template <typename E>
struct EnumName {
    E value;
    std::string_view name;
};

constexpr std::array<EnumName<CurveType>, 5> kCurveNames{{{CurveType::Linear, "linear"},
                                                          {CurveType::Power, "power"},
                                                          {CurveType::Log, "log"},
                                                          {CurveType::Exp, "exp"},
                                                          {CurveType::SCurve, "scurve"}}};
constexpr std::array<EnumName<ThresholdMode>, 4> kThresholdNames{{{ThresholdMode::None, "none"},
                                                                  {ThresholdMode::Gate, "gate"},
                                                                  {ThresholdMode::Binary, "binary"},
                                                                  {ThresholdMode::Subtract, "subtract"}}};
constexpr std::array<EnumName<EnvelopeMode>, 3> kEnvelopeNames{{{EnvelopeMode::None, "none"},
                                                                {EnvelopeMode::PeakHold, "peakhold"},
                                                                {EnvelopeMode::LinearFall, "linearfall"}}};
constexpr std::array<EnumName<ModOp>, 5> kOpNames{{{ModOp::Add, "add"},
                                                   {ModOp::Multiply, "multiply"},
                                                   {ModOp::Replace, "replace"},
                                                   {ModOp::Min, "min"},
                                                   {ModOp::Max, "max"}}};

template <typename E, std::size_t N>
std::string_view enumToString(const std::array<EnumName<E>, N>& table, E value) {
    for (const auto& entry : table) {
        if (entry.value == value) {
            return entry.name;
        }
    }
    return table[0].name;
}

// Reads an optional enum key. Missing -> `out` unchanged; wrong type or unknown name -> error.
template <typename E, std::size_t N>
Result<void> readEnum(const json& j, const char* key, const std::array<EnumName<E>, N>& table, E& out) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return {};
    }
    if (!it->is_string()) {
        return fail("'{}' must be a string", key);
    }
    const auto name = it->get<std::string>();
    for (const auto& entry : table) {
        if (entry.name == name) {
            out = entry.value;
            return {};
        }
    }
    return fail("unknown value '{}' for '{}'", name, key);
}

Result<void> readFloat(const json& j, const char* key, float& out) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return {};
    }
    if (!it->is_number()) {
        return fail("'{}' must be a number", key);
    }
    out = it->get<float>();
    return {};
}

Result<void> readInt(const json& j, const char* key, int& out) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return {};
    }
    if (!it->is_number_integer()) {
        return fail("'{}' must be an integer", key);
    }
    out = it->get<int>();
    return {};
}

Result<void> readBool(const json& j, const char* key, bool& out) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return {};
    }
    if (!it->is_boolean()) {
        return fail("'{}' must be a boolean", key);
    }
    out = it->get<bool>();
    return {};
}

Result<void> readString(const json& j, const char* key, std::string& out) {
    const auto it = j.find(key);
    if (it == j.end()) {
        return fail("missing required key '{}'", key);
    }
    if (!it->is_string()) {
        return fail("'{}' must be a string", key);
    }
    out = it->get<std::string>();
    return {};
}

// Checks a JSON value against a parameter's kind without modifying the parameter.
Result<void> validateParameterJson(const IParameter& param, const json& value) {
    switch (param.kind()) {
    case ParamKind::Float:
    case ParamKind::Int:
        if (!value.is_number()) {
            return fail("parameter '{}' expects a number", param.path());
        }
        return {};
    case ParamKind::Bool:
        if (!value.is_boolean()) {
            return fail("parameter '{}' expects a boolean", param.path());
        }
        return {};
    case ParamKind::Vec2:
    case ParamKind::Vec3:
    case ParamKind::Vec4:
    case ParamKind::Color:
        if (!value.is_array() || value.size() != param.componentCount()) {
            return fail("parameter '{}' expects an array of {} numbers", param.path(),
                        param.componentCount());
        }
        for (const auto& element : value) {
            if (!element.is_number()) {
                return fail("parameter '{}' expects an array of {} numbers", param.path(),
                            param.componentCount());
            }
        }
        return {};
    }
    return fail("parameter '{}' has an unknown kind", param.path());
}

} // namespace

json parameterToJson(const IParameter& param) {
    switch (param.kind()) {
    case ParamKind::Float:
        return json(static_cast<double>(param.baseComponent(0)));
    case ParamKind::Int:
        return json(static_cast<int>(std::lround(param.baseComponent(0))));
    case ParamKind::Bool:
        return json(param.baseComponent(0) >= 0.5f);
    case ParamKind::Vec2:
    case ParamKind::Vec3:
    case ParamKind::Vec4:
    case ParamKind::Color: {
        json array = json::array();
        for (std::size_t i = 0; i < param.componentCount(); ++i) {
            array.push_back(static_cast<double>(param.baseComponent(i)));
        }
        return array;
    }
    }
    return json();
}

Result<void> parameterFromJson(IParameter& param, const json& value) {
    if (auto check = validateParameterJson(param, value); !check) {
        return check;
    }
    if (value.is_array()) {
        for (std::size_t i = 0; i < param.componentCount(); ++i) {
            param.setBaseComponent(i, value[i].get<float>());
        }
    } else if (value.is_boolean()) {
        param.setBaseComponent(0, value.get<bool>() ? 1.0f : 0.0f);
    } else {
        param.setBaseComponent(0, value.get<float>());
    }
    return {};
}

json chainToJson(const ProcessorChain& chain) {
    json j;
    j["gain"] = static_cast<double>(chain.gain);
    j["offset"] = static_cast<double>(chain.offset);
    j["curve"] = enumToString(kCurveNames, chain.curve);
    j["curveAmount"] = static_cast<double>(chain.curveAmount);
    j["clampEnabled"] = chain.clampEnabled;
    j["clampMin"] = static_cast<double>(chain.clampMin);
    j["clampMax"] = static_cast<double>(chain.clampMax);
    j["threshold"] = enumToString(kThresholdNames, chain.threshold);
    j["thresholdLevel"] = static_cast<double>(chain.thresholdLevel);
    j["attackMs"] = static_cast<double>(chain.attackMs);
    j["decayMs"] = static_cast<double>(chain.decayMs);
    j["envelope"] = enumToString(kEnvelopeNames, chain.envelope);
    j["envelopeHoldMs"] = static_cast<double>(chain.envelopeHoldMs);
    j["envelopeFallPerSecond"] = static_cast<double>(chain.envelopeFallPerSecond);
    j["remapEnabled"] = chain.remapEnabled;
    j["remapInMin"] = static_cast<double>(chain.remapInMin);
    j["remapInMax"] = static_cast<double>(chain.remapInMax);
    j["remapOutMin"] = static_cast<double>(chain.remapOutMin);
    j["remapOutMax"] = static_cast<double>(chain.remapOutMax);
    return j;
}

Result<ProcessorChain> chainFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("processor chain must be a JSON object");
    }
    ProcessorChain chain;
    std::array<Result<void>, 19> results{
        readFloat(j, "gain", chain.gain),
        readFloat(j, "offset", chain.offset),
        readEnum(j, "curve", kCurveNames, chain.curve),
        readFloat(j, "curveAmount", chain.curveAmount),
        readBool(j, "clampEnabled", chain.clampEnabled),
        readFloat(j, "clampMin", chain.clampMin),
        readFloat(j, "clampMax", chain.clampMax),
        readEnum(j, "threshold", kThresholdNames, chain.threshold),
        readFloat(j, "thresholdLevel", chain.thresholdLevel),
        readFloat(j, "attackMs", chain.attackMs),
        readFloat(j, "decayMs", chain.decayMs),
        readEnum(j, "envelope", kEnvelopeNames, chain.envelope),
        readFloat(j, "envelopeHoldMs", chain.envelopeHoldMs),
        readFloat(j, "envelopeFallPerSecond", chain.envelopeFallPerSecond),
        readBool(j, "remapEnabled", chain.remapEnabled),
        readFloat(j, "remapInMin", chain.remapInMin),
        readFloat(j, "remapInMax", chain.remapInMax),
        readFloat(j, "remapOutMin", chain.remapOutMin),
        readFloat(j, "remapOutMax", chain.remapOutMax),
    };
    for (const auto& r : results) {
        if (!r) {
            return fail("processor chain: {}", r.error().message);
        }
    }
    return chain;
}

json routeToJson(const ModRoute& route) {
    json j;
    j["source"] = route.source;
    j["target"] = route.target;
    j["component"] = route.component;
    j["amount"] = static_cast<double>(route.amount);
    j["op"] = enumToString(kOpNames, route.op);
    j["enabled"] = route.enabled;
    j["chain"] = chainToJson(route.chain);
    return j;
}

Result<ModRoute> routeFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("route must be a JSON object");
    }
    ModRoute route;
    if (auto r = readString(j, "source", route.source); !r) {
        return fail("route: {}", r.error().message);
    }
    if (auto r = readString(j, "target", route.target); !r) {
        return fail("route: {}", r.error().message);
    }
    std::array<Result<void>, 4> results{
        readInt(j, "component", route.component),
        readFloat(j, "amount", route.amount),
        readEnum(j, "op", kOpNames, route.op),
        readBool(j, "enabled", route.enabled),
    };
    for (const auto& r : results) {
        if (!r) {
            return fail("route {} -> {}: {}", route.source, route.target, r.error().message);
        }
    }
    if (const auto it = j.find("chain"); it != j.end()) {
        auto chain = chainFromJson(*it);
        if (!chain) {
            return fail("route {} -> {}: {}", route.source, route.target, chain.error().message);
        }
        route.chain = *chain;
    }
    return route;
}

json saveProject(const ParameterSet& params, const Modulator& modulator) {
    json doc;
    doc["format"] = kProjectFormatName;
    doc["version"] = kProjectFormatVersion;
    json parameters = json::object();
    for (const IParameter* param : params.ordered()) {
        if (param->flags().serialized) {
            parameters[param->path()] = parameterToJson(*param);
        }
    }
    doc["parameters"] = std::move(parameters);
    json routes = json::array();
    for (const ModRoute& route : modulator.routes()) {
        routes.push_back(routeToJson(route));
    }
    doc["routes"] = std::move(routes);
    return doc;
}

Result<void> loadProject(const json& doc, ParameterSet& params, Modulator& modulator) {
    if (!doc.is_object()) {
        return fail("project document must be a JSON object");
    }
    const auto format = doc.find("format");
    if (format == doc.end() || !format->is_string() || format->get<std::string>() != kProjectFormatName) {
        return fail("not an avgen project: expected format '{}'", kProjectFormatName);
    }
    const auto version = doc.find("version");
    if (version == doc.end() || !version->is_number_integer()) {
        return fail("project is missing an integer 'version'");
    }
    if (version->get<int>() > kProjectFormatVersion) {
        return fail("project version {} is newer than supported version {}", version->get<int>(),
                    kProjectFormatVersion);
    }

    // Validate everything before mutating anything so a bad file leaves the state untouched.
    std::vector<std::pair<IParameter*, const json*>> pending;
    if (const auto parameters = doc.find("parameters"); parameters != doc.end()) {
        if (!parameters->is_object()) {
            return fail("'parameters' must be an object");
        }
        for (const auto& [path, value] : parameters->items()) {
            IParameter* param = params.find(path);
            if (param == nullptr) {
                log::warn("project references unknown parameter '{}'; ignored", path);
                continue;
            }
            if (auto check = validateParameterJson(*param, value); !check) {
                return check;
            }
            pending.emplace_back(param, &value);
        }
    }
    std::vector<ModRoute> routes;
    if (const auto routesJson = doc.find("routes"); routesJson != doc.end()) {
        if (!routesJson->is_array()) {
            return fail("'routes' must be an array");
        }
        routes.reserve(routesJson->size());
        for (const auto& routeJson : *routesJson) {
            auto route = routeFromJson(routeJson);
            if (!route) {
                return fail("routes[{}]: {}", routes.size(), route.error().message);
            }
            routes.push_back(std::move(*route));
        }
    }

    for (auto& [param, value] : pending) {
        if (auto applied = parameterFromJson(*param, *value); !applied) {
            return applied;
        }
    }
    modulator.clearRoutes();
    for (auto& route : routes) {
        modulator.addRoute(std::move(route));
    }
    return {};
}

Result<void> saveProjectFile(const std::filesystem::path& path, const ParameterSet& params,
                             const Modulator& modulator) {
    std::ofstream out(path);
    if (!out) {
        return fail("cannot open '{}' for writing", path.string());
    }
    out << saveProject(params, modulator).dump(2) << '\n';
    if (!out) {
        return fail("failed while writing '{}'", path.string());
    }
    return {};
}

Result<void> loadProjectFile(const std::filesystem::path& path, ParameterSet& params, Modulator& modulator) {
    std::ifstream in(path);
    if (!in) {
        return fail("cannot open '{}' for reading", path.string());
    }
    const json doc = json::parse(in, nullptr, false);
    if (doc.is_discarded()) {
        return fail("'{}' is not valid JSON", path.string());
    }
    if (auto loaded = loadProject(doc, params, modulator); !loaded) {
        return fail("'{}': {}", path.string(), loaded.error().message);
    }
    return {};
}

} // namespace avgen::params
