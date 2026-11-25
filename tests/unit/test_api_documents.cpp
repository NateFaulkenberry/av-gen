// ADR-2157 (Engine API stage S4): the document tools. `document.get`, `document.validate`, `document.patch` (RFC 6902
// against the scene document, then a rebuild) and `project.validate` (a private offline engine), all through a real
// `api::Session` over a real `app::Engine`, as the PoC client will drive them.
//
// The owner's regression list for scene editing (spec section 5): the interaction between Composition::toJson,
// Composition::fromJson, Engine::setCompositionJson, parameter overrides, transaction snapshots and rollback. Each
// TEST_CASE below names the arm it covers.

#include "ai/tool_api.hpp"
#include "api/describe.hpp"
#include "api/session.hpp"
#include "api/warning_codes.hpp"
#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "support/gltf_fixture.hpp"
#include "support/project_round_trip.hpp"
#include "support/synth.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <unistd.h>

using namespace avgen;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

const fs::path kRoot(AVGEN_SOURCE_DIR);

fs::path freshDir(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("avgen_docs_" + name + "_" + std::to_string(getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return fs::weakly_canonical(dir);
}

std::string code(const api::CallResult& r) {
    if (r.error) {
        UNSCOPED_INFO(r.error->message);
    }
    return r.error ? ai::toolErrorCodeName(r.error->code) : "none";
}

void checkOutput(const api::Session& s, const std::string& tool, const api::CallResult& r) {
    const ai::Tool* t = s.tools().find(tool);
    REQUIRE(t != nullptr);
    REQUIRE_FALSE(t->definition.outputSchema.is_null());
    const auto problem = ai::validateAgainstSchema(t->definition.outputSchema, r.result, tool);
    INFO((problem ? problem->message : std::string{}) << "\n" << r.result.dump(2).substr(0, 4000));
    CHECK_FALSE(problem.has_value());
}

fs::path writeWav(const fs::path& path, float seconds = 2.0f) {
    constexpr std::uint32_t rate = 48000;
    const auto frames = static_cast<std::size_t>(seconds * rate);
    auto mono = testsupport::mix(testsupport::clickTrack(120.0f, rate, frames), testsupport::sine(60.0f, rate, frames, 0.4f));
    auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(mono, 2), 2, rate);
    REQUIRE(file.writeWav(path).has_value());
    return path;
}

// A column with no emissive material: the Blender bug of the evaluation's evidence 05.
fs::path writeColumnGlb(const std::string& name) {
    std::vector<testsupport::GlbMesh> meshes(1);
    meshes[0].node = "Column_Body";
    meshes[0].material = "Body";
    testsupport::appendBox(meshes[0], {-0.5f, 0.0f, -0.5f}, {0.5f, 4.0f, 0.5f});
    return testsupport::writeMeshesGlb(name, meshes);
}

const json kKey = {{"name", "key"},          {"type", "directional"}, {"direction", {-0.4, -0.8, -0.45}},
                   {"color", {1.0, 0.95, 0.9}}, {"intensity", 3.0},      {"castsShadow", true}};
const json kRim = {{"name", "rim"},           {"type", "point"}, {"position", {2.5, 4.0, -2.0}},
                   {"color", {0.4, 0.6, 1.0}}, {"intensity", 40.0}, {"range", 12.0}};
const json kFloor = {{"name", "floor"},
                     {"kind", "procedural"},
                     {"position", {0.0, -0.05, 0.0}},
                     {"procedural",
                      {{"source", {{"kind", "box"}, {"size", {10.0, 0.1, 10.0}}}},
                       {"distribution", {{"kind", "single"}}},
                       {"material", {{"baseColor", {0.08, 0.08, 0.09}}, {"roughness", 0.6}, {"metallic", 0.0}}}}}};

struct Rig {
    fs::path root;
    fs::path read;
    fs::path write;
    app::Engine engine{app::EngineMode::Offline};
    api::LocalHost host{engine};
    std::unique_ptr<api::Session> s;

    explicit Rig(const std::string& name, api::HostPolicy policy = api::HostPolicy::Authoring)
        : root(freshDir(name)), read(root / "exports"), write(root / "work") {
        fs::create_directories(read);
        fs::create_directories(write);
        api::SessionOptions o;
        o.client = api::ClientInfo{api::ClientKind::Test, "test", "1"};
        o.permissions.readRoots = {read};
        o.permissions.writeRoots = {write};
        o.policy = policy;
        auto made = api::Session::create(std::move(o), host);
        REQUIRE(made.has_value());
        s = std::move(*made);
        s->pump();
    }
    ~Rig() {
        s.reset();
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    api::CallResult call(const std::string& tool, const json& args, const api::CallOptions& o = {}) {
        return s->call(tool, args, o);
    }
    api::CallResult ok(const std::string& tool, const json& args, const api::CallOptions& o = {}) {
        auto r = call(tool, args, o);
        INFO(tool << ": " << code(r) << "\n" << args.dump().substr(0, 2000));
        REQUIRE(r.success);
        return r;
    }
    json scene() { return ok("document.get", json{{"document", "scene"}}).result; }
    std::string hash() { return scene().at("hash").get<std::string>(); }
    api::CallResult patch(const json& ops, bool dryRun = false, const api::CallOptions& o = {}) {
        json args{{"document", "scene"}, {"patch", ops}};
        if (dryRun) {
            args["dryRun"] = true;
        }
        return call("document.patch", args, o);
    }
    json param(const std::string& path) {
        const auto r = ok("parameter.get", json{{"paths", {path}}});
        return r.result.at("parameters").at(0).at("value");
    }
    bool hasParam(const std::string& path) { return engine.params().find(path) != nullptr; }

    // PoC steps 1-3: a project with an empty scene, a model imported, a node from its id.
    void column() {
        ok("project.create", json{{"path", "poc"}, {"scene", "empty"}});
        fs::copy_file(writeColumnGlb("docs_column_" + std::to_string(getpid())), read / "column.glb",
                      fs::copy_options::overwrite_existing);
        const auto imported = ok("asset.import", json{{"file", (read / "column.glb").string()}});
        ok("scene.create_node", json{{"name", "column"}, {"asset", imported.result.at("id")}});
    }
    int nodeIndex(const std::string& name) {
        const json doc = scene().at("content");
        const json& nodes = doc.at("nodes");
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].value("name", std::string{}) == name) {
                return static_cast<int>(i);
            }
        }
        FAIL("no node " << name);
        return -1;
    }
};

