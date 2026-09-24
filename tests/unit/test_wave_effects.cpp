// Surface waves (ADR-207, ADR-702): the data model, source and direction resolution, activation and
// timing, the GPU packing, and the parameters that make every number on a wave modulatable.
//
// ADR-702 made ADR-207's two "world effects" -- the camera travel beam and the hero pulse -- two
// ordinary effect TYPES (Travel Beam, Ground Pulse) in the one `EffectInstance` list, with their
// payload in `EffectInstance::wave` and their parameters under `fx/<id>/`. The questions below are
// the ones ADR-207 asked, asked of the new model.
//
// Everything here runs without a GPU, which is the point of resolving on the CPU: the questions
// worth asking about a propagation system -- did it activate, where did it end up pointing, how far
// has the front got, is it the same at 30 fps as at 120 -- are all answerable before a pixel exists.

#include "app/cinematic.hpp"
#include "assets/asset_registry.hpp"
#include "params/parameter_set.hpp"
#include "params/modulation.hpp"
#include "signals/signal_bus.hpp"
#include "world/effects/effect_params.hpp"
#include "scene/composition.hpp"
#include "world/atmospherics.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"
#include "world/wave_effect.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace avgen;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

// A scene with two nodes in it, so `node:` sources have something to resolve against.
class FakeScene final : public world::EffectSceneQuery {
public:
    [[nodiscard]] bool nodePosition(std::string_view name, glm::vec3& out) const override {
        if (name == "altar") {
            out = glm::vec3(10.0f, 3.0f, -4.0f);
            return true;
        }
        if (name == "beacon") {
            out = glm::vec3(-20.0f, 1.0f, 40.0f);
            return true;
        }
        return false;
    }
    [[nodiscard]] bool nodeForward(std::string_view name, glm::vec3& out) const override {
        if (name == "altar") {
            out = glm::vec3(1.0f, 0.0f, 0.0f);
            return true;
        }
        return false;
    }
};

std::vector<world::HeroPoint> twoHeroes() {
    world::HeroPoint a;
    a.name = "elder";
    a.position = glm::vec3(0.0f, 5.0f, 0.0f);
    a.radius = 6.0f;
    a.colorAccent = glm::vec3(0.2f, 0.9f, 0.4f);
    world::HeroPoint b;
    b.name = "lantern";
    b.position = glm::vec3(100.0f, 2.0f, 0.0f);
    b.radius = 3.0f;
    b.colorAccent = glm::vec3(1.0f, 0.3f, 0.1f);
    return {a, b};
}

// One held shot and one travel shot, the two things a director's cut offers an effect.
std::vector<world::ShotSpan> cut() {
    world::ShotSpan hold;
    hold.start = 0.0;
    hold.end = 10.0;
    hold.spotlight = true;
    hold.subject = "elder";
    hold.subjectPosition = glm::vec3(0.0f, 5.0f, 0.0f);
    hold.subjectRadius = 6.0f;

    world::ShotSpan travel;
    travel.start = 10.0;
    travel.end = 16.0;
    travel.travel = true;
    travel.subject = "elder";
    travel.subjectPosition = glm::vec3(0.0f, 5.0f, 0.0f);
    travel.handoff = "lantern";
    travel.handoffPosition = glm::vec3(100.0f, 2.0f, 0.0f);
    return {hold, travel};
}

world::EffectContext contextAt(double seconds, const std::vector<world::ShotSpan>& spans,
                               const std::vector<world::HeroPoint>& heroes, const FakeScene& scene) {
    world::EffectContext ctx;
    ctx.seconds = seconds;
    ctx.cameraPosition = glm::vec3(0.0f, 20.0f, 60.0f);
    ctx.cameraTarget = glm::vec3(0.0f, 5.0f, 0.0f);
    ctx.cameraForward = glm::vec3(0.0f, 0.0f, -1.0f);
    ctx.cameraVelocity = glm::vec3(0.0f, 0.0f, -8.0f);
    ctx.shots = spans;
    ctx.heroes = heroes;
    ctx.scene = &scene;
    return ctx;
}

// A bare surface wave: what a default-constructed ADR-207 `WorldEffect` was -- a World source, a
// radial wave, always active -- as a Ground Pulse instance owned by the World.
world::EffectInstance wave(const std::string& id) {
    world::EffectInstance e;
    e.kind = world::EffectKind::GroundPulse;
    e.id = id;
    e.name = id;
    return e;
}

// ADR-207's two ready-made effects, as the registry's factories now make them. On the World owner a
// Ground Pulse's `owner` source becomes `focusHero` (`adaptEffectToOwner`), which is exactly the
// scene-wide hero pulse ADR-207 shipped.
world::EffectInstance cameraTravelBeam(const std::string& name = "Travel Beam") {
    return world::makeEffect(world::EffectKind::TravelBeam, name);
}
world::EffectInstance heroGroundPulse(const std::string& name = "Ground Pulse") {
    return world::makeEffect(world::EffectKind::GroundPulse, name);
}

} // namespace

TEST_CASE("a surface wave round-trips through JSON", "[world][effects][json]") {
    world::EffectInstance e = cameraTravelBeam("Travel");
    e.wave.appearance.rainbow = true;
    e.wave.appearance.rainbowScale = 0.031f;
    e.wave.sparkle.enabled = true;
    e.wave.sparkle.seed = 77;
    e.timing.repeatSeconds = 2.75;
    e.wave.propagation.beamRadius = 42.0f;

    const nlohmann::json j = e.toJson();
    // ADR-702's canonical shape: identity and owner at the root, the type's own rows under
    // `parameters`, and nothing of any other type's.
    CHECK(j.at("type") == "travelBeam");
    CHECK(j.at("id") == e.id);
    CHECK(j.at("owner").at("kind") == "world");
    REQUIRE(j.contains("parameters"));
    CHECK(j.at("parameters").contains("propagation"));
    CHECK_FALSE(j.contains("comet"));
    CHECK_FALSE(j.contains("aurora"));

    const auto back = world::EffectInstance::fromJson(j);
    REQUIRE(back);
    CHECK(back->name == "Travel");
    CHECK(back->id == e.id);
    CHECK(back->kind == world::EffectKind::TravelBeam);
    CHECK(back->wave.source.kind == world::SourceKind::Camera);
    CHECK(back->wave.hasTarget);
    CHECK(back->wave.target.kind == world::SourceKind::FocusHero);
    CHECK(back->activation == world::Activation::CameraTravel);
    CHECK(back->wave.propagation.kind == world::PropagationKind::DirectionalWave);
    CHECK(back->wave.propagation.direction == world::DirectionMode::Blended);
    CHECK(back->wave.appearance.rainbow);
    CHECK_THAT(back->wave.appearance.rainbowScale, WithinAbs(0.031f, 1e-6f));
    CHECK(back->wave.sparkle.enabled);
    CHECK(back->wave.sparkle.seed == 77u);
    CHECK_THAT(back->timing.repeatSeconds, WithinAbs(2.75, 1e-9));
    CHECK_THAT(back->wave.propagation.beamRadius, WithinAbs(42.0f, 1e-6f));
    // A second trip changes nothing, which is what "deterministic serialisation" means in practice.
    CHECK(back->toJson() == j);
}

