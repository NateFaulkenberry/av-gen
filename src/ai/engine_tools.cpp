#include "ai/engine_tools.hpp"

#include "ai/capabilities.hpp"
#include "ai/tool_context.hpp"
#include "ai/transaction.hpp"
#include "app/engine.hpp"
#include "core/log.hpp"
#include "params/serialization.hpp"
#include "scene/composition.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace avgen::ai {
namespace {

using nlohmann::json;

// ---- shared helpers ---------------------------------------------------------------------------

constexpr std::size_t kDefaultLimit = 40;
constexpr std::size_t kMaxLimit = 200;

std::size_t limitOf(const json& args) {
    const auto raw = args.value("limit", static_cast<double>(kDefaultLimit));
    const auto clamped = std::clamp(raw, 1.0, static_cast<double>(kMaxLimit));
    return static_cast<std::size_t>(clamped);
}

bool containsNoCase(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    const auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                                [](char a, char b) {
                                    return std::tolower(static_cast<unsigned char>(a)) ==
                                           std::tolower(static_cast<unsigned char>(b));
                                });
    return it != haystack.end();
}

const char* paramKindName(params::ParamKind kind) {
    switch (kind) {
    case params::ParamKind::Float: return "float";
    case params::ParamKind::Int: return "int";
    case params::ParamKind::Bool: return "bool";
    case params::ParamKind::Vec2: return "vec2";
    case params::ParamKind::Vec3: return "vec3";
    case params::ParamKind::Vec4: return "vec4";
    case params::ParamKind::Color: return "color";
    }
    return "float";
}

std::vector<float> baseValues(const params::IParameter& p) {
    std::vector<float> out;
    out.reserve(p.componentCount());
    for (std::size_t i = 0; i < p.componentCount(); ++i) {
        out.push_back(p.baseComponent(i));
    }
    return out;
}

std::vector<float> finalValues(const params::IParameter& p) {
    std::vector<float> out;
    out.reserve(p.componentCount());
    for (std::size_t i = 0; i < p.componentCount(); ++i) {
        out.push_back(p.finalComponent(i));
    }
    return out;
}

// A scalar parameter reads back as a number, a vector as an array. Wrapping a float in a
// one-element array is technically uniform and practically an invitation to get it wrong.
json valueJson(const std::vector<float>& values) {
    if (values.size() == 1) {
        return values[0];
    }
    return values;
}

// Compact by default (§39): path, kind and value. `full` adds the metadata an agent needs before
// it decides what to write -- ranges, default, group, label.
json paramJson(const params::IParameter& p, bool full) {
    json j;
    j["path"] = p.path();
    j["kind"] = paramKindName(p.kind());
    j["value"] = valueJson(baseValues(p));
    const std::vector<float> fin = finalValues(p);
    if (fin != baseValues(p)) {
        // The modulated value this frame. Different from the base means something is driving it,
        // which is exactly the thing an agent must notice before it "sets" a value.
        j["modulatedValue"] = valueJson(fin);
    }
    if (full) {
        j["label"] = p.label();
        j["group"] = p.group();
        j["default"] = valueJson([&] {
            std::vector<float> d;
            for (std::size_t i = 0; i < p.componentCount(); ++i) {
                d.push_back(p.defaultComponent(i));
            }
            return d;
        }());
        j["hardMin"] = p.hardMin(0);
        j["hardMax"] = p.hardMax(0);
        j["softMin"] = p.softMin(0);
        j["softMax"] = p.softMax(0);
        j["modulatable"] = p.flags().modulatable;
        j["components"] = p.componentCount();
    }
    return j;
}

// Everything that would overwrite a base value before it reaches the frame. This is guard (2) in
// the header: the difference between "I set it" and "I set it and it took effect".
std::vector<std::string> overridesFor(app::Engine& engine, const std::string& path, int component) {
    std::vector<std::string> notes;
    for (const params::Track& track : engine.timeline().tracks()) {
        if (track.target != path || !track.enabled || track.param == nullptr) {
            continue;
        }
        if (component >= 0 && track.component >= 0 && track.component != component) {
            continue;
        }
        if (track.mode == params::TrackMode::Replace && engine.timeline().enabled) {
            notes.push_back(fmt::format(
                "timeline track on '{}' is in replace mode and rewrites this value every frame",
                path));
        }
    }
    for (const params::ModRoute& route : engine.modulator().routes()) {
        if (route.target != path || !route.enabled || route.targetParam == nullptr) {
            continue;
        }
        if (component >= 0 && route.component >= 0 && route.component != component) {
            continue;
        }
        if (route.op == params::ModOp::Replace) {
            notes.push_back(fmt::format("modulation route '{}' -> '{}' replaces this value",
                                        route.source, path));
        } else if (route.op == params::ModOp::Multiply &&
                   std::abs(route.amount) > 0.0f) {
            notes.push_back(fmt::format("modulation route '{}' -> '{}' multiplies this value",
                                        route.source, path));
        }
    }
    return notes;
}

// Reads the argument as a value vector for a parameter of `count` components. Accepts a scalar for
// a scalar parameter, a scalar broadcast to a vector, or an array of exactly `count`.
Result<std::vector<float>> readValueArg(const json& value, std::size_t count) {
    std::vector<float> out;
    if (value.is_number()) {
        out.assign(count, static_cast<float>(value.get<double>()));
        return out;
    }
    if (value.is_boolean()) {
        out.assign(count, value.get<bool>() ? 1.0f : 0.0f);
        return out;
    }
    if (value.is_array()) {
        if (value.size() != count) {
            return fail("expected {} component(s), got {}", count, value.size());
        }
        for (const auto& v : value) {
            if (!v.is_number()) {
                return fail("component values must be numbers");
            }
            out.push_back(static_cast<float>(v.get<double>()));
        }
        return out;
    }
    return fail("value must be a number, a boolean or an array of numbers");
}

ToolResult notFoundParameter(const std::string& path) {
    return ToolResult::failure(
        ToolErrorCode::NotFound, fmt::format("no parameter '{}'", path),
        "call parameter.search with a fragment of the name to find the canonical path");
}

// The heart of guard (2) and the "read it back" rule: write, re-read, report what landed.
struct SetOutcome {
    std::vector<float> before;
    std::vector<float> after;
    bool clamped = false;
    std::vector<std::string> overrides;
};

SetOutcome setParameter(app::Engine& engine, params::IParameter& param,
                        const std::vector<float>& values, int component) {
    SetOutcome out;
    out.before = baseValues(param);
    for (std::size_t i = 0; i < param.componentCount(); ++i) {
        if (component >= 0 && static_cast<int>(i) != component) {
            continue;
        }
        param.setBaseComponent(i, values[i]);
    }
    out.after = baseValues(param);
    for (std::size_t i = 0; i < param.componentCount(); ++i) {
        if (component >= 0 && static_cast<int>(i) != component) {
            continue;
        }
        if (std::abs(out.after[i] - values[i]) > 1e-5f) {
            out.clamped = true;
        }
    }
    out.overrides = overridesFor(engine, param.path(), component);
    return out;
}

json outcomeJson(const params::IParameter& param, const SetOutcome& outcome) {
    json j;
    j["path"] = param.path();
    j["previous"] = valueJson(outcome.before);
    j["value"] = valueJson(outcome.after);
    if (outcome.clamped) {
        j["clamped"] = true;
        j["hardMin"] = param.hardMin(0);
        j["hardMax"] = param.hardMax(0);
    }
    if (!outcome.overrides.empty()) {
        // Not an error: overriding is what automation is for. It is reported because an agent that
        // does not know its write is being overwritten will report success for a change nobody
        // will ever see.
        j["overridden"] = outcome.overrides;
    }
    return j;
}

// ---- node helpers ----------------------------------------------------------------------------

const scene::CompositionNode* findNode(app::Engine& engine, const std::string& name) {
    auto* comp = engine.composition();
    if (comp == nullptr) {
        return nullptr;
    }
    for (const auto& node : comp->nodes()) {
        if (node && node->name == name) {
            return node.get();
        }
    }
    return nullptr;
}

json nodeSummary(const scene::CompositionNode& node, bool full) {
    json j;
    j["name"] = node.name;
    j["kind"] = scene::nodeKindName(node.kind);
    if (!node.parent.empty()) {
        j["parent"] = node.parent;
    }
    j["visible"] = node.visible;
    j["parameterPrefix"] = "nodes/" + node.name + "/";
    if (full) {
        if (node.positionParam != nullptr) {
            j["position"] = valueJson(baseValues(*node.positionParam));
        }
        if (node.rotationParam != nullptr) {
            j["rotationDegrees"] = valueJson(baseValues(*node.rotationParam));
        }
        if (node.scaleParam != nullptr) {
            j["scale"] = valueJson(baseValues(*node.scaleParam));
        }
        if (node.emissiveParam != nullptr) {
            j["emissiveBoost"] = node.emissiveParam->base();
        }
        if (node.roughnessParam != nullptr) {
            j["roughnessScale"] = node.roughnessParam->base();
        }
        if (!node.asset.empty()) {
            j["asset"] = node.asset.generic_string();
        }
        if (!node.materialPartParams.empty()) {
            j["materialParts"] = node.materialPartParams.size();
        }
    }
    return j;
}

// ---- registration ----------------------------------------------------------------------------

ToolAnnotations readOnly() {
    ToolAnnotations a;
    a.readOnly = true;
    a.idempotent = true;
    a.requiresMainThread = true; // engine state is read by the frame loop; a read still needs the queue
    return a;
}

