#include "ai/engine_tools.hpp"
#include "assets/asset_catalog.hpp"

#include "ai/capabilities.hpp"
#include "ai/tool_context.hpp"
#include "ai/transaction.hpp"
#include "app/engine.hpp"
#include "app/world_builder.hpp"
#include "analysis/analysis_track.hpp"
#include "core/hash.hpp"
#include "core/log.hpp"
#include "params/serialization.hpp"
#include "scene/composition.hpp"
#include "entity/entity.hpp"
#include "entity/field.hpp"
#include "seq/sequence.hpp"
#include "seq/layers.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
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

std::string importTypeFor(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".glb" || ext == ".gltf") return "models";
    if (ext == ".hdr" || ext == ".exr") return "environments";
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".webp") return "textures";
    return {};
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


// Writing a project to disk, or opening one. Deliberately *not* `mutatesProject`: the snapshot
// domain is parameter state, and a snapshot cannot un-write a file or put back the session that
// opening another project replaced. Claiming otherwise would promise a rollback nothing keeps, so
// these say `mutatesSession` and `undoable = false`, which is the truth.
ToolAnnotations sessionWrite(bool destructive = false) {
    ToolAnnotations a;
    a.mutatesSession = true;
    a.destructive = destructive;
    a.idempotent = !destructive;
    a.undoable = false;
    a.requiresMainThread = true;
    return a;
}

// Replaces a large part of the project in one go -- an ecology, a scene -- but is still inside the
// snapshot domain, so it is undoable. `destructive` says the previous contents are gone if it is
// *not* rolled back, which is a different claim from "cannot be undone".
ToolAnnotations mutatingDestructive() {
    ToolAnnotations a = mutating();
    a.destructive = true;
    a.idempotent = false;
    return a;
}

