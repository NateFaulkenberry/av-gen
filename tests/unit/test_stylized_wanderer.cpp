// The Glowmere Stylized wanderer: whether the numbers its scene authors as stride speeds describe
// the clips they name, and whether its rate matcher covers the speeds it actually travels at.
//
// This is ADR-204 §3 arriving in a second scene. The four Glowmere Valley 2 aliens had their
// `walkSpeed`/`runSpeed` set from "the speed the creature cruises at" rather than from the clip,
// and `Gait::footSlip` could not see it because the authored number is both the expected value and
// half the actual one. The stylized wanderer never had them set at all: its gait block carries
// `matchRate`, `rateMin` and `rateMax` and nothing else, so it walks a 121-unit Mixamo alien scaled
// to 0.08 against `GaitSettings`' person-scale defaults -- a 1.6 m/s walk clip and a 3.0 m/s run
// threshold -- and ADR-198 already recorded what that does: "a six-metre creature is running at a
// speed a person would never reach; left alone, all four would have sprinted from the moment they
// moved."
//
// The expected stride speeds below were measured off the raw glTF accessors by a pure-stdlib
// Python script that shares no code with this engine (the method is `tests/support/stride_speed.hpp`
// and ADR-204 §3): 53.12 model units/s for `Walk` and 114.27 for `Run`, both stable to about 1% as
// the contact threshold moves from the bottom 10% of the toe's height range to the bottom 30%.
// Those two literals are the independent expected values; everything else here is derived from them.

#include "app/engine.hpp"
#include "assets/gltf_loader.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "entity/gait.hpp"
#include "scene/animation.hpp"
#include "scene/composition.hpp"
#include "scene/scene.hpp"
#include "support/stride_speed.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

using namespace avgen;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

// Measured in Python off `assets/imported/alien.gltf`'s accessors; see the file comment.
constexpr float kWalkStrideUnits = 53.12f;
constexpr float kRunStrideUnits = 114.27f;

// Mixamo names its feet differently from the Quaternius pack `test_alien_locomotion.cpp` measures,
// which is the whole reason the estimator takes the joint names rather than knowing them.
constexpr std::array<std::string_view, 2> kToes{{"mixamorig:LeftToeBase", "mixamorig:RightToeBase"}};

fs::path stylizedScene() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-stylized.scene.json";
}
fs::path wandererAsset() {
    return fs::path(AVGEN_SOURCE_DIR) / "assets" / "imported" / "alien.gltf";
}

nlohmann::json sceneJson() {
    std::ifstream in(stylizedScene());
    REQUIRE(in.good());
    return nlohmann::json::parse(in);
}

const nlohmann::json& named(const nlohmann::json& list, const char* name) {
    for (const nlohmann::json& item : list) {
        if (item.value("name", std::string()) == name) {
            return item;
        }
    }
    FAIL("the scene has no entry named " << name);
    return list;
}

scene::Scene loadWanderer() {
    scene::Scene s;
    const auto summary = assets::loadGltf(wandererAsset(), s, {});
    REQUIRE(summary.has_value());
    REQUIRE(s.rigs.size() == 1);
    return s;
}

float strideOf(const scene::SkinnedRig& rig, const char* clipName, float contact = 0.2f) {
    const int index = rig.findClip(clipName);
    REQUIRE(index >= 0);
    return testing::clipStrideSpeed(rig, rig.clips[static_cast<std::size_t>(index)], kToes, contact);
}

// The gait the scene authors, with every field the bands are checked against filled in from the
// scene rather than from `GaitSettings`' defaults where the scene states one.
entity::GaitSettings authoredGait(const nlohmann::json& entity) {
    entity::GaitSettings g;
    const nlohmann::json& gait = entity.at("gait");
    g.matchRate = gait.value("matchRate", g.matchRate);
    g.walkSpeed = gait.value("walkSpeed", g.walkSpeed);
    g.runSpeed = gait.value("runSpeed", g.runSpeed);
    g.rateMin = gait.value("rateMin", g.rateMin);
    g.rateMax = gait.value("rateMax", g.rateMax);
    g.moveEnter = gait.value("moveEnter", g.moveEnter);
    g.moveExit = gait.value("moveExit", g.moveExit);
    g.runEnter = gait.value("runEnter", g.runEnter);
    g.runExit = gait.value("runExit", g.runExit);
    return g;
}

} // namespace