ToolAnnotations mutating() {
    ToolAnnotations a;
    a.mutatesProject = true;
    a.idempotent = true;
    a.undoable = true; // the parameter domain is exactly what a snapshot covers (transaction.hpp)
    a.requiresMainThread = true;
    return a;
}

void add(ToolRegistry& r, std::string name, std::string title, std::string description,
         json inputSchema, ToolAnnotations annotations, ToolFn fn) {
    Tool tool;
    tool.definition.name = std::move(name);
    tool.definition.title = std::move(title);
    tool.definition.description = std::move(description);
    tool.definition.inputSchema = std::move(inputSchema);
    tool.definition.annotations = annotations;
    tool.execute = std::move(fn);
    r.add(std::move(tool));
}

json noArgs() { return schema::object(json::object()); }

// ================================================================================================
// capability.*
// ================================================================================================

void registerCapabilityTools(ToolRegistry& registry) {
    add(registry, "capability.list", "What AV Gen supports",
        "The capability registry: every engine domain, whether it is reachable from here, what "
        "this particular session actually contains, and -- importantly -- the domains that are NOT "
        "exposed and why. Call this before planning anything ambitious; it will save you from "
        "trying to do something this build cannot do.",
        noArgs(), readOnly(),
        [&registry](const json&, ToolContext& ctx) -> ToolResult {
            json doc = capabilityDocument(registry, ctx.engine());
            std::size_t unavailable = 0;
            for (const auto& domain : doc.value("domains", json::array())) {
                if (!domain.value("available", true)) {
                    ++unavailable;
                }
            }
            return ToolResult::ok(std::move(doc),
                                  fmt::format("{} domain(s), {} unavailable",
                                              doc["domains"].size(), unavailable));
        });

    add(registry, "capability.list_tools", "List tools",
        "List every operation this build of AV Gen exposes, with a one-line description. Call this "
        "when a tool name is rejected, or to discover what is available before planning.",
        schema::object({{"domain", schema::string("Only tools in this namespace, e.g. 'parameter'")}}),
        readOnly(),
        [&registry](const json& args, ToolContext&) -> ToolResult {
            const std::string domain = args.value("domain", std::string{});
            json tools = json::array();
            for (const Tool* tool : registry.all()) {
                if (!domain.empty() && tool->definition.domain() != domain) {
                    continue;
                }
                json t;
                t["name"] = tool->definition.name;
                t["description"] = tool->definition.description;
                t["readOnly"] = tool->definition.annotations.readOnly;
                t["mutatesProject"] = tool->definition.annotations.mutatesProject;
                tools.push_back(std::move(t));
            }
            json out;
            out["tools"] = tools;
            out["count"] = tools.size();
            return ToolResult::ok(std::move(out), fmt::format("{} tool(s)", out["count"].get<int>()));
        });
}

// ================================================================================================
// project.*
// ================================================================================================

void registerProjectTools(ToolRegistry& registry, SnapshotStore& snapshots) {
    add(registry, "project.get_state", "Project state",
        "High-level state of the open project: file, scene, audio, transport, and how much of each "
        "kind of thing exists. Start here; it is cheap and it tells you what to ask about next.",
        noArgs(), readOnly(),
        [](const json&, ToolContext& ctx) -> ToolResult {
            app::Engine& engine = ctx.engine();
            json j;
            j["projectFile"] = engine.projectPath().empty()
                                   ? json(nullptr)
                                   : json(engine.projectPath().filename().generic_string());
            j["sceneKind"] = engine.composition() != nullptr ? "composition"
                             : engine.gltfScene() != nullptr ? "gltf"
                                                             : "orb";
            if (auto* comp = engine.composition()) {
                j["nodeCount"] = comp->nodes().size();
            }
            j["parameterCount"] = engine.params().size();
            j["modulationRoutes"] = engine.modulator().routes().size();
            j["timelineTracks"] = engine.timeline().tracks().size();
            j["timelineCues"] = engine.timeline().cues().size();
            j["presets"] = engine.presets().presets().size();
            j["signals"] = engine.signals().size();
            j["hasAudio"] = engine.hasAudio();
            if (engine.hasAudio()) {
                j["audioFile"] = engine.audioPath().filename().generic_string();
                j["durationSeconds"] = engine.durationSeconds();
            }
            j["playing"] = engine.isPlaying();
            j["positionSeconds"] = engine.positionSeconds();
            j["beats"] = engine.timelineClock().beats;
            j["lights"] = engine.scene().lights.size();
            j["entities"] = engine.scene().entities.size();
            if (!engine.projectWarnings().empty()) {
                // Surfaced deliberately: an unresolved binding is the engine already knowing that
                // something in this project does nothing.
                j["warnings"] = engine.projectWarnings();
            }
            if (!engine.timeline().unboundTargets().empty()) {
                j["unboundTimelineTargets"] = engine.timeline().unboundTargets();
            }
            return ToolResult::ok(std::move(j), "project state");
        });

    add(registry, "project.create_snapshot", "Create rollback point",
        "Capture a restorable snapshot of the parameter domain (parameter values, modulation "
        "routes, presets and the timeline). Take one before substantial work. The AI task wrapper "
        "already takes one automatically; this is for a checkpoint inside a long task.",
        schema::object({{"label", schema::string("Why this point is being kept")}}),
        [] {
            ToolAnnotations a = mutating();
            a.mutatesProject = false; // it records state, it does not change it
            a.readOnly = true;
            return a;
        }(),
        [&snapshots](const json& args, ToolContext& ctx) -> ToolResult {
            const auto& snapshot =
                snapshots.capture(ctx.engine(), args.value("label", std::string("manual")));
            return ToolResult::ok(snapshot.describe(), "snapshot " + snapshot.id);
        });

    add(registry, "project.list_snapshots", "List rollback points",
        "The snapshots taken this session, oldest first.", noArgs(), readOnly(),
        [&snapshots](const json&, ToolContext&) -> ToolResult {
            json list = json::array();
            for (const ProjectSnapshot& s : snapshots.all()) {
                list.push_back(s.describe());
            }
            json out;
            out["snapshots"] = std::move(list);
            return ToolResult::ok(std::move(out), fmt::format("{} snapshot(s)", snapshots.size()));
        });

    add(registry, "project.restore_snapshot", "Restore rollback point",
        "Put the parameter domain back to a snapshot. Everything changed since is discarded.",
        schema::object({{"id", schema::string("Snapshot id from project.create_snapshot")}}, {"id"}),
        [] {
            ToolAnnotations a = mutating();
            a.destructive = true;
            a.idempotent = false;
            return a;
        }(),
        [&snapshots](const json& args, ToolContext& ctx) -> ToolResult {
            const std::string id = args.at("id").get<std::string>();
            const json before = SnapshotStore::captureDocument(ctx.engine());
            if (auto r = snapshots.restore(ctx.engine(), id); !r) {
                return ToolResult::failure(ToolErrorCode::NotFound, r.error().message,
                                           "call project.list_snapshots for valid ids");
            }
            const json after = SnapshotStore::captureDocument(ctx.engine());
            const auto changed = SnapshotStore::changedParameters(before, after);
            json out;
            out["restored"] = id;
            out["parametersChanged"] = changed.size();
            if (changed.size() <= 20) {
                out["paths"] = changed;
            }
            ctx.changes().note(id, fmt::format("restored, {} parameter(s) changed", changed.size()));
            return ToolResult::ok(std::move(out), "restored " + id);
        });
}

// ================================================================================================
// parameter.*
// ================================================================================================

