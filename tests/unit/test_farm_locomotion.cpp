// The farm animals' locomotion: whether what is drawn agrees with what the body does (ADR-240).
//
// `test_farm_animals.cpp` checks that the pack imported -- rigs, clips, geometry, scale.  This file
// checks the two claims that the import cannot make and that nothing else was checking:
//
//   1. **An authored stride speed is a claim about a clip.** ADR-213 scaled nine plausible
//      real-animal speeds by the world's 3.6x factor and said so plainly: "the animals' authored
//      stride speeds were never measured against their clips ... that is the next honest step, and
//      it is not this one."  It is this one.  `Gait::footSlip` cannot make the check -- with rate
//      matching on and unsaturated it returns `speed / (authored * (speed / authored))` = 1.0
//      whatever the clip contains, because the authored number is both the expected value and the
//      thing under test (ADR-204 §3, ADR-226).
//
//   2. **A body walks the way it is drawn facing.**  ADR-204 established that for the aliens and
//      left one branch uncovered: ADR-162's walk back onto the navigable set translated the body
//      along an escape vector while leaving `yaw` alone, and wrote `speed` and `Activity::Walk`
//      while it did.
//
// The estimator is `tests/support/stride_speed.hpp`, shared with the alien and the stylized
// wanderer.  Its expected values here were produced by a pure-stdlib Python script that walks the
// glTF node hierarchy by hand and shares no line with this engine -- the loader, the skeleton and
// the sampler the C++ reads them through are all independent of it.

#include "app/engine.hpp"
#include "assets/gltf_loader.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "entity/gait.hpp"
#include "entity/navigation.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/animation.hpp"
#include "scene/composition.hpp"
#include "scene/detail_limits.hpp"
#include "scene/scene.hpp"
#include "scene/skeleton.hpp"
#include "signals/signal_bus.hpp"
#include "stage/staging.hpp"
#include "support/stride_speed.hpp"
#include "world/world_map.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <catch2/catch_approx.hpp>
#include "support/ramp.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace avgen;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

// ---- the pack ----------------------------------------------------------------------------------

struct Species {
    const char* name;
    // The joints the estimator names as feet.  Read out of the GLBs rather than guessed: each rig
    // names them differently, which is why `clipStrideSpeed` takes them as an argument.
    std::array<std::string_view, 4> toes;
    std::size_t toeCount;
    // Model units per second, measured off the raw glTF accessors in Python at a 20% contact
    // fraction on the take's own 30 fps grid.  **Not produced by any code in this repository.**
    float strideSpeed;
};

constexpr std::array<Species, 9> kFarm{{
    {"bull", {"HoofB.L", "HoofB.R", "HoofF.L", "HoofF.R"}, 4, 1.6688f},
    {"cow", {"HoofB.L", "HoofB.R", "HoofF.L", "HoofF.R"}, 4, 1.5527f},
    {"horse", {"HoofB.L", "HoofB.R", "HoofF.L", "HoofF.R"}, 4, 1.6277f},
    {"sheep", {"FootB.L", "FootB.R", "FootF01.L", "FootF01.R"}, 4, 0.6894f},
    {"pig", {"FootB.L", "FootB.R", "FootF01.L", "FootF01.R"}, 4, 0.7903f},
    {"goat", {"FootB.L", "FootB.R", "FootF01.L", "FootF01.R"}, 4, 0.5831f},
    {"rooster", {"Foot.L", "Foot.R", "", ""}, 2, 0.4972f},
    {"chicken", {"Foot.L", "Foot.R", "", ""}, 2, 0.5652f},
    {"chick", {"Foot.L", "Foot.R", "", ""}, 2, 0.1789f},
}};

fs::path farmDir() { return fs::path(AVGEN_SOURCE_DIR) / "assets" / "farm"; }
fs::path valleyScene() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2.scene.json";
}
bool farmPresent() { return fs::exists(farmDir() / "cow.glb"); }

scene::Scene loadAnimal(const char* name) {
    scene::Scene s;
    const auto summary = assets::loadGltf(farmDir() / (std::string(name) + ".glb"), s, {});
    REQUIRE(summary.has_value());
    REQUIRE(s.rigs.size() == 1);
    return s;
}

float strideSpeedOf(const scene::SkinnedRig& rig, const Species& species,
                    float contactFraction = 0.2f) {
    const int walk = rig.findClip("Walk");
    REQUIRE(walk >= 0);
    return testing::clipStrideSpeed(rig, rig.clips[static_cast<std::size_t>(walk)],
                                    std::span<const std::string_view>(species.toes.data(),
                                                                      species.toeCount),
                                    contactFraction);
}

nlohmann::json sceneJson() {
    std::ifstream in(valleyScene());
    REQUIRE(in.good());
    return nlohmann::json::parse(in);
}

const nlohmann::json* findNamed(const nlohmann::json& list, std::string_view name) {
    for (const nlohmann::json& item : list) {
        if (item.value("name", std::string()) == name) {
            return &item;
        }
    }
    return nullptr;
}

// A scene node's scale, found wherever the node sits in the tree.
bool nodeScale(const nlohmann::json& nodes, std::string_view name, float& out) {
    for (const nlohmann::json& n : nodes) {
        if (n.value("name", std::string()) == name) {
            out = n.contains("scale") ? n.at("scale")[0].get<float>() : 1.0f;
            return true;
        }
        if (n.contains("children") && nodeScale(n.at("children"), name, out)) {
            return true;
        }
    }
    return false;
}

