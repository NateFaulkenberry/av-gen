// Slice 4: directed performances (ADR-766 on the Motion lead's ADR-824). Orders at seconds, applied
// inside the simulation on play and scrub alike; live, so baked only by recording. Played checks run
// from zero with the cull lifted and no audio.

#include "app/directing_apply.hpp"
#include "app/directing_context.hpp"
#include "app/directing_record.hpp"
#include "app/engine.hpp"
#include "directing/compiler.hpp"
#include "directing/plan.hpp"
#include "entity/entity.hpp"
#include "scene/composition.hpp"
#include "ui/edit_history.hpp"

#include "support/project_assets.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numbers>

using namespace avgen;
using namespace avgen::directing;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

void load(app::Engine& engine) {
    REQUIRE(engine.loadProject(fs::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json"));
    scene::DetailLimits limits = engine.detailLimits();
    limits.entityDistanceCull = false;
    engine.setDetailLimits(limits);
    REQUIRE(engine.setAudioClips({}).has_value());
}

// Rook, told at 20 s to face Vane, at 22 s to react, and at 26 s released back to his own devices.
json directedPlan() {
    return json::parse(R"({
      "schemaVersion": 1, "id": "rook-told", "title": "Rook is told what to do", "tier": "directed",
      "subjects": [{"alias": "rook", "text": "Rook"}, {"alias": "vane", "text": "Vane"}],
      "performances": [{"key": "orders", "subject": "rook", "mode": "directed", "beats": [
        {"action": "face", "target": "vane", "at": {"seconds": 20}},
        {"action": "react", "at": {"seconds": 22}},
        {"action": "release", "at": {"seconds": 26}}]}]
    })");
}

Plan planFrom(const json& doc) {
    PlanParse parsed = parsePlan(doc);
    for (const Issue& i : parsed.issues) {
        INFO(i.toJson().dump());
    }
    REQUIRE(parsed.plan);
    return *parsed.plan;
}

bool has(const Validation& v, IssueCode code, std::string_view item) {
    return std::any_of(v.issues.begin(), v.issues.end(), [&](const Issue& i) { return i.code == code && i.item == item; });
}

void frame(app::Engine& engine, std::uint64_t f) {
    engine.update(FrameTime{static_cast<double>(f) / 60.0, f == 0 ? 0.0 : 1.0 / 60.0, f});
}

} // namespace

TEST_CASE("a directed performance compiles to scheduled orders, only in a live plan", "[directing][directed]") {
    testsupport::skipUnlessGlowmereBenchmarkAssetsPresent();
    app::Engine engine(app::EngineMode::Offline);
    load(engine);
    const SceneFacts facts = app::sceneFactsFor(engine);
    const Compilation c = compilePlan(planFrom(directedPlan()), facts);
    INFO(c.diffText());
    REQUIRE_FALSE(c.validation.hasErrors());
    std::vector<const seq::SequenceEvent*> orders;
    for (const seq::SequenceEvent& e : c.staged.sequence.events) {
        if (e.what.kind == seq::EventActionKind::EntityAction) {
            orders.push_back(&e);
        }
    }
    REQUIRE(orders.size() == 3);
    CHECK(orders[0]->when.timeSeconds == 20.0);
    CHECK(orders[0]->what.target == "rook");
    CHECK(orders[0]->what.value == "face");
    CHECK(orders[0]->what.argument == "vane");
    CHECK(orders[1]->what.value == "pose");
    CHECK(orders[1]->what.argument == "react");
    CHECK(orders[2]->what.value == "release");
    CHECK(c.diffText().find("live: carried out by rook its own way") != std::string::npos);

    SECTION("a baked plan refuses it") {
        json doc = directedPlan();
        doc["tier"] = "baked";
        CHECK(has(compilePlan(planFrom(doc), facts).validation, IssueCode::NonDeterministic, "orders"));
    }
    SECTION("what an order cannot be") {
        json doc = directedPlan();
        doc["subjects"].push_back(json{{"alias", "lantern"}, {"text", "the Lantern hero"}, {"hint", "hero"}});
        doc["performances"][0]["beats"].push_back(json{{"action", "face"}, {"target", "lantern"}, {"at", "0:12"}});
        doc["performances"][0]["beats"].push_back(json{{"action", "backflip"}, {"at", "0:13"}});
        doc["performances"][0]["beats"].push_back(json{{"action", "react"}, {"at", "0:14"}, {"emits", "rook.reacts"}});
        doc["performances"][0]["beats"].push_back(json{{"action", "react"}}); // no time
        const Validation v = compilePlan(planFrom(doc), facts).validation;
        CHECK(has(v, IssueCode::Unsupported, "orders"));   // face a place; backflip; an event from a pose
        CHECK(has(v, IssueCode::SchemaInvalid, "orders")); // the untimed order
    }
}