// =================================================================================================
// 1. The instrument
// =================================================================================================

TEST_CASE("the stride estimator recovers the wanderer clips' measured speeds",
          "[wanderer][locomotion][stride]") {
    // Without this every number below is this engine checking itself. The two literals came out of
    // a script that walks the glTF node hierarchy by hand and shares nothing with the loader, the
    // skeleton or the sampler this reads them through.
    const scene::Scene s = loadWanderer();
    const scene::SkinnedRig& rig = s.rigs.front();
    const float walk = strideOf(rig, "Walk");
    const float run = strideOf(rig, "Run");
    INFO("walk " << walk << " units/s, run " << run << " units/s");
    CHECK(walk == Approx(kWalkStrideUnits).epsilon(0.02));
    CHECK(run == Approx(kRunStrideUnits).epsilon(0.02));

    // A property of the take, not of the threshold: the answer barely moves as contact is taken to
    // mean the bottom tenth of the toe's height range or the bottom third.
    for (const float contact : {0.10f, 0.15f, 0.25f, 0.30f}) {
        INFO("contact fraction " << contact);
        CHECK(strideOf(rig, "Walk", contact) == Approx(walk).epsilon(0.03));
        CHECK(strideOf(rig, "Run", contact) == Approx(run).epsilon(0.03));
    }

    // The run covers more ground than the walk, and the idle covers none: the estimator must not
    // invent a stride for a clip that has none.
    CHECK(run > walk * 2.0f);
    CHECK(strideOf(rig, "Idle") < walk * 0.05f);
}

// =================================================================================================
// 2. The claim the scene makes about those clips
// =================================================================================================

TEST_CASE("the stylized wanderer's authored stride speeds describe the clips it names",
          "[wanderer][locomotion][stride]") {
    const nlohmann::json doc = sceneJson();
    const nlohmann::json& node = named(doc.at("nodes"), "wanderer");
    const nlohmann::json& ent = named(doc.at("entities"), "wanderer");
    const auto scale = node.at("scale")[0].get<float>();
    // Uniform, or "times the node's scale" is not a single number.
    CHECK(node.at("scale")[1].get<float>() == scale);
    CHECK(node.at("scale")[2].get<float>() == scale);

    const scene::Scene s = loadWanderer();
    const scene::SkinnedRig& rig = s.rigs.front();
    const std::string walkClip = ent.at("clips").at("walk");
    const std::string runClip = ent.at("clips").at("run");
    const float walk = strideOf(rig, walkClip.c_str()) * scale;
    const float run = strideOf(rig, runClip.c_str()) * scale;

    const nlohmann::json& gait = ent.at("gait");
    REQUIRE(gait.contains("walkSpeed"));
    REQUIRE(gait.contains("runSpeed"));
    const auto authoredWalk = gait.at("walkSpeed").get<float>();
    const auto authoredRun = gait.at("runSpeed").get<float>();
    INFO("scale " << scale << "; authored walk " << authoredWalk << " vs clip " << walk
                  << "; authored run " << authoredRun << " vs clip " << run);
    // 5%, as ADR-204: a twentieth of a stride is inside what reads as a character adjusting its
    // pace, and far outside the -62% and -56% this was written against.
    CHECK(authoredWalk == Approx(walk).epsilon(0.05));
    CHECK(authoredRun == Approx(run).epsilon(0.05));
}