// Every farm entity in the shipped scene, with the numbers this file measures against.
struct Placed {
    std::string name;
    std::string species;
    float scale = 1.0f;
    float walkSpeed = 0.0f;
    float runSpeed = 0.0f;
    float cruise = 0.0f;   // the wander's own travel speed
    float moveExit = 0.0f;
    float turnRate = 0.0f; // rad/s
    float pauseMax = 0.0f;
    entity::GaitSettings gait;
};

std::vector<Placed> placedFarm(const nlohmann::json& doc) {
    std::vector<Placed> out;
    for (const nlohmann::json& e : doc.at("entities")) {
        if (!e.contains("tags")) {
            continue;
        }
        bool farm = false;
        std::string species;
        for (const nlohmann::json& t : e.at("tags")) {
            const std::string tag = t.get<std::string>();
            if (tag == "farm") {
                farm = true;
            } else if (tag != "animal") {
                species = tag;
            }
        }
        if (!farm) {
            continue;
        }
        Placed p;
        p.name = e.at("name").get<std::string>();
        p.species = species;
        REQUIRE(nodeScale(doc.at("nodes"), e.at("node").get<std::string>(), p.scale));
        const nlohmann::json& g = e.at("gait");
        p.gait.matchRate = g.value("matchRate", false);
        p.gait.walkSpeed = g.at("walkSpeed").get<float>();
        p.gait.runSpeed = g.at("runSpeed").get<float>();
        p.gait.rateMin = g.at("rateMin").get<float>();
        p.gait.rateMax = g.at("rateMax").get<float>();
        p.gait.moveEnter = g.at("moveEnter").get<float>();
        p.gait.moveExit = g.at("moveExit").get<float>();
        p.gait.runEnter = g.at("runEnter").get<float>();
        p.gait.runExit = g.at("runExit").get<float>();
        p.gait.idleRate = g.value("idleRate", 1.0f);
        p.walkSpeed = p.gait.walkSpeed;
        p.runSpeed = p.gait.runSpeed;
        p.moveExit = p.gait.moveExit;
        for (const nlohmann::json& b : e.at("behaviors")) {
            if (b.value("kind", std::string()) != "wander") {
                continue;
            }
            p.cruise = b.at("speed").get<float>();
            p.turnRate = b.at("turnRate").get<float>() / 57.2957795f;
            p.pauseMax = b.at("pauseMax").get<float>();
        }
        out.push_back(std::move(p));
    }
    return out;
}

const Species& speciesNamed(std::string_view name) {
    for (const Species& s : kFarm) {
        if (name == s.name) {
            return s;
        }
    }
    FAIL("no such species: " << name);
    return kFarm.front();
}

// ---- the entity harness ------------------------------------------------------------------------

entity::NodeBinding binding(const std::string& node, glm::vec3 anchor) {
    entity::NodeBinding b;
    b.node = node;
    b.exists = true;
    b.transformPrefix = "nodes/" + node + "/";
    b.anchor = anchor;
    return b;
}

void registerNode(params::ParameterSet& params, const std::string& node) {
    params.add(params::ParamDesc<glm::vec3>{.path = "nodes/" + node + "/position",
                                            .defaultValue = glm::vec3(0.0f),
                                            .hardMin = glm::vec3(-1e4f),
                                            .hardMax = glm::vec3(1e4f)});
    params.add(params::ParamDesc<glm::vec3>{.path = "nodes/" + node + "/rotation",
                                            .defaultValue = glm::vec3(0.0f),
                                            .hardMin = glm::vec3(-360.0f),
                                            .hardMax = glm::vec3(360.0f)});
    params.add(params::ParamDesc<glm::vec3>{.path = "nodes/" + node + "/scale",
                                            .defaultValue = glm::vec3(1.0f),
                                            .hardMin = glm::vec3(0.001f),
                                            .hardMax = glm::vec3(100.0f)});
}

entity::BehaviorDesc behaviorDesc(const char* kind, nlohmann::json settings) {
    entity::BehaviorDesc d;
    d.kind = kind;
    d.name = kind;
    settings["kind"] = kind;
    d.settings = std::move(settings);
    return d;
}

} // namespace

// =================================================================================================
// 1. The instrument
// =================================================================================================

TEST_CASE("the stride estimator recovers the farm clips' measured speeds",
          "[farm][locomotion][stride]") {
    if (!farmPresent()) {
        SKIP("assets/farm is not present");
    }
    // Without this every number below would be this engine agreeing with itself (ADR-182).  The
    // expected values came out of a Python script that parses the GLB, walks the node hierarchy and
    // samples the animation channels itself.
    for (const Species& s : kFarm) {
        INFO(s.name);
        const scene::Scene scn = loadAnimal(s.name);
        const scene::SkinnedRig& rig = scn.rigs.front();
        // The feet this rig names really are on it.  A misspelled joint makes the estimator return
        // 0 and every comparison below vacuous.
        for (std::size_t i = 0; i < s.toeCount; ++i) {
            INFO(s.toes[i]);
            CHECK(rig.skeleton.find(s.toes[i]) >= 0);
        }
        const float measured = strideSpeedOf(rig, s);
        INFO("C++ " << measured << " vs Python " << s.strideSpeed);
        CHECK(measured == Approx(s.strideSpeed).epsilon(0.02));
    }
}

