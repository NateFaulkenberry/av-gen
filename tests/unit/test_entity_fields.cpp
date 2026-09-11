// Trigger volumes, music influence fields and the reaction arc (ADR-097).
//
// Three defect classes are guarded here and each test that guards one carries its own negative
// control -- the unfixed behaviour, demonstrated in the same test, so that a check which would pass
// against a system that does nothing cannot be mistaken for a check that passes because the system
// works:
//
//   1. **Binary in/out.** A volume that answers yes/no makes a crowd snap on at a line in the air.
//      Every falloff test shows what the constant curve does at the same two points.
//   2. **A second reactivity system.** A field that added routes of its own instead of scaling the
//      ones an entity already declared. The flagship test drives the *same* reaction with and
//      without a field and shows the only difference is its depth.
//   3. **Walking -> Dance -> Idle.** An arc that ends by resetting rather than by releasing. The
//      arc test runs a control world with no field beside the one with a field and requires that
//      they agree again, frame for frame, once the arc is over.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "entity/field.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "signals/signal_bus.hpp"
#include "support/temp_dir.hpp"
#include "world/world_map.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

std::string nodeName(int i) { return "npc" + std::to_string(i); }

// One node-like parameter block per crowd member: a transform and one addressable material part
// called "Lamp", which is what the reactions in these tests point at.
void registerCrowd(params::ParameterSet& params, int count) {
    for (int i = 0; i < count; ++i) {
        const std::string n = nodeName(i);
        params.add(params::ParamDesc<glm::vec3>{.path = "nodes/" + n + "/position",
                                                .defaultValue = glm::vec3(0.0f),
                                                .hardMin = glm::vec3(-1e4f),
                                                .hardMax = glm::vec3(1e4f)});
        params.add(params::ParamDesc<glm::vec3>{.path = "nodes/" + n + "/rotation",
                                                .defaultValue = glm::vec3(0.0f),
                                                .hardMin = glm::vec3(-360.0f),
                                                .hardMax = glm::vec3(360.0f)});
        params.add(params::ParamDesc<glm::vec3>{.path = "nodes/" + n + "/scale",
                                                .defaultValue = glm::vec3(1.0f),
                                                .hardMin = glm::vec3(0.001f),
                                                .hardMax = glm::vec3(100.0f)});
        params.add(params::ParamDesc<float>{.path = "procedural/" + n + "/parts/0/emissiveGain",
                                            .defaultValue = 0.0f,
                                            .hardMin = 0.0f,
                                            .hardMax = 50.0f});
    }
    params.add(params::ParamDesc<float>{
        .path = "lightrig/key/stage/intensity", .defaultValue = 0.0f, .hardMin = 0.0f, .hardMax = 20.0f});
}

std::vector<entity::NodeBinding> crowdBindings(int count) {
    std::vector<entity::NodeBinding> out;
    for (int i = 0; i < count; ++i) {
        entity::NodeBinding b;
        b.node = nodeName(i);
        b.exists = true;
        b.transformPrefix = "nodes/" + nodeName(i) + "/";
        b.geometryPrefix = "procedural/" + nodeName(i) + "/";
        b.partNames = {"Lamp"};
        b.anchor = glm::vec3(0.0f);
        out.push_back(std::move(b));
    }
    return out;
}

entity::BehaviorDesc behavior(const char* kind, nlohmann::json settings = nlohmann::json::object()) {
    entity::BehaviorDesc d;
    d.kind = kind;
    d.name = kind;
    settings["kind"] = kind;
    d.settings = std::move(settings);
    return d;
}

entity::EntityDesc npc(int i, std::vector<std::string> tags = {}) {
    entity::EntityDesc d;
    d.name = nodeName(i);
    d.node = nodeName(i);
    d.tags = std::move(tags);
    d.reactions.push_back({.signal = "audio.bass", .target = "parts/Lamp/emissiveGain", .depth = 2.0f});
    return d;
}

entity::FieldDesc sphereField(const char* name, glm::vec3 center, float radius) {
    entity::FieldDesc f;
    f.name = name;
    f.volume.shape = entity::VolumeShape::Sphere;
    f.volume.center = center;
    f.volume.radius = radius;
    f.volume.falloff = entity::Falloff::Linear;
    return f;
}

// The engine's frame, in the engine's order (app/engine.cpp): resetFinals, automation, the field
// pass, the routes, then the behaviours. Getting this order wrong is the whole point -- a field
// evaluated after the routes is a field every reaction in the scene is one frame behind.
struct Stage {
    params::ParameterSet params;
    signals::SignalBus bus;
    params::Modulator modulator;
    entity::EntityWorld world;
    signals::SignalId bass = signals::kInvalidSignal;
    std::vector<std::pair<std::string, glm::vec3>> placed; // stands in for the timeline's tracks
    glm::vec3 view{0.0f};
    double time = 0.0;
    std::uint64_t frame = 0;
    float bassValue = 1.0f;

    explicit Stage(int nodes = 1) {
        registerCrowd(params, nodes);
        bass = bus.declare("audio.bass", 0.0f, 1.0f);
    }

    void install(std::vector<entity::EntityDesc> entities, std::vector<entity::FieldDesc> fields,
                 std::uint32_t seed = 4242u, const entity::Navigator* nav = nullptr) {
        const int count = static_cast<int>(entities.size());
        world.setEntities(std::move(entities), seed);
        world.setBindings(crowdBindings(std::max(count, 1)));
        if (nav != nullptr) {
            world.setNavigator(*nav);
        }
        world.setFields(std::move(fields)); // before registerParameters: a field has knobs too
        world.registerParameters(params);
        world.bind(params);
        std::vector<std::string> problems;
        for (params::ModRoute& route : world.compileReactions(params, problems)) {
            route.fromEntity = true;
            modulator.addRoute(std::move(route));
        }
        if (!problems.empty()) {
            FAIL_CHECK(problems.front());
        }
        // Unresolvable at this point only when a route names a signal a field has yet to publish;
        // step() re-binds when that happens, the same way the composition does.
        (void)modulator.bind(bus, params);
    }

    void place(const std::string& node, glm::vec3 where) {
        for (auto& p : placed) {
            if (p.first == node) {
                p.second = where;
                return;
            }
        }
        placed.emplace_back(node, where);
    }

    void step(double dt) {
        params.resetFinals();
        bus.set(bass, bassValue);
        // The automation layer: where a baked actor is at this instant. A final written before the
        // field pass is exactly what seq::Sequence::bake() produces, which is why a field over one
        // of these is a pure function of time.
        for (const auto& [node, where] : placed) {
            if (auto* p = params.findAs<glm::vec3>("nodes/" + node + "/position")) {
                for (std::size_t c = 0; c < 3; ++c) {
                    p->setFinalComponent(c, where[static_cast<int>(c)]);
                }
            }
        }
        entity::FieldUpdate fu;
        fu.time = time;
        fu.dt = dt;
        fu.bus = &bus;
        fu.viewPosition = view;
        const std::size_t signalsBefore = bus.size();
        world.updateFields(fu, params);
        if (bus.size() != signalsBefore) {
            // What Composition::updateFields does: a field publishes itself on its first pass, so
            // a route that named `field.<name>.*` at load bound to nothing and has to be re-bound
            // once. Mirrored here because this helper is the engine's frame, not an approximation.
            (void)modulator.bind(bus, params);
        }
        world.applySpatialGain(modulator.routes());
        modulator.applyRoutes(bus, params, dt);
        entity::EntityUpdate eu;
        eu.time = time;
        eu.dt = dt;
        eu.frameIndex = frame;
        eu.bus = &bus;
        eu.viewPosition = view;
        world.update(eu, params);
        bus.clearEvents();
        time += dt;
        ++frame;
    }

