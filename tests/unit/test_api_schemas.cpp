// ADR-2152: the document schemas are generated from the parsers' key lists and an authored typing
// table, and these tests hold the three together. Ids follow the Phase 0 plan's table:
//
//   D1  the committed schemas are what the engine generates (T1's artefact list carries them)
//   D2  every key in each key list has a typing entry, and every entry names a listed key
//   D3  every shipped example validates against its schema with the engine's own validator
//   D4  `writtenBack: false` entries are exactly ADR-2151's documented normalisations
//   C1  capability.list's `features` is featureList(), and every node kind in it parses
//   C3  no authored "not exposed" prose names a tool that exists, or a stale claim
//
// plus the document-schema keyword subset (so the engine can validate its own documents) and the
// validator's three additions (anyOf, const, local $ref).

#include "ai/capabilities.hpp"
#include "ai/tool_api.hpp"
#include "api/describe.hpp"
#include "api/schema_types.hpp"
#include "app/engine.hpp"
#include "assets/asset_registry.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/post_settings.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

using nlohmann::json;
namespace api = avgen::api;
namespace ai = avgen::ai;
namespace fs = std::filesystem;

const fs::path kRoot(AVGEN_SOURCE_DIR);

std::string parentOf(const std::string& pointer) {
    const auto slash = pointer.rfind('/');
    return slash == std::string::npos ? std::string{} : pointer.substr(0, slash);
}

std::string leafOf(const std::string& pointer) { return pointer.substr(pointer.rfind('/') + 1); }

// D2, as a function so the test can also prove it fires.
std::vector<std::string> bijectionProblems(const api::DocumentTyping& typing) {
    std::vector<std::string> problems;
    std::set<std::string> listed;
    std::set<std::string> listPointers;
    for (const api::KeyList& list : typing.keyLists) {
        listPointers.insert(list.pointer);
        for (const std::string& key : list.keys) {
            const std::string pointer = list.pointer + "/" + key;
            listed.insert(pointer);
            const bool typed = std::any_of(typing.entries.begin(), typing.entries.end(),
                                           [&](const api::TypingEntry& e) { return e.pointer == pointer; });
            if (!typed) {
                problems.push_back("'" + key + "' (" + list.source + ") has no typing entry: add one for '" + pointer +
                                   "' to " + typing.tableFile +
                                   " (a fragment, or {\"x-avgen-untyped\": true} if it is not typed yet)");
            }
        }
    }
    std::set<std::string> seen;
    for (const api::TypingEntry& e : typing.entries) {
        if (!seen.insert(e.pointer).second) {
            problems.push_back("'" + e.pointer + "' is typed twice in " + typing.tableFile);
        }
        if (listPointers.contains(parentOf(e.pointer))) {
            if (!listed.contains(e.pointer)) {
                problems.push_back("'" + e.pointer + "' in " + typing.tableFile +
                                   " names no key its parser lists: remove it, or add the key to the list");
            }
        } else if (e.writtenBack) {
            // Below every key list, an entry exists only to record a key a save does not keep.
            problems.push_back("'" + e.pointer + "' in " + typing.tableFile +
                               " is under no key list and says nothing (writtenBack is true): remove it");
        }
        if (!e.writtenBack && e.note.empty()) {
            problems.push_back("'" + e.pointer + "' is not written back and does not say why");
        }
    }
    return problems;
}

// The keywords `ai::validateAgainstSchema` enforces, and the annotations a document schema may carry.
const std::set<std::string> kEnforced = {"type",     "properties", "required", "additionalProperties", "enum",
                                         "minimum",  "maximum",    "minItems", "maxItems",             "items",
                                         "anyOf",    "const",      "$ref"};
const std::set<std::string> kAnnotations = {"$schema", "$id", "$defs", "description", "title"};
const std::set<std::string> kUnknownKeyPolicies = {"keep", "warn", "warn-drop", "drop"};