bool contains(const json& list, const std::string& value) {
    return std::find(list.begin(), list.end(), json(value)) != list.end();
}

} // namespace

// ---- 1 and 7: what a rebuild keeps -------------------------------------------------------------------------------

TEST_CASE("A patch that adds a light registers its parameters and keeps an earlier rotation edit",
          "[api][documents][adr2157]") {
    Rig rig("addlight");
    rig.column();
    rig.ok("parameter.set", json{{"path", "nodes/column/rotation"}, {"value", {0.0, 30.0, 0.0}}});
    REQUIRE_FALSE(rig.hasParam("lights/rim/intensity"));

    const auto r = rig.patch(json::array({{{"op", "add"}, {"path", "/lights"}, {"value", json::array({kRim})}}}));
    INFO(code(r));
    REQUIRE(r.success);
    checkOutput(*rig.s, "document.patch", r);
    CHECK(r.result.at("applied") == true);
    CHECK(contains(r.result.at("parametersAdded"), "lights/rim/intensity"));
    CHECK(r.result.at("hashBefore") != r.result.at("hashAfter"));
    CHECK(r.result.at("hashAfter") == rig.hash());
    CHECK(rig.param("lights/rim/intensity").get<double>() == Catch::Approx(40.0));
    const json rotation = rig.param("nodes/column/rotation");
    CHECK(rotation[1].get<double>() == Catch::Approx(30.0).margin(1e-3));
    // It ran in an implicit transaction (a structural, undoable edit).
    CHECK(r.transaction.has_value());
}

TEST_CASE("Overrides survive a rebuild: kept where the document does not carry them, yielded where the patch set "
          "them, dropped with their node",
          "[api][documents][adr2157]") {
    Rig rig("overrides");
    rig.column();
    REQUIRE(rig.patch(json::array({{{"op", "add"}, {"path", "/nodes/-"}, {"value", kFloor}}})).success);
    REQUIRE(rig.hasParam("post/bloom/intensity"));

    // Overrides of three kinds: one the scene document does not carry (post), two on the column (the node the
    // patch below edits), one on the floor (the node it then removes), plus a route.
    rig.ok("parameter.set", json{{"path", "post/bloom/intensity"}, {"value", 0.6}});
    rig.ok("parameter.set", json{{"path", "nodes/column/rotation"}, {"value", {0.0, 45.0, 0.0}}});
    rig.ok("parameter.set", json{{"path", "nodes/column/position"}, {"value", {1.0, 0.0, 0.0}}});
    rig.ok("parameter.set", json{{"path", "nodes/floor/scale"}, {"value", {2.0, 1.0, 2.0}}});
    const std::size_t defaults = rig.engine.modulator().routes().size(); // the new project's default routes
    rig.ok("modulation.create", json{{"source", "audio.bass"}, {"target", "nodes/column/position"}, {"amount", 0.5}});
    const std::size_t routes = rig.engine.modulator().routes().size();
    REQUIRE(routes == defaults + 1);

    const int column = rig.nodeIndex("column");
    const int floor = rig.nodeIndex("floor");
    // The patch sets the column's tint (registering nodes/column/tint), moves it to x = 3 (the position override
    // must yield: the patch decided the value the document carries), and removes the floor.
    const auto r = rig.patch(json::array({
        {{"op", "add"}, {"path", fmt::format("/nodes/{}/tint", column)}, {"value", {1.0, 0.5, 0.25}}},
        {{"op", "replace"}, {"path", fmt::format("/nodes/{}/position", column)}, {"value", {3.0, 0.0, 0.0}}},
        {{"op", "remove"}, {"path", fmt::format("/nodes/{}", floor)}},
    }));
    INFO(code(r));
    REQUIRE(r.success);
    checkOutput(*rig.s, "document.patch", r);
    const json& o = r.result.at("overrides");
    INFO(o.dump(2));
    CHECK(rig.param("post/bloom/intensity").get<double>() == Catch::Approx(0.6));
    CHECK(contains(o.at("kept"), "post/bloom/intensity"));
    CHECK(rig.param("nodes/column/rotation")[1].get<double>() == Catch::Approx(45.0).margin(1e-3));
    CHECK(rig.param("nodes/column/position")[0].get<double>() == Catch::Approx(3.0));
    CHECK(contains(o.at("yielded"), "nodes/column/position"));
    CHECK(contains(o.at("dropped"), "nodes/floor/scale"));
    CHECK(contains(r.result.at("parametersAdded"), "nodes/column/tint"));
    CHECK(contains(r.result.at("parametersRemoved"), "nodes/floor/scale"));
    CHECK(rig.param("nodes/column/tint")[1].get<double>() == Catch::Approx(0.5));
    // The route came back, bound to the rebuilt parameter.
    REQUIRE(rig.engine.modulator().routes().size() == routes);
    CHECK(rig.engine.modulator().routes().back().target == "nodes/column/position");
    // The post override is still an override for the next project save (the basis was not retaken).
    const json project = rig.ok("document.get", json{{"document", "project"}}).result.at("content");
    CHECK(project.at("parameters").contains("post/bloom/intensity"));
}