void registerParameterTools(ToolRegistry& registry) {
    add(registry, "parameter.list_groups", "Parameter groups",
        "The top-level parameter groups in this scene and how many parameters each holds. The "
        "cheapest way to orient yourself before searching: groups are things like 'camera', 'env', "
        "'scene', 'nodes', 'procedural', 'post', 'layers'.",
        noArgs(), readOnly(),
        [](const json&, ToolContext& ctx) -> ToolResult {
            std::vector<std::pair<std::string, int>> groups;
            for (const params::IParameter* p : ctx.engine().params().ordered()) {
                const std::string& g = p->group();
                auto it = std::find_if(groups.begin(), groups.end(),
                                       [&](const auto& e) { return e.first == g; });
                if (it == groups.end()) {
                    groups.emplace_back(g, 1);
                } else {
                    ++it->second;
                }
            }
            std::sort(groups.begin(), groups.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });
            json out = json::object();
            json list = json::array();
            for (const auto& [name, count] : groups) {
                list.push_back(json{{"group", name}, {"parameters", count}});
            }
            out["groups"] = std::move(list);
            out["total"] = ctx.engine().params().size();
            return ToolResult::ok(std::move(out),
                                  fmt::format("{} group(s), {} parameter(s)", groups.size(),
                                              ctx.engine().params().size()));
        });

    add(registry, "parameter.search", "Search parameters",
        "Find parameters by a fragment of their path or label. This is the primary way to discover "
        "what a scene actually exposes -- node transforms, sky and sun, fog, wind, camera, post, "
        "material emission. Results carry canonical paths; never guess a path.",
        schema::object(
            {{"query", schema::string("Case-insensitive fragment, e.g. 'emissive', 'sky', 'fog'")},
             {"group", schema::string("Restrict to one group")},
             {"modulatableOnly", schema::boolean("Only parameters that can be a modulation target")},
             {"limit", schema::integer("Maximum results (default 40, max 200)", 1, kMaxLimit)},
             {"offset", schema::integer("Skip this many results", 0)}}),
        readOnly(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            const std::string query = args.value("query", std::string{});
            const std::string group = args.value("group", std::string{});
            const bool modOnly = args.value("modulatableOnly", false);
            const std::size_t limit = limitOf(args);
            const auto offset = static_cast<std::size_t>(args.value("offset", 0.0));

            std::vector<const params::IParameter*> matches;
            for (const params::IParameter* p : ctx.engine().params().ordered()) {
                if (!group.empty() && p->group() != group) {
                    continue;
                }
                if (modOnly && !p->flags().modulatable) {
                    continue;
                }
                if (!containsNoCase(p->path(), query) && !containsNoCase(p->label(), query)) {
                    continue;
                }
                matches.push_back(p);
            }
            json list = json::array();
            for (std::size_t i = offset; i < matches.size() && list.size() < limit; ++i) {
                list.push_back(paramJson(*matches[i], false));
            }
            json out;
            out["parameters"] = std::move(list);
            out["matched"] = matches.size();
            out["returned"] = out["parameters"].size();
            if (offset + out["returned"].get<std::size_t>() < matches.size()) {
                out["nextOffset"] = offset + out["returned"].get<std::size_t>();
            }
            return ToolResult::ok(std::move(out),
                                  fmt::format("{} match(es) for '{}'", matches.size(), query));
        });

    add(registry, "parameter.get", "Get parameters",
        "Full metadata for named parameters: current base value, the modulated value in effect "
        "this frame, default, hard and soft ranges, group and whether it can be modulated. Read "
        "before writing -- ranges here are what a set will be clamped to.",
        schema::object({{"paths", schema::array(schema::string("Parameter path"),
                                                "Canonical parameter paths", 1, 32)}},
                       {"paths"}),
        readOnly(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            json found = json::array();
            json missing = json::array();
            for (const auto& entry : args.at("paths")) {
                const std::string path = entry.get<std::string>();
                const params::IParameter* p = ctx.engine().params().find(path);
                if (p == nullptr) {
                    missing.push_back(path);
                    continue;
                }
                json j = paramJson(*p, true);
                const auto notes = overridesFor(ctx.engine(), path, -1);
                if (!notes.empty()) {
                    j["overridden"] = notes;
                }
                if (ctx.engine().timeline().isAutomated(path)) {
                    j["automated"] = true;
                }
                found.push_back(std::move(j));
            }
            if (found.empty() && !missing.empty()) {
                return ToolResult::failure(
                    ToolErrorCode::NotFound,
                    fmt::format("none of the {} path(s) exist", missing.size()),
                    "call parameter.search to find canonical paths");
            }
            json out;
            out["parameters"] = std::move(found);
            if (!missing.empty()) {
                out["notFound"] = std::move(missing);
            }
            return ToolResult::ok(std::move(out), "parameters");
        });

    add(registry, "parameter.set", "Set a parameter",
        "Set a parameter's base value. Accepts a number for a scalar, or an array with one entry "
        "per component for a vector or colour. Values outside the parameter's hard range are "
        "clamped and the result says so. If a timeline track or a modulation route is overwriting "
        "this value every frame, the result says that too -- a set that is silently overridden "
        "looks exactly like a set that worked.",
        schema::object(
            {{"path", schema::string("Canonical parameter path")},
             {"value", json{{"description", "Number, boolean, or array of numbers"}}},
             {"component",
              schema::integer("Write only this component (0-based). Omit to write all.", 0, 3)}},
            {"path", "value"}),
        mutating(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            const std::string path = args.at("path").get<std::string>();
            params::IParameter* p = ctx.engine().params().find(path);
            if (p == nullptr) {
                return notFoundParameter(path);
            }
            const int component = args.contains("component")
                                      ? static_cast<int>(args.at("component").get<double>())
                                      : -1;
            if (component >= static_cast<int>(p->componentCount())) {
                return ToolResult::failure(
                    ToolErrorCode::OutOfRange,
                    fmt::format("'{}' has {} component(s); {} was requested", path,
                                p->componentCount(), component),
                    "omit 'component' to write the whole value");
            }
            auto values = readValueArg(args.at("value"), p->componentCount());
            if (!values) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments, values.error().message,
                                           fmt::format("'{}' is a {} with {} component(s)", path,
                                                       paramKindName(p->kind()),
                                                       p->componentCount()));
            }
            const SetOutcome outcome = setParameter(ctx.engine(), *p, *values, component);
            ctx.changes().note(path,
                               fmt::format("{} -> {}", valueJson(outcome.before).dump(),
                                           valueJson(outcome.after).dump()),
                               outcome.clamped);
            return ToolResult::ok(outcomeJson(*p, outcome),
                                  fmt::format("{} = {}", path, valueJson(outcome.after).dump()));
        });

    add(registry, "parameter.reset", "Reset a parameter",
        "Put a parameter back to the value the scene declared as its default.",
        schema::object({{"path", schema::string("Canonical parameter path")}}, {"path"}), mutating(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            const std::string path = args.at("path").get<std::string>();
            params::IParameter* p = ctx.engine().params().find(path);
            if (p == nullptr) {
                return notFoundParameter(path);
            }
            const std::vector<float> before = baseValues(*p);
            p->resetToDefault();
            json out;
            out["path"] = path;
            out["previous"] = valueJson(before);
            out["value"] = valueJson(baseValues(*p));
            ctx.changes().note(path, "reset to default");
            return ToolResult::ok(std::move(out), path + " reset");
        });
}

// ================================================================================================
// scene.*
// ================================================================================================

void registerSceneTools(ToolRegistry& registry) {
    add(registry, "scene.get_summary", "Scene summary",
        "What is in the scene: the node list with kinds, the light count, the camera, the bounds, "
        "and the counts of meshes, entities, particles and procedural objects. Deliberately a "
        "summary -- use scene.find_nodes and scene.get_node for detail rather than asking for "
        "everything.",
        noArgs(), readOnly(),
        [](const json&, ToolContext& ctx) -> ToolResult {
            app::Engine& engine = ctx.engine();
            const scene::Scene& scene = engine.scene();
            json j;
            json nodes = json::array();
            if (auto* comp = engine.composition()) {
                for (const auto& node : comp->nodes()) {
                    if (node) {
                        nodes.push_back(nodeSummary(*node, false));
                    }
                }
            }
            j["nodes"] = std::move(nodes);
            j["counts"] = json{{"meshes", scene.meshes.size()},
                               {"entities", scene.entities.size()},
                               {"lights", scene.lights.size()},
                               {"particleSystems", scene.particles.size()},
                               {"procedurals", scene.procedurals.size()},
                               {"sdfs", scene.sdfs.size()},
                               {"splines", scene.splines.splines.size()},
                               {"rigs", scene.rigs.size()}};
            const auto [lo, hi] = scene.bounds();
            j["bounds"] = json{{"min", {lo.x, lo.y, lo.z}}, {"max", {hi.x, hi.y, hi.z}}};
            j["camera"] = json{{"position", {scene.camera.position.x, scene.camera.position.y,
                                             scene.camera.position.z}},
                               {"target", {scene.camera.target.x, scene.camera.target.y,
                                           scene.camera.target.z}},
                               {"fovDegrees", glm::degrees(scene.camera.effectiveFovY())}};
            j["environmentMapLoaded"] = !engine.environmentPath().empty();
            return ToolResult::ok(std::move(j), fmt::format("{} node(s), {} light(s)",
                                                            j["nodes"].size(), scene.lights.size()));
        });

    add(registry, "scene.find_nodes", "Find nodes",
        "Find composition nodes by a fragment of their name or by kind. Returns canonical node "
        "names and each node's parameter prefix, which is what every other tool wants.",
        schema::object({{"query", schema::string("Case-insensitive name fragment")},
                        {"kind", schema::string("Restrict to one node kind",
                                                {"gltf", "orb", "grid", "particles", "scene",
                                                 "procedural", "field", "spline", "sdf", "terrain"})},
                        {"limit", schema::integer("Maximum results", 1, kMaxLimit)}}),
        readOnly(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            auto* comp = ctx.engine().composition();
            if (comp == nullptr) {
                return ToolResult::failure(
                    ToolErrorCode::Unavailable,
                    "the active scene is not a composition, so it has no named nodes",
                    "call scene.get_summary; parameter.search still finds everything this scene "
                    "exposes");
            }
            const std::string query = args.value("query", std::string{});
            const std::string kind = args.value("kind", std::string{});
            const std::size_t limit = limitOf(args);
            json list = json::array();
            std::size_t matched = 0;
            for (const auto& node : comp->nodes()) {
                if (!node) {
                    continue;
                }
                if (!kind.empty() && scene::nodeKindName(node->kind) != kind) {
                    continue;
                }
                if (!containsNoCase(node->name, query)) {
                    continue;
                }
                ++matched;
                if (list.size() < limit) {
                    list.push_back(nodeSummary(*node, false));
                }
            }
            json out;
            out["nodes"] = std::move(list);
            out["matched"] = matched;
            return ToolResult::ok(std::move(out), fmt::format("{} node(s)", matched));
        });

    add(registry, "scene.get_node", "Get a node",
        "Everything about one node: transform, visibility, emissive boost, roughness scale, asset, "
        "and the parameter paths that drive it.",
        schema::object({{"name", schema::string("Node name from scene.find_nodes")}}, {"name"}),
        readOnly(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            const std::string name = args.at("name").get<std::string>();
            const scene::CompositionNode* node = findNode(ctx.engine(), name);
            if (node == nullptr) {
                return ToolResult::failure(ToolErrorCode::NotFound,
                                           fmt::format("no node named '{}'", name),
                                           "call scene.find_nodes to list the canonical names");
            }
            json j = nodeSummary(*node, true);
            json paths = json::array();
            const std::string prefix = "nodes/" + node->name + "/";
            for (const params::IParameter* p : ctx.engine().params().ordered()) {
                if (p->path().rfind(prefix, 0) == 0) {
                    paths.push_back(p->path());
                }
            }
            j["parameters"] = std::move(paths);
            return ToolResult::ok(std::move(j), "node " + name);
        });

    add(registry, "scene.set_node_transform", "Move a node",
        "Set a node's position, rotation (Euler degrees) or scale. Any field may be omitted to "
        "leave it alone. This writes the node's transform parameters, so it keyframes, modulates "
        "and saves like every other value.",
        schema::object({{"name", schema::string("Node name")},
                        {"position", schema::array(schema::number("metres"), "World position", 3, 3)},
                        {"rotation", schema::array(schema::number("degrees"), "Euler XYZ", 3, 3)},
                        {"scale", schema::array(schema::number("factor"), "Per-axis scale", 3, 3)}},
                       {"name"}),
        mutating(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            const std::string name = args.at("name").get<std::string>();
            const scene::CompositionNode* node = findNode(ctx.engine(), name);
            if (node == nullptr) {
                return ToolResult::failure(ToolErrorCode::NotFound,
                                           fmt::format("no node named '{}'", name),
                                           "call scene.find_nodes to list the canonical names");
            }
            json out;
            out["name"] = name;
            bool wrote = false;
            const auto apply = [&](const char* key, params::IParameter* param) {
                if (param == nullptr || !args.contains(key)) {
                    return;
                }
                auto values = readValueArg(args.at(key), param->componentCount());
                if (!values) {
                    return;
                }
                const SetOutcome outcome = setParameter(ctx.engine(), *param, *values, -1);
                out[key] = outcomeJson(*param, outcome);
                ctx.changes().note(param->path(), valueJson(outcome.after).dump(), outcome.clamped);
                wrote = true;
            };
            apply("position", node->positionParam);
            apply("rotation", node->rotationParam);
            apply("scale", node->scaleParam);
            if (!wrote) {
                return ToolResult::failure(
                    ToolErrorCode::InvalidArguments,
                    fmt::format("nothing to write: node '{}' has no transform parameters, or no "
                                "position/rotation/scale was given",
                                name),
                    "pass at least one of position, rotation, scale");
            }
            return ToolResult::ok(std::move(out), "moved " + name);
        });

    add(registry, "scene.set_node_visible", "Show or hide a node",
        "Set a node's visibility. On a nested scene node this propagates through the whole subtree.",
        schema::object({{"name", schema::string("Node name")},
                        {"visible", schema::boolean("Whether the node is drawn")}},
                       {"name", "visible"}),
        mutating(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            const std::string name = args.at("name").get<std::string>();
            const scene::CompositionNode* node = findNode(ctx.engine(), name);
            if (node == nullptr || node->visibleParam == nullptr) {
                return ToolResult::failure(
                    ToolErrorCode::NotFound,
                    fmt::format("no node named '{}' with a visibility parameter", name),
                    "call scene.find_nodes to list the canonical names");
            }
            const bool value = args.at("visible").get<bool>();
            node->visibleParam->setBase(value);
            ctx.changes().note(node->visibleParam->path(), value ? "visible" : "hidden");
            json out;
            out["name"] = name;
            out["visible"] = node->visibleParam->base();
            return ToolResult::ok(std::move(out),
                                  fmt::format("{} {}", name, value ? "shown" : "hidden"));
        });
}