TEST_CASE("a surface wave read from a file keeps the defaults of everything the file omits",
          "[world][effects][json]") {
    // A file that states only what it means must still open, with everything it left out at its
    // default rather than at zero.
    const nlohmann::json minimal = {{"id", "ripple"},
                                    {"type", "groundPulse"},
                                    {"name", "Ripple"},
                                    {"activation", "heroFocus"},
                                    {"parameters", {{"source", {{"kind", "hero"}, {"name", "elder"}}}}}};
    const auto e = world::EffectInstance::fromJson(minimal);
    REQUIRE(e);
    const world::EffectInstance defaults = wave("defaults");
    CHECK(e->enabled);
    CHECK(e->owner.isWorld()); // an absent owner is the World
    CHECK(e->wave.source.kind == world::SourceKind::Hero);
    CHECK(e->wave.source.name == "elder");
    CHECK(e->wave.propagation.kind == defaults.wave.propagation.kind);
    CHECK_THAT(e->wave.propagation.speed, WithinAbs(defaults.wave.propagation.speed, 1e-6f));
    CHECK_THAT(e->wave.appearance.intensity, WithinAbs(defaults.wave.appearance.intensity, 1e-6f));
    CHECK(e->wave.sparkle.enabled == defaults.wave.sparkle.enabled);
    CHECK_THAT(e->wave.response.foliage, WithinAbs(defaults.wave.response.foliage, 1e-6f));
}

TEST_CASE("an invalid surface wave is refused rather than clamped", "[world][effects][json]") {
    const auto doc = [](nlohmann::json parameters) {
        return nlohmann::json{{"id", "x"}, {"type", "groundPulse"}, {"parameters", std::move(parameters)}};
    };
    SECTION("the control: the same document with a well-formed source loads") {
        // Without this arm every refusal below could be a document this reader refuses anyway.
        CHECK(world::EffectInstance::fromJson(doc({{"source", {{"kind", "hero"}, {"name", "elder"}}}})));
    }
    SECTION("a named source with no name") {
        CHECK_FALSE(world::EffectInstance::fromJson(doc({{"source", {{"kind", "hero"}}}})));
    }
    SECTION("a named target with no name") {
        CHECK_FALSE(world::EffectInstance::fromJson(doc({{"target", {{"kind", "node"}}}})));
    }
    SECTION("an unknown source kind") {
        const auto e = world::EffectInstance::fromJson(doc({{"source", {{"kind", "wormhole"}}}}));
        REQUIRE_FALSE(e);
        CHECK(e.error().message.find("wormhole") != std::string::npos);
    }
    SECTION("an unknown propagation kind leaves the default, the registry's rule for every choice") {
        // ADR-566/702: a choice row is read by NAME, and a name this build does not know leaves the
        // default rather than failing the file -- the rule every registry type follows, so a file
        // written by a build with one more propagation kind still opens. (ADR-207's hand-written
        // reader refused it; the registry walk replaced that reader.)
        const auto e = world::EffectInstance::fromJson(doc({{"propagation", {{"kind", "hyperbolic"}}}}));
        REQUIRE(e);
        CHECK(e->wave.propagation.kind == wave("d").wave.propagation.kind);
    }
    SECTION("a zero speed, which would be a front that never arrives") {
        CHECK_FALSE(world::EffectInstance::fromJson(doc({{"propagation", {{"speed", 0.0}}}})));
    }
    SECTION("an id with a '/', which is half a parameter path") {
        world::EffectInstance e = wave("beam/one");
        CHECK_FALSE(e.validate());
    }
    SECTION("a direction that needs a target, with no target") {
        world::EffectInstance e = wave("beam");
        e.wave.propagation.direction = world::DirectionMode::SourceToTarget;
        e.wave.hasTarget = false;
        const auto ok = e.validate();
        REQUIRE_FALSE(ok);
        CHECK(ok.error().message.find("target") != std::string::npos);
    }
    SECTION("a pre-ADR-702 entry is named, not guessed at") {
        const nlohmann::json old = {{"name", "Ripple"}, {"source", {{"kind", "focusHero"}}}};
        const auto e = world::EffectInstance::fromJson(old);
        REQUIRE_FALSE(e);
        CHECK(e.error().message.find("type") != std::string::npos);
    }
}

TEST_CASE("two surface waves may share a name but not an id", "[world][effects]") {
    // ADR-702: the id, not the display name, is half of the parameter path, so two effects called
    // "Beam" are fine and two effects with the id "beam" are two things writing one path.
    world::EffectInstance a = cameraTravelBeam("Beam");
    world::EffectInstance b = cameraTravelBeam("Beam");
    b.order = 1;
    REQUIRE(a.id == b.id);
    std::vector<world::EffectInstance> clash{a, b};
    const auto ok = world::validateEffects(clash);
    REQUIRE_FALSE(ok);
    CHECK(ok.error().message.find(a.id) != std::string::npos);

    b.id = "beam-2";
    const std::vector<world::EffectInstance> named{a, b};
    CHECK(world::validateEffects(named));
}