void checkDocumentSchema(const json& schema, const json& root, const std::string& where,
                         std::vector<std::string>& problems) {
    if (!schema.is_object()) {
        problems.push_back(where + ": a schema must be an object");
        return;
    }
    for (const auto& [k, v] : schema.items()) {
        if (!kEnforced.contains(k) && !kAnnotations.contains(k) && !k.starts_with("x-avgen-")) {
            problems.push_back(where + ": keyword '" + k + "' is neither enforced by the engine's validator nor an annotation");
        }
    }
    if (const auto t = schema.find("type"); t != schema.end() && !t->is_string()) {
        problems.push_back(where + ": 'type' must be one string (the validator reads no type lists)");
    }
    if (const auto a = schema.find("additionalProperties"); a != schema.end() && !a->is_boolean()) {
        problems.push_back(where + ": 'additionalProperties' must be a boolean");
    }
    if (const auto u = schema.find("x-avgen-unknownKeys"); u != schema.end()) {
        if (!u->is_string() || !kUnknownKeyPolicies.contains(u->get<std::string>())) {
            problems.push_back(where + ": x-avgen-unknownKeys must be keep, warn, warn-drop or drop");
        }
    }
    if (const auto r = schema.find("$ref"); r != schema.end()) {
        const std::string target = r->is_string() ? r->get<std::string>() : std::string{};
        if (!target.starts_with("#/$defs/") || !root.contains(json::json_pointer(target.substr(1)))) {
            problems.push_back(where + ": $ref '" + target + "' does not resolve to a local definition");
        }
    }
    if (const auto p = schema.find("properties"); p != schema.end()) {
        for (const auto& [k, v] : p->items()) {
            checkDocumentSchema(v, root, where + "/properties/" + k, problems);
        }
        if (const auto req = schema.find("required"); req != schema.end()) {
            for (const json& name : *req) {
                if (!p->contains(name.get<std::string>())) {
                    problems.push_back(where + ": required '" + name.get<std::string>() + "' is not a property");
                }
            }
        }
    }
    if (const auto i = schema.find("items"); i != schema.end()) {
        checkDocumentSchema(*i, root, where + "/items", problems);
    }
    if (const auto any = schema.find("anyOf"); any != schema.end()) {
        if (!any->is_array() || any->empty()) {
            problems.push_back(where + ": anyOf must be a non-empty array");
        } else {
            for (std::size_t n = 0; n < any->size(); ++n) {
                checkDocumentSchema(any->at(n), root, where + "/anyOf/" + std::to_string(n), problems);
            }
        }
    }
    if (const auto defs = schema.find("$defs"); defs != schema.end()) {
        for (const auto& [k, v] : defs->items()) {
            checkDocumentSchema(v, root, where + "/$defs/" + k, problems);
        }
    }
}

json readJsonFile(const fs::path& path) {
    std::ifstream in(path);
    return json::parse(in, nullptr, false);
}

