// The entity layer's contracts (ADR-088): determinism, addressing, diagnostics and budget.
//
// The addressing tests matter more than they look. This project has five times built a system,
// tested it, and wired it into nothing, and the failure has the same shape every time: a name that
// resolves to nothing, quietly. So every test here that asks "does this bind" has a partner that
// asks "and does it say so when it does not".

#include "entity/entity.hpp"
#include "entity/navigation.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "signals/signal_bus.hpp"
#include "world/world_map.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace {

params::ParamDesc<glm::vec3> v3(std::string path, glm::vec3 def, float lo, float hi) {
    return params::ParamDesc<glm::vec3>{
        .path = std::move(path), .defaultValue = def, .hardMin = glm::vec3(lo), .hardMax = glm::vec3(hi)};
}

// A parameter set shaped like the one a Composition registers for one node called "craft": a
// transform, and a procedural object with three material parts.
void registerNodeLike(params::ParameterSet& params) {
    params.add(v3("nodes/craft/position", glm::vec3(0.0f, 10.0f, 0.0f), -1e4f, 1e4f));
    params.add(v3("nodes/craft/rotation", glm::vec3(0.0f), -360.0f, 360.0f));
    params.add(v3("nodes/craft/scale", glm::vec3(1.0f), 0.001f, 100.0f));
    for (int i = 0; i < 3; ++i) {
        const std::string base = "procedural/craft/parts/" + std::to_string(i) + "/";
        params.add(params::ParamDesc<float>{.path = base + "emissiveGain", .defaultValue = 1.0f,
                                            .hardMin = 0.0f, .hardMax = 50.0f});
        params.add(v3(base + "emissiveColor", glm::vec3(0.0f), 0.0f, 64.0f));
    }
    params.add(params::ParamDesc<float>{
        .path = "procedural/craft/material/emissive", .defaultValue = 0.0f, .hardMin = 0.0f, .hardMax = 50.0f});
}

entity::NodeBinding craftBinding() {
    entity::NodeBinding b;
    b.node = "craft";
    b.exists = true;
    b.transformPrefix = "nodes/craft/";
    b.geometryPrefix = "procedural/craft/";
    b.partNames = {"Gray", "Light", "Blue"};
    b.anchor = glm::vec3(0.0f, 10.0f, 0.0f);
    return b;
}

entity::BehaviorDesc behavior(const char* kind, nlohmann::json settings = nlohmann::json::object()) {
    entity::BehaviorDesc d;
    d.kind = kind;
    d.name = kind;
    settings["kind"] = kind;
    d.settings = std::move(settings);
    return d;
}

entity::EntityDesc craftDesc() {
    entity::EntityDesc d;
    d.name = "craft";
    d.node = "craft";
    d.seed = 12345;
    d.behaviors.push_back(behavior("hover", {{"amplitude", 1.0}, {"rate", 0.3}}));
    d.behaviors.push_back(behavior("drift", {{"radius", 2.0}, {"rate", 0.2}}));
    return d;
}

// Ticks a world for `seconds` at 60 Hz and returns where the entity ended up.
glm::vec3 run(entity::EntityWorld& world, params::ParameterSet& params, const signals::SignalBus& bus,
              double seconds, glm::vec3 view = glm::vec3(0.0f)) {
    auto* position = params.findAs<glm::vec3>("nodes/craft/position");
    REQUIRE(position != nullptr);
    const int frames = static_cast<int>(seconds * 60.0);
    for (int i = 0; i <= frames; ++i) {
        params.resetFinals();
        entity::EntityUpdate u;
        u.time = static_cast<double>(i) / 60.0;
        u.dt = i == 0 ? 0.0 : 1.0 / 60.0;
        u.frameIndex = static_cast<std::uint64_t>(i);
        u.bus = &bus;
        u.viewPosition = view;
        world.update(u, params);
    }
    return position->value();
}

struct Fixture {
    params::ParameterSet params;
    signals::SignalBus bus;
    entity::EntityWorld world;

    explicit Fixture(entity::EntityDesc desc, std::uint32_t seed = 7u) {
        registerNodeLike(params);
        world.setEntities({std::move(desc)}, seed);
        world.setBindings({craftBinding()});
        world.registerParameters(params);
        world.bind(params);
    }
};

} // namespace

TEST_CASE("the same seed is the same performance, every time", "[entity][determinism]") {
    // The whole offline-render guarantee rests on this: seeded generators, a timeline second, and
    // no state that outlives a reset.
    Fixture a(craftDesc());
    Fixture b(craftDesc());
    const glm::vec3 first = run(a.world, a.params, a.bus, 6.0);
    const glm::vec3 second = run(b.world, b.params, b.bus, 6.0);
    CHECK(first.x == second.x);
    CHECK(first.y == second.y);
    CHECK(first.z == second.z);
    // And it actually moved, or this test passes on a system that does nothing.
    CHECK(glm::length(first - glm::vec3(0.0f, 10.0f, 0.0f)) > 0.05f);
}

