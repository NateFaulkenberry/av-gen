// World effects (ADR-207): the data model, source and direction resolution, activation and timing,
// the GPU packing, and the parameters that make every number on an effect modulatable.
//
// Everything here runs without a GPU, which is the point of resolving on the CPU: the questions
// worth asking about a propagation system -- did it activate, where did it end up pointing, how far
// has the front got, is it the same at 30 fps as at 120 -- are all answerable before a pixel exists.

#include "app/cinematic.hpp"
#include "assets/asset_registry.hpp"
#include "params/parameter_set.hpp"
#include "params/modulation.hpp"
#include "signals/signal_bus.hpp"
#include "world/effect_params.hpp"
#include "scene/composition.hpp"
#include "ui/world_effects_panel.hpp"
#include "world/effects.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <fstream>

using namespace avgen;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

// A scene with two nodes in it, so `node:` sources have something to resolve against.
class FakeScene final : public world::WorldEffectScene {
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

world::WorldEffectContext contextAt(double seconds, const std::vector<world::ShotSpan>& spans,
                                    const std::vector<world::HeroPoint>& heroes, const FakeScene& scene) {
    world::WorldEffectContext ctx;
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

} // namespace

TEST_CASE("a world effect round-trips through JSON", "[world][effects][json]") {
    world::WorldEffect e = world::cameraTravelBeam("Travel");
    e.appearance.rainbow = true;
    e.appearance.rainbowScale = 0.031f;
    e.sparkle.enabled = true;
    e.sparkle.seed = 77;
    e.timing.repeatSeconds = 2.75;
    e.propagation.beamRadius = 42.0f;

    const nlohmann::json j = e.toJson();
    const auto back = world::WorldEffect::fromJson(j);
    REQUIRE(back);
    CHECK(back->name == "Travel");
    CHECK(back->source.kind == world::SourceKind::Camera);
    CHECK(back->hasTarget);
    CHECK(back->target.kind == world::SourceKind::FocusHero);
    CHECK(back->activation == world::Activation::CameraTravel);
    CHECK(back->propagation.kind == world::PropagationKind::DirectionalWave);
    CHECK(back->propagation.direction == world::DirectionMode::Blended);
    CHECK(back->appearance.rainbow);
    CHECK_THAT(back->appearance.rainbowScale, WithinAbs(0.031f, 1e-6f));
    CHECK(back->sparkle.enabled);
    CHECK(back->sparkle.seed == 77u);
    CHECK_THAT(back->timing.repeatSeconds, WithinAbs(2.75, 1e-9));
    CHECK_THAT(back->propagation.beamRadius, WithinAbs(42.0f, 1e-6f));
    // A second trip changes nothing, which is what "deterministic serialisation" means in practice.
    CHECK(back->toJson() == j);
}

TEST_CASE("a world effect read from a file keeps the defaults of everything the file omits",
          "[world][effects][json]") {
    // Backwards compatibility in the only form that matters here: a file written before a field
    // existed must still open, with that field at its default rather than at zero.
    const nlohmann::json minimal = {{"name", "Ripple"},
                                    {"source", {{"kind", "hero"}, {"name", "elder"}}},
                                    {"activation", "heroFocus"}};
    const auto e = world::WorldEffect::fromJson(minimal);
    REQUIRE(e);
    const world::WorldEffect defaults;
    CHECK(e->enabled);
    CHECK(e->propagation.kind == defaults.propagation.kind);
    CHECK_THAT(e->propagation.speed, WithinAbs(defaults.propagation.speed, 1e-6f));
    CHECK_THAT(e->appearance.intensity, WithinAbs(defaults.appearance.intensity, 1e-6f));
    CHECK(e->sparkle.enabled == defaults.sparkle.enabled);
    CHECK_THAT(e->response.foliage, WithinAbs(defaults.response.foliage, 1e-6f));
}

TEST_CASE("an invalid world effect is refused rather than clamped", "[world][effects][json]") {
    SECTION("a named source with no name") {
        const nlohmann::json j = {{"name", "x"}, {"source", {{"kind", "hero"}}}};
        CHECK_FALSE(world::WorldEffect::fromJson(j));
    }
    SECTION("an unknown propagation kind") {
        const nlohmann::json j = {{"name", "x"}, {"propagation", {{"kind", "hyperbolic"}}}};
        const auto e = world::WorldEffect::fromJson(j);
        REQUIRE_FALSE(e);
        CHECK(e.error().message.find("hyperbolic") != std::string::npos);
    }
    SECTION("a zero speed, which would be a front that never arrives") {
        const nlohmann::json j = {{"name", "x"}, {"propagation", {{"speed", 0.0}}}};
        CHECK_FALSE(world::WorldEffect::fromJson(j));
    }
    SECTION("a name with a '/', which is half a parameter path") {
        world::WorldEffect e;
        e.name = "beam/one";
        CHECK_FALSE(e.validate());
    }
    SECTION("a direction that needs a target, with no target") {
        world::WorldEffect e;
        e.name = "beam";
        e.propagation.direction = world::DirectionMode::SourceToTarget;
        e.hasTarget = false;
        const auto ok = e.validate();
        REQUIRE_FALSE(ok);
        CHECK(ok.error().message.find("target") != std::string::npos);
    }
}

TEST_CASE("two world effects may not share a name", "[world][effects]") {
    std::vector<world::WorldEffect> effects{world::cameraTravelBeam("Beam"), world::cameraTravelBeam("Beam")};
    const auto ok = world::validateWorldEffects(effects);
    REQUIRE_FALSE(ok);
    CHECK(ok.error().message.find("Beam") != std::string::npos);
}

TEST_CASE("a source resolves to the thing it names, never to a coordinate", "[world][effects][source]") {
    const FakeScene scene;
    const auto heroes = twoHeroes();
    const auto spans = cut();
    std::array<world::ResolvedEffect, world::kMaxGpuWorldEffects> out{};

    SECTION("a node source rides the node's transform") {
        world::WorldEffect e;
        e.name = "n";
        e.source.kind = world::SourceKind::Node;
        e.source.name = "altar";
        e.propagation.direction = world::DirectionMode::Explicit;
        REQUIRE(e.validate());
        const auto ctx = contextAt(1.0, spans, heroes, scene);
        REQUIRE(world::resolveWorldEffects(std::array{e}, ctx, out) == 1);
        CHECK_THAT(out[0].origin.x, WithinAbs(10.0f, 1e-4f));
        CHECK_THAT(out[0].origin.z, WithinAbs(-4.0f, 1e-4f));
    }

    SECTION("a node source that is not in this scene falls back to its authored position") {
        // Rather than jumping to the origin, which is what a scene swap would otherwise do to it.
        world::WorldEffect e;
        e.name = "n";
        e.source.kind = world::SourceKind::Node;
        e.source.name = "gone";
        e.source.position = glm::vec3(7.0f, 0.0f, 7.0f);
        e.propagation.direction = world::DirectionMode::Explicit;
        const auto ctx = contextAt(1.0, spans, heroes, scene);
        REQUIRE(world::resolveWorldEffects(std::array{e}, ctx, out) == 1);
        CHECK_THAT(out[0].origin.x, WithinAbs(7.0f, 1e-4f));
    }

    SECTION("a hero source lends its accent colour to an effect that stated none") {
        world::WorldEffect e;
        e.name = "n";
        e.source.kind = world::SourceKind::Hero;
        e.source.name = "elder";
        e.appearance.color = glm::vec3(1.0f); // "no opinion"
        e.propagation.direction = world::DirectionMode::Explicit;
        const auto ctx = contextAt(1.0, spans, heroes, scene);
        REQUIRE(world::resolveWorldEffects(std::array{e}, ctx, out) == 1);
        CHECK_THAT(out[0].color.g, WithinAbs(0.9f, 1e-4f));
    }

    SECTION("a ground offset drops the origin to the base of the thing") {
        world::WorldEffect e = world::heroGroundPulse("Pulse");
        e.source.kind = world::SourceKind::Hero;
        e.source.name = "elder";
        e.source.groundOffset = 5.0f;
        e.activation = world::Activation::Always;
        e.timing.delay = 0.0;
        e.timing.fadeIn = 0.0;
        const auto ctx = contextAt(1.0, spans, heroes, scene);
        REQUIRE(world::resolveWorldEffects(std::array{e}, ctx, out) == 1);
        CHECK_THAT(out[0].origin.y, WithinAbs(0.0f, 1e-4f)); // hero at y=5, offset 5
    }

    SECTION("a focus-hero source follows whoever the cut is holding") {
        world::WorldEffect e = world::heroGroundPulse("Pulse");
        e.timing.delay = 0.0;
        e.timing.fadeIn = 0.0;
        const auto ctx = contextAt(4.0, spans, heroes, scene);
        REQUIRE(world::resolveWorldEffects(std::array{e}, ctx, out) == 1);
        CHECK_THAT(out[0].origin.x, WithinAbs(0.0f, 1e-4f)); // the elder, not the lantern
    }
}

TEST_CASE("a direction is derived, not authored", "[world][effects][direction]") {
    const FakeScene scene;
    const auto heroes = twoHeroes();
    const auto spans = cut();
    std::array<world::ResolvedEffect, world::kMaxGpuWorldEffects> out{};
    // Inside the travel span, so a camera-travel effect is live and the handoff is resolvable.
    const auto ctx = contextAt(12.0, spans, heroes, scene);

    SECTION("camera -> target points at the shot's handoff, not at the subject it is leaving") {
        world::WorldEffect e = world::cameraTravelBeam("Beam");
        e.propagation.direction = world::DirectionMode::CameraToTarget;
        e.timing.delay = 0.0;
        e.timing.fadeIn = 0.0;
        REQUIRE(world::resolveWorldEffects(std::array{e}, ctx, out) == 1);
        // Camera at (0, 20, 60), lantern at (100, 2, 0): the axis must lead with +x.
        CHECK(out[0].axis.x > 0.7f);
    }

    SECTION("camera velocity uses the trajectory, not the aim") {
        world::WorldEffect e = world::cameraTravelBeam("Beam");
        e.propagation.direction = world::DirectionMode::CameraVelocity;
        e.timing.delay = 0.0;
        e.timing.fadeIn = 0.0;
        REQUIRE(world::resolveWorldEffects(std::array{e}, ctx, out) == 1);
        CHECK_THAT(out[0].axis.z, WithinAbs(-1.0f, 1e-4f));
    }

    SECTION("the blend leans towards the destination without ignoring where the camera looks") {
        world::WorldEffect e = world::cameraTravelBeam("Beam");
        e.timing.delay = 0.0;
        e.timing.fadeIn = 0.0;
        REQUIRE(e.propagation.direction == world::DirectionMode::Blended);
        REQUIRE(world::resolveWorldEffects(std::array{e}, ctx, out) == 1);
        const glm::vec3 axis = out[0].axis;
        CHECK_THAT(glm::length(axis), WithinAbs(1.0f, 1e-4f));
        CHECK(axis.x > 0.0f);  // the destination pulls it sideways
        CHECK(axis.z < 0.0f);  // the aim and the velocity hold it forward
    }

    SECTION("a source's own forward axis, when it has one") {
        world::WorldEffect e;
        e.name = "n";
        e.source.kind = world::SourceKind::Node;
        e.source.name = "altar";
        e.propagation.kind = world::PropagationKind::DirectionalWave;
        e.propagation.direction = world::DirectionMode::SourceForward;
        e.timing.fadeIn = 0.0;
        e.timing.repeatSeconds = 1.0; // Always makes one pass; a standing effect repeats
        REQUIRE(world::resolveWorldEffects(std::array{e}, ctx, out) == 1);
        CHECK_THAT(out[0].axis.x, WithinAbs(1.0f, 1e-4f));
    }

    SECTION("a mode with nothing to read falls back rather than producing a zero axis") {
        world::WorldEffect e;
        e.name = "n";
        e.source.kind = world::SourceKind::World;
        e.propagation.kind = world::PropagationKind::DirectionalWave;
        e.propagation.direction = world::DirectionMode::SourceForward; // a world source has no forward
        e.timing.fadeIn = 0.0;
        e.timing.repeatSeconds = 1.0;
        REQUIRE(world::resolveWorldEffects(std::array{e}, ctx, out) == 1);
        CHECK_THAT(glm::length(out[0].axis), WithinAbs(1.0f, 1e-4f));
    }
}

TEST_CASE("activation is gated on the cut, not on a timer", "[world][effects][activation]") {
    const FakeScene scene;
    const auto heroes = twoHeroes();
    const auto spans = cut();
    std::array<world::ResolvedEffect, world::kMaxGpuWorldEffects> out{};

    world::WorldEffect beam = world::cameraTravelBeam("Beam");
    beam.timing.delay = 0.0;
    beam.timing.fadeIn = 0.0;
    beam.timing.fadeOut = 0.0;
    world::WorldEffect pulse = world::heroGroundPulse("Pulse");
    pulse.timing.delay = 0.0;
    pulse.timing.fadeIn = 0.0;
    pulse.timing.fadeOut = 0.0;
    const std::array effects{beam, pulse};

    SECTION("while the camera holds a hero, the pulse runs and the beam does not") {
        const auto ctx = contextAt(4.0, spans, heroes, scene);
        REQUIRE(world::resolveWorldEffects(effects, ctx, out) == 1);
        CHECK(out[0].effect->name == "Pulse");
    }
    SECTION("while the camera travels, the beam runs and the pulse does not") {
        const auto ctx = contextAt(12.0, spans, heroes, scene);
        REQUIRE(world::resolveWorldEffects(effects, ctx, out) == 1);
        CHECK(out[0].effect->name == "Beam");
    }
    SECTION("past the end of the cut, neither exists") {
        const auto ctx = contextAt(40.0, spans, heroes, scene);
        CHECK(world::resolveWorldEffects(effects, ctx, out) == 0);
    }
    SECTION("with no cut at all, neither exists -- which is the honest answer for a free camera") {
        const std::vector<world::ShotSpan> none;
        const auto ctx = contextAt(4.0, none, heroes, scene);
        CHECK(world::resolveWorldEffects(effects, ctx, out) == 0);
    }
    SECTION("a disabled effect is not resolved however open its window is") {
        world::WorldEffect off = pulse;
        off.enabled = false;
        const auto ctx = contextAt(4.0, spans, heroes, scene);
        CHECK(world::resolveWorldEffects(std::array{off}, ctx, out) == 0);
    }
    SECTION("an authored window activates on the transport clock alone") {
        world::WorldEffect w;
        w.name = "w";
        w.activation = world::Activation::Window;
        w.timing.windowStart = 30.0;
        w.timing.windowSeconds = 4.0;
        w.timing.fadeIn = 0.0;
        w.timing.fadeOut = 0.0;
        w.propagation.direction = world::DirectionMode::Explicit;
        CHECK(world::resolveWorldEffects(std::array{w}, contextAt(29.9, spans, heroes, scene), out) == 0);
        CHECK(world::resolveWorldEffects(std::array{w}, contextAt(31.0, spans, heroes, scene), out) == 1);
        CHECK(world::resolveWorldEffects(std::array{w}, contextAt(34.1, spans, heroes, scene), out) == 0);
    }
}

TEST_CASE("timing shapes the effect inside its activation", "[world][effects][timing]") {
    const FakeScene scene;
    const auto heroes = twoHeroes();
    const auto spans = cut();
    std::array<world::ResolvedEffect, world::kMaxGpuWorldEffects> out{};

    world::WorldEffect pulse = world::heroGroundPulse("Pulse");
    pulse.timing.delay = 1.0;
    pulse.timing.fadeIn = 2.0;
    pulse.timing.fadeOut = 2.0;
    pulse.timing.repeatSeconds = 0.0;
    pulse.timing.lifetime = 0.0; // as long as the hold

    SECTION("the delay holds it off") {
        CHECK(world::resolveWorldEffects(std::array{pulse}, contextAt(0.5, spans, heroes, scene), out) == 0);
    }
    SECTION("the fade in ramps rather than switching") {
        REQUIRE(world::resolveWorldEffects(std::array{pulse}, contextAt(2.0, spans, heroes, scene), out) == 1);
        const float early = out[0].envelope;
        REQUIRE(world::resolveWorldEffects(std::array{pulse}, contextAt(3.0, spans, heroes, scene), out) == 1);
        CHECK(out[0].envelope > early);
        CHECK(early > 0.0f);
        CHECK(early < 1.0f);
    }
    SECTION("the fade out is symmetric at the end of the hold") {
        // Slowed so the front is still inside its range when the hold ends: this section is about
        // the envelope, and an effect that has simply outrun its range would answer a different
        // question with the same number (ADR-182).
        world::WorldEffect slow = pulse;
        slow.propagation.speed = 1.0f;
        REQUIRE(world::resolveWorldEffects(std::array{slow}, contextAt(9.5, spans, heroes, scene), out) == 1);
        CHECK(out[0].envelope < 0.5f);
    }
    SECTION("the front advances at the stated speed") {
        REQUIRE(world::resolveWorldEffects(std::array{pulse}, contextAt(3.0, spans, heroes, scene), out) == 1);
        const float a = out[0].frontDistance;
        REQUIRE(world::resolveWorldEffects(std::array{pulse}, contextAt(4.0, spans, heroes, scene), out) == 1);
        CHECK_THAT(out[0].frontDistance - a, WithinRel(pulse.propagation.speed, 1e-4f));
    }
    SECTION("a repeat restarts the front, so a scrub lands the same ring") {
        world::WorldEffect repeating = pulse;
        repeating.timing.repeatSeconds = 2.0;
        REQUIRE(world::resolveWorldEffects(std::array{repeating}, contextAt(2.5, spans, heroes, scene), out) == 1);
        const float first = out[0].frontDistance;
        // 2 s later is the same point in the next ring: the same front distance, to the bit.
        REQUIRE(world::resolveWorldEffects(std::array{repeating}, contextAt(4.5, spans, heroes, scene), out) == 1);
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
    const std::array effects{world::cameraTravelBeam("Beam"), world::heroGroundPulse("Pulse")};

    world::WorldEffectFrame realtime{};
    world::WorldEffectFrame offline{};
    // A "realtime playthrough": every 1/120 s up to the second in question.
    for (int i = 0; i <= 120 * 12; ++i) {
        world::buildWorldEffectFrame(effects, contextAt(i / 120.0, spans, heroes, scene), realtime);
    }
    // An "offline render" that jumps straight there at 30 fps.
    for (int i = 0; i <= 30 * 12; ++i) {
        world::buildWorldEffectFrame(effects, contextAt(i / 30.0, spans, heroes, scene), offline);
    }
    REQUIRE(realtime.count == offline.count);
    CHECK(std::memcmp(realtime.effects, offline.effects, sizeof(realtime.effects)) == 0);
}

TEST_CASE("packing puts every authored number in the lane the shader reads", "[world][effects][gpu]") {
    const FakeScene scene;
    const auto heroes = twoHeroes();
    const auto spans = cut();
    std::array<world::ResolvedEffect, world::kMaxGpuWorldEffects> out{};

    world::WorldEffect e = world::heroGroundPulse("Pulse");
    e.timing.delay = 0.0;
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    e.timing.repeatSeconds = 0.0;
    e.propagation.ringCount = 3.0f;
    e.propagation.range = 55.0f;
    e.appearance.intensity = 2.0f;
    e.appearance.color = glm::vec3(0.25f, 0.5f, 1.0f);
    e.appearance.width = 2.0f;
    e.sparkle.enabled = true;
    e.sparkle.fadeDistance = 64.0f;
    REQUIRE(world::resolveWorldEffects(std::array{e}, contextAt(2.0, spans, heroes, scene), out) == 1);
    const world::WorldEffectGpu g = world::packWorldEffect(out[0]);

    CHECK_THAT(g.originKind.w, WithinAbs(1.0f, 1e-6f)); // RadialWave
    CHECK_THAT(g.axisFront.w, WithinRel(e.propagation.speed * 2.0f, 1e-4f));
    // `width` multiplies the widths and nothing else, which is what makes it one knob.
    CHECK_THAT(g.shape.x, WithinRel(e.propagation.frontWidth * 2.0f, 1e-4f));
    CHECK_THAT(g.shape.y, WithinRel(e.propagation.trailLength * 2.0f, 1e-4f));
    CHECK_THAT(g.shape.z, WithinAbs(55.0f, 1e-4f));
    CHECK_THAT(g.vertical.z, WithinAbs(3.0f, 1e-6f));
    // Intensity and the envelope are folded into the radiance, so the shader multiplies once.
    CHECK_THAT(g.color.r, WithinRel(0.5f, 1e-4f));
    CHECK_THAT(g.color.w, WithinAbs(64.0f, 1e-4f)); // the sparkle fade distance
    CHECK_THAT(g.sparkle.x, WithinRel(e.sparkle.density, 1e-4f));
    CHECK_THAT(g.response.y, WithinRel(e.response.foliage, 1e-4f));

    SECTION("sparkle off is a zero density, so the shader's guard is the off switch") {
        world::WorldEffect quiet = e;
        quiet.sparkle.enabled = false;
        REQUIRE(world::resolveWorldEffects(std::array{quiet}, contextAt(2.0, spans, heroes, scene), out) == 1);
        CHECK_THAT(world::packWorldEffect(out[0]).sparkle.x, WithinAbs(0.0f, 1e-9f));
    }
    SECTION("rainbow is a flag the shader branches on, not a colour list") {
        world::WorldEffect bow = e;
        bow.appearance.rainbow = true;
        REQUIRE(world::resolveWorldEffects(std::array{bow}, contextAt(2.0, spans, heroes, scene), out) == 1);
        CHECK_THAT(world::packWorldEffect(out[0]).edge.w, WithinAbs(1.0f, 1e-9f));
    }
}

TEST_CASE("no more than the GPU limit of effects is ever written", "[world][effects][gpu]") {
    const FakeScene scene;
    const auto heroes = twoHeroes();
    const std::vector<world::ShotSpan> none;
    std::vector<world::WorldEffect> many;
    for (int i = 0; i < 12; ++i) {
        world::WorldEffect e;
        e.name = "e" + std::to_string(i);
        e.propagation.direction = world::DirectionMode::Explicit;
        e.timing.fadeIn = 0.0;
        many.push_back(e);
    }
    world::WorldEffectFrame frame{};
    world::buildWorldEffectFrame(many, contextAt(1.0, none, heroes, scene), frame);
    CHECK(frame.count == world::kMaxGpuWorldEffects);
}

TEST_CASE("a style configures parameters and nothing else", "[world][effects][presets]") {
    world::WorldEffect e = world::cameraTravelBeam("Beam");
    const world::EffectEndpoint sourceBefore = e.source;
    const world::Activation activationBefore = e.activation;
    REQUIRE(world::applyBeamStyle(e, "Rainbow"));
    CHECK(e.appearance.rainbow);
    CHECK(e.style == "Rainbow");
    // A style is appearance, not wiring: what the effect is *about* is untouched.
    CHECK(e.source.kind == sourceBefore.kind);
    CHECK(e.activation == activationBefore);
    CHECK_FALSE(world::applyBeamStyle(e, "Not A Style"));
    CHECK(e.style == "Rainbow");

    world::WorldEffect p = world::heroGroundPulse("Pulse");
    REQUIRE(world::applyPulseStyle(p, "Shockwave"));
    CHECK_FALSE(p.sparkle.enabled);
    CHECK(p.propagation.falloff > 2.0f);
    CHECK_FALSE(world::beamStyleNames().empty());
    CHECK_FALSE(world::pulseStyleNames().empty());
}

TEST_CASE("every meaningful world effect parameter is declared and modulatable",
          "[world][effects][params]") {
    params::ParameterSet set;
    const std::vector<world::WorldEffect> effects{world::cameraTravelBeam("Beam"),
                                                  world::heroGroundPulse("Pulse")};
    world::WorldEffectParameters registered = world::registerWorldEffectParameters(set, effects);
    REQUIRE(registered.effects.size() == 2);

    CHECK(set.find("worldfx/Beam/intensity") != nullptr);
    CHECK(set.find("worldfx/Beam/sparkleIntensity") != nullptr);
    CHECK(set.find("worldfx/Beam/rainbowSpeed") != nullptr);
    CHECK(set.find("worldfx/Pulse/speed") != nullptr);
    CHECK(set.find("worldfx/Pulse/response/foliage") != nullptr);
    CHECK(set.find("worldfx/Pulse/repeat") != nullptr);
    CHECK(world::worldEffectParameterPrefix("Pulse") == "worldfx/Pulse/");

    // §16: if a parameter can be authored, it must be possible to modulate it. A route only binds to
    // a parameter whose flags allow it, so this is the check that the promise is real.
    for (const std::string& path : registered.registered) {
        const params::IParameter* p = set.find(path);
        REQUIRE(p != nullptr);
        INFO(path);
        CHECK(p->flags().modulatable);
        CHECK(p->flags().serialized);
        CHECK(p->group() == "worldfx");
    }

    SECTION("defaults come from the authored effect, not from a table") {
        CHECK_THAT(set.find("worldfx/Pulse/speed")->baseComponent(0),
                   WithinRel(effects[1].propagation.speed, 1e-5f));
    }

    SECTION("the finals reach the live effect") {
        std::vector<world::WorldEffect> live = effects;
        set.find("worldfx/Beam/intensity")->setFinalComponent(0, 7.5f);
        set.find("worldfx/Pulse/enabled")->setFinalComponent(0, 0.0f);
        world::applyWorldEffectParameters(registered, live);
        CHECK_THAT(live[0].appearance.intensity, WithinAbs(7.5f, 1e-5f));
        CHECK_FALSE(live[1].enabled);
    }

    SECTION("a modulation route reaches an effect parameter through the ordinary path") {
        signals::SignalBus bus;
        const signals::SignalId beat = bus.declare("beat.pulse", 0.0f, 1.0f, true);
        params::Modulator modulator;
        params::ModRoute route;
        route.source = "beat.pulse";
        route.target = "worldfx/Pulse/intensity";
        route.amount = 3.0f;
        modulator.addRoute(route);
        const auto bound = modulator.bind(bus, set);
        REQUIRE(bound);
        set.resetFinals();
        bus.setEvent(beat, true, 1.0f);
        modulator.applyRoutes(bus, set, 1.0 / 60.0);
        std::vector<world::WorldEffect> live = effects;
        world::applyWorldEffectParameters(registered, live);
        CHECK(live[1].appearance.intensity > effects[1].appearance.intensity);
    }

    SECTION("unregistering removes every path it wrote") {
        world::unregisterWorldEffectParameters(set, registered);
        CHECK(set.find("worldfx/Beam/intensity") == nullptr);
        CHECK(set.find("worldfx/Pulse/response/emissive") == nullptr);
        CHECK(registered.effects.empty());
        // And applying a cleared registrar leaves the effects exactly as authored.
        std::vector<world::WorldEffect> live = effects;
        world::applyWorldEffectParameters(registered, live);
        CHECK_THAT(live[0].appearance.intensity, WithinRel(effects[0].appearance.intensity, 1e-6f));
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
    class SceneWithHeroNode final : public world::WorldEffectScene {
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
    std::array<world::ResolvedEffect, world::kMaxGpuWorldEffects> out{};

    world::WorldEffect e;
    e.name = "ripple";
    e.source.kind = world::SourceKind::Hero;
    e.source.name = "elder";
    e.timing.fadeIn = 0.0;
    world::WorldEffectContext ctx;
    ctx.seconds = 0.5;
    ctx.heroes = heroes;
    ctx.scene = &scene;
    REQUIRE(world::resolveWorldEffects(std::array{e}, ctx, out) == 1);
    CHECK_THAT(out[0].origin.y, WithinAbs(1.0f, 1e-4f));

    SECTION("and falls back to the hero's own position when the scene has no such node") {
        world::WorldEffectContext bare = ctx;
        bare.scene = nullptr;
        REQUIRE(world::resolveWorldEffects(std::array{e}, bare, out) == 1);
        CHECK_THAT(out[0].origin.y, WithinAbs(5.0f, 1e-4f));
    }
}

TEST_CASE("world effects round-trip through a scene file", "[world][effects][json][composition]") {
    // The `"worldEffects"` block is a sibling of `"heroes"`, and the properties that matter are the
    // ones a hand-edited file depends on: a scene that never declared one is unchanged, a scene that
    // did gets it back, and a malformed one is refused with the index in the message.
    const std::string base = R"({"format": "avgen-scene", "version": 1, "name": "fx", "nodes": []})";
    assets::AssetRegistry registry;

    SECTION("a scene with no world effects has none, and writes none back") {
        auto comp = scene::Composition::fromJson(nlohmann::json::parse(base), registry);
        REQUIRE(comp);
        CHECK((*comp)->worldEffects().empty());
        CHECK_FALSE((*comp)->toJson().contains("worldEffects"));
    }

    SECTION("a declared effect survives the trip") {
        nlohmann::json doc = nlohmann::json::parse(base);
        doc["worldEffects"] = nlohmann::json::array({world::heroGroundPulse("Ripple").toJson(),
                                                     world::cameraTravelBeam("Beam").toJson()});
        auto comp = scene::Composition::fromJson(doc, registry);
        REQUIRE(comp);
        REQUIRE((*comp)->worldEffects().size() == 2);
        CHECK((*comp)->worldEffects()[0].name == "Ripple");
        CHECK((*comp)->worldEffects()[1].activation == world::Activation::CameraTravel);
        const nlohmann::json written = (*comp)->toJson();
        REQUIRE(written.contains("worldEffects"));
        CHECK(written["worldEffects"] == doc["worldEffects"]);
    }

    SECTION("a malformed effect is refused, and the message says which one") {
        nlohmann::json doc = nlohmann::json::parse(base);
        doc["worldEffects"] = nlohmann::json::array(
            {world::heroGroundPulse("Ripple").toJson(), nlohmann::json{{"name", "bad"}, {"propagation", {{"speed", -1.0}}}}});
        auto comp = scene::Composition::fromJson(doc, registry);
        REQUIRE_FALSE(comp);
        CHECK(comp.error().message.find("worldEffects[1]") != std::string::npos);
    }

    SECTION("two effects of one name are refused, because a name is half a parameter path") {
        nlohmann::json doc = nlohmann::json::parse(base);
        doc["worldEffects"] = nlohmann::json::array(
            {world::heroGroundPulse("Same").toJson(), world::cameraTravelBeam("Same").toJson()});
        CHECK_FALSE(scene::Composition::fromJson(doc, registry));
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
    REQUIRE(doc.contains("worldEffects"));
    std::vector<world::WorldEffect> effects;
    for (const nlohmann::json& entry : doc.at("worldEffects")) {
        auto e = world::WorldEffect::fromJson(entry);
        REQUIRE(e);
        effects.push_back(std::move(*e));
    }
    REQUIRE(world::validateWorldEffects(effects));
    REQUIRE(effects.size() == 2);

    const world::WorldEffect& beam = effects[0];
    CHECK(beam.propagation.kind == world::PropagationKind::DirectionalWave);
    // The camera beam fires while the camera travels, and it is aimed at where it is going rather
    // than along a world axis somebody typed.
    CHECK(beam.activation == world::Activation::CameraTravel);
    CHECK(beam.source.kind == world::SourceKind::Camera);
    CHECK(beam.propagation.direction == world::DirectionMode::Blended);
    CHECK(beam.hasTarget);
    CHECK(beam.target.kind == world::SourceKind::FocusHero);

    const world::WorldEffect& pulse = effects[1];
    CHECK(pulse.propagation.kind == world::PropagationKind::RadialWave);
    CHECK(pulse.activation == world::Activation::HeroFocus);
    // No Glowmere coordinates anywhere: the pulse follows whichever hero the cut is holding.
    CHECK(pulse.source.kind == world::SourceKind::FocusHero);
    CHECK(pulse.source.name.empty());
    // It repeats, or it is one ring at the start of a ten-second hold rather than a pulse.
    CHECK(pulse.timing.repeatSeconds > 0.0);
#endif
}

TEST_CASE("the Beat response slider owns an ordinary modulation route", "[world][effects][ui]") {
    // §13 and §16: no special-case audio hook. What the panel's one musical control writes is a
    // route the Modulation panel can see, curve, re-point and delete.
    CHECK(ui::beatResponseSource() == "beat.pulse");
    CHECK(ui::beatResponseTarget("Hero Pulse") == "worldfx/Hero Pulse/intensity");
    // A 0..1 slider has to mean something in the target's units: the whole of what the knob offers.
    CHECK_THAT(ui::beatResponseDepth(1.0f, 8.0f), WithinAbs(8.0f, 1e-6f));
    CHECK_THAT(ui::beatResponseDepth(0.25f, 8.0f), WithinAbs(2.0f, 1e-6f));
    CHECK_THAT(ui::beatResponseDepth(-3.0f, 8.0f), WithinAbs(0.0f, 1e-6f));
}