    [[nodiscard]] float lamp(int i) const {
        const auto* p = params.find("procedural/" + nodeName(i) + "/parts/0/emissiveGain");
        REQUIRE(p != nullptr);
        return p->finalComponent(0);
    }
    [[nodiscard]] float influence(int i) const {
        const entity::Entity* e = world.find(nodeName(i));
        REQUIRE(e != nullptr);
        return e->influence();
    }
};

entity::Navigator flatGround() {
    static world::WorldMap map = world::defaultWorld();
    static world::Ecology ecology;
    world::ClearanceField field;
    field.map = &map;
    field.ecology = &ecology;
    field.cameraRadius = 0.6f;
    field.groundClearance = 0.0f;
    return entity::Navigator(&map, field);
}

} // namespace

// ---- §18: the region -------------------------------------------------------------------------

TEST_CASE("a volume is 1 at the centre, 0 at the surface, and the surface is outside",
          "[entity][field][trigger]") {
    entity::TriggerVolume sphere;
    sphere.shape = entity::VolumeShape::Sphere;
    sphere.radius = 10.0f;
    sphere.falloff = entity::Falloff::Linear;

    CHECK(sphere.influenceAt(glm::vec3(0.0f)) == 1.0f);
    // Exactly on the boundary. A half-open rule is the only one under which an enter edge and an
    // exit edge at the same point agree rather than depending on which side the float landed.
    CHECK(sphere.influenceAt(glm::vec3(10.0f, 0.0f, 0.0f)) == 0.0f);
    CHECK_FALSE(sphere.contains(glm::vec3(10.0f, 0.0f, 0.0f)));
    CHECK(sphere.influenceAt(glm::vec3(10.0001f, 0.0f, 0.0f)) == 0.0f);
    CHECK(sphere.contains(glm::vec3(9.999f, 0.0f, 0.0f)));
    CHECK_THAT(sphere.influenceAt(glm::vec3(5.0f, 0.0f, 0.0f)), WithinAbs(0.5, 1e-5));
    // Monotone all the way out, with no step anywhere.
    float previous = 2.0f;
    for (int i = 0; i <= 40; ++i) {
        const float v = sphere.influenceAt(glm::vec3(static_cast<float>(i) * 0.5f, 0.0f, 0.0f));
        CHECK(v <= previous + 1e-6f);
        previous = v;
    }

    entity::TriggerVolume box;
    box.shape = entity::VolumeShape::Box;
    box.halfExtents = glm::vec3(4.0f, 2.0f, 6.0f);
    box.falloff = entity::Falloff::Linear;
    CHECK(box.influenceAt(glm::vec3(0.0f)) == 1.0f);
    CHECK(box.contains(glm::vec3(3.9f, 1.9f, 5.9f)));
    CHECK(box.influenceAt(glm::vec3(4.0f, 0.0f, 0.0f)) == 0.0f);  // the face
    CHECK(box.influenceAt(glm::vec3(0.0f, 2.0f, 0.0f)) == 0.0f);  // a different face
    CHECK_FALSE(box.contains(glm::vec3(4.1f, 0.0f, 0.0f)));
    SECTION("a rotated box takes its orientation with it") {
        box.rotation = glm::vec3(0.0f, 90.0f, 0.0f);
        // The long axis was +Z and is now +X. 5.5 m along X is inside; 5.5 m along Z is not.
        CHECK(box.contains(glm::vec3(5.5f, 0.0f, 0.0f)));
        CHECK_FALSE(box.contains(glm::vec3(0.0f, 0.0f, 5.5f)));
    }

    entity::TriggerVolume capsule;
    capsule.shape = entity::VolumeShape::Capsule;
    capsule.radius = 2.0f;
    capsule.height = 6.0f;
    capsule.falloff = entity::Falloff::Linear;
    CHECK(capsule.influenceAt(glm::vec3(0.0f, 3.0f, 0.0f)) == 1.0f); // still on the segment
    CHECK(capsule.influenceAt(glm::vec3(0.0f, 5.0f, 0.0f)) == 0.0f); // the cap's surface
    CHECK(capsule.influenceAt(glm::vec3(2.0f, 0.0f, 0.0f)) == 0.0f); // the side
    CHECK(capsule.contains(glm::vec3(1.9f, 0.0f, 0.0f)));
    CHECK(capsule.reach() == 5.0f);
}

TEST_CASE("a field's falloff is a curve, not an edge", "[entity][field][trigger]") {
    // Addendum §13: binary in/out is the wrong answer, and the reason is visible here. Two dancers,
    // one just inside the door and one in front of the speaker, must not be doing the same thing.
    entity::TriggerVolume v;
    v.radius = 10.0f;
    const glm::vec3 nearEdge(9.5f, 0.0f, 0.0f);
    const glm::vec3 nearCentre(0.5f, 0.0f, 0.0f);

    v.falloff = entity::Falloff::Constant;
    // The negative control: the defect this file exists to prevent, demonstrated.
    CHECK(v.influenceAt(nearEdge) == v.influenceAt(nearCentre));

    for (const entity::Falloff curve :
         {entity::Falloff::Linear, entity::Falloff::Smooth, entity::Falloff::InverseSquare}) {
        v.falloff = curve;
        INFO("falloff " << entity::falloffName(curve));
        CHECK(v.influenceAt(nearCentre) > v.influenceAt(nearEdge) + 0.5f);
        CHECK_THAT(v.influenceAt(glm::vec3(0.0f)), WithinAbs(1.0, 1e-5));
        CHECK(v.influenceAt(glm::vec3(10.0f, 0.0f, 0.0f)) == 0.0f);
    }

    // The three curves are actually different shapes, not three names for a ramp.
    v.falloff = entity::Falloff::Linear;
    const float linear = v.influenceAt(glm::vec3(2.5f, 0.0f, 0.0f));
    v.falloff = entity::Falloff::Smooth;
    const float smooth = v.influenceAt(glm::vec3(2.5f, 0.0f, 0.0f));
    v.falloff = entity::Falloff::InverseSquare;
    const float inverse = v.influenceAt(glm::vec3(2.5f, 0.0f, 0.0f));
    // Smooth is the gentlest near the source and inverse-square is the sharpest: an inverse
    // square puts most of its travel in the first few metres, which is what a speaker does.
    CHECK(smooth > linear);
    CHECK(inverse < linear);

    SECTION("`inner` is a plateau, not a scale") {
        v.falloff = entity::Falloff::Linear;
        v.inner = 0.5f;
        CHECK(v.influenceAt(glm::vec3(4.9f, 0.0f, 0.0f)) == 1.0f); // inside the plateau
        CHECK_THAT(v.influenceAt(glm::vec3(7.5f, 0.0f, 0.0f)), WithinAbs(0.5, 1e-5));
        CHECK(v.influenceAt(glm::vec3(10.0f, 0.0f, 0.0f)) == 0.0f);
    }
}

TEST_CASE("crossing a volume produces one enter edge and one exit edge", "[entity][field][trigger]") {
    Stage stage(1);
    stage.install({npc(0)}, {sphereField("stage", glm::vec3(0.0f), 10.0f)});

    const auto walkTo = [&](float x) {
        stage.place("npc0", glm::vec3(x, 0.0f, 0.0f));
        stage.step(1.0 / 60.0);
    };

    int enters = 0;
    int exits = 0;
    const auto count = [&] {
        for (const entity::TriggerEvent& e : stage.world.triggerEvents()) {
            (e.enter ? enters : exits) += 1;
        }
    };

    walkTo(30.0f);
    count();
    CHECK(enters == 0);
    for (int i = 0; i < 40; ++i) { // walk in
        walkTo(30.0f - static_cast<float>(i));
        count();
    }
    CHECK(enters == 1);
    CHECK(exits == 0);
    for (int i = 0; i < 5; ++i) { // and stay: staying is not an edge
        walkTo(0.0f);
        count();
    }
    CHECK(enters == 1);
    for (int i = 0; i < 40; ++i) { // and out again
        walkTo(static_cast<float>(i));
        count();
    }
    CHECK(enters == 1);
    CHECK(exits == 1);

    SECTION("and the edge is published as an ordinary signal") {
        // §39-§43's answer for a light or a particle system: a field is a signal source, so
        // anything that can route from a signal can react to a volume without knowing what one is.
        const auto occupancy = stage.bus.find("field.stage.occupancy");
        REQUIRE(occupancy);
        REQUIRE(stage.bus.find("field.stage.enter"));
        REQUIRE(stage.bus.find("field.stage.exit"));
        walkTo(0.0f);
        CHECK(stage.bus.value(*occupancy) == 1.0f);
        walkTo(100.0f);
        CHECK(stage.bus.value(*occupancy) == 0.0f);
    }
}

