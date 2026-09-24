// Entity-derived signals and the `owner.` alias (Effect Library Wave 1, package 1.2).
//
// `entity.<name>.{speed, velocity.x/y/z, acceleration, cameraDistance}` are published by the engine
// for every entity some effect or route subscribes to, BEFORE the routes run, from the last completed
// simulation step (rendering-architecture §3). They are only worth publishing if they are the same
// number in a scrub as in a play -- a Space Warp whose strength follows its UFO's speed must not
// flare on the first frame after a click -- so the first case is ADR-700's harness shape: a body
// scripted to move, played to a late second, then a fresh engine scrubbed there, and every signal
// compared exactly.
//
// `owner.` is how a type's default route names "my owner's speed" before it has an owner. It is
// resolved when the routes are attached, refused by name on a World owner, and follows a rename.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/timeline.hpp"
#include "world/effects/effect_params.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"
#include "world/effects/history_bank.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <numbers>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinRel;

namespace {

// One craft on a simulated orbit (an integrator, so a replay that skipped a step would land
// elsewhere), 15 m radius at 20 degrees a second: 5.236 m/s.
constexpr const char* kScene = R"({ "format": "avgen-scene", "version": 1, "name": "signals-fixture",
  "nodes": [ { "kind": "orb", "name": "craft", "position": [0, 6, 0] } ],
  "entities": [ { "name": "craft", "node": "craft", "seed": 11,
                  "behaviors": [ { "kind": "orbit", "radius": 15.0, "rate": 20.0, "authority": "simulation" },
                                 { "kind": "drift", "radius": 1.0, "rate": 0.3 } ] } ] })";

constexpr double kOrbitSpeed = 15.0 * 20.0 * std::numbers::pi / 180.0;

world::EffectInstance trailOn(const std::string& owner) {
    world::EffectInstance e = world::makeEffect(world::EffectKind::Trail, "Trail");
    e.id = "craft-trail";
    e.owner = world::EffectOwner::entity(owner);
    return e;
}

void install(app::Engine& engine) {
    REQUIRE(engine.setCompositionJson(nlohmann::json::parse(kScene)).has_value());
    REQUIRE(engine.composition() != nullptr);
    engine.composition()->scene().detailLimits.entityDistanceCull = false;
    // A baked camera move (ADR-075), which is what makes `cameraDistance` a function of time: a
    // camera nobody keyed is wherever it was last drawn, and after a jump that is the second the
    // playhead left.
    params::Track camera;
    camera.target = "camera/position";
    camera.keys.push_back(params::Key{.time = 0.0, .value = {0.0f, 12.0f, 40.0f, 0.0f}});
    camera.keys.push_back(params::Key{.time = 60.0, .value = {-30.0f, 20.0f, 10.0f, 0.0f}});
    engine.timeline().addTrack(camera);
    static_cast<void>(engine.timeline().bind(engine.params()));
    REQUIRE(engine.setEffects({trailOn("craft")}).has_value());
    REQUIRE(engine.addDefaultEffectRoutes("craft-trail") > 0);
}

void frameAt(app::Engine& engine, long long frame) {
    engine.update(FrameTime{static_cast<double>(frame) / 60.0, frame == 0 ? 0.0 : 1.0 / 60.0,
                            static_cast<std::uint64_t>(frame)});
}

float signal(app::Engine& engine, const std::string& name) {
    const auto id = engine.signals().find(name);
    REQUIRE(id.has_value());
    return engine.signals().value(*id);
}

constexpr const char* kLeaves[] = {"speed", "velocity.x", "velocity.y", "velocity.z", "acceleration",
                                   "cameraDistance"};

} // namespace

TEST_CASE("entity.<name>.speed at a late second is the same played and scrubbed",
          "[signals][hist][seek][determinism][adr700]") {
    // 37.5 s, the roadmap's second; the frame after it is the first one a scrub draws.
    constexpr long long kTarget = 2250; // 37.5 s
    app::Engine played(app::EngineMode::Offline);
    install(played);
    for (long long f = 0; f <= kTarget + 1; ++f) {
        frameAt(played, f);
    }

    app::Engine scrubbed(app::EngineMode::Offline);
    install(scrubbed);
    frameAt(scrubbed, 0); // a loaded engine has drawn a frame before anybody scrubs
    scrubbed.seekSeconds(static_cast<double>(kTarget) / 60.0);
    frameAt(scrubbed, kTarget + 1);

    // The control first: the body moves, at the orbit's speed, so equality below is not two zeros.
    const float speed = signal(played, "entity.craft.speed");
    INFO("played speed " << speed << " m/s");
    CHECK_THAT(speed, WithinRel(static_cast<float>(kOrbitSpeed), 0.25f));
    for (const char* leaf : kLeaves) {
        const std::string name = std::string("entity.craft.") + leaf;
        INFO(name << ": play " << signal(played, name) << ", scrub " << signal(scrubbed, name));
        CHECK(signal(played, name) == signal(scrubbed, name));
    }
    // And the velocity an effect asks its owner for (`EffectSceneQuery::nodeVelocity`) is the same
    // history, so it agrees too.
    glm::vec3 vp(0.0f);
    glm::vec3 vs(1.0f);
    REQUIRE(played.historyBank().velocity("craft", vp));
    REQUIRE(scrubbed.historyBank().velocity("craft", vs));
    CHECK(vp == vs);

    SECTION("and a few frames later still") {
        for (long long f = kTarget + 2; f <= kTarget + 20; ++f) {
            frameAt(played, f);
            frameAt(scrubbed, f);
        }
        CHECK(signal(played, "entity.craft.speed") == signal(scrubbed, "entity.craft.speed"));
        CHECK(signal(played, "entity.craft.acceleration") == signal(scrubbed, "entity.craft.acceleration"));
        CHECK(signal(played, "entity.craft.cameraDistance") == signal(scrubbed, "entity.craft.cameraDistance"));
    }
    SECTION("control: one step off is a different number") {
        app::Engine off(app::EngineMode::Offline);
        install(off);
        frameAt(off, 0);
        off.seekSeconds(static_cast<double>(kTarget + 1) / 60.0);
        frameAt(off, kTarget + 2);
        CHECK(signal(off, "entity.craft.velocity.x") != signal(played, "entity.craft.velocity.x"));
    }
}