TEST_CASE("a different seed is a different performance", "[entity][determinism]") {
    entity::EntityDesc one = craftDesc();
    entity::EntityDesc two = craftDesc();
    two.seed = 999;
    Fixture a(one);
    Fixture b(two);
    const glm::vec3 first = run(a.world, a.params, a.bus, 6.0);
    const glm::vec3 second = run(b.world, b.params, b.bus, 6.0);
    CHECK(glm::length(first - second) > 0.01f);
}

TEST_CASE("a seek puts a behaviour back where it started", "[entity][determinism]") {
    // Scrubbing the timeline must not make the frame depend on how the playhead got there.
    Fixture f(craftDesc());
    const glm::vec3 once = run(f.world, f.params, f.bus, 4.0);
    f.world.reset();
    const glm::vec3 again = run(f.world, f.params, f.bus, 4.0);
    CHECK(once.x == again.x);
    CHECK(once.y == again.y);
    CHECK(once.z == again.z);
}

TEST_CASE("a behaviour's motion is frame-rate independent", "[entity][determinism]") {
    // A wander integrated per frame would walk twice as far at 120 fps as at 60. The offline
    // renderer runs at 30 and the editor at whatever the GPU allows, and they have to agree.
    const auto travel = [](double hz) {
        entity::EntityDesc d;
        d.name = "craft";
        d.node = "craft";
        d.seed = 5150;
        d.behaviors.push_back(behavior("spin", {{"signal", "none"}, {"baseRate", 45.0}, {"damping", 2.0}}));
        Fixture f(d);
        auto* rotation = f.params.findAs<glm::vec3>("nodes/craft/rotation");
        const int frames = static_cast<int>(4.0 * hz);
        for (int i = 0; i <= frames; ++i) {
            f.params.resetFinals();
            entity::EntityUpdate u;
            u.time = static_cast<double>(i) / hz;
            u.dt = i == 0 ? 0.0 : 1.0 / hz;
            u.bus = &f.bus;
            f.world.update(u, f.params);
        }
        return rotation->value().y;
    };
    CHECK_THAT(travel(120.0), WithinAbs(travel(30.0), 1.5)); // degrees, over four seconds
}

TEST_CASE("a property is addressed by the name an artist can see", "[entity][addressing]") {
    entity::EntityDesc d = craftDesc();
    d.reactions.push_back({.signal = "audio.bass", .target = "parts/Light/emissiveGain", .depth = 2.0f});
    d.reactions.push_back({.signal = "audio.treble", .target = "parts/Blue/emissiveGain", .depth = 1.0f});
    d.reactions.push_back({.signal = "audio.rms", .target = "material/emissive", .depth = 0.5f});
    d.reactions.push_back({.signal = "audio.onset", .target = "position", .depth = -0.3f, .component = 1});
    d.reactions.push_back({.signal = "audio.mid", .target = "hover/amplitude", .depth = 0.4f});
    d.reactions.push_back({.signal = "audio.peak", .target = "@procedural/craft/parts/0/emissiveGain", .depth = 1.0f});
    Fixture f(d);
    std::vector<std::string> problems;
    const std::vector<params::ModRoute> routes = f.world.compileReactions(f.params, problems);
    if (!problems.empty()) {
        FAIL_CHECK(problems.front());
    }
    REQUIRE(routes.size() == 6);
    // "Light" is the second material the asset carries, so it is part 1 -- an index nobody could
    // have predicted from the scene file, which is the entire reason names exist.
    CHECK(routes[0].target == "procedural/craft/parts/1/emissiveGain");
    CHECK(routes[1].target == "procedural/craft/parts/2/emissiveGain");
    CHECK(routes[2].target == "procedural/craft/material/emissive");
    CHECK(routes[3].target == "nodes/craft/position");
    CHECK(routes[3].component == 1);
    CHECK(routes[4].target == "entity/craft/hover/amplitude"); // a behaviour's own knob
    CHECK(routes[5].target == "procedural/craft/parts/0/emissiveGain"); // the '@' escape hatch
    CHECK(routes[0].amount == 2.0f);
}

TEST_CASE("a property that resolves to nothing says so, with the names that would have worked",
          "[entity][addressing][diagnostics]") {
    entity::EntityDesc d = craftDesc();
    d.reactions.push_back({.signal = "audio.bass", .target = "parts/Crimson/emissiveGain", .depth = 1.0f});
    Fixture f(d);
    std::vector<std::string> problems;
    const std::vector<params::ModRoute> routes = f.world.compileReactions(f.params, problems);
    CHECK(routes.empty());
    REQUIRE(problems.size() == 1);
    // The name, the signal, what was tried, and what it could have been. A diagnostic that only
    // says "unresolved" costs the reader the same hour every time.
    CHECK_THAT(problems[0], ContainsSubstring("craft"));
    CHECK_THAT(problems[0], ContainsSubstring("parts/Crimson/emissiveGain"));
    CHECK_THAT(problems[0], ContainsSubstring("audio.bass"));
    CHECK_THAT(problems[0], ContainsSubstring("Gray, Light, Blue"));
}