TEST_CASE("the farm clips have a stance, and how well defined it is", "[farm][locomotion][stride]") {
    if (!farmPresent()) {
        SKIP("assets/farm is not present");
    }
    // The alien pack's estimate is stable to half a percent from a 10% contact threshold to a 30%
    // one, and this pack's is not: these are five-to-fourteen-key Bezier cycles sampled onto 25-28
    // frames, and the planted foot is not held at a constant speed through its stance.  That is a
    // property of the clips and it has to be *stated* rather than asserted away, because it is the
    // uncertainty every authored number in this file inherits.
    //
    // What is asserted is the part that is real: every one of the nine has a stance -- a stretch
    // where a foot is down and moving backward -- and the estimate does not move by more than a
    // factor of two across the whole threshold sweep.  Measured worst: the bull at 2.22 (10%) to
    // 1.31 (30%), a factor of 1.70.  The errors this replaced ran from 0.33x to 2.2x, so a number
    // good to a factor of 1.7 in the worst case is not a tolerance anybody is hiding behind.
    float worstSpread = 1.0f;
    std::string worstAnimal;
    for (const Species& s : kFarm) {
        INFO(s.name);
        const scene::Scene scn = loadAnimal(s.name);
        const scene::SkinnedRig& rig = scn.rigs.front();
        float lo = 1e9f;
        float hi = 0.0f;
        for (const float fraction : {0.10f, 0.15f, 0.20f, 0.25f, 0.30f}) {
            const float v = strideSpeedOf(rig, s, fraction);
            INFO("contact " << fraction << " -> " << v);
            CHECK(v > 0.0f); // it found a stance at all
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        INFO("spread " << hi / lo);
        CHECK(hi / lo < 2.0f);
        if (hi / lo > worstSpread) {
            worstSpread = hi / lo;
            worstAnimal = s.name;
        }
    }
    WARN(fmt::format("worst threshold spread {:.2f}x ({})", worstSpread, worstAnimal));
}

// =================================================================================================
// 2. The pack ships its own animation and nothing else
// =================================================================================================

TEST_CASE("every farm animal plays its own built-in Walk and nothing generated",
          "[farm][locomotion][assets]") {
    if (!farmPresent()) {
        SKIP("assets/farm is not present");
    }
    // The brief: "prefer the models' built-in animations only" and remove anything custom or
    // generated.  There is nothing to remove, and this is the check that keeps it that way -- one
    // clip per GLB, called `Walk`, and every activity the scene maps resolves to that one name.
    for (const Species& s : kFarm) {
        INFO(s.name);
        const scene::Scene scn = loadAnimal(s.name);
        const scene::SkinnedRig& rig = scn.rigs.front();
        REQUIRE(rig.clips.size() == 1);
        CHECK(rig.clips.front().name == "Walk");
    }
    const nlohmann::json doc = sceneJson();
    for (const Placed& p : placedFarm(doc)) {
        INFO(p.name);
        const nlohmann::json* ent = findNamed(doc.at("entities"), p.name);
        REQUIRE(ent != nullptr);
        REQUIRE(ent->contains("clips"));
        for (const auto& [activity, clip] : ent->at("clips").items()) {
            INFO(activity);
            CHECK(clip.get<std::string>() == "Walk");
        }
    }
}

// =================================================================================================
// 3. The authored numbers against the clips
// =================================================================================================

TEST_CASE("each farm animal's authored stride speed describes the clip it names",
          "[farm][locomotion][stride]") {
    if (!farmPresent()) {
        SKIP("assets/farm is not present");
    }
    const nlohmann::json doc = sceneJson();
    const std::vector<Placed> placed = placedFarm(doc);
    REQUIRE(!placed.empty());
    std::map<std::string, float> clipSpeed;
    for (const Placed& p : placed) {
        INFO(p.name);
        if (!clipSpeed.contains(p.species)) {
            const scene::Scene scn = loadAnimal(p.species.c_str());
            clipSpeed[p.species] = strideSpeedOf(scn.rigs.front(), speciesNamed(p.species));
        }
        const float clip = clipSpeed[p.species] * p.scale;
        INFO("scale " << p.scale << "; authored walk " << p.walkSpeed << " vs clip " << clip);
        // 5%, the same band the four aliens are held to.  ADR-213's scaled real-animal guesses came
        // out at -55% (bull, cow) to +207% (chick).
        CHECK(p.walkSpeed == Approx(clip).epsilon(0.05));
        // These nine ship **one** clip, and `clips` maps run onto it too.  So the run stride speed
        // is the walk stride speed: there is no second clip for it to be a claim about, and a
        // `runSpeed` that differs from `walkSpeed` would scale the playback rate of the walk cycle
        // by the ratio between them the moment the gait ever said Run.
        CHECK(p.runSpeed == Approx(p.walkSpeed).epsilon(1e-4));
    }
}

TEST_CASE("rate matching covers each farm animal's whole travelling range",
          "[farm][locomotion][gait]") {
    const nlohmann::json doc = sceneJson();
    const std::vector<Placed> placed = placedFarm(doc);
    REQUIRE(!placed.empty());
    for (const Placed& p : placed) {
        INFO(p.name);
        REQUIRE(p.gait.matchRate);
        REQUIRE(p.cruise > 0.0f);
        // A `wander` travels between nothing and its own `speed`; it has no second, faster gear.
        // Above the floor the clamp covers the band and the clip tracks the ground exactly.
        //
        // **Below the floor, by the owner's decision (ADR-622, option B, 2026-09-21), it does
        // not.** The floor was raised to `kVisibleClipRate` so the legs visibly shuffle at the
        // slowest crawl rather than creep, and the stated price is that there they outrun the
        // body. This test used to require tracking across the whole band, and it caught that
        // price exactly: at `moveExit` (0.0162 m/s) the legs now cycle about 4x faster than the
        // ground moves. So the band is split at the speed where the floor takes over, and the
        // part below it is **asserted to the exact value the floor implies** rather than dropped:
        // a floor that governed more or less than it should would still fail here.
        const float floorSpeed = p.gait.walkSpeed * p.gait.rateMin;
        for (const float v : {p.gait.moveExit, (p.gait.moveExit + p.cruise) * 0.5f, p.cruise}) {
            INFO("walking at " << v << " (the floor governs below " << floorSpeed << ")");
            const float expected = v >= floorSpeed ? 1.0f : v / floorSpeed;
            CHECK(entity::Gait::footSlip(p.gait, entity::Activity::Walk, v) ==
                  Approx(expected).epsilon(0.02));
        }
        // And the cruise is a *walk*.  ADR-226: if the speed the behaviour actually travels at is
        // above `runEnter`, the walk clip the scene names is decoration.  With one clip apiece the
        // stakes are lower than they were there and the statement is the same one.
        CHECK(p.cruise < p.gait.runEnter);
    }
}

// =================================================================================================
// 4. The walk back onto the navigable set
// =================================================================================================

TEST_CASE("a wanderer that steps off the navigable set walks back onto it",
          "[farm][entity][wander][navigation]") {
    // The reproduction of the 22.28 s stall (ADR-240).  `wander`'s give-up branch could not tell
    // "every way out is blocked" from "this body is standing off the navigable set", and the second
    // one is not a pause: `pathClear` samples navigability from the body's own position outward, so
    // from off the set *every* ray fails and the steering fan finds nothing however wide it reaches
    // (ADR-162).  `explore` was given the walk back onto the path and `wander` never was.
    //
    // Built here rather than only measured in the shipped scene, so the mechanism is pinned
    // independently of which animals that scene happens to place.
    // A world with a shoreline, which is ADR-162's own example of how a body ends up off the
    // navigable set: `waterMargin` puts the band just above the waterline outside it too, so the
    // edge is a real wedge rather than a contrived one.
    world::WorldMap map;
    map.size = glm::vec2(400.0f, 400.0f);
    map.seaLevel = 4.0f;
    map.layers = {{.frequency = 0.004f, .amplitude = 22.0f},
                  {.frequency = 0.02f, .amplitude = 6.0f}};
    map.prepare();
    world::Ecology ecology;
    world::ClearanceField clearance;
    clearance.map = &map;
    clearance.ecology = &ecology;
    entity::Navigator nav(&map, clearance);
    REQUIRE(nav.valid());

    // Find a wedge: somewhere the navigator refuses, with navigable ground inside the escape's
    // reach.  Searched rather than hand-placed, because a coordinate typed here would stop being a
    // wedge the day the terrain generator changes.
    glm::vec2 wedge(0.0f);
    bool foundWedge = false;
    for (float x = -150.0f; x <= 150.0f && !foundWedge; x += 2.0f) {
        for (float z = -150.0f; z <= 150.0f && !foundWedge; z += 2.0f) {
            const glm::vec2 p(x, z);
            if (nav.navigable(p)) {
                continue;
            }
            bool wayOut = false;
            for (float r = 2.0f; r <= 20.0f && !wayOut; r += 2.0f) {
                for (int k = 0; k < 12 && !wayOut; ++k) {
                    const float a = static_cast<float>(k) * 0.5235987756f;
                    wayOut = nav.navigable(p + glm::vec2(std::sin(a), std::cos(a)) * r);
                }
            }
            if (wayOut) {
                wedge = p;
                foundWedge = true;
            }
        }
    }
    REQUIRE(foundWedge);
    INFO(fmt::format("wedged at ({:.1f},{:.1f})", wedge.x, wedge.y));

    // **The control arm.**  The probe has to establish the state it claims to measure: from here
    // the steering fan really does find nothing, in every direction, so a body that only had the
    // fan would have to stand still (ADR-182, ADR-151).
    // How far the walk back actually has to go: the same outward spiral the escape itself runs.
    float refugeDistance = 0.0f;
    for (float r = 2.0f; r <= 24.0f && refugeDistance <= 0.0f; r += 2.0f) {
        for (int k = 0; k < 12 && refugeDistance <= 0.0f; ++k) {
            const float a = static_cast<float>(k) * 0.5235987756f;
            if (nav.navigable(wedge + glm::vec2(std::sin(a), std::cos(a)) * r)) {
                refugeDistance = r;
            }
        }
    }
    REQUIRE(refugeDistance > 0.0f);

    int steerable = 0;
    for (int k = 0; k < 16; ++k) {
        const float a = static_cast<float>(k) * 0.3926991f;
        const glm::vec2 target = wedge + glm::vec2(std::sin(a), std::cos(a)) * 12.0f;
        if (glm::length(nav.steer(wedge, target, 3.0f)) > 0.5f) {
            ++steerable;
        }
    }
    INFO("directions the steering fan could take: " << steerable << "/16");
    REQUIRE(steerable == 0);

    params::ParameterSet params;
    signals::SignalBus bus;
    entity::EntityWorld world;
    world.setNavigator(nav);
    registerNode(params, "walker");

    entity::EntityDesc walker;
    walker.name = "walker";
    walker.node = "walker";
    walker.seed = 11;
    walker.behaviors.push_back(behaviorDesc("wander", {{"speed", 2.0},
                                                       {"turnRate", 180.0},
                                                       {"minRange", 4.0},
                                                       {"maxRange", 20.0},
                                                       {"pauseMin", 0.0},
                                                       {"pauseMax", 0.1},
                                                       {"homeRadius", 30.0}}));
    world.setEntities({walker}, 7u);
    world.setBindings({binding("walker", glm::vec3(wedge.x, map.height(wedge), wedge.y))});
    world.registerParameters(params);
    world.bind(params);

    const entity::Entity* who = world.entities().front().get();
    REQUIRE(who != nullptr);

    double freedAt = -1.0;
    float worstFacingGap = 0.0f;
    int movingFrames = 0;
    int againstFacing = 0;
    glm::vec3 last = who->state().position();
    constexpr float kWalkerSpeed = 2.0f;
    constexpr float kWalkerTurn = 180.0f / 57.2957795f; // rad/s, as authored below
    for (int i = 0; i <= 1800; ++i) { // 30 simulated seconds
        params.resetFinals();
        entity::EntityUpdate u;
        u.time = static_cast<double>(i) / 60.0;
        u.dt = i == 0 ? 0.0 : 1.0 / 60.0;
        u.frameIndex = static_cast<std::uint64_t>(i);
        u.bus = &bus;
        world.update(u, params);

        const glm::vec3 now = who->state().position();
        const glm::vec2 step(now.x - last.x, now.z - last.z);
        last = now;
        if (freedAt < 0.0 && nav.navigable(glm::vec2(now.x, now.z))) {
            freedAt = u.time;
        }
        const float len = glm::length(step);
        if (len < 1e-4f) {
            continue;
        }
        ++movingFrames;
        // Every metre it covers is a metre along the way it is drawn facing.  This is the half of
        // the defect that survives the stall being fixed: an escape that translated the body
        // sideways would still free it, and would still look wrong doing it.
        const float yaw = who->locomotion().yaw;
        const glm::vec2 facing(std::sin(yaw), std::cos(yaw));
        const float alignment = glm::dot(facing, step / len);
        worstFacingGap = std::min(worstFacingGap, alignment);
        if (alignment < 0.9f) {
            ++againstFacing;
        }
    }

    INFO(fmt::format("freed at {:.2f} s; {} moving frames, {} not along the facing, worst "
                     "alignment {:.3f}",
                     freedAt, movingFrames, againstFacing, worstFacingGap));
    // It got out, and quickly: the escape is a walking step per frame toward a refuge at most 24 m
    // away.  Before the fix this body stood at the wedge for the whole twenty seconds.
    REQUIRE(freedAt >= 0.0);
    // The bound is the journey, not a number somebody liked: the opening pause `Wander::reset`
    // rolls (up to two seconds), then a pivot onto the way out at the authored turn rate, then the
    // walk to the refuge at the authored speed.  Anything slower than that is not a walk back onto
    // the path.  Before the fix this body stood at the wedge for the whole thirty seconds.
    constexpr double kOpeningPause = 2.0; // `Wander::reset`: rng.range(0, 2)
    const double bound = kOpeningPause + static_cast<double>(refugeDistance) / kWalkerSpeed +
                         3.14159265 / static_cast<double>(kWalkerTurn);
    INFO(fmt::format("refuge {:.1f} m away; an opening pause, a pivot and a walk is {:.2f} s",
                     refugeDistance, bound));
    CHECK(freedAt <= bound);
    // And it walked out rather than being slid out.
    REQUIRE(movingFrames > 60);
    CHECK(againstFacing == 0);
}

// =================================================================================================
// 5. The shipped scene
// =================================================================================================

TEST_CASE("the farm animals travel the way they are drawn facing, at the speed their clips say",
          "[farm][locomotion][glowmere2][integration]") {
    if (!farmPresent()) {
        SKIP("assets/farm is not present");
    }
    const nlohmann::json doc = sceneJson();
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(valleyScene()).has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);
    // Every entity updated every frame, however far from the view. Set through the *engine*
    // rather than on the scene: `Engine::update` writes its own policy onto `scene().detailLimits`
    // at the top of every frame, so a scene-level poke is overwritten before anything reads it.
    // With the band left on, a distant animal is stepped once every `coarseInterval` and advances
    // a tenth of a second in one jump -- which is not a locomotion measurement, it is a
    // measurement of the level-of-detail ladder (ADR-186). Measured with the band on: 5,393 of
    // 5,400 frames coarse, and 5,624 "backwards" steps that vanish with it off.
    {
        scene::DetailLimits limits = engine.detailLimits();
        limits.entityDistanceCull = false;
        engine.setDetailLimits(limits);
    }

    struct Row {
        std::string name;
        std::string species;
        const entity::Entity* who = nullptr;
        const scene::CompositionNode* node = nullptr;
        glm::vec3 last{0.0f};
        double lastMoved = 0.0;
        int moving = 0;
        int backwards = 0;        // travelled against the way it is drawn facing
        int backwardsWalking = 0; //   ...and its own locomotion accounts for the step
        int locomotor = 0;
        int slipOut = 0;
        float worstSlip = 1.0f;
        int frozenWhileMoving = 0;
        float previousSpeed = 0.0f; // for the authored-ramp test below
        // ADR-622, the owner's rateMin decision. On frames where the playback rate is pinned to the
        // gait's floor, how far the legs' stride carried beyond where the body actually went, in
        // metres. Positive is the cost option B was chosen knowing about: legs outrunning travel.
        std::vector<float> overTravel;
        double travelled = 0.0;
        float worstYawGap = 0.0f;
        int coarse = 0;
        bool everDirected = false;
        float wobble = 0.0f;   // the yaw swing this animal's own `liveliness` is authored to add
        float worstTilt = 0.0f; // the steepest lean `ground` gave it, radians
        const params::Parameter<glm::vec3>* rotParam = nullptr;
    };
    std::vector<Row> rows;
    std::map<std::string, float> clipSpeed;
    for (const Placed& p : placedFarm(doc)) {
        Row r;
        r.name = p.name;
        r.species = p.species;
        for (const auto& e : comp->entityWorld().entities()) {
            if (e->name() == p.name) {
                r.who = e.get();
            }
        }
        r.node = comp->findNode(p.name);
        REQUIRE(r.who != nullptr);
        REQUIRE(r.node != nullptr);
        r.last = comp->nodeWorldTransform(*r.node).position;
        r.rotParam = engine.params().findAs<glm::vec3>("nodes/" + p.name + "/rotation");
        REQUIRE(r.rotParam != nullptr);
        const nlohmann::json* ent = findNamed(doc.at("entities"), p.name);
        REQUIRE(ent != nullptr);
        for (const nlohmann::json& b : ent->at("behaviors")) {
            if (b.value("kind", std::string()) == "liveliness") {
                r.wobble = b.value("sway", 0.0f) / 57.2957795f;
            }
        }
        rows.push_back(std::move(r));
        if (!clipSpeed.contains(p.species)) {
            const scene::Scene scn = loadAnimal(p.species.c_str());
            clipSpeed[p.species] = strideSpeedOf(scn.rigs.front(), speciesNamed(p.species)) * p.scale;
        }
    }
    REQUIRE(rows.size() >= 12);

    std::set<std::string> retired;
    FixedStepClock clock(60.0);
    constexpr int kSteps = 90 * 60;
    for (int i = 0; i < kSteps; ++i) {
        const FrameTime ft = engine.tick(clock);
        engine.update(ft);
        for (const std::string& name : comp->director().retired()) {
            retired.insert(name);
        }
        for (Row& r : rows) {
            // Lifted, or already taken. An abducted animal is not wandering, and a retired one has
            // been hidden and left wherever the beam put it -- neither is a locomotion sample.
            if (r.who->directorMotion().active || retired.contains(r.name)) {
                r.everDirected = true;
                r.last = comp->nodeWorldTransform(*r.node).position;
                continue;
            }
            if (r.who->state().detail != 1.0f) {
                ++r.coarse;
            }
            const entity::LocomotionState& loco = r.who->locomotion();
            const scene::Transform t = comp->nodeWorldTransform(*r.node);
            const glm::vec2 step(t.position.x - r.last.x, t.position.z - r.last.z);
            r.last = t.position;
            const float len = glm::length(step);
            r.travelled += len;

            const bool locomotor = loco.activity == entity::Activity::Walk ||
                                   loco.activity == entity::Activity::Run;
            if (locomotor) {
                ++r.locomotor;
                // Measured against the *clip*, not against the authored number: `footSlip` divides
                // the authored speed by itself and reports 1.0 whatever the clip contains.  This is
                // the ground the body crossed over the stride the clip actually played.
                const float stride = clipSpeed[r.species] * loco.playbackRate;
                const float slip = loco.speed / std::max(stride, 1e-4f);
                if (slip > 1.1f || slip < 1.0f / 1.1f) {
                    ++r.slipOut;
                }
                r.worstSlip = std::max({r.worstSlip, slip, 1.0f / std::max(slip, 1e-4f)});
            }
            // A clip frozen while the body covers ground.  `idleRate` is 0 on this pack -- these
            // animals have no idle clip, so a standing one freezes its walk cycle mid-stride
            // (ADR-213) -- and a *moving* body doing that is a statue gliding over the terrain.
            // **A body inside its own authored acceleration budget is ramping, not frozen**
            // (`support/ramp.hpp`, and the same definition `test_abduction_poc` uses for the same
            // reason). This detector was written when a body was either at speed or stopped, so a
            // slow clip and a moving body could never coexist honestly. With an authored ramp they
            // do, for as long as the ramp lasts. Not a tolerance: the budget is authored and the
            // question is exact.
            const bool ramping =
                testsupport::withinAuthoredRamp(r.who->desc().gait, r.previousSpeed, loco.speed, ft.deltaTime);
            if (!ramping && loco.playbackRate < entity::kVisibleClipRate && len > 0.01f) {
                ++r.frozenWhileMoving;
            }
            // **At the floor, measured against what actually moved.** `len` is the drawn
            // displacement, so it includes the crowd-separation push (ADR-622 records that as a
            // separate gap); it is what a viewer compares the legs against, which is the point.
            {
                const entity::GaitSettings& gait = r.who->desc().gait;
                const bool atFloor = gait.matchRate && loco.speed > 0.0f &&
                                     loco.playbackRate <= gait.rateMin + 1e-5f;
                if (atFloor && ft.deltaTime > 0.0) {
                    const float legs = clipSpeed[r.species] * loco.playbackRate *
                                       static_cast<float>(ft.deltaTime);
                    r.overTravel.push_back(legs - len);
                }
            }
            r.previousSpeed = loco.speed;

            if (len < 1e-4f) {
                continue;
            }
            const auto since = static_cast<float>(ft.renderTime - r.lastMoved);
            r.lastMoved = ft.renderTime;
            ++r.moving;
            const glm::vec3 fwd = t.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
            const float drawn = std::atan2(fwd.x, fwd.z);
            {
                float gap = std::fmod(std::abs(drawn - loco.yaw), 6.2831853f);
                if (gap > 3.14159265f) {
                    gap = 6.2831853f - gap;
                }
                r.worstYawGap = std::max(r.worstYawGap, gap);
                const glm::vec3 euler = glm::radians(r.rotParam->value());
                r.worstTilt = std::max({r.worstTilt, std::abs(euler.x), std::abs(euler.z)});
            }
            const glm::vec2 facing(std::sin(drawn), std::cos(drawn));
            if (glm::dot(facing, step / len) >= -0.2f) {
                continue;
            }
            ++r.backwards;
            // ADR-204's classification.  Two different things produce a backwards step and only one
            // of them is an animation fault: what the body's own `speed` along its own `yaw`
            // accounts for is the walk, and what it does not is a push out of a solid or out of a
            // crowd, which is the guarantee those mechanisms exist to make.
            const glm::vec2 heading(std::sin(loco.yaw), std::cos(loco.yaw));
            if (glm::length(step - heading * loco.speed * since) < len * 0.35f) {
                ++r.backwardsWalking;
            }
        }
    }

    int totalBackwardsWalking = 0;
    int totalFrozen = 0;
    int totalSlipOut = 0;
    int totalLocomotor = 0;
    // ---- ADR-622: what the owner's rateMin decision costs, measured rather than predicted -----
    //
    // Reported, not asserted. Option B -- the floor raised to `kVisibleClipRate` -- was chosen
    // knowing it trades skate *under* the cycle for skate *over* it, and this is the size of that
    // trade on the pack it applies to. Distribution and worst case, never a mean alone: at a crawl
    // most frames are near zero and one bad frame is the one a viewer notices.
    {
        std::vector<float> all;
        std::string worstAnimal;
        float worst = -1e9f;
        for (const Row& r : rows) {
            for (const float o : r.overTravel) {
                all.push_back(o);
                if (o > worst) {
                    worst = o;
                    worstAnimal = r.name;
                }
            }
        }
        if (!all.empty()) {
            std::sort(all.begin(), all.end());
            const auto at = [&](double q) {
                return all[std::min(all.size() - 1, static_cast<std::size_t>(q * all.size()))];
            };
            std::size_t over = 0;
            for (const float o : all) {
                over += o > 0.0f ? 1u : 0u;
            }
            WARN(fmt::format("ADR-622 floor-pinned frames: {} across the herd; legs outrun the body on "
                             "{} ({:.1f}%). Over-travel per frame, mm: p50 {:+.3f}  p90 {:+.3f}  "
                             "p99 {:+.3f}  worst {:+.3f} ({})",
                             all.size(), over, 100.0 * over / all.size(), 1000.0 * at(0.50),
                             1000.0 * at(0.90), 1000.0 * at(0.99), 1000.0 * worst, worstAnimal));
        } else {
            WARN("ADR-622: no floor-pinned frames in this run");
        }
    }

    for (const Row& r : rows) {
        INFO(fmt::format("{}: travelled {:.1f} m, moving {} frames, backwards {} (walk-explained "
                         "{}), frozen-while-moving {}, slip outside 1.1x {}/{} (worst {:.2f}x), "
                         "worst yaw gap {:.3f} rad (authored sway {:.3f}), coarse {} frames, "
                         "directed {}",
                         r.name, r.travelled, r.moving, r.backwards, r.backwardsWalking,
                         r.frozenWhileMoving, r.slipOut, r.locomotor, r.worstSlip, r.worstYawGap,
                         r.wobble, r.coarse, r.everDirected));
        // The measurement has something to measure -- and it measured the thing it claims to.  A
        // coarsely-updated entity advances a tenth of a second in one jump, which is a reading of
        // the level-of-detail ladder rather than of locomotion (ADR-186, ADR-182).
        REQUIRE(r.coarse == 0);
        // Two seconds of this animal's own walk, rather than five metres.
        //
        // The guard is here so the sample is not degenerate -- a frozen animal has nothing to
        // measure -- and "five metres" said that only for a cast that happened to be 3.6x. At the
        // authored scale (ADR-334) a goat covers 2.99 m in this window and 5.0 failed, which is a
        // fact about the size of the world rather than about locomotion. `clipSpeed` is the
        // species' measured stride speed times the node's own scale, so this reads the same at any
        // scale -- and a body that does not move still fails it.
        INFO(r.species << " walks its clip at " << clipSpeed[r.species] << " m/s at this scale");
        CHECK(r.travelled > clipSpeed[r.species] * 2.0);
        CHECK(r.moving > 60);
        CHECK(r.locomotor > 60);
        totalBackwardsWalking += r.backwardsWalking;
        totalFrozen += r.frozenWhileMoving;
        totalSlipOut += r.slipOut;
        totalLocomotor += r.locomotor;
        // Per animal, so one wrong animal cannot hide behind seventeen right ones.
        //
        // **Zero, not a fraction.**  A step the body's own travel accounts for is never against the
        // way the body is drawn.
        CHECK(r.backwardsWalking == 0);
        CHECK(r.frozenWhileMoving == 0);
        // And the drawn facing follows the body to within the two things the scene authored on top
        // of it, and nothing else.  Both are derived rather than chosen:
        //
        //   * `liveliness`'s `sway`, a deliberate yaw wobble of a couple of degrees;
        //   * the lean `ground` gives the body on a slope.  A body pitched by p and rolled by r is
        //     drawn with its forward at `atan2(cos r sin y cos p + sin r sin p, cos y cos p)`,
        //     which differs from y by at most `asin(sin|t| tan|t|)` for a tilt of |t| -- 0.10 rad
        //     at the 0.33 rad lean these slopes actually produced.  That is geometry, not a
        //     mismatch: a body standing on a hillside really does point slightly off in plan.
        //
        // The gaps this test started at were 3.13 rad.
        const float lean =
            std::asin(std::min(1.0f, std::sin(r.worstTilt) * std::tan(r.worstTilt)));
        INFO("worst lean " << r.worstTilt << " rad, allowing " << lean << " rad of plan-view swing");
        CHECK(r.worstYawGap < r.wobble + lean + 0.02f);
    }
    INFO(fmt::format("totals: backwards-walking {}, frozen-while-moving {}, slip outside 1.1x "
                     "{}/{} locomotor frames",
                     totalBackwardsWalking, totalFrozen, totalSlipOut, totalLocomotor));
    // The feet against the ground, measured off the clips.  Not zero: `minDwell` holds a gait for a
    // quarter of a second after the body's speed has left that gait's band (ADR-204, ADR-226), and
    // the estimate itself carries the threshold spread the stance test above states.
    CHECK(totalSlipOut * 20 < totalLocomotor);
}