// ---- §19: the flagship -----------------------------------------------------------------------

TEST_CASE("a field scales the depth of the reaction an entity already had",
          "[entity][field][reaction]") {
    // The constraint the brief states twice: do not build a second audio-reactivity system. The
    // reaction here is one an author would have written before fields existed -- `audio.bass ->
    // parts/Lamp/emissiveGain`, depth 2 -- and the field's only effect on it is its depth.
    Stage withField(1);
    withField.install({npc(0)}, {sphereField("stage", glm::vec3(0.0f), 10.0f)});
    Stage noField(1);
    noField.install({npc(0)}, {}); // the negative control: the same scene with no field in it

    const auto at = [](Stage& s, float x) {
        s.place("npc0", glm::vec3(x, 0.0f, 0.0f));
        s.step(1.0 / 60.0);
        return s.lamp(0);
    };

    // Outside: the reaction is silent, because the field governs this entity and it is not in it.
    CHECK(at(withField, 40.0f) == 0.0f);
    CHECK(withField.influence(0) == 0.0f);
    // At the centre: exactly what the author wrote, depth 2 at full bass, and no more.
    CHECK_THAT(at(withField, 0.0f), WithinAbs(2.0, 1e-5));
    CHECK(withField.influence(0) == 1.0f);
    // Half way out, linear falloff: half the depth. Continuous, not a step.
    CHECK_THAT(at(withField, 5.0f), WithinAbs(1.0, 1e-4));

    // And the control, at every one of those points, is the unscaled reaction. A scene with no
    // fields is exactly the scene it was before this file existed.
    CHECK_THAT(at(noField, 40.0f), WithinAbs(2.0, 1e-5));
    CHECK_THAT(at(noField, 0.0f), WithinAbs(2.0, 1e-5));
    CHECK(noField.influence(0) == 1.0f);

    SECTION("strength above 1 boosts the reaction rather than capping it") {
        entity::FieldDesc loud = sphereField("stage", glm::vec3(0.0f), 10.0f);
        loud.strength = 2.5f;
        Stage s(1);
        s.install({npc(0)}, {loud});
        CHECK_THAT(at(s, 0.0f), WithinAbs(5.0, 1e-4));
    }

    SECTION("a floor keeps a reaction alive outside the field") {
        entity::FieldDesc soft = sphereField("stage", glm::vec3(0.0f), 10.0f);
        soft.floorGain = 0.25f;
        Stage s(1);
        s.install({npc(0)}, {soft});
        CHECK_THAT(at(s, 40.0f), WithinAbs(0.5, 1e-4));
        CHECK_THAT(at(s, 0.0f), WithinAbs(2.0, 1e-4));
    }

    SECTION("a reaction marked not spatial is left alone") {
        // A Multiply route whose chain rests at 1 is *muted* by a gain of zero rather than made
        // quiet, so an author has to be able to opt one out.
        entity::EntityDesc d = npc(0);
        d.reactions[0].spatial = false;
        Stage s(1);
        s.install({d}, {sphereField("stage", glm::vec3(0.0f), 10.0f)});
        CHECK_THAT(at(s, 40.0f), WithinAbs(2.0, 1e-5));
    }

    SECTION("a field that does not scale reactions is a pure trigger") {
        entity::FieldDesc trigger = sphereField("door", glm::vec3(0.0f), 10.0f);
        trigger.scaleReactions = false;
        Stage s(1);
        s.install({npc(0)}, {trigger});
        CHECK_THAT(at(s, 40.0f), WithinAbs(2.0, 1e-5)); // modulation untouched
        CHECK(s.world.triggerEvents().empty());
        at(s, 0.0f);
        REQUIRE(s.world.triggerEvents().size() == 1); // but the edge still fires
        CHECK(s.world.triggerEvents().front().enter);
    }
}

TEST_CASE("a field only governs the entities its filter matches", "[entity][field][reaction]") {
    Stage s(3);
    entity::FieldDesc stageField = sphereField("stage", glm::vec3(0.0f), 10.0f);
    stageField.tags = {"dancer"};
    entity::EntityDesc tagged = npc(0, {"dancer"});
    entity::EntityDesc untagged = npc(1);
    entity::EntityDesc byProfile = npc(2);
    byProfile.profile = "dancer"; // the profile an entity was built from is a tag it did not need
    s.install({tagged, untagged, byProfile}, {stageField});

    for (int i = 0; i < 3; ++i) {
        s.place(nodeName(i), glm::vec3(40.0f, 0.0f, 0.0f));
    }
    s.step(1.0 / 60.0);
    CHECK(s.influence(0) == 0.0f); // governed, and outside
    CHECK(s.influence(1) == 1.0f); // not governed at all: unchanged, and never queried
    CHECK(s.influence(2) == 0.0f); // governed by its profile name
    CHECK(s.world.fieldCounts().governed == 2);
}

// ---- §21: the arc ----------------------------------------------------------------------------