TEST_CASE("rate matching covers the wanderer's whole travelling range",
          "[wanderer][locomotion][gait]") {
    // The clamp is the other half of ADR-204 §3. A `rateMin` that bites inside the band the body
    // travels in is sliding by another name, so the band and the clamp are checked against each
    // other rather than against a number somebody liked.
    const nlohmann::json doc = sceneJson();
    const nlohmann::json& ent = named(doc.at("entities"), "wanderer");
    const entity::GaitSettings g = authoredGait(ent);
    REQUIRE(g.matchRate);

    const nlohmann::json* exploreBehavior = nullptr;
    for (const nlohmann::json& b : ent.at("behaviors")) {
        if (b.value("kind", std::string()) == "explore") {
            exploreBehavior = &b;
        }
    }
    REQUIRE(exploreBehavior != nullptr);
    const auto topRun = exploreBehavior->at("runSpeed").get<float>();

    // The walk band runs from the speed the gait stops calling it standing to the speed it starts
    // calling it running; the run band from there to the fastest the behaviour travels.
    for (const float v : {g.moveExit, (g.moveExit + g.runEnter) * 0.5f, g.runEnter}) {
        INFO("walk at " << v << " m/s");
        CHECK(entity::Gait::footSlip(g, entity::Activity::Walk, v) == Approx(1.0f).epsilon(0.02));
    }
    for (const float v : {g.runExit, (g.runExit + topRun) * 0.5f, topRun}) {
        INFO("run at " << v << " m/s");
        CHECK(entity::Gait::footSlip(g, entity::Activity::Run, v) == Approx(1.0f).epsilon(0.02));
    }
}

TEST_CASE("the wanderer's gait bands are scaled to the wanderer", "[wanderer][locomotion][gait]") {
    // ADR-198: `runEnter` 3.0 and `runExit` 2.2 are person-scale, and "a six-metre creature is
    // running at a speed a person would never reach -- left alone, all four would have sprinted
    // from the moment they moved." This body is nine metres tall and cruises at 5 m/s, so with the
    // defaults it crossed `runEnter` before it had finished accelerating: measured over two
    // simulated minutes, 108 walk frames against 5,633 run frames. The walk clip it carries was
    // effectively never played.
    //
    // What makes that a band error rather than a taste is that it is checkable: the speed the
    // behaviour *cruises* at has to be a walk, or the walk clip is decoration.
    const nlohmann::json doc = sceneJson();
    const nlohmann::json& ent = named(doc.at("entities"), "wanderer");
    const entity::GaitSettings g = authoredGait(ent);
    const nlohmann::json* explore = nullptr;
    for (const nlohmann::json& b : ent.at("behaviors")) {
        if (b.value("kind", std::string()) == "explore") {
            explore = &b;
        }
    }
    REQUIRE(explore != nullptr);
    const auto cruise = explore->at("speed").get<float>();
    const auto dash = explore->at("runSpeed").get<float>();
    INFO("cruise " << cruise << ", dash " << dash << "; bands move " << g.moveExit << "/"
                   << g.moveEnter << ", run " << g.runExit << "/" << g.runEnter);
    CHECK(g.moveEnter < cruise);
    CHECK(g.runEnter > cruise);
    CHECK(g.runEnter < dash);
    // The hysteresis is hysteresis: equal thresholds are the flicker `GaitSettings` exists to stop.
    CHECK(g.moveEnter > g.moveExit);
    CHECK(g.runEnter > g.runExit);
    CHECK(g.runExit > g.moveEnter);
}

// =================================================================================================
// 3. The scene it ships in
// =================================================================================================

