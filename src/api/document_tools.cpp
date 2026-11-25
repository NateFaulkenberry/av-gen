// The document tools (ADR-2157, Engine API stage S4): `document.get`, `document.validate`,
// `document.patch` and `project.validate`.
//
// One coherent contract for structural scene editing instead of a tool per scene object: a client reads
// the scene document, sends an RFC 6902 JSON Patch against it, and the engine rebuilds the composition
// from the result. Everything rides on the two functions that already define the scene document --
// `Composition::toJson` (lossless since ADR-2151) and `Engine::setCompositionJson` (the assistant's
// rollback path) -- so a patch inherits what they keep, and there is no second writer.
//
// `document.patch`, in order (02-session-api.md section 7):
//   1. D0 = toJson();
//   2. D1 = D0 patched (`nlohmann::json::patch`, one operation at a time so a failure names its op);
//   3. refuse what the API path refuses: a NEW schema error or a NEW unknown key (one D0 did not already
//      have) is INVALID_ARGUMENTS with `details.pointer`. Files on disk stay warn-only (ADR-278);
//   4. capture the session's parameter overrides (`params::overrideParameters` against the basis) and
//      the routes, which `installComposition` clears;
//   5. `setCompositionJson(D1)`;
//   6. put the routes back and re-apply the overrides -- except where the patch decided the value: an
//      override is re-applied only when doing so leaves the document the patch produced unchanged. A
//      value the document carries (a node's rotation, a light's intensity) is the patch's to set; a
//      value it does not carry (`post`, which toJson writes as read) is the session's. That rule needs
//      no table of which parameter lives where, and it is decided by the same toJson that defines D1;
//   7. return the changed paths, the parameters that appeared and disappeared, and what happened to
//      each override.
// `dryRun` stops after 3, plus a parse on a private Offline engine with its own AssetRegistry, and
// changes nothing. A failed parse leaves the old composition installed (fromJson fails before the
// install); a failure inside the install puts the pre-patch document back from a snapshot, so in both
// cases the scene's hash is what it was. The call runs in the Session's transaction, implicit or the
// caller's, whose snapshot already covers the composition (ADR-2153).

#include "ai/tool_api.hpp"
#include "ai/tool_context.hpp"
#include "ai/transaction.hpp"
#include "api/describe.hpp"
#include "api/session.hpp"
#include "api/warning_codes.hpp"
#include "app/engine.hpp"
#include "assets/asset_registry.hpp"
#include "core/hash.hpp"
#include "core/log.hpp"
#include "params/serialization.hpp"
#include "scene/composition.hpp"
#include "scene/route_liveness.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>

namespace avgen::api {
namespace {

namespace fs = std::filesystem;
using nlohmann::json;
namespace schema = ai::schema;
using ai::ToolErrorCode;
using ai::ToolResult;

// ---- findings ------------------------------------------------------------------------------------

std::string escapePointerToken(std::string_view token) {
    std::string out;
    for (const char c : token) {
        if (c == '~') {
            out += "~0";
        } else if (c == '/') {
            out += "~1";
        } else {
            out += c;
        }
    }
    return out;
}

struct Finding {
    FindingCode code;
    std::string pointer;
    std::string message;
    std::string document; // "scene" or "project"