TEST_CASE("an arc enters, holds, and returns to what the entity was doing", "[entity][field][arc]") {
    // Addendum §14: `Walking -> Dance -> Walking`, never `Walking -> Dance -> Idle`. What makes
    // that true here is *reuse*, not new code: `wander` already yields to Activity::React and says
    // in its own comment that "the destination and the pause timer are kept, and the walk resumes
    // from where it stopped". So the arc writes React before the behaviours run rather than after,
    // and the existing contract does the work. The proof is that the walk that comes back is the
    // *same* walk -- same heading, same destination -- and not a fresh one.
    const entity::Navigator nav = flatGround();
    entity::EntityDesc walker = npc(0, {"dancer"});
    walker.seed = 20260911u;
    walker.behaviors.push_back(behavior("wander", {{"speed", 1.4}, {"pauseMin", 0.2}, {"pauseMax", 0.6}}));
    entity::EntityDesc marker = npc(1); // an entity with no behaviours: the field rides on it

    entity::FieldDesc field = sphereField("stage", glm::vec3(0.0f), 6.0f);
    field.source = "npc1";
    field.tags = {"dancer"};
    field.arc.enabled = true;
    field.arc.holdSeconds = 2.0f;
    field.arc.releaseSeconds = 0.5f;
    field.arc.refractorySeconds = 120.0f; // one arc in this test, so the tail is not a second one
    field.arc.activity = "react";

    Stage live(2);
    live.install({walker, marker}, {field}, 4242u, &nav);
    Stage control(2);
    control.install({walker, marker}, {}, 4242u, &nav);
    live.place("npc1", glm::vec3(500.0f, 0.0f, 0.0f)); // the field starts a long way off

    const entity::Entity* subject = live.world.find("npc0");
    const entity::Entity* twin = control.world.find("npc0");
    REQUIRE(subject != nullptr);
    REQUIRE(twin != nullptr);
    INFO("navigator valid: " << nav.valid());

    const double dt = 1.0 / 60.0;
    // Warm up until the walker is mid-stride, so what follows is about interrupting a walk.
    bool walking = false;
    for (int i = 0; i < 900 && !walking; ++i) {
        live.step(dt);
        control.step(dt);
        walking = i > 120 && subject->locomotion().activity == entity::Activity::Walk;
    }
    REQUIRE(walking);
    REQUIRE_FALSE(subject->arcActive());
    CHECK(subject->locomotion().activity == twin->locomotion().activity);

    const glm::vec3 whereItWas = subject->locomotion().position;

    // Bring the field to the walker. `place` writes the marker's position final, which is what an
    // automation track does, so this is a field on a baked source arriving at a live character.
    live.place("npc1", whereItWas);
    live.step(dt);
    control.step(dt);
    REQUIRE(subject->arcActive());
    CHECK(subject->locomotion().activity == entity::Activity::React);
    CHECK(subject->locomotion().reaction > 0.5f);
    CHECK(twin->locomotion().activity != entity::Activity::React); // the control never reacts

    // The hold: two seconds of React, and the body holds still because `wander` yielded rather
    // than being switched off.
    const glm::vec3 whereItStopped = subject->locomotion().position;
    const float yawAtStop = subject->locomotion().yaw;
    for (int i = 0; i < 115; ++i) {
        live.step(dt);
        control.step(dt);
        REQUIRE(subject->locomotion().activity == entity::Activity::React);
        // Exactly, not approximately: `wander` yields on React before it touches either, so the
        // place and the facing are the ones it stopped at and nothing is re-derived.
        REQUIRE(subject->locomotion().position == whereItStopped);
        REQUIRE(subject->locomotion().yaw == yawAtStop);
    }
    CHECK(subject->locomotion().speed == 0.0f);

    // The release: the level falls rather than snapping off.
    const float beforeRelease = subject->arcLevel();
    for (int i = 0; i < 15; ++i) {
        live.step(dt);
        control.step(dt);
    }
    CHECK(subject->arcLevel() < beforeRelease);
    CHECK(subject->arcLevel() > 0.0f);

    // And after it: the body kept its facing all the way through, and the walk that comes back is
    // the one that was interrupted -- not a new one. A *reset* wander draws a fresh 0..2 s pause
    // and stands still, so resuming inside ten frames is the proof that the destination and the
    // pause timer survived. The section below is that negative control.
    int tail = 0;
    while (subject->arcActive() && tail < 120) {
        live.step(dt);
        control.step(dt);
        ++tail;
    }
    REQUIRE_FALSE(subject->arcActive());
    CHECK(subject->arcLevel() == 0.0f);
    for (int i = 0; i < 10; ++i) {
        live.step(dt);
        control.step(dt);
    }
    CHECK(subject->locomotion().activity == entity::Activity::Walk);
    CHECK(subject->locomotion().speed > 0.1f);
    CHECK(glm::length(subject->locomotion().position - whereItStopped) > 0.05f);

    SECTION("the negative control: ending an arc by resetting loses the walk") {
        // `Walking -> Dance -> Idle` is what an implementation that put the entity 'back' would
        // produce, and reset() is that implementation. It is visibly not the same thing: the gait
        // is gone, the destination is gone, and the character is back at its anchor.
        live.place("npc1", glm::vec3(5000.0f, 0.0f, 0.0f)); // the field out of the way first
        live.world.reset();
        live.step(dt);
        CHECK(subject->locomotion().activity == entity::Activity::Idle);
        CHECK(subject->locomotion().speed == 0.0f);
        CHECK(glm::length(subject->locomotion().position - whereItStopped) > 1.0f); // and back at
                                                                                   // the start
    }
}

TEST_CASE("an arc's delay, length and intensity vary per entity", "[entity][field][arc]") {
    // A crowd that all starts dancing on the same frame for the same length of time is a chorus
    // line. The variation is seeded from each entity's own seed, so it is different per entity and
    // identical run to run.
    constexpr int kCount = 12;
    entity::FieldDesc field = sphereField("stage", glm::vec3(0.0f), 100.0f);
    field.arc.enabled = true;
    field.arc.delaySeconds = 0.5f;
    field.arc.delayJitter = 0.4f;
    field.arc.holdSeconds = 3.0f;
    field.arc.holdJitter = 1.2f;
    field.arc.intensity = 1.0f;
    field.arc.intensityJitter = 0.5f;

    std::vector<entity::EntityDesc> crowd;
    for (int i = 0; i < kCount; ++i) {
        crowd.push_back(npc(i)); // no explicit seed: each derives one from its own name
    }
    Stage s(kCount);
    s.install(crowd, {field});
    for (int i = 0; i < kCount; ++i) {
        s.place(nodeName(i), glm::vec3(0.0f)); // everyone crosses in on the same frame
    }

    const double dt = 1.0 / 60.0;
    std::vector<int> startFrame(kCount, -1);
    std::vector<int> endFrame(kCount, -1);
    std::vector<float> peak(kCount, 0.0f);
    for (int frame = 0; frame < 600; ++frame) {
        s.step(dt);
        for (int i = 0; i < kCount; ++i) {
            const entity::Entity* e = s.world.find(nodeName(i));
            const float level = e->arcLevel();
            peak[i] = std::max(peak[i], level);
            if (level > 0.0f && startFrame[i] < 0) {
                startFrame[i] = frame;
            }
            if (level == 0.0f && startFrame[i] >= 0 && endFrame[i] < 0) {
                endFrame[i] = frame;
            }
        }
    }
    for (int i = 0; i < kCount; ++i) {
        INFO("entity " << i);
        REQUIRE(startFrame[i] >= 0);
        REQUIRE(endFrame[i] > startFrame[i]);
    }
    const auto distinct = [](std::vector<int> v) {
        std::sort(v.begin(), v.end());
        return static_cast<int>(std::unique(v.begin(), v.end()) - v.begin());
    };
    CHECK(distinct(startFrame) > kCount / 2);
    std::vector<int> lengths;
    for (int i = 0; i < kCount; ++i) {
        lengths.push_back(endFrame[i] - startFrame[i]);
    }
    CHECK(distinct(lengths) > kCount / 2);
    CHECK(*std::max_element(peak.begin(), peak.end()) > *std::min_element(peak.begin(), peak.end()) + 0.2f);

    SECTION("and it is the same variation every run") {
        Stage again(kCount);
        again.install(crowd, {field});
        for (int i = 0; i < kCount; ++i) {
            again.place(nodeName(i), glm::vec3(0.0f));
        }
        std::vector<int> repeatStart(kCount, -1);
        for (int frame = 0; frame < 600; ++frame) {
            again.step(dt);
            for (int i = 0; i < kCount; ++i) {
                if (again.world.find(nodeName(i))->arcLevel() > 0.0f && repeatStart[i] < 0) {
                    repeatStart[i] = frame;
                }
            }
        }
        CHECK(repeatStart == startFrame);
    }
}

// ---- ADR-091: determinism --------------------------------------------------------------------

TEST_CASE("a field over a baked source is the same frame however the playhead got there",
          "[entity][field][determinism]") {
    // ADR-091's promise, tested the way the brief asks: play to t, and jump to t, and compare. The
    // source here is a node an automation track drives and an entity with no behaviours, so the
    // whole chain is a pure function of time and the two answers must be bit-identical.
    const auto position = [](double t) {
        return glm::vec3(30.0f - 6.0f * static_cast<float>(t), 0.0f, 0.0f);
    };
    const auto build = [] {
        auto s = std::make_unique<Stage>(1);
        s->install({npc(0)}, {sphereField("stage", glm::vec3(0.0f), 10.0f)});
        return s;
    };

    auto played = build();
    const double dt = 1.0 / 60.0;
    for (int i = 0; i <= 300; ++i) {
        played->place("npc0", position(static_cast<double>(i) * dt));
        played->step(dt);
    }
    const float playedLamp = played->lamp(0);
    const float playedInfluence = played->influence(0);

    auto scrubbed = build();
    scrubbed->time = 300.0 * dt;
    scrubbed->place("npc0", position(300.0 * dt));
    scrubbed->step(dt); // one evaluation, at the frame we jumped to
    CHECK(scrubbed->influence(0) == playedInfluence);
    CHECK(scrubbed->lamp(0) == playedLamp);
    CHECK(playedInfluence > 0.0f); // and it is not passing because nothing happened

    SECTION("a replay is identical too") {
        auto replay = build();
        for (int i = 0; i <= 300; ++i) {
            replay->place("npc0", position(static_cast<double>(i) * dt));
            replay->step(dt);
        }
        CHECK(replay->lamp(0) == playedLamp);
        CHECK(replay->influence(0) == playedInfluence);
    }
}

