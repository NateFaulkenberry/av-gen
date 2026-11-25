// ADR-2152: the project document's typing table (`schemas/avgen-project.schema.json`).
//
// One entry per key of `kProjectKeys` (`app::Engine::projectFileKeys()`), and per key of the route and
// processor-chain lists (`params::routeFileKeys`, `params::chainFileKeys`). Each was typed by reading
// `Engine::loadProject`, `params::loadProject`, `routeFromJson`, `chainFromJson` and
// `RenderSettings::fromJson`; a block the Engine API PoC does not need is `x-avgen-untyped`.
//
// Every project entry is written back: a key in `kProjectKeys` is this build's to write or to leave
// out (ADR-2132), and a key outside it is kept verbatim.

#include "api/schema_types.hpp"

#include "app/engine.hpp"
#include "app/render_settings.hpp"
#include "audio/source_role.hpp"
#include "params/serialization.hpp"

#include <string>
#include <vector>

namespace avgen::api {
namespace {

using namespace typing;

void add(std::vector<TypingEntry>& out, std::string pointer, json fragment) {
    out.push_back({std::move(pointer), std::move(fragment), true, {}});
}

std::vector<std::string> keysOf(std::span<const std::string_view> keys) { return {keys.begin(), keys.end()}; }

void rootEntries(std::vector<TypingEntry>& e) {
    add(e, "/format", constant(params::kProjectFormatName));
    add(e, "/version",
        range(integer("Older versions are migrated in memory; a newer one is refused."), 1,
              params::kProjectFormatVersion));
    add(e, "/app", described(object({{"name", string()}, {"version", string()}}, "drop"),
                             "The writer's stamp, replaced on every save (api::version())."));
    add(e, "/parameters",
        described(json{{"type", "object"}, {"additionalProperties", true}},
                  "Overrides of registered parameters, path -> value (ADR-2132). A path no parameter answers is kept "
                  "for the next save. Paths come from parameter.search."));
    add(e, "/routes", arrayOf(ref("route"), "Modulation routes: a signal driving a parameter."));
    add(e, "/sources", untyped("Derived signals."));
    add(e, "/presets", untyped());
    add(e, "/shaders", untyped("Shader layers; each `path` is an asset reference."));
    add(e, "/timeline", untyped("Parameter automation."));
    add(e, "/assets", ref("assets"));
    for (const char* key : {"autoDirector", "songPlan", "directingPlans", "directingEvaluations", "cameraAimFollow",
                            "cameraContinuousTake", "cameraShotSpans", "parkedDirector"}) {
        add(e, std::string("/") + key, untyped("Director state."));
    }
    add(e, "/effects", untyped("The session's effects, over the scene's (ADR-702)."));
    add(e, "/heroes", untyped("The session's heroes, when they differ from the scene file's."));
    add(e, "/sceneNodes",
        described(object({{"added", arrayOf(ref("node"), "Nodes added in the session, as a scene file writes them.")},
                           {"removed", arrayOf(string(), "Names of scene-file nodes removed in the session.")}},
                          "drop"),
                  "ADR-330: the node edits a project makes to the scene file it names by path."));
    add(e, "/lights", untyped("The session's authored lights, when they differ from the scene file's."));
    for (const char* key : {"cameraDirection", "staging", "states", "palette", "director", "worldMacros",
                            "composition", "sequence"}) {
        add(e, std::string("/") + key, untyped());
    }
    add(e, "/render", ref("render"));
    add(e, "/pathtrace", untyped("ADR-366."));
    add(e, "/transport", untyped());
    add(e, "/feeds", untyped("ADR-1580: which sources feed analysis."));
    add(e, "/sonic", untyped("ADR-1020."));
    add(e, "/live", untyped("ADR-1100: the live block."));
    add(e, "/control", untyped());
    add(e, "/outputs", untyped());
    add(e, "/worldEffects", untyped("Refused at load (pre-ADR-702)."));
    add(e, "/atmosphericEffects", untyped("Refused at load (pre-ADR-702)."));
}

void routeEntries(std::vector<TypingEntry>& e) {
    const auto route = [&](const char* key, json fragment) {
        add(e, std::string("/routes/*/") + key, std::move(fragment));
    };
    route("source", string("A signal name (signal.list)."));
    route("target", string("A parameter path."));
    route("component", integer("Which component of a vector parameter; -1 for all."));
    route("amount", number());
    route("op", oneOfNames(params::modOpNames()));
    route("polarity", oneOfNames(params::polarityNames()));
    route("enabled", boolean());
    route("chain", ref("chain"));
    route("depthSource", string("ADR-900: a signal scaling the amount."));
    route("depthMin", number());
    route("depthMax", number());
    route("depthDefault", number("ADR-1570."));
    route("optional", boolean("ADR-1570."));
    route("planItem", string("ADR-924: the Director plan item that made the route."));

    const auto chain = [&](const char* key, json fragment) {
        add(e, std::string("/routes/*/chain/") + key, std::move(fragment));
    };
    chain("curve", oneOfNames(params::curveTypeNames()));
    chain("threshold", oneOfNames(params::thresholdModeNames()));
    chain("envelope", oneOfNames(params::envelopeModeNames()));
    for (const char* key : {"clampEnabled", "remapEnabled", "integrate"}) {
        chain(key, boolean());
    }
    chain("delayMs", atLeast(number("ADR-900."), 0));
    chain("normalizeSeconds", range(number("ADR-1182; 0 is off."), 0, 3600));
    chain("springHz", atLeast(number("ADR-1041; 0 is off."), 0));
    chain("springDamping", atLeast(number(), 0));
    for (const char* key : {"attackMs", "clampMax", "clampMin", "curveAmount", "decayMs", "envelopeFallPerSecond",
                            "envelopeHoldMs", "envelopeRearm", "envelopeRefractoryMs", "gain", "integrateMax",
                            "integrateMin", "normalizeFloor", "normalizeSmoothMs", "offset", "remapInMax",
                            "remapInMin", "remapOutMax", "remapOutMin", "thresholdLevel", "trendFastMs", "trendFloor",
                            "trendSeconds"}) {
        chain(key, number());
    }
}

json assetRefDef() {
    return anyOf(
        {string("A path relative to the project file (the older form, still read)."),
         json{{"type", "object"},
              {"required", {"path"}},
              {"properties",
               {{"path", string("Relative to the project file.")},
                {"size", atLeast(integer("Bytes; with sha256, relinks a moved file."), 0)},
                {"sha256", string()}}},
              {"additionalProperties", true},
              {"x-avgen-unknownKeys", "drop"}}},
        "A file the project names. A missing file is searched for under the project folder by name, size and hash.");
}

json assetsDef() {
    std::vector<std::string> roles(audio::kSourceRoleNames.begin(), audio::kSourceRoleNames.end());
    json clip = object({{"file", ref("assetRef")},
                        {"start", number("Seconds on the timeline.")},
                        {"in", number("Seconds into the file.")},
                        {"duration", number()},
                        {"gain", number()},
                        {"fadeIn", number()},
                        {"fadeOut", number()},
                        {"enabled", boolean()},
                        {"name", string()}},
                       "drop", {"file"});
    json stem = object({{"file", ref("assetRef")},
                        {"key", string("The stem's signal key: stem.<key>.*.")},
                        {"label", string()},
                        {"offset", number("Seconds.")},
                        {"role", oneOfNames(roles, "ADR-1630: what the stem is. Any other value is read as other, with a warning.")},
                        {"gainDb", number("Clamped to the analysis trim range.")}},
                       "drop", {"file"});
    json sceneRef = object({{"kind", oneOfNames(std::vector<std::string>{"orb", "gltf", "composition"},
                                                "The three kinds Engine::loadProject branches on; orb when absent.")},
                            {"path", ref("assetRef")},
                            {"inline", described(json{{"type", "object"}, {"additionalProperties", true}},
                                                 "A whole scene document (avgen-scene.schema.json).")}},
                           "drop");
    return object({{"audio", ref("assetRef")},
                   {"audioClips", arrayOf(std::move(clip), "An arrangement; wins over `audio` (never both written).")},
                   {"stems", arrayOf(std::move(stem), "ADR-1520: analysis stems.")},
                   {"scene", std::move(sceneRef)},
                   {"environment", ref("assetRef")}},
                  "drop");
}

json renderDef() {
    std::vector<std::string> outputs;
    for (const app::RenderOutputSpelling& s : app::renderOutputSpellings()) {
        outputs.emplace_back(s.name);
    }
    json props = {{"output", oneOfNames(outputs, "png is read as sequence.")},
                  {"supersample", number()},
                  {"muxAudio", boolean()}};
    for (const char* key : {"width", "height", "fps", "start", "end", "quality", "encoderThreads"}) {
        props[key] = number();
    }
    for (const char* key : {"path", "pattern", "codec", "tier", "limits", "backend"}) {
        props[key] = string();
    }
    std::string aovs = "Comma-separated, from:";
    for (const std::string_view a : app::RenderSettings::aovNames()) {
        aovs += " " + std::string(a);
    }
    props["aovs"] = string(aovs);
    return described(object(std::move(props), "drop"), "Offline render settings (RenderSettings::fromJson).");
}

} // namespace

DocumentTyping projectTyping() {
    DocumentTyping t;
    t.tableFile = "src/api/schema_types_project.cpp";
    t.keyLists = {
        {"", "", keysOf(app::Engine::projectFileKeys()), "kProjectKeys in src/app/engine_project.cpp",
         object({}, "keep", {"format", "version"})},
        {"/routes/*", "route", keysOf(params::routeFileKeys()), "kRouteKeys in src/params/serialization.cpp",
         object({}, "warn-drop", {"source", "target"})},
        {"/routes/*/chain", "chain", keysOf(params::chainFileKeys()), "kChainKeys in src/params/serialization.cpp",
         object({}, "drop")},
    };
    rootEntries(t.entries);
    routeEntries(t.entries);
    t.defs = sceneNodeDefs();
    t.defs["assetRef"] = assetRefDef();
    t.defs["assets"] = assetsDef();
    t.defs["render"] = renderDef();
    return t;
}

} // namespace avgen::api