// ================================================================================================
// camera.*
// ================================================================================================

// Orbit is mode 0, free is 1, spline is 2 (scene/composition.cpp). Writing position and target in
// orbit mode changes nothing at all, which is guard (3).
constexpr float kCameraModeFree = 1.0f;

void registerCameraTools(ToolRegistry& registry) {
    add(registry, "camera.get", "Camera state",
        "The camera's pose, mode, field of view, and the lens, exposure and focus settings that "
        "are exposed as parameters. Read this before moving the camera: in orbit mode the position "
        "and target parameters are ignored, and the mode is part of the answer.",
        noArgs(), readOnly(),
        [](const json&, ToolContext& ctx) -> ToolResult {
            app::Engine& engine = ctx.engine();
            json j;
            const auto read = [&](const char* path) -> json {
                const params::IParameter* p = engine.params().find(path);
                return p == nullptr ? json(nullptr) : valueJson(baseValues(*p));
            };
            j["mode"] = read("camera/mode");
            j["modeNames"] = json{{"0", "orbit"}, {"1", "free"}, {"2", "spline"}};
            j["position"] = read("camera/position");
            j["target"] = read("camera/target");
            j["fovDegrees"] = read("camera/fov");
            j["orbitDistance"] = read("camera/distance");
            j["orbitHeight"] = read("camera/height");
            j["effective"] =
                json{{"position", {engine.scene().camera.position.x, engine.scene().camera.position.y,
                                   engine.scene().camera.position.z}},
                     {"target", {engine.scene().camera.target.x, engine.scene().camera.target.y,
                                 engine.scene().camera.target.z}},
                     {"fovDegrees", glm::degrees(engine.scene().camera.effectiveFovY())}};
            j["lens"] = json{{"focalLengthMm", engine.lens().focalLength},
                             {"sensorHeightMm", engine.lens().sensorHeight},
                             {"aperture", engine.lens().aperture},
                             {"focusDistance", engine.lens().focusDistance},
                             {"shutterAngle", engine.lens().shutterAngle},
                             {"useExplicitFov", engine.lens().useExplicitFov}};
            j["exposure"] = json{
                {"mode", engine.exposure().mode == scene::ExposureSettings::Mode::Automatic
                             ? "automatic"
                             : "manual"},
                {"aperture", engine.exposure().aperture},
                {"shutterSeconds", engine.exposure().shutterSeconds},
                {"iso", engine.exposure().iso},
                {"compensationEv", engine.exposure().compensation}};
            j["focusDistance"] = engine.trackedFocusDistance();
            return ToolResult::ok(std::move(j), "camera");
        });

    add(registry, "camera.set", "Move the camera",
        "Place the camera. Position and target are world-space metres; fovDegrees is the vertical "
        "field of view. The camera must be in free mode for an explicit pose to have any effect, "
        "so this switches it there and tells you it did. Pass mode explicitly to override.",
        schema::object({{"position", schema::array(schema::number("metres"), "Eye position", 3, 3)},
                        {"target", schema::array(schema::number("metres"), "Look-at point", 3, 3)},
                        {"fovDegrees", schema::number("Vertical field of view", 5.0, 120.0)},
                        {"mode", schema::string("Camera mode", {"orbit", "free", "spline"})}}),
        mutating(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            app::Engine& engine = ctx.engine();
            params::IParameter* mode = engine.params().find("camera/mode");
            params::IParameter* position = engine.params().find("camera/position");
            params::IParameter* target = engine.params().find("camera/target");
            params::IParameter* fov = engine.params().find("camera/fov");
            if (mode == nullptr && position == nullptr) {
                return ToolResult::failure(
                    ToolErrorCode::Unavailable,
                    "this scene exposes no camera parameters",
                    "call parameter.search with 'camera' to see what it does expose");
            }
            json out;
            const bool wantsPose = args.contains("position") || args.contains("target");
            if (args.contains("mode") && mode != nullptr) {
                const std::string name = args.at("mode").get<std::string>();
                const float value = name == "free" ? 1.0f : (name == "spline" ? 2.0f : 0.0f);
                mode->setBaseComponent(0, value);
                out["mode"] = name;
            } else if (wantsPose && mode != nullptr && mode->baseComponent(0) != kCameraModeFree) {
                // Guard (3). Writing the pose without this is the definition of a change that does
                // nothing and says nothing.
                out["modeChangedTo"] = "free";
                out["modeChangedReason"] =
                    "camera/position and camera/target are ignored in orbit and spline modes";
                mode->setBaseComponent(0, kCameraModeFree);
                ctx.changes().note("camera/mode", "orbit -> free");
            }
            const auto apply = [&](const char* key, params::IParameter* param) {
                if (param == nullptr || !args.contains(key)) {
                    return;
                }
                auto values = readValueArg(args.at(key), param->componentCount());
                if (!values) {
                    return;
                }
                const SetOutcome outcome = setParameter(engine, *param, *values, -1);
                out[key] = outcomeJson(*param, outcome);
                ctx.changes().note(param->path(), valueJson(outcome.after).dump(), outcome.clamped);
            };
            apply("position", position);
            apply("target", target);
            apply("fovDegrees", fov);
            if (out.empty()) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments,
                                           "nothing to set",
                                           "pass at least one of position, target, fovDegrees, mode");
            }
            return ToolResult::ok(std::move(out), "camera set");
        });

    add(registry, "camera.frame_node", "Frame a node",
        "Point the camera at a node and stand far enough back to fit it in shot. Computes the "
        "distance from the node's own world bounds and the current field of view, so it is a "
        "framing decision rather than a guessed coordinate. 'margin' widens the shot: 1.0 fits "
        "exactly, 1.5 leaves room around the subject.",
        schema::object({{"name", schema::string("Node name from scene.find_nodes")},
                        {"margin", schema::number("Headroom factor (default 1.4)", 1.0, 8.0)},
                        {"elevationDegrees",
                         schema::number("How far above the subject to place the camera", -80.0, 80.0)},
                        {"azimuthDegrees",
                         schema::number("Compass angle around the subject", -360.0, 360.0)}},
                       {"name"}),
        mutating(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            app::Engine& engine = ctx.engine();
            const std::string name = args.at("name").get<std::string>();
            const scene::CompositionNode* node = findNode(engine, name);
            if (node == nullptr) {
                return ToolResult::failure(ToolErrorCode::NotFound,
                                           fmt::format("no node named '{}'", name),
                                           "call scene.find_nodes to list the canonical names");
            }
            params::IParameter* position = engine.params().find("camera/position");
            params::IParameter* target = engine.params().find("camera/target");
            params::IParameter* mode = engine.params().find("camera/mode");
            if (position == nullptr || target == nullptr) {
                return ToolResult::failure(ToolErrorCode::Unavailable,
                                           "this scene exposes no camera position or target",
                                           "use camera.set with the parameters it does expose");
            }
            // The node's placement plus the scene's extent: a composition node's own bounds are not
            // separately tracked, so the honest available measure is the whole scene's radius
            // scaled by the node's scale. Reported as `basis` so the caller knows what it got.
            const glm::vec3 centre = node->positionParam != nullptr
                                         ? node->positionParam->base()
                                         : node->transform.position;
            const auto [lo, hi] = engine.scene().bounds();
            const float sceneRadius = 0.5f * glm::length(hi - lo);
            const glm::vec3 nodeScale = node->scaleParam != nullptr ? node->scaleParam->base()
                                                                    : node->transform.scale;
            const float radius =
                std::max(0.25f, sceneRadius * 0.25f * std::max({nodeScale.x, nodeScale.y, nodeScale.z}));

            const float margin = static_cast<float>(args.value("margin", 1.4));
            float fovDegrees = 45.0f;
            if (const params::IParameter* fov = engine.params().find("camera/fov")) {
                fovDegrees = fov->baseComponent(0);
            }
            const float halfFov = glm::radians(std::max(5.0f, fovDegrees)) * 0.5f;
            const float distance = (radius * margin) / std::max(0.05f, std::tan(halfFov));

            const float elev = glm::radians(static_cast<float>(args.value("elevationDegrees", 12.0)));
            const float azim = glm::radians(static_cast<float>(args.value("azimuthDegrees", 35.0)));
            const glm::vec3 offset{distance * std::cos(elev) * std::sin(azim),
                                   distance * std::sin(elev),
                                   distance * std::cos(elev) * std::cos(azim)};

            json out;
            if (mode != nullptr && mode->baseComponent(0) != kCameraModeFree) {
                mode->setBaseComponent(0, kCameraModeFree);
                out["modeChangedTo"] = "free";
                ctx.changes().note("camera/mode", "-> free");
            }
            const std::vector<float> eye{centre.x + offset.x, centre.y + offset.y, centre.z + offset.z};
            const std::vector<float> look{centre.x, centre.y, centre.z};
            const SetOutcome posOut = setParameter(engine, *position, eye, -1);
            const SetOutcome tgtOut = setParameter(engine, *target, look, -1);
            ctx.changes().note("camera/position", valueJson(posOut.after).dump(), posOut.clamped);
            ctx.changes().note("camera/target", valueJson(tgtOut.after).dump(), tgtOut.clamped);
            out["subject"] = name;
            out["position"] = outcomeJson(*position, posOut);
            out["target"] = outcomeJson(*target, tgtOut);
            out["distance"] = distance;
            out["basis"] = "subject position, scene radius scaled by the node's scale, current fov";
            return ToolResult::ok(std::move(out), "framed " + name);
        });
}

