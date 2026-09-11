#include "params/serialization.hpp"

#include <algorithm>

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
constexpr std::array<EnumName<Polarity>, 2> kPolarityNames{
    {{Polarity::Unipolar, "unipolar"}, {Polarity::Bipolar, "bipolar"}}};

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

std::string_view curveTypeName(CurveType curve) {
    return enumToString(kCurveNames, curve);
}
std::optional<CurveType> curveTypeFromName(std::string_view name) {
    for (const auto& e : kCurveNames) {
        if (e.name == name) return e.value;
    }
    return std::nullopt;
}
std::string_view modOpName(ModOp op) {
    return enumToString(kOpNames, op);
}
std::optional<ModOp> modOpFromName(std::string_view name) {
    for (const auto& e : kOpNames) {
        if (e.name == name) return e.value;
    }
    return std::nullopt;
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
    j["polarity"] = enumToString(kPolarityNames, route.polarity);
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
    std::array<Result<void>, 5> results{
        readInt(j, "component", route.component),
        readFloat(j, "amount", route.amount),
        readEnum(j, "op", kOpNames, route.op),
        readEnum(j, "polarity", kPolarityNames, route.polarity), // absent (v1) = unipolar
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

json saveProject(const ParameterSet& params, const Modulator& modulator, const signals::SourceRack* sources,
                 const PresetBank* presets) {
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
        // Routes a subsystem installed are that subsystem's to re-create: a procedural graph
        // rebuilds its own on evaluation (ADR-028) and an entity compiles its own from the scene
        // file's `reactions` (ADR-087). Writing them here would mean a project that grows a
        // duplicate of every one of them each time it is saved, and a route the author cannot
        // delete because the thing that owns it puts it straight back.
        if (route.fromGraph || route.fromEntity) {
            continue;
        }
        routes.push_back(routeToJson(route));
    }
    doc["routes"] = std::move(routes);
    if (sources != nullptr) {
        doc["sources"] = sources->toJson();
    }
    if (presets != nullptr) {
        doc["presets"] = presets->toJson();
    }
    return doc;
}

namespace {

// Validates the envelope shared by migrateProject and loadProject and returns the version.
Result<int> readEnvelope(const json& doc) {
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
    const int v = version->get<int>();
    if (v > kProjectFormatVersion) {
        return fail("project version {} is newer than supported version {}", v, kProjectFormatVersion);
    }
    if (v < 1) {
        return fail("project version {} is not valid", v);
    }
    return v;
}

// Inserts `value` under `key` when absent. Returns true when it was added.
bool addDefault(json& doc, const char* key, json value) {
    if (doc.contains(key)) {
        return false;
    }
    doc[key] = std::move(value);
    return true;
}

// 1 -> 2: routes gain an explicit polarity; sources and presets become explicit (empty) sections.
std::string migrate1To2(json& doc) {
    std::size_t routesFixed = 0;
    if (const auto routes = doc.find("routes"); routes != doc.end() && routes->is_array()) {
        for (json& route : *routes) {
            if (route.is_object() && addDefault(route, "polarity", "unipolar")) {
                ++routesFixed;
            }
        }
    }
    std::string added;
    if (addDefault(doc, "sources", json::array())) {
        added += " 'sources'";
    }
    if (addDefault(doc, "presets", json::array())) {
        added += " 'presets'";
    }
    return fmt::format("1 -> 2: set polarity 'unipolar' on {} route(s); added empty{}", routesFixed,
                       added.empty() ? " (nothing)" : added.c_str());
}

// 2 -> 3: "shaders" is declared (the engine has written it since 0.4); "timeline" stays optional.
std::string migrate2To3(json& doc) {
    const bool added = addDefault(doc, "shaders", json::array());
    return fmt::format("2 -> 3: {} 'shaders'; 'timeline' left absent (none)",
                       added ? "added empty" : "kept existing");
}

// 3 -> 4: asset references and the writing application, both filled in by the engine on save.
std::string migrate3To4(json& doc) {
    const bool assets = addDefault(doc, "assets", json::object());
    const bool app = addDefault(doc, "app", json{{"name", "avgen"}, {"version", "unknown"}});
    return fmt::format("3 -> 4: {} 'assets'; {} 'app'", assets ? "added empty" : "kept existing",
                       app ? "added placeholder" : "kept existing");
}

} // namespace

Result<MigrationReport> migrateProject(json& doc) {
    const auto version = readEnvelope(doc);
    if (!version) {
        return std::unexpected(version.error());
    }
    MigrationReport report{.fromVersion = *version, .toVersion = kProjectFormatVersion};
    for (int v = *version; v < kProjectFormatVersion; ++v) {
        switch (v) {
        case 1:
            report.steps.push_back(migrate1To2(doc));
            break;
        case 2:
            report.steps.push_back(migrate2To3(doc));
            break;
        case 3:
            report.steps.push_back(migrate3To4(doc));
            break;
        default:
            return fail("no migration from project version {}", v);
        }
    }
    doc["version"] = kProjectFormatVersion;
    return report;
}

Result<void> loadProject(const json& original, ParameterSet& params, Modulator& modulator,
                         signals::SourceRack* sources, PresetBank* presets) {
    // Work on an upgraded copy so the caller's document (and its version) stays as it was.
    json doc = original;
    const auto migration = migrateProject(doc);
    if (!migration) {
        return std::unexpected(migration.error());
    }
    for (const std::string& step : migration->steps) {
        log::info("project migrated: {}", step);
    }

    // Validate everything before mutating anything so a bad file leaves the state untouched.
    // Sources and presets are parsed into temporaries even when the caller does not want them, so
    // a malformed document is rejected consistently. The scratch rack is attached to a scratch
    // parameter set so the document's source parameters are checked against the parameters the
    // new rack will register (the current rack's ones disappear when it is replaced).
    const auto sourcesJson = doc.find("sources");
    signals::SignalBus scratchBus;
    ParameterSet scratchParams;
    if (sourcesJson != doc.end()) {
        signals::SourceRack scratch;
        scratch.attach(scratchBus, scratchParams);
        if (auto parsed = scratch.fromJson(*sourcesJson); !parsed) {
            return parsed;
        }
    }
    PresetBank parsedPresets;
    const auto presetsJson = doc.find("presets");
    if (presetsJson != doc.end()) {
        if (auto parsed = parsedPresets.fromJson(*presetsJson); !parsed) {
            return parsed;
        }
    }
    // Pending values are kept by path: replacing the rack invalidates parameter pointers.
    std::vector<std::pair<std::string, const json*>> pending;
    if (const auto parameters = doc.find("parameters"); parameters != doc.end()) {
        if (!parameters->is_object()) {
            return fail("'parameters' must be an object");
        }
        for (const auto& [path, value] : parameters->items()) {
            const IParameter* param = sources != nullptr ? scratchParams.find(path) : nullptr;
            if (param == nullptr) {
                param = params.find(path);
            }
            if (param == nullptr) {
                log::warn("project references unknown parameter '{}'; ignored", path);
                continue;
            }
            if (auto check = validateParameterJson(*param, value); !check) {
                return check;
            }
            pending.emplace_back(path, &value);
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

    // Commit, in document order: sources, parameter values, routes, presets. A section that is
    // absent from the document means "none" for the sections that describe the project as a whole.
    if (sources != nullptr) {
        if (sourcesJson != doc.end()) {
            if (auto replaced = sources->fromJson(*sourcesJson); !replaced) {
                return replaced; // validated above; cannot fail
            }
        } else {
            sources->clear();
        }
    }
    for (const auto& [path, value] : pending) {
        IParameter* param = params.find(path);
        if (param == nullptr) {
            // Belonged to the replaced rack, or the new rack is attached elsewhere.
            log::warn("project parameter '{}' no longer exists after loading sources; ignored", path);
            continue;
        }
        if (auto applied = parameterFromJson(*param, *value); !applied) {
            return applied;
        }
    }
    // Replace the authored routes and keep the ones a subsystem installed. clearRoutes() here
    // used to take everything, which meant a scene's graph routes (ADR-028) and an entity's
    // reactions (ADR-087) were installed when the composition attached and deleted a few hundred
    // lines later by the project's own second parameter pass -- bound, counted in the log, and
    // then gone, which is exactly the shape of failure this codebase keeps shipping.
    {
        std::vector<ModRoute>& live = modulator.routes();
        live.erase(std::remove_if(live.begin(), live.end(),
                                  [](const ModRoute& r) { return !r.fromGraph && !r.fromEntity; }),
                   live.end());
    }
    for (auto& route : routes) {
        modulator.addRoute(std::move(route));
    }
    if (presets != nullptr) {
        *presets = std::move(parsedPresets);
    }
    return {};
}

Result<void> saveProjectFile(const std::filesystem::path& path, const ParameterSet& params,
                             const Modulator& modulator, const signals::SourceRack* sources,
                             const PresetBank* presets) {
    std::ofstream out(path);
    if (!out) {
        return fail("cannot open '{}' for writing", path.string());
    }
    out << saveProject(params, modulator, sources, presets).dump(2) << '\n';
    if (!out) {
        return fail("failed while writing '{}'", path.string());
    }
    return {};
}

Result<void> loadProjectFile(const std::filesystem::path& path, ParameterSet& params, Modulator& modulator,
                             signals::SourceRack* sources, PresetBank* presets) {
    std::ifstream in(path);
    if (!in) {
        return fail("cannot open '{}' for reading", path.string());
    }
    const json doc = json::parse(in, nullptr, false);
    if (doc.is_discarded()) {
        return fail("'{}' is not valid JSON", path.string());
    }
    if (auto loaded = loadProject(doc, params, modulator, sources, presets); !loaded) {
        return fail("'{}': {}", path.string(), loaded.error().message);
    }
    return {};
}

} // namespace avgen::params