TEST_CASE("which determinism guarantee a field has is written down", "[entity][field][determinism]") {
    // ADR-091: "this must be stated where an author chooses, not discovered when a render differs."
    entity::EntityDesc still = npc(0);
    entity::EntityDesc walker = npc(1);
    walker.behaviors.push_back(behavior("drift", {{"radius", 3.0}, {"rate", 0.2}}));

    entity::FieldDesc fixed = sphereField("fixed", glm::vec3(0.0f), 10.0f);
    entity::FieldDesc onBaked = sphereField("onBaked", glm::vec3(0.0f), 10.0f);
    onBaked.source = "npc0";
    entity::FieldDesc onLive = sphereField("onLive", glm::vec3(0.0f), 10.0f);
    onLive.source = "npc1";

    Stage s(2);
    s.install({still, walker}, {fixed, onBaked, onLive});
    const std::vector<entity::FieldRuntime>& rt = s.world.fieldRuntime();
    REQUIRE(rt.size() == 3);
    CHECK(rt[0].authority == entity::FieldAuthority::Static);
    CHECK(rt[1].authority == entity::FieldAuthority::Baked);
    CHECK(rt[2].authority == entity::FieldAuthority::Live);

    const std::vector<std::string>& report = s.world.fieldReport();
    REQUIRE(report.size() == 3);
    CHECK_THAT(report[0], ContainsSubstring("static"));
    CHECK_THAT(report[1], ContainsSubstring("baked"));
    CHECK_THAT(report[2], ContainsSubstring("live"));
    CHECK_THAT(report[2], ContainsSubstring("NOT scrub-exact"));
    // A field that is merely fine is not reported as a problem; only one that asked for a
    // guarantee it did not get.
    CHECK(s.world.problems().empty());

    SECTION("and asking for scrub-exactness you did not get is an error, not a surprise") {
        onLive.requireScrubExact = true;
        Stage strict(2);
        strict.install({still, walker}, {fixed, onBaked, onLive});
        REQUIRE_FALSE(strict.world.problems().empty());
        CHECK_THAT(strict.world.problems().back(), ContainsSubstring("onLive"));
        CHECK_THAT(strict.world.problems().back(), ContainsSubstring("npc1"));
        CHECK_THAT(strict.world.problems().back(), ContainsSubstring("ADR-091"));
    }

    SECTION("a source that names nothing says so rather than sitting at the origin in silence") {
        entity::FieldDesc lost = sphereField("lost", glm::vec3(0.0f), 10.0f);
        lost.source = "nobody";
        Stage s2(2);
        s2.install({still, walker}, {lost});
        REQUIRE_FALSE(s2.world.problems().empty());
        CHECK_THAT(s2.world.problems().front(), ContainsSubstring("nobody"));
    }
}

TEST_CASE("a field follows its source", "[entity][field]") {
    Stage s(2);
    entity::FieldDesc follower = sphereField("bubble", glm::vec3(0.0f, 0.0f, 0.0f), 6.0f);
    follower.source = "npc0";
    follower.tags = {"dancer"};
    s.install({npc(0), npc(1, {"dancer"})}, {follower});

    s.place("npc0", glm::vec3(0.0f));
    s.place("npc1", glm::vec3(50.0f, 0.0f, 0.0f));
    s.step(1.0 / 60.0);
    CHECK(s.influence(1) == 0.0f);
    s.place("npc0", glm::vec3(50.0f, 0.0f, 0.0f)); // the source walks over to npc1
    s.step(1.0 / 60.0);
    CHECK(s.influence(1) == 1.0f);
}

// ---- §47: the budget -------------------------------------------------------------------------

TEST_CASE("the field pass is a grid, not a scan", "[entity][field][performance]") {
    // The brief's constraint: not O(entities x volumes) per frame. A crowd of 512 across a square
    // kilometre with 16 small fields in it: the number of exact tests must be a small fraction of
    // 512 x 16, and it is the grid that makes it so.
    constexpr int kEntities = 512;
    constexpr int kFields = 16;
    std::vector<entity::EntityDesc> crowd;
    crowd.reserve(kEntities);
    for (int i = 0; i < kEntities; ++i) {
        crowd.push_back(npc(i));
    }
    std::vector<entity::FieldDesc> fields;
    for (int f = 0; f < kFields; ++f) {
        fields.push_back(sphereField(("field" + std::to_string(f)).c_str(),
                                     glm::vec3(static_cast<float>(f) * 60.0f - 480.0f, 0.0f, 0.0f),
                                     12.0f));
    }
    Stage s(kEntities);
    s.install(crowd, fields);
    for (int i = 0; i < kEntities; ++i) {
        const float angle = static_cast<float>(i) * 0.61803f;
        const float radius = 40.0f + static_cast<float>(i % 97) * 5.0f;
        s.place(nodeName(i), glm::vec3(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius));
    }

    s.step(1.0 / 60.0);
    const entity::EntityWorld::FieldCounts counts = s.world.fieldCounts();
    CHECK(counts.governed == kEntities);
    CHECK(counts.queried == kEntities);
    const std::size_t scan = static_cast<std::size_t>(kEntities) * kFields;
    WARN("field pass: " << counts.tested << " exact tests, " << counts.cells
                        << " grid cells (a full scan would be " << scan << ")");
    CHECK(counts.tested < scan / 4);

    // A budget rather than a benchmark, and best-of-N rather than a median: under contention with
    // other work on this machine the slow runs say nothing about the code, and the fast one is the
    // only honest lower bound on what it costs.
    constexpr int kRuns = 7;
    constexpr int kFrames = 120;
    double best = 1e30;
    for (int run = 0; run < kRuns; ++run) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kFrames; ++i) {
            s.step(1.0 / 60.0);
        }
        const auto end = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double, std::micro>(end - start).count());
    }
    const double perFrame = best / kFrames;

    // The same crowd with no fields in it, so what the field pass costs is a difference rather
    // than a number with everything else baked into it. Best-of-N on both halves: under contention
    // the slow runs say nothing about this code, and the fast one is the only honest lower bound.
    Stage bare(kEntities);
    {
        std::vector<entity::EntityDesc> plain;
        plain.reserve(kEntities);
        for (int i = 0; i < kEntities; ++i) {
            plain.push_back(npc(i));
        }
        bare.install(plain, {});
        for (int i = 0; i < kEntities; ++i) {
            const float angle = static_cast<float>(i) * 0.61803f;
            const float radius = 40.0f + static_cast<float>(i % 97) * 5.0f;
            bare.place(nodeName(i),
                       glm::vec3(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius));
        }
    }
    double bareBest = 1e30;
    for (int run = 0; run < kRuns; ++run) {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kFrames; ++i) {
            bare.step(1.0 / 60.0);
        }
        const auto end = std::chrono::steady_clock::now();
        bareBest = std::min(bareBest, std::chrono::duration<double, std::micro>(end - start).count());
    }
    const double barePerFrame = bareBest / kFrames;
    WARN("best of " << kRuns << " runs of " << kFrames << " frames, " << kEntities
                    << " entities: " << barePerFrame << " us/frame with no fields, " << perFrame
                    << " us/frame with " << kFields << " fields; the field pass costs "
                    << (perFrame - barePerFrame) << " us/frame ("
                    << (perFrame - barePerFrame) / kEntities * 1000.0 << " ns per entity)");
    CHECK(perFrame < 1600.0); // a tenth of a 60 fps frame, the same budget the entity tick has
}