// ---- 2 and 3: what is refused, and that a refusal changes nothing -----------------------------------------------

TEST_CASE("A patch that does not apply, fails the schema or fails the parse leaves the scene identical",
          "[api][documents][adr2157]") {
    Rig rig("invalid");
    rig.column();
    const std::string before = rig.hash();
    const scene::Composition* comp = rig.engine.composition();

    SECTION("an operation that does not apply") {
        const auto r = rig.patch(json::array({{{"op", "replace"}, {"path", "/nodes/9/tint"}, {"value", {1, 1, 1}}}}));
        CHECK(code(r) == "INVALID_ARGUMENTS");
        REQUIRE(r.error);
        CHECK(r.error->details.at("pointer") == "/nodes/9/tint");
    }
    SECTION("a schema error the document did not have") {
        const auto r = rig.patch(json::array({{{"op", "add"}, {"path", "/nodes/0/tint"}, {"value", "red"}}}));
        CHECK(code(r) == "INVALID_ARGUMENTS");
        REQUIRE(r.error);
        CHECK(r.error->details.at("pointer") == "/nodes/0/tint");
        CHECK(r.error->details.at("findings").at(0).at("code") == "schema.type");
    }
    SECTION("a document the parser refuses though the schema allows it") {
        // `effects` is untyped in the schema; the parser refuses an entry that is not an object. Inside an explicit
        // transaction, so nothing rolls back on the call's behalf: the patch itself must leave the old composition.
        const auto begun = rig.ok("transaction.begin", json{{"label", "t"}});
        api::CallOptions in;
        in.transaction = begun.result.at("id").get<std::string>();
        const auto r = rig.patch(json::array({{{"op", "add"}, {"path", "/effects"}, {"value", json::array({42})}}}), false, in);
        CHECK(code(r) == "INVALID_ARGUMENTS");
        REQUIRE(r.error);
        CHECK(r.error->details.at("findings").at(0).at("code") == "scene.parse");
        CHECK(rig.engine.composition() == comp); // the old composition is still the installed one, not a rebuild
        CHECK(rig.hash() == before);
        rig.ok("transaction.commit", json{{"id", *in.transaction}});
    }
    // (An implicit transaction rolls a failed call back by reinstalling its snapshot, so outside one the
    // composition object may be a new one; its document is what must not change.)
    CHECK(rig.hash() == before);
}

TEST_CASE("An unknown key is refused with its pointer; a note and an existing unknown key are not",
          "[api][documents][adr2157]") {
    Rig rig("unknown");
    rig.column();
    const std::string before = rig.hash();
    for (const auto& [path, value, pointer] : std::vector<std::tuple<std::string, json, std::string>>{
             {"/nodes/0/tnit", json::array({1, 1, 1}), "/nodes/0/tnit"},
             {"/lights", json::array({json{{"name", "k"}, {"type", "point"}, {"intensty", 3.0}}}), "/lights/0/intensty"},
             {"/camera/fieldOfView", 40, "/camera/fieldOfView"},
             {"/nodes/-",
              json{{"name", "f"}, {"kind", "procedural"}, {"procedural", {{"source", {{"kind", "box"}, {"sise", {1, 1, 1}}}}}}},
              "/nodes/1/procedural/source/sise"},
             {"/futureBlock", json{{"a", 1}}, "/futureBlock"},
         }) {
        INFO(path);
        const auto r = rig.patch(json::array({{{"op", "add"}, {"path", path}, {"value", value}}}));
        CHECK(code(r) == "INVALID_ARGUMENTS");
        REQUIRE(r.error);
        CHECK(r.error->details.at("pointer") == pointer);
        CHECK(r.error->details.at("findings").at(0).at("code") == "scene.unknownKey");
        CHECK(rig.hash() == before);
    }
    // A `_` note is always allowed, on a node and at the top level.
    REQUIRE(rig.patch(json::array({{{"op", "add"}, {"path", "/nodes/0/_note"}, {"value", "kept"}},
                                   {{"op", "add"}, {"path", "/_note"}, {"value", "top"}}}))
                .success);
    const json doc = rig.scene().at("content");
    CHECK(doc.at("_note") == "top");
    CHECK(doc.at("nodes").at(0).at("_note") == "kept");
}