// A project name, used as a directory name. Refused rather than sanitised when it would escape the
// projects root: an agent that asked for "../../etc" has made a mistake worth reporting, and
// quietly rewriting it to something safe teaches it nothing and hides the bug.
[[nodiscard]] bool safeProjectName(std::string_view name) {
    if (name.empty() || name.size() > 120 || name.front() == '.') {
        return false;
    }
    if (name.find("..") != std::string_view::npos) {
        return false;
    }
    return name.find('/') == std::string_view::npos &&
           name.find('\\') == std::string_view::npos &&
           name.find('\0') == std::string_view::npos;
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
            // The summary is built *before* the document is moved. Reading `doc` inside the same
            // call that moves it is unsequenced, and it reported zeros for a document that was
            // perfectly correct -- the exact "the report disagrees with reality" failure this API
            // is written against, committed in the API itself.
            const auto summary = fmt::format("{} domain(s), {} unavailable",
                                             doc.at("domains").size(), unavailable);
            return ToolResult::ok(std::move(doc), summary);
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
            const auto summary = fmt::format("{} tool(s)", tools.size());
            json out;
            out["count"] = tools.size();
            out["tools"] = std::move(tools);
            return ToolResult::ok(std::move(out), summary);
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
    // ---- the project's own life cycle ---------------------------------------------------------
    //
    // These exist because everything else in this file edits a project somebody else created. An
    // assistant asked to *start* a piece had no verb for it: `project.get_state` could describe the
    // session and the snapshot tools could roll one back, and nothing could make one or keep it.
    //
    // Every one takes a **name**, never a path. A tool that took a path would let one bad argument
    // write anywhere on the machine; a name resolves under `ToolContext::projectsRoot()`, so the
    // worst a wrong answer does is make a directory in a folder the user already owns.
    add(registry, "project.list", "List projects",
        "Every project in the projects folder, by name. Use this before opening one rather than "
        "guessing a name.",
        noArgs(), readOnly(),
        [](const json&, ToolContext& ctx) -> ToolResult {
            if (!ctx.hasProjectsRoot()) {
                return ToolResult::failure(ToolErrorCode::Unavailable,
                                           "no projects folder is configured in this session",
                                           "projects cannot be listed, created or opened by name "
                                           "until the host sets one");
            }
            json names = json::array();
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator(ctx.projectsRoot(), ec)) {
                if (!entry.is_directory(ec)) {
                    continue;
                }
                const std::string name = entry.path().filename().string();
                if (!std::filesystem::exists(entry.path() / (name + ".json"), ec)) {
                    continue;
                }
                names.push_back(name);
            }
            const auto count = names.size();
            return ToolResult::ok(json{{"root", ctx.projectsRoot().string()},
                                       {"projects", std::move(names)}},
                                  fmt::format("{} project(s)", count));
        });

    add(registry, "project.create", "Create a project",
        "Start a new empty project under this name and save it, so it exists on disk before "
        "anything is built in it. The session is replaced: anything unsaved is lost.",
        schema::object({{"name", schema::string("Project name, used as its folder and file name")}}),
        sessionWrite(true),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            const std::string name = args.value("name", std::string{});
            if (!safeProjectName(name)) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments,
                                           fmt::format("'{}' is not a usable project name", name),
                                           "use a plain name with no slashes or leading dots");
            }
            if (!ctx.hasProjectsRoot()) {
                return ToolResult::failure(ToolErrorCode::Unavailable,
                                           "no projects folder is configured in this session");
            }
            const std::filesystem::path dir = ctx.projectsRoot() / name;
            const std::filesystem::path file = dir / (name + ".json");
            std::error_code ec;
            if (std::filesystem::exists(file, ec)) {
                return ToolResult::failure(ToolErrorCode::Conflict,
                                           fmt::format("a project called '{}' already exists", name),
                                           "open it with project.open, or choose another name");
            }
            std::filesystem::create_directories(dir, ec);
            if (ec) {
                return ToolResult::failure(ToolErrorCode::Unavailable,
                                           fmt::format("cannot create '{}': {}", dir.string(),
                                                       ec.message()));
            }
            app::Engine& engine = ctx.engine();
            engine.newProject();
            if (auto r = engine.saveProject(file); !r) {
                return ToolResult::failure(ToolErrorCode::Unavailable, r.error().message);
            }
            return ToolResult::ok(json{{"name", name},
                                       {"file", file.string()},
                                       {"exists", std::filesystem::exists(file, ec)}},
                                  fmt::format("created '{}'", name));
        });

    add(registry, "project.save", "Save the project",
        "Write the open project back to its own file. Fails when the session has never been saved, "
        "which is when project.create or project.save_as is the tool you want. Reports the path and "
        "confirms it exists afterwards, because a save that silently wrote nothing is the failure "
        "worth catching.",
        noArgs(), sessionWrite(),
        [](const json&, ToolContext& ctx) -> ToolResult {
            app::Engine& engine = ctx.engine();
            if (engine.projectPath().empty()) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments,
                                           "this session has no project file yet",
                                           "use project.create to start one, or project.save_as to "
                                           "name this session");
            }
            const std::filesystem::path file = engine.projectPath();
            if (auto r = engine.saveProject(file); !r) {
                return ToolResult::failure(ToolErrorCode::Unavailable, r.error().message);
            }
            std::error_code ec;
            return ToolResult::ok(
                json{{"file", file.string()},
                     {"exists", std::filesystem::exists(file, ec)},
                     {"bytes", static_cast<std::uint64_t>(std::filesystem::file_size(file, ec))}},
                fmt::format("saved {}", file.filename().string()));
        });

    add(registry, "project.save_as", "Save the project under a name",
        "Write the open session to a project of this name and carry on working in it. Use it to "
        "name a session that was never saved, or to fork one.",
        schema::object({{"name", schema::string("Project name, used as its folder and file name")}}),
        sessionWrite(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            const std::string name = args.value("name", std::string{});
            if (!safeProjectName(name)) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments,
                                           fmt::format("'{}' is not a usable project name", name));
            }
            if (!ctx.hasProjectsRoot()) {
                return ToolResult::failure(ToolErrorCode::Unavailable,
                                           "no projects folder is configured in this session");
            }
            const std::filesystem::path dir = ctx.projectsRoot() / name;
            const std::filesystem::path file = dir / (name + ".json");
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            app::Engine& engine = ctx.engine();
            if (auto r = engine.saveProject(file); !r) {
                return ToolResult::failure(ToolErrorCode::Unavailable, r.error().message);
            }
            return ToolResult::ok(json{{"name", name},
                                       {"file", file.string()},
                                       {"exists", std::filesystem::exists(file, ec)}},
                                  fmt::format("saved as '{}'", name));
        });

    add(registry, "project.open", "Open a project",
        "Load a project by name, replacing the session. Anything unsaved is lost. Reports what came "
        "back -- parameters, routes, tracks, whether audio and a scene resolved -- and any load "
        "warnings, because a project whose scene is missing still 'opens' and the warnings are how "
        "you find out.",
        schema::object({{"name", schema::string("Project name, as reported by project.list")}}),
        sessionWrite(true),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            const std::string name = args.value("name", std::string{});
            if (!safeProjectName(name)) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments,
                                           fmt::format("'{}' is not a usable project name", name));
            }
            if (!ctx.hasProjectsRoot()) {
                return ToolResult::failure(ToolErrorCode::Unavailable,
                                           "no projects folder is configured in this session");
            }
            const std::filesystem::path file = ctx.projectsRoot() / name / (name + ".json");
            std::error_code ec;
            if (!std::filesystem::exists(file, ec)) {
                return ToolResult::failure(ToolErrorCode::NotFound,
                                           fmt::format("no project called '{}'", name),
                                           "project.list reports what is there");
            }
            app::Engine& engine = ctx.engine();
            if (auto r = engine.loadProject(file); !r) {
                return ToolResult::failure(ToolErrorCode::Unavailable, r.error().message);
            }
            json out;
            out["name"] = name;
            out["file"] = file.string();
            out["parameters"] = engine.params().size();
            out["routes"] = engine.modulator().routes().size();
            out["timelineTracks"] = engine.timeline().tracks().size();
            out["hasAudio"] = engine.hasAudio();
            out["hasComposition"] = engine.composition() != nullptr;
            if (!engine.projectWarnings().empty()) {
                out["warnings"] = engine.projectWarnings();
                out["warningNote"] = "the project opened, but these assets did not resolve";
            }
            return ToolResult::ok(std::move(out), fmt::format("opened '{}'", name));
        });

    // ---- generating a world ---------------------------------------------------------------------
    //
    // The composer is the one part of this engine that can fill a world without a human placing
    // anything, and until now the assistant had no way to ask it to. `world.generate` is a wrapper
    // over exactly what `--generate` and the Generate World button already do -- the same
    // `composeFromRecipeFile` and the same `installWorld` -- so a world an assistant makes is the
    // same object a person makes.
    //
    // Know the limit before promising anything with it: the composer places by *density* and a
    // world is capped at 64 scatter layers. It dresses a landscape. It does not lay out a street.
    add(registry, "world.list_recipes", "List world recipes",
        "The world recipes this session can reach, by name. A recipe is a seed, an extent, an asset "
        "library and a set of ecology weights; generating one fills a world with what the library "
        "allows. Read this before generating rather than guessing a name.",
        noArgs(), readOnly(),
        [](const json&, ToolContext& ctx) -> ToolResult {
            json found = json::array();
            std::error_code ec;
            for (const std::filesystem::path& root : ctx.contentRoots()) {
                if (!std::filesystem::is_directory(root, ec)) {
                    continue;
                }
                for (const auto& entry : std::filesystem::recursive_directory_iterator(
                         root, std::filesystem::directory_options::skip_permission_denied, ec)) {
                    if (!entry.is_regular_file(ec)) {
                        continue;
                    }
                    const std::string name = entry.path().filename().string();
                    if (name.size() < 13 || name.compare(name.size() - 12, 12, ".recipe.json") != 0) {
                        continue;
                    }
                    found.push_back(json{{"name", name.substr(0, name.size() - 12)},
                                         {"file", entry.path().string()}});
                }
            }
            const auto count = found.size();
            return ToolResult::ok(json{{"recipes", std::move(found)}},
                                  fmt::format("{} recipe(s)", count));
        });

    add(registry, "world.generate", "Generate a world",
        "Compose a world from a recipe and install it into the scene, replacing whatever ecology the "
        "terrain had. This is the same path as the Generate World button. Reports what was placed, "
        "which is the answer worth reading: a recipe whose library has nothing the weights ask for "
        "composes successfully and puts nothing in the world.",
        schema::object({{"recipe", schema::string("Recipe name, as reported by world.list_recipes")}}),
        mutatingDestructive(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            const std::string name = args.value("recipe", std::string{});
            if (name.empty()) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments, "no recipe named");
            }
            std::error_code ec;
            std::filesystem::path file;
            for (const std::filesystem::path& root : ctx.contentRoots()) {
                if (!std::filesystem::is_directory(root, ec)) {
                    continue;
                }
                for (const auto& entry : std::filesystem::recursive_directory_iterator(
                         root, std::filesystem::directory_options::skip_permission_denied, ec)) {
                    if (entry.is_regular_file(ec) &&
                        entry.path().filename().string() == name + ".recipe.json") {
                        file = entry.path();
                        break;
                    }
                }
                if (!file.empty()) {
                    break;
                }
            }
            if (file.empty()) {
                return ToolResult::failure(ToolErrorCode::NotFound,
                                           fmt::format("no recipe called '{}'", name),
                                           "world.list_recipes reports what is reachable");
            }
            auto world = app::composeFromRecipeFile(file);
            if (!world) {
                return ToolResult::failure(ToolErrorCode::Unavailable, world.error().message);
            }
            app::Engine& engine = ctx.engine();
            if (engine.composition() == nullptr) {
                engine.newComposition();
            }
            if (auto installed = app::installWorld(engine, *world); !installed) {
                return ToolResult::failure(ToolErrorCode::Unavailable, installed.error().message);
            }
            json out;
            out["recipe"] = name;
            out["world"] = world->recipe.world;
            out["seed"] = world->recipe.seed;
            out["extent"] = world->recipe.extent;
            out["assetsConsidered"] = world->assetsConsidered;
            out["layers"] = world->composed.layers.size();
            out["focalRegions"] = world->composed.plan.focal.size();
            out["voidRegions"] = world->composed.plan.voids.size();
            out["composeSeconds"] = world->composeSeconds;
            // Layers, not instances. Composing produces the *rule* for each scatter -- which asset,
            // which category, which biomes and slopes it may sit on -- and the instances themselves
            // only exist once the terrain is built from it. Reporting an instance count here would
            // be inventing a number this stage has not computed.
            json layers = json::array();
            for (const auto& layer : world->composed.layers) {
                layers.push_back(json{{"name", layer.name},
                                      {"asset", layer.asset},
                                      {"category", layer.category},
                                      {"maxSlope", layer.maxSlope}});
            }
            out["layerDetail"] = std::move(layers);
            if (world->composed.layers.empty()) {
                out["warning"] =
                    "the recipe composed but produced no layers: the library has nothing in the "
                    "categories the ecology weights ask for";
            }
            return ToolResult::ok(std::move(out),
                                  fmt::format("{} layer(s) from {} asset(s)",
                                              world->composed.layers.size(),
                                              world->assetsConsidered));
        });

    // ---- bringing media in ------------------------------------------------------------------------
    //
    // Reading a file the user points at is a different question from writing one, and it gets a
    // different answer: `ToolContext::resolveContent` bounds it to the directories the host listed,
    // with `..` and symlinks resolved *before* the containment test. Without that an import tool is
    // an arbitrary-file-read primitive handed to a language model.
    add(registry, "asset.list", "List available assets",
        "List stable asset IDs visible to this session, including built-in and project-owned assets.",
        schema::object({{"query", schema::string("Optional name, ID or type search")},
                        {"limit", schema::integer("Maximum results (default 100)", 1, kMaxLimit)}}),
        readOnly(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            const auto catalog = assets::catalogAssets(ctx.contentRoots(), ctx.engine().projectPath().parent_path());
            if (!catalog) return ToolResult::failure(ToolErrorCode::Unavailable, catalog.error().message);
            const auto found = assets::searchAssets(*catalog, args.value("query", std::string{}), limitOf(args));
            json out = json::array();
            for (const auto& asset : found) {
                out.push_back(json{{"id", asset.id}, {"name", asset.name}, {"type", asset.type},
                                   {"source", assets::assetSourceName(asset.source)},
                                   {"path", asset.path.string()}, {"tags", asset.tags}});
            }
            return ToolResult::ok(json{{"assets", std::move(out)}, {"totalVisible", catalog->size()}},
                                  fmt::format("{} asset(s) visible", found.size()));
        });

    add(registry, "asset.search", "Search available assets",
        "Search built-in and project asset metadata by stable ID, name or type.",
        schema::object({{"query", schema::string("Name, ID or type search")},
                        {"limit", schema::integer("Maximum results (default 100)", 1, kMaxLimit)}}),
        readOnly(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            const std::string query = args.value("query", std::string{});
            if (query.empty()) return ToolResult::failure(ToolErrorCode::InvalidArguments, "query must not be empty");
            const auto catalog = assets::catalogAssets(ctx.contentRoots(), ctx.engine().projectPath().parent_path());
            if (!catalog) return ToolResult::failure(ToolErrorCode::Unavailable, catalog.error().message);
            const auto found = assets::searchAssets(*catalog, query, limitOf(args));
            json out = json::array();
            for (const auto& asset : found) {
                out.push_back(json{{"id", asset.id}, {"name", asset.name}, {"type", asset.type},
                                   {"source", assets::assetSourceName(asset.source)},
                                   {"path", asset.path.string()}, {"tags", asset.tags}});
            }
            return ToolResult::ok(json{{"assets", std::move(out)}}, fmt::format("{} matching asset(s)", found.size()));
        });

    add(registry, "asset.get", "Get asset metadata",
        "Resolve one stable asset ID from asset.list or asset.search and return its ownership, type, path and tags.",
        schema::object({{"id", schema::string("Stable asset ID")}}, {"id"}),
        readOnly(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            const std::string id = args.at("id").get<std::string>();
            const auto catalog = assets::catalogAssets(ctx.contentRoots(), ctx.engine().projectPath().parent_path());
            if (!catalog) return ToolResult::failure(ToolErrorCode::Unavailable, catalog.error().message);
            for (const auto& asset : *catalog) {
                if (asset.id != id) continue;
                return ToolResult::ok(json{{"id", asset.id}, {"name", asset.name}, {"type", asset.type},
                                           {"source", assets::assetSourceName(asset.source)},
                                           {"path", asset.path.string()}, {"tags", asset.tags}},
                                      "asset metadata");
            }
            return ToolResult::failure(ToolErrorCode::NotFound, "asset ID is not visible in this session");
        });

    add(registry, "asset.import", "Import an asset into the project",
        "Copy a supported model, environment or texture into the open project's assets directory. The source must be reported by asset.list_importable or be inside an authorized content root; the original is never modified. Returns a stable project asset ID and copied dependencies.",
        schema::object({{"file", schema::string("Path to a reachable model, environment or texture")}}),
        sessionWrite(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            const std::string given = args.value("file", std::string{});
            if (given.empty()) return ToolResult::failure(ToolErrorCode::InvalidArguments, "no file named");
            const auto source = ctx.resolveContent(given);
            if (!source) return ToolResult::failure(ToolErrorCode::NotFound, "file is outside authorized content roots");
            app::Engine& engine = ctx.engine();
            if (engine.projectPath().empty()) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments, "open or create a project before importing assets");
            }
            const std::string type = importTypeFor(*source);
            if (type.empty()) return ToolResult::failure(ToolErrorCode::Unsupported, "supported asset types are glTF, GLB, HDR, EXR and common textures");
            auto hash = sha256File(*source);
            if (!hash) return ToolResult::failure(ToolErrorCode::Unavailable, hash.error().message);
            const std::filesystem::path assetRoot = engine.projectPath().parent_path() / "assets" / type;
            std::error_code ec;
            std::filesystem::create_directories(assetRoot, ec);
            if (ec) return ToolResult::failure(ToolErrorCode::Unavailable, "cannot create project asset directory: " + ec.message());
            std::vector<std::filesystem::path> dependencies;
            if (source->extension() == ".gltf") {
                std::ifstream in(*source);
                json gltf = json::parse(in, nullptr, false);
                if (gltf.is_discarded()) return ToolResult::failure(ToolErrorCode::Unsupported, "invalid glTF JSON");
                for (const char* key : {"buffers", "images"}) {
                    if (!gltf.contains(key) || !gltf.at(key).is_array()) continue;
                    for (const auto& item : gltf.at(key)) {
                        if (!item.is_object() || !item.contains("uri") || !item.at("uri").is_string()) continue;
                        const std::string uri = item.at("uri").get<std::string>();
                        if (uri.rfind("data:", 0) == 0 || uri.find("#") != std::string::npos) continue;
                        const auto dependency = source->parent_path() / uri;
                        if (!std::filesystem::is_regular_file(dependency, ec)) {
                            return ToolResult::failure(ToolErrorCode::NotFound, "missing glTF dependency: " + uri);
                        }
                        dependencies.push_back(dependency);
                    }
                }
            }
            for (const auto& entry : std::filesystem::recursive_directory_iterator(engine.projectPath().parent_path() / "assets",
                                                                                     std::filesystem::directory_options::skip_permission_denied, ec)) {
                if (!entry.is_regular_file(ec)) continue;
                auto existing = sha256File(entry.path());
                if (existing && *existing == *hash) {
                    return ToolResult::ok(json{{"id", "asset://project/" + type + "/" + entry.path().filename().string()},
                                               {"path", entry.path().string()}, {"source", "project"}, {"duplicate", true}},
                                          "asset already imported");
                }
            }
            const std::filesystem::path target = assetRoot / source->filename();
            const bool targetExisted = std::filesystem::exists(target, ec);
            std::vector<std::filesystem::path> createdFiles;
            bool committed = false;
            const auto rollback = [&] {
                if (committed) return;
                for (auto it = createdFiles.rbegin(); it != createdFiles.rend(); ++it) {
                    std::error_code removeError;
                    std::filesystem::remove(*it, removeError);
                }
            };
            std::filesystem::copy_file(*source, target, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) return ToolResult::failure(ToolErrorCode::Unavailable, "cannot copy asset: " + ec.message());
            if (!targetExisted) createdFiles.push_back(target);
            std::vector<std::string> copied;
            copied.push_back(target.string());
            for (const auto& dependency : dependencies) {
                    const std::filesystem::path dependencyTarget = assetRoot / dependency.filename();
                    const bool dependencyExisted = std::filesystem::exists(dependencyTarget, ec);
                    std::filesystem::copy_file(dependency, dependencyTarget, std::filesystem::copy_options::overwrite_existing, ec);
                    if (ec) { rollback(); return ToolResult::failure(ToolErrorCode::Unavailable, "cannot copy glTF dependency: " + ec.message()); }
                    if (!dependencyExisted) createdFiles.push_back(dependencyTarget);
                    copied.push_back(dependencyTarget.string());
            }
            const std::string id = "asset://project/" + type + "/" + target.stem().string();
            const std::filesystem::path manifestPath = engine.projectPath().parent_path() / "assets" / "manifest.json";
            json manifest = json::object();
            manifest["format"] = "avgen-project-assets";
            manifest["version"] = 1;
            manifest["assets"] = json::array();
            std::ifstream manifestIn(manifestPath);
            if (manifestIn) {
                json existing = json::parse(manifestIn, nullptr, false);
                if (!existing.is_discarded() && existing.is_object() && existing["assets"].is_array()) {
                    manifest = std::move(existing);
                }
            }
            bool replaced = false;
            for (auto& entry : manifest["assets"]) {
                if (entry.is_object() && entry.value("id", std::string{}) == id) {
                    entry = json{{"id", id}, {"type", type}, {"path", std::filesystem::relative(target, engine.projectPath().parent_path()).generic_string()},
                                 {"sha256", *hash}};
                    replaced = true;
                }
            }
            if (!replaced) {
                manifest["assets"].push_back(json{{"id", id}, {"type", type},
                                                    {"path", std::filesystem::relative(target, engine.projectPath().parent_path()).generic_string()},
                                                    {"sha256", *hash}});
            }
            const std::filesystem::path manifestTemp = manifestPath.string() + ".tmp";
            { std::ofstream out(manifestTemp, std::ios::trunc); out << manifest.dump(2) << '\n'; }
            std::filesystem::rename(manifestTemp, manifestPath, ec);
            if (ec) { rollback(); return ToolResult::failure(ToolErrorCode::Unavailable, "cannot update project asset manifest: " + ec.message()); }
            committed = true;
            return ToolResult::ok(json{{"id", id}, {"source", "project"}, {"type", type},
                                       {"path", target.string()}, {"sha256", *hash}, {"copied", copied}},
                                  "asset imported into project");
        });

    add(registry, "asset.list_importable", "List importable media",
        "Audio, glTF scenes and HDR environments the session can reach, by path. Use this to find a "
        "file rather than guessing where it lives -- a path outside the folders this session was "
        "given is refused, and guessing produces that refusal rather than a file.",
        schema::object({{"kind", schema::string("audio, scene or environment; omit for all")},
                        {"limit", schema::integer("Maximum results (default 40)", 1, kMaxLimit)}}),
        readOnly(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            const std::string kind = args.value("kind", std::string{});
            const std::size_t limit = limitOf(args);
            const auto wanted = [&kind](const std::string& ext) {
                const bool audio = ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".aiff" ||
                                   ext == ".aif" || ext == ".ogg" || ext == ".m4a";
                const bool scene = ext == ".gltf" || ext == ".glb";
                const bool env = ext == ".hdr";
                if (kind.empty()) return audio || scene || env;
                if (kind == "audio") return audio;
                if (kind == "scene") return scene;
                if (kind == "environment") return env;
                return false;
            };
            json found = json::array();
            std::error_code ec;
            for (const std::filesystem::path& root : ctx.contentRoots()) {
                if (!std::filesystem::is_directory(root, ec) || found.size() >= limit) {
                    continue;
                }
                for (const auto& entry : std::filesystem::recursive_directory_iterator(
                         root, std::filesystem::directory_options::skip_permission_denied, ec)) {
                    if (found.size() >= limit) {
                        break;
                    }
                    if (!entry.is_regular_file(ec)) {
                        continue;
                    }
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (!wanted(ext)) {
                        continue;
                    }
                    found.push_back(json{{"file", entry.path().string()},
                                         {"name", entry.path().filename().string()},
                                         {"kind", ext == ".hdr" ? "environment"
                                                                : (ext == ".gltf" || ext == ".glb" ? "scene"
                                                                                                   : "audio")}});
                }
            }
            const auto count = found.size();
            json roots = json::array();
            for (const auto& r : ctx.contentRoots()) {
                roots.push_back(r.string());
            }
            return ToolResult::ok(json{{"files", std::move(found)}, {"searched", std::move(roots)}},
                                  fmt::format("{} file(s)", count));
        });

    add(registry, "asset.import_audio", "Import audio",
        "Copy an audio file into the open project and make it the session's track, then analyze it. "
        "Copied rather than referenced on purpose: a project that points at a file on somebody's "
        "desktop stops working the day that file moves, and the project format stores a relative "
        "path and a hash precisely so it does not have to. Reports the duration, rate and channel "
        "count read back from the decoder, because an import that silently decoded nothing is the "
        "failure worth catching.",
        schema::object({{"file", schema::string("Path to the audio file, as reported by "
                                                "asset.list_importable")}}),
        sessionWrite(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            const std::string given = args.value("file", std::string{});
            if (given.empty()) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments, "no file named");
            }
            const auto resolved = ctx.resolveContent(given);
            if (!resolved) {
                return ToolResult::failure(
                    ToolErrorCode::NotFound,
                    fmt::format("'{}' is not inside a folder this session may read", given),
                    "asset.list_importable reports what is reachable");
            }
            std::error_code ec;
            if (!std::filesystem::is_regular_file(*resolved, ec)) {
                return ToolResult::failure(ToolErrorCode::NotFound,
                                           fmt::format("'{}' is not a file", resolved->string()));
            }
            app::Engine& engine = ctx.engine();
            if (engine.projectPath().empty()) {
                return ToolResult::failure(
                    ToolErrorCode::InvalidArguments,
                    "this session has no project to import into",
                    "use project.create first: the copy goes beside the project file, and without "
                    "one there is nowhere for it to live");
            }
            const std::filesystem::path audioDir = engine.projectPath().parent_path() / "audio";
            std::filesystem::create_directories(audioDir, ec);
            const std::filesystem::path target = audioDir / resolved->filename();
            std::filesystem::copy_file(*resolved, target,
                                       std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                return ToolResult::failure(ToolErrorCode::Unavailable,
                                           fmt::format("cannot copy into the project: {}", ec.message()));
            }
            const auto duration = engine.loadAudio(target);
            if (!duration) {
                std::filesystem::remove(target, ec); // do not leave a copy of a file that will not decode
                return ToolResult::failure(ToolErrorCode::Unsupported, duration.error().message,
                                           "the file was copied in and removed again; nothing in the "
                                           "project points at it");
            }
            json out;
            out["file"] = target.string();
            out["source"] = resolved->string();
            out["durationSeconds"] = *duration;
            if (const auto file = engine.audioFile(); file != nullptr) {
                out["sampleRate"] = file->sampleRate();
                out["channels"] = file->channels();
                out["frames"] = file->frameCount();
            }
            out["analyzed"] = engine.track() != nullptr;
            if (engine.track() != nullptr) {
                out["beatCount"] = engine.track()->beats().beatTimes.size();
            }
            return ToolResult::ok(std::move(out),
                                  fmt::format("imported {} ({:.1f} s)",
                                              target.filename().string(), *duration));
        });

    // ---- authoring the piece ----------------------------------------------------------------------
    //
    // `sequence.get_state` reads the piece; these three write it. Each edits `Engine::sequence()`
    // and then re-installs, because a sequence is a *value* and installing is what turns it into
    // timeline tracks and overlay layers. Re-installing is safe to do on every edit: it is
    // idempotent by design -- every track and layer the previous install owned is replaced, never
    // stacked -- which is what makes a per-edit bake the right shape here rather than a separate
    // "commit" the assistant could forget.
    //
    // Each reports the install's `unresolved` list. A track whose target parameter does not exist
    // evaluates and writes nothing, which looks exactly like a scene that is not reacting, and it
    // is the one kind of problem that must never be left to the log alone.
    add(registry, "sequence.add_marker", "Add a marker",
        "Put a section or cue marker on the piece. Sections are the song's structure -- INTRO, "
        "VERSE, CHORUS -- and are what a shot is placed against; cues are a point an author wants "
        "to find again. Beat markers are not authored here: they come from the analysis.",
        schema::object({{"time", schema::number("Seconds from the start of the piece")},
                        {"name", schema::string("What this moment is, e.g. CHORUS")},
                        {"kind", schema::string("section (default) or cue")}}),
        mutating(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            if (!args.contains("time") || !args.at("time").is_number()) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments, "a marker needs a time");
            }
            const std::string name = args.value("name", std::string{});
            if (name.empty()) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments, "a marker needs a name");
            }
            const std::string kindName = args.value("kind", std::string("section"));
            const auto kind = seq::markerKindFromName(kindName);
            if (!kind || *kind == seq::MarkerKind::Beat) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments,
                                           fmt::format("'{}' is not a marker an author sets", kindName),
                                           "use section or cue; beat markers come from the analysis");
            }
            app::Engine& engine = ctx.engine();
            seq::Marker marker;
            marker.timeSeconds = std::max(args.at("time").get<double>(), 0.0);
            marker.name = name;
            marker.kind = *kind;
            engine.sequence().markers.push_back(marker);
            std::stable_sort(engine.sequence().markers.begin(), engine.sequence().markers.end(),
                             [](const seq::Marker& a, const seq::Marker& b) {
                                 return a.timeSeconds < b.timeSeconds;
                             });
            const auto report = engine.installSequence();
            if (!report) {
                return ToolResult::failure(ToolErrorCode::Unavailable, report.error().message);
            }
            return ToolResult::ok(json{{"name", marker.name},
                                       {"timeSeconds", marker.timeSeconds},
                                       {"kind", seq::markerKindName(marker.kind)},
                                       {"markers", engine.sequence().markers.size()}},
                                  fmt::format("marker '{}' at {:.2f}s", marker.name, marker.timeSeconds));
        });

    add(registry, "sequence.add_shot", "Add a shot",
        "Add a shot to the piece: a span of time with one visual setup. `scene` names a scene slot "
        "the sequence already has; leaving it empty inherits the previous shot's. The camera is "
        "left to inherit unless a preset is named, because a shot with no camera holds the last "
        "one, which is what a cut between two angles of the same setup means.",
        schema::object({{"name", schema::string("Shot name")},
                        {"start", schema::number("Seconds from the start of the piece")},
                        {"duration", schema::number("Seconds (default 8)")},
                        {"scene", schema::string("Scene slot id; empty inherits the previous shot's")}}),
        mutating(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            const std::string name = args.value("name", std::string{});
            if (name.empty()) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments, "a shot needs a name");
            }
            app::Engine& engine = ctx.engine();
            seq::Sequence& piece = engine.sequence();
            if (std::ranges::any_of(piece.shots, [&name](const seq::Shot& s) { return s.name == name; })) {
                return ToolResult::failure(ToolErrorCode::Conflict,
                                           fmt::format("a shot called '{}' already exists", name));
            }
            const std::string slot = args.value("scene", std::string{});
            if (!slot.empty() &&
                !std::ranges::any_of(piece.scenes,
                                     [&slot](const seq::SceneSlot& s) { return s.id == slot; })) {
                return ToolResult::failure(ToolErrorCode::NotFound,
                                           fmt::format("no scene slot called '{}'", slot),
                                           "sequence.get_state lists the slots this piece has");
            }
            seq::Shot shot;
            shot.name = name;
            shot.startSeconds = std::max(args.value("start", 0.0), 0.0);
            shot.durationSeconds = std::max(args.value("duration", 8.0), 0.05);
            shot.scene = slot;
            piece.shots.push_back(shot);
            std::stable_sort(piece.shots.begin(), piece.shots.end(),
                             [](const seq::Shot& a, const seq::Shot& b) {
                                 return a.startSeconds < b.startSeconds;
                             });
            const auto report = engine.installSequence();
            if (!report) {
                return ToolResult::failure(ToolErrorCode::Unavailable, report.error().message);
            }
            json out{{"name", shot.name},
                     {"startSeconds", shot.startSeconds},
                     {"endSeconds", shot.endSeconds()},
                     {"scene", shot.scene},
                     {"shots", piece.shots.size()},
                     {"durationSeconds", piece.duration()}};
            if (!report->unresolved.empty()) {
                out["unresolved"] = report->unresolved;
                out["unresolvedNote"] = "these tracks name parameters this scene does not have; "
                                        "they animate nothing";
            }
            if (!report->warnings.empty()) {
                out["warnings"] = report->warnings;
            }
            return ToolResult::ok(std::move(out),
                                  fmt::format("shot '{}' at {:.2f}s", shot.name, shot.startSeconds));
        });

    add(registry, "sequence.add_overlay", "Add a text overlay",
        "Put timed text over the frame -- a title, a lyric line, a placeholder to fill in later. "
        "This is the 2D composition layer system (ADR-083) reached through the sequence, so the "
        "text is an overlay cue that becomes a real layer on install, editable afterwards like any "
        "other.",
        schema::object({{"text", schema::string("The line to show")},
                        {"start", schema::number("Seconds from the start of the piece")},
                        {"end", schema::number("Seconds; must be after start")},
                        {"id", schema::string("Stable id; derived from the text when omitted")},
                        {"style", schema::string("A named style the composition owns")}}),
        mutating(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            const std::string text = args.value("text", std::string{});
            if (text.empty()) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments, "an overlay needs text");
            }
            const double start = std::max(args.value("start", 0.0), 0.0);
            const double end = args.value("end", start + 3.0);
            if (!(end > start)) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments,
                                           "an overlay must end after it starts");
            }
            app::Engine& engine = ctx.engine();
            seq::Sequence& piece = engine.sequence();
            seq::OverlayCue cue;
            cue.id = args.value("id", std::string{});
            if (cue.id.empty()) {
                cue.id = fmt::format("overlay-{}", piece.overlays.size() + 1);
            }
            if (std::ranges::any_of(piece.overlays,
                                    [&cue](const seq::OverlayCue& o) { return o.id == cue.id; })) {
                return ToolResult::failure(ToolErrorCode::Conflict,
                                           fmt::format("an overlay with id '{}' already exists", cue.id));
            }
            cue.kind = seq::OverlayKind::Text;
            cue.content = text;
            cue.style = args.value("style", std::string{});
            cue.startSeconds = start;
            cue.endSeconds = end;
            piece.overlays.push_back(cue);
            const auto report = engine.installSequence();
            if (!report) {
                return ToolResult::failure(ToolErrorCode::Unavailable, report.error().message);
            }
            json out{{"id", cue.id},
                     {"text", cue.content},
                     {"startSeconds", cue.startSeconds},
                     {"endSeconds", cue.endSeconds},
                     {"overlays", piece.overlays.size()},
                     {"layersRealised", report->overlays.size()}};
            if (report->overlays.empty()) {
                out["warning"] = "the cue was added but became no layer: this session has no "
                                 "composition for it to live in";
            }
            return ToolResult::ok(std::move(out),
                                  fmt::format("overlay '{}' {:.2f}-{:.2f}s", cue.id, start, end));
        });

    // ---- entities and the fields that govern them -------------------------------------------------
    //
    // The engine has had all of this since ADR-088 and ADR-097 and none of it was reachable from a
    // prompt. §18 of the bootstrap brief asks for a "proximity influence prototype"; what exists is
    // the finished thing -- a field is a position, a radius, a falloff and a strength, and it scales
    // the depth of the reactions an entity already has, so a character walking past a lamp makes
    // the lamp answer the music more strongly without a second reactivity system.
    add(registry, "entity.list", "List entities",
        "The entities in the scene: what each one drives, the behaviours it runs, how many reactions "
        "it carries, and the profile it was built from. Reactions are what a music field scales, so "
        "an entity with none is one a field cannot affect.",
        noArgs(), readOnly(),
        [](const json&, ToolContext& ctx) -> ToolResult {
            const scene::Composition* comp = ctx.engine().composition();
            if (comp == nullptr) {
                return ToolResult::failure(ToolErrorCode::Unavailable,
                                           "this session has no composition");
            }
            json list = json::array();
            for (const entity::EntityDesc& e : comp->entities()) {
                json behaviours = json::array();
                for (const auto& b : e.behaviors) {
                    behaviours.push_back(b.kind);
                }
                list.push_back(json{{"name", e.name},
                                    {"node", e.driven()},
                                    {"profile", e.profile},
                                    {"behaviours", std::move(behaviours)},
                                    {"reactions", e.reactions.size()},
                                    {"sockets", e.sockets.size()}});
            }
            const auto count = list.size();
            return ToolResult::ok(json{{"entities", std::move(list)}},
                                  fmt::format("{} entit{}", count, count == 1 ? "y" : "ies"));
        });

    add(registry, "field.list", "List music influence fields",
        "The fields in the scene and what each resolved to: where it is, how far it reaches, which "
        "entities it governs, and -- the part worth reading -- whether it follows something baked or "
        "something live. A field on a baked actor is a pure function of time and survives a scrub "
        "exactly; one on a live entity does not (ADR-091).",
        noArgs(), readOnly(),
        [](const json&, ToolContext& ctx) -> ToolResult {
            const scene::Composition* comp = ctx.engine().composition();
            if (comp == nullptr) {
                return ToolResult::failure(ToolErrorCode::Unavailable,
                                           "this session has no composition");
            }
            json list = json::array();
            for (const entity::FieldDesc& f : comp->fields()) {
                list.push_back(json{{"name", f.name},
                                    {"source", f.source},
                                    {"strength", f.strength},
                                    {"floorGain", f.floorGain},
                                    {"reach", f.volume.reach()},
                                    {"scaleReactions", f.scaleReactions},
                                    {"enabled", f.enabled},
                                    {"tags", f.tags}});
            }
            json report = json::array();
            for (const std::string& line : comp->entityWorld().fieldReport()) {
                report.push_back(line);
            }
            const auto count = list.size();
            return ToolResult::ok(json{{"fields", std::move(list)}, {"resolution", std::move(report)}},
                                  fmt::format("{} field(s)", count));
        });

    add(registry, "field.create", "Create a music influence field",
        "Put a music influence field in the scene. It scales the depth of the reactions its governed "
        "entities already have, so the same `audio.bass -> emissiveGain` an author wrote becomes "
        "spatial: full strength at the centre, nothing at the edge, smooth between. Give it a "
        "`source` to make it follow a node or entity -- a character carrying the music with them -- "
        "and `tags` to say which entities it governs.",
        schema::object({{"name", schema::string("Field name")},
                        {"radius", schema::number("Reach in metres (default 12)")},
                        {"source", schema::string("Node or entity whose position it follows; omit to "
                                                  "pin it at `center`")},
                        {"center", schema::array(schema::number("metres"), "World position when it follows nothing")},
                        {"strength", schema::number("Gain at full influence (default 1)")},
                        {"floorGain", schema::number("Gain outside the volume (default 0)")},
                        {"falloff", schema::string("constant, linear, smooth (default) or inverseSquare")},
                        {"tags", schema::array(schema::string("tag"),
                                               "Entity tags or profile names it governs; empty means all")}}),
        mutating(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            scene::Composition* comp = ctx.engine().composition();
            if (comp == nullptr) {
                return ToolResult::failure(ToolErrorCode::Unavailable,
                                           "this session has no composition to put a field in");
            }
            const std::string name = args.value("name", std::string{});
            if (name.empty()) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments, "a field needs a name");
            }
            std::vector<entity::FieldDesc> fields = comp->fields();
            if (std::ranges::any_of(fields, [&name](const entity::FieldDesc& f) { return f.name == name; })) {
                return ToolResult::failure(ToolErrorCode::Conflict,
                                           fmt::format("a field called '{}' already exists", name));
            }
            entity::FieldDesc desc;
            desc.name = name;
            desc.source = args.value("source", std::string{});
            desc.strength = static_cast<float>(args.value("strength", 1.0));
            desc.floorGain = static_cast<float>(args.value("floorGain", 0.0));
            desc.volume.shape = entity::VolumeShape::Sphere;
            desc.volume.radius = static_cast<float>(std::max(args.value("radius", 12.0), 0.01));
            if (args.contains("center") && args.at("center").is_array() && args.at("center").size() == 3) {
                desc.volume.center = glm::vec3(args.at("center")[0].get<float>(),
                                               args.at("center")[1].get<float>(),
                                               args.at("center")[2].get<float>());
            }
            if (args.contains("falloff")) {
                if (!entity::falloffFromName(args.at("falloff").get<std::string>(), desc.volume.falloff)) {
                    return ToolResult::failure(
                        ToolErrorCode::InvalidArguments,
                        fmt::format("'{}' is not a falloff", args.at("falloff").get<std::string>()),
                        "use constant, linear, smooth or inverseSquare");
                }
            }
            if (args.contains("tags") && args.at("tags").is_array()) {
                for (const auto& t : args.at("tags")) {
                    if (t.is_string()) {
                        desc.tags.push_back(t.get<std::string>());
                    }
                }
            }
            fields.push_back(desc);
            if (auto r = comp->setFields(std::move(fields)); !r) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments, r.error().message);
            }
            json out{{"name", desc.name},
                     {"reach", desc.volume.reach()},
                     {"source", desc.source},
                     {"strength", desc.strength},
                     {"fields", comp->fields().size()}};
            json report = json::array();
            for (const std::string& line : comp->entityWorld().fieldReport()) {
                report.push_back(line);
            }
            out["resolution"] = std::move(report);
            if (desc.source.empty()) {
                out["note"] = "this field does not follow anything, so it stays where `center` put it";
            }
            return ToolResult::ok(std::move(out),
                                  fmt::format("field '{}', reach {:.1f} m", desc.name, desc.volume.reach()));
        });

    // ---- looking at the frame ----------------------------------------------------------------------
    //
    // The assistant has no eyes. Every provider in the registry declares `vision: false`, so an
    // image would be a file it cannot read; what it can use is the frame described as *numbers*.
    //
    // This answers the question a shot is actually judged on -- is the thing I framed in the frame,
    // and how big is it -- by projecting each node's bounds through the camera the scene has. Pure
    // arithmetic over `scene::Camera` and `Composition::nodeBounds`, so it needs no renderer and
    // works headless, which is also what makes it testable.
    add(registry, "render.probe", "Look at the frame",
        "What the camera can currently see, as numbers: which nodes fall inside the frustum, where "
        "each sits in the frame, and how much of the height it fills. Use it to check a shot frames "
        "its subject before trusting that it does -- a camera pointed at nothing reports nothing "
        "rather than looking fine. It reads the camera the last frame used, the same as camera.get, "
        "so after moving the camera let a frame happen before asking what it sees.",
        schema::object({{"node", schema::string("Ask about one node; omit for everything visible")},
                        {"aspect", schema::number("Frame aspect ratio (default 16:9)")},
                        {"limit", schema::integer("Maximum nodes reported (default 40)", 1, kMaxLimit)}}),
        readOnly(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            app::Engine& engine = ctx.engine();
            scene::Composition* comp = engine.composition();
            if (comp == nullptr) {
                return ToolResult::failure(ToolErrorCode::Unavailable,
                                           "this session has no composition to look at");
            }
            const scene::Camera& camera = engine.scene().camera;
            const auto aspect = static_cast<float>(std::max(args.value("aspect", 16.0 / 9.0), 0.01));
            const glm::mat4 viewProjection = camera.projection(aspect) * camera.view();
            const std::string only = args.value("node", std::string{});
            const std::size_t limit = limitOf(args);

            // One node, projected. Returns nothing when its bounds are unknown, which is a different
            // answer from "off screen" and is reported as such.
            const auto look = [&](const std::string& name) -> std::optional<json> {
                const scene::WorldBounds bounds = comp->nodeBounds(name);
                if (!bounds.valid) {
                    return std::nullopt;
                }
                // The eight corners, not the centre: a building whose centre is behind the camera
                // can still fill the frame, and a centre-only test would call it invisible.
                float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;
                float nearestZ = 1e30f;
                int inFront = 0;
                for (int corner = 0; corner < 8; ++corner) {
                    const glm::vec3 p((corner & 1) ? bounds.max.x : bounds.min.x,
                                      (corner & 2) ? bounds.max.y : bounds.min.y,
                                      (corner & 4) ? bounds.max.z : bounds.min.z);
                    const glm::vec4 clip = viewProjection * glm::vec4(p, 1.0f);
                    if (clip.w <= 1e-4f) {
                        continue; // behind the eye; contributes no screen position
                    }
                    ++inFront;
                    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                    minX = std::min(minX, ndc.x); maxX = std::max(maxX, ndc.x);
                    minY = std::min(minY, ndc.y); maxY = std::max(maxY, ndc.y);
                    nearestZ = std::min(nearestZ, clip.w);
                }
                json j;
                j["node"] = name;
                j["distance"] = glm::length(bounds.centre() - camera.position);
                j["radius"] = bounds.radius();
                if (inFront == 0) {
                    j["onScreen"] = false;
                    j["why"] = "entirely behind the camera";
                    return j;
                }
                const bool overlaps = maxX >= -1.0f && minX <= 1.0f && maxY >= -1.0f && minY <= 1.0f;
                j["onScreen"] = overlaps;
                j["partlyBehind"] = inFront < 8;
                // Normalised screen rectangle, 0..1 from the top-left, which is how a person would
                // describe where something sits in a frame.
                j["frame"] = json{{"left", (minX + 1.0f) * 0.5f},
                                  {"right", (maxX + 1.0f) * 0.5f},
                                  {"top", 1.0f - (maxY + 1.0f) * 0.5f},
                                  {"bottom", 1.0f - (minY + 1.0f) * 0.5f}};
                j["heightFraction"] = std::clamp((maxY - minY) * 0.5f, 0.0f, 4.0f);
                j["nearestDistance"] = nearestZ;
                if (!overlaps) {
                    j["why"] = "outside the frame";
                }
                return j;
            };

            json out;
            out["camera"] = json{{"position", {camera.position.x, camera.position.y, camera.position.z}},
                                 {"target", {camera.target.x, camera.target.y, camera.target.z}},
                                 {"fovYDegrees", camera.effectiveFovY() * 57.2957795f},
                                 {"aspect", aspect}};
            if (!only.empty()) {
                if (comp->findNode(only) == nullptr) {
                    return ToolResult::failure(ToolErrorCode::NotFound,
                                               fmt::format("no node called '{}'", only),
                                               "scene.find_nodes reports what the scene has");
                }
                const auto seen = look(only);
                if (!seen) {
                    return ToolResult::failure(ToolErrorCode::Unavailable,
                                               fmt::format("'{}' has no bounds to project", only),
                                               "a node with no geometry cannot be framed");
                }
                out["node"] = *seen;
                const bool on = (*seen)["onScreen"].get<bool>();
                return ToolResult::ok(std::move(out),
                                      fmt::format("'{}' is {}", only, on ? "in frame" : "not in frame"));
            }
            json visible = json::array();
            std::size_t offScreen = 0;
            std::size_t unbounded = 0;
            for (const auto& node : comp->nodes()) {
                const auto seen = look(node->name);
                if (!seen) {
                    ++unbounded;
                    continue;
                }
                if (!(*seen)["onScreen"].get<bool>()) {
                    ++offScreen;
                    continue;
                }
                if (visible.size() < limit) {
                    visible.push_back(*seen);
                }
            }
            const auto shown = visible.size();
            out["visible"] = std::move(visible);
            out["offScreen"] = offScreen;
            out["withoutBounds"] = unbounded;
            if (shown == 0) {
                out["warning"] = "the camera is framing nothing: every node with bounds is outside "
                                 "the frustum";
            }
            return ToolResult::ok(std::move(out), fmt::format("{} node(s) in frame", shown));
        });

    // ---- making and unmaking objects ---------------------------------------------------------------
    //
    // The verb the whole tool surface was missing. Everything else here edits something a person
    // already placed; these three put an object in the world, take one out, and say what belongs to
    // what.
    //
    // They are `mutating()` -- inside the snapshot domain -- which is only true because the
    // transaction now captures the composition as well as the parameters (see
    // `SnapshotStore::captureDocument`). Before that, a rollback would have restored a tool's
    // numbers and left the object it made standing in the scene.
    add(registry, "scene.create_node", "Create a node",
        "Put an object in the scene: a glTF asset by file, or an empty Group to parent things to. "
        "The asset path is resolved against the folders this session may read, so a model that is "
        "not reachable is refused here rather than becoming a node that renders nothing.",
        schema::object({{"name", schema::string("Node name; must not already exist")},
                        {"kind", schema::string("gltf (default) or group")},
                        {"asset", schema::string("Path to the .gltf/.glb, for kind gltf")},
                        {"parent", schema::string("Parent node name; omit for the root")},
                        {"position", schema::array(schema::number("metres"), "World position", 3, 3)},
                        {"scale", schema::number("Uniform scale (default 1)")},
                        {"rotationDegrees", schema::array(schema::number("degrees"), "Euler XYZ", 3, 3)}}),
        mutating(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            app::Engine& engine = ctx.engine();
            scene::Composition* comp = engine.composition();
            if (comp == nullptr) {
                return ToolResult::failure(ToolErrorCode::Unavailable,
                                           "this session has no composition to put a node in",
                                           "open a project with a scene, or generate a world first");
            }
            const std::string name = args.value("name", std::string{});
            if (name.empty()) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments, "a node needs a name");
            }
            if (comp->findNode(name) != nullptr) {
                return ToolResult::failure(ToolErrorCode::Conflict,
                                           fmt::format("a node called '{}' already exists", name),
                                           "names address nodes everywhere else in this API, so two "
                                           "of them would make one unreachable");
            }
            const std::string kindName = args.value("kind", std::string("gltf"));
            auto kind = scene::nodeKindFromName(kindName);
            if (!kind) {
                return ToolResult::failure(ToolErrorCode::InvalidArguments,
                                           fmt::format("'{}' is not a node kind", kindName),
                                           "use gltf for a model, or group for an empty transform");
            }
            if (*kind != scene::NodeKind::Gltf && *kind != scene::NodeKind::Group) {
                return ToolResult::failure(
                    ToolErrorCode::Unsupported,
                    fmt::format("this tool does not create a '{}' node", kindName),
                    "gltf and group are what an assistant can place meaningfully; terrain, "
                    "procedural and particle nodes carry settings blocks that belong in a scene file");
            }
            scene::CompositionNode node;
            node.name = name;
            node.kind = *kind;
            if (*kind == scene::NodeKind::Gltf) {
                const std::string asset = args.value("asset", std::string{});
                if (asset.empty()) {
                    return ToolResult::failure(ToolErrorCode::InvalidArguments,
                                               "a gltf node needs an asset");
                }
                const auto resolved = ctx.resolveContent(asset);
                if (!resolved) {
                    return ToolResult::failure(
                        ToolErrorCode::NotFound,
                        fmt::format("'{}' is not inside a folder this session may read", asset),
                        "asset.list_importable reports the models that are reachable");
                }
                std::error_code ec;
                if (!std::filesystem::is_regular_file(*resolved, ec)) {
                    return ToolResult::failure(ToolErrorCode::NotFound,
                                               fmt::format("'{}' is not a file", resolved->string()));
                }
                node.asset = *resolved;
            }
            const std::string parent = args.value("parent", std::string{});
            if (!parent.empty()) {
                if (comp->findNode(parent) == nullptr) {
                    return ToolResult::failure(ToolErrorCode::NotFound,
                                               fmt::format("no parent node called '{}'", parent));
                }
                node.parent = parent;
            }
            if (args.contains("position") && args.at("position").is_array() &&
                args.at("position").size() == 3) {
                node.transform.position = glm::vec3(args.at("position")[0].get<float>(),
                                                    args.at("position")[1].get<float>(),
                                                    args.at("position")[2].get<float>());
            }
            if (args.contains("rotationDegrees") && args.at("rotationDegrees").is_array() &&
                args.at("rotationDegrees").size() == 3) {
                node.transform.rotation = glm::vec3(args.at("rotationDegrees")[0].get<float>(),
                                                    args.at("rotationDegrees")[1].get<float>(),
                                                    args.at("rotationDegrees")[2].get<float>());
            }
            const auto uniform = static_cast<float>(args.value("scale", 1.0));
            node.transform.scale = glm::vec3(uniform);

            const auto added = comp->addNode(std::move(node));
            if (!added) {
                return ToolResult::failure(ToolErrorCode::Unavailable, added.error().message);
            }
            // What it actually came out as, read back: a model whose bounds are empty loaded
            // nothing, and that is invisible in every other report.
            const scene::WorldBounds bounds = comp->nodeBounds(name);
            json out{{"name", name},
                     {"kind", scene::nodeKindName(*kind)},
                     {"parent", (*added)->parent},
                     {"nodes", comp->nodes().size()}};
            out["bounds"] = json{{"valid", bounds.valid},
                                 {"size", {bounds.size().x, bounds.size().y, bounds.size().z}}};
            if (*kind == scene::NodeKind::Gltf && !bounds.valid) {
                out["warning"] = "the node was created but has no geometry: the asset loaded nothing";
            }
            return ToolResult::ok(std::move(out), fmt::format("created '{}'", name));
        });

    add(registry, "scene.delete_node", "Delete a node",
        "Remove a node from the scene. Its children go with it, so deleting a group deletes what it "
        "holds -- the count is reported rather than left to be discovered.",
        schema::object({{"name", schema::string("Node name")}}), mutating(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            scene::Composition* comp = ctx.engine().composition();
            if (comp == nullptr) {
                return ToolResult::failure(ToolErrorCode::Unavailable, "this session has no composition");
            }
            const std::string name = args.value("name", std::string{});
            if (comp->findNode(name) == nullptr) {
                return ToolResult::failure(ToolErrorCode::NotFound,
                                           fmt::format("no node called '{}'", name),
                                           "scene.find_nodes reports the canonical names");
            }
            const std::size_t before = comp->nodes().size();
            // The subtree, deepest first. `Composition::removeNode` takes one node and leaves its
            // children behind pointing at a parent that no longer exists -- an orphan whose world
            // transform is then whatever the root's is, which reads as an object that teleported.
            // Deleting a group has to mean deleting what it holds.
            std::vector<std::string> doomed{name};
            for (std::size_t i = 0; i < doomed.size(); ++i) {
                for (const auto& candidate : comp->nodes()) {
                    if (candidate->parent == doomed[i] &&
                        std::ranges::find(doomed, candidate->name) == doomed.end()) {
                        doomed.push_back(candidate->name);
                    }
                }
            }
            for (auto it = doomed.rbegin(); it != doomed.rend(); ++it) {
                comp->removeNode(*it);
            }
            const std::size_t after = comp->nodes().size();
            return ToolResult::ok(json{{"name", name},
                                       {"removed", before - after},
                                       {"nodes", after}},
                                  fmt::format("removed {} node(s)", before - after));
        });

    add(registry, "scene.set_parent", "Re-parent a node",
        "Move a node under another, or to the root. The node keeps its own local transform, so it "
        "moves with its new parent rather than staying where it looked -- which is what parenting "
        "means and is worth knowing before using it to tidy a scene.",
        schema::object({{"name", schema::string("Node to move")},
                        {"parent", schema::string("New parent; empty moves it to the root")}}),
        mutating(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            app::Engine& engine = ctx.engine();
            scene::Composition* comp = engine.composition();
            if (comp == nullptr) {
                return ToolResult::failure(ToolErrorCode::Unavailable, "this session has no composition");
            }
            const std::string name = args.value("name", std::string{});
            scene::CompositionNode* node = comp->findNode(name);
            if (node == nullptr) {
                return ToolResult::failure(ToolErrorCode::NotFound,
                                           fmt::format("no node called '{}'", name));
            }
            const std::string parent = args.value("parent", std::string{});
            if (!parent.empty()) {
                if (comp->findNode(parent) == nullptr) {
                    return ToolResult::failure(ToolErrorCode::NotFound,
                                               fmt::format("no node called '{}'", parent));
                }
                if (parent == name) {
                    return ToolResult::failure(ToolErrorCode::InvalidArguments,
                                               "a node cannot be its own parent");
                }
                // Walking up from the proposed parent: if this node is on that path, the assignment
                // would make a cycle, and a cycle in the transform hierarchy is an infinite loop the
                // next frame rather than an error anybody sees.
                for (const scene::CompositionNode* up = comp->findNode(parent); up != nullptr;) {
                    if (up->name == name) {
                        return ToolResult::failure(
                            ToolErrorCode::Conflict,
                            fmt::format("'{}' is already inside '{}'", parent, name),
                            "that would make the hierarchy a loop");
                    }
                    up = up->parent.empty() ? nullptr : comp->findNode(up->parent);
                }
            }
            const std::string was = node->parent;
            // Detach and re-add rather than writing `parent` in place: `addNode` is what marks the
            // composition dirty and rebuilds the hierarchy, and a field written behind its back
            // would take effect at whatever unrelated moment something else caused a rebuild.
            std::unique_ptr<scene::CompositionNode> detached = comp->detachNode(name);
            if (detached == nullptr) {
                return ToolResult::failure(ToolErrorCode::Unavailable,
                                           fmt::format("'{}' could not be detached", name));
            }
            detached->parent = parent;
            const auto added = comp->addNode(std::move(*detached));
            if (!added) {
                return ToolResult::failure(ToolErrorCode::Unavailable, added.error().message);
            }
            return ToolResult::ok(json{{"name", name},
                                       {"parent", parent},
                                       {"previousParent", was}},
                                  parent.empty() ? fmt::format("'{}' moved to the root", name)
                                                 : fmt::format("'{}' is now under '{}'", name, parent));
        });

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
            const auto summary = fmt::format("{} group(s), {} parameter(s)", groups.size(),
                                             ctx.engine().params().size());
            out["groups"] = std::move(list);
            out["total"] = ctx.engine().params().size();
            return ToolResult::ok(std::move(out), summary);
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
            const std::size_t returned = list.size();
            json out;
            out["parameters"] = std::move(list);
            out["matched"] = matches.size();
            out["returned"] = returned;
            if (offset + returned < matches.size()) {
                out["nextOffset"] = offset + returned;
            }
            const auto summary = fmt::format("{} match(es) for '{}'", matches.size(), query);
            return ToolResult::ok(std::move(out), summary);
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
            const auto summary = fmt::format("{} node(s), {} light(s)", j.at("nodes").size(),
                                             scene.lights.size());
            return ToolResult::ok(std::move(j), summary);
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
    "env/sky/intensity",    "scene/brightness",     "scene/volumeDensity",
    "scene/fogHeight",      "scene/fogHeightFalloff",
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
                                           "e.g. {\"scene/volumeDensity\": 0.008}");
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
            if (applied.empty()) {
                return ToolResult::failure(
                    ToolErrorCode::NotFound, "none of the given paths exist in this scene",
                    "call environment.get for the paths this scene actually has");
            }
            const auto summary = fmt::format("{} environment value(s) set", applied.size());
            json out;
            out["applied"] = std::move(applied);
            if (!unknown.empty()) {
                out["notFound"] = std::move(unknown);
            }
            return ToolResult::ok(std::move(out), summary);
        });

    add(registry, "lighting.list", "List lights",
        "Every light in the flattened scene, with the parameter paths that control it. A light that "
        "came in with a glTF node is scaled and tinted by that node's lightIntensity and lightColor; "
        "rig lights are driven by lightrig/*; ecology lights are derived and are not editable.",
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
                // The controlling parameters. Two conventions, because there are two kinds of light
                // here: the rig names its own, and a light an asset brought in is controlled through
                // the node that brought it -- "<node>/<light>" is the name, so the node is the part
                // before the slash.
                json controls = json::array();
                for (const params::IParameter* p : engine.params().ordered()) {
                    if (p->group() == "lightrig" && containsNoCase(p->path(), light.name)) {
                        controls.push_back(p->path());
                    }
                }
                if (const auto slash = light.name.find('/'); slash != std::string::npos) {
                    const std::string owner = light.name.substr(0, slash);
                    for (const char* field : {"lightIntensity", "lightColor"}) {
                        const std::string path = "nodes/" + owner + "/" + field;
                        if (engine.params().find(path) != nullptr) {
                            controls.push_back(path);
                        }
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
                "Lights are regenerated whenever the scene rebuilds, so they are changed through "
                "parameters rather than written directly: a light from a glTF node through that "
                "node's nodes/<node>/lightIntensity and nodes/<node>/lightColor (a scale and a tint "
                "on the asset's own values), a rig light through lightrig/*, and the whole scene "
                "through scene/keyLight or env/sky/sunIntensity. Ecology lights are aggregates "
                "derived from glowing vegetation and have no individual controls.";
            const auto summary = fmt::format("{} light(s)", engine.scene().lights.size());
            return ToolResult::ok(std::move(out), summary);
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
            const auto summary = fmt::format("{} track(s), {} cue(s)",
                                             engine.timeline().tracks().size(),
                                             engine.timeline().cues().size());
            return ToolResult::ok(std::move(out), summary);
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
    // The piece, as opposed to the tracks it bakes into. `sequencer.get_state` above reports
    // `params::Timeline` -- the baked result -- which is the right thing to keyframe against and the
    // wrong thing to reason about: it cannot say what a shot is, when it cuts, or what the song does
    // underneath it. Until this existed the assistant could add a keyframe to a music video and not
    // be able to answer "what shots are in it".
    //
    // Read-only, and deliberately so for now: editing a sequence means re-baking it, and a bake
    // replaces every track it owns (`ownedBySequence` in `sequencer.get_state`), so a mutating
    // version needs the transaction story worked out first.
    add(registry, "sequence.get_state", "Cinematic sequence",
        "The music video itself: its shots and when each one cuts, the scenes they cut between, the "
        "actors and the animation clips they play, the text overlays, and the section markers of "
        "the song. Also the music's own shape -- tempo, how many beats were detected, and the beats "
        "themselves around a moment you name. This is what to read before placing or judging a "
        "shot; `sequencer.get_state` reports the baked tracks, not the piece.",
        schema::object(
            {{"beatsAround", schema::number("Return the individual beat times near this second. "
                                            "Omit for the summary alone, which is usually enough.")},
             {"beatWindow", schema::number("Half-width in seconds around `beatsAround` (default 4)")},
             {"limit", schema::integer("Maximum shots, actors and overlays each (default 40)", 1,
                                       kMaxLimit)}}),
        readOnly(),
        [](const json& args, ToolContext& ctx) -> ToolResult {
            app::Engine& engine = ctx.engine();
            const seq::Sequence& piece = engine.sequence();
            const std::size_t limit = limitOf(args);
            json out;
            out["name"] = piece.name;
            out["durationSeconds"] = piece.duration();

            // The song. `audio.get_analysis` answers "what does it sound like at the playhead"; this
            // answers "what shape is it", which is the question a cut is made against.
            json audio;
            audio["hasAudio"] = engine.hasAudio();
            if (engine.hasAudio()) {
                audio["file"] = engine.audioPath().filename().generic_string();
                audio["durationSeconds"] = engine.durationSeconds();
            }
            if (const analysis::AnalysisTrack* track = engine.track(); track != nullptr) {
                const std::vector<double>& beats = track->beats().beatTimes;
                audio["beatCount"] = beats.size();
                if (!beats.empty()) {
                    audio["firstBeatSeconds"] = beats.front();
                    audio["lastBeatSeconds"] = beats.back();
                }
                if (args.contains("beatsAround") && !beats.empty()) {
                    const double centre = args.value("beatsAround", 0.0);
                    const double half = std::max(args.value("beatWindow", 4.0), 0.0);
                    json near = json::array();
                    for (const double beat : beats) {
                        if (beat >= centre - half && beat <= centre + half) {
                            near.push_back(beat);
                        }
                    }
                    audio["beatsNear"] = std::move(near);
                    audio["beatsNearWindow"] = json{{"centre", centre}, {"halfWidth", half}};
                }
            }
            audio["tempoBpm"] = engine.latestFrame().tempoBpm;
            out["audio"] = std::move(audio);

            json scenes = json::array();
            for (const seq::SceneSlot& slot : piece.scenes) {
                scenes.push_back(json{{"id", slot.id}, {"node", slot.node}, {"file", slot.file}});
            }
            out["scenes"] = std::move(scenes);

            json shots = json::array();
            for (std::size_t i = 0; i < piece.shots.size() && i < limit; ++i) {
                const seq::Shot& shot = piece.shots[i];
                json j;
                j["index"] = i;
                j["name"] = shot.name;
                j["startSeconds"] = shot.startSeconds;
                j["durationSeconds"] = shot.durationSeconds;
                j["endSeconds"] = shot.endSeconds();
                j["scene"] = shot.scene;
                j["camera"] = json{{"kind", seq::cameraKindName(shot.camera.kind)},
                                   {"lookAtActor", shot.camera.lookAtActor},
                                   {"keys", shot.camera.keys.size()}};
                j["transitionIn"] = json{{"kind", seq::transitionKindName(shot.in.kind)},
                                         {"seconds", shot.in.seconds}};
                j["transitionOut"] = json{{"kind", seq::transitionKindName(shot.out.kind)},
                                          {"seconds", shot.out.seconds}};
                j["tracks"] = shot.tracks.size();
                shots.push_back(std::move(j));
            }
            out["shots"] = std::move(shots);
            out["shotCount"] = piece.shots.size();

            json actors = json::array();
            for (std::size_t i = 0; i < piece.actors.size() && i < limit; ++i) {
                const seq::Actor& actor = piece.actors[i];
                json clips = json::array();
                for (const seq::ClipCue& cue : actor.clips) {
                    clips.push_back(json{{"timeSeconds", cue.timeSeconds},
                                         {"clip", cue.clip},
                                         {"speed", cue.speed}});
                }
                actors.push_back(json{{"id", actor.id},
                                      {"node", actor.nodeName()},
                                      {"visible", actor.visible},
                                      {"positionKeys", actor.keys.size()},
                                      {"pathActive", actor.path.active},
                                      {"clips", std::move(clips)}});
            }
            out["actors"] = std::move(actors);
            out["actorCount"] = piece.actors.size();

            json overlays = json::array();
            for (std::size_t i = 0; i < piece.overlays.size() && i < limit; ++i) {
                const seq::OverlayCue& cue = piece.overlays[i];
                overlays.push_back(json{{"id", cue.id},
                                        {"content", cue.content},
                                        {"style", cue.style},
                                        {"startSeconds", cue.startSeconds},
                                        {"endSeconds", cue.endSeconds}});
            }
            out["overlays"] = std::move(overlays);
            out["overlayCount"] = piece.overlays.size();

            // Sections and cues in full -- there are a handful and they are the landmarks a shot is
            // placed against. Beat markers are counted rather than listed: a three-minute song has
            // hundreds, and `audio.beatsNear` is the bounded way to ask for the ones that matter.
            json sections = json::array();
            json cues = json::array();
            std::size_t beatMarkers = 0;
            for (const seq::Marker& marker : piece.markers) {
                switch (marker.kind) {
                case seq::MarkerKind::Section:
                    sections.push_back(json{{"timeSeconds", marker.timeSeconds}, {"name", marker.name}});
                    break;
                case seq::MarkerKind::Cue:
                    cues.push_back(json{{"timeSeconds", marker.timeSeconds}, {"name", marker.name}});
                    break;
                case seq::MarkerKind::Beat:
                    ++beatMarkers;
                    break;
                }
            }
            out["sections"] = std::move(sections);
            out["cues"] = std::move(cues);
            out["beatMarkers"] = beatMarkers;
            out["events"] = piece.events.size();

            const auto summary =
                fmt::format("{} shot(s), {} actor(s), {} overlay(s) over {:.1f} s",
                            piece.shots.size(), piece.actors.size(), piece.overlays.size(),
                            piece.duration());
            return ToolResult::ok(std::move(out), summary);
        });

    add(registry, "audio.get_analysis", "Audio analysis",
        "What the analyzer currently hears: loudness, the five frequency bands, spectral centroid, "
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
            const auto summary = fmt::format("{} route(s)", list.size());
            json out;
            out["routes"] = std::move(list);
            out["total"] = routes.size();
            out["masterGain"] = ctx.engine().modulator().masterGain;
            return ToolResult::ok(std::move(out), summary);
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
