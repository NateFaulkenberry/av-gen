// Character Animation Lab -- zone D/E, the grounded body that is nevertheless drawn underground.
//
// The lab brief (§31) says a diagnostic that reads a convenient variable proves nothing: a test
// that checks `state().position().y == groundHeight` passes while the character is visibly buried,
// because that is not the number the character is drawn at. It is drawn at
//
//     visualPosition() = state().position() + motion.position
//
// and `motion.position` is the sum of every behaviour's offset for the frame. So the invariant with
// any force in it is about the *drawn* y, read back off the node's own position parameter -- the
// same value the renderer consumes -- rather than off the simulation.
//
// What that invariant catches: `liveliness` adds a stride bob as
//
//     motion.position.y += sin(phase * 2) * bounce * strength
//
// and sin is symmetric. The header calls it a rise -- "a body rises on each foot" -- but a symmetric
// oscillation spends half of every stride cycle *below* the position grounding put the feet at.
// Grounding cannot correct for it: grounding writes `state.travel.y` (simulation) and the bob writes
// `motion.position.y` (visual), the two never meet, and in Glowmere the behaviour order is not even
// consistent between species -- the farm animals run liveliness before ground, the aliens run
// explore before liveliness.
//
// Glowmere authors bounce 0.14..0.32 m on the five aliens and 0.02..0.06 m on the sixteen farm
// animals. An alien stands 1.79 m tall, so a third of a metre of sink is ankle-to-shin deep.
//
// ADR-182: the control arm is the identical run with `bounce = 0`. If the harness itself measured a
// phantom penetration -- a grounding lag, a terrain sample taken at the wrong place -- the control
// would show it too. The control is what makes the bounce arm mean the bounce.

#include "entity/entity.hpp"
#include "entity/navigation.hpp"
#include "params/parameter_set.hpp"
#include "signals/signal_bus.hpp"
#include "world/ecology.hpp"
#include "world/world_map.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace avgen;