TEST_CASE("a source resolves to the thing it names, never to a coordinate", "[world][effects][source]") {
    const FakeScene scene;
    const auto heroes = twoHeroes();
    const auto spans = cut();
    std::array<world::ResolvedWave, world::kMaxGpuWaves> out{};

    SECTION("a node source rides the node's transform") {
        world::EffectInstance e = wave("n");
        e.wave.source.kind = world::SourceKind::Node;
        e.wave.source.name = "altar";
        e.wave.propagation.direction = world::DirectionMode::Explicit;
        REQUIRE(e.validate());
        const auto ctx = contextAt(1.0, spans, heroes, scene);
        REQUIRE(world::resolveWaves(std::array{e}, ctx, out) == 1);
        CHECK_THAT(out[0].origin.x, WithinAbs(10.0f, 1e-4f));
        CHECK_THAT(out[0].origin.z, WithinAbs(-4.0f, 1e-4f));
    }

    SECTION("a node source that is not in this scene falls back to its authored position") {
        // Rather than jumping to the origin, which is what a scene swap would otherwise do to it.
        world::EffectInstance e = wave("n");
        e.wave.source.kind = world::SourceKind::Node;
        e.wave.source.name = "gone";
        e.wave.source.position = glm::vec3(7.0f, 0.0f, 7.0f);
        e.wave.propagation.direction = world::DirectionMode::Explicit;
        const auto ctx = contextAt(1.0, spans, heroes, scene);
        REQUIRE(world::resolveWaves(std::array{e}, ctx, out) == 1);
        CHECK_THAT(out[0].origin.x, WithinAbs(7.0f, 1e-4f));
    }

    SECTION("a hero source lends its accent colour to an effect that stated none") {
        world::EffectInstance e = wave("n");
        e.wave.source.kind = world::SourceKind::Hero;
        e.wave.source.name = "elder";
        e.wave.appearance.color = glm::vec3(1.0f); // "no opinion"
        e.wave.propagation.direction = world::DirectionMode::Explicit;
        const auto ctx = contextAt(1.0, spans, heroes, scene);
        REQUIRE(world::resolveWaves(std::array{e}, ctx, out) == 1);
        CHECK_THAT(out[0].color.g, WithinAbs(0.9f, 1e-4f));
    }

    SECTION("a ground offset drops the origin to the base of the thing") {
        world::EffectInstance e = heroGroundPulse("Pulse");
        e.wave.source.kind = world::SourceKind::Hero;
        e.wave.source.name = "elder";
        e.wave.source.groundOffset = 5.0f;
        e.activation = world::Activation::Always;
        e.timing.delay = 0.0;
        e.timing.fadeIn = 0.0;
        const auto ctx = contextAt(1.0, spans, heroes, scene);
        REQUIRE(world::resolveWaves(std::array{e}, ctx, out) == 1);
        CHECK_THAT(out[0].origin.y, WithinAbs(0.0f, 1e-4f)); // hero at y=5, offset 5
    }

    SECTION("a focus-hero source follows whoever the cut is holding") {
        world::EffectInstance e = heroGroundPulse("Pulse");
        e.timing.delay = 0.0;
        e.timing.fadeIn = 0.0;
        const auto ctx = contextAt(4.0, spans, heroes, scene);
        REQUIRE(world::resolveWaves(std::array{e}, ctx, out) == 1);
        CHECK_THAT(out[0].origin.x, WithinAbs(0.0f, 1e-4f)); // the elder, not the lantern
    }
}

TEST_CASE("a direction is derived, not authored", "[world][effects][direction]") {
    const FakeScene scene;
    const auto heroes = twoHeroes();
    const auto spans = cut();
    std::array<world::ResolvedWave, world::kMaxGpuWaves> out{};
    // Inside the travel span, so a camera-travel effect is live and the handoff is resolvable.
    const auto ctx = contextAt(12.0, spans, heroes, scene);

    SECTION("camera -> target points at the shot's handoff, not at the subject it is leaving") {
        world::EffectInstance e = cameraTravelBeam("Beam");
        e.wave.propagation.direction = world::DirectionMode::CameraToTarget;
        e.timing.delay = 0.0;
        e.timing.fadeIn = 0.0;
        REQUIRE(world::resolveWaves(std::array{e}, ctx, out) == 1);
        // Camera at (0, 20, 60), lantern at (100, 2, 0): the axis must lead with +x.
        CHECK(out[0].axis.x > 0.7f);
    }

    SECTION("camera velocity uses the trajectory, not the aim") {
        world::EffectInstance e = cameraTravelBeam("Beam");
        e.wave.propagation.direction = world::DirectionMode::CameraVelocity;
        e.timing.delay = 0.0;
        e.timing.fadeIn = 0.0;
        REQUIRE(world::resolveWaves(std::array{e}, ctx, out) == 1);
        CHECK_THAT(out[0].axis.z, WithinAbs(-1.0f, 1e-4f));
    }

    SECTION("the blend leans towards the destination without ignoring where the camera looks") {
        world::EffectInstance e = cameraTravelBeam("Beam");
        e.timing.delay = 0.0;
        e.timing.fadeIn = 0.0;
        REQUIRE(e.wave.propagation.direction == world::DirectionMode::Blended);
        REQUIRE(world::resolveWaves(std::array{e}, ctx, out) == 1);
        const glm::vec3 axis = out[0].axis;
        CHECK_THAT(glm::length(axis), WithinAbs(1.0f, 1e-4f));
        CHECK(axis.x > 0.0f);  // the destination pulls it sideways
        CHECK(axis.z < 0.0f);  // the aim and the velocity hold it forward
    }

    SECTION("a source's own forward axis, when it has one") {
        world::EffectInstance e = wave("n");
        e.wave.source.kind = world::SourceKind::Node;
        e.wave.source.name = "altar";
        e.wave.propagation.kind = world::PropagationKind::DirectionalWave;
        e.wave.propagation.direction = world::DirectionMode::SourceForward;
        e.timing.fadeIn = 0.0;
        e.timing.repeatSeconds = 1.0; // Always makes one pass; a standing effect repeats
        REQUIRE(world::resolveWaves(std::array{e}, ctx, out) == 1);
        CHECK_THAT(out[0].axis.x, WithinAbs(1.0f, 1e-4f));
    }

    SECTION("a mode with nothing to read falls back rather than producing a zero axis") {
        world::EffectInstance e = wave("n");
        e.wave.source.kind = world::SourceKind::World;
        e.wave.propagation.kind = world::PropagationKind::DirectionalWave;
        e.wave.propagation.direction = world::DirectionMode::SourceForward; // a world source has no forward
        e.timing.fadeIn = 0.0;
        e.timing.repeatSeconds = 1.0;
        REQUIRE(world::resolveWaves(std::array{e}, ctx, out) == 1);
        CHECK_THAT(glm::length(out[0].axis), WithinAbs(1.0f, 1e-4f));
    }
}

