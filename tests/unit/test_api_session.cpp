// ADR-2153 (Engine API stage S1): `api::Session` -- the one call path every client uses, its permissions
// and path policy, its transactions and compensation log, the live policy and the capability answers.
//
// Everything here drives a real `app::Engine` through a real Session. The hosts are `api::LocalHost`
// (what Studio uses) or `FakeHost` below, which is the same thing with a show switch a test can flip:
// Studio has no external client yet, so the live policy can only be exercised this way.

#include "ai/tool_api.hpp"
#include "ai/tool_context.hpp"
#include "api/compensation.hpp"
#include "api/path_policy.hpp"
#include "api/session.hpp"
#include "api/version.hpp"
#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "scene/composition.hpp"
#include "support/gltf_fixture.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>
#include <unistd.h>

using namespace avgen;
using Catch::Matchers::WithinAbs;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

fs::path helixScene() { return fs::path(AVGEN_SOURCE_DIR) / "examples" / "helix" / "helix.scene.json"; }

fs::path freshDir(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("avgen_session_" + name + "_" + std::to_string(getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

// A host whose show a test switches on and off, with a known time left.
class FakeHost final : public api::Host {
public:
    explicit FakeHost(app::Engine& engine) : engine_(&engine) {}
    app::Engine& engine() override { return *engine_; }
    ai::MainThreadQueue& queue() override { return queue_; }
    void installHooks(ai::ToolContext&) override {}
    bool showRunning() override { return show; }
    std::optional<double> showRemainingSeconds() override { return 5.0; }
    bool show = false;

private:
    app::Engine* engine_;
    ai::MainThreadQueue queue_;
};

api::SessionOptions options(std::vector<fs::path> read = {}, std::vector<fs::path> write = {},
                            api::ClientKind kind = api::ClientKind::Test) {
    api::SessionOptions o;
    o.client = api::ClientInfo{kind, "test", "1"};
    o.permissions.readRoots = std::move(read);
    o.permissions.writeRoots = std::move(write);
    return o;
}

std::unique_ptr<api::Session> makeSession(api::Host& host, api::SessionOptions o) {
    auto s = api::Session::create(std::move(o), host);
    REQUIRE(s.has_value());
    (*s)->pump(); // binds this thread as the host thread, so calls from it run inline
    return std::move(*s);
}

ai::Tool probeTool(std::string name, ai::ToolAnnotations a, ai::ToolFn fn) {
    ai::Tool t;
    t.definition.name = std::move(name);
    t.definition.title = "probe";
    t.definition.description = "a test probe";
    t.definition.inputSchema = ai::schema::object(json::object());
    t.definition.annotations = std::move(a);
    t.execute = std::move(fn);
    return t;
}

ai::ToolAnnotations reading() {
    ai::ToolAnnotations a;
    a.readOnly = true;
    return a;
}

std::string code(const api::CallResult& r) {
    if (r.error) {
        UNSCOPED_INFO(r.error->message);
    }
    return r.error ? ai::toolErrorCodeName(r.error->code) : "none";
}

fs::path writeWav(const fs::path& file) {
    constexpr std::uint32_t rate = 48000;
    const auto tone = audio::AudioFile::fromInterleaved(
        testsupport::interleave(testsupport::sine(220.0f, rate, rate, 0.4f), 2), 2, rate);
    REQUIRE(tone.writeWav(file).has_value());
    return file;
}

// A project created through the session, so the folder it lives in is the session's write root.
fs::path createProject(api::Session& s, const std::string& name) {
    const auto created = s.call("project.create", json{{"name", name}});
    INFO(created.summary);
    REQUIRE(created.success);
    return fs::path(created.result.at("file").get<std::string>()).parent_path();
}

} // namespace

// ---- paths and permissions -----------------------------------------------------------------------

TEST_CASE("A path outside the session's roots is PermissionDenied, not NotFound", "[api][session][paths]") {
    const fs::path root = freshDir("outside");
    fs::create_directories(root / "allowed");
    fs::create_directories(root / "secret");
    writeWav(root / "secret" / "private.wav");
    app::Engine engine(app::EngineMode::Offline);
    api::LocalHost host(engine);
    auto s = makeSession(host, options({root / "allowed"}, {root / "projects"}));
    createProject(*s, "P");

    for (const fs::path& bad : {root / "secret" / "private.wav", root / "allowed" / ".." / "secret" / "private.wav"}) {
        INFO(bad.string());
        const auto r = s->call("asset.import_audio", json{{"file", bad.string()}});
        CHECK(code(r) == "PERMISSION_DENIED");
        REQUIRE(r.error);
        CHECK(r.error->details.value("path", std::string{}) == bad.string());
    }
    // Inside a root but missing is still NotFound: "forbidden" and "missing" are different answers now.
    const auto missing = s->call("asset.import_audio", json{{"file", (root / "allowed" / "nope.wav").string()}});
    CHECK(code(missing) == "NOT_FOUND");
    fs::remove_all(root);
}

TEST_CASE("A session with no roots refuses path tools and says how to start one", "[api][session][paths]") {
    app::Engine engine(app::EngineMode::Offline);
    api::LocalHost host(engine);
    auto s = makeSession(host, options());
    for (const char* tool : {"project.list", "project.create"}) {
        INFO(tool);
        const auto r = s->call(tool, std::string(tool) == "project.create" ? json{{"name", "x"}} : json::object());
        CHECK(code(r) == "PERMISSION_DENIED");
        REQUIRE(r.error);
        CHECK(r.error->recovery == "start the session with --root <dir> or --write-root <dir>");
    }
}

TEST_CASE("A relative path resolves against the project folder, not the working directory", "[api][session][paths]") {
    const fs::path root = freshDir("relative");
    const fs::path elsewhere = freshDir("relative_cwd");
    app::Engine engine(app::EngineMode::Offline);
    api::LocalHost host(engine);
    auto s = makeSession(host, options({}, {root}));
    const fs::path project = createProject(*s, "Rel");
    fs::create_directories(project / "models");
    const fs::path glb = testsupport::writeTriangleGlb("session_relative");
    fs::copy_file(glb, project / "models" / "tri.glb", fs::copy_options::overwrite_existing);
    // The same relative name in the working directory must NOT be what is picked up.
    fs::create_directories(elsewhere / "models");
    fs::copy_file(glb, elsewhere / "models" / "tri.glb", fs::copy_options::overwrite_existing);

    const fs::path before = fs::current_path();
    fs::current_path(elsewhere);
    engine.newComposition();
    const auto made = s->call("scene.create_node", json{{"name", "tri"}, {"asset", "models/tri.glb"}});
    const api::PathPolicy policy(s->permissions(), &engine);
    const auto resolved = policy.forRead("models/tri.glb");
    fs::current_path(before);

    INFO(made.summary);
    REQUIRE(made.success);
    REQUIRE(engine.composition()->findNode("tri") != nullptr);
    CHECK(fs::weakly_canonical(engine.composition()->findNode("tri")->asset) ==
          fs::weakly_canonical(project / "models" / "tri.glb"));
    REQUIRE(resolved.has_value());
    CHECK(fs::weakly_canonical(*resolved) == fs::weakly_canonical(project / "models" / "tri.glb"));
    fs::remove(glb);
    fs::remove_all(root);
    fs::remove_all(elsewhere);
}

TEST_CASE("Permissions can be narrowed and never widened; roots must be absolute", "[api][session][paths]") {
    app::Engine engine(app::EngineMode::Offline);
    api::LocalHost host(engine);
    CHECK_FALSE(api::Session::create(options({"relative/root"}), host).has_value());
    auto s = makeSession(host, options({"/tmp/a"}, {"/tmp/w"}));
    api::SessionPermissions wider = s->permissions();
    wider.subprocess = true;
    CHECK_FALSE(s->narrow(wider).has_value());
    api::SessionPermissions narrower = s->permissions();
    narrower.writeRoots.clear();
    narrower.gpu = false;
    CHECK(s->narrow(narrower).has_value());
    CHECK(s->permissions().writeRoots.empty());
    narrower.writeRoots = {"/tmp/w"};
    CHECK_FALSE(s->narrow(narrower).has_value()); // a write root, once taken away, does not come back
}

TEST_CASE("A subprocess tool is refused without the permission, before the host is involved",
          "[api][session][paths]") {
    app::Engine engine(app::EngineMode::Offline);
    api::LocalHost host(engine);
    auto s = makeSession(host, options());
    const auto r = s->call("performance.profile_scene", json::object());
    CHECK(code(r) == "PERMISSION_DENIED");
    CHECK(r.error->recovery.find("--allow-subprocess") != std::string::npos);
}

// ---- snapshot stores ------------------------------------------------------------------------------------

TEST_CASE("Two sessions have independent snapshot stores", "[api][session]") {
    app::Engine a(app::EngineMode::Offline);
    app::Engine b(app::EngineMode::Offline);
    api::LocalHost hostA(a);
    api::LocalHost hostB(b);
    auto sa = makeSession(hostA, options());
    auto sb = makeSession(hostB, options());
    const auto snap = sa->call("project.create_snapshot", json{{"label", "a's"}});
    REQUIRE(snap.success);
    const std::string id = snap.result.at("id").get<std::string>();
    CHECK(sa->snapshots().size() == 1);
    CHECK(sb->snapshots().size() == 0);
    const auto listed = sb->call("project.list_snapshots");
    REQUIRE(listed.success);
    CHECK(listed.result.at("snapshots").empty());
    CHECK(code(sb->call("project.restore_snapshot", json{{"id", id}})) == "NOT_FOUND");
    CHECK(sa->call("project.restore_snapshot", json{{"id", id}}).success);
}

// ---- transactions --------------------------------------------------------------------------------------------

TEST_CASE("An explicit transaction rolls back a parameter, a route and a created node", "[api][session][transaction]") {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadFile(helixScene()).has_value());
    api::LocalHost host(engine);
    auto s = makeSession(host, options());
    const float fogBefore = engine.params().find("scene/volumeDensity")->baseComponent(0);
    const std::size_t routesBefore = engine.modulator().routes().size();
    const std::size_t nodesBefore = engine.composition()->nodes().size();

    const auto begun = s->call("transaction.begin", json{{"label", "try things"}});
    REQUIRE(begun.success);
    const std::string id = begun.result.at("id").get<std::string>();
    api::CallOptions in;
    in.transaction = id;
    REQUIRE(s->call("parameter.set", json{{"path", "scene/volumeDensity"}, {"value", 0.77}}, in).success);
    REQUIRE(s->call("modulation.create", json{{"source", "audio.bass"}, {"target", "scene/volumeDensity"}, {"amount", 2.0}},
                    in)
                .success);
    const auto node = s->call("scene.create_node", json{{"name", "regrettable"}, {"kind", "group"}}, in);
    INFO(node.summary);
    REQUIRE(node.success);
    CHECK(node.transaction == id);
    CHECK(engine.composition()->findNode("regrettable") != nullptr);
    CHECK(engine.modulator().routes().size() == routesBefore + 1);

    SECTION("a second begin is refused, and so is a mutating call beside it") {
        CHECK(code(s->call("transaction.begin", json{{"label", "again"}})) == "FAILED_PRECONDITION");
        CHECK(code(s->call("parameter.set", json{{"path", "scene/volumeDensity"}, {"value", 0.1}})) ==
              "FAILED_PRECONDITION");
        CHECK(s->call("parameter.get", json{{"paths", {"scene/volumeDensity"}}}).success); // reads still run
    }

    const auto status = s->call("transaction.status", json::object());
    REQUIRE(status.success);
    CHECK(status.result.at("calls").get<int>() >= 3);
    const auto back = s->call("transaction.rollback", json{{"id", id}});
    INFO(back.summary);
    REQUIRE(back.success);
    CHECK(back.result.at("state") == "rolled-back");
    CHECK(engine.composition()->findNode("regrettable") == nullptr);
    CHECK(engine.composition()->nodes().size() == nodesBefore);
    CHECK(engine.modulator().routes().size() == routesBefore);
    CHECK_THAT(engine.params().find("scene/volumeDensity")->baseComponent(0), WithinAbs(fogBefore, 1e-6));
    CHECK(s->call("transaction.status", json::object()).result.at("state") == "none");
    CHECK(code(s->call("transaction.commit", json{{"id", id}})) == "FAILED_PRECONDITION");
}

TEST_CASE("A failed mutating call rolls back its own implicit transaction", "[api][session][transaction]") {
    app::Engine engine(app::EngineMode::Offline);
    ai::ToolAnnotations a;
    a.mutatesProject = true;
    a.undoable = true;
    api::LocalHost host(engine);
    api::SessionOptions o = options();
    o.extraTools.push_back(probeTool("test.half_then_fail", a, [](const json&, ai::ToolContext& ctx) {
        ctx.engine().params().find("orb/scale")->setBaseComponent(0, 9.0f);
        return ai::ToolResult::failure(ai::ToolErrorCode::Internal, "failed half-way");
    }));
    auto s = makeSession(host, std::move(o));
    const float before = engine.params().find("orb/scale")->baseComponent(0);
    const auto r = s->call("test.half_then_fail", json::object());
    CHECK_FALSE(r.success);
    REQUIRE(r.transaction.has_value()); // it ran in one of its own
    CHECK_THAT(engine.params().find("orb/scale")->baseComponent(0), WithinAbs(before, 1e-6));
    const auto ok = s->call("parameter.set", json{{"path", "orb/scale"}, {"value", 2.0}});
    REQUIRE(ok.success);
    REQUIRE(ok.transaction.has_value());
    CHECK_THAT(engine.params().find("orb/scale")->baseComponent(0), WithinAbs(2.0, 1e-6));
}

TEST_CASE("An irreversible call is refused inside a transaction unless accepted, which makes it partial",
          "[api][session][transaction]") {
    app::Engine engine(app::EngineMode::Offline);
    api::LocalHost host(engine);
    auto s = makeSession(host, options());
    const std::string id = s->call("transaction.begin", json{{"label", "seek"}}).result.at("id").get<std::string>();
    api::CallOptions in;
    in.transaction = id;
    const auto refused = s->call("sequencer.set_playhead", json{{"seconds", 1.0}}, in);
    CHECK(code(refused) == "FAILED_PRECONDITION");
    CHECK(refused.error->details.value("field", std::string{}) == "irreversible");
    CHECK(s->call("transaction.status", json::object()).result.at("partial") == false);

    in.irreversible = api::Irreversible::Accept;
    const auto accepted = s->call("sequencer.set_playhead", json{{"seconds", 1.0}}, in);
    INFO(accepted.summary);
    REQUIRE(accepted.success);
    REQUIRE(accepted.warnings.size() == 1);
    CHECK(accepted.warnings[0].code == "transaction.partial");
    CHECK(s->call("transaction.status", json::object()).result.at("partial") == true);
    const auto committed = s->call("transaction.commit", json{{"id", id}});
    REQUIRE(committed.success);
    CHECK(committed.result.at("partial") == true);
    CHECK(committed.result.at("notRestorable").at("tools") == json::array({"sequencer.set_playhead"}));
}

// ---- the compensation log --------------------------------------------------------------------------------------

TEST_CASE("A rollback deletes the files its transaction created", "[api][session][compensation]") {
    const fs::path root = freshDir("comp_created");
    fs::create_directories(root / "allowed");
    const fs::path glb = testsupport::writeTriangleGlb("session_comp_created");
    fs::copy_file(glb, root / "allowed" / "tree.glb", fs::copy_options::overwrite_existing);
    app::Engine engine(app::EngineMode::Offline);
    api::LocalHost host(engine);
    auto s = makeSession(host, options({root / "allowed"}, {root / "projects"}));
    const fs::path project = createProject(*s, "C");

    const std::string id = s->call("transaction.begin", json{{"label", "import"}}).result.at("id").get<std::string>();
    api::CallOptions in;
    in.transaction = id;
    in.irreversible = api::Irreversible::Accept;
    REQUIRE(s->call("asset.import", json{{"file", (root / "allowed" / "tree.glb").string()}}, in).success);
    CHECK(fs::exists(project / "assets" / "models" / "tree.glb"));
    CHECK(fs::exists(project / "assets" / "manifest.json"));
    const auto status = s->call("transaction.status", json::object());
    CHECK(status.result.at("createdFiles").size() == 2);
    const auto back = s->call("transaction.rollback", json{{"id", id}});
    INFO(back.summary);
    REQUIRE(back.success);
    CHECK(back.result.at("files").at("deleted").size() == 2);
    CHECK_FALSE(fs::exists(project / "assets" / "models" / "tree.glb"));
    CHECK_FALSE(fs::exists(project / "assets" / "manifest.json"));
    CHECK(fs::exists(root / "allowed" / "tree.glb")); // the source is never touched
    fs::remove(glb);
    fs::remove_all(root);
}

TEST_CASE("A rollback keeps a file that existed before, and restores one it replaced", "[api][session][compensation]") {
    const fs::path root = freshDir("comp_existed");
    fs::create_directories(root / "allowed");
    const fs::path glb = testsupport::writeTriangleGlb("session_comp_existed");
    fs::copy_file(glb, root / "allowed" / "tree.glb", fs::copy_options::overwrite_existing);
    app::Engine engine(app::EngineMode::Offline);
    api::LocalHost host(engine);
    auto s = makeSession(host, options({root / "allowed"}, {root / "projects"}));
    const fs::path project = createProject(*s, "E");
    // The target is already there (different bytes, so not a duplicate), and so is a manifest.
    fs::create_directories(project / "assets" / "models");
    std::ofstream(project / "assets" / "models" / "tree.glb") << "an older tree";
    const std::string manifest = R"({"assets":[],"format":"avgen-project-assets","version":1})";
    std::ofstream(project / "assets" / "manifest.json") << manifest;

    const std::string id = s->call("transaction.begin", json{{"label", "import"}}).result.at("id").get<std::string>();
    api::CallOptions in;
    in.transaction = id;
    in.irreversible = api::Irreversible::Accept;
    REQUIRE(s->call("asset.import", json{{"file", (root / "allowed" / "tree.glb").string()}}, in).success);
    const auto back = s->call("transaction.rollback", json{{"id", id}});
    INFO(back.summary);
    REQUIRE(back.success);
    CHECK(fs::exists(project / "assets" / "models" / "tree.glb")); // existed before: never deleted
    CHECK(back.result.at("files").at("deleted").empty());
    CHECK(back.result.at("files").at("restored").size() == 1);
    CHECK(back.result.at("notRestored").at("files").size() == 1); // its old bytes were not kept, and it says so
    std::ifstream in2(project / "assets" / "manifest.json");
    const std::string restored((std::istreambuf_iterator<char>(in2)), std::istreambuf_iterator<char>());
    CHECK(restored == manifest);
    fs::remove(glb);
    fs::remove_all(root);
}

TEST_CASE("A rollback keeps a file the project or the scene names outside the transaction",
          "[api][session][compensation]") {
    const fs::path root = freshDir("comp_referenced");
    fs::create_directories(root / "allowed");
    app::Engine engine(app::EngineMode::Offline);
    api::LocalHost host(engine);
    auto s = makeSession(host, options({root / "allowed"}, {root / "projects"}));
    const fs::path project = createProject(*s, "R");

    SECTION("the project: a save inside the transaction points at the imported track") {
        writeWav(root / "allowed" / "track.wav");
        const std::string id = s->call("transaction.begin", json{{"label", "t"}}).result.at("id").get<std::string>();
        api::CallOptions in;
        in.transaction = id;
        in.irreversible = api::Irreversible::Accept;
        REQUIRE(s->call("asset.import_audio", json{{"file", (root / "allowed" / "track.wav").string()}}, in).success);
        REQUIRE(s->call("project.save", json::object(), in).success);
        const auto back = s->call("transaction.rollback", json{{"id", id}});
        REQUIRE(back.success);
        CHECK(fs::exists(project / "audio" / "track.wav"));
        CHECK(back.result.at("files").at("kept").size() == 1);
    }
    SECTION("the scene: a node that already named the path the import lands on") {
        const fs::path glb = testsupport::writeTriangleGlb("session_comp_referenced");
        fs::copy_file(glb, root / "allowed" / "tree.glb", fs::copy_options::overwrite_existing);
        const json scene{{"format", "avgen-scene"},
                         {"version", scene::Composition::kFormatVersion},
                         {"name", "waiting"},
                         {"nodes", json::array({json{{"name", "tree"}, {"kind", "gltf"},
                                                     {"asset", "assets/models/tree.glb"}}})}};
        std::ofstream(project / "waiting.scene.json") << scene.dump(2);
        REQUIRE(engine.loadComposition(project / "waiting.scene.json").has_value());
        const std::string id = s->call("transaction.begin", json{{"label", "t"}}).result.at("id").get<std::string>();
        api::CallOptions in;
        in.transaction = id;
        in.irreversible = api::Irreversible::Accept;
        REQUIRE(s->call("asset.import", json{{"file", (root / "allowed" / "tree.glb").string()}}, in).success);
        const auto back = s->call("transaction.rollback", json{{"id", id}});
        REQUIRE(back.success);
        CHECK(fs::exists(project / "assets" / "models" / "tree.glb")); // the scene names it
        CHECK_FALSE(fs::exists(project / "assets" / "manifest.json")); // nothing names this one
        fs::remove(glb);
    }
    fs::remove_all(root);
}

TEST_CASE("A rollback keeps a file another transaction depends on", "[api][session][compensation]") {
    const fs::path root = freshDir("comp_shared");
    fs::create_directories(root / "allowed");
    const fs::path glb = testsupport::writeTriangleGlb("session_comp_shared");
    fs::copy_file(glb, root / "allowed" / "tree.glb", fs::copy_options::overwrite_existing);
    app::Engine engineA(app::EngineMode::Offline);
    app::Engine engineB(app::EngineMode::Offline);
    api::LocalHost hostA(engineA);
    api::LocalHost hostB(engineB);
    auto a = makeSession(hostA, options({root / "allowed"}, {root / "projects"}));
    auto b = makeSession(hostB, options({root / "allowed"}, {root / "projects"}));
    const fs::path project = createProject(*a, "Shared");
    REQUIRE(b->call("project.open", json{{"name", "Shared"}}).success);
    const fs::path model = project / "assets" / "models" / "tree.glb";

    api::CallOptions inA;
    inA.transaction = a->call("transaction.begin", json{{"label", "a"}}).result.at("id").get<std::string>();
    inA.irreversible = api::Irreversible::Accept;
    REQUIRE(a->call("asset.import", json{{"file", (root / "allowed" / "tree.glb").string()}}, inA).success);
    api::CallOptions inB;
    inB.transaction = b->call("transaction.begin", json{{"label", "b"}}).result.at("id").get<std::string>();
    inB.irreversible = api::Irreversible::Accept;
    const auto dup = b->call("asset.import", json{{"file", (root / "allowed" / "tree.glb").string()}}, inB);
    REQUIRE(dup.success);
    CHECK(dup.result.at("duplicate") == true);

    SECTION("the other transaction committed: the file is kept") {
        REQUIRE(b->call("transaction.commit", json{{"id", *inB.transaction}}).success);
        const auto back = a->call("transaction.rollback", json{{"id", *inA.transaction}});
        REQUIRE(back.success);
        CHECK(fs::exists(model));
    }
    SECTION("the other transaction is still open: the file is kept") {
        const auto back = a->call("transaction.rollback", json{{"id", *inA.transaction}});
        REQUIRE(back.success);
        CHECK(fs::exists(model));
        REQUIRE(b->call("transaction.rollback", json{{"id", *inB.transaction}}).success);
    }
    SECTION("the other transaction rolled back first: the creator's rollback deletes it") {
        REQUIRE(b->call("transaction.rollback", json{{"id", *inB.transaction}}).success);
        const auto back = a->call("transaction.rollback", json{{"id", *inA.transaction}});
        REQUIRE(back.success);
        CHECK_FALSE(fs::exists(model));
    }
    CHECK_FALSE(api::FileClaims::instance().claimed(fs::weakly_canonical(model).string()));
    fs::remove(glb);
    fs::remove_all(root);
}

TEST_CASE("A rollback never deletes a render output", "[api][session][compensation]") {
    const fs::path root = freshDir("comp_render");
    app::Engine engine(app::EngineMode::Offline);
    api::LocalHost host(engine);
    ai::ToolAnnotations a;
    a.mutatesSession = true;
    a.undoable = false;
    api::SessionOptions o = options({}, {root});
    o.extraTools.push_back(probeTool("test.render", a, [root](const json&, ai::ToolContext& ctx) {
        std::ofstream(root / "frame.png") << "pixels";
        ctx.noteCreatedFile(root / "frame.png", ai::ToolContext::FileRole::RenderOutput);
        std::ofstream(root / "scratch.bin") << "scratch";
        ctx.noteCreatedFile(root / "scratch.bin");
        return ai::ToolResult::ok(json::object(), "rendered");
    }));
    auto s = makeSession(host, std::move(o));
    api::CallOptions in;
    in.transaction = s->call("transaction.begin", json{{"label", "r"}}).result.at("id").get<std::string>();
    in.irreversible = api::Irreversible::Accept;
    REQUIRE(s->call("test.render", json::object(), in).success);
    REQUIRE(s->call("transaction.rollback", json{{"id", *in.transaction}}).success);
    CHECK(fs::exists(root / "frame.png"));
    CHECK_FALSE(fs::exists(root / "scratch.bin"));
    fs::remove_all(root);
}

// ---- threading -------------------------------------------------------------------------------------------------

TEST_CASE("Session::call from another thread waits for the pump; from the host thread it runs inline",
          "[api][session][threading]") {
    app::Engine engine(app::EngineMode::Offline);
    api::LocalHost host(engine);
    std::atomic<std::thread::id> ranOn{};
    api::SessionOptions o = options();
    o.extraTools.push_back(probeTool("test.where", reading(), [&ranOn](const json&, ai::ToolContext&) {
        ranOn.store(std::this_thread::get_id());
        return ai::ToolResult::ok(json::object(), "here");
    }));
    auto s = makeSession(host, std::move(o)); // this thread is the host thread from here

    const auto inlineResult = s->call("test.where", json::object());
    REQUIRE(inlineResult.success);
    CHECK(ranOn.load() == std::this_thread::get_id());

    ranOn.store(std::thread::id{});
    std::atomic<bool> done{false};
    api::CallResult fromClient;
    std::thread client([&] {
        fromClient = s->call("test.where", json::object());
        done.store(true);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds{150});
    CHECK_FALSE(done.load()); // nobody pumped, so it is still waiting
    CHECK(ranOn.load() == std::thread::id{});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (!done.load() && std::chrono::steady_clock::now() < deadline) {
        s->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    client.join();
    REQUIRE(fromClient.success);
    CHECK(ranOn.load() == std::this_thread::get_id());
}

TEST_CASE("Two client threads call one session while the host pumps", "[api][session][threading][tsan]") {
    // The TSan target (ADR-2153): run under the tsan preset, any engine access off the host thread shows up.
    app::Engine engine(app::EngineMode::Offline);
    api::LocalHost host(engine);
    auto s = makeSession(host, options());
    std::atomic<int> finished{0};
    std::atomic<int> failures{0};
    const auto client = [&](float base) {
        for (int i = 0; i < 60; ++i) {
            const auto set = s->call("parameter.set", json{{"path", "orb/scale"}, {"value", base + i * 0.01}});
            const auto get = s->call("parameter.get", json{{"paths", {"orb/scale"}}});
            const auto info = s->call("session.info", json::object());
            if (!set.success || !get.success || !info.success) {
                failures.fetch_add(1);
            }
        }
        finished.fetch_add(1);
    };
    std::thread one(client, 1.0f);
    std::thread two(client, 3.0f);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{60};
    while (finished.load() < 2 && std::chrono::steady_clock::now() < deadline) {
        s->pump();
        std::this_thread::sleep_for(std::chrono::microseconds{200});
    }
    one.join();
    two.join();
    CHECK(finished.load() == 2);
    CHECK(failures.load() == 0);
}

// ---- the live policy -------------------------------------------------------------------------------------------

TEST_CASE("A live host refuses structural, job, project and seek calls while a show runs", "[api][session][live]") {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadFile(helixScene()).has_value());
    FakeHost host(engine);
    api::SessionOptions o = options({}, {fs::temp_directory_path()});
    o.policy = api::HostPolicy::Live;
    o.permissions.subprocess = true;
    auto s = makeSession(host, std::move(o));
    host.show = true;

    for (const auto& [tool, args] : std::vector<std::pair<std::string, json>>{
             {"scene.create_node", json{{"name", "g"}, {"kind", "group"}}},
             {"performance.profile_scene", json::object()},
             {"project.save", json::object()},
             {"sequencer.set_playhead", json{{"seconds", 2.0}}}}) {
        INFO(tool);
        const auto r = s->call(tool, args);
        CHECK(code(r) == "BUSY");
        REQUIRE(r.error);
        CHECK(r.error->details.value("retryAfterMs", 0) == 5000);
        CHECK(r.error->message.find("live show") != std::string::npos);
    }
    CHECK(engine.composition()->findNode("g") == nullptr);
    // Reads, and instant edits, stay available.
    CHECK(code(s->call("parameter.get", json{{"paths", {"scene/volumeDensity"}}})) == "none");
    CHECK(code(s->call("scene.get_summary", json::object())) == "none");
    CHECK(s->call("parameter.set", json{{"path", "scene/volumeDensity"}, {"value", 0.2}}).success);
    const auto info = s->call("session.info", json{{"tools", {"scene.create_node"}}});
    REQUIRE(info.success);
    CHECK(info.result.at("showRunning") == true);
    CHECK(info.result.at("toolDetail").at(0).at("available") == false);

    // The show ends: the same call goes through.
    host.show = false;
    CHECK(s->call("scene.create_node", json{{"name", "g"}, {"kind", "group"}}).success);
}

TEST_CASE("Studio's own assistant is not refused by the live policy", "[api][session][live]") {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadFile(helixScene()).has_value());
    FakeHost host(engine);
    api::SessionOptions o = options({}, {}, api::ClientKind::Studio);
    o.policy = api::HostPolicy::Live;
    auto s = makeSession(host, std::move(o));
    host.show = true;
    CHECK(s->call("scene.create_node", json{{"name", "g"}, {"kind", "group"}}).success);
}

// ---- capability answers -------------------------------------------------------------------------------------------

TEST_CASE("session.info answers supported, available and permitted per tool", "[api][session][capability]") {
    app::Engine engine(app::EngineMode::Offline); // no composition, no renderer, no hooks
    api::LocalHost host(engine);
    auto s = makeSession(host, options()); // subprocess not permitted
    const auto info = s->call("session.info", json{{"tools", {"performance.get_stats", "performance.profile_scene",
                                                              "scene.create_node", "parameter.get", "no.such_tool"}}});
    INFO(info.summary);
    REQUIRE(info.success);
    const json& d = info.result.at("toolDetail");
    REQUIRE(d.size() == 5);
    // The hook is missing: supported and permitted, not available.
    CHECK(d[0].at("supported") == true);
    CHECK(d[0].at("available") == false);
    CHECK(d[0].at("permitted") == true);
    CHECK(d[0].at("unavailableReason").get<std::string>().find("renderer") != std::string::npos);
    // The permissions deny it: supported, not permitted (and its profiler is not installed either).
    CHECK(d[1].at("supported") == true);
    CHECK(d[1].at("permitted") == false);
    CHECK(d[1].at("available") == false);
    // No composition: not available.
    CHECK(d[2].at("available") == false);
    CHECK(d[2].at("unavailableReason") == "no composition is loaded");
    // An ordinary read: all three.
    CHECK(d[3].at("supported") == true);
    CHECK(d[3].at("available") == true);
    CHECK(d[3].at("permitted") == true);
    // Not in the catalogue.
    CHECK(d[4].at("supported") == false);

    // Installing the hook makes it available: the answer comes from what the host has, not from the catalogue.
    host.setPerformanceSource([] { return ai::PerformanceSnapshot{}; });
    CHECK(s->availability("performance.get_stats").available);
    CHECK(info.result.at("version").at("api") == api::kApiLevel);
    CHECK(info.result.at("client").at("kind") == "test");
    CHECK(info.result.at("project").at("state") == "none");
    CHECK(info.result.at("tools").at("supported").get<std::size_t>() == s->tools().size());
}

// ---- the wire envelope ------------------------------------------------------------------------------------------------

TEST_CASE("The wire envelope has every key on every call", "[api][session][wire]") {
    app::Engine engine(app::EngineMode::Offline);
    api::LocalHost host(engine);
    auto s = makeSession(host, options({"/nonexistent-root"}));
    const std::set<std::string> keys = {"success", "result", "summary", "changes", "warnings", "transaction", "job"};
    const auto keysOf = [](const json& j) {
        std::set<std::string> k;
        for (const auto& [name, v] : j.items()) {
            k.insert(name);
        }
        return k;
    };

    const json ok = s->call("parameter.set", json{{"path", "orb/scale"}, {"value", 1.5}}).toWire();
    CHECK(keysOf(ok) == keys);
    CHECK(ok.at("success") == true);
    CHECK(ok.at("result").is_object());
    CHECK(ok.at("changes").is_array());
    CHECK_FALSE(ok.at("changes").empty());
    CHECK(ok.at("changes").at(0).contains("target"));
    CHECK(ok.at("transaction").is_string()); // the implicit transaction it ran in
    CHECK(ok.at("job").is_null());

    const json read = s->call("parameter.get", json{{"paths", {"orb/scale"}}}).toWire();
    CHECK(read.at("transaction").is_null());

    const json bad = s->call("scene.create_node", json{{"name", "x"}, {"asset", "/etc/passwd"}}).toWire();
    std::set<std::string> failureKeys = keys;
    failureKeys.insert("error");
    CHECK(keysOf(bad) == failureKeys);
    CHECK(bad.at("success") == false);
    CHECK(bad.at("result").is_null());
    CHECK(bad.at("error").contains("code"));
    CHECK(bad.at("error").contains("message"));
    CHECK(bad.at("error").contains("recovery"));

    const json unknown = s->call("no.such_tool", json::object()).toWire();
    CHECK(unknown.at("error").at("code") == "NOT_FOUND");
    const json invalid = s->call("parameter.set", json{{"path", 3}}).toWire();
    CHECK(invalid.at("error").at("code") == "INVALID_ARGUMENTS");
}