    [[nodiscard]] json toJson() const {
        const FindingCodeInfo& info = findingCode(code);
        return json{{"severity", std::string(findingSeverityName(info.severity))},
                    {"code", std::string(info.code)},
                    {"pointer", pointer},
                    {"message", message},
                    {"document", document}};
    }
    [[nodiscard]] bool isError() const { return findingCode(code).severity == FindingSeverity::Error; }
    [[nodiscard]] bool sameAs(const Finding& o) const { return code == o.code && pointer == o.pointer; }
};

json findingsJson(const std::vector<Finding>& findings) {
    json out = json::array();
    for (const Finding& f : findings) {
        out.push_back(f.toJson());
    }
    return out;
}

bool anyError(const std::vector<Finding>& findings) {
    return std::any_of(findings.begin(), findings.end(), [](const Finding& f) { return f.isError(); });
}

// The document's content hash: sha256 of its canonical text (nlohmann objects are key-sorted).
std::string contentHash(const json& doc) { return "sha256:" + sha256Hex(doc.dump()); }

// ---- schema validation, as findings --------------------------------------------------------------

// The validator's location text ("scene.nodes[0].tint", seeded with `seed`) as a JSON pointer.
std::string pointerFromValidatorPath(std::string_view text, std::string_view seed) {
    std::string_view path = text.substr(0, text.find(' '));
    if (path.starts_with(seed)) {
        path.remove_prefix(seed.size());
    }
    std::string out;
    std::size_t i = 0;
    while (i < path.size()) {
        if (path[i] == '.') {
            const std::size_t end = path.find_first_of(".[", i + 1);
            out += "/" + escapePointerToken(path.substr(i + 1, end == std::string_view::npos ? end : end - i - 1));
            i = end == std::string_view::npos ? path.size() : end;
        } else if (path[i] == '[') {
            const std::size_t end = path.find(']', i);
            if (end == std::string_view::npos) {
                break;
            }
            out += "/" + std::string(path.substr(i + 1, end - i - 1));
            i = end + 1;
        } else {
            break;
        }
    }
    return out;
}

FindingCode classifySchemaError(const ai::ToolError& e) {
    const std::string& m = e.message;
    if (e.code == ToolErrorCode::OutOfRange) {
        return FindingCode::SchemaRange;
    }
    if (m.find("matches none of") != std::string::npos) {
        return FindingCode::SchemaInvalid;
    }
    if (m.find("missing required field") != std::string::npos) {
        return FindingCode::SchemaRequired;
    }
    if (m.find("must be one of:") != std::string::npos) {
        return FindingCode::SchemaEnum;
    }
    if (m.find(" (got ") != std::string::npos || m.find("must be a finite number") != std::string::npos) {
        return FindingCode::SchemaType;
    }
    return FindingCode::SchemaInvalid;
}

// A sub-schema of `root` that can be validated on its own: its local `$ref`s resolve against the root's
// `$defs`.
json standalone(const json& sub, const json& root) {
    json s = sub;
    if (s.is_object() && root.contains("$defs") && !s.contains("$defs")) {
        s["$defs"] = root.at("$defs");
    }
    return s;
}

// The generated document schema's verdict on `doc`, one finding per top-level key at most (the
// validator stops at the first problem it meets inside a block).
void schemaFindings(const json& docSchema, const json& doc, const std::string& document, std::vector<Finding>& out) {
    if (!doc.is_object()) {
        out.push_back({FindingCode::SchemaType, "", fmt::format("the {} document must be an object", document), document});
        return;
    }
    json envelope{{"type", "object"}};
    if (docSchema.contains("required")) {
        envelope["required"] = docSchema.at("required");
    }
    if (auto e = ai::validateAgainstSchema(envelope, doc, document)) {
        out.push_back({classifySchemaError(*e), pointerFromValidatorPath(e->message, document), e->message, document});
    }
    const json& props = docSchema.at("properties");
    for (const auto& [key, value] : doc.items()) {
        const auto sub = props.find(key);
        if (sub == props.end() || value.is_null()) {
            continue;
        }
        const std::string seed = document + "." + key;
        if (auto e = ai::validateAgainstSchema(standalone(*sub, docSchema), value, seed)) {
            if (e->code == ToolErrorCode::Internal) {
                log::warn("document schema: {}", e->message);
                continue;
            }
            std::string pointer = "/" + escapePointerToken(key) + pointerFromValidatorPath(e->message, seed);
            out.push_back({classifySchemaError(*e), std::move(pointer), e->message, document});
        }
    }
}

// ---- unknown keys, against the schema ------------------------------------------------------------
//
// The schema's `properties` are the parsers' key lists (ADR-2152), so a key outside them is one the
// engine does not read. Walked wherever the schema types an object: an `x-avgen-untyped` block, and a
// node whose kind is not typed, are not judged. `_` keys are notes (ADR-278) and always allowed.

const json* resolveRef(const json& root, const json& s) {
    const auto ref = s.find("$ref");
    if (ref == s.end() || !ref->is_string()) {
        return nullptr;
    }
    const std::string target = ref->get<std::string>();
    if (!target.starts_with("#/")) {
        return nullptr;
    }
    try {
        const json::json_pointer p(target.substr(1));
        return root.contains(p) ? &root.at(p) : nullptr;
    } catch (const json::exception&) {
        return nullptr;
    }
}

// The schema objects that apply to `value`: `s`, what its `$ref` names, and the `anyOf` form `value`
// matches (the first). False when a form should apply and none does: the value is then not judged.
bool collectViews(const json& root, const json& s, const json& value, std::vector<const json*>& views, int depth = 0) {
    if (!s.is_object() || depth > 32) {
        return true;
    }
    views.push_back(&s);
    if (const json* target = resolveRef(root, s)) {
        if (!collectViews(root, *target, value, views, depth + 1)) {
            return false;
        }
    }
    if (const auto any = s.find("anyOf"); any != s.end() && any->is_array()) {
        for (const json& form : *any) {
            if (!ai::validateAgainstSchema(standalone(form, root), value, "value")) {
                return collectViews(root, form, value, views, depth + 1);
            }
        }
        return false;
    }
    return true;
}

void unknownKeysIn(const json& root, const json& s, const json& value, const std::string& pointer,
                   std::set<std::string>& out) {
    std::vector<const json*> views;
    const bool matched = collectViews(root, s, value, views);
    bool untyped = !matched;
    for (const json* v : views) {
        untyped = untyped || v->value("x-avgen-untyped", false);
    }
    if (untyped) {
        return;
    }
    if (value.is_object()) {
        bool listed = false;
        for (const json* v : views) {
            listed = listed || v->contains("properties");
        }
        for (const auto& [key, child] : value.items()) {
            const json* sub = nullptr;
            for (const json* v : views) {
                if (const auto props = v->find("properties"); props != v->end() && props->contains(key)) {
                    sub = &props->at(key);
                    break;
                }
            }
            const std::string childPointer = pointer + "/" + escapePointerToken(key);
            if (sub == nullptr) {
                if (listed && !key.starts_with("_")) {
                    out.insert(childPointer);
                }
                continue;
            }
            unknownKeysIn(root, *sub, child, childPointer, out);
        }
    } else if (value.is_array()) {
        for (const json* v : views) {
            if (const auto items = v->find("items"); items != v->end() && items->is_object()) {
                for (std::size_t i = 0; i < value.size(); ++i) {
                    unknownKeysIn(root, *items, value[i], pointer + "/" + std::to_string(i), out);
                }
                break;
            }
        }
    }
}

std::set<std::string> unknownSceneKeys(const json& sceneSchemaDoc, const json& doc) {
    std::set<std::string> out;
    unknownKeysIn(sceneSchemaDoc, sceneSchemaDoc, doc, "", out);
    return out;
}

const json& cachedSceneSchema() {
    static const json s = sceneSchema();
    return s;
}
const json& cachedProjectSchema() {
    static const json s = projectSchema();
    return s;
}

// ---- private engines -------------------------------------------------------------------------------

// A scratch folder of this process's own, removed when it goes out of scope.
struct ScratchDir {
    fs::path path;
    ScratchDir() {
        static std::atomic<std::uint64_t> next{1};
        path = fs::temp_directory_path() /
               fmt::format("avgen-validate-{}-{}", static_cast<long>(getpid()), next.fetch_add(1));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~ScratchDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;
};

// Every node asset the scene document names, checked against `registry` (the asset ids and relative
// paths resolve as the engine's own load would resolve them).
void nodeAssetFindings(const json& doc, const assets::AssetRegistry& registry, std::vector<Finding>& out) {
    const auto nodes = doc.find("nodes");
    if (nodes == doc.end() || !nodes->is_array()) {
        return;
    }
    for (std::size_t i = 0; i < nodes->size(); ++i) {
        const json& node = (*nodes)[i];
        if (!node.is_object() || !node.contains("asset") || !node.at("asset").is_string()) {
            continue;
        }
        const std::string asset = node.at("asset").get<std::string>();
        if (asset.empty()) {
            continue;
        }
        const fs::path resolved = registry.resolve(fs::path(asset));
        std::error_code ec;
        if (!fs::exists(resolved, ec)) {
            out.push_back({FindingCode::AssetMissing, fmt::format("/nodes/{}/asset", i),
                           fmt::format("node '{}': '{}' does not exist (resolved to '{}'); the node is skipped at "
                                       "load and kept on save",
                                       node.value("name", std::string{}), asset, resolved.string()),
                           "scene"});
        }
    }
}

// Builds a composition from `doc` on a private Offline engine whose registry resolves exactly as
// `like`'s does. Appends `scene.parse` on refusal. Returns the private engine's parameter paths, or
// nothing when the parse failed.
std::optional<std::set<std::string>> privateSceneParse(const json& doc, const assets::AssetRegistry& like,
                                                      std::vector<Finding>& out) {
    app::Engine scratch(app::EngineMode::Offline);
    scratch.setLiveControl(false);
    scratch.assets().setBaseDirectory(like.baseDirectory());
    if (auto r = scratch.setCompositionJson(doc); !r) {
        out.push_back({FindingCode::SceneParse, "", r.error().message, "scene"});
        return std::nullopt;
    }
    std::set<std::string> paths;
    for (const params::IParameter* p : scratch.params().ordered()) {
        paths.insert(p->path());
    }
    return paths;
}

// ---- project.validate ------------------------------------------------------------------------------

json readJson(const fs::path& path) {
    std::ifstream in(path);
    if (!in) {
        return nullptr;
    }
    json j = json::parse(in, nullptr, false);
    return j.is_discarded() ? json(nullptr) : j;
}

// The asset references a project document names, as (pointer, path): the shapes `readAssetRef` reads.
void projectAssetRefs(const json& doc, std::vector<std::pair<std::string, std::string>>& out) {
    const auto pathOf = [](const json& ref) -> std::string {
        if (ref.is_string()) {
            return ref.get<std::string>();
        }
        if (ref.is_object() && ref.contains("path") && ref.at("path").is_string()) {
            return ref.at("path").get<std::string>();
        }
        return {};
    };
    const auto assetsIt = doc.find("assets");
    if (assetsIt == doc.end() || !assetsIt->is_object()) {
        return;
    }
    const json& a = *assetsIt;
    for (const char* key : {"audio", "environment"}) {
        if (a.contains(key)) {
            if (std::string p = pathOf(a.at(key)); !p.empty()) {
                out.emplace_back(fmt::format("/assets/{}", key), p);
            }
        }
    }
    if (a.contains("scene") && a.at("scene").is_object() && a.at("scene").contains("path")) {
        if (std::string p = pathOf(a.at("scene").at("path")); !p.empty()) {
            out.emplace_back("/assets/scene/path", p);
        }
    }
    if (a.contains("audioClips") && a.at("audioClips").is_array()) {
        for (std::size_t i = 0; i < a.at("audioClips").size(); ++i) {
            const json& clip = a.at("audioClips")[i];
            const json& ref = clip.is_object() && clip.contains("file") ? clip.at("file") : clip;
            if (std::string p = pathOf(ref); !p.empty()) {
                out.emplace_back(fmt::format("/assets/audioClips/{}", i), p);
            }
        }
    }
    if (a.contains("stems") && a.at("stems").is_array()) {
        for (std::size_t i = 0; i < a.at("stems").size(); ++i) {
            const json& stem = a.at("stems")[i];
            if (stem.is_object() && stem.contains("file")) {
                if (std::string p = pathOf(stem.at("file")); !p.empty()) {
                    out.emplace_back(fmt::format("/assets/stems/{}/file", i), p);
                }
            }
        }
    }
}

std::string auditPointer(const char* list, const scene::AuditEntry& e) {
    const std::string origin = e.detail.is_object() ? e.detail.value("origin", std::string{}) : std::string{};
    if (std::string_view(list) == "routes" && (origin.empty() || origin == "project")) {
        return fmt::format("/routes/{}", e.index);
    }
    if (std::string_view(list) == "tracks") {
        return fmt::format("/timeline/tracks/{}", e.index);
    }
    return {};
}

// Validates the project file at `file` on a private Offline engine. `displayFile` is what the result
// names (the scratch copy's path means nothing to a client).
json validateProjectFile(const fs::path& file, const std::string& displayFile) {
    std::vector<Finding> findings;
    const json doc = readJson(file);
    if (doc.is_null()) {
        findings.push_back({FindingCode::ProjectParse, "", fmt::format("'{}' is not readable JSON", displayFile),
                            "project"});
        return json{{"valid", false},
                    {"file", displayFile},
                    {"findings", findingsJson(findings)},
                    {"counts", json{{"error", 1}, {"warning", 0}, {"info", 0}}},
                    {"loaded", nullptr}};
    }
    schemaFindings(cachedProjectSchema(), doc, "project", findings);
    if (doc.is_object()) {
        for (const auto& [key, _] : doc.items()) {
            if (!key.starts_with("_") && !app::Engine::isProjectKey(key)) {
                findings.push_back({FindingCode::ProjectUnknownKey, "/" + escapePointerToken(key),
                                    fmt::format("'{}' is not a project key this build reads; it is kept on save", key),
                                    "project"});
            }
        }
    }

    app::Engine engine(app::EngineMode::Offline);
    engine.setLiveControl(false);
    json summary = nullptr;
    if (auto r = engine.loadProject(file); !r) {
        findings.push_back({FindingCode::ProjectParse, "", r.error().message, "project"});
    } else {
        const std::vector<std::string>& warnings = engine.projectWarnings();
        // Asset references: a missing file the loader relinked is reported as relinked, not missing.
        const fs::path dir = fs::absolute(file).parent_path();
        std::vector<std::pair<std::string, std::string>> refs;
        projectAssetRefs(doc, refs);
        for (const auto& [pointer, given] : refs) {
            const fs::path p(given);
            const fs::path resolved = p.is_absolute() ? p : dir / p;
            std::error_code ec;
            if (fs::exists(resolved, ec)) {
                continue;
            }
            const bool relinked = std::any_of(warnings.begin(), warnings.end(), [&](const std::string& w) {
                return w.starts_with("relinked ") && w.find(given) != std::string::npos;
            });
            if (!relinked) {
                findings.push_back({FindingCode::AssetMissing, pointer,
                                    fmt::format("'{}' does not exist (resolved to '{}')", given, resolved.string()),
                                    "project"});
            }
        }
        if (auto r2 = engine.requireProjectScene(); !r2) {
            findings.push_back({FindingCode::SceneMissing, "/assets/scene", r2.error().message, "project"});
        }
        for (const std::string& w : warnings) {
            if (w.starts_with("relinked ")) {
                findings.push_back({FindingCode::AssetRelinked, "", w, "project"});
            } else if (!engine.projectSceneError().empty() && w == "scene: " + engine.projectSceneError()) {
                continue; // reported above as scene.missing
            } else {
                findings.push_back({FindingCode::ProjectWarning, "", w, "project"});
            }
        }
        for (const std::string& path : engine.parameterBasis().unknownAtLoad) {
            findings.push_back({FindingCode::ParameterUnknown, "/parameters/" + escapePointerToken(path),
                                fmt::format("no parameter answers '{}'; it is not applied and is kept on save", path),
                                "project"});
        }
        // The scene: its file against the schema (unknown keys are warnings on disk), its node assets.
        if (const scene::Composition* comp = engine.composition(); comp != nullptr) {
            if (!engine.compositionPath().empty()) {
                const json sceneFile = readJson(engine.compositionPath());
                if (sceneFile.is_object()) {
                    std::vector<Finding> sceneSchemaFindings;
                    schemaFindings(cachedSceneSchema(), sceneFile, "scene", sceneSchemaFindings);
                    findings.insert(findings.end(), sceneSchemaFindings.begin(), sceneSchemaFindings.end());
                    for (const std::string& pointer : unknownSceneKeys(cachedSceneSchema(), sceneFile)) {
                        findings.push_back({FindingCode::SceneUnknownKey, pointer,
                                            "the engine does not read this key; it is warned about at load", "scene"});
                    }
                }
            }
            nodeAssetFindings(comp->toJson(), engine.assetRegistry(), findings);
        }
        // The route audit (ADR-902), as `avgen --audit-routes` runs it.
        const scene::RouteAudit audit = engine.auditRoutes();
        const auto report = [&](const char* list, const std::vector<scene::AuditEntry>& entries) {
            for (const scene::AuditEntry& e : entries) {
                if (e.verdict == params::liveness::Verdict::Live) {
                    continue;
                }
                const bool dead = e.verdict == params::liveness::Verdict::Dead;
                std::string rule;
                for (const auto& f : e.findings) {
                    if (f.verdict == e.verdict) {
                        rule = f.rule;
                        break;
                    }
                }
                findings.push_back({dead ? FindingCode::RouteDead : FindingCode::RouteHazard, auditPointer(list, e),
                                    fmt::format("{} [{}] {}: {}", list, rule, e.label, scene::auditReason(e)),
                                    "project"});
            }
        };
        report("routes", audit.routes);
        report("tracks", audit.tracks);
        report("effectDefaultRoutes", audit.effectDefaultRoutes);
        report("effects", audit.effects);
        summary = json{{"parameters", engine.params().ordered().size()},
                       {"routes", audit.routes.size()},
                       {"hasComposition", engine.composition() != nullptr}};
    }
    std::map<std::string, int> tally{{"error", 0}, {"warning", 0}, {"info", 0}};
    for (const Finding& f : findings) {
        ++tally[std::string(findingSeverityName(findingCode(f.code).severity))];
    }
    json counts = tally;
    return json{{"valid", !anyError(findings)},
                {"file", displayFile},
                {"findings", findingsJson(findings)},
                {"counts", std::move(counts)},
                {"loaded", std::move(summary)}};
}

// ---- document.patch ---------------------------------------------------------------------------------

std::set<std::string> parameterPaths(const params::ParameterSet& params) {
    std::set<std::string> out;
    for (const params::IParameter* p : params.ordered()) {
        out.insert(p->path());
    }
    return out;
}

ToolResult invalidPatch(std::string message, std::string pointer, json findings = nullptr,
                        std::string recovery = "fix the patch; nothing was changed") {
    json details{{"pointer", std::move(pointer)}};
    if (!findings.is_null()) {
        details["findings"] = std::move(findings);
    }
    return ToolResult::failure(ToolErrorCode::InvalidArguments, std::move(message), std::move(recovery),
                               std::move(details));
}

json changedPaths(const json& before, const json& after) {
    json out = json::array();
    for (const json& op : json::diff(before, after)) {
        out.push_back(json{{"op", op.at("op")}, {"path", op.at("path")}});
    }
    return out;
}

struct OverrideOutcome {
    json kept = json::array();     // re-applied over the rebuilt document
    json yielded = json::array();  // the patch set the value the document carries
    json dropped = json::array();  // the parameter no longer exists
};

// Steps 4-6 after the rebuild: the routes back, then each override re-applied unless the patch decided
// the value (see the header). `overrides` is path -> value as `overrideParameters` wrote it.
Result<OverrideOutcome> reapplyOverrides(app::Engine& engine, const json& overrides, const json& routes) {
    OverrideOutcome outcome;
    const scene::Composition* comp = engine.composition();
    const json rebuilt = comp != nullptr ? comp->toJson() : json();
    json candidates = json::object(); // path -> the live value before the patch
    json rebuiltValues = json::object();
    for (const auto& [path, value] : overrides.items()) {
        params::IParameter* p = engine.params().find(path);
        if (p == nullptr) {
            outcome.dropped.push_back(path);
            continue;
        }
        const json now = params::parameterToJson(*p);
        if (now == value) {
            outcome.kept.push_back(path); // the rebuilt document already holds it
            continue;
        }
        candidates[path] = value;
        rebuiltValues[path] = now;
    }
    json routesDoc{{"format", params::kProjectFormatName},
                   {"version", params::kProjectFormatVersion},
                   {"routes", routes.is_array() ? routes : json::array()}};
    routesDoc["parameters"] = candidates;
    if (auto r = params::loadProject(routesDoc, engine.params(), engine.modulator(), nullptr, nullptr); !r) {
        return std::unexpected(r.error());
    }
    const auto set = [&](const std::string& path, const json& value) {
        if (params::IParameter* p = engine.params().find(path); p != nullptr) {
            (void)params::parameterFromJson(*p, value);
        }
    };
    if (comp != nullptr && !candidates.empty() && comp->toJson() != rebuilt) {
        // Some override changes what the patch wrote. Decide each one alone, from the rebuilt state.
        for (const auto& [path, value] : rebuiltValues.items()) {
            set(path, value);
        }
        for (const auto& [path, value] : candidates.items()) {
            set(path, value);
            if (comp->toJson() == rebuilt) {
                outcome.kept.push_back(path);
            } else {
                set(path, rebuiltValues.at(path));
                outcome.yielded.push_back(path);
            }
        }
    } else {
        for (const auto& [path, _] : candidates.items()) {
            outcome.kept.push_back(path);
        }
    }
    engine.rebind();
    engine.modulator().resetState();
    return outcome;
}

ToolResult patchScene(const json& args, ai::ToolContext& ctx) {
    app::Engine& engine = ctx.engine();
    scene::Composition* comp = engine.composition();
    if (comp == nullptr) {
        return ToolResult::failure(ToolErrorCode::FailedPrecondition, "no composition is loaded",
                                   "project.create {scene: \"empty\"} or project.open a project with a scene first");
    }
    const json& patch = args.at("patch");
    const bool dryRun = args.value("dryRun", false);

    // 1-2. The current document, patched one operation at a time so a failure names its operation.
    const json before = comp->toJson();
    const std::string hashBefore = contentHash(before);
    json patched = before;
    for (std::size_t i = 0; i < patch.size(); ++i) {
        const json& op = patch[i];
        const std::string where = op.is_object() ? op.value("path", std::string{}) : std::string{};
        try {
            patched = patched.patch(json::array({op}));
        } catch (const json::exception& e) {
            return invalidPatch(fmt::format("patch operation {} ({} {}) does not apply: {}", i,
                                            op.is_object() ? op.value("op", std::string("?")) : std::string("?"), where,
                                            e.what()),
                                where, nullptr,
                                "read the document with document.get and patch against it; nothing was changed");
        }
    }

    // 3. What the API path refuses: new schema errors and new unknown keys.
    std::vector<Finding> wasFindings;
    std::vector<Finding> nowFindings;
    schemaFindings(cachedSceneSchema(), before, "scene", wasFindings);
    schemaFindings(cachedSceneSchema(), patched, "scene", nowFindings);
    std::vector<Finding> refused;
    for (const Finding& f : nowFindings) {
        const bool old = std::any_of(wasFindings.begin(), wasFindings.end(), [&](const Finding& w) { return w.sameAs(f); });
        if (f.isError() && !old) {
            refused.push_back(f);
        }
    }
    const std::set<std::string> unknownBefore = unknownSceneKeys(cachedSceneSchema(), before);
    for (const std::string& pointer : unknownSceneKeys(cachedSceneSchema(), patched)) {
        if (!unknownBefore.contains(pointer)) {
            refused.push_back({FindingCode::SceneUnknownKey, pointer,
                               "the engine does not read this key; the API refuses it rather than drop it silently",
                               "scene"});
        }
    }
    if (!refused.empty()) {
        const Finding& first = refused.front();
        return invalidPatch(fmt::format("the patched scene is refused at '{}': {}", first.pointer, first.message),
                            first.pointer, findingsJson(refused),
                            "fix the patch (schemas/avgen-scene.schema.json lists every key the scene reads); nothing "
                            "was changed");
    }

    const std::set<std::string> pathsBefore = parameterPaths(engine.params());
    const auto diffPaths = [&](const std::set<std::string>& after) {
        json appeared = json::array();
        json disappeared = json::array();
        for (const std::string& p : after) {
            if (!pathsBefore.contains(p)) {
                appeared.push_back(p);
            }
        }
        for (const std::string& p : pathsBefore) {
            if (!after.contains(p)) {
                disappeared.push_back(p);
            }
        }
        return std::pair{std::move(appeared), std::move(disappeared)};
    };

    if (dryRun) {
        std::vector<Finding> parse;
        const auto paths = privateSceneParse(patched, engine.assetRegistry(), parse);
        nodeAssetFindings(patched, engine.assetRegistry(), parse);
        if (!paths) {
            return invalidPatch(fmt::format("the engine refuses the patched scene: {}", parse.front().message), "",
                                findingsJson(parse));
        }
        auto [appeared, disappeared] = diffPaths(*paths);
        json out{{"dryRun", true},
                 {"applied", false},
                 {"hashBefore", hashBefore},
                 {"hashAfter", hashBefore},
                 {"predictedHash", contentHash(patched)},
                 {"changed", changedPaths(before, patched)},
                 {"parametersAdded", std::move(appeared)},
                 {"parametersRemoved", std::move(disappeared)},
                 {"findings", findingsJson(parse)}};
        return ToolResult::ok(std::move(out), fmt::format("dry run: {} operation(s) would apply", patch.size()));
    }

    // 4. The session's overrides and the routes, which the rebuild clears.
    const json overrides = params::overrideParameters(engine.params(), engine.parameterBasis());
    const json routes = params::saveProject(engine.params(), engine.modulator()).value("routes", json::array());
    const json snapshot = ai::SnapshotStore::captureDocument(engine);

    // 5. The rebuild.
    if (auto r = engine.setCompositionJson(patched); !r) {
        if (engine.composition() != comp) {
            // The install had begun: put the pre-patch project back, so a failure changes nothing.
            if (auto restored = ai::SnapshotStore::applyDocument(engine, snapshot); !restored) {
                return ToolResult::failure(ToolErrorCode::Internal,
                                           fmt::format("the patched scene failed to install ({}), and restoring the "
                                                       "previous one failed too: {}",
                                                       r.error().message, restored.error().message),
                                           "roll back the transaction this call ran in");
            }
        }
        return invalidPatch(fmt::format("the engine refuses the patched scene: {}", r.error().message), "",
                            findingsJson({Finding{FindingCode::SceneParse, "", r.error().message, "scene"}}));
    }

    // 6. Routes, then overrides.
    auto outcome = reapplyOverrides(engine, overrides, routes);
    if (!outcome) {
        (void)ai::SnapshotStore::applyDocument(engine, snapshot);
        return ToolResult::failure(ToolErrorCode::Internal,
                                   fmt::format("the session's parameters did not re-apply after the rebuild: {}",
                                               outcome.error().message),
                                   "nothing was changed; report this");
    }

    // 7. What changed.
    const json after = engine.composition()->toJson();
    auto [appeared, disappeared] = diffPaths(parameterPaths(engine.params()));
    json changed = changedPaths(before, after);
    for (const json& c : changed) {
        ctx.changes().note("scene" + c.at("path").get<std::string>(), c.at("op").get<std::string>());
    }
    const std::size_t changedCount = changed.size();
    json out{{"dryRun", false},
             {"applied", true},
             {"hashBefore", hashBefore},
             {"hashAfter", contentHash(after)},
             {"changed", std::move(changed)},
             {"parametersAdded", std::move(appeared)},
             {"parametersRemoved", std::move(disappeared)},
             {"overrides", json{{"kept", outcome->kept}, {"yielded", outcome->yielded}, {"dropped", outcome->dropped}}},
             {"findings", json::array()}};
    return ToolResult::ok(std::move(out), fmt::format("patched the scene: {} operation(s), {} path(s) changed",
                                                      patch.size(), changedCount));
}

// ---- registration -------------------------------------------------------------------------------------

json anything(std::string description) { return json{{"description", std::move(description)}}; }

json findingsSchema() {
    std::vector<std::string> codes;
    for (const FindingCodeInfo& f : kFindingCodes) {
        codes.emplace_back(f.code);
    }
    return schema::array(
        schema::object({{"severity", schema::string("error, warning or info", {"error", "warning", "info"})},
                        {"code", schema::string("A finding code (docs/api/errors.md)", codes)},
                        {"pointer", schema::string("A JSON pointer into `document`; empty for the whole document")},
                        {"message", schema::string("What was found, in a sentence")},
                        {"document", schema::string("scene or project", {"scene", "project"})}},
                       {"severity", "code", "pointer", "message", "document"}),
        "Structured findings");
}

ai::ToolAnnotations readAnnotations(std::vector<std::string> permissions = {}) {
    ai::ToolAnnotations a;
    a.readOnly = true;
    a.idempotent = true;
    a.requiresMainThread = true;
    a.permissions = std::move(permissions);
    return a;
}

// A validator builds a whole private engine (a composition, a project load with its audio analysis): seconds on
// a large world, on the host thread. Classed as a job, so a live host refuses it during a show, as it refuses
// the Director's scratch-copy evaluations.
ai::ToolAnnotations validatorAnnotations(std::vector<std::string> permissions = {}) {
    ai::ToolAnnotations a = readAnnotations(std::move(permissions));
    a.expensive = true;
    a.executionClass = ai::ExecutionClass::Job;
    return a;
}

void add(ai::ToolRegistry& r, std::string name, std::string title, std::string description, json input,
         ai::ToolAnnotations annotations, json output, ai::ToolFn fn) {
    ai::Tool tool;
    tool.definition.name = std::move(name);
    tool.definition.title = std::move(title);
    tool.definition.description = std::move(description);
    tool.definition.inputSchema = std::move(input);
    tool.definition.outputSchema = std::move(output);
    tool.definition.annotations = std::move(annotations);
    tool.definition.since = 2;
    tool.execute = std::move(fn);
    r.add(std::move(tool));
}

} // namespace

void registerDocumentTools(ai::ToolRegistry& registry) {
    add(registry, "document.get", "Get a document",
        "The scene or project document as the engine would write it now, with a content hash to compare later "
        "reads with. `scene` is Composition::toJson: every key the scene reads, plus the keys it keeps without "
        "reading (`_` notes, unknown top-level keys); the shape is schemas/avgen-scene.schema.json, and it is "
        "what document.patch paths point into. `project` is the document a project save would write, built "
        "without writing anything. Read it before patching.",
        schema::object({{"document", schema::string("Which document", {"scene", "project"})}}, {"document"}),
        readAnnotations(),
        schema::object({{"document", schema::string("scene or project")},
                        {"content", anything("The document")},
                        {"hash", schema::string("sha256:<hex> of the document's canonical JSON text")}},
                       {"document", "content", "hash"}, true),
        [](const json& args, ai::ToolContext& ctx) -> ToolResult {
            app::Engine& engine = ctx.engine();
            const std::string which = args.at("document").get<std::string>();
            json content;
            if (which == "scene") {
                const scene::Composition* comp = engine.composition();
                if (comp == nullptr) {
                    return ToolResult::failure(ToolErrorCode::FailedPrecondition, "no composition is loaded",
                                               "project.create {scene: \"empty\"} or open a project with a scene");
                }
                content = comp->toJson();
            } else {
                // Built for the project's own path (asset paths relative to its folder), or a scratch path when
                // it was never saved. projectDocument writes nothing.
                const ScratchDir scratch;
                const fs::path at = engine.projectPath().empty() ? scratch.path / "project.json" : engine.projectPath();
                content = engine.projectDocument(at);
            }
            std::string hash = contentHash(content);
            const std::string summary = fmt::format("{} document, {}", which, hash.substr(0, 19));
            return ToolResult::ok(json{{"document", which}, {"content", std::move(content)}, {"hash", std::move(hash)}},
                                  summary);
        });

    add(registry, "document.validate", "Validate a document",
        "Check a scene or project document without installing it. First against the generated schema "
        "(schemas/avgen-<document>.schema.json, with the engine's own validator), then by building it on a private "
        "offline engine with its own asset registry: a scene is parsed into a composition and its node assets are "
        "resolved as the session's scene resolves them; a project is loaded from a scratch copy whose relative "
        "paths are resolved against the open project's folder. Returns structured findings "
        "{severity, code, pointer, message, document}; valid is true when none is an error. Unknown scene keys "
        "are warnings here (a file on disk is only warned about); document.patch refuses a patch that adds one.",
        schema::object({{"document", schema::string("Which kind of document `content` is", {"scene", "project"})},
                        {"content", schema::object(json::object(), {}, true)}},
                       {"document", "content"}),
        validatorAnnotations(),
        schema::object({{"valid", schema::boolean("No finding is an error")},
                        {"document", schema::string("scene or project")},
                        {"hash", schema::string("sha256:<hex> of the content")},
                        {"findings", findingsSchema()},
                        {"parameters", schema::integer("scene: the parameters the parsed composition registers")}},
                       {"valid", "document", "hash", "findings"}, true),
        [](const json& args, ai::ToolContext& ctx) -> ToolResult {
            app::Engine& engine = ctx.engine();
            const std::string which = args.at("document").get<std::string>();
            const json& content = args.at("content");
            std::vector<Finding> findings;
            json out{{"document", which}, {"hash", contentHash(content)}};
            if (which == "scene") {
                schemaFindings(cachedSceneSchema(), content, "scene", findings);
                for (const std::string& pointer : unknownSceneKeys(cachedSceneSchema(), content)) {
                    findings.push_back({FindingCode::SceneUnknownKey, pointer,
                                        "the engine does not read this key: a load warns about it, and "
                                        "document.patch refuses to add it",
                                        "scene"});
                }
                if (const auto paths = privateSceneParse(content, engine.assetRegistry(), findings)) {
                    out["parameters"] = paths->size();
                }
                nodeAssetFindings(content, engine.assetRegistry(), findings);
            } else {
                // The content's relative paths mean the open project's folder, and it is loaded from a scratch
                // folder (nothing is written beside the person's files), so its asset references are made
                // absolute against that folder first.
                json copy = content;
                const fs::path base = ctx.paths().projectFolder();
                if (copy.is_object() && !base.empty()) {
                    std::vector<std::pair<std::string, std::string>> refs;
                    projectAssetRefs(copy, refs);
                    for (const auto& [pointer, given] : refs) {
                        if (fs::path(given).is_absolute()) {
                            continue;
                        }
                        const std::string absolute = (base / given).lexically_normal().string();
                        json& slot = copy.at(json::json_pointer(pointer));
                        if (slot.is_object()) {
                            slot["path"] = absolute;
                        } else {
                            slot = absolute;
                        }
                    }
                }
                const ScratchDir scratch;
                const fs::path file = scratch.path / "content.json";
                std::ofstream(file) << copy.dump(2) << '\n';
                json report = validateProjectFile(file, "(content)");
                for (const json& f : report.at("findings")) {
                    // Re-read into findings for one shape of result; the codes are the table's.
                    for (const FindingCodeInfo& info : kFindingCodes) {
                        if (info.code == f.at("code").get<std::string>()) {
                            findings.push_back({info.id, f.at("pointer").get<std::string>(),
                                                f.at("message").get<std::string>(), f.at("document").get<std::string>()});
                            break;
                        }
                    }
                }
            }
            out["valid"] = !anyError(findings);
            out["findings"] = findingsJson(findings);
            const auto summary = fmt::format("{} document {}: {} finding(s)", which,
                                             out["valid"].get<bool>() ? "valid" : "invalid", findings.size());
            return ToolResult::ok(std::move(out), summary);
        });

    ai::ToolAnnotations patchAnnotations;
    patchAnnotations.mutatesProject = true;
    patchAnnotations.undoable = true; // the transaction's snapshot covers the composition (captureDocument)
    patchAnnotations.requiresMainThread = true;
    patchAnnotations.executionClass = ai::ExecutionClass::Structural;
    patchAnnotations.requirements = {"composition"};
    add(registry, "document.patch", "Patch the scene document",
        "Edit the scene by applying an RFC 6902 JSON Patch to its document (document.get {document: scene}), then "
        "rebuilding the composition from the result. One contract for every structural edit: add a light "
        "(`{op: add, path: /lights/-, value: {...}}`), a node (`/nodes/-`), change a node's tint "
        "(`/nodes/<i>/tint`), remove anything. Refused with INVALID_ARGUMENTS and details.pointer when an operation "
        "does not apply, when the result has a schema error it did not have before, or when it adds a key the "
        "engine does not read; nothing is changed then. Keys the patch does not touch are kept, including `_` "
        "notes and unknown top-level keys. The session's parameter overrides survive the rebuild: each is "
        "re-applied unless the patch set the value the document carries (overrides.yielded lists those). Routes "
        "are kept. dryRun checks the patch, builds the result on a private engine and changes nothing. It runs "
        "in the call's transaction, so transaction.rollback restores the pre-patch document. A rebuild restarts "
        "entity simulations; a live host refuses it while a show runs.",
        schema::object({{"document", schema::string("Only the scene can be patched", {"scene"})},
                        {"patch", schema::array(schema::object({{"op", schema::string("RFC 6902 operation",
                                                                                      {"add", "remove", "replace", "move",
                                                                                       "copy", "test"})},
                                                                {"path", schema::string("A JSON pointer into the scene")},
                                                                {"from", schema::string("move/copy: the source pointer")},
                                                                {"value", anything("add/replace/test: the value")}},
                                                               {"op", "path"}, true),
                                                "RFC 6902 operations, applied in order", 1, 256)},
                        {"dryRun", schema::boolean("Check and predict, change nothing")}},
                       {"document", "patch"}),
        patchAnnotations,
        schema::object({{"dryRun", schema::boolean("This was a dry run")},
                        {"applied", schema::boolean("The composition was rebuilt from the patched document")},
                        {"hashBefore", schema::string("The scene document's hash before")},
                        {"hashAfter", schema::string("Its hash after (equal to hashBefore on a dry run)")},
                        {"predictedHash", schema::string("dryRun: the hash of the patched document as sent")},
                        {"changed", schema::array(anything("{op, path}: a JSON pointer that changed"),
                                                  "What changed in the scene document")},
                        {"parametersAdded", schema::array(schema::string("parameter path"),
                                                          "Parameters the rebuild registered that did not exist")},
                        {"parametersRemoved", schema::array(schema::string("parameter path"),
                                                            "Parameters that no longer exist")},
                        {"overrides", anything("{kept, yielded, dropped}: the session's overrides after the rebuild")},
                        {"findings", findingsSchema()}},
                       {"dryRun", "applied", "hashBefore", "hashAfter", "changed", "parametersAdded",
                        "parametersRemoved", "findings"},
                       true),
        [](const json& args, ai::ToolContext& ctx) -> ToolResult { return patchScene(args, ctx); });

    add(registry, "project.validate", "Validate a project",
        "Check a project without opening it in this session: it is loaded on a private offline engine with its "
        "own asset registry (Engine::loadProject, requireProjectScene, the route audit), checked against "
        "schemas/avgen-project.schema.json, its scene file against the scene schema, and every asset it and its "
        "scene name is resolved. Returns findings {severity, code, pointer, message, document} with codes such as "
        "asset.missing, asset.relinked, parameter.unknown, route.dead, scene.unknownKey and schema.*; valid is "
        "true when none is an error. `path` is a project file (resolved like project.open); without it, the "
        "session's own project as it is in memory, through a scratch copy (nothing of the person's is written).",
        schema::object({{"path", schema::string("A project .json; omit for the open project as it is now")}}),
        validatorAnnotations({"read"}),
        schema::object({{"valid", schema::boolean("No finding is an error")},
                        {"file", schema::string("The project validated, or (session) for the in-memory one")},
                        {"findings", findingsSchema()},
                        {"counts", anything("{error, warning, info}")},
                        {"loaded", anything("{parameters, routes, hasComposition} when the load succeeded, else null")}},
                       {"valid", "file", "findings", "counts"}, true),
        [](const json& args, ai::ToolContext& ctx) -> ToolResult {
            json report;
            if (args.contains("path")) {
                const auto file = ctx.paths().forRead(args.at("path").get<std::string>());
                if (!file) {
                    return ToolResult::failure(file.error());
                }
                report = validateProjectFile(*file, file->string());
            } else {
                app::Engine& engine = ctx.engine();
                const ScratchDir scratch;
                const fs::path stem = engine.projectPath().empty() ? fs::path("session") : engine.projectPath().stem();
                const fs::path copy = scratch.path / (stem.string() + ".json");
                if (auto r = engine.writeProjectCopy(copy); !r) {
                    return ToolResult::failure(ToolErrorCode::Unavailable, r.error().message);
                }
                report = validateProjectFile(copy, "(session)");
            }
            const auto summary = fmt::format("{}: {} ({} error, {} warning)", report.at("file").get<std::string>(),
                                             report.at("valid").get<bool>() ? "valid" : "invalid",
                                             report.at("counts").at("error").get<int>(),
                                             report.at("counts").at("warning").get<int>());
            return ToolResult::ok(std::move(report), summary);
        });
}

} // namespace avgen::api