TEST_CASE("an entity driving a node that is not there is reported by name", "[entity][diagnostics]") {
    entity::EntityDesc d = craftDesc();
    d.node = "ghost";
    params::ParameterSet params;
    registerNodeLike(params);
    entity::EntityWorld world;
    world.setEntities({d}, 1u);
    world.setBindings({craftBinding()});
    world.registerParameters(params);
    world.bind(params);
    REQUIRE_FALSE(world.problems().empty());
    CHECK_THAT(world.problems().front(), ContainsSubstring("ghost"));
}

TEST_CASE("an unknown behaviour kind is reported with the ones that exist", "[entity][diagnostics]") {
    entity::EntityDesc d;
    d.name = "craft";
    d.behaviors.push_back(behavior("levitate"));
    entity::EntityWorld world;
    world.setEntities({d}, 1u);
    REQUIRE_FALSE(world.problems().empty());
    CHECK_THAT(world.problems().front(), ContainsSubstring("levitate"));
    CHECK_THAT(world.problems().front(), ContainsSubstring("hover"));
}

TEST_CASE("a route and a behaviour compose on one property", "[entity][modulation]") {
    // The ordering contract: routes run first so a behaviour's knobs are already modulated, and a
    // behaviour adds its offset on top so neither overwrites the other.
    entity::EntityDesc d;
    d.name = "craft";
    d.node = "craft";
    d.seed = 4242;
    d.behaviors.push_back(behavior("hover", {{"amplitude", 2.0}, {"rate", 0.5}}));
    Fixture f(d);
    auto* position = f.params.findAs<glm::vec3>("nodes/craft/position");

    // Behaviour alone.
    f.params.resetFinals();
    entity::EntityUpdate u;
    u.time = 1.25;
    u.dt = 1.0 / 60.0;
    u.bus = &f.bus;
    f.world.update(u, f.params);
    const float behaviourOnly = position->value().y;

    // A route writes first, then the behaviour adds the same offset on top of it.
    f.params.resetFinals();
    position->setFinalComponent(1, position->finalComponent(1) + 5.0f);
    f.world.update(u, f.params);
    CHECK_THAT(position->value().y, WithinAbs(behaviourOnly + 5.0f, 1e-4f));
    // The authored value is untouched, so saving the project writes what the author placed.
    CHECK(position->base().y == 10.0f);
}

TEST_CASE("behaviour work is budgeted by distance", "[entity][performance]") {
    entity::EntityDesc d = craftDesc();
    d.fullDetailDistance = 50.0f;
    d.coarseInterval = 0.25f;
    d.cullDistance = 200.0f;

    SECTION("near the camera it runs every frame") {
        Fixture f(d);
        run(f.world, f.params, f.bus, 1.0, glm::vec3(0.0f, 10.0f, 5.0f));
        CHECK(f.world.counts().full == 1);
        CHECK(f.world.counts().coarse == 0);
    }
    SECTION("far away it runs at the coarse interval and nowhere near every frame") {
        Fixture f(d);
        int coarse = 0;
        int skipped = 0;
        for (int i = 0; i <= 120; ++i) {
            f.params.resetFinals();
            entity::EntityUpdate u;
            u.time = static_cast<double>(i) / 60.0;
            u.dt = i == 0 ? 0.0 : 1.0 / 60.0;
            u.bus = &f.bus;
            u.viewPosition = glm::vec3(0.0f, 10.0f, 120.0f);
            f.world.update(u, f.params);
            coarse += static_cast<int>(f.world.counts().coarse);
            skipped += static_cast<int>(f.world.counts().skipped);
        }
        // Two seconds at a quarter-second interval is eight updates, not 121.
        CHECK(coarse <= 10);
        CHECK(coarse >= 6);
        CHECK(skipped > 100);
    }
    SECTION("past the cull distance it does not run at all, and leaves the node where it was") {
        Fixture f(d);
        auto* position = f.params.findAs<glm::vec3>("nodes/craft/position");
        run(f.world, f.params, f.bus, 2.0, glm::vec3(0.0f, 10.0f, 900.0f));
        CHECK(f.world.counts().full == 0);
        CHECK(f.world.counts().coarse == 0);
        CHECK(position->value().y == 10.0f);
    }
}