TEST_CASE("every farm animal's rate floor is visible, and a save keeps it", "[farm][locomotion][gait]") {
    // **The owner's decision, 2026-09-21: the farm animals' floor is raised to
    // `kVisibleClipRate`** (ADR-622, option B -- the legs visibly shuffle at the slowest crawl
    // rather than creeping at 0.5% of walk speed).
    //
    // Before this test the floors were 0.005 to 0.0168, below what the frozen detector itself calls
    // stopped, and nothing said whether that was a choice or a coincidence. **This makes it a
    // decision the next reader meets**: a farm floor in a live scene below the visible threshold
    // fails here, with the reason, instead of drifting back down unnoticed.
    //
    // And it checks the save, because `rateMin` is written **only when `matchRate` is on** -- a
    // farm body with rate matching off would have its new floor dropped on the way out, which is
    // ADR-618's shape exactly. So every farm body must be rate-matched *and* must round-trip.
    //
    // **Live scenes only.** `tractor-beam-lab-legacy.scene.json` is a test's "before" arm (ADR-262)
    // and `_pre-defects.scene.json` reproduces a historical render; both keep their original floors
    // on purpose and are deliberately not listed.
    const char* const scenes[] = {
        "glowmere-valley-2.scene.json",       "glowmere-valley-2-song.scene.json",
        "glowmere-valley-2-multicam.scene.json", "glowmere-atmospherics.scene.json",
        "tractor-beam-lab.scene.json",
    };
    int checked = 0;
    for (const char* name : scenes) {
        const fs::path file = fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / name;
        REQUIRE(fs::exists(file));
        std::ifstream in(file);
        const nlohmann::json doc = nlohmann::json::parse(in);

        // A farm body is one whose node loads an asset under assets/farm/ -- decided by the asset,
        // never by the value it happens to carry.
        std::map<std::string, std::string> assetOf;
        std::function<void(const nlohmann::json&)> walk = [&](const nlohmann::json& o) {
            if (o.is_object()) {
                if (o.contains("name") && o.contains("asset") && o.at("asset").is_string()) {
                    assetOf[o.at("name").get<std::string>()] = o.at("asset").get<std::string>();
                }
                for (const auto& [k, v] : o.items()) {
                    walk(v);
                }
            } else if (o.is_array()) {
                for (const auto& v : o) {
                    walk(v);
                }
            }
        };
        walk(doc);
        const auto isFarm = [&](const entity::EntityDesc& d) {
            const auto it = assetOf.find(d.node);
            return it != assetOf.end() && it->second.find("/assets/farm/") != std::string::npos;
        };

        auto loaded = entity::entitiesFromJson(doc.at("entities"), file.parent_path());
        INFO(name << ": " << (loaded.has_value() ? std::string() : loaded.error().message));
        REQUIRE(loaded.has_value());
        const nlohmann::json saved = entity::entitiesToJson(*loaded);
        REQUIRE(saved.is_array());
        REQUIRE(saved.size() == loaded->size());

        for (std::size_t i = 0; i < loaded->size(); ++i) {
            const entity::EntityDesc& d = (*loaded)[i];
            if (!isFarm(d)) {
                continue;
            }
            INFO(name << " / " << d.name);
            ++checked;
            // The decision.
            CHECK(d.gait.rateMin >= entity::kVisibleClipRate);
            // The precondition for it to survive a save.
            CHECK(d.gait.matchRate);
            // And that it does.
            const nlohmann::json& g = saved[i].at("gait");
            REQUIRE(g.contains("rateMin"));
            CHECK(g.at("rateMin").get<float>() == Catch::Approx(d.gait.rateMin));
        }
    }
    // The scenes must actually contain farm bodies, or this checks nothing.
    CHECK(checked == 80);
}