TEST_CASE("owner.speed resolves to the owner's signal, and a World owner refuses it by name",
          "[signals][effects][modulation]") {
    world::EffectInstance entityOwned = trailOn("craft");
    auto routes = world::defaultEffectRoutes(entityOwned);
    REQUIRE(routes.has_value());
    bool sawOwner = false;
    for (const params::ModRoute& r : *routes) {
        CHECK_FALSE(r.source.starts_with("owner."));
        if (r.target == "fx/craft-trail/opacity") {
            CHECK(r.source == "entity.craft.speed");
            sawOwner = true;
        }
    }
    CHECK(sawOwner);
    // The schema form (what the conformance probe checks the targets of) still says `owner.`.
    bool raw = false;
    for (const params::ModRoute& r : world::defaultEffectRoutes("craft-trail", world::EffectKind::Trail)) {
        raw = raw || r.source == "owner.speed";
    }
    CHECK(raw);

    world::EffectInstance worldOwned = entityOwned;
    worldOwned.owner = world::EffectOwner::world();
    const auto refused = world::defaultEffectRoutes(worldOwned);
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error().message, ContainsSubstring("owner.speed"));
    CHECK_THAT(refused.error().message, ContainsSubstring("World"));

    // A type whose routes never say `owner.` is unaffected by its owner being the World.
    world::EffectInstance aurora = world::makeEffect(world::EffectKind::Aurora, "Aurora");
    CHECK(world::defaultEffectRoutes(aurora).has_value());
}

TEST_CASE("owner. follows its owner through a rename, bound to the new name's signal",
          "[signals][effects][modulation]") {
    app::Engine engine(app::EngineMode::Offline);
    install(engine);
    const auto sourceOf = [&](const std::string& target) -> const params::ModRoute* {
        for (const params::ModRoute& r : engine.modulator().routes()) {
            if (r.target == target) {
                return &r;
            }
        }
        return nullptr;
    };
    const params::ModRoute* before = sourceOf("fx/craft-trail/opacity");
    REQUIRE(before != nullptr);
    CHECK(before->source == "entity.craft.speed");
    CHECK(before->sourceId != signals::kInvalidSignal); // declared and bound, not a dangling name

    auto moved = engine.renameEffectOwner(world::EffectOwner::entity("craft"), world::EffectOwner::entity("ship"));
    REQUIRE(moved.has_value());
    CHECK(*moved == 1);
    REQUIRE(engine.effects().size() == 1);
    CHECK(engine.effects()[0].owner == world::EffectOwner::entity("ship"));
    const params::ModRoute* after = sourceOf("fx/craft-trail/opacity");
    REQUIRE(after != nullptr);
    CHECK(after->source == "entity.ship.speed");
    REQUIRE(after->sourceId != signals::kInvalidSignal);
    CHECK(engine.signals().info(after->sourceId).name == "entity.ship.speed");
    // The new owner is what HIST records now, and the old one is not.
    CHECK(engine.historyBank().find("ship") < engine.historyBank().ringCount());
    CHECK(engine.historyBank().find("craft") == engine.historyBank().ringCount());

    SECTION("control: the effect-only rename leaves the route on the old name") {
        std::vector<world::EffectInstance> list = engine.effects();
        world::renameEffectOwner(list, world::EffectOwner::entity("ship"), world::EffectOwner::entity("boat"));
        CHECK(list[0].owner == world::EffectOwner::entity("boat"));
        CHECK(sourceOf("fx/craft-trail/opacity")->source == "entity.ship.speed");
    }
}

TEST_CASE("only what reads an owner's motion subscribes it: Glowmere's hero pulses publish nothing",
          "[signals][hist][effects]") {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.setCompositionJson(nlohmann::json::parse(kScene)).has_value());
    // A surface wave owned by the craft reads nothing of its motion.
    world::EffectInstance pulse = world::makeEffect(world::EffectKind::GroundPulse, "Ground Pulse");
    pulse.owner = world::EffectOwner::entity("craft");
    REQUIRE(engine.setEffects({pulse}).has_value());
    CHECK(engine.historyBank().empty());
    CHECK_FALSE(engine.signals().find("entity.craft.speed").has_value());
    // A hand-made route reading the craft's speed does subscribe it.
    params::ModRoute r;
    r.source = "entity.craft.speed";
    r.target = "post/bloom/intensity";
    r.amount = 0.1f;
    engine.modulator().addRoute(r);
    REQUIRE(engine.setEffects({pulse}).has_value()); // any rebind re-reads the routes
    CHECK(engine.historyBank().find("craft") < engine.historyBank().ringCount());
    CHECK(engine.signals().find("entity.craft.speed").has_value());
}