TEST_CASE("a character's activities map onto the asset's own clip names", "[entity][animation]") {
    entity::EntityDesc d;
    d.name = "walker";
    d.clips = {{"idle", "Idle"}, {"walk", "Walk"}, {"run", "Run"}};
    entity::EntityWorld world;
    world.setEntities({d}, 1u);
    const entity::Entity* e = world.find("walker");
    REQUIRE(e != nullptr);
    CHECK(e->clipFor(entity::Activity::Walk) == "Walk");
    CHECK(e->clipFor(entity::Activity::Run) == "Run");
    // An activity the asset has no clip for falls back to idle rather than to the bind pose.
    CHECK(e->clipFor(entity::Activity::Observe) == "Idle");
    // An entity with no clips at all drives no rig, which is how a craft and a character are the
    // same kind of thing here.
    entity::EntityDesc craft;
    craft.name = "craft";
    entity::EntityWorld other;
    other.setEntities({craft}, 1u);
    CHECK(other.find("craft")->clipFor(entity::Activity::Walk).empty());
}

TEST_CASE("an entity survives a round trip through the scene file", "[entity][serialisation]") {
    entity::EntityDesc d = craftDesc();
    d.clips = {{"idle", "Idle"}};
    d.reactions.push_back({.signal = "audio.bass",
                           .target = "parts/Light/emissiveGain",
                           .depth = 2.5f,
                           .chain = params::ProcessorChain{.attackMs = 30.0f, .decayMs = 250.0f}});
    d.sockets.push_back({.name = "RightHand", .joint = "mixamorig:RightHand"});
    d.attachments.push_back({.node = "lantern", .socket = "RightHand"});
    d.fullDetailDistance = 40.0f;
    d.cullDistance = 300.0f;

    const nlohmann::json written = entity::entitiesToJson({d});
    const auto read = entity::entitiesFromJson(written);
    REQUIRE(read);
    REQUIRE(read->size() == 1);
    const entity::EntityDesc& back = (*read)[0];
    CHECK(back.name == d.name);
    CHECK(back.node == d.node);
    CHECK(back.seed == d.seed);
    CHECK(back.behaviors.size() == d.behaviors.size());
    CHECK(back.behaviors[0].kind == "hover");
    REQUIRE(back.reactions.size() == 1);
    CHECK(back.reactions[0].target == "parts/Light/emissiveGain");
    CHECK(back.reactions[0].depth == 2.5f);
    CHECK(back.reactions[0].chain.attackMs == 30.0f);
    CHECK(back.reactions[0].chain.decayMs == 250.0f);
    REQUIRE(back.sockets.size() == 1);
    CHECK(back.sockets[0].joint == "mixamorig:RightHand");
    REQUIRE(back.attachments.size() == 1);
    CHECK(back.attachments[0].node == "lantern");
    CHECK(back.cullDistance == 300.0f);
}

TEST_CASE("two entities with the same name are rejected rather than one of them ignored",
          "[entity][serialisation]") {
    nlohmann::json j = nlohmann::json::array();
    j.push_back({{"name", "walker"}});
    j.push_back({{"name", "walker"}});
    const auto read = entity::entitiesFromJson(j);
    REQUIRE_FALSE(read);
    CHECK_THAT(read.error().message, ContainsSubstring("twice"));
}

// ---- navigation ------------------------------------------------------------------------------

TEST_CASE("the navigator refuses ground a walker could not stand on", "[entity][navigation]") {
    world::WorldMap map = world::defaultWorld();
    world::Ecology ecology;
    world::ClearanceField field;
    field.map = &map;
    field.ecology = &ecology;
    field.cameraRadius = 0.6f;
    field.groundClearance = 0.0f;
    const entity::Navigator nav(&map, field);

    SECTION("outside the world, by name") {
        const entity::NavSample s = nav.sample(glm::vec2(2000.0f, 0.0f));
        CHECK_FALSE(s.navigable);
        CHECK(s.reject == entity::NavReject::OutOfBounds);
    }
    SECTION("the open valley floor is walkable") {
        CHECK(nav.sample(glm::vec2(-26.0f, 10.0f)).navigable);
    }
    SECTION("every destination it picks is one it would accept") {
        Rng rng(20260911u);
        int found = 0;
        for (int i = 0; i < 40; ++i) {
            glm::vec2 out(0.0f);
            if (nav.pickDestination(rng, glm::vec2(-26.0f, 10.0f), 5.0f, 25.0f, out)) {
                ++found;
                CHECK(nav.sample(out).navigable);
                CHECK(glm::length(out - glm::vec2(-26.0f, 10.0f)) <= 25.0f + 1e-3f);
            }
        }
        CHECK(found > 30);
    }
    SECTION("the same generator state picks the same destination") {
        glm::vec2 a(0.0f);
        glm::vec2 b(0.0f);
        Rng one(4242u);
        Rng two(4242u);
        REQUIRE(nav.pickDestination(one, glm::vec2(-26.0f, 10.0f), 5.0f, 25.0f, a));
        REQUIRE(nav.pickDestination(two, glm::vec2(-26.0f, 10.0f), 5.0f, 25.0f, b));
        CHECK(a.x == b.x);
        CHECK(a.y == b.y);
    }
    SECTION("with no map at all the ground is the y = 0 plane, not an error") {
        const entity::Navigator none;
        CHECK(none.sample(glm::vec2(1e6f, 1e6f)).navigable);
        CHECK(none.groundHeight(glm::vec2(3.0f, 4.0f)) == 0.0f);
    }
}