TEST_CASE("a distant entity does not pay for a field query every frame", "[entity][field][performance]") {
    // §29's three bands, applied to the field pass: the same fullDetailDistance / coarseInterval /
    // cullDistance an entity already carries for its behaviours.
    entity::EntityDesc near = npc(0);
    entity::EntityDesc coarse = npc(1);
    coarse.fullDetailDistance = 50.0f;
    coarse.coarseInterval = 0.5f;
    entity::EntityDesc culled = npc(2);
    culled.cullDistance = 100.0f;

    Stage s(3);
    s.install({near, coarse, culled}, {sphereField("stage", glm::vec3(0.0f), 10.0f)});
    s.view = glm::vec3(0.0f);
    s.place("npc0", glm::vec3(1.0f, 0.0f, 0.0f));
    s.place("npc1", glm::vec3(200.0f, 0.0f, 0.0f));
    s.place("npc2", glm::vec3(200.0f, 0.0f, 0.0f));

    s.step(1.0 / 60.0);
    CHECK(s.world.fieldCounts().governed == 3);
    // Only the near one. The culled entity is never asked, and the coarse one waits for its
    // interval to come round -- exactly as the behaviour pass already treats them.
    CHECK(s.world.fieldCounts().queried == 1);

    int queried = 0;
    for (int i = 0; i < 60; ++i) { // one second at 60 Hz
        s.step(1.0 / 60.0);
        queried += static_cast<int>(s.world.fieldCounts().queried);
    }
    // npc0 every frame (60), npc1 twice at a half-second interval, npc2 never.
    CHECK(queried >= 60);
    CHECK(queried <= 64);
}

// ---- §39-§43: things that are not characters -------------------------------------------------

TEST_CASE("a light, a material and a particle system react without a parallel path",
          "[entity][field][reaction]") {
    // The gap analysis said this was already legal: an entity with reactions and no behaviours,
    // driving any node. It is -- and a light, which is not a node at all, is reachable through the
    // '@' absolute path. Nothing here is a second reactivity mechanism; it is the first one,
    // pointed somewhere else.
    entity::EntityDesc lightRig;
    lightRig.name = "npc0";
    lightRig.node = "npc0";
    lightRig.reactions.push_back(
        {.signal = "field.stage.occupancy", .target = "@lightrig/key/stage/intensity", .depth = 8.0f});
    CHECK(lightRig.behaviors.empty());

    Stage s(2);
    entity::FieldDesc field = sphereField("stage", glm::vec3(0.0f), 10.0f);
    field.tags = {"dancer"};
    s.install({lightRig, npc(1, {"dancer"})}, {field});

    const auto* intensity = s.params.find("lightrig/key/stage/intensity");
    REQUIRE(intensity != nullptr);

    s.place("npc1", glm::vec3(40.0f, 0.0f, 0.0f));
    s.step(1.0 / 60.0);
    s.step(1.0 / 60.0); // the occupancy signal is published by the field pass of the frame before
    CHECK(intensity->finalComponent(0) == 0.0f);

    s.place("npc1", glm::vec3(0.0f));
    s.step(1.0 / 60.0);
    s.step(1.0 / 60.0);
    CHECK_THAT(intensity->finalComponent(0), WithinAbs(8.0, 1e-4));

    SECTION("a fixture that drives no node at all is not a warning") {
        // A light is not a composition node, so an entity whose reactions are every one of them
        // absolute has no use for one. Warning about the node it does not drive would train the
        // reader to ignore the warning that matters.
        entity::EntityDesc fixture;
        fixture.name = "houseLights";
        fixture.reactions.push_back(
            {.signal = "audio.rms", .target = "@lightrig/key/stage/intensity", .depth = 3.0f});
        Stage quiet(1);
        quiet.install({fixture}, {});
        CHECK(quiet.world.problems().empty());

        // The negative control: the same entity with something that *does* need a node.
        entity::EntityDesc broken = fixture;
        broken.behaviors.push_back(behavior("hover"));
        params::ParameterSet params;
        registerCrowd(params, 1);
        entity::EntityWorld world;
        world.setEntities({broken}, 1u);
        world.setBindings(crowdBindings(1));
        world.registerParameters(params);
        world.bind(params);
        REQUIRE_FALSE(world.problems().empty());
        CHECK_THAT(world.problems().front(), ContainsSubstring("houseLights"));
    }
}

// ---- §20: the profile library ----------------------------------------------------------------

TEST_CASE("ten NPCs share one profile by name", "[entity][field][serialisation]") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "avgen-profile-library-test";
    std::filesystem::create_directories(dir);
    const std::filesystem::path libraryPath = dir / "night-shift.profiles.json";
    {
        std::ofstream out(libraryPath);
        out << R"({
  "format": "avgen-entity-profile-library",
  "version": 1,
  "profiles": {
    "dancer": { "tags": ["crowd"],
                "reactions": [ { "signal": "audio.bass", "target": "parts/Lamp/emissiveGain", "depth": 2.5 } ],
                "clips": { "idle": "Idle", "walk": "Walk", "react": "Dance" } },
    "barfly": { "tags": ["crowd"],
                "reactions": [ { "signal": "audio.mid", "target": "parts/Lamp/emissiveGain", "depth": 0.4 } ] }
  }
})";
    }
    const auto library = entity::loadProfileLibrary(libraryPath);
    REQUIRE(library);
    CHECK(library->size() == 2);
    REQUIRE(library->find("dancer") != nullptr);
    CHECK(library->find("dancer")->reactions.size() == 1);
    CHECK(library->find("nobody") == nullptr);

    nlohmann::json entities = nlohmann::json::array();
    for (int i = 0; i < 10; ++i) {
        entities.push_back({{"name", nodeName(i)}, {"profile", "dancer"}});
    }
    entities.push_back({{"name", "npc10"},
                        {"profile", "dancer"},
                        {"reactions", nlohmann::json::array({{{"signal", "audio.treble"},
                                                              {"target", "parts/Lamp/emissiveGain"},
                                                              {"depth", 1.0}}})}});
    const auto read = entity::entitiesFromJson(entities, dir, &*library);
    REQUIRE(read);
    REQUIRE(read->size() == 11);
    for (int i = 0; i < 10; ++i) {
        CHECK((*read)[static_cast<std::size_t>(i)].reactions.size() == 1);
        CHECK((*read)[static_cast<std::size_t>(i)].profile == "dancer");
        CHECK((*read)[static_cast<std::size_t>(i)].clips.size() == 3);
        CHECK((*read)[static_cast<std::size_t>(i)].tags == std::vector<std::string>{"crowd"});
    }
    // The profile's reactions come first and the entity's own are appended, exactly as a profile
    // file behaves -- one rule, two spellings.
    CHECK((*read)[10].reactions.size() == 2);
    CHECK((*read)[10].reactions.back().signal == "audio.treble");

    // Saving names the profile and writes only what the entity added, so the library stays shared.
    const nlohmann::json written = entity::entityToJson((*read)[10]);
    CHECK(written["profile"] == "dancer");
    REQUIRE(written.contains("reactions"));
    CHECK(written["reactions"].size() == 1);
    CHECK_FALSE(written.contains("tags")); // the tag came from the profile; it is not re-inlined
    const auto again = entity::entitiesFromJson(nlohmann::json::array({written}), dir, &*library);
    REQUIRE(again);
    CHECK((*again)[0].reactions.size() == 2); // and it round-trips without growing

    SECTION("a name the library does not have names the ones it does") {
        nlohmann::json bad = nlohmann::json::array();
        bad.push_back({{"name", "npc0"}, {"profile", "danser"}});
        const auto failed = entity::entitiesFromJson(bad, dir, &*library);
        REQUIRE_FALSE(failed);
        CHECK_THAT(failed.error().message, ContainsSubstring("danser"));
        CHECK_THAT(failed.error().message, ContainsSubstring("dancer"));
        CHECK_THAT(failed.error().message, ContainsSubstring("barfly"));
    }

    SECTION("a profile that is a path still works when a library is present") {
        const std::filesystem::path single = dir / "solo.profile.json";
        {
            std::ofstream out(single);
            out << R"({"format":"avgen-entity-profile","version":1,
                       "reactions":[{"signal":"audio.rms","target":"parts/Lamp/emissiveGain","depth":9}]})";
        }
        nlohmann::json one = nlohmann::json::array();
        one.push_back({{"name", "npc0"}, {"profile", "solo.profile.json"}});
        const auto ok = entity::entitiesFromJson(one, dir, &*library);
        REQUIRE(ok);
        CHECK((*ok)[0].reactions.front().depth == 9.0f);
    }

    std::filesystem::remove_all(dir);
}

