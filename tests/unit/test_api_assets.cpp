// ADR-2156 (Engine API stage S3): one asset identity. A project is created by path with its scene beside it,
// `asset.import` returns `asset://project/<type>/<file>.<ext>` and writes manifest v2 with the engine's own
// load summary, and every tool that takes an asset takes that id or a project-relative path -- resolved by the
// session's PathPolicy against the PROJECT folder, never the working directory.
//
// Everything here drives a real `app::Engine` through a real `api::Session`, as the PoC client will.

#include "ai/tool_api.hpp"
#include "api/compensation.hpp"
#include "api/path_policy.hpp"
#include "api/session.hpp"
#include "app/engine.hpp"
#include "assets/asset_catalog.hpp"
#include "scene/composition.hpp"
#include "support/gltf_fixture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace avgen;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

fs::path freshDir(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("avgen_assets_" + name + "_" + std::to_string(getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return fs::weakly_canonical(dir);
}

std::unique_ptr<api::Session> makeSession(app::Engine&, api::Host& host, std::vector<fs::path> read,
                                          std::vector<fs::path> write) {
    api::SessionOptions o;
    o.client = api::ClientInfo{api::ClientKind::Test, "test", "1"};
    o.permissions.readRoots = std::move(read);
    o.permissions.writeRoots = std::move(write);
    auto s = api::Session::create(std::move(o), host);
    REQUIRE(s.has_value());
    (*s)->pump();
    return std::move(*s);
}

std::string code(const api::CallResult& r) {
    if (r.error) {
        UNSCOPED_INFO(r.error->message);
    }
    return r.error ? ai::toolErrorCodeName(r.error->code) : "none";
}

// The result matches the tool's published output schema.
void checkOutput(const api::Session& s, const std::string& tool, const api::CallResult& r) {
    const ai::Tool* t = s.tools().find(tool);
    REQUIRE(t != nullptr);
    REQUIRE_FALSE(t->definition.outputSchema.is_null());
    const auto problem = ai::validateAgainstSchema(t->definition.outputSchema, r.result, tool);
    INFO((problem ? problem->message : std::string{}) << "\n" << r.result.dump(2));
    CHECK_FALSE(problem.has_value());
}

std::string slurp(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// A two-part model: a skinned box (12 triangles, one joint) and a static plane (2 triangles).
fs::path writeColumnGlb(const std::string& name, float height = 2.0f) {
    std::vector<testsupport::GlbMesh> meshes(2);
    meshes[0].node = "Column_Body";
    meshes[0].material = "Stone";
    meshes[0].skinned = true;
    testsupport::appendBox(meshes[0], {-0.5f, 0.0f, -0.5f}, {0.5f, height, 0.5f});
    meshes[1].node = "Column_Base";
    meshes[1].material = "Glow";
    testsupport::appendPlane(meshes[1], {-1.0f, -1.0f}, {1.0f, 1.0f}, 0.0f);
    return testsupport::writeMeshesGlb(name, meshes);
}

// Runs the test from another folder, and puts the process back however the test ends.
struct CwdGuard {
    fs::path before = fs::current_path();
    explicit CwdGuard(const fs::path& to) { fs::current_path(to); }
    ~CwdGuard() {
        std::error_code ec;
        fs::current_path(before, ec);
    }
    CwdGuard(const CwdGuard&) = delete;
    CwdGuard& operator=(const CwdGuard&) = delete;
};

struct Rig {
    fs::path root;
    fs::path read;  // where the client's exports are
    fs::path write; // where projects go
    app::Engine engine{app::EngineMode::Offline};
    api::LocalHost host{engine};
    std::unique_ptr<api::Session> s;
    explicit Rig(const std::string& name) : root(freshDir(name)), read(root / "exports"), write(root / "work") {
        fs::create_directories(read);
        fs::create_directories(write);
        s = makeSession(engine, host, {read}, {write});
    }
    ~Rig() {
        s.reset();
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    fs::path create(const std::string& path, const std::string& scene = "empty") {
        const auto r = s->call("project.create", json{{"path", path}, {"scene", scene}});
        INFO(code(r));
        REQUIRE(r.success);
        return fs::path(r.result.at("folder").get<std::string>());
    }
};

} // namespace

TEST_CASE("PoC steps 1-3: create a project, import a model, make a node from its id, from another working directory",
          "[api][assets]") {
    Rig rig("poc");
    fs::copy_file(writeColumnGlb("assets_poc_column"), rig.read / "column.glb", fs::copy_options::overwrite_existing);
    // A decoy with the same relative names in the working directory: nothing may pick it up.
    const fs::path elsewhere = rig.root / "cwd";
    fs::create_directories(elsewhere / "assets" / "models");
    fs::copy_file(testsupport::writeTriangleGlb("assets_poc_decoy"), elsewhere / "assets" / "models" / "column.glb",
                  fs::copy_options::overwrite_existing);
    const CwdGuard cwd(elsewhere);

    // 1. The project, by path, relative to the write root; its scene beside it.
    const auto created = rig.s->call("project.create", json{{"path", "poc/poc.json"}, {"scene", "empty"}});
    INFO(code(created));
    REQUIRE(created.success);
    checkOutput(*rig.s, "project.create", created);
    const fs::path project = rig.write / "poc";
    CHECK(fs::path(created.result.at("file").get<std::string>()) == project / "poc.json");
    CHECK(fs::path(created.result.at("scene").get<std::string>()) == project / "poc.scene.json");
    CHECK(fs::exists(project / "poc.json"));
    CHECK(fs::exists(project / "poc.scene.json"));
    REQUIRE(rig.engine.composition() != nullptr);
    CHECK(fs::absolute(rig.engine.compositionPath()).parent_path() == project);
    // The project file names the scene, so it reopens with it.
    const json doc = json::parse(slurp(project / "poc.json"));
    CHECK(doc.at("assets").at("scene").at("kind") == "composition");
    CHECK(doc.at("assets").at("scene").contains("path"));

    // 2. The import: the canonical id, manifest v2, the engine's own summary.
    const json source{{"file", (rig.read / "column.glb").string()}, {"tool", "Blender"}, {"toolVersion", "5.2"},
                      {"exportProfile", "avgen-gltf-1"}, {"exportSettingsSha256", "abc"}};
    const auto imported =
        rig.s->call("asset.import", json{{"file", (rig.read / "column.glb").string()}, {"source", source}});
    INFO(code(imported));
    REQUIRE(imported.success);
    checkOutput(*rig.s, "asset.import", imported);
    const std::string id = imported.result.at("id").get<std::string>();
    CHECK(id == "asset://project/models/column.glb");
    CHECK(imported.result.at("duplicate") == false);
    const json& summary = imported.result.at("import");
    CHECK(summary.at("meshes") == 2);
    CHECK(summary.at("rigs") == 1);
    CHECK(summary.at("triangles") == 14);
    const json manifest = json::parse(slurp(project / "assets" / "manifest.json"));
    CHECK(manifest.at("version") == 2);
    REQUIRE(manifest.at("assets").size() == 1);
    const json& entry = manifest.at("assets")[0];
    CHECK(entry.at("id") == id);
    CHECK(entry.at("path") == "assets/models/column.glb");
    CHECK(entry.at("size") == fs::file_size(rig.read / "column.glb"));
    CHECK(entry.at("source") == source);
    CHECK(entry.at("import") == summary);

    // 3. A node from the id. It is the project's file, not the working directory's decoy, and the node keeps
    //    the id because the scene's registry reads it back as the same file.
    const auto node = rig.s->call("scene.create_node", json{{"name", "column"}, {"asset", id}});
    INFO(code(node));
    REQUIRE(node.success);
    checkOutput(*rig.s, "scene.create_node", node);
    CHECK(node.result.at("bounds").at("valid") == true);
    CHECK(node.result.at("bounds").at("max")[1].get<double>() == Catch::Approx(2.0).margin(1e-4));
    CHECK(node.result.at("asset").at("id") == id);
    const scene::CompositionNode* made = rig.engine.composition()->findNode("column");
    REQUIRE(made != nullptr);
    CHECK(made->asset.generic_string() == id);
    CHECK(fs::equivalent(rig.engine.assetRegistry().resolve(made->asset), project / "assets" / "models" / "column.glb"));

    // A project-relative path names the same file.
    const auto byPath = rig.s->call("scene.create_node", json{{"name", "column2"}, {"asset", "assets/models/column.glb"}});
    INFO(code(byPath));
    REQUIRE(byPath.success);
    CHECK(byPath.result.at("asset").at("id") == id);
    CHECK(byPath.result.at("bounds").at("max")[1].get<double>() == Catch::Approx(2.0).margin(1e-4));

    // asset.get answers for the id and for the path, with the manifest's entry.
    for (const std::string& name : {id, std::string("assets/models/column.glb")}) {
        const auto got = rig.s->call("asset.get", json{{"id", name}});
        INFO(name << ": " << code(got));
        REQUIRE(got.success);
        checkOutput(*rig.s, "asset.get", got);
        CHECK(got.result.at("id") == id);
        CHECK(got.result.at("manifest").at("source") == source);
        CHECK(got.result.at("manifest").at("import").at("triangles") == 14);
    }
    // asset.list reports the same id.
    const auto listed = rig.s->call("asset.list", json{{"query", "column"}});
    REQUIRE(listed.success);
    bool found = false;
    for (const json& a : listed.result.at("assets")) {
        found = found || a.at("id") == id;
    }
    CHECK(found);
}

TEST_CASE("A duplicate import returns the same id, and copies nothing", "[api][assets]") {
    Rig rig("dup");
    fs::copy_file(writeColumnGlb("assets_dup"), rig.read / "column.glb", fs::copy_options::overwrite_existing);
    fs::copy_file(rig.read / "column.glb", rig.read / "renamed.glb", fs::copy_options::overwrite_existing);
    rig.create("dup");
    const auto first = rig.s->call("asset.import", json{{"file", (rig.read / "column.glb").string()}});
    REQUIRE(first.success);
    for (const char* again : {"column.glb", "renamed.glb"}) {
        const auto dup = rig.s->call("asset.import", json{{"file", (rig.read / again).string()}});
        INFO(again << ": " << code(dup));
        REQUIRE(dup.success);
        checkOutput(*rig.s, "asset.import", dup);
        CHECK(dup.result.at("duplicate") == true);
        CHECK(dup.result.at("id") == first.result.at("id"));
        CHECK(dup.result.at("copied").empty());
        CHECK(dup.result.at("import") == first.result.at("import"));
    }
    CHECK_FALSE(fs::exists(rig.write / "dup" / "assets" / "models" / "renamed.glb"));
}

TEST_CASE("A version 1 manifest is read by path and rewritten as version 2", "[api][assets]") {
    Rig rig("v1");
    const fs::path project = rig.create("v1");
    fs::create_directories(project / "assets" / "models");
    fs::copy_file(testsupport::writeTriangleGlb("assets_v1_old"), project / "assets" / "models" / "old.glb",
                  fs::copy_options::overwrite_existing);
    // What asset.import wrote before ADR-2156: an extensionless id, no size, plus a key this build ignores.
    std::ofstream(project / "assets" / "manifest.json")
        << R"({"format":"avgen-project-assets","version":1,"_note":"kept","assets":[)"
           R"({"id":"asset://project/models/old","type":"models","path":"assets/models/old.glb","sha256":"x","_by":"hand"}]})";

    const auto read = assets::readManifest(project);
    REQUIRE(read.has_value());
    CHECK(read->versionRead == 1);
    CHECK(read->corrected);
    REQUIRE(read->assets.size() == 1);
    CHECK(read->assets[0].id == "asset://project/models/old.glb");
    CHECK(read->assets[0].size == fs::file_size(project / "assets" / "models" / "old.glb"));

    // Listed and resolved under the corrected id; the old spelling is not an alias.
    const auto got = rig.s->call("asset.get", json{{"id", "asset://project/models/old.glb"}});
    INFO(code(got));
    REQUIRE(got.success);
    CHECK(got.result.at("manifest").at("sha256") == "x");
    CHECK(code(rig.s->call("scene.create_node", json{{"name", "o"}, {"asset", "asset://project/models/old"}})) ==
          "NOT_FOUND");

    // The next import rewrites the file: version 2, the id corrected, unknown keys kept.
    fs::copy_file(writeColumnGlb("assets_v1_new"), rig.read / "new.glb", fs::copy_options::overwrite_existing);
    REQUIRE(rig.s->call("asset.import", json{{"file", (rig.read / "new.glb").string()}}).success);
    const json manifest = json::parse(slurp(project / "assets" / "manifest.json"));
    CHECK(manifest.at("version") == 2);
    CHECK(manifest.at("_note") == "kept");
    REQUIRE(manifest.at("assets").size() == 2);
    CHECK(manifest.at("assets")[0].at("id") == "asset://project/models/old.glb");
    CHECK(manifest.at("assets")[0].at("_by") == "hand");
    CHECK(manifest.at("assets")[0].at("size") == fs::file_size(project / "assets" / "models" / "old.glb"));
    CHECK(manifest.at("assets")[1].at("id") == "asset://project/models/new.glb");

    // The catalogue Studio's asset panel shows reads both versions the same way.
    const auto catalog = assets::catalogAssets({project}, project);
    REQUIRE(catalog.has_value());
    std::vector<std::string> ids;
    for (const auto& r : *catalog) {
        if (r.source == assets::AssetSource::Project && r.type == "model") ids.push_back(r.id);
    }
    CHECK(ids == std::vector<std::string>{"asset://project/models/new.glb", "asset://project/models/old.glb"});

    // A manifest this build cannot read is refused, never overwritten.
    std::ofstream(project / "assets" / "manifest.json", std::ios::trunc) << "{ not json";
    fs::copy_file(testsupport::writeTriangleGlb("assets_v1_third"), rig.read / "third.glb",
                  fs::copy_options::overwrite_existing);
    const auto refused = rig.s->call("asset.import", json{{"file", (rig.read / "third.glb").string()}});
    CHECK(code(refused) == "FAILED_PRECONDITION");
    CHECK(slurp(project / "assets" / "manifest.json") == "{ not json");
    CHECK_FALSE(fs::exists(project / "assets" / "models" / "third.glb"));
}

TEST_CASE("The import summary is the engine's load of the generated model", "[api][assets]") {
    Rig rig("summary");
    rig.create("summary");
    SECTION("a triangle, a material and a light") {
        fs::copy_file(testsupport::writeTriangleGlb("assets_summary_tri"), rig.read / "tri.glb",
                      fs::copy_options::overwrite_existing);
        const auto r = rig.s->call("asset.import", json{{"file", (rig.read / "tri.glb").string()}});
        INFO(code(r));
        REQUIRE(r.success);
        const json& s = r.result.at("import");
        CHECK(s.at("meshes") == 1);
        CHECK(s.at("materials") == 1);
        CHECK(s.at("lights") == 1);
        CHECK(s.at("rigs") == 0);
        CHECK(s.at("clips") == 0);
        CHECK(s.at("triangles") == 1);
        CHECK(s.at("warnings").empty());
        CHECK(s.at("bounds").at("min")[0].get<double>() == Catch::Approx(2.0));
        CHECK(s.at("bounds").at("max")[0].get<double>() == Catch::Approx(3.0));
        CHECK(s.at("bounds").at("max")[1].get<double>() == Catch::Approx(1.0));
    }
    SECTION("two parts, one skinned") {
        fs::copy_file(writeColumnGlb("assets_summary_col"), rig.read / "column.glb", fs::copy_options::overwrite_existing);
        const auto r = rig.s->call("asset.import", json{{"file", (rig.read / "column.glb").string()}});
        INFO(code(r));
        REQUIRE(r.success);
        const json& s = r.result.at("import");
        CHECK(s.at("meshes") == 2);
        CHECK(s.at("entities") == 2);
        CHECK(s.at("materials") == 2);
        CHECK(s.at("rigs") == 1);
        CHECK(s.at("joints") == 1);
        CHECK(s.at("triangles") == 14);
        CHECK(s.at("materialNames") == json{"Stone", "Glow"});
        CHECK(s.at("rigDetail")[0].at("joints") == json{"Column_Body_joint"});
    }
    SECTION("a file that does not load is refused, and nothing is left") {
        std::ofstream(rig.read / "junk.glb") << "not a glb";
        const auto r = rig.s->call("asset.import", json{{"file", (rig.read / "junk.glb").string()}});
        CHECK(code(r) == "UNSUPPORTED");
        CHECK_FALSE(fs::exists(rig.write / "summary" / "assets" / "models" / "junk.glb"));
        CHECK_FALSE(fs::exists(rig.write / "summary" / "assets" / "manifest.json"));
    }
}

TEST_CASE("An asset id cannot leave the project's assets folder", "[api][assets][paths]") {
    Rig rig("escape");
    SECTION("no project open: an id names nothing") {
        CHECK(code(rig.s->call("asset.get", json{{"id", "asset://project/models/x.glb"}})) == "FAILED_PRECONDITION");
    }
    SECTION("with a project") {
        const fs::path project = rig.create("escape");
        fs::create_directories(project / "assets" / "models");
        const fs::path glb = testsupport::writeTriangleGlb("assets_escape");
        fs::copy_file(glb, project / "x.glb", fs::copy_options::overwrite_existing);
        fs::copy_file(glb, rig.read / "outside.glb", fs::copy_options::overwrite_existing);
        // The file is real and even readable (the project folder is in the write root), but not an asset.
        for (const char* id : {"asset://project/../x.glb", "asset://project/models/../../x.glb"}) {
            INFO(id);
            CHECK(code(rig.s->call("scene.create_node", json{{"name", "n"}, {"asset", id}})) == "PERMISSION_DENIED");
            CHECK(code(rig.s->call("asset.get", json{{"id", id}})) == "PERMISSION_DENIED");
        }
        // A symlink inside assets/ that points out of the project is refused on its canonical form, even
        // though its target is in a folder the session may read.
        fs::create_symlink(rig.read / "outside.glb", project / "assets" / "models" / "link.glb");
        CHECK(code(rig.s->call("scene.create_node", json{{"name", "n"}, {"asset", "asset://project/models/link.glb"}})) ==
              "PERMISSION_DENIED");
        const api::PathPolicy policy(rig.s->permissions(), &rig.engine);
        CHECK(policy.assetIdFor(project / "assets" / "models" / "link.glb").empty());
        // Another scheme is not resolved at all.
        CHECK(code(rig.s->call("asset.get", json{{"id", "asset://builtin/x.glb"}})) == "UNSUPPORTED");
        CHECK(code(rig.s->call("asset.get", json{{"id", "asset://engine/x.glb"}})) == "UNSUPPORTED");
        CHECK(code(rig.s->call("asset.get", json{{"id", "asset://project/"}})) == "INVALID_ARGUMENTS");
    }
}

TEST_CASE("A rolled-back import leaves no file and restores the manifest; a saved project's file survives",
          "[api][assets][compensation]") {
    Rig rig("rollback");
    const fs::path project = rig.create("rb");
    fs::copy_file(writeColumnGlb("assets_rb_a"), rig.read / "a.glb", fs::copy_options::overwrite_existing);
    fs::copy_file(writeColumnGlb("assets_rb_b", 3.0f), rig.read / "b.glb", fs::copy_options::overwrite_existing);
    REQUIRE(rig.s->call("asset.import", json{{"file", (rig.read / "a.glb").string()}}).success);
    const std::string manifestBefore = slurp(project / "assets" / "manifest.json");
    const auto listing = [&] {
        std::vector<std::string> names;
        for (const auto& e : fs::recursive_directory_iterator(project / "assets")) {
            names.push_back(e.path().lexically_relative(project).generic_string());
        }
        std::sort(names.begin(), names.end());
        return names;
    };
    const auto filesBefore = listing();

    api::CallOptions in;
    in.irreversible = api::Irreversible::Accept;

    SECTION("an import, then a node from it, rolled back") {
        in.transaction = rig.s->call("transaction.begin", json{{"label", "b"}}).result.at("id").get<std::string>();
        const auto b = rig.s->call("asset.import", json{{"file", (rig.read / "b.glb").string()}}, in);
        REQUIRE(b.success);
        REQUIRE(rig.s->call("scene.create_node", json{{"name", "b"}, {"asset", b.result.at("id")}}, in).success);
        const auto back = rig.s->call("transaction.rollback", json{{"id", *in.transaction}});
        INFO(back.result.dump(2));
        REQUIRE(back.success);
        CHECK(listing() == filesBefore); // no copy, no staging name
        CHECK(slurp(project / "assets" / "manifest.json") == manifestBefore);
        CHECK(rig.engine.composition()->findNode("b") == nullptr);
    }
    SECTION("a save inside the transaction names the id: the file is kept") {
        in.transaction = rig.s->call("transaction.begin", json{{"label", "b"}}).result.at("id").get<std::string>();
        const auto b = rig.s->call("asset.import", json{{"file", (rig.read / "b.glb").string()}}, in);
        REQUIRE(b.success);
        REQUIRE(rig.s->call("scene.create_node", json{{"name", "b"}, {"asset", b.result.at("id")}}, in).success);
        REQUIRE(rig.s->call("project.save", json::object(), in).success);
        // The project names the node's asset by its id, which is what makes the project portable.
        CHECK(slurp(project / "rb.json").find("asset://project/models/b.glb") != std::string::npos);
        const auto back = rig.s->call("transaction.rollback", json{{"id", *in.transaction}});
        INFO(back.result.dump(2));
        REQUIRE(back.success);
        CHECK(fs::exists(project / "assets" / "models" / "b.glb"));
    }
    SECTION("a file the first, committed import made is never deleted by a later rollback") {
        in.transaction = rig.s->call("transaction.begin", json{{"label", "dup"}}).result.at("id").get<std::string>();
        const auto dup = rig.s->call("asset.import", json{{"file", (rig.read / "a.glb").string()}}, in);
        REQUIRE(dup.success);
        CHECK(dup.result.at("duplicate") == true);
        REQUIRE(rig.s->call("transaction.rollback", json{{"id", *in.transaction}}).success);
        CHECK(fs::exists(project / "assets" / "models" / "a.glb"));
        CHECK(slurp(project / "assets" / "manifest.json") == manifestBefore);
    }
}

TEST_CASE("project.open and project.save_as take a name, a .json path or a folder", "[api][assets][project]") {
    Rig rig("paths");
    const fs::path project = rig.create("Night");
    CHECK(project == rig.write / "Night");
    CHECK(fs::exists(rig.write / "Night" / "Night.scene.json"));
    REQUIRE(rig.s->call("project.save_as", json{{"path", (rig.write / "Fork").string()}}).success);
    CHECK(fs::exists(rig.write / "Fork" / "Fork.json"));
    // A relative path resolves against the OPEN project's folder (PathPolicy's one rule), so a second project
    // is named by its bare name or an absolute path.
    for (const std::string p : {std::string("Night"), (rig.write / "Night").string(),
                                (rig.write / "Night" / "Night.json").string()}) {
        INFO(p);
        const auto opened = rig.s->call("project.open", json{{"path", p}});
        INFO(code(opened));
        CHECK(opened.success);
    }
    CHECK(code(rig.s->call("project.open", json{{"path", "Nowhere"}})) == "NOT_FOUND");
    const auto listed = rig.s->call("project.list", json::object());
    REQUIRE(listed.success);
    checkOutput(*rig.s, "project.list", listed);
    CHECK(listed.result.at("projects") == json{"Fork", "Night"});
}