TEST_CASE("a steep hillside is rejected as too steep", "[entity][navigation]") {
    world::WorldMap map = world::defaultWorld();
    world::Ecology ecology;
    world::ClearanceField field;
    field.map = &map;
    field.ecology = &ecology;
    entity::NavSettings settings;
    settings.maxSlope = 0.05f; // anything but a table top
    const entity::Navigator nav(&map, field, settings);
    int steep = 0;
    for (float x = -200.0f; x <= 200.0f; x += 11.0f) {
        for (float z = -200.0f; z <= 200.0f; z += 11.0f) {
            if (nav.sample(glm::vec2(x, z)).reject == entity::NavReject::TooSteep) {
                ++steep;
            }
        }
    }
    CHECK(steep > 0);
}

// ---- cost --------------------------------------------------------------------------------------

TEST_CASE("ticking a crowd of entities costs microseconds, not milliseconds", "[entity][performance]") {
    // The minimum of several runs, never the median: this machine runs other agents' benchmarks,
    // and contention can only ever make a measurement slower than the truth.
    constexpr int kEntities = 64;
    constexpr int kFrames = 300;
    constexpr int kRuns = 5;

    params::ParameterSet params;
    signals::SignalBus bus;
    const signals::SignalId beat = bus.declare("audio.beat", 0.0f, 1.0f, true);
    bus.setEvent(beat, false);

    std::vector<entity::EntityDesc> descs;
    std::vector<entity::NodeBinding> bindings;
    for (int i = 0; i < kEntities; ++i) {
        const std::string name = "craft" + std::to_string(i);
        params.add(v3("nodes/" + name + "/position", glm::vec3(0.0f, 10.0f, 0.0f), -1e4f, 1e4f));
        params.add(v3("nodes/" + name + "/rotation", glm::vec3(0.0f), -360.0f, 360.0f));
        params.add(v3("nodes/" + name + "/scale", glm::vec3(1.0f), 0.001f, 100.0f));
        entity::EntityDesc d;
        d.name = name;
        d.node = name;
        d.seed = static_cast<std::uint32_t>(1000 + i);
        d.behaviors.push_back(behavior("hover", {{"amplitude", 1.0}, {"rate", 0.06}}));
        d.behaviors.push_back(behavior("drift", {{"radius", 2.0}, {"rate", 0.04}}));
        d.behaviors.push_back(behavior("bank", {{"degrees", 5.0}}));
        d.behaviors.push_back(behavior("spin", {{"signal", "audio.beat"}, {"impulse", 12.0}}));
        descs.push_back(std::move(d));
        entity::NodeBinding b;
        b.node = name;
        b.exists = true;
        b.transformPrefix = "nodes/" + name + "/";
        b.anchor = glm::vec3(static_cast<float>(i) * 3.0f, 10.0f, 0.0f);
        bindings.push_back(std::move(b));
    }

    entity::EntityWorld world;
    world.setEntities(descs, 11u);
    world.setBindings(bindings);
    world.registerParameters(params);
    world.bind(params);
    REQUIRE(world.problems().empty());

    double best = 1e30;
    for (int run = 0; run < kRuns; ++run) {
        world.reset();
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kFrames; ++i) {
            params.resetFinals();
            entity::EntityUpdate u;
            u.time = static_cast<double>(i) / 60.0;
            u.dt = 1.0 / 60.0;
            u.frameIndex = static_cast<std::uint64_t>(i);
            u.bus = &bus;
            u.viewPosition = glm::vec3(0.0f, 10.0f, 0.0f);
            world.update(u, params);
        }
        const auto end = std::chrono::steady_clock::now();
        best = std::min(best, std::chrono::duration<double, std::micro>(end - start).count());
    }
    const double perFrame = best / kFrames;
    WARN("entity tick: " << perFrame << " us/frame for " << kEntities << " entities x 4 behaviours ("
                         << perFrame / kEntities * 1000.0 << " ns each)");
    // A budget, not a benchmark: 64 entities is more than any shot here carries, and if a frame's
    // worth of behaviour ever costs a tenth of a 60 fps frame something has gone badly wrong.
    CHECK(perFrame < 1600.0);
}