TEST_CASE("activation is gated on the cut, not on a timer", "[world][effects][activation]") {
    const FakeScene scene;
    const auto heroes = twoHeroes();
    const auto spans = cut();
    std::array<world::ResolvedWave, world::kMaxGpuWaves> out{};

    world::EffectInstance beam = cameraTravelBeam("Beam");
    beam.timing.delay = 0.0;
    beam.timing.fadeIn = 0.0;
    beam.timing.fadeOut = 0.0;
    world::EffectInstance pulse = heroGroundPulse("Pulse");
    pulse.timing.delay = 0.0;
    pulse.timing.fadeIn = 0.0;
    pulse.timing.fadeOut = 0.0;
    const std::array effects{beam, pulse};

    SECTION("while the camera holds a hero, the pulse runs and the beam does not") {
        const auto ctx = contextAt(4.0, spans, heroes, scene);
        REQUIRE(world::resolveWaves(effects, ctx, out) == 1);
        CHECK(out[0].effect->name == "Pulse");
    }
    SECTION("while the camera travels, the beam runs and the pulse does not") {
        const auto ctx = contextAt(12.0, spans, heroes, scene);
        REQUIRE(world::resolveWaves(effects, ctx, out) == 1);
        CHECK(out[0].effect->name == "Beam");
    }
    SECTION("past the end of the cut, neither exists") {
        const auto ctx = contextAt(40.0, spans, heroes, scene);
        CHECK(world::resolveWaves(effects, ctx, out) == 0);
    }
    SECTION("with no cut at all, neither exists -- which is the honest answer for a free camera") {
        const std::vector<world::ShotSpan> none;
        const auto ctx = contextAt(4.0, none, heroes, scene);
        CHECK(world::resolveWaves(effects, ctx, out) == 0);
    }
    SECTION("a disabled effect is not resolved however open its window is") {
        world::EffectInstance off = pulse;
        off.enabled = false;
        const auto ctx = contextAt(4.0, spans, heroes, scene);
        CHECK(world::resolveWaves(std::array{off}, ctx, out) == 0);
    }
    SECTION("an authored window activates on the transport clock alone") {
        world::EffectInstance w = wave("w");
        w.activation = world::Activation::Window;
        w.timing.windowStart = 30.0;
        w.timing.windowSeconds = 4.0;
        w.timing.fadeIn = 0.0;
        w.timing.fadeOut = 0.0;
        w.wave.propagation.direction = world::DirectionMode::Explicit;
        CHECK(world::resolveWaves(std::array{w}, contextAt(29.9, spans, heroes, scene), out) == 0);
        CHECK(world::resolveWaves(std::array{w}, contextAt(31.0, spans, heroes, scene), out) == 1);
        CHECK(world::resolveWaves(std::array{w}, contextAt(34.1, spans, heroes, scene), out) == 0);
    }
}

TEST_CASE("an entity-owned pulse rides its owner and fires only for its own hero",
          "[world][effects][activation][owner]") {
    // ADR-702: ADR-207's one pulse that followed focus became one Ground Pulse per hero, owned by
    // that hero with an `owner` source. Together they must behave like the one pulse did -- the
    // held hero's ring, at that hero -- and apart, each is its own.
    const FakeScene scene;
    const auto heroes = twoHeroes();
    const auto spans = cut(); // holds the elder for [0, 10), then travels to the lantern
    std::array<world::ResolvedWave, world::kMaxGpuWaves> out{};

    std::vector<world::EffectInstance> effects;
    REQUIRE(world::addEffect(effects, world::EffectOwner::entity("elder"), world::EffectKind::GroundPulse));
    REQUIRE(world::addEffect(effects, world::EffectOwner::entity("lantern"), world::EffectKind::GroundPulse));
    REQUIRE(effects.size() == 2);
    for (world::EffectInstance& e : effects) {
        REQUIRE(e.wave.source.kind == world::SourceKind::Owner);
        REQUIRE(e.activation == world::Activation::HeroFocus);
        e.timing.delay = 0.0;
        e.timing.fadeIn = 0.0;
    }

    const auto ctx = contextAt(4.0, spans, heroes, scene);
    REQUIRE(world::resolveWaves(effects, ctx, out) == 1);
    CHECK(out[0].effect->owner.name == "elder");
    CHECK_THAT(out[0].origin.x, WithinAbs(0.0f, 1e-4f)); // the elder stands at x = 0, the lantern at 100

    SECTION("the lantern's pulse stands at the lantern when the cut holds it") {
        std::vector<world::ShotSpan> lanternHold = spans;
        lanternHold[0].subject = "lantern";
        lanternHold[0].subjectPosition = heroes[1].position;
        REQUIRE(world::resolveWaves(effects, contextAt(4.0, lanternHold, heroes, scene), out) == 1);
        CHECK(out[0].effect->owner.name == "lantern");
        CHECK_THAT(out[0].origin.x, WithinAbs(100.0f, 1e-4f));
    }
    SECTION("disabling one hero's pulse leaves the other's alone") {
        effects[0].enabled = false;
        CHECK(world::resolveWaves(effects, ctx, out) == 0); // the elder is held, and its pulse is off
        std::vector<world::ShotSpan> lanternHold = spans;
        lanternHold[0].subject = "lantern";
        CHECK(world::resolveWaves(effects, contextAt(4.0, lanternHold, heroes, scene), out) == 1);
    }
    SECTION("a pulse whose owner is not a hero or node of this scene draws nothing") {
        std::vector<world::EffectInstance> orphan;
        REQUIRE(world::addEffect(orphan, world::EffectOwner::entity("nobody"), world::EffectKind::GroundPulse));
        orphan[0].activation = world::Activation::Always;
        orphan[0].timing.fadeIn = 0.0;
        orphan[0].timing.delay = 0.0;
        CHECK(world::resolveWaves(orphan, ctx, out) == 0);
    }
}