// ================================================================================================
// environment.*  /  lighting.*  /  material.*
// ================================================================================================

// The environment's parameter vocabulary, as registered by scene::Composition. Named here rather
// than guessed at call time: a tool that asks for "env/sky/sunIntensity" and gets nothing should
// say the scene has no sky, not silently do nothing.
constexpr const char* kEnvironmentPaths[] = {
    "env/intensity",        "env/rotation",         "env/sky/enabled",
    "env/sky/background",   "env/sky/zenithColor",  "env/sky/horizonColor",
    "env/sky/groundColor",  "env/sky/sunColor",     "env/sky/haze",
    "env/sky/sunIntensity", "env/sky/sunSize",      "env/sky/sunGlow",
    "env/sky/intensity",    "scene/brightness",     "scene/fogDensity",
    "scene/fogHeight",      "scene/fogHeightFalloff", "scene/volumeDensity",
    "scene/volumeScattering", "scene/keyLight",     "scene/windSpeed",
    "scene/windDirection",  "scene/stylized",
};

void registerEnvironmentTools(ToolRegistry& registry) {
    add(registry, "environment.get", "Environment state",
        "Sky, sun, fog, volumetrics, wind and ambient brightness, as the parameters that drive "
        "them. Anything absent is absent from this scene, not zero.",
        noArgs(), readOnly(),
        [](const json&, ToolContext& ctx) -> ToolResult {
            json present = json::object();
            json absent = json::array();
            for (const char* path : kEnvironmentPaths) {
                if (const params::IParameter* p = ctx.engine().params().find(path)) {
                    present[path] = paramJson(*p, true);
                } else {
                    absent.push_back(path);
                }
            }
            json out;
            out["parameters"] = std::move(present);
            if (!absent.empty()) {
                out["notInThisScene"] = std::move(absent);
            }
            out["environmentMap"] = ctx.engine().environmentPath().empty()
                                        ? json(nullptr)
                                        : json(ctx.engine().environmentPath().filename().generic_string());
            out["exposureCompensationEv"] = ctx.engine().exposure().compensation;
            return ToolResult::ok(std::move(out), "environment");
        });

    add(registry, "environment.set", "Set environment values",
        "Set several environment parameters at once, which is how an atmosphere is actually "
        "changed -- fog, sun and sky colour move together or the result looks wrong. Each entry is "
        "a parameter path from environment.get and its new value. Unknown paths are reported "
        "rather than ignored.",
        schema::object(
            {{"values", json{{"type", "object"}, {"description", "path -> value"}}}},
            {"values"}),
        mutating(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            const json& values = args.at("values");
            if (!values.is_object() || values.empty()) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments,
                                           "'values' must be a non-empty object of path -> value",
                                           "e.g. {\"scene/fogDensity\": 0.08}");
            }
            json applied = json::object();
            json unknown = json::array();
            for (const auto& [path, value] : values.items()) {
                params::IParameter* p = ctx.engine().params().find(path);
                if (p == nullptr) {
                    unknown.push_back(path);
                    continue;
                }
                auto parsed = readValueArg(value, p->componentCount());
                if (!parsed) {
                    return ToolResult::failure(
                        ToolErrorCode::InvalidArguments,
                        fmt::format("{}: {}", path, parsed.error().message),
                        fmt::format("'{}' is a {} with {} component(s)", path,
                                    paramKindName(p->kind()), p->componentCount()));
                }
                const SetOutcome outcome = setParameter(ctx.engine(), *p, *parsed, -1);
                applied[path] = outcomeJson(*p, outcome);
                ctx.changes().note(path, valueJson(outcome.after).dump(), outcome.clamped);
            }
            json out;
            out["applied"] = std::move(applied);
            if (!unknown.empty()) {
                out["notFound"] = unknown;
            }
            if (out["applied"].empty()) {
                return ToolResult::failure(
                    ToolErrorCode::NotFound, "none of the given paths exist in this scene",
                    "call environment.get for the paths this scene actually has");
            }
            return ToolResult::ok(std::move(out),
                                  fmt::format("{} environment value(s) set", out["applied"].size()));
        });

    add(registry, "lighting.list", "List lights",
        "Every light in the flattened scene, with the parameter paths that control it where those "
        "exist. Read the note in the result: a scene's lights are rebuilt from its description, so "
        "lights that no parameter drives cannot be edited through this API in this build.",
        noArgs(), readOnly(),
        [](const json&, ToolContext& ctx) -> ToolResult {
            app::Engine& engine = ctx.engine();
            json lights = json::array();
            for (const scene::PunctualLight& light : engine.scene().lights) {
                json j;
                j["name"] = light.name;
                j["type"] = static_cast<int>(light.type);
                j["intensity"] = light.intensity;
                j["color"] = {light.color.r, light.color.g, light.color.b};
                j["temperatureK"] = light.temperature;
                j["position"] = {light.position.x, light.position.y, light.position.z};
                j["direction"] = {light.direction.x, light.direction.y, light.direction.z};
                j["range"] = light.range;
                j["castsShadow"] = light.castsShadow;
                // The controlling parameters, found by the naming convention the light rig uses.
                json controls = json::array();
                for (const params::IParameter* p : engine.params().ordered()) {
                    if (p->group() == "lightrig" && containsNoCase(p->path(), light.name)) {
                        controls.push_back(p->path());
                    }
                }
                if (!controls.empty()) {
                    j["parameters"] = std::move(controls);
                }
                lights.push_back(std::move(j));
            }
            json rigParams = json::array();
            for (const params::IParameter* p : engine.params().ordered()) {
                if (p->group() == "lightrig") {
                    rigParams.push_back(p->path());
                }
            }
            json out;
            out["lights"] = std::move(lights);
            if (!rigParams.empty()) {
                out["lightRigParameters"] = std::move(rigParams);
            }
            if (const params::IParameter* key = engine.params().find("scene/keyLight")) {
                out["keyLightScale"] = key->baseComponent(0);
            }
            out["note"] =
                "Lights are regenerated whenever the scene rebuilds. Change them through the "
                "lightrig/* parameters, scene/keyLight or env/sky/sunIntensity listed here; a "
                "direct write to a light would be discarded at the next rebuild, so this build "
                "exposes no such tool.";
            return ToolResult::ok(std::move(out),
                                  fmt::format("{} light(s)", engine.scene().lights.size()));
        });

    add(registry, "material.list", "List material controls",
        "The material parameters this scene exposes: per-node emissive boost and roughness scale, "
        "and for procedural nodes the per-part tint, emissive colour, emissive gain, roughness and "
        "opacity. Emissive colour is HDR -- values well above 1 are correct and are what makes "
        "something glow.",
        schema::object({{"node", schema::string("Restrict to one node")},
                        {"limit", schema::integer("Maximum results", 1, kMaxLimit)}}),
        readOnly(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            const std::string node = args.value("node", std::string{});
            const std::size_t limit = limitOf(args);
            json list = json::array();
            std::size_t matched = 0;
            for (const params::IParameter* p : ctx.engine().params().ordered()) {
                const std::string& path = p->path();
                const bool material =
                    containsNoCase(path, "emissive") || containsNoCase(path, "roughness") ||
                    containsNoCase(path, "tint") || containsNoCase(path, "opacity") ||
                    containsNoCase(path, "metallic") || containsNoCase(path, "baseColor");
                if (!material) {
                    continue;
                }
                if (!node.empty() && !containsNoCase(path, node)) {
                    continue;
                }
                ++matched;
                if (list.size() < limit) {
                    list.push_back(paramJson(*p, true));
                }
            }
            json out;
            out["materials"] = std::move(list);
            out["matched"] = matched;
            out["note"] = "Emissive values are HDR and are not clamped to 1.";
            return ToolResult::ok(std::move(out), fmt::format("{} material control(s)", matched));
        });
}