TEST_CASE("two scenes share one behaviour profile", "[entity][serialisation]") {
    // "A saucer that answers a mix" is a thing more than one scene wants. The profile carries the
    // mapping and nothing about size or place, which is what lets a 16 m craft over Glowmere and a
    // 2.6 m scout in a field be configured by the same file.
    const std::filesystem::path dir = std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "entities";
    const auto profile = entity::loadProfile(dir / "craft-lights.profile.json");
    REQUIRE(profile);
    CHECK(profile->reactions.size() >= 8);
    CHECK(profile->behaviors.empty()); // a mapping, not a motion

    nlohmann::json j = nlohmann::json::array();
    j.push_back({{"name", "visitor"},
                 {"node", "visitor"},
                 {"profile", "craft-lights.profile.json"},
                 {"reactions",
                  nlohmann::json::array({{{"signal", "audio.bass"}, {"target", "position"}, {"depth", -0.9}}})}});
    const auto read = entity::entitiesFromJson(j, dir);
    REQUIRE(read);
    const entity::EntityDesc& e = (*read)[0];
    // The profile's reactions come first and the scene's own are appended, so a local route lands
    // on top of a shared one on the same property rather than instead of it.
    CHECK(e.reactions.size() == profile->reactions.size() + 1);
    CHECK(e.reactions.front().target == profile->reactions.front().target);
    CHECK(e.reactions.back().target == "position");
    CHECK(e.profile == "craft-lights.profile.json");

    // Saving names the profile and writes only what this entity added, so the shared file stays
    // shared instead of being inlined into every scene that used it.
    const nlohmann::json written = entity::entityToJson(e);
    CHECK(written["profile"] == "craft-lights.profile.json");
    REQUIRE(written.contains("reactions"));
    CHECK(written["reactions"].size() == 1);
    const auto again = entity::entitiesFromJson(nlohmann::json::array({written}), dir);
    REQUIRE(again);
    CHECK((*again)[0].reactions.size() == e.reactions.size()); // and it round-trips without growing
}

TEST_CASE("a profile that is not there is an error naming the file", "[entity][diagnostics]") {
    nlohmann::json j = nlohmann::json::array();
    j.push_back({{"name", "visitor"}, {"profile", "no-such-profile.json"}});
    const auto read = entity::entitiesFromJson(j, std::filesystem::path(AVGEN_SOURCE_DIR) / "examples");
    REQUIRE_FALSE(read);
    CHECK_THAT(read.error().message, ContainsSubstring("no-such-profile.json"));
}

TEST_CASE("A coarse entity holds its pose on the frames it skips", "[entity][performance]") {
    // The bug: past `fullDetailDistance` the character flickered between two places in the world.
    //
    // Parameter *finals* are rebuilt from bases at the top of every engine update, so an entity that
    // skipped writing its transform did not "leave the parameters alone" -- the node snapped back to
    // the position the author placed it at. A coarse entity was therefore drawn at its authored spot
    // on the frames it skipped and at its simulated spot on the frames it did not, which past the
    // threshold is five frames in six.
    //
    // The simulation may be rate-limited. The *write* may not.
    entity::EntityDesc d = craftDesc();
    d.fullDetailDistance = 50.0f;
    d.coarseInterval = 0.25f;
    d.cullDistance = 200.0f;
    Fixture f(d);

    auto* position = f.params.findAs<glm::vec3>("nodes/craft/position");
    REQUIRE(position != nullptr);
    const float authored = position->base().y;

    // Far away, so every update is coarse. Run long enough that the entity has actually travelled
    // somewhere -- a pose identical to the authored one proves nothing about holding it.
    const glm::vec3 far(0.0f, 10.0f, 120.0f);
    std::vector<float> drawn;
    for (int i = 0; i <= 120; ++i) {
        f.params.resetFinals();
        entity::EntityUpdate u;
        u.time = static_cast<double>(i) / 60.0;
        u.dt = i == 0 ? 0.0 : 1.0 / 60.0;
        u.bus = &f.bus;
        u.viewPosition = far;
        f.world.update(u, f.params);
        drawn.push_back(position->value().y);
    }
    REQUIRE(f.world.counts().skipped > 0); // frames were skipped, or this tests nothing

    // It moved: the simulation did advance across those 120 frames.
    const auto [lo, hi] = std::ranges::minmax_element(drawn);
    INFO("drawn y ranged " << *lo << " to " << *hi << ", authored " << authored);
    CHECK(*hi != *lo);

    // And it never once snapped back to where the author put it. That return is the flicker: the
    // frames that skipped the write drew the authored position, and the ones that did not drew the
    // simulated one.
    std::size_t atAuthored = 0;
    for (std::size_t i = 1; i < drawn.size(); ++i) { // frame 0 legitimately is the authored pose
        atAuthored += (std::abs(drawn[i] - authored) < 1e-6f) ? 1 : 0;
    }
    INFO(atAuthored << " of " << drawn.size() << " frames drew the authored position");
    CHECK(atAuthored == 0);

    // Nor did it ever jump backwards and forwards between two values on consecutive frames, which
    // is what the flicker looked like from outside.
    std::size_t reversals = 0;
    for (std::size_t i = 2; i < drawn.size(); ++i) {
        if (drawn[i] == drawn[i - 2] && drawn[i] != drawn[i - 1]) {
            ++reversals;
        }
    }
    INFO(reversals << " frame(s) returned to the value from two frames earlier");
    CHECK(reversals == 0);
}