TEST_CASE("timing shapes the effect inside its activation", "[world][effects][timing]") {
    const FakeScene scene;
    const auto heroes = twoHeroes();
    const auto spans = cut();
    std::array<world::ResolvedWave, world::kMaxGpuWaves> out{};

    world::EffectInstance pulse = heroGroundPulse("Pulse");
    pulse.timing.delay = 1.0;
    pulse.timing.fadeIn = 2.0;
    pulse.timing.fadeOut = 2.0;
    pulse.timing.repeatSeconds = 0.0;
    pulse.timing.lifetime = 0.0; // as long as the hold

    SECTION("the delay holds it off") {
        CHECK(world::resolveWaves(std::array{pulse}, contextAt(0.5, spans, heroes, scene), out) == 0);
    }
    SECTION("the fade in ramps rather than switching") {
        REQUIRE(world::resolveWaves(std::array{pulse}, contextAt(2.0, spans, heroes, scene), out) == 1);
        const float early = out[0].envelope;
        REQUIRE(world::resolveWaves(std::array{pulse}, contextAt(3.0, spans, heroes, scene), out) == 1);
        CHECK(out[0].envelope > early);
        CHECK(early > 0.0f);
        CHECK(early < 1.0f);
    }
    SECTION("the fade out is symmetric at the end of the hold") {
        // Slowed so the front is still inside its range when the hold ends: this section is about
        // the envelope, and an effect that has simply outrun its range would answer a different
        // question with the same number (ADR-182).
        world::EffectInstance slow = pulse;
        slow.wave.propagation.speed = 1.0f;
        REQUIRE(world::resolveWaves(std::array{slow}, contextAt(9.5, spans, heroes, scene), out) == 1);
        CHECK(out[0].envelope < 0.5f);
    }
    SECTION("the front advances at the stated speed") {
        REQUIRE(world::resolveWaves(std::array{pulse}, contextAt(3.0, spans, heroes, scene), out) == 1);
        const float a = out[0].frontDistance;
        REQUIRE(world::resolveWaves(std::array{pulse}, contextAt(4.0, spans, heroes, scene), out) == 1);
        CHECK_THAT(out[0].frontDistance - a, WithinRel(pulse.wave.propagation.speed, 1e-4f));
    }
    SECTION("a repeat restarts the front, so a scrub lands the same ring") {
        world::EffectInstance repeating = pulse;
        repeating.timing.repeatSeconds = 2.0;
        REQUIRE(world::resolveWaves(std::array{repeating}, contextAt(2.5, spans, heroes, scene), out) == 1);
        const float first = out[0].frontDistance;
        // 2 s later is the same point in the next ring: the same front distance, to the bit.
        REQUIRE(world::resolveWaves(std::array{repeating}, contextAt(4.5, spans, heroes, scene), out) == 1);
        CHECK_THAT(out[0].frontDistance, WithinAbs(first, 1e-4f));
    }
}

TEST_CASE("resolution is a pure function of the transport second", "[world][effects][determinism]") {
    // The claim ADR-091 makes about everything the engine bakes, tested where it is easiest to
    // break: an effect whose front position came from a frame delta would fail this, because the
    // two calls below differ in nothing except that one of them has been called before.
    const FakeScene scene;
    const auto heroes = twoHeroes();
    const auto spans = cut();
    const std::array effects{cameraTravelBeam("Beam"), heroGroundPulse("Pulse")};

    world::WaveFrame realtime{};
    world::WaveFrame offline{};
    // A "realtime playthrough": every 1/120 s up to the second in question.
    for (int i = 0; i <= 120 * 12; ++i) {
        world::buildWaveFrame(effects, contextAt(i / 120.0, spans, heroes, scene), realtime);
    }
    // An "offline render" that jumps straight there at 30 fps.
    for (int i = 0; i <= 30 * 12; ++i) {
        world::buildWaveFrame(effects, contextAt(i / 30.0, spans, heroes, scene), offline);
    }
    REQUIRE(realtime.count == offline.count);
    CHECK(std::memcmp(realtime.effects, offline.effects, sizeof(realtime.effects)) == 0);
}

TEST_CASE("packing puts every authored number in the lane the shader reads", "[world][effects][gpu]") {
    const FakeScene scene;
    const auto heroes = twoHeroes();
    const auto spans = cut();
    std::array<world::ResolvedWave, world::kMaxGpuWaves> out{};

    world::EffectInstance e = heroGroundPulse("Pulse");
    e.timing.delay = 0.0;
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    e.timing.repeatSeconds = 0.0;
    e.wave.propagation.ringCount = 3.0f;
    e.wave.propagation.range = 55.0f;
    e.wave.appearance.intensity = 2.0f;
    e.wave.appearance.color = glm::vec3(0.25f, 0.5f, 1.0f);
    e.wave.appearance.width = 2.0f;
    e.wave.sparkle.enabled = true;
    e.wave.sparkle.fadeDistance = 64.0f;
    REQUIRE(world::resolveWaves(std::array{e}, contextAt(2.0, spans, heroes, scene), out) == 1);
    const world::WaveGpu g = world::packWave(out[0]);

    CHECK_THAT(g.originKind.w, WithinAbs(1.0f, 1e-6f)); // RadialWave
    CHECK_THAT(g.axisFront.w, WithinRel(e.wave.propagation.speed * 2.0f, 1e-4f));
    // `width` multiplies the widths and nothing else, which is what makes it one knob.
    CHECK_THAT(g.shape.x, WithinRel(e.wave.propagation.frontWidth * 2.0f, 1e-4f));
    CHECK_THAT(g.shape.y, WithinRel(e.wave.propagation.trailLength * 2.0f, 1e-4f));
    CHECK_THAT(g.shape.z, WithinAbs(55.0f, 1e-4f));
    CHECK_THAT(g.vertical.z, WithinAbs(3.0f, 1e-6f));
    // Intensity and the envelope are folded into the radiance, so the shader multiplies once.
    CHECK_THAT(g.color.r, WithinRel(0.5f, 1e-4f));
    CHECK_THAT(g.color.w, WithinAbs(64.0f, 1e-4f)); // the sparkle fade distance
    CHECK_THAT(g.sparkle.x, WithinRel(e.wave.sparkle.density, 1e-4f));
    CHECK_THAT(g.response.y, WithinRel(e.wave.response.foliage, 1e-4f));

    SECTION("sparkle off is a zero density, so the shader's guard is the off switch") {
        world::EffectInstance quiet = e;
        quiet.wave.sparkle.enabled = false;
        REQUIRE(world::resolveWaves(std::array{quiet}, contextAt(2.0, spans, heroes, scene), out) == 1);
        CHECK_THAT(world::packWave(out[0]).sparkle.x, WithinAbs(0.0f, 1e-9f));
    }
    SECTION("rainbow is a flag the shader branches on, not a colour list") {
        world::EffectInstance bow = e;
        bow.wave.appearance.rainbow = true;
        REQUIRE(world::resolveWaves(std::array{bow}, contextAt(2.0, spans, heroes, scene), out) == 1);
        CHECK_THAT(world::packWave(out[0]).edge.w, WithinAbs(1.0f, 1e-9f));
    }
}