TEST_CASE("the wanderer's feet keep step with the ground it crosses",
          "[wanderer][locomotion][integration]") {
    // The measurement the one-shot runtime warning cannot make: it fires once, on whichever frame
    // first offends, and says nothing about how much of the walk is spent out of step.
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(stylizedScene()).has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);

    const entity::Entity* who = nullptr;
    for (const auto& e : comp->entityWorld().entities()) {
        if (e->name() == "wanderer") {
            who = e.get();
        }
    }
    REQUIRE(who != nullptr);
    const scene::CompositionNode* node = comp->findNode("wanderer");
    REQUIRE(node != nullptr);

    // The camera is pinned to the body so nothing is culled or coarsely updated -- an entity that
    // stopped simulating would pass every assertion below by never moving.
    auto* cameraMode = engine.params().findAs<int>("camera/mode");
    auto* cameraPos = engine.params().findAs<glm::vec3>("camera/position");
    auto* cameraTarget = engine.params().findAs<glm::vec3>("camera/target");
    REQUIRE(cameraMode != nullptr);
    REQUIRE(cameraPos != nullptr);
    REQUIRE(cameraTarget != nullptr);
    cameraMode->setBase(1);

    FixedStepClock clock(60.0);
    constexpr int kSteps = 120 * 60; // two simulated minutes
    int locomotor = 0;
    int slipOut = 0;
    float worstSlip = 1.0f;
    int walkFrames = 0;
    int runFrames = 0;
    double travelled = 0.0;
    glm::vec3 last = comp->nodeWorldTransform(*node).position;
    for (int i = 0; i < kSteps; ++i) {
        const glm::vec3 at = comp->nodeWorldTransform(*node).position;
        cameraPos->setBase(at + glm::vec3(0.0f, 12.0f, 24.0f));
        cameraTarget->setBase(at);
        const FrameTime ft = engine.tick(clock);
        engine.update(ft);

        const entity::LocomotionState& loco = who->locomotion();
        const bool moving =
            loco.activity == entity::Activity::Walk || loco.activity == entity::Activity::Run;
        if (moving) {
            ++locomotor;
            (loco.activity == entity::Activity::Walk ? walkFrames : runFrames) += 1;
            const float slip = entity::Gait::footSlip(who->desc().gait, loco.activity, loco.speed);
            if (slip > 1.1f || slip < 1.0f / 1.1f) {
                ++slipOut;
            }
            worstSlip = std::max(worstSlip, std::max(slip, 1.0f / std::max(slip, 1e-4f)));
        }
        const glm::vec3 now = comp->nodeWorldTransform(*node).position;
        travelled += glm::length(glm::vec2(now.x - last.x, now.z - last.z));
        last = now;
    }

    INFO("travelled " << travelled << " m; " << walkFrames << " walk / " << runFrames
                      << " run frames; slip outside 1.1x on " << slipOut << "/" << locomotor
                      << "; worst " << worstSlip << "x");
    // The probe established what it measures (ADR-182): the body went somewhere, and it used both
    // travelling gaits. Without all three the slip count below is vacuous -- and "it never ran" is
    // exactly how the *old* numbers could have been made to look clean.
    REQUIRE(travelled > 40.0);
    REQUIRE(walkFrames > 200);
    REQUIRE(runFrames > 60);
    // The band fix, stated as what it is for: this body cruises, and cruising is a walk. Before
    // the bands were scaled it was 108 walk frames against 5,633 run.
    CHECK(walkFrames > runFrames);

    // 1.5x is not a number chosen here: it is the threshold `Entity::update` warns at, so this
    // asserts the user-visible contract -- the engine never says these feet are out of step.
    CHECK(worstSlip < 1.5f);
    // What is left is ADR-204's own "what this does not fix": `GaitSettings::minDwell` holds a
    // gait for a quarter of a second after the body's speed has left that gait's band, so a body
    // accelerating at 8 m/s^2 through `runEnter` spends up to 0.25 s labelled Walk at up to 10 m/s
    // against a 4.25 m/s clip. Measured at 10 frames of 5,698 and a worst of 1.33x, down from
    // 176 of 5,741 and 6.27x. Bounded rather than zero, because removing it means either a gait
    // that flickers or a minimum dwell that knows about clips.
    CHECK(slipOut * 100 < locomotor);
}