// ================================================================================================
// sequencer.*
// ================================================================================================

void registerSequencerTools(ToolRegistry& registry) {
    add(registry, "sequencer.get_state", "Timeline state",
        "The timeline: its tracks and what each one drives, its cues, the playhead in seconds and "
        "beats, the tempo, and -- important -- any track whose target parameter does not exist. An "
        "unbound track is animation that silently does nothing, so it is always reported.",
        noArgs(), readOnly(),
        [](const json&, ToolContext& ctx) -> ToolResult {
            app::Engine& engine = ctx.engine();
            json tracks = json::array();
            for (std::size_t i = 0; i < engine.timeline().tracks().size(); ++i) {
                const params::Track& t = engine.timeline().tracks()[i];
                json j;
                j["index"] = i;
                j["target"] = t.target;
                j["component"] = t.component;
                j["timeBase"] = params::timeBaseName(t.timeBase);
                j["mode"] = params::trackModeName(t.mode);
                j["enabled"] = t.enabled;
                j["keys"] = t.keys.size();
                j["bound"] = t.param != nullptr;
                if (!t.keys.empty()) {
                    j["firstKeyTime"] = t.firstKeyTime();
                    j["lastKeyTime"] = t.lastKeyTime();
                }
                if (t.loopLength > 0.0) {
                    j["loopLength"] = t.loopLength;
                }
                tracks.push_back(std::move(j));
            }
            json cues = json::array();
            for (const params::Cue& c : engine.timeline().cues()) {
                cues.push_back(json{{"time", c.time},
                                    {"name", c.name},
                                    {"preset", c.preset},
                                    {"morphSeconds", c.morphSeconds},
                                    {"timeBase", params::timeBaseName(c.timeBase)}});
            }
            json out;
            out["enabled"] = engine.timeline().enabled;
            out["tracks"] = std::move(tracks);
            out["cues"] = std::move(cues);
            out["durationSeconds"] = engine.timeline().durationSeconds();
            out["playhead"] = json{{"seconds", engine.timelineClock().seconds},
                                   {"beats", engine.timelineClock().beats}};
            out["playing"] = engine.isPlaying();
            out["tempoBpm"] = engine.latestFrame().tempoBpm;
            out["phraseBars"] = engine.phraseBars();
            out["sectionPhrases"] = engine.sectionPhrases();
            if (!engine.timeline().unboundTargets().empty()) {
                out["unboundTargets"] = engine.timeline().unboundTargets();
                out["unboundWarning"] =
                    "these tracks name parameters this scene does not have; they animate nothing";
            }
            if (!engine.sequenceTargets().empty()) {
                out["ownedBySequence"] = engine.sequenceTargets();
                out["sequenceNote"] =
                    "tracks on these targets belong to the installed cinematic sequence and are "
                    "replaced whenever it is re-installed";
            }
            return ToolResult::ok(std::move(out),
                                  fmt::format("{} track(s), {} cue(s)",
                                              engine.timeline().tracks().size(),
                                              engine.timeline().cues().size()));
        });

    add(registry, "sequencer.add_keyframe", "Add a keyframe",
        "Key a parameter at a time, creating the track if it does not exist. Times are seconds by "
        "default, or beats -- use beats when the move has to land on the music, and read the tempo "
        "from sequencer.get_state rather than converting by hand. The target parameter must "
        "already exist: a track naming an unknown parameter animates nothing, so this refuses it.",
        schema::object(
            {{"target", schema::string("Parameter path to animate")},
             {"time", schema::number("Key time, in the chosen time base")},
             {"value", json{{"description", "Number or array; omit to key the current value"}}},
             {"component", schema::integer("Key only this component (0-based)", 0, 3)},
             {"interp", schema::string("Interpolation leaving this key",
                                       {"step", "linear", "smooth", "easein", "easeout", "easeinout",
                                        "bezier"})},
             {"timeBase", schema::string("Clock for 'time'", {"seconds", "beats"})},
             {"mode", schema::string("How the track meets the base value",
                                     {"replace", "add", "multiply"})}},
            {"target", "time"}),
        mutating(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            app::Engine& engine = ctx.engine();
            const std::string target = args.at("target").get<std::string>();
            params::IParameter* param = engine.params().find(target);
            if (param == nullptr) {
                // Guard (1). params::Timeline::bind skips unknown targets and says nothing; that
                // has cost this project two features already. It is not going to cost a third.
                return ToolResult::failure(
                    ToolErrorCode::NotFound,
                    fmt::format("no parameter '{}' -- a track on it would animate nothing", target),
                    "call parameter.search to find the canonical path first");
            }
            const int component = args.contains("component")
                                      ? static_cast<int>(args.at("component").get<double>())
                                      : -1;
            if (component >= static_cast<int>(param->componentCount())) {
                return ToolResult::failure(
                    ToolErrorCode::OutOfRange,
                    fmt::format("'{}' has {} component(s)", target, param->componentCount()));
            }
            const auto interp =
                params::keyInterpFromName(args.value("interp", std::string("smooth")))
                    .value_or(params::KeyInterp::Smooth);
            const auto timeBase =
                params::timeBaseFromName(args.value("timeBase", std::string("seconds")))
                    .value_or(params::TimeBase::Seconds);
            const double time = args.at("time").get<double>();

            // The engine's own recorder: it creates the track, keys the parameter's *current base*
            // and keeps the key list sorted. Then the value is overwritten if one was given, so a
            // caller that omits `value` gets "key what is there now", which is what an author
            // actually does.
            params::Track* track = engine.timeline().recordKey(engine.params(), target, component,
                                                               time, interp, timeBase);
            if (track == nullptr) {
                return ToolResult::failure(ToolErrorCode::Internal,
                                           fmt::format("could not create a track for '{}'", target));
            }
            if (args.contains("mode")) {
                if (const auto mode = params::trackModeFromName(args.at("mode").get<std::string>())) {
                    track->mode = *mode;
                }
            }
            std::string valueNote = "current value";
            if (args.contains("value")) {
                const std::size_t count = component >= 0 ? 1 : param->componentCount();
                auto values = readValueArg(args.at("value"), count);
                if (!values) {
                    return ToolResult::failure(ToolErrorCode::InvalidArguments,
                                               values.error().message);
                }
                for (params::Key& key : track->keys) {
                    if (std::abs(key.time - time) > 1e-6) {
                        continue;
                    }
                    for (std::size_t i = 0; i < count && i < key.value.size(); ++i) {
                        key.value[i] = (*values)[i];
                    }
                }
                valueNote = json(*values).dump();
            }
            // Bind immediately, and report whether it took. A track added without a bind holds a
            // null parameter pointer until something else happens to rebind.
            if (auto r = engine.timeline().bind(engine.params()); !r) {
                log::warn("ai: timeline bind after add_keyframe: {}", r.error().message);
            }
            const params::Track* bound = engine.timeline().findTrack(target, component);
            json out;
            out["target"] = target;
            out["time"] = time;
            out["timeBase"] = params::timeBaseName(timeBase);
            out["interp"] = params::keyInterpName(interp);
            out["mode"] = params::trackModeName(track->mode);
            out["keys"] = track->keys.size();
            out["bound"] = bound != nullptr && bound->param != nullptr;
            out["value"] = valueNote;
            if (!out["bound"].get<bool>()) {
                return ToolResult::failure(
                    ToolErrorCode::Conflict,
                    fmt::format("the track on '{}' did not bind and would animate nothing", target),
                    "check the parameter still exists");
            }
            ctx.changes().note(target, fmt::format("keyframe at {} {}", time,
                                                   params::timeBaseName(timeBase)));
            return ToolResult::ok(std::move(out),
                                  fmt::format("keyed {} at {}", target, time));
        });

    add(registry, "sequencer.remove_track", "Remove a track",
        "Delete a timeline track and every key on it.",
        schema::object({{"target", schema::string("Parameter path the track drives")},
                        {"component", schema::integer("Which component's track", -1, 3)}},
                       {"target"}),
        [] {
            ToolAnnotations a = mutating();
            a.destructive = true;
            return a;
        }(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            app::Engine& engine = ctx.engine();
            const std::string target = args.at("target").get<std::string>();
            const int component = args.contains("component")
                                      ? static_cast<int>(args.at("component").get<double>())
                                      : -1;
            auto& tracks = engine.timeline().tracks();
            const auto it = std::find_if(tracks.begin(), tracks.end(), [&](const params::Track& t) {
                return t.target == target && t.component == component;
            });
            if (it == tracks.end()) {
                return ToolResult::failure(
                    ToolErrorCode::NotFound,
                    fmt::format("no track on '{}' (component {})", target, component),
                    "call sequencer.get_state to list the tracks");
            }
            const std::size_t keys = it->keys.size();
            engine.timeline().removeTrack(static_cast<std::size_t>(it - tracks.begin()));
            if (auto r = engine.timeline().bind(engine.params()); !r) {
                log::warn("ai: timeline bind after remove_track: {}", r.error().message);
            }
            ctx.changes().note(target, fmt::format("track removed ({} key(s))", keys));
            json out;
            out["target"] = target;
            out["keysRemoved"] = keys;
            out["tracksRemaining"] = tracks.size();
            return ToolResult::ok(std::move(out), "removed track on " + target);
        });

    add(registry, "sequencer.set_playhead", "Move the playhead",
        "Seek the transport to a time in seconds. Use this to look at a moment in the piece before "
        "or after changing it.",
        schema::object({{"seconds", schema::number("Position, in seconds", 0.0)}}, {"seconds"}),
        [] {
            ToolAnnotations a;
            a.requiresMainThread = true;
            a.idempotent = true;
            // Session state, not project state: seeking changes what you are looking at, not what
            // would be saved, so it is outside the transaction and says so.
            a.mutatesSession = true;
            return a;
        }(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            const double seconds = args.at("seconds").get<double>();
            ctx.engine().seekSeconds(seconds);
            json out;
            out["seconds"] = ctx.engine().positionSeconds();
            out["beats"] = ctx.engine().timelineClock().beats;
            return ToolResult::ok(std::move(out), fmt::format("playhead at {:.2f}s", seconds));
        });
}