TEST_CASE("no more than the GPU limit of effects is ever written", "[world][effects][gpu]") {
    const FakeScene scene;
    const auto heroes = twoHeroes();
    const std::vector<world::ShotSpan> none;
    std::vector<world::EffectInstance> many;
    for (int i = 0; i < 12; ++i) {
        world::EffectInstance e = wave("e" + std::to_string(i));
        e.wave.propagation.direction = world::DirectionMode::Explicit;
        e.timing.fadeIn = 0.0;
        many.push_back(e);
    }
    world::WaveFrame frame{};
    world::buildWaveFrame(many, contextAt(1.0, none, heroes, scene), frame);
    CHECK(frame.count == world::kMaxGpuWaves);
}

TEST_CASE("a style configures parameters and nothing else", "[world][effects][presets]") {
    world::EffectInstance e = cameraTravelBeam("Beam");
    const world::EffectEndpoint sourceBefore = e.wave.source;
    const world::Activation activationBefore = e.activation;
    REQUIRE(world::applyEffectStyle(e, e.kind, "Rainbow"));
    CHECK(e.wave.appearance.rainbow);
    CHECK(e.style == "Rainbow");
    // A style is appearance, not wiring: what the effect is *about* is untouched.
    CHECK(e.wave.source.kind == sourceBefore.kind);
    CHECK(e.activation == activationBefore);
    CHECK_FALSE(world::applyEffectStyle(e, e.kind, "Not A Style"));
    // Nor is a pulse's style a beam's: the two answer different questions (ADR-207 §18).
    CHECK_FALSE(world::applyEffectStyle(e, e.kind, "Water"));
    CHECK(e.style == "Rainbow");

    world::EffectInstance p = heroGroundPulse("Pulse");
    REQUIRE(world::applyEffectStyle(p, p.kind, "Shockwave"));
    CHECK(p.style == "Shockwave");
    CHECK_FALSE(p.wave.sparkle.enabled);
    CHECK(p.wave.propagation.falloff > 2.0f);
    CHECK_FALSE(world::beamStyleNames().empty());
    CHECK_FALSE(world::pulseStyleNames().empty());
}

TEST_CASE("every meaningful surface wave parameter is declared and modulatable",
          "[world][effects][params]") {
    params::ParameterSet set;
    const std::vector<world::EffectInstance> effects{cameraTravelBeam("Beam"),
                                                  heroGroundPulse("Pulse")};
    // Addressed by id, which the factory slugs from the name: "Beam" -> `fx/beam/`.
    REQUIRE(effects[0].id == "beam");
    REQUIRE(effects[1].id == "pulse");
    world::EffectParameters registered = world::registerEffectParameters(set, effects);
    REQUIRE(registered.effects.size() == 2);

    CHECK(set.find("fx/beam/intensity") != nullptr);
    CHECK(set.find("fx/beam/sparkleIntensity") != nullptr);
    CHECK(set.find("fx/beam/rainbowSpeed") != nullptr);
    CHECK(set.find("fx/pulse/speed") != nullptr);
    CHECK(set.find("fx/pulse/response/foliage") != nullptr);
    CHECK(set.find("fx/pulse/repeat") != nullptr);
    CHECK(world::effectParameterPrefix("pulse") == "fx/pulse/");
    // ADR-702: a surface wave does not light the ground, so it registers none of the shared
    // ground-glow rows that nothing would read (`sharedFieldApplies`).
    CHECK(set.find("fx/pulse/groundIntensity") == nullptr);

    // §16: if a parameter can be authored, it must be possible to modulate it. A route only binds to
    // a parameter whose flags allow it, so this is the check that the promise is real.
    for (const std::string& path : registered.registered) {
        const params::IParameter* p = set.find(path);
        REQUIRE(p != nullptr);
        INFO(path);
        CHECK(p->flags().modulatable);
        CHECK(p->flags().serialized);
        CHECK(p->group() == "fx");
    }

    SECTION("defaults come from the authored effect, not from a table") {
        CHECK_THAT(set.find("fx/pulse/speed")->baseComponent(0),
                   WithinRel(effects[1].wave.propagation.speed, 1e-5f));
    }

    SECTION("the finals reach the live effect") {
        std::vector<world::EffectInstance> live = effects;
        set.find("fx/beam/intensity")->setFinalComponent(0, 7.5f);
        set.find("fx/pulse/enabled")->setFinalComponent(0, 0.0f);
        world::applyEffectParameters(registered, live);
        CHECK_THAT(live[0].wave.appearance.intensity, WithinAbs(7.5f, 1e-5f));
        CHECK_FALSE(live[1].enabled);
    }

    SECTION("a modulation route reaches an effect parameter through the ordinary path") {
        signals::SignalBus bus;
        const signals::SignalId beat = bus.declare("beat.pulse", 0.0f, 1.0f, true);
        params::Modulator modulator;
        params::ModRoute route;
        route.source = "beat.pulse";
        route.target = "fx/pulse/intensity";
        route.amount = 3.0f;
        modulator.addRoute(route);
        const auto bound = modulator.bind(bus, set);
        REQUIRE(bound);
        set.resetFinals();
        bus.setEvent(beat, true, 1.0f);
        modulator.applyRoutes(bus, set, 1.0 / 60.0);
        std::vector<world::EffectInstance> live = effects;
        world::applyEffectParameters(registered, live);
        CHECK(live[1].wave.appearance.intensity > effects[1].wave.appearance.intensity);
    }

    SECTION("unregistering removes every path it wrote") {
        world::unregisterEffectParameters(set, registered);
        CHECK(set.find("fx/beam/intensity") == nullptr);
        CHECK(set.find("fx/pulse/response/emissive") == nullptr);
        CHECK(registered.effects.empty());
        // And applying a cleared registrar leaves the effects exactly as authored.
        std::vector<world::EffectInstance> live = effects;
        world::applyEffectParameters(registered, live);
        CHECK_THAT(live[0].wave.appearance.intensity, WithinRel(effects[0].wave.appearance.intensity, 1e-6f));
    }
}

