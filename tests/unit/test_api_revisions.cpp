// ADR-2154 (Engine API stage S1b): project revisions -- "changed since the last save" without serialising --
// and the time policy -- what the world clock does when nothing is playing.
//
// Everything here drives a real `app::Engine`, through a real `api::Session` where the contract is the
// Session's (bumps on commit, restore on rollback, the new tools) and directly where it is the engine's
// (`Engine::tick` under each policy).

#include "ai/tool_api.hpp"
#include "ai/tool_context.hpp"
#include "api/session.hpp"
#include "app/ai_edit_sink.hpp"
#include "app/edit_system.hpp"
#include "app/engine.hpp"
#include "scene/composition.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <string>
#include <unistd.h>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

fs::path helixScene() { return fs::path(AVGEN_SOURCE_DIR) / "examples" / "helix" / "helix.scene.json"; }

fs::path freshDir(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("avgen_revisions_" + name + "_" + std::to_string(getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::unique_ptr<api::Session> makeSession(api::Host& host, std::vector<fs::path> write = {},
                                          std::vector<ai::Tool> extra = {}) {
    api::SessionOptions o;
    o.client = api::ClientInfo{api::ClientKind::Test, "test", "1"};
    o.permissions.readRoots = write;
    o.permissions.writeRoots = std::move(write);
    o.extraTools = std::move(extra);
    auto s = api::Session::create(std::move(o), host);
    REQUIRE(s.has_value());
    (*s)->pump(); // binds this thread as the host thread, so calls from it run inline
    return std::move(*s);
}

std::string code(const api::CallResult& r) {
    return r.error ? std::string(ai::toolErrorCodeName(r.error->code)) : std::string();
}

// A helix project saved to disk, so there is a document to lose.
struct SavedProject {
    fs::path dir;
    fs::path file;
    app::Engine engine{app::EngineMode::Offline};
    explicit SavedProject(const std::string& name) : dir(freshDir(name)), file(dir / "project.json") {
        REQUIRE(engine.loadFile(helixScene()).has_value());
        REQUIRE(engine.saveProject(file).has_value());
        REQUIRE_FALSE(engine.revisions().dirty());
    }
};

bool sessionDirty(api::Session& s) {
    const auto info = s.call("session.info", json::object());
    REQUIRE(info.success);
    return info.result.at("revisions").at("dirty").get<bool>();
}

std::vector<float> finals(const app::Engine& engine) {
    std::vector<float> out;
    for (const params::IParameter* p : const_cast<app::Engine&>(engine).params().ordered()) {
        if (p == nullptr) {
            continue;
        }
        for (std::size_t i = 0; i < p->componentCount(); ++i) {
            out.push_back(p->finalComponent(i));
        }
    }
    return out;
}

} // namespace

TEST_CASE("A parameter edit makes the project dirty; saving makes it clean", "[api][revisions]") {
    SavedProject p("edit_save");
    api::LocalHost host(p.engine);
    auto s = makeSession(host, {p.dir});
    CHECK_FALSE(sessionDirty(*s));

    const std::uint64_t before = p.engine.revisions().project;
    const auto set = s->call("parameter.set", json{{"path", "scene/volumeDensity"}, {"value", 0.61}});
    REQUIRE(set.success);
    CHECK(p.engine.revisions().project != before);
    CHECK(sessionDirty(*s));

    // A read bumps nothing.
    const std::uint64_t afterEdit = p.engine.revisions().project;
    REQUIRE(s->call("parameter.get", json{{"paths", {"scene/volumeDensity"}}}).success);
    CHECK(p.engine.revisions().project == afterEdit);

    const auto saved = s->call("project.save", json::object());
    INFO(saved.summary);
    REQUIRE(saved.success);
    CHECK_FALSE(sessionDirty(*s)); // and the save was not counted as an edit of its own
}

TEST_CASE("A failed call does not bump", "[api][revisions]") {
    SavedProject p("failed");
    ai::ToolAnnotations a;
    a.mutatesProject = true;
    a.undoable = true;
    ai::Tool half;
    half.definition.name = "test.half_then_fail";
    half.definition.title = "half";
    half.definition.description = "writes, then fails";
    half.definition.inputSchema = ai::schema::object({});
    half.definition.annotations = a;
    half.execute = [](const json&, ai::ToolContext& ctx) {
        ctx.engine().params().find("scene/volumeDensity")->setBaseComponent(0, 0.9f);
        ctx.engine().bumpProjectRevision(); // even a writer that bumps is put back by the rollback
        return ai::ToolResult::failure(ai::ToolErrorCode::Internal, "failed half-way");
    };
    api::LocalHost host(p.engine);
    std::vector<ai::Tool> extra;
    extra.push_back(std::move(half));
    auto s = makeSession(host, {p.dir}, std::move(extra));
    const app::ProjectRevisions before = p.engine.revisions();
    CHECK_FALSE(s->call("test.half_then_fail", json::object()).success);
    CHECK(p.engine.revisions() == before);
    // An invalid call never reaches the host.
    CHECK_FALSE(s->call("parameter.set", json{{"path", 3}}).success);
    CHECK(p.engine.revisions() == before);
}

TEST_CASE("An edit set back to its origin stays dirty until diff_saved clears it", "[api][revisions]") {
    SavedProject p("set_back");
    api::LocalHost host(p.engine);
    auto s = makeSession(host, {p.dir});
    const double origin = p.engine.params().find("scene/volumeDensity")->baseComponent(0);
    REQUIRE(s->call("parameter.set", json{{"path", "scene/volumeDensity"}, {"value", origin + 0.25}}).success);
    REQUIRE(s->call("parameter.set", json{{"path", "scene/volumeDensity"}, {"value", origin}}).success);
    // The counters' one false positive: something authored happened, and a save would write the same bytes.
    CHECK(sessionDirty(*s));

    SECTION("diff_saved finds nothing and clears it") {
        const auto diff = s->call("project.diff_saved", json::object());
        INFO(diff.summary);
        REQUIRE(diff.success);
        CHECK(diff.result.at("compared").get<bool>());
        CHECK_FALSE(diff.result.at("differs").get<bool>());
        CHECK(diff.result.at("cleared").get<bool>());
        CHECK_FALSE(diff.result.at("revisions").at("dirty").get<bool>());
        CHECK_FALSE(sessionDirty(*s));
    }

    SECTION("the control: a real difference stays dirty") {
        REQUIRE(s->call("parameter.set", json{{"path", "scene/volumeDensity"}, {"value", origin + 0.5}}).success);
        const auto diff = s->call("project.diff_saved", json::object());
        REQUIRE(diff.success);
        CHECK(diff.result.at("differs").get<bool>());
        CHECK_FALSE(diff.result.at("cleared").get<bool>());
        CHECK_FALSE(diff.result.at("unrecorded").get<bool>());
        CHECK(diff.result.at("pathCount").get<std::size_t>() >= 1);
        CHECK(sessionDirty(*s));
    }
}

TEST_CASE("diff_saved reports a write no revision recorded, and leaves it", "[api][revisions]") {
    SavedProject p("unrecorded");
    api::LocalHost host(p.engine);
    auto s = makeSession(host, {p.dir});
    // An editor write that bypassed the edit history and the Session: the dangerous direction.
    p.engine.params().find("scene/volumeDensity")->setBaseComponent(0, 0.123f);
    CHECK_FALSE(sessionDirty(*s));
    const auto diff = s->call("project.diff_saved", json::object());
    REQUIRE(diff.success);
    CHECK(diff.result.at("unrecorded").get<bool>());
    bool named = false;
    for (const json& path : diff.result.at("paths")) {
        named = named || path.get<std::string>().find("volumeDensity") != std::string::npos;
    }
    CHECK(named);
    // Reported, not decided: a client is told; the close gate decides (Engine::projectDirtyOnDemand).
    CHECK_FALSE(sessionDirty(*s));
    CHECK(p.engine.projectDirtyOnDemand(/*touchedSinceSave=*/true));
}

TEST_CASE("A rolled-back transaction leaves the project as clean or dirty as it was", "[api][revisions][transaction]") {
    SavedProject p("rollback");
    api::LocalHost host(p.engine);
    auto s = makeSession(host, {p.dir});
    const auto run = [&](bool commit) {
        const auto begun = s->call("transaction.begin", json{{"label", "try"}});
        REQUIRE(begun.success);
        api::CallOptions in;
        in.transaction = begun.result.at("id").get<std::string>();
        const std::uint64_t atBegin = p.engine.revisions().project;
        REQUIRE(s->call("parameter.set", json{{"path", "scene/volumeDensity"}, {"value", 0.4}}, in).success);
        REQUIRE(s->call("scene.create_node", json{{"name", "maybe"}, {"kind", "group"}}, in).success);
        // The calls inside do not bump individually...
        CHECK(p.engine.revisions().project == atBegin);
        const auto end = s->call(commit ? "transaction.commit" : "transaction.rollback", json{{"id", *in.transaction}});
        REQUIRE(end.success);
        return end;
    };

    SECTION("clean before, clean after a rollback") {
        const auto back = run(false);
        CHECK_FALSE(back.result.at("revisions").at("dirty").get<bool>());
        CHECK_FALSE(sessionDirty(*s));
    }
    SECTION("...and once at commit") {
        const app::ProjectRevisions before = p.engine.revisions();
        const auto done = run(true);
        CHECK(done.result.at("revisions").at("dirty").get<bool>());
        CHECK(p.engine.revisions().project != before.project);
        CHECK(p.engine.revisions().scene != before.scene); // it created a node
    }
    SECTION("dirty before, dirty after a rollback") {
        REQUIRE(s->call("parameter.set", json{{"path", "scene/volumeDensity"}, {"value", 0.2}}).success);
        const app::ProjectRevisions before = p.engine.revisions();
        static_cast<void>(run(false));
        CHECK(p.engine.revisions() == before);
        CHECK(sessionDirty(*s));
    }
}

TEST_CASE("An entity world played for 300 frames stays clean", "[api][revisions][time]") {
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json";
    if (!fs::exists(project)) {
        SKIP("Glowmere Valley 2 Multi-Camera is not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(project).has_value());
    REQUIRE(engine.composition() != nullptr);
    engine.setViewport(1920, 1080);
    api::LocalHost host(engine);
    host.setOfferedTimePolicies({app::TimePolicy::Offline, app::TimePolicy::Stepped});
    auto s = makeSession(host);
    REQUIRE(s->call("transport.set_policy", json{{"policy", "stepped"}}).success);
    // Played, not seeked: the entities walk, the world effects write their parameters back.
    const auto stepped = s->call("transport.step", json{{"frames", 300}, {"fps", 30.0}});
    INFO(stepped.summary);
    REQUIRE(stepped.success);
    CHECK_THAT(engine.lastRenderTime(), WithinAbs(10.0, 1e-9));
    CHECK_FALSE(sessionDirty(*s));
    // The control (ADR-182): an authored change in the same session is seen.
    REQUIRE(s->call("parameter.set", json{{"path", "camera/fov"}, {"value", 61.0}}).success);
    CHECK(sessionDirty(*s));
}

TEST_CASE("Studio's history sink: a Session call bumps once, and undo back to the save is clean",
          "[api][revisions][edits]") {
    SavedProject p("history");
    app::EditSystem edits;
    edits.attach(&p.engine);
    api::LocalHost host(p.engine);
    auto s = makeSession(host, {p.dir});
    app::EditHistoryTransactionSink sink(p.engine, edits, s->transactionSink());
    REQUIRE(s->setTransactionSink(&sink).has_value());

    const std::uint64_t before = p.engine.revisions().project;
    REQUIRE(s->call("parameter.set", json{{"path", "scene/volumeDensity"}, {"value", 0.55}}).success);
    REQUIRE(edits.history().canUndo()); // the sink recorded it as one undoable edit
    CHECK(p.engine.revisions().dirty());
    CHECK(p.engine.revisions().project != before);

    REQUIRE(edits.execute(app::EditAction::Undo, p.engine));
    CHECK_FALSE(p.engine.revisions().dirty());
    REQUIRE(edits.execute(app::EditAction::Redo, p.engine));
    CHECK(p.engine.revisions().dirty());
    REQUIRE(s->setTransactionSink(nullptr).has_value());
}

// ---- the time policy ----------------------------------------------------------------------------------

TEST_CASE("Under StoppedFrozen, parked ticks hold renderTime and every parameter final", "[api][time]") {
    app::Engine engine(app::EngineMode::Live);
    REQUIRE(engine.loadFile(helixScene()).has_value());
    FixedStepClock clock(60.0);
    const auto tickAndUpdate = [&] {
        const FrameTime t = engine.tick(clock);
        engine.update(t);
        return t;
    };
    for (int i = 0; i < 30; ++i) {
        static_cast<void>(tickAndUpdate()); // a world that has been running a while
    }
    CHECK(engine.timePolicy() == app::TimePolicy::StoppedAmbient); // Studio's default, unchanged
    REQUIRE_FALSE(engine.transport().isPlaying());

    SECTION("frozen: 100 parked ticks, one time, one set of finals") {
        REQUIRE(engine.setTimePolicy(app::TimePolicy::StoppedFrozen).has_value());
        CHECK(engine.effectiveTimePolicy() == app::TimePolicy::StoppedFrozen);
        const FrameTime first = tickAndUpdate();
        CHECK(first.deltaTime == 0.0);
        const std::vector<float> held = finals(engine);
        for (int i = 0; i < 100; ++i) {
            const FrameTime t = tickAndUpdate();
            CHECK(t.renderTime == first.renderTime);
            CHECK(t.deltaTime == 0.0);
        }
        CHECK(finals(engine) == held);
        // Back to ambient resumes from the held time: no jump by the time spent frozen.
        REQUIRE(engine.setTimePolicy(app::TimePolicy::StoppedAmbient).has_value());
        const FrameTime resumed = tickAndUpdate();
        CHECK(resumed.renderTime > first.renderTime);
        CHECK_THAT(resumed.renderTime - first.renderTime, WithinAbs(1.0 / 60.0, 1e-9));
    }
    SECTION("the control: ambient keeps the clock moving while parked") {
        const FrameTime a = tickAndUpdate();
        const FrameTime b = tickAndUpdate();
        CHECK(b.renderTime > a.renderTime);
        CHECK(b.deltaTime > 0.0);
    }
}

TEST_CASE("StoppedAmbient is the clock's own time, exactly as before", "[api][time]") {
    // The policy that is Studio's default must be the old code path: renderTime is whatever the clock
    // produced while parked, frame for frame.
    app::Engine engine(app::EngineMode::Live);
    FixedStepClock clock(60.0);
    FixedStepClock reference(60.0);
    for (int i = 0; i < 120; ++i) {
        const FrameTime t = engine.tick(clock);
        const FrameTime r = reference.tick();
        CHECK(t.renderTime == r.renderTime);
        CHECK(t.deltaTime == r.deltaTime);
        engine.update(t);
    }
    CHECK(engine.effectiveTimePolicy() == app::TimePolicy::StoppedAmbient);
}

TEST_CASE("Stepped advances exactly n/fps per transport.step, and only when stepped", "[api][time]") {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadFile(helixScene()).has_value());
    api::LocalHost host(engine);

    SECTION("a host that does not offer it refuses it") {
        auto s = makeSession(host); // a headless host: only its own Offline policy
        const auto r = s->call("transport.set_policy", json{{"policy", "stepped"}});
        CHECK(code(r) == "FAILED_PRECONDITION");
        CHECK(code(s->call("transport.step", json{{"frames", 1}})) == "FAILED_PRECONDITION");
        const auto policy = s->call("transport.get_policy", json::object());
        REQUIRE(policy.success);
        CHECK(policy.result.at("policy") == "offline");
        CHECK(policy.result.at("offered") == json::array({"offline"}));
    }

    SECTION("stepping") {
        host.setOfferedTimePolicies({app::TimePolicy::Offline, app::TimePolicy::Stepped});
        auto s = makeSession(host);
        REQUIRE(s->call("transport.set_policy", json{{"policy", "stepped"}}).success);
        CHECK(code(s->call("transport.set_policy", json{{"policy", "stopped-frozen"}})) == "FAILED_PRECONDITION");
        const double t0 = engine.lastRenderTime();
        auto r = s->call("transport.step", json{{"frames", 30}, {"fps", 30.0}});
        INFO(r.summary);
        REQUIRE(r.success);
        CHECK_THAT(engine.lastRenderTime() - t0, WithinAbs(1.0, 1e-12));
        CHECK_THAT(engine.transport().positionSeconds() - t0, WithinAbs(1.0, 1e-12)); // played: the transport too
        r = s->call("transport.step", json{{"seconds", 0.5}, {"fps", 24.0}});
        REQUIRE(r.success);
        CHECK(r.result.at("stepped") == 12);
        CHECK_THAT(engine.lastRenderTime() - t0, WithinAbs(1.5, 1e-12));
        r = s->call("transport.step", json{{"frames", 7}, {"fps", 60.0}});
        REQUIRE(r.success);
        CHECK_THAT(engine.lastRenderTime() - t0, WithinAbs(1.5 + 7.0 / 60.0, 1e-12));
        CHECK(code(s->call("transport.step", json{{"frames", 1}, {"seconds", 1.0}})) == "INVALID_ARGUMENTS");
        const auto info = s->call("session.info", json::object());
        REQUIRE(info.success);
        CHECK(info.result.at("timePolicy").at("policy") == "stepped");
    }

    SECTION("a live engine under Stepped does not advance on its own") {
        app::Engine live(app::EngineMode::Live);
        REQUIRE(live.setTimePolicy(app::TimePolicy::Stepped).has_value());
        FixedStepClock clock(60.0);
        const FrameTime a = live.tick(clock);
        for (int i = 0; i < 10; ++i) {
            CHECK(live.tick(clock).renderTime == a.renderTime);
        }
        REQUIRE(live.stepFrames(6, 60.0).has_value());
        CHECK_THAT(live.lastRenderTime() - a.renderTime, WithinAbs(0.1, 1e-12));
        CHECK(live.tick(clock).renderTime == live.lastRenderTime());
    }
}