// ---- 4 and 5: transactions and dry runs -------------------------------------------------------------------------

TEST_CASE("transaction.rollback restores the pre-patch document, its parameters and its routes",
          "[api][documents][adr2157]") {
    Rig rig("rollback");
    rig.column();
    rig.ok("parameter.set", json{{"path", "nodes/column/rotation"}, {"value", {0.0, 20.0, 0.0}}});
    rig.ok("modulation.create", json{{"source", "audio.bass"}, {"target", "nodes/column/emissiveBoost"}, {"amount", 2.0}});
    const std::size_t routes = rig.engine.modulator().routes().size();
    const json before = rig.scene();

    const auto begun = rig.ok("transaction.begin", json{{"label", "patch"}});
    api::CallOptions in;
    in.transaction = begun.result.at("id").get<std::string>();
    const auto r = rig.patch(json::array({{{"op", "add"}, {"path", "/lights"}, {"value", json::array({kKey, kRim})}},
                                          {{"op", "add"}, {"path", "/nodes/-"}, {"value", kFloor}}}),
                             false, in);
    INFO(code(r));
    REQUIRE(r.success);
    CHECK(r.transaction == in.transaction);
    CHECK(rig.hasParam("lights/key/intensity"));
    CHECK(rig.hash() != before.at("hash").get<std::string>());

    rig.ok("transaction.rollback", json{{"id", *in.transaction}});
    const json after = rig.scene();
    CHECK(after.at("hash") == before.at("hash"));
    CHECK(after.at("content") == before.at("content"));
    CHECK_FALSE(rig.hasParam("lights/key/intensity"));
    CHECK_FALSE(rig.hasParam("procedural/floor/material/baseColor"));
    CHECK(rig.param("nodes/column/rotation")[1].get<double>() == Catch::Approx(20.0).margin(1e-3));
    REQUIRE(rig.engine.modulator().routes().size() == routes);
    CHECK(rig.engine.modulator().routes().back().target == "nodes/column/emissiveBoost");
}

TEST_CASE("A dry run predicts and changes nothing", "[api][documents][adr2157]") {
    Rig rig("dryrun");
    rig.column();
    const json before = rig.scene();
    const auto r = rig.patch(json::array({{{"op", "add"}, {"path", "/lights"}, {"value", json::array({kRim})}}}), true);
    INFO(code(r));
    REQUIRE(r.success);
    checkOutput(*rig.s, "document.patch", r);
    CHECK(r.result.at("dryRun") == true);
    CHECK(r.result.at("applied") == false);
    CHECK(r.result.at("hashAfter") == before.at("hash"));
    CHECK(r.result.at("predictedHash") != before.at("hash"));
    CHECK(contains(r.result.at("parametersAdded"), "lights/rim/intensity"));
    CHECK(r.changes.empty());
    CHECK(rig.scene() == before);
    CHECK_FALSE(rig.hasParam("lights/rim/intensity"));
    // A dry run that the parser would refuse is refused the same way.
    const auto bad = rig.patch(json::array({{{"op", "add"}, {"path", "/effects"}, {"value", json::array({42})}}}), true);
    CHECK(code(bad) == "INVALID_ARGUMENTS");
    CHECK(rig.scene() == before);
}

TEST_CASE("A live host refuses document.patch during a show, dry run or not", "[api][documents][adr2157][live]") {
    Rig rig("live", api::HostPolicy::Live);
    rig.column();
    bool show = true;
    rig.host.setShowPredicate([&] { return show; });
    const auto r = rig.patch(json::array({{{"op", "add"}, {"path", "/lights"}, {"value", json::array({kRim})}}}));
    CHECK(code(r) == "BUSY");
    CHECK(code(rig.call("document.get", json{{"document", "scene"}})) == "none");
    show = false;
    CHECK(rig.patch(json::array({{{"op", "add"}, {"path", "/lights"}, {"value", json::array({kRim})}}})).success);
}

// ---- 6: forward-compatible data -------------------------------------------------------------------------------------