TEST_CASE("a directed sequence flattens to spans a world effect can gate on",
          "[world][effects][cinematic]") {
    app::Sequence seq;
    app::Shot hold;
    hold.name = "hold";
    hold.kind = app::ShotKind::Approach;
    hold.startSeconds = 0.0;
    hold.durationSeconds = 8.0;
    hold.subject.name = "elder";
    hold.subject.position = glm::vec3(1.0f, 2.0f, 3.0f);
    hold.subject.radius = 6.0f;
    hold.spotlight.active = true;
    hold.spotlight.emphasis = 0.8f;

    app::Shot move;
    move.name = "move";
    move.kind = app::ShotKind::Transition;
    move.startSeconds = 8.0;
    move.durationSeconds = 5.0;
    move.subject = hold.subject;
    app::FocalTarget next;
    next.name = "lantern";
    next.position = glm::vec3(40.0f, 1.0f, 0.0f);
    next.radius = 3.0f;
    move.handoff = next;
    // The director gives a transition an emphasis too; the span must still not call it a hold.
    move.spotlight.active = true;
    move.spotlight.emphasis = 0.6f;
    // And a shot the director left at zero emphasis -- every intro shot is one -- is still a shot
    // the camera has landed on, which is what a hero-focus effect is gated on.
    app::Shot quiet;
    quiet.name = "quiet";
    quiet.kind = app::ShotKind::Establish;
    quiet.startSeconds = 13.0;
    quiet.durationSeconds = 4.0;
    quiet.subject.name = "cairn";
    quiet.subject.radius = 4.0f;

    seq.shots = {hold, move, quiet};
    const std::vector<world::ShotSpan> spans = seq.shotSpans();
    REQUIRE(spans.size() == 3);
    CHECK(spans[2].spotlight);
    CHECK_THAT(spans[2].emphasis, WithinAbs(0.0f, 1e-6f));
    CHECK_THAT(spans[0].emphasis, WithinAbs(0.8f, 1e-6f));
    CHECK(spans[0].spotlight);
    CHECK_FALSE(spans[0].travel);
    CHECK(spans[0].subject == "elder");
    CHECK(spans[1].travel);
    CHECK_FALSE(spans[1].spotlight);
    CHECK(spans[1].handoff == "lantern");
    CHECK_THAT(spans[1].handoffPosition.x, WithinAbs(40.0f, 1e-5f));
}

TEST_CASE("a hero source resolves to the object the hero is, not to the middle of its bounds",
          "[world][effects][source]") {
    // ADR-107: a hero is one object and its name is that object's node; ADR-199 is what assuming the
    // node's origin and the hero's `position` are the same thing costs. Glowmere's elder is the case
    // this exists for -- its `position` describes the cap, twelve metres above the ground its stem
    // stands on -- and a ripple through the ground wants the ground.
    class SceneWithHeroNode final : public world::EffectSceneQuery {
    public:
        [[nodiscard]] bool nodePosition(std::string_view name, glm::vec3& out) const override {
            if (name == "elder") {
                out = glm::vec3(0.0f, 1.0f, 0.0f); // where it stands
                return true;
            }
            return false;
        }
    };
    const SceneWithHeroNode scene;
    const auto heroes = twoHeroes(); // "elder" is at y = 5: the middle of its cap
    const std::vector<world::ShotSpan> none;
    std::array<world::ResolvedWave, world::kMaxGpuWaves> out{};

    world::EffectInstance e = wave("ripple");
    e.wave.source.kind = world::SourceKind::Hero;
    e.wave.source.name = "elder";
    e.timing.fadeIn = 0.0;
    world::EffectContext ctx;
    ctx.seconds = 0.5;
    ctx.heroes = heroes;
    ctx.scene = &scene;
    REQUIRE(world::resolveWaves(std::array{e}, ctx, out) == 1);
    CHECK_THAT(out[0].origin.y, WithinAbs(1.0f, 1e-4f));

    SECTION("and falls back to the hero's own position when the scene has no such node") {
        world::EffectContext bare = ctx;
        bare.scene = nullptr;
        REQUIRE(world::resolveWaves(std::array{e}, bare, out) == 1);
        CHECK_THAT(out[0].origin.y, WithinAbs(5.0f, 1e-4f));
    }
}