// ================================================================================================
// audio.*  /  signal.*  /  modulation.*
// ================================================================================================

void registerAudioTools(ToolRegistry& registry) {
    add(registry, "audio.get_analysis", "Audio analysis",
        "What the analyser currently hears: loudness, the five frequency bands, spectral centroid, "
        "onset, tempo and beat position, plus when each musical event (beat, downbeat, build, "
        "break, drop, impact) last fired. This is the state audio-reactive work is built on.",
        noArgs(), readOnly(),
        [](const json&, ToolContext& ctx) -> ToolResult {
            app::Engine& engine = ctx.engine();
            json out;
            out["hasAudio"] = engine.hasAudio();
            out["hasFrame"] = engine.hasFrame();
            if (!engine.hasFrame()) {
                out["note"] = "no analysis frame yet: load audio and start the transport";
                return ToolResult::ok(std::move(out), "no analysis yet");
            }
            const analysis::AnalysisFrame& f = engine.latestFrame();
            out["timeSeconds"] = f.timeSeconds;
            out["rms"] = f.rms;
            out["peak"] = f.peak;
            json bands = json::array();
            for (std::uint32_t i = 0; i < f.bandCount && i < analysis::kMaxBands; ++i) {
                bands.push_back(f.bands[i]);
            }
            out["bands"] = std::move(bands);
            out["bandNames"] = json::array({"bass", "lowMid", "mid", "highMid", "treble"});
            out["spectralCentroid"] = f.centroidNorm;
            out["spectralFlux"] = f.flux;
            out["onsetStrength"] = f.onsetStrength;
            out["tempoBpm"] = f.tempoBpm;
            out["tempoConfidence"] = f.tempoConfidence;
            out["beatCount"] = f.beatCount;
            out["beatPhase"] = f.beatPhase;
            json events = json::object();
            for (std::size_t i = 0; i < engine.music().eventCount(); ++i) {
                const auto event = static_cast<signals::MusicalEvent>(i);
                const double when = engine.music().lastEventTime(event);
                events[signals::musicalEventName(event)] =
                    when <= app::MusicRuntime::kNever ? json(nullptr) : json(when);
            }
            out["lastEventSeconds"] = std::move(events);
            return ToolResult::ok(std::move(out), "analysis");
        });

    add(registry, "signal.list", "List signals",
        "Every named control signal on the bus and its current value: audio bands, tempo, beat "
        "phase, musical events, time, MIDI/OSC control channels and any LFO or macro sources. "
        "These are the sources a modulation route can be built from -- use the exact names here.",
        schema::object({{"query", schema::string("Case-insensitive name fragment, e.g. 'bass'")},
                        {"eventsOnly", schema::boolean("Only momentary event signals")},
                        {"limit", schema::integer("Maximum results", 1, kMaxLimit)}}),
        readOnly(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            const std::string query = args.value("query", std::string{});
            const bool eventsOnly = args.value("eventsOnly", false);
            const std::size_t limit = limitOf(args);
            const signals::SignalBus& bus = ctx.engine().signals();
            json list = json::array();
            std::size_t matched = 0;
            for (signals::SignalId id = 0; id < bus.size(); ++id) {
                const signals::SignalInfo& info = bus.info(id);
                if (eventsOnly && !info.isEvent) {
                    continue;
                }
                if (!containsNoCase(info.name, query)) {
                    continue;
                }
                ++matched;
                if (list.size() < limit) {
                    list.push_back(json{{"name", info.name},
                                        {"value", bus.value(id)},
                                        {"min", info.minValue},
                                        {"max", info.maxValue},
                                        {"isEvent", info.isEvent}});
                }
            }
            json out;
            out["signals"] = std::move(list);
            out["matched"] = matched;
            return ToolResult::ok(std::move(out), fmt::format("{} signal(s)", matched));
        });
}