TEST_CASE("Unrelated fields, notes, unknown top-level keys and looks survive a patch", "[api][documents][adr2157]") {
    Rig rig("survive");
    rig.column();
    // The scene as a file might carry it: install through the same path a rollback uses.
    json doc = rig.engine.composition()->toJson();
    doc["_note"] = "hand-written; keep me";
    doc["futureBlock"] = json{{"mode", "spiral"}, {"layers", json::array({1, 2, 3})}};
    doc["futureScalar"] = 7;
    doc["looks"] = json::array({json{{"name", "dusk"}, {"from", 1}, {"params", {{"nodes/column/visible", true}}}}});
    doc["nodes"][0]["_note"] = "the column";
    doc["environment"]["intensity"] = 0.25;
    REQUIRE(rig.engine.setCompositionJson(doc).has_value());
    const json before = rig.scene().at("content");
    REQUIRE(before.contains("futureBlock"));
    REQUIRE(before.contains("looks"));

    const auto r = rig.patch(json::array({{{"op", "add"}, {"path", "/lights"}, {"value", json::array({kRim})}},
                                          {{"op", "add"}, {"path", "/nodes/0/tint"}, {"value", {1.0, 0.5, 0.5}}}}));
    INFO(code(r));
    REQUIRE(r.success);
    const json after = rig.scene().at("content");
    for (const char* key : {"_note", "futureBlock", "futureScalar", "looks", "camera", "environment", "name"}) {
        INFO(key);
        CHECK(after.at(key) == before.at(key));
    }
    CHECK(after.at("nodes").at(0).at("_note") == "the column");
    CHECK(after.at("nodes").at(0).at("asset") == before.at("nodes").at(0).at("asset"));
    // The change list names what the patch did, not the untouched blocks.
    for (const json& c : r.result.at("changed")) {
        const std::string path = c.at("path").get<std::string>();
        INFO(path);
        CHECK((path.starts_with("/lights") || path.starts_with("/nodes/0/tint")));
    }
}

// ---- ADR-2140: the proc-params cache across a rebuild ---------------------------------------------------------------

TEST_CASE("A patched procedural's parameters are the ones its applied geometry reads", "[api][documents][adr2140]") {
    Rig rig("procparams");
    rig.column();
    REQUIRE(rig.patch(json::array({{{"op", "add"}, {"path", "/nodes/-"}, {"value", kFloor}}})).success);
    const auto applied = [&]() -> const scene::ProceduralGeometry& {
        testsupport::stepFrames(rig.engine, 2);
        const auto& procedurals = rig.engine.scene().procedurals;
        const auto it = std::find_if(procedurals.begin(), procedurals.end(),
                                     [](const scene::ProceduralGeometry& g) { return g.name == "floor"; });
        REQUIRE(it != procedurals.end());
        return *it;
    };
    CHECK(applied().material.baseColor.r == Catch::Approx(0.08f));

    // The patch rebuilds every procedural's parameters; the warm cache from the frames above must not be read.
    const int floor = rig.nodeIndex("floor");
    const auto r = rig.patch(json::array(
        {{{"op", "replace"}, {"path", fmt::format("/nodes/{}/procedural/material/baseColor", floor)}, {"value", {0.5, 0.25, 0.125}}}}));
    INFO(code(r));
    REQUIRE(r.success);
    CHECK(rig.param("procedural/floor/material/baseColor")[0].get<double>() == Catch::Approx(0.5));
    CHECK(applied().material.baseColor.r == Catch::Approx(0.5f));
    CHECK(applied().material.baseColor.b == Catch::Approx(0.125f));
    // A set after the rebuild reaches the geometry through the new parameters.
    rig.ok("parameter.set", json{{"path", "procedural/floor/material/baseColor"}, {"value", {0.75, 0.75, 0.75}}});
    CHECK(applied().material.baseColor.g == Catch::Approx(0.75f));
    // And another rebuild (an unrelated patch) keeps that set as an override.
    REQUIRE(rig.patch(json::array({{{"op", "add"}, {"path", "/lights"}, {"value", json::array({kKey})}}})).success);
    CHECK(applied().material.baseColor.g == Catch::Approx(0.75f));
}

// ---- document.get and document.validate ---------------------------------------------------------------------------