TEST_CASE("the shipped profile library loads and is asset-agnostic", "[entity][field][serialisation]") {
    // The example an author copies. It is in the tests because a library nobody reads is a library
    // that stops parsing the week after it is written, and because "nothing here names an asset" is
    // a claim worth checking rather than asserting in a comment.
    const auto library = entity::loadProfileLibrary(std::filesystem::path(AVGEN_SOURCE_DIR) /
                                                    "examples" / "entities" / "crowd.profiles.json");
    REQUIRE(library);
    CHECK(library->size() == 3);
    REQUIRE(library->find("dancer") != nullptr);
    REQUIRE(library->find("onlooker") != nullptr);
    REQUIRE(library->find("lamp") != nullptr);
    CHECK(library->find("lamp")->behaviors.empty()); // a prop: reactions and nothing else
    for (const std::string& name : library->names()) {
        const entity::EntityDesc* profile = library->find(name);
        REQUIRE(profile != nullptr);
        INFO("profile " << name);
        CHECK_FALSE(profile->reactions.empty());
        for (const entity::ReactionDesc& reaction : profile->reactions) {
            // No `parts/<material>` anywhere: every target is a property every node has, which is
            // what lets one library dress a city square, a club and a field of standing stones.
            CHECK_THAT(reaction.target, !ContainsSubstring("parts/"));
        }
    }
}

// ---- serialisation ---------------------------------------------------------------------------

TEST_CASE("a field round-trips through a scene file", "[entity][field][serialisation]") {
    const nlohmann::json j = nlohmann::json::parse(R"([
      { "name": "stage", "shape": "sphere", "center": [0, 1, -12], "radius": 14,
        "falloff": "inverseSquare", "inner": 0.25, "strength": 1.4, "floorGain": 0.1,
        "tags": ["dancer"], "source": "singer", "requireScrubExact": false,
        "arc": { "holdSeconds": 6, "holdJitter": 1.5, "delayJitter": 0.8, "activity": "observe",
                 "holdStill": true, "refractorySeconds": 2 } },
      { "name": "doorway", "shape": "box", "center": [4, 0, 0], "halfExtents": [1, 2, 0.5],
        "rotation": [0, 45, 0], "falloff": "constant", "scaleReactions": false },
      { "name": "column", "shape": "capsule", "radius": 2, "height": 8 }
    ])");
    const auto fields = entity::fieldsFromJson(j);
    REQUIRE(fields);
    REQUIRE(fields->size() == 3);
    CHECK((*fields)[0].volume.falloff == entity::Falloff::InverseSquare);
    CHECK((*fields)[0].strength == 1.4f);
    CHECK((*fields)[0].source == "singer");
    CHECK((*fields)[0].arc.enabled);
    CHECK((*fields)[0].arc.holdSeconds == 6.0f);
    CHECK((*fields)[0].arc.activity == "observe");
    CHECK((*fields)[0].arc.holdStill);
    CHECK((*fields)[1].volume.shape == entity::VolumeShape::Box);
    CHECK_FALSE((*fields)[1].scaleReactions);
    CHECK((*fields)[2].volume.shape == entity::VolumeShape::Capsule);
    CHECK((*fields)[2].arc.enabled == false);

    const auto again = entity::fieldsFromJson(entity::fieldsToJson(*fields));
    REQUIRE(again);
    REQUIRE(again->size() == 3);
    CHECK((*again)[0].volume.radius == (*fields)[0].volume.radius);
    CHECK((*again)[0].volume.inner == (*fields)[0].volume.inner);
    CHECK((*again)[0].arc.holdJitter == (*fields)[0].arc.holdJitter);
    CHECK((*again)[0].arc.refractorySeconds == 2.0f);
    CHECK((*again)[1].volume.rotation.y == 45.0f);
    CHECK((*again)[2].volume.height == 8.0f);

    SECTION("and a bad one says which field and what was wrong") {
        const auto bad = entity::fieldsFromJson(
            nlohmann::json::parse(R"([{"name":"stage","shape":"blob"}])"));
        REQUIRE_FALSE(bad);
        CHECK_THAT(bad.error().message, ContainsSubstring("stage"));
        CHECK_THAT(bad.error().message, ContainsSubstring("blob"));
        CHECK_THAT(bad.error().message, ContainsSubstring("capsule"));
        const auto twice = entity::fieldsFromJson(
            nlohmann::json::parse(R"([{"name":"a","radius":1},{"name":"a","radius":2}])"));
        REQUIRE_FALSE(twice);
        CHECK_THAT(twice.error().message, ContainsSubstring("twice"));
    }
}

TEST_CASE("the broad-phase grid finds what is near a box", "[entity][field][trigger]") {
    // The grid on its own, because a bug here is a bug that shows up as an entity that silently
    // never enters a field -- the exact failure mode this project keeps shipping.
    std::vector<glm::vec3> points;
    for (int i = 0; i < 1000; ++i) {
        const auto f = static_cast<float>(i);
        points.emplace_back(std::fmod(f * 7.3f, 400.0f) - 200.0f, 0.0f, std::fmod(f * 3.1f, 400.0f) - 200.0f);
    }
    entity::EntityGrid grid;
    grid.build(points, 12.0f);
    CHECK(grid.pointCount() == points.size());
    CHECK(grid.cellCount() > 1);

    const glm::vec3 lo(-30.0f, -5.0f, -30.0f);
    const glm::vec3 hi(30.0f, 5.0f, 30.0f);
    std::vector<std::uint32_t> found;
    grid.forEachNear(lo, hi, [&](std::uint32_t i) { found.push_back(i); });
    // Every point actually in the box has to be in the result; the result may hold more, because
    // this is the broad phase and the caller does the exact test.
    for (std::uint32_t i = 0; i < points.size(); ++i) {
        const glm::vec3& p = points[i];
        if (p.x >= lo.x && p.x <= hi.x && p.z >= lo.z && p.z <= hi.z) {
            INFO("point " << i);
            REQUIRE(std::find(found.begin(), found.end(), i) != found.end());
        }
    }
    CHECK(found.size() < points.size()); // and it is actually narrowing something

    SECTION("a point cloud spread over a continent still fits in a bounded grid") {
        std::vector<glm::vec3> wide{glm::vec3(-500000.0f, 0.0f, 0.0f), glm::vec3(500000.0f, 0.0f, 0.0f)};
        grid.build(wide, 1.0f, 4096);
        CHECK(grid.cellCount() <= 4096);
        CHECK(grid.cellSize() > 1.0f); // it inflated the cell rather than allocating a billion
        int seen = 0;
        grid.forEachNear(glm::vec3(-600000.0f), glm::vec3(600000.0f), [&](std::uint32_t) { ++seen; });
        CHECK(seen == 2);
    }

    SECTION("an empty build is empty, not undefined") {
        grid.build({}, 10.0f);
        CHECK(grid.pointCount() == 0);
        int seen = 0;
        grid.forEachNear(glm::vec3(-1.0f), glm::vec3(1.0f), [&](std::uint32_t) { ++seen; });
        CHECK(seen == 0);
    }
}


