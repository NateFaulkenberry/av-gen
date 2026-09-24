// EMIT (ADR-703): the Particle Emitter effect type, through the real engine.
//
// What is asked here is what makes an effect-owned particle system an EFFECT rather than a particle
// system somebody placed: its numbers are `fx/<id>/...` and nothing else, it rides its owner, it
// leaves when its instance leaves, it reports its status, and it is capped with a reason.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "scene/composition.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"
#include "world/effects/particle_emitter.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace avgen;

namespace {

const scene::ParticleSystem* systemOf(const app::Engine& engine, const std::string& id) {
    const std::string name = world::particleSystemName(id);
    for (const scene::ParticleSystem& s : const_cast<app::Engine&>(engine).scene().particles) {
        if (s.name == name) {
            return &s;
        }
    }
    return nullptr;
}

void step(app::Engine& engine, double seconds) {
    engine.update(FrameTime{seconds, 1.0 / 60.0, static_cast<std::uint64_t>(seconds * 60.0)});
}

world::EffectInstance emitter(const world::EffectOwner& owner, const char* style = "Meadow Fireflies") {
    world::EffectInstance e = world::makeEffect(world::EffectKind::ParticleEmitter, "Particles");
    e.id.clear();
    e.owner = owner;
    REQUIRE(world::applyEffectStyle(e, world::EffectKind::ParticleEmitter, style));
    return e;
}

scene::CompositionNode orb(const std::string& name, glm::vec3 at) {
    scene::CompositionNode n;
    n.name = name;
    n.kind = scene::NodeKind::Orb;
    n.transform.position = at;
    return n;
}

} // namespace

TEST_CASE("a World-owned emitter writes one fx system, driven only by its fx/ parameters",
          "[effects][emit]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    std::vector<world::EffectInstance> list;
    const auto id = world::insertEffect(list, emitter(world::EffectOwner::world()));
    REQUIRE(id.has_value());
    REQUIRE(engine.setEffects(list).has_value());
    step(engine, 1.0);

    const scene::ParticleSystem* s = systemOf(engine, *id);
    REQUIRE(s != nullptr);
    CHECK(s->enabled);
    CHECK(s->spawnRate == Catch::Approx(60.0f)); // Meadow Fireflies' rate
    CHECK(engine.effectStatus(*id) == world::EffectStatus::Drawn);

    SECTION("its rate is the fx/ parameter, including a route or a key on it") {
        auto* rate = engine.params().find(world::effectParameterPrefix(*id) + "rate");
        REQUIRE(rate != nullptr);
        rate->setBaseComponent(0, 300.0f);
        step(engine, 1.1);
        CHECK(systemOf(engine, *id)->spawnRate == Catch::Approx(300.0f));
    }

    SECTION("there is exactly one authority: no particles/fx:* parameter exists") {
        for (const params::IParameter* p : engine.params().ordered()) {
            INFO(p->path());
            CHECK_FALSE(p->path().starts_with("particles/fx:"));
        }
    }

    SECTION("disabling it stops the system and says so") {
        auto* on = engine.params().find(world::effectParameterPrefix(*id) + "enabled");
        REQUIRE(on != nullptr);
        on->setBaseComponent(0, 0.0f);
        step(engine, 1.2);
        CHECK_FALSE(systemOf(engine, *id)->enabled);
        CHECK(engine.effectStatus(*id) == world::EffectStatus::Disabled);
    }

    SECTION("removing the instance removes its system, and leaves nothing behind") {
        const std::size_t before = engine.scene().particles.size();
        REQUIRE(engine.setEffects({}).has_value());
        step(engine, 1.3);
        CHECK(systemOf(engine, *id) == nullptr);
        CHECK(engine.scene().particles.size() == before - 1);
    }

    SECTION("a closed activation window stops emission but keeps the system, so particles finish") {
        std::vector<world::EffectInstance> timed = engine.capturedEffects();
        timed[0].activation = world::Activation::Window;
        timed[0].timing.windowStart = 10.0;
        timed[0].timing.windowSeconds = 5.0;
        REQUIRE(engine.setEffects(timed).has_value());
        step(engine, 2.0);
        REQUIRE(systemOf(engine, *id) != nullptr);
        CHECK(systemOf(engine, *id)->spawnRate == 0.0f);
        CHECK(systemOf(engine, *id)->enabled);
        CHECK(engine.effectStatus(*id) == world::EffectStatus::Dormant);
        step(engine, 12.0);
        CHECK(systemOf(engine, *id)->spawnRate > 0.0f);
        CHECK(engine.effectStatus(*id) == world::EffectStatus::Drawn);
    }
}