TEST_CASE("document.get returns both documents with a stable hash; document.validate reports structured findings",
          "[api][documents][adr2157]") {
    Rig rig("getvalidate");
    CHECK(code(rig.call("document.get", json{{"document", "scene"}})) == "FAILED_PRECONDITION");
    rig.column();
    const auto scene = rig.ok("document.get", json{{"document", "scene"}});
    checkOutput(*rig.s, "document.get", scene);
    CHECK(scene.result.at("hash") == rig.hash());
    CHECK(scene.result.at("content").at("format") == "avgen-scene");
    const auto project = rig.ok("document.get", json{{"document", "project"}});
    checkOutput(*rig.s, "document.get", project);
    CHECK(project.result.at("content").at("format") == "avgen-project");

    json doc = scene.result.at("content");
    const auto good = rig.ok("document.validate", json{{"document", "scene"}, {"content", doc}});
    checkOutput(*rig.s, "document.validate", good);
    CHECK(good.result.at("valid") == true);
    CHECK(good.result.at("findings").empty());

    doc["nodes"][0]["tint"] = "red";
    doc["nodes"][0]["tnit"] = 1;
    doc["nodes"].push_back(json{{"name", "ghost"}, {"kind", "gltf"}, {"asset", "asset://project/models/ghost.glb"}});
    doc["version"] = 2;
    const auto bad = rig.ok("document.validate", json{{"document", "scene"}, {"content", doc}});
    checkOutput(*rig.s, "document.validate", bad);
    CHECK(bad.result.at("valid") == false);
    std::set<std::string> found;
    for (const json& f : bad.result.at("findings")) {
        found.insert(f.at("code").get<std::string>() + " " + f.at("pointer").get<std::string>());
    }
    INFO(bad.result.at("findings").dump(2));
    CHECK(found.contains("schema.type /nodes/0/tint"));
    CHECK(found.contains("schema.invalid /version"));
    CHECK(found.contains("scene.unknownKey /nodes/0/tnit"));
    CHECK(found.contains("asset.missing /nodes/1/asset"));
    CHECK(found.contains("scene.parse "));
    // Nothing was installed.
    CHECK(rig.hash() == scene.result.at("hash").get<std::string>());

    const auto proj = rig.ok("document.validate", json{{"document", "project"}, {"content", project.result.at("content")}});
    checkOutput(*rig.s, "document.validate", proj);
    INFO(proj.result.at("findings").dump(2));
    CHECK(proj.result.at("valid") == true);
}

// ---- 8: project.validate reproduces evidence 05 ---------------------------------------------------------------------

TEST_CASE("project.validate on the evaluation's column project reports route.dead when the model emits nothing",
          "[api][documents][adr2157]") {
    // The evaluation's poc-today folder holds only the FIXED column.glb (the Blender script was corrected and the
    // asset re-exported), so the bug is rebuilt here: the same project and scene, a column with no emissive material.
    Rig rig("validate");
    const fs::path dir = rig.read / "column";
    fs::create_directories(dir / "assets");
    writeWav(dir / "assets" / "track.wav");
    const auto writeProject = [&](const fs::path& glb) {
        fs::copy_file(glb, dir / "assets" / "column.glb", fs::copy_options::overwrite_existing);
        std::ofstream(dir / "column.scene.json")
            << json{{"format", "avgen-scene"},
                    {"version", 1},
                    {"name", "column"},
                    {"camera", {{"mode", 1}, {"position", {0.0, 2.6, 9.0}}, {"target", {0.0, 2.0, 0.0}}, {"fov", 40.0}}},
                    {"lights", json::array({kKey, kRim})},
                    {"nodes", json::array({kFloor, json{{"name", "column"}, {"kind", "gltf"}, {"asset", "assets/column.glb"}}})}}
                   .dump(2);
        std::ofstream(dir / "column.json")
            << json{{"format", "avgen-project"},
                    {"version", 4},
                    {"assets", {{"audio", {{"path", "assets/track.wav"}}}, {"scene", {{"kind", "composition"}, {"path", "column.scene.json"}}}}},
                    {"parameters", {{"post/bloom/enabled", true}, {"post/bloom/intensity", 0.6}}},
                    {"routes", json::array({json{{"source", "audio.bass"},
                                                 {"target", "nodes/column/emissiveBoost"},
                                                 {"amount", 6.0},
                                                 {"op", "add"},
                                                 {"polarity", "unipolar"},
                                                 {"chain", {{"attackMs", 20}, {"decayMs", 250}}}}})}}
                   .dump(2);
    };
    const auto deadRoutes = [](const json& report) {
        std::vector<json> out;
        for (const json& f : report.at("findings")) {
            if (f.at("code") == "route.dead") {
                out.push_back(f);
            }
        }
        return out;
    };

    writeProject(writeColumnGlb("docs_validate_dull"));
    const auto bug = rig.ok("project.validate", json{{"path", (dir / "column.json").string()}});
    checkOutput(*rig.s, "project.validate", bug);
    INFO(bug.result.dump(2));
    const auto dead = deadRoutes(bug.result);
    REQUIRE(dead.size() == 1);
    CHECK(dead[0].at("pointer") == "/routes/0");
    CHECK(dead[0].at("severity") == "warning");
    CHECK(dead[0].at("message").get<std::string>().find("node-emits-nothing") != std::string::npos);
    CHECK(bug.result.at("counts").at("error") == 0);
    // The session's own project is untouched: validate opened nothing here.
    CHECK(rig.engine.projectPath().empty());

    // The fixed model (an emissive material) makes the same route live: the finding is the bug, not the fixture.
    writeProject(testsupport::writeTriangleGlb("docs_validate_lit"));
    const auto fixed = rig.ok("project.validate", json{{"path", (dir / "column.json").string()}});
    INFO(fixed.result.dump(2));
    CHECK(deadRoutes(fixed.result).empty());
    CHECK(fixed.result.at("valid") == true);

    // A missing audio file, an unknown parameter path and a misspelt project key, each with its code.
    json project = json::parse(std::ifstream(dir / "column.json"));
    project["assets"]["audio"]["path"] = "assets/gone.wav";
    project["parameters"]["nodes/nothing/scale"] = 2.0;
    project["rotues"] = json::array();
    std::ofstream(dir / "column.json") << project.dump(2);
    const auto broken = rig.ok("project.validate", json{{"path", (dir / "column.json").string()}});
    INFO(broken.result.dump(2));
    std::set<std::string> found;
    for (const json& f : broken.result.at("findings")) {
        found.insert(f.at("code").get<std::string>() + " " + f.at("pointer").get<std::string>());
    }
    CHECK(found.contains("asset.missing /assets/audio"));
    CHECK(found.contains("parameter.unknown /parameters/nodes~1nothing~1scale"));
    CHECK(found.contains("project.unknownKey /rotues"));
    CHECK(broken.result.at("valid") == false);

    // Outside the session's roots: refused, not read.
    CHECK(code(rig.call("project.validate", json{{"path", "/etc/hosts"}})) == "PERMISSION_DENIED");
}