// Shipped examples, as the existing sweep (test_scene_authored_lights.cpp) finds them: everything
// under examples/ but the generated `_bench/` copies.
std::vector<fs::path> shippedExamples(bool scenes) {
    std::vector<fs::path> out;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(kRoot / "examples")) {
        const std::string name = entry.path().filename().string();
        if (!entry.is_regular_file() || entry.path().extension() != ".json" ||
            entry.path().string().find("/_bench/") != std::string::npos) {
            continue;
        }
        if (name.ends_with(".scene.json") == scenes) {
            out.push_back(entry.path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Shipped documents that legitimately cannot validate, with the reason. Empty: every shipped
// example validates. A schema stricter than the parser is a schema bug, so an entry here needs a
// reason that is not "the schema says no".
const std::map<std::string, std::string> kSkipped = {};

} // namespace

TEST_CASE("D1: both document schemas are generated artefacts, so T1 compares them byte for byte", "[api][schema]") {
    std::set<std::string> paths;
    for (const api::Artefact& a : api::generatedArtefacts(api::makeCatalogueRegistry())) {
        paths.insert(a.relativePath.generic_string());
    }
    CHECK(paths.contains("schemas/avgen-project.schema.json"));
    CHECK(paths.contains("schemas/avgen-scene.schema.json"));
    const json project = api::projectSchema();
    const json scene = api::sceneSchema();
    CHECK(project.at("$schema") == "https://json-schema.org/draft/2020-12/schema");
    CHECK(project.at("$id") == "avgen-project.schema.json");
    CHECK(scene.at("$id") == "avgen-scene.schema.json");
    // Unknown top-level keys are kept by design (ADR-2132, ADR-2151), so neither root closes.
    CHECK(project.at("additionalProperties") == true);
    CHECK(scene.at("additionalProperties") == true);
    CHECK(scene.at("x-avgen-unknownKeys") == "warn");
    CHECK(project.at("x-avgen-unknownKeys") == "keep");
    for (const char* def : {"assetRef", "route", "chain", "assets", "render", "node", "procedural"}) {
        INFO(def);
        CHECK(project.at("$defs").contains(def));
    }
    for (const char* def : {"camera", "environment", "sky", "light", "lightRig", "rigLight", "post", "node"}) {
        INFO(def);
        CHECK(scene.at("$defs").contains(def));
    }
}

TEST_CASE("The document schemas use only keywords the engine's validator enforces, plus annotations",
          "[api][schema]") {
    for (const json& schema : {api::projectSchema(), api::sceneSchema()}) {
        std::vector<std::string> problems;
        checkDocumentSchema(schema, schema, schema.at("$id").get<std::string>(), problems);
        for (const std::string& p : problems) {
            UNSCOPED_INFO(p);
        }
        CHECK(problems.empty());
    }
}

TEST_CASE("D2: every parser key has a typing entry, and every typing entry names a parser key", "[api][schema]") {
    for (const api::DocumentTyping& typing : {api::projectTyping(), api::sceneTyping()}) {
        INFO(typing.tableFile);
        const std::vector<std::string> problems = bijectionProblems(typing);
        for (const std::string& p : problems) {
            UNSCOPED_INFO(p);
        }
        CHECK(problems.empty());
        // The generated schema carries every listed key at the object the list describes.
        CHECK_FALSE(typing.keyLists.empty());
    }
    // The root lists really are the parsers' own lists, not copies.
    CHECK(api::sceneTyping().keyLists.front().keys.size() == avgen::scene::sceneFileKeys().size());
    CHECK(api::projectTyping().keyLists.front().keys.size() == avgen::app::Engine::projectFileKeys().size());
    for (const std::string_view key : avgen::app::Engine::projectFileKeys()) {
        INFO(key);
        CHECK(api::projectSchema().at("properties").contains(std::string(key)));
    }
    for (const std::string_view key : avgen::scene::sceneEnvironmentKeys()) {
        INFO(key);
        CHECK(api::sceneSchema().at("$defs").at("environment").at("properties").contains(std::string(key)));
    }
}

TEST_CASE("D2: a key added to kSceneKeys without a typing entry fails, naming the key and the file", "[api][schema]") {
    api::DocumentTyping typing = api::sceneTyping();
    REQUIRE(bijectionProblems(typing).empty());
    typing.keyLists.front().keys.push_back("newShowcaseBlock");
    const std::vector<std::string> problems = bijectionProblems(typing);
    REQUIRE(problems.size() == 1);
    CHECK(problems.front().find("'newShowcaseBlock'") != std::string::npos);
    CHECK(problems.front().find("kSceneKeys") != std::string::npos);
    CHECK(problems.front().find("src/api/schema_types_scene.cpp") != std::string::npos);

    api::DocumentTyping stale = api::sceneTyping();
    stale.entries.push_back({"/environment/removedKey", {{"type", "number"}}, true, {}});
    REQUIRE(bijectionProblems(stale).size() == 1);
    CHECK(bijectionProblems(stale).front().find("names no key") != std::string::npos);
}

TEST_CASE("D3: every shipped scene and project validates against its schema", "[api][schema][examples]") {
    const json sceneSchema = api::sceneSchema();
    const json projectSchema = api::projectSchema();
    int scenes = 0;
    int projects = 0;
    int skipped = 0;
    std::vector<std::string> failures;
    const auto check = [&](const fs::path& path, const json& schema) {
        const std::string rel = fs::relative(path, kRoot).generic_string();
        if (kSkipped.contains(rel)) {
            ++skipped;
            return;
        }
        const json doc = readJsonFile(path);
        if (auto e = ai::validateAgainstSchema(schema, doc, "document")) {
            failures.push_back(rel + ": " + e->message);
        }
    };
    for (const fs::path& p : shippedExamples(true)) {
        ++scenes;
        check(p, sceneSchema);
    }
    for (const fs::path& p : shippedExamples(false)) {
        const json doc = readJsonFile(p);
        // Only project documents: examples/ also holds rigs, profiles, graphs and other formats.
        if (doc.is_discarded() || !doc.is_object() || doc.value("format", std::string{}) != "avgen-project") {
            continue;
        }
        ++projects;
        check(p, projectSchema);
    }
    for (const std::string& f : failures) {
        UNSCOPED_INFO(f);
    }
    INFO(scenes << " scenes, " << projects << " projects, " << skipped << " skipped");
    CHECK(failures.empty());
    // Floors, as the key-list sweep has: a glob that silently matched nothing would pass.
    CHECK(scenes >= 250);
    CHECK(projects >= 200);
    CHECK(static_cast<std::size_t>(skipped) == kSkipped.size());
}

TEST_CASE("D4: the keys a save does not write back are exactly ADR-2151's documented normalisations",
          "[api][schema][adr2151]") {
    // ADR-2151, "Documented normalisations" and its reported table, by the pointer each concerns. A
    // value-level normalisation (a default omitted, a name sanitised, float precision, rotation
    // spelling) is not a key, so it is not here: the key is written back, in another spelling.
    const std::set<std::string> documented = {
        "/lightRig",             // written as environment.lightRig
        "/lights",               // an empty list is written as absent
        "/entities",             // an empty list is written as absent
        "/heroes",               // an empty list is written as absent
        "/environment/vortex",   // migrated to an effect (ADR-441)
        "/nodes/*/<unknown>",    // a node key the parser does not read is dropped (reported)
        "/nodes/*/world/biomes", // an empty list is written as absent (reported, world_map.cpp)
    };
    std::set<std::string> notWrittenBack;
    for (const api::TypingEntry& e : api::sceneTyping().entries) {
        if (!e.writtenBack) {
            notWrittenBack.insert(e.pointer);
        }
    }
    CHECK(notWrittenBack == documented);
    // Projects: every listed key is this build's to write or to leave out, and the rest are kept (ADR-2132).
    for (const api::TypingEntry& e : api::projectTyping().entries) {
        INFO(e.pointer);
        CHECK(e.writtenBack);
    }
    // And the published schema says so where a client reads it.
    const json published = api::sceneSchema().at("x-avgen-notWrittenBack");
    CHECK(published.size() == documented.size());
    CHECK(api::sceneSchema().at("properties").at("lightRig").at("x-avgen-writtenBack") == false);
}

TEST_CASE("C1: capability.list's features are the generated list, and every node kind in it parses",
          "[api][schema][capabilities]") {
    avgen::app::Engine engine(avgen::app::EngineMode::Offline);
    const ai::ToolRegistry registry = api::makeCatalogueRegistry();
    const json doc = ai::capabilityDocument(registry, engine);
    const std::vector<std::string> features = api::featureList();
    REQUIRE(doc.contains("features"));
    CHECK(doc.at("features").get<std::vector<std::string>>() == features);
    CHECK(std::set<std::string>(features.begin(), features.end()).size() == features.size());

    // Every node kind loads as a one-node scene, or is named here with what it needs.
    const std::map<std::string, std::string> needsInput = {
        {"gltf", "a glTF asset; a node naming none is skipped (and kept, ADR-2151)"},
        {"scene", "a nested scene file"},
        {"spline", "at least two control points in its `spline` block"},
    };
    avgen::assets::AssetRegistry assets;
    assets.setBaseDirectory(fs::temp_directory_path());
    int kinds = 0;
    for (const std::string& f : features) {
        if (!f.starts_with("node.")) {
            continue;
        }
        ++kinds;
        const std::string kind = f.substr(5);
        INFO(kind);
        const json scene{{"format", "avgen-scene"},
                         {"version", 1},
                         {"nodes", json::array({json{{"name", "one"}, {"kind", kind}}})}};
        auto comp = avgen::scene::Composition::fromJson(scene, assets);
        if (needsInput.contains(kind)) {
            continue;
        }
        REQUIRE(comp.has_value());
        CHECK((*comp)->nodes().size() == 1);
    }
    CHECK(kinds == static_cast<int>(avgen::scene::allNodeKinds().size()));

    // The three asset features name keys the project schema types.
    const json project = api::projectSchema();
    for (const std::string& f : features) {
        if (!f.starts_with("assets.")) {
            continue;
        }
        const std::string key = f.substr(7);
        INFO(f);
        CHECK((project.at("$defs").at("assets").at("properties").contains(key) ||
               project.at("properties").contains(key)));
    }
    // Every group the plan names is present.
    for (const char* prefix : {"scene.", "scene.environment.", "node.", "effect.", "render.", "aov.", "assets."}) {
        INFO(prefix);
        CHECK(std::any_of(features.begin(), features.end(), [&](const std::string& f) { return f.starts_with(prefix); }));
    }
    CHECK(std::find(features.begin(), features.end(), "scene.format") == features.end());
    CHECK(std::find(features.begin(), features.end(), "render.video") != features.end());
}

TEST_CASE("C3: authored 'not exposed' prose names no tool that exists, and no claim known to be stale",
          "[api][schema][capabilities]") {
    const ai::ToolRegistry registry = api::makeCatalogueRegistry();
    // Claims that were true once and are not now (ADR-2152): authored lights register parameters,
    // and the scene domain creates and deletes nodes inside a rollback that captures the composition.
    const std::vector<std::string> stale = {"register parameters the way a light rig", "node creation and deletion can be added",
                                            "would survive a rollback"};
    for (const ai::DomainCapability& d : ai::domainCapabilities()) {
        INFO(d.id);
        for (const ai::Tool* t : registry.all()) {
            INFO(t->definition.name);
            CHECK(d.unavailableReason.find(t->definition.name) == std::string::npos);
        }
        for (const std::string& phrase : stale) {
            INFO(phrase);
            CHECK(d.unavailableReason.find(phrase) == std::string::npos);
            CHECK(d.description.find(phrase) == std::string::npos);
        }
    }
}

TEST_CASE("Every post key the schema lists is one applyPostJson reads, with the type it requires", "[api][schema]") {
    avgen::params::ParameterSet params;
    const avgen::scene::PostParameters post = avgen::scene::registerPostParameters(params, avgen::scene::PostSettings{});
    const std::vector<avgen::scene::PostFileKey> keys = avgen::scene::postFileKeys();
    CHECK(keys.size() > 60);
    for (const avgen::scene::PostFileKey& k : keys) {
        const std::string key(k.key);
        INFO(key);
        json good;
        json bad = "text";
        switch (k.type) {
        case avgen::scene::PostKeyType::Number: good = 0.5; break;
        case avgen::scene::PostKeyType::Boolean: good = true; break;
        case avgen::scene::PostKeyType::Vec3: good = json::array({0.5, 0.5, 0.5}); break;
        case avgen::scene::PostKeyType::Vec2: good = json::array({0.5, 0.5}); break;
        case avgen::scene::PostKeyType::UnsignedInteger: good = 1u; break; // a file's 1 parses unsigned
        }
        CHECK(avgen::scene::applyPostJson(json{{key, good}}, post).has_value());
        CHECK_FALSE(avgen::scene::applyPostJson(json{{key, bad}}, post).has_value());
    }
}

TEST_CASE("The validator's anyOf, const and local $ref", "[api][schema][validator]") {
    SECTION("const") {
        const json s{{"const", "avgen-scene"}};
        CHECK_FALSE(ai::validateAgainstSchema(s, "avgen-scene"));
        const auto e = ai::validateAgainstSchema(s, "avgen-project", "format");
        REQUIRE(e);
        CHECK(e->code == ai::ToolErrorCode::InvalidArguments);
        CHECK(e->message == "format must be \"avgen-scene\"");
        CHECK(ai::validateAgainstSchema(json{{"const", 1}}, 2));
        CHECK_FALSE(ai::validateAgainstSchema(json{{"const", 1}}, 1));
    }
    SECTION("anyOf") {
        const json s{{"anyOf", json::array({json{{"type", "string"}},
                                            json{{"type", "object"}, {"required", {"path"}}}})}};
        CHECK_FALSE(ai::validateAgainstSchema(s, "audio/track.wav"));
        CHECK_FALSE(ai::validateAgainstSchema(s, json{{"path", "a.wav"}}));
        const auto e = ai::validateAgainstSchema(s, json{{"size", 3}}, "assets.audio");
        REQUIRE(e);
        CHECK(e->code == ai::ToolErrorCode::InvalidArguments);
        CHECK(e->message.find("matches none of the 2 allowed forms") != std::string::npos);
        CHECK(e->message.find("missing required field 'path'") != std::string::npos);
        // A single-form anyOf reports the form's own error, code included.
        const json one{{"anyOf", json::array({json{{"type", "number"}, {"maximum", 1}}})}};
        const auto range = ai::validateAgainstSchema(one, 5, "x");
        REQUIRE(range);
        CHECK(range->code == ai::ToolErrorCode::OutOfRange);
    }
    SECTION("$ref, resolved against the root and applied beside its siblings") {
        const json s{{"type", "object"},
                     {"properties", {{"route", {{"$ref", "#/$defs/route"}, {"description", "one route"}}}}},
                     {"$defs", {{"route", {{"type", "object"}, {"required", {"source"}}}}}}};
        CHECK_FALSE(ai::validateAgainstSchema(s, json{{"route", {{"source", "audio.rms"}}}}));
        const auto e = ai::validateAgainstSchema(s, json{{"route", json::object()}});
        REQUIRE(e);
        CHECK(e->message == "arguments.route is missing required field 'source'");
        // Nested: a $ref inside an array inside a $ref.
        const json nested{{"$ref", "#/$defs/list"},
                          {"$defs", {{"list", {{"type", "array"}, {"items", {{"$ref", "#/$defs/n"}}}}},
                                     {"n", {{"type", "number"}}}}}};
        CHECK_FALSE(ai::validateAgainstSchema(nested, json::array({1, 2})));
        CHECK(ai::validateAgainstSchema(nested, json::array({1, "x"})));
        // A reference that does not resolve is the schema's fault, and says so.
        const auto broken = ai::validateAgainstSchema(json{{"$ref", "#/$defs/missing"}}, 1);
        REQUIRE(broken);
        CHECK(broken->code == ai::ToolErrorCode::Internal);
        // A reference to itself stops rather than recursing for ever.
        const json loop{{"$ref", "#/$defs/a"}, {"$defs", {{"a", {{"$ref", "#/$defs/a"}}}}}};
        const auto looped = ai::validateAgainstSchema(loop, 1);
        REQUIRE(looped);
        CHECK(looped->code == ai::ToolErrorCode::Internal);
    }
    SECTION("the existing keywords and messages are unchanged") {
        const json s{{"type", "object"}, {"properties", {{"n", {{"type", "integer"}, {"minimum", 0}}}}}};
        const auto e = ai::validateAgainstSchema(s, json{{"n", -1}});
        REQUIRE(e);
        CHECK(e->code == ai::ToolErrorCode::OutOfRange);
        CHECK(e->message == "arguments.n must be >= 0");
    }
}

TEST_CASE("The engine validates a document against its own schema: a typed error says where", "[api][schema]") {
    const json schema = api::sceneSchema();
    json doc{{"format", "avgen-scene"}, {"version", 1}, {"camera", {{"fov", "wide"}}}};
    auto e = ai::validateAgainstSchema(schema, doc, "scene");
    REQUIRE(e);
    CHECK(e->message == "scene.camera.fov must be a number (got string)");
    doc = {{"format", "avgen-scene"}, {"version", 2}};
    e = ai::validateAgainstSchema(schema, doc, "scene");
    REQUIRE(e);
    CHECK(e->message.find("scene.version must be 1") != std::string::npos);
    // An unknown top-level key is kept, so it is valid; a light with no name is not.
    doc = {{"format", "avgen-scene"}, {"version", 1}, {"_note", "kept"}, {"somebodyElses", 3}};
    CHECK_FALSE(ai::validateAgainstSchema(schema, doc, "scene"));
    doc = {{"format", "avgen-scene"}, {"version", 1}, {"lights", json::array({json{{"type", "point"}}})}};
    e = ai::validateAgainstSchema(schema, doc, "scene");
    REQUIRE(e);
    CHECK(e->message == "scene.lights[0] is missing required field 'name'");
    // A node kind comes from scene::nodeKindName, and a typed kind's block is typed.
    doc = {{"format", "avgen-scene"},
           {"version", 1},
           {"nodes", json::array({json{{"name", "floor"},
                                       {"kind", "procedural"},
                                       {"procedural", {{"source", {{"kind", "box"}, {"size", {10, 0.1}}}}}}}})}};
    e = ai::validateAgainstSchema(schema, doc, "scene");
    REQUIRE(e);
    CHECK(e->message.find("size") != std::string::npos);
    doc["nodes"][0]["procedural"]["source"]["size"] = {10, 0.1, 10};
    CHECK_FALSE(ai::validateAgainstSchema(schema, doc, "scene"));
    doc["nodes"][0]["kind"] = "spaceship";
    CHECK(ai::validateAgainstSchema(schema, doc, "scene"));
}