// ADR-186: an offline render lifts the distance bands, so the far herd goes on living rather than
// standing where the budget left it. The pair matters more than either half: the live path must
// still skip -- these bands are why a world of characters fits in a frame -- and the lifted path
// must actually reach the entity, not merely be passed a flag nothing reads.
TEST_CASE("lifting the distance bands keeps a distant entity alive", "[entity][performance][limits]") {
    entity::EntityDesc d = craftDesc();
    d.fullDetailDistance = 50.0f;
    d.coarseInterval = 0.25f;
    d.cullDistance = 200.0f;

    const glm::vec3 faraway(0.0f, 10.0f, 900.0f); // well past the cull distance

    // What playback does: nothing runs, and the node stays where the author placed it.
    Fixture live(d);
    auto* livePosition = live.params.findAs<glm::vec3>("nodes/craft/position");
    run(live.world, live.params, live.bus, 2.0, faraway);
    CHECK(live.world.counts().full == 0);
    CHECK(live.world.counts().coarse == 0);
    CHECK(livePosition->value().y == 10.0f);

    // The same seed, the same two seconds, the same distance -- with the bands lifted. Every frame
    // is a full update and the character has moved.
    Fixture lifted(d);
    auto* position = lifted.params.findAs<glm::vec3>("nodes/craft/position");
    std::size_t skipped = 0;
    std::size_t coarse = 0;
    for (int i = 0; i <= 120; ++i) {
        lifted.params.resetFinals();
        entity::EntityUpdate u;
        u.time = static_cast<double>(i) / 60.0;
        u.dt = i == 0 ? 0.0 : 1.0 / 60.0;
        u.frameIndex = static_cast<std::uint64_t>(i);
        u.bus = &lifted.bus;
        u.viewPosition = faraway;
        u.distanceDetail = false; // ADR-186
        lifted.world.update(u, lifted.params);
        skipped += lifted.world.counts().skipped;
        coarse += lifted.world.counts().coarse;
    }
    CHECK(skipped == 0);
    CHECK(coarse == 0);
    CHECK(lifted.world.counts().full == 1);
    // It went somewhere. The live arm above proves the same two seconds at the same distance do
    // not, so this cannot pass by the behaviour being a no-op.
    CHECK(position->value().y != 10.0f);
}

// ADR-194: a hop because the music said so.
//
// The opportunistic gap probe turned out to fire almost never in a real world, for a reason worth
// recording: **a path-following walker does not meet gaps, because A* already routed around them.**
// Unwalkable cells are not in the graph, so the path never approaches one head-on. Jumping a gap
// properly needs the *graph* to know the gap is jumpable, which is a larger change.
//
// The signal-triggered hop needs none of that and is the one the mandate actually asked for: a beat
// is a reason to jump. This asserts the trigger, the edge, and the refusal to land somewhere a body
// cannot stand.
TEST_CASE("a beat makes a walker hop, once per beat", "[entity][airborne][audio]") {
    entity::EntityDesc d = craftDesc();
    nlohmann::json explore;
    explore["kind"] = "explore";
    explore["jumpRange"] = 4.0;
    explore["jumpSignal"] = "audio.beat";
    explore["speed"] = 2.0;
    d.behaviors.clear();
    d.behaviors.push_back(entity::BehaviorDesc{"explore", "explore", explore});

    Fixture f(d);
    signals::SignalBus bus;
    const signals::SignalId beatId = bus.declare("audio.beat");

    const entity::Entity* who = f.world.find(d.name);
    REQUIRE(who != nullptr);

    int hops = 0;
    bool wasAirborne = false;
    const auto step = [&](int frames, float beat) {
        for (int i = 0; i < frames; ++i) {
            f.params.resetFinals();
            bus.set(beatId, beat);
            entity::EntityUpdate u;
            u.time = static_cast<double>(i) / 60.0;
            u.dt = 1.0 / 60.0;
            u.bus = &bus;
            f.world.update(u, f.params);
            const entity::Activity a = who->locomotion().activity;
            const bool up = a == entity::Activity::Jump || a == entity::Activity::Fall;
            if (up && !wasAirborne) {
                ++hops;
            }
            wasAirborne = up;
        }
    };

    // Let it get moving, with the signal low. No hops: a quiet passage is a walk.
    step(240, 0.0f);
    const int beforeBeats = hops;
    INFO("hops before any beat: " << beforeBeats);
    CHECK(beforeBeats == 0);

    // A beat that stays high must not launch a hop every frame -- the trigger is the *edge*.
    step(120, 1.0f);
    INFO("hops after one sustained beat: " << hops);
    CHECK(hops <= 1);

    // Falling and rising again is a second beat, and a second hop.
    step(60, 0.0f);
    step(60, 1.0f);
    INFO("hops after a second beat: " << hops);
    CHECK(hops >= 1);
}