namespace {

entity::NodeBinding binding(const std::string& node, glm::vec3 anchor) {
    entity::NodeBinding b;
    b.node = node;
    b.exists = true;
    b.transformPrefix = "nodes/" + node + "/";
    b.anchor = anchor;
    return b;
}

// The node's position parameter is registered at the anchor, not at the origin.
//
// This cost a control arm to learn. `applyOffsets` *adds* `travel + motion.position` to whatever
// final the parameter already carries, and in a real scene that base is the position the author
// placed the node at -- the same value the binding's anchor is taken from. A harness that registers
// the node at (0,0,0) therefore reads back `travel`: a displacement, not a world position. Sampling
// the terrain under a displacement gave a control arm that reported 4.19 m of penetration with the
// bob switched off, which is the harness being wrong rather than the engine.
void registerNode(params::ParameterSet& params, const std::string& node, glm::vec3 anchor) {
    params.add(params::ParamDesc<glm::vec3>{.path = "nodes/" + node + "/position",
                                            .defaultValue = anchor,
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

struct GroundReport {
    float deepestPenetration = 0.0f; // metres the DRAWN body sank below its own ground
    float highestFloat = 0.0f;       // metres the DRAWN body rose above it
    float deepestSimPenetration = 0.0f; // the same measured on the SIMULATION position
    int movingFrames = 0;
};

// Walks one body for 30 simulated seconds and measures the drawn y against the ground under it.
//
// The drawn y is read back from `nodes/<node>/position`, which is what `applyOffsets` writes and
// what the renderer reads. Nothing here consults `state().position()` except to report it beside
// the real answer, which is the entire point of the file.
GroundReport walkAndMeasure(float bounce, float stride) {
    world::WorldMap map;
    map.size = glm::vec2(400.0f, 400.0f);
    map.seaLevel = 4.0f;
    map.layers = {{.frequency = 0.004f, .amplitude = 22.0f}, {.frequency = 0.02f, .amplitude = 6.0f}};
    map.prepare();
    world::Ecology ecology;
    world::ClearanceField clearance;
    clearance.map = &map;
    clearance.ecology = &ecology;
    entity::Navigator nav(&map, clearance);
    REQUIRE(nav.valid());

    // Somewhere navigable and not on a cliff, found rather than typed.
    glm::vec2 start(0.0f);
    bool found = false;
    for (float x = -80.0f; x <= 80.0f && !found; x += 4.0f) {
        for (float z = -80.0f; z <= 80.0f && !found; z += 4.0f) {
            if (nav.navigable({x, z})) {
                start = {x, z};
                found = true;
            }
        }
    }
    REQUIRE(found);

    params::ParameterSet params;
    signals::SignalBus bus;
    entity::EntityWorld world;
    world.setNavigator(nav);
    registerNode(params, "walker", glm::vec3(start.x, map.height(start), start.y));

    entity::EntityDesc walker;
    walker.name = "walker";
    walker.node = "walker";
    walker.seed = 4242;
    // Glowmere's own farm ordering: liveliness, then the mover, then ground.
    walker.behaviors.push_back(behaviorDesc("liveliness", {{"bounce", bounce}, {"stride", stride},
                                                           {"bounceRate", 0.9}, {"sway", 0.0}, {"nod", 0.0}}));
    walker.behaviors.push_back(behaviorDesc("wander", {{"speed", 3.0},
                                                       {"turnRate", 140.0},
                                                       {"minRange", 6.0},
                                                       {"maxRange", 30.0},
                                                       {"pauseMin", 0.0},
                                                       {"pauseMax", 0.1},
                                                       {"homeRadius", 60.0}}));
    walker.behaviors.push_back(behaviorDesc("ground", nlohmann::json::object()));
    world.setEntities({walker}, 7u);
    world.setBindings({binding("walker", glm::vec3(start.x, map.height(start), start.y))});
    world.registerParameters(params);
    world.bind(params);

    const entity::Entity* who = world.entities().front().get();
    REQUIRE(who != nullptr);
    const auto* positionParam = params.findAs<glm::vec3>("nodes/walker/position");
    REQUIRE(positionParam != nullptr);

    GroundReport out;
    for (int i = 0; i <= 1800; ++i) { // 30 s at 60 Hz
        params.resetFinals();
        entity::EntityUpdate u;
        u.time = static_cast<double>(i) / 60.0;
        u.dt = i == 0 ? 0.0 : 1.0 / 60.0;
        u.frameIndex = static_cast<std::uint64_t>(i);
        u.bus = &bus;
        world.update(u, params);

        if (i < 60) {
            continue; // let grounding's smoother settle before believing anything
        }
        if (who->state().speed < 0.2f) {
            continue; // a standing body has no stride and no bob; the claim is about walking
        }
        ++out.movingFrames;

        const glm::vec3 drawn = positionParam->value();
        const glm::vec3 sim = who->state().position();
        const float ground = nav.groundHeight({drawn.x, drawn.z});
        const float delta = drawn.y - ground;
        out.deepestPenetration = std::max(out.deepestPenetration, -delta);
        out.highestFloat = std::max(out.highestFloat, delta);
        out.deepestSimPenetration =
            std::max(out.deepestSimPenetration, -(sim.y - nav.groundHeight({sim.x, sim.z})));
    }
    return out;
}

} // namespace

TEST_CASE("a walking body is never drawn below the ground it was grounded to",
          "[unit][charlab][grounding][liveliness]") {
    // BEFORE the fix in `Liveliness::update`, with `motion.position.y += sin(phase * 2) * ...`:
    //
    //     control (bounce 0.00): drawn sank 0.0030 m, rose 0.0054 m
    //     bounce  (bounce 0.32): drawn sank 0.1819 m, rose 0.1838 m   <- FAILED
    //                            simulation sank 0.0030 m            <- and looked fine
    //
    // AFTER, with `(1 - cos(phase * 2))`: the sink collapses to the control's own noise floor and
    // the rise is retained at full amplitude.

    // The control. Same walk, no bob. Grounding alone holds the body on the surface, so this is the
    // floor below which nothing in this harness can measure (ADR-182).
    const GroundReport control = walkAndMeasure(0.0f, 5.35f);
    REQUIRE(control.movingFrames > 300);
    INFO(fmt::format("control (bounce 0.00): drawn sank {:.4f} m, rose {:.4f} m over {} moving frames",
                     control.deepestPenetration, control.highestFloat, control.movingFrames));
    CHECK(control.deepestPenetration < 0.02f);

    // The arm: Glowmere's own numbers for `ember` and `vane`, the deepest-bobbing aliens.
    const GroundReport bobbed = walkAndMeasure(0.32f, 5.35f);
    REQUIRE(bobbed.movingFrames > 300);
    INFO(fmt::format("bounce  (bounce 0.32): drawn sank {:.4f} m, rose {:.4f} m; simulation sank {:.4f} m",
                     bobbed.deepestPenetration, bobbed.highestFloat, bobbed.deepestSimPenetration));

    // The invariant. A grounded, walking body is drawn on or above its ground -- never under it.
    CHECK(bobbed.deepestPenetration < 0.02f);

    // And the bob is still a bob, at the height it was authored to be. A lower bound alone would
    // be satisfied by deleting the feature outright -- and, as it turned out, by overshooting it:
    // the first version of this fix used `1 - cos` unscaled, which spans [0, 2] where `sin` spanned
    // [-1, 1]. It lifted the trough to the ground correctly and doubled the peak while doing it,
    // taking the drawn rise from 0.1838 m to 0.3641 m with every test still green. Hence a band
    // rather than a floor: this is the authored amplitude, and a fix is not allowed to move it in
    // either direction.
    CHECK(bobbed.highestFloat > 0.10f);
    CHECK(bobbed.highestFloat < 0.25f);
}

TEST_CASE("the simulation position cannot see a visual offset, and so cannot police one",
          "[unit][charlab][grounding][contract]") {
    // The §31 case, kept permanently and deliberately: this is the diagnostic that would have
    // passed throughout the bug above. It asserts something true -- grounding really does hold the
    // simulation position on the surface -- and that truth is worth nothing on its own, because the
    // character is not drawn there.
    //
    // Keeping it beside the real invariant is the point. `state().position()` is the simulation
    // contract; `visualPosition()` (and the node position parameter it lands in) is the rendering
    // contract; a grounding claim about a character is only meaningful against the second.
    const GroundReport bobbed = walkAndMeasure(0.32f, 5.35f);
    REQUIRE(bobbed.movingFrames > 300);
    CHECK(bobbed.deepestSimPenetration < 0.02f);
}