TEST_CASE("orders, played: Rook turns to Vane and reacts; a scrub lands where the play did; a recording bakes it",
          "[directing][directed][benchmark]") {
    testsupport::skipUnlessGlowmereBenchmarkAssetsPresent();
    app::Engine played(app::EngineMode::Offline);
    load(played);
    const Compilation c = compilePlan(planFrom(directedPlan()), app::sceneFactsFor(played));
    REQUIRE_FALSE(c.validation.hasErrors());
    ui::EditHistory history;
    REQUIRE(app::applyCompilation(played, history, c));
    const entity::Entity* rook = played.composition()->entityWorld().find("rook");
    const entity::Entity* tide = played.composition()->entityWorld().find("vane");
    // The same world without the orders: where Rook faces on his own at that moment.
    app::Engine idle(app::EngineMode::Offline);
    load(idle);
    const entity::Entity* idleRook = idle.composition()->entityWorld().find("rook");
    const entity::Entity* idleVane = idle.composition()->entityWorld().find("vane");
    float idleFacingError = 1e9f;
    REQUIRE(rook != nullptr);
    REQUIRE(tide != nullptr);
    float facingError = 1e9f;
    scene::Composition::ClipReadout at8;
    scene::Composition::ClipReadout idleAt23;
    double reactFrom = -1.0;
    double idleReactFrom = -1.0;
    const auto facing = [](const entity::Entity& a, const entity::Entity& b) {
        const glm::vec3 p = a.state().position();
        const glm::vec3 q = b.state().position();
        return std::abs(std::remainder(a.state().yaw - std::atan2(q.x - p.x, q.z - p.z), 2.0f * std::numbers::pi_v<float>));
    };
    for (std::uint64_t f = 0; f <= 24 * 60; ++f) {
        frame(played, f);
        frame(idle, f);
        if (f >= 20 * 60 && f < 22 * 60) { // between the face order and the react order
            facingError = std::min(facingError, facing(*rook, *tide));
            idleFacingError = std::min(idleFacingError, facing(*idleRook, *idleVane));
        }
        // When the rig starts playing the react clip after the face order: the first frame it says so.
        // (Not `startSeconds`, which is the clip's phase origin and moves with the clip's speed.)
        const double now = static_cast<double>(f) / 60.0;
        if (f > 20 * 60 && reactFrom < 0.0 && played.composition()->clipReadout("rook", now).state == "Crazy") {
            reactFrom = now;
        }
        if (f > 20 * 60 && idleReactFrom < 0.0 && idle.composition()->clipReadout("rook", now).state == "Crazy") {
            idleReactFrom = now;
        }
        if (f == 23 * 60) {
            at8 = played.composition()->clipReadout("rook", 23.0); // the rig's own account of what it plays
            idleAt23 = idle.composition()->clipReadout("rook", 23.0);
        }
    }
    INFO("closest to facing Vane between the orders " << facingError << " rad (on his own " << idleFacingError
                                           << "); the rig starts Crazy at " << reactFrom << " s (on his own at "
                                           << idleReactFrom << " s); at 23 s it plays '" << at8.state
                                           << "' (on his own '" << idleAt23.state << "')");
    CHECK(facingError < 0.2f);
    CHECK(idleFacingError > 0.5f);      // ...which he would not have done on his own
    CHECK(at8.state == "Crazy");   // Rook's react clip (the card maps react -> Crazy)
    CHECK(reactFrom >= 22.0);      // ...from the order's second
    CHECK(reactFrom < 22.1);
    const bool idleReactsThen = idleReactFrom >= 22.0 && idleReactFrom < 22.1;
    CHECK_FALSE(idleReactsThen);   // on his own he does not start reacting then

    // A scrub to 24 s lands where the play did (ADR-824: orders applied inside the simulation).
    app::Engine scrubbed(app::EngineMode::Offline);
    load(scrubbed);
    ui::EditHistory h2;
    REQUIRE(app::applyCompilation(scrubbed, h2, compilePlan(planFrom(directedPlan()), app::sceneFactsFor(scrubbed))));
    frame(scrubbed, 0);
    scrubbed.seekSeconds(24.0);
    frame(scrubbed, 24 * 60 + 1);
    frame(played, 24 * 60 + 1);
    const glm::vec3 p = played.composition()->entityWorld().find("rook")->visualPosition();
    const glm::vec3 s = scrubbed.composition()->entityWorld().find("rook")->visualPosition();
    CHECK(glm::length(p - s) == 0.0f);

    // And a recording bakes it: a scripted actor over the orders' span, played back exactly.
    auto report = app::recordLivePerformances(played, c, app::RecordOptions{});
    REQUIRE(report.has_value());
    for (const std::string& n : report->notes) {
        UNSCOPED_INFO(n);
    }
    CHECK(report->plan.tier == Tier::Baked);
    REQUIRE(report->plan.performances[0].recording.has_value());
    CHECK(report->replayWorstMetres < 0.01);
    CHECK(report->scrubWorstMetres == 0.0);
}