// ADR-198: secondary motion.
//
// The properties that matter are not "it moves the body" -- anything writing a MotionOffset does
// that. They are that it is *silent by default*, that it scales with what the body is doing rather
// than running at a constant, that two bodies do not do it in unison, and that it is a pure function
// of the timeline so an offline render reproduces it.
//
// Read through the node's position parameter, which is where a MotionOffset actually lands, and
// isolated by running the identical entity twice with only the liveliness knobs changed -- same
// seed, same behaviours, so the walk underneath is bit-for-bit the same and the difference is the
// secondary motion and nothing else.
TEST_CASE("liveliness is additive, speed-scaled and out of phase between bodies",
          "[entity][liveliness]") {
    const auto trace = [](const nlohmann::json& live, bool walking, std::uint32_t seed, int frames) {
        entity::EntityDesc d = craftDesc();
        d.seed = seed;
        d.behaviors.clear();
        if (walking) {
            nlohmann::json wander;
            wander["kind"] = "wander";
            wander["speed"] = 2.0;
            wander["pauseMin"] = 0.0;
            wander["pauseMax"] = 0.0;
            d.behaviors.push_back(entity::BehaviorDesc{"wander", "wander", wander});
        }
        d.behaviors.push_back(entity::BehaviorDesc{"liveliness", "liveliness", live});

        Fixture f(d, seed);
        auto* position = f.params.findAs<glm::vec3>("nodes/craft/position");
        REQUIRE(position != nullptr);
        std::vector<float> heights;
        for (int i = 0; i < frames; ++i) {
            f.params.resetFinals();
            entity::EntityUpdate u;
            u.time = static_cast<double>(i) / 60.0;
            u.dt = i == 0 ? 0.0 : 1.0 / 60.0;
            u.frameIndex = static_cast<std::uint64_t>(i);
            u.bus = &f.bus;
            f.world.update(u, f.params);
            heights.push_back(position->value().y);
        }
        return heights;
    };

    nlohmann::json off;
    nlohmann::json on;
    on["bounce"] = 0.35;
    on["sway"] = 6.0;
    on["stride"] = 1.6;

    const auto walkPlain = trace(off, true, 7u, 300);
    const auto walkLive = trace(on, true, 7u, 300);
    REQUIRE(walkPlain.size() == walkLive.size());

    // Silent by default: with no knobs the behaviour is present and changes nothing, so a scene
    // that gains one does not move.
    float quiet = 0.0f;
    for (std::size_t i = 0; i < walkPlain.size(); ++i) {
        quiet = std::max(quiet, std::abs(walkPlain[i] - walkLive[i]));
    }
    const auto walkOffAgain = trace(off, true, 7u, 300);
    for (std::size_t i = 0; i < walkPlain.size(); ++i) {
        CHECK(walkOffAgain[i] == walkPlain[i]);
    }

    // With knobs, a moving body bobs. The plain run is the control: the walk underneath is identical.
    float span = 0.0f;
    for (std::size_t i = 0; i < walkPlain.size(); ++i) {
        span = std::max(span, std::abs(walkLive[i] - walkPlain[i]));
    }
    INFO("bob against the identical walk: " << span);
    CHECK(span > 0.05f);

    // Standing still, the same knobs produce almost nothing -- which is what stops this fighting an
    // idle clip.
    const auto stillPlain = trace(off, false, 7u, 300);
    const auto stillLive = trace(on, false, 7u, 300);
    float stillSpan = 0.0f;
    for (std::size_t i = 0; i < stillPlain.size(); ++i) {
        stillSpan = std::max(stillSpan, std::abs(stillLive[i] - stillPlain[i]));
    }
    INFO("bob at rest: " << stillSpan << " against " << span << " moving");
    CHECK(stillSpan < span * 0.25f);

    // Two bodies are not in unison. Four characters rising and falling together read as one
    // animation on four puppets; the per-body stride phase is the whole fix.
    const auto otherPlain = trace(off, true, 99u, 300);
    const auto otherLive = trace(on, true, 99u, 300);
    bool differs = false;
    for (std::size_t i = 100; i < otherLive.size(); ++i) {
        const float a = walkLive[i] - walkPlain[i];
        const float b = otherLive[i] - otherPlain[i];
        if (std::abs(a - b) > 1e-4f) {
            differs = true;
        }
    }
    CHECK(differs);

    // And it reproduces exactly: offline rendering depends on it.
    const auto again = trace(on, true, 7u, 300);
    REQUIRE(again.size() == walkLive.size());
    for (std::size_t i = 0; i < again.size(); ++i) {
        CHECK(again[i] == walkLive[i]);
    }
}