TEST_CASE("project.validate with no path validates the session's project as it is in memory",
          "[api][documents][adr2157]") {
    Rig rig("inmemory");
    rig.column();
    REQUIRE(rig.patch(json::array({{{"op", "add"}, {"path", "/lights"}, {"value", json::array({kKey, kRim})}}})).success);
    const auto r = rig.ok("project.validate", json::object());
    checkOutput(*rig.s, "project.validate", r);
    INFO(r.result.dump(2));
    CHECK(r.result.at("file") == "(session)");
    CHECK(r.result.at("valid") == true);
    // The in-memory edit is what was validated: the scratch copy carries the patched lights.
    CHECK(r.result.at("loaded").at("parameters").get<int>() > 0);
    // Nothing was written beside the project.
    for (const auto& e : fs::directory_iterator(rig.write / "poc")) {
        const std::string name = e.path().filename().string();
        INFO(name);
        CHECK((name == "poc.json" || name == "poc.scene.json" || name == "assets"));
    }
}

// ---- 9: D5, every shipped example project ---------------------------------------------------------------------------

// D5's explicit list: shipped projects whose gitignored assets (commercial or third-party models, the owner's audio)
// are not in a fresh checkout. On a machine that has them they validate clean; without them their only errors may
// be asset.missing / scene.missing. Anything else is a failure, and so is a missing asset in a project not listed.
// Measured 2026-11-25 on a worktree with the asset links (tools/link-worktree-assets.sh): every entry's missing file
// is gitignored -- a generated scene (`scene/`, the build scripts' output) or the CHRONOS souls.
const std::set<std::string> kMissingAssetsAllowed = {
    "examples/aurora-borealis/aurora-borealis.json",          // scene/aurora-borealis.scene.json, generated
    "examples/bioluminescent/rift-live.json",                 // rift.scene.json, generated
    "examples/bioluminescent/rift.json",                      // rift.scene.json, generated
    "examples/chronos/singularity/singularity-b.json",        // assets/chronos/build/souls/*.glb
    "examples/chronos/singularity/singularity-c.json",
    "examples/chronos/singularity/singularity-d.json",
    "examples/chronos/singularity/singularity-e.json",
    "examples/chronos/singularity/singularity-f.json",
    "examples/musical-geography/musical-geography-live.json", // scene/release-live.scene.json, generated
    "examples/musical-geography/musical-geography.json",      // scene/release-offline.scene.json, generated
    "examples/rainbow-world/rainbow-world-offline.json",      // scene/rainbow-world-offline.scene.json, generated
    "examples/rainbow-world/rainbow-world.json",              // scene/rainbow-world.scene.json, generated
};