TEST_CASE("an entity-owned emitter rides its owner", "[effects][emit]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    REQUIRE(engine.addNode(orb("ufo", glm::vec3(10.0f, 4.0f, -3.0f))).has_value());
    std::vector<world::EffectInstance> list;
    const auto id = world::insertEffect(list, emitter(world::EffectOwner::entity("ufo"), "Arcane Swirl"));
    REQUIRE(id.has_value());
    REQUIRE(engine.setEffects(list).has_value());
    step(engine, 1.0);
    step(engine, 1.0 + 1.0 / 60.0); // the second frame reads the first flattening

    const scene::ParticleSystem* s = systemOf(engine, *id);
    REQUIRE(s != nullptr);
    CHECK(s->position.x == Catch::Approx(10.0f).margin(0.6f));
    CHECK(s->position.y == Catch::Approx(4.0f).margin(0.6f));
    CHECK(s->position.z == Catch::Approx(-3.0f).margin(0.6f));
    CHECK(s->attractorPosition == s->position);
    CHECK(engine.effectStatus(*id) == world::EffectStatus::Drawn);

    // Move the owner through its own parameter; the system follows on the next frames.
    auto* position = engine.params().find("nodes/ufo/position");
    REQUIRE(position != nullptr);
    position->setBaseComponent(0, -20.0f);
    step(engine, 1.1);
    step(engine, 1.1 + 1.0 / 60.0);
    CHECK(systemOf(engine, *id)->position.x == Catch::Approx(-20.0f).margin(0.6f));
}

TEST_CASE("the effect particle-system budget drops the overflow, with a reason", "[effects][emit]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    std::vector<world::EffectInstance> list;
    for (std::size_t i = 0; i < world::kMaxEffectParticleSystems + 1; ++i) {
        REQUIRE(world::insertEffect(list, emitter(world::EffectOwner::world())).has_value());
    }
    REQUIRE(engine.setEffects(list).has_value());
    step(engine, 1.0);
    std::size_t drawn = 0;
    std::size_t dropped = 0;
    for (const world::EffectInstance& e : engine.effects()) {
        drawn += engine.effectStatus(e.id) == world::EffectStatus::Drawn;
        if (engine.effectStatus(e.id) == world::EffectStatus::Dropped) {
            ++dropped;
            CHECK(engine.effectStatusReason(e.id).find("budget") != std::string_view::npos);
        }
    }
    CHECK(drawn == world::kMaxEffectParticleSystems);
    CHECK(dropped == 1);
}

TEST_CASE("an emitter's look and rows survive the file", "[effects][emit][serialization]") {
    world::EffectInstance e = emitter(world::EffectOwner::world(), "Campfire Embers");
    e.id = "embers";
    const auto back = world::EffectInstance::fromJson(e.toJson());
    REQUIRE(back.has_value());
    CHECK(back->toJson() == e.toJson());
    scene::ParticleSystem a;
    scene::ParticleSystem b;
    world::describeParticleSystem(e, 1.0f, a);
    world::describeParticleSystem(*back, 1.0f, b);
    CHECK(a.spawnRate == b.spawnRate);
    CHECK(a.gravity == b.gravity);
    CHECK(a.colorCurve.keys.size() == 4); // the embers' blackbody ramp
    CHECK(a.seed == b.seed);              // the seed is the id's, not a counter's
}