TEST_CASE("surface waves round-trip through a scene file", "[world][effects][json][composition]") {
    // The `"effects"` block is a sibling of `"heroes"`, and the properties that matter are the ones a
    // hand-edited file depends on: a scene that never declared one is unchanged, a scene that did
    // gets it back, and a malformed one is refused with the index in the message.
    const std::string base = R"({"format": "avgen-scene", "version": 1, "name": "fx", "nodes": []})";
    assets::AssetRegistry registry;

    SECTION("a scene with no effects has none, and writes none back") {
        auto comp = scene::Composition::fromJson(nlohmann::json::parse(base), registry);
        REQUIRE(comp);
        CHECK((*comp)->effects().empty());
        CHECK_FALSE((*comp)->toJson().contains("effects"));
    }

    SECTION("a declared effect survives the trip") {
        nlohmann::json doc = nlohmann::json::parse(base);
        world::EffectInstance beam = cameraTravelBeam("Beam");
        beam.order = 1; // below the pulse in the World's stack
        doc["effects"] = nlohmann::json::array({heroGroundPulse("Ripple").toJson(), beam.toJson()});
        auto comp = scene::Composition::fromJson(doc, registry);
        REQUIRE(comp);
        REQUIRE((*comp)->effects().size() == 2);
        CHECK((*comp)->effects()[0].name == "Ripple");
        CHECK((*comp)->effects()[1].activation == world::Activation::CameraTravel);
        const nlohmann::json written = (*comp)->toJson();
        REQUIRE(written.contains("effects"));
        CHECK(written["effects"] == doc["effects"]);
    }

    SECTION("a malformed effect is refused, and the message says which one") {
        nlohmann::json doc = nlohmann::json::parse(base);
        doc["effects"] = nlohmann::json::array(
            {heroGroundPulse("Ripple").toJson(),
             nlohmann::json{{"id", "bad"}, {"type", "groundPulse"}, {"order", 1},
                            {"parameters", {{"propagation", {{"speed", -1.0}}}}}}});
        auto comp = scene::Composition::fromJson(doc, registry);
        REQUIRE_FALSE(comp);
        CHECK(comp.error().message.find("effects[1]") != std::string::npos);
    }

    SECTION("two effects of one id are refused, because an id is half a parameter path") {
        nlohmann::json doc = nlohmann::json::parse(base);
        world::EffectInstance beam = cameraTravelBeam("Same");
        beam.order = 1;
        doc["effects"] = nlohmann::json::array({heroGroundPulse("Same").toJson(), beam.toJson()});
        REQUIRE(doc["effects"][0]["id"] == doc["effects"][1]["id"]);
        CHECK_FALSE(scene::Composition::fromJson(doc, registry));
    }

    SECTION("the pre-ADR-702 key is named, not silently ignored") {
        nlohmann::json doc = nlohmann::json::parse(base);
        doc["worldEffects"] = nlohmann::json::array({nlohmann::json{{"name", "Old"}}});
        auto comp = scene::Composition::fromJson(doc, registry);
        REQUIRE_FALSE(comp);
        CHECK(comp.error().message.find("worldEffects") != std::string::npos);
    }
}

TEST_CASE("the shipped Glowmere scene declares the two effects the brief is about",
          "[world][effects][glowmere2]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR is not defined");
#else
    const std::filesystem::path path =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2.scene.json";
    if (!std::filesystem::is_regular_file(path)) {
        SKIP("the Glowmere Valley 2 example is not present in this checkout");
    }
    std::ifstream in(path);
    REQUIRE(in);
    const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    REQUIRE_FALSE(doc.is_discarded());
    REQUIRE(doc.contains("effects"));
    CHECK_FALSE(doc.contains("worldEffects"));
    std::vector<world::EffectInstance> effects;
    for (const nlohmann::json& entry : doc.at("effects")) {
        auto e = world::EffectInstance::fromJson(entry);
        REQUIRE(e);
        effects.push_back(std::move(*e));
    }
    REQUIRE(world::validateEffects(effects));

    // ADR-207's travel beam: ONE, on the World.
    std::vector<const world::EffectInstance*> beams;
    std::vector<const world::EffectInstance*> pulses;
    for (const world::EffectInstance& e : effects) {
        if (e.kind == world::EffectKind::TravelBeam) beams.push_back(&e);
        if (e.kind == world::EffectKind::GroundPulse) pulses.push_back(&e);
    }
    REQUIRE(beams.size() == 1);
    const world::EffectInstance& beam = *beams.front();
    CHECK(beam.owner.isWorld());
    CHECK(beam.wave.propagation.kind == world::PropagationKind::DirectionalWave);
    // The camera beam fires while the camera travels, and it is aimed at where it is going rather
    // than along a world axis somebody typed.
    CHECK(beam.activation == world::Activation::CameraTravel);
    CHECK(beam.wave.source.kind == world::SourceKind::Camera);
    CHECK(beam.wave.propagation.direction == world::DirectionMode::Blended);
    CHECK(beam.wave.hasTarget);
    CHECK(beam.wave.target.kind == world::SourceKind::FocusHero);

    // ADR-207's one hero pulse that followed focus is now one Ground Pulse PER HERO (ADR-702),
    // each owned by its hero and riding it. Every hero the scene declares has exactly one.
    REQUIRE(doc.contains("heroes"));
    std::vector<std::string> heroes;
    for (const nlohmann::json& h : doc.at("heroes")) {
        heroes.push_back(h.at("name").get<std::string>());
    }
    REQUIRE_FALSE(heroes.empty());
    CHECK(pulses.size() == heroes.size());
    for (const std::string& hero : heroes) {
        INFO("hero: " << hero);
        const auto mine = world::effectsOf(effects, world::EffectOwner::entity(hero));
        REQUIRE(mine.size() == 1);
        const world::EffectInstance& pulse = effects[mine.front()];
        CHECK(pulse.kind == world::EffectKind::GroundPulse);
        CHECK(pulse.wave.propagation.kind == world::PropagationKind::RadialWave);
        CHECK(pulse.activation == world::Activation::HeroFocus);
        // No Glowmere coordinates anywhere: the pulse rides the hero it is attached to.
        CHECK(pulse.wave.source.kind == world::SourceKind::Owner);
        CHECK(pulse.wave.source.name.empty());
        // It repeats, or it is one ring at the start of a ten-second hold rather than a pulse.
        CHECK(pulse.timing.repeatSeconds > 0.0);
    }
#endif
}

TEST_CASE("a pulse's beat response is an ordinary modulation route", "[world][effects][modulation]") {
    // §13 and §16: no special-case audio hook. What the Beat response control drives is the schema's
    // `beatLeaf`, and the type's default route is a route the Modulation panel can see, curve,
    // re-point and delete -- aimed at the instance's id, not its display name.
    const world::EffectSchema* schema = world::effectSchema(world::EffectKind::GroundPulse);
    REQUIRE(schema != nullptr);
    CHECK(std::string_view(schema->beatLeaf) == "intensity");
    const std::vector<params::ModRoute> routes =
        world::defaultEffectRoutes("hero-pulse", world::EffectKind::GroundPulse);
    REQUIRE(routes.size() == 1);
    CHECK(routes[0].source == "beat.pulse");
    CHECK(routes[0].target == "fx/hero-pulse/intensity");
    CHECK(routes[0].amount > 0.0f);
}