TEST_CASE("D5: project.validate finds no error in any shipped example project", "[api][documents][examples][adr2157]") {
    // Examples whose assets are gitignored (commercial or third-party models, large audio) cannot validate on a
    // machine without them, and the only errors allowed are those `asset.missing` / `scene.missing` findings.
    const auto start = std::chrono::steady_clock::now();
    std::vector<fs::path> projects;
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(kRoot / "examples")) {
        if (!e.is_regular_file() || e.path().extension() != ".json" || e.path().string().find("/_bench/") != std::string::npos ||
            e.path().filename().string().ends_with(".scene.json")) {
            continue;
        }
        std::ifstream in(e.path());
        const json doc = json::parse(in, nullptr, false);
        if (!doc.is_discarded() && doc.is_object() && doc.value("format", std::string{}) == "avgen-project") {
            projects.push_back(e.path());
        }
    }
    std::sort(projects.begin(), projects.end());
    Rig rig("d5");
    // A session that may read the examples.
    api::SessionOptions o;
    o.client = api::ClientInfo{api::ClientKind::Test, "d5", "1"};
    o.permissions.readRoots = {fs::weakly_canonical(kRoot / "examples")};
    auto made = api::Session::create(std::move(o), rig.host);
    REQUIRE(made.has_value());
    auto& s = **made;

    int clean = 0;
    int missingAssets = 0;
    std::vector<std::string> failures;
    for (const fs::path& p : projects) {
        const std::string rel = fs::relative(p, kRoot).generic_string();
        const auto r = s.call("project.validate", json{{"path", p.string()}});
        if (!r.success) {
            failures.push_back(rel + ": the call failed: " + code(r));
            continue;
        }
        bool onlyMissing = true;
        bool anyError = false;
        std::string first;
        for (const json& f : r.result.at("findings")) {
            if (f.at("severity") != "error") {
                continue;
            }
            anyError = true;
            const std::string c = f.at("code").get<std::string>();
            const bool missing = c == "asset.missing" || c == "scene.missing";
            // The message reported is the first error that is not a missing file, else the first missing file.
            if (first.empty() || (!missing && onlyMissing)) {
                first = c + " " + f.at("pointer").get<std::string>() + ": " + f.at("message").get<std::string>();
            }
            onlyMissing = onlyMissing && missing;
        }
        if (!anyError) {
            ++clean;
        } else if (onlyMissing && kMissingAssetsAllowed.contains(rel)) {
            ++missingAssets;
        } else if (onlyMissing) {
            failures.push_back(rel + ": only missing assets, but not on the allowed list: " + first);
        } else {
            failures.push_back(rel + ": " + first);
        }
    }
    for (const std::string& f : failures) {
        UNSCOPED_INFO(f);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    INFO(projects.size() << " projects: " << clean << " clean, " << missingAssets << " with gitignored assets missing, "
                         << failures.size() << " failing; " << seconds << " s");
    CHECK(failures.empty());
    // Floors: a sweep that matched nothing, or a machine where most examples cannot load, would pass vacuously.
    CHECK(projects.size() >= 200);
    CHECK(clean >= 230); // 239 of 251 on 2026-11-25
    WARN(projects.size() << " projects: " << clean << " clean, " << missingAssets << " missing assets; " << seconds << " s");
}

// ---- 10: the PoC scene through tool calls only ------------------------------------------------------------------------

TEST_CASE("PoC steps 1-5: the column scene is built through tool calls only", "[api][documents][adr2157][poc]") {
    Rig rig("poc");
    // 1-3: project.create {scene: empty}, the import, a node from its id.
    rig.column();
    const int column = rig.nodeIndex("column");
    // 4: one patch adds the floor, the key and the rim, and sets the column's tint.
    const auto r = rig.patch(json::array({
        {{"op", "add"}, {"path", "/nodes/-"}, {"value", kFloor}},
        {{"op", "add"}, {"path", "/lights"}, {"value", json::array({kKey, kRim})}},
        {{"op", "add"}, {"path", fmt::format("/nodes/{}/tint", column)}, {"value", {1.0, 1.0, 1.0}}},
    }));
    INFO(code(r));
    REQUIRE(r.success);
    for (const char* p : {"lights/key/intensity", "lights/rim/intensity", "nodes/column/tint", "procedural/floor/material/baseColor",
                          "nodes/column/emissiveBoost"}) {
        INFO(p);
        CHECK(rig.hasParam(p));
    }
    // 5: the camera.
    rig.ok("camera.set", json{{"position", {0.0, 2.6, 9.0}}, {"target", {0.0, 2.0, 0.0}}, {"fovDegrees", 40.0}});
    const json doc = rig.scene().at("content");
    CHECK(doc.at("lights").size() == 2);
    CHECK(doc.at("nodes").size() == 2);
    CHECK(doc.at("camera").at("fov").get<double>() == Catch::Approx(40.0));
    // The whole thing validates, and the client wrote no file: the project folder holds only what the engine wrote.
    const auto v = rig.ok("project.validate", json::object());
    INFO(v.result.dump(2));
    CHECK(v.result.at("valid") == true);
    rig.ok("project.save", json::object());
    std::set<std::string> files;
    for (const auto& e : fs::recursive_directory_iterator(rig.write / "poc")) {
        if (e.is_regular_file()) {
            files.insert(fs::relative(e.path(), rig.write / "poc").generic_string());
        }
    }
    CHECK(files == std::set<std::string>{"poc.json", "poc.scene.json", "assets/manifest.json", "assets/models/column.glb"});
}

// ---- the published table ------------------------------------------------------------------------------------------------

TEST_CASE("E2: every finding code is published once, with a severity and a meaning", "[api][catalogue][adr2157]") {
    const json errors = api::errorCatalogue();
    REQUIRE(errors.contains("findings"));
    std::set<std::string> names;
    for (const json& f : errors.at("findings")) {
        const std::string c = f.at("code").get<std::string>();
        INFO(c);
        CHECK(names.insert(c).second);
        CHECK(f.at("meaning").get<std::string>().size() > 30);
        const std::string sev = f.at("severity").get<std::string>();
        CHECK((sev == "error" || sev == "warning" || sev == "info"));
    }
    CHECK(names.size() == api::kFindingCodes.size());
    for (const char* required : {"asset.relinked", "asset.missing", "parameter.unknown", "route.dead", "scene.unknownKey",
                                 "schema.type"}) {
        CHECK(names.contains(required));
    }
}