// ---- the whole path, through a scene file ----------------------------------------------------

TEST_CASE("a scene file installs a profile library, entities and a field that reaches a parameter",
          "[entity][field][composition][serialisation]") {
    // The end of the wire. This project has five times built a system, tested its pieces, and wired
    // it into nothing -- so the last test is the one that starts at JSON on disk and finishes at a
    // number in a parameter, through Composition::loadFile, attach(), and the three per-frame hooks
    // in the order app::Engine calls them.
    const std::filesystem::path dir = testsupport::processTempDir() / "avgen-field-scene";
    std::filesystem::create_directories(dir);
    {
        std::ofstream out(dir / "crowd.profiles.json");
        out << R"({ "format": "avgen-entity-profile-library", "version": 1,
                    "profiles": { "dancer": { "tags": ["crowd"], "reactions": [
                      { "signal": "audio.bass", "target": "emissiveBoost", "depth": 3.0 } ] } } })";
    }
    {
        std::ofstream out(dir / "club.json");
        out << R"({ "format": "avgen-scene", "version": 1, "name": "club",
          "nodes": [ { "kind": "orb", "name": "dancerA", "position": [0, 0, 0] },
                     { "kind": "orb", "name": "dancerB", "position": [40, 0, 0] },
                     { "kind": "orb", "name": "bartender", "position": [0, 0, 0] } ],
          "entityProfiles": "crowd.profiles.json",
          "entities": [ { "name": "dancerA", "profile": "dancer" },
                        { "name": "dancerB", "profile": "dancer" },
                        { "name": "bartender", "reactions": [
                          { "signal": "audio.bass", "target": "emissiveBoost", "depth": 3.0 } ] } ],
          "fields": [ { "name": "floor", "shape": "sphere", "center": [0, 0, 0], "radius": 10,
                        "falloff": "linear", "tags": ["crowd"] } ] })";
    }

    assets::AssetRegistry registry{dir};
    auto loaded = scene::Composition::loadFile("club.json", registry);
    REQUIRE(loaded);
    scene::Composition& comp = **loaded;
    CHECK(comp.entityProfileLibraryPath() == "crowd.profiles.json");
    REQUIRE(comp.entities().size() == 3);
    CHECK(comp.entities()[0].reactions.size() == 1); // it came from the library, by name
    CHECK(comp.entities()[0].tags == std::vector<std::string>{"crowd"});
    REQUIRE(comp.fields().size() == 1);
    CHECK(comp.fields()[0].volume.radius == 10.0f);

    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    const signals::SignalId bass = bus.declare("audio.bass", 0.0f, 1.0f);
    // A composition installs its own default routes; without these the bind below fails on them
    // rather than on anything to do with fields.
    for (const char* name : {"audio.mid", "audio.rms", "audio.treble"}) {
        bus.declare(name, 0.0f, 1.0f);
    }
    bus.declare("audio.onset", 0.0f, 1.0f, true);
    comp.attach(params, modulator);
    REQUIRE(comp.entityProblems().empty());
    REQUIRE(comp.entityWorld().fieldReport().size() == 1);
    CHECK_THAT(comp.entityWorld().fieldReport()[0], ContainsSubstring("static"));
    if (const auto bound = modulator.bind(bus, params); !bound) {
        FAIL_CHECK(bound.error().message);
    }

    const auto* a = params.find("nodes/dancerA/emissiveBoost");
    const auto* b = params.find("nodes/dancerB/emissiveBoost");
    const auto* bar = params.find("nodes/bartender/emissiveBoost");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(bar != nullptr);

    FrameTime time;
    time.renderTime = 0.0;
    time.deltaTime = 1.0 / 60.0;
    params.resetFinals();
    bus.set(bass, 1.0f);
    comp.updateFields(time, bus, modulator);
    modulator.applyRoutes(bus, params, time.deltaTime);
    comp.updateBehaviour(time, bus);

    // dancerA is at the centre of the field, dancerB is thirty metres outside it, and the bartender
    // carries no crowd tag so no field governs them at all. `emissiveBoost` rests at 1, so the
    // untouched answer is 1 + 3 and the silenced one is 1 + 0.
    CHECK_THAT(a->finalComponent(0), WithinAbs(4.0, 1e-4));
    CHECK_THAT(b->finalComponent(0), WithinAbs(1.0, 1e-4));
    CHECK_THAT(bar->finalComponent(0), WithinAbs(4.0, 1e-4));

    // And it survives a save: the library is named rather than inlined, and the field comes back.
    const nlohmann::json written = comp.toJson();
    CHECK(written["entityProfiles"] == "crowd.profiles.json");
    REQUIRE(written.contains("fields"));
    CHECK(written["fields"].size() == 1);
    CHECK(written["entities"][0]["profile"] == "dancer");
    CHECK_FALSE(written["entities"][0].contains("reactions")); // still shared, not copied
    REQUIRE(comp.saveFile(dir / "club-again.json"));
    auto again = scene::Composition::loadFile("club-again.json", registry);
    REQUIRE(again);
    REQUIRE((*again)->fields().size() == 1);
    CHECK((*again)->entities()[0].reactions.size() == 1);

    std::filesystem::remove_all(dir);
}

TEST_CASE("a field's strength, size and centre are keyframeable knobs", "[entity][field]") {
    // ADR-088's standard, applied to fields: an authored number that cannot be keyframed stops
    // being interesting the moment a shot needs it to change. The field pass reads the parameter
    // *finals*, which at that point in the frame are exactly base plus automation -- so a timeline
    // key lands and the field stays a pure function of time.
    Stage s(1);
    entity::FieldDesc field = sphereField("stage", glm::vec3(0.0f), 10.0f);
    field.strength = 1.0f;
    s.install({npc(0)}, {field});

    auto* strength = s.params.findAs<float>("entity/fields/stage/strength");
    auto* scale = s.params.findAs<float>("entity/fields/stage/scale");
    auto* centre = s.params.findAs<glm::vec3>("entity/fields/stage/center");
    auto* inner = s.params.findAs<float>("entity/fields/stage/inner");
    REQUIRE(strength != nullptr);
    REQUIRE(scale != nullptr);
    REQUIRE(centre != nullptr);
    REQUIRE(inner != nullptr);
    CHECK(strength->value() == 1.0f);
    CHECK(scale->value() == 1.0f);

    const auto at = [&](float x) {
        s.place("npc0", glm::vec3(x, 0.0f, 0.0f));
        s.step(1.0 / 60.0);
        return s.lamp(0);
    };
    CHECK_THAT(at(5.0f), WithinAbs(1.0, 1e-4)); // linear falloff, half way out, depth 2

    strength->setBase(3.0f);
    CHECK_THAT(at(5.0f), WithinAbs(3.0, 1e-4));

    strength->setBase(1.0f);
    scale->setBase(4.0f); // a 40 m field: 5 m out is now nearly the centre
    CHECK_THAT(at(5.0f), WithinAbs(1.75, 1e-3));

    scale->setBase(1.0f);
    centre->setBase(glm::vec3(60.0f, 0.0f, 0.0f)); // the field moves away and takes its gain with it
    CHECK(at(5.0f) == 0.0f);
    CHECK_THAT(at(60.0f), WithinAbs(2.0, 1e-4));

    SECTION("and they are unregistered with everything else on a scene swap") {
        s.world.unregisterParameters(s.params);
        CHECK(s.params.findAs<float>("entity/fields/stage/strength") == nullptr);
    }
}