void registerModulationTools(ToolRegistry& registry) {
    const auto routeJson = [](const params::ModRoute& route, std::size_t index) {
        json j;
        j["index"] = index;
        j["source"] = route.source;
        j["target"] = route.target;
        j["component"] = route.component;
        j["amount"] = route.amount;
        j["op"] = std::string(params::modOpName(route.op));
        j["polarity"] = route.polarity == params::Polarity::Bipolar ? "bipolar" : "unipolar";
        j["enabled"] = route.enabled;
        j["bound"] = route.targetParam != nullptr && route.sourceId != signals::kInvalidSignal;
        j["lastOutput"] = route.lastOutput;
        if (route.fromGraph) {
            j["owner"] = "procedural graph";
        } else if (route.fromEntity) {
            j["owner"] = "entity reaction";
        }
        if (route.chain.attackMs > 0.0f || route.chain.decayMs > 0.0f) {
            j["smoothing"] = json{{"attackMs", route.chain.attackMs},
                                  {"decayMs", route.chain.decayMs}};
        }
        return j;
    };

    add(registry, "modulation.list", "List modulation routes",
        "Every modulation route: which signal drives which parameter, how hard, with what "
        "operation, and whether it is actually bound. An unbound route is a connection that does "
        "nothing, so check that field.",
        schema::object({{"target", schema::string("Only routes aimed at this parameter path")},
                        {"source", schema::string("Only routes from this signal")}}),
        readOnly(),
        [routeJson](const json& args, ToolContext& ctx) -> ToolResult {
            const std::string target = args.value("target", std::string{});
            const std::string source = args.value("source", std::string{});
            json list = json::array();
            const auto& routes = ctx.engine().modulator().routes();
            for (std::size_t i = 0; i < routes.size(); ++i) {
                if (!target.empty() && routes[i].target != target) {
                    continue;
                }
                if (!source.empty() && routes[i].source != source) {
                    continue;
                }
                list.push_back(routeJson(routes[i], i));
            }
            json out;
            out["routes"] = std::move(list);
            out["total"] = routes.size();
            out["masterGain"] = ctx.engine().modulator().masterGain;
            return ToolResult::ok(std::move(out), fmt::format("{} route(s)", out["routes"].size()));
        });

    add(registry, "modulation.create", "Create a modulation route",
        "Connect a signal to a parameter so it moves with the music. 'amount' is the depth and may "
        "be negative to invert. 'op' says how it meets the parameter's value: add is the usual "
        "choice, multiply scales it, replace overwrites it. Use attackMs/decayMs to smooth a jumpy "
        "source, and an envelope to turn a momentary event such as music.drop into something that "
        "decays rather than flashing for one frame. The route is bound immediately and the result "
        "says whether it resolved.",
        schema::object(
            {{"source", schema::string("Signal name from signal.list, e.g. 'audio.bass'")},
             {"target", schema::string("Parameter path from parameter.search")},
             {"amount", schema::number("Depth; negative inverts", -8.0, 8.0)},
             {"op", schema::string("How it combines", {"add", "multiply", "replace", "min", "max"})},
             {"polarity", schema::string("Bipolar maps 0..1 to -1..1 first",
                                         {"unipolar", "bipolar"})},
             {"component", schema::integer("Drive only this component (0-based)", 0, 3)},
             {"attackMs", schema::number("Rise smoothing, milliseconds", 0.0, 60000.0)},
             {"decayMs", schema::number("Fall smoothing, milliseconds", 0.0, 60000.0)},
             {"envelope", schema::string("Turn an event into a falling envelope",
                                         {"none", "peakhold", "linearfall"})},
             {"envelopeFallPerSecond", schema::number("How fast the envelope falls", 0.0, 100.0)}},
            {"source", "target"}),
        mutating(),
        [routeJson](const json& args, ToolContext& ctx) -> ToolResult {
            app::Engine& engine = ctx.engine();
            const std::string source = args.at("source").get<std::string>();
            const std::string target = args.at("target").get<std::string>();
            if (!engine.signals().find(source)) {
                return ToolResult::failure(
                    ToolErrorCode::NotFound, fmt::format("no signal named '{}'", source),
                    "call signal.list to see the exact signal names on the bus");
            }
            params::IParameter* param = engine.params().find(target);
            if (param == nullptr) {
                return notFoundParameter(target);
            }
            if (!param->flags().modulatable) {
                return ToolResult::failure(
                    ToolErrorCode::Unsupported,
                    fmt::format("'{}' is not a modulation target", target),
                    "call parameter.search with modulatableOnly to find one that is");
            }
            params::ModRoute route;
            route.source = source;
            route.target = target;
            route.amount = static_cast<float>(args.value("amount", 1.0));
            route.op = params::modOpFromName(args.value("op", std::string("add")))
                           .value_or(params::ModOp::Add);
            route.polarity = args.value("polarity", std::string("unipolar")) == "bipolar"
                                 ? params::Polarity::Bipolar
                                 : params::Polarity::Unipolar;
            route.component = args.contains("component")
                                  ? static_cast<int>(args.at("component").get<double>())
                                  : -1;
            route.chain.attackMs = static_cast<float>(args.value("attackMs", 0.0));
            route.chain.decayMs = static_cast<float>(args.value("decayMs", 0.0));
            const std::string envelope = args.value("envelope", std::string("none"));
            if (envelope == "peakhold") {
                route.chain.envelope = params::EnvelopeMode::PeakHold;
            } else if (envelope == "linearfall") {
                route.chain.envelope = params::EnvelopeMode::LinearFall;
            }
            if (args.contains("envelopeFallPerSecond")) {
                route.chain.envelopeFallPerSecond =
                    static_cast<float>(args.at("envelopeFallPerSecond").get<double>());
            }
            engine.modulator().addRoute(std::move(route));
            engine.rebind();

            const auto& routes = engine.modulator().routes();
            const std::size_t index = routes.size() - 1;
            const params::ModRoute& created = routes[index];
            json out = routeJson(created, index);
            if (!out["bound"].get<bool>()) {
                return ToolResult::failure(
                    ToolErrorCode::Conflict,
                    fmt::format("the route '{}' -> '{}' did not bind and would do nothing", source,
                                target),
                    "check both names with signal.list and parameter.search");
            }
            ctx.changes().note(target, fmt::format("modulated by {} ({} {:.2f})", source,
                                                   params::modOpName(created.op), created.amount));
            return ToolResult::ok(std::move(out), fmt::format("{} -> {}", source, target));
        });

    add(registry, "modulation.set_depth", "Change a route's depth",
        "Change how hard an existing modulation route drives its target.",
        schema::object({{"index", schema::integer("Route index from modulation.list", 0)},
                        {"amount", schema::number("New depth", -8.0, 8.0)},
                        {"enabled", schema::boolean("Turn the route on or off")}},
                       {"index"}),
        mutating(),
        [routeJson](const json& args, ToolContext& ctx) -> ToolResult {
            auto& routes = ctx.engine().modulator().routes();
            const auto index = static_cast<std::size_t>(args.at("index").get<double>());
            if (index >= routes.size()) {
                return ToolResult::failure(
                    ToolErrorCode::NotFound,
                    fmt::format("route {} does not exist ({} route(s))", index, routes.size()),
                    "call modulation.list for current indices");
            }
            params::ModRoute& route = routes[index];
            if (args.contains("amount")) {
                route.amount = static_cast<float>(args.at("amount").get<double>());
            }
            if (args.contains("enabled")) {
                route.enabled = args.at("enabled").get<bool>();
            }
            ctx.changes().note(route.target,
                               fmt::format("route depth {:.2f}, {}", route.amount,
                                           route.enabled ? "enabled" : "disabled"));
            return ToolResult::ok(routeJson(route, index), "route updated");
        });

    add(registry, "modulation.remove", "Remove a modulation route",
        "Delete a modulation route by index. Indices shift afterwards, so re-read modulation.list "
        "before removing another.",
        schema::object({{"index", schema::integer("Route index from modulation.list", 0)}},
                       {"index"}),
        [] {
            ToolAnnotations a = mutating();
            a.destructive = true;
            a.idempotent = false;
            return a;
        }(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            auto& routes = ctx.engine().modulator().routes();
            const auto index = static_cast<std::size_t>(args.at("index").get<double>());
            if (index >= routes.size()) {
                return ToolResult::failure(
                    ToolErrorCode::NotFound,
                    fmt::format("route {} does not exist ({} route(s))", index, routes.size()),
                    "call modulation.list for current indices");
            }
            const std::string source = routes[index].source;
            const std::string target = routes[index].target;
            if (routes[index].fromGraph || routes[index].fromEntity) {
                return ToolResult::failure(
                    ToolErrorCode::Conflict,
                    fmt::format("route {} is owned by a {} and would be recreated", index,
                                routes[index].fromGraph ? "procedural graph" : "entity reaction"),
                    "change the graph or the entity's reactions instead");
            }
            routes.erase(routes.begin() + static_cast<std::ptrdiff_t>(index));
            ctx.engine().rebind();
            ctx.changes().note(target, fmt::format("route from {} removed", source));
            json out;
            out["removed"] = json{{"source", source}, {"target", target}};
            out["remaining"] = routes.size();
            return ToolResult::ok(std::move(out), fmt::format("removed {} -> {}", source, target));
        });
}

// ================================================================================================
// performance.*
// ================================================================================================

void registerPerformanceTools(ToolRegistry& registry) {
    add(registry, "performance.get_stats", "Performance",
        "Frame cost and what it is spent on: frame time, GPU time where the device can measure it, "
        "draw calls, triangles, visible and culled instances, light count and per-pass GPU time. "
        "Read this before and after any change made under a frame-rate constraint. If no renderer "
        "is attached to this session the result says so rather than reporting zeros.",
        noArgs(), readOnly(),
        [](const json&, ToolContext& ctx) -> ToolResult {
            const PerformanceSnapshot snapshot = ctx.performance();
            if (!snapshot.available) {
                return ToolResult::failure(
                    ToolErrorCode::Unavailable,
                    fmt::format("no performance data: {}", snapshot.unavailableReason),
                    "this is a session without a renderer; frame cost cannot be measured here");
            }
            json j;
            j["fps"] = snapshot.fps;
            j["cpuFrameMs"] = snapshot.cpuFrameMs;
            j["frameIntervalMs"] = snapshot.frameIntervalMs;
            if (snapshot.gpuFrameMs >= 0.0) {
                j["gpuFrameMs"] = snapshot.gpuFrameMs;
            } else {
                j["gpuFrameMs"] = nullptr;
                j["gpuTimingNote"] = "this device reports no timestamp queries";
            }
            j["drawCalls"] = snapshot.drawCalls;
            j["triangles"] = snapshot.triangles;
            j["visibleInstances"] = snapshot.visibleInstances;
            j["culledInstances"] = snapshot.culledInstances;
            j["lights"] = snapshot.lights;
            j["entities"] = snapshot.entities;
            j["shadowDraws"] = snapshot.shadowDraws;
            j["particleSystems"] = snapshot.particleSystems;
            j["proceduralObjects"] = snapshot.proceduralObjects;
            j["resolution"] = json{{"width", snapshot.width}, {"height", snapshot.height}};
            if (!snapshot.gpuPasses.empty()) {
                json passes = json::array();
                for (const PassTime& pass : snapshot.gpuPasses) {
                    passes.push_back(json{{"label", pass.label}, {"ms", pass.milliseconds}});
                }
                j["gpuPasses"] = std::move(passes);
            }
            return ToolResult::ok(std::move(j),
                                  fmt::format("{:.1f} fps, {} draws", snapshot.fps,
                                              snapshot.drawCalls));
        });
}

} // namespace

// ================================================================================================

void registerEngineTools(ToolRegistry& registry) {
    // The snapshot store lives for the process: `project.create_snapshot` and
    // `project.restore_snapshot` have to agree on the same ids across calls, and a store owned by
    // the registry would be destroyed with it. One per process is right -- snapshots are session
    // state, and there is one engine per process.
    static SnapshotStore snapshots;

    registerCapabilityTools(registry);
    registerProjectTools(registry, snapshots);
    registerParameterTools(registry);
    registerSceneTools(registry);
    registerCameraTools(registry);
    registerEnvironmentTools(registry);
    registerSequencerTools(registry);
    registerAudioTools(registry);
    registerModulationTools(registry);
    registerPerformanceTools(registry);
}

} // namespace avgen::ai
