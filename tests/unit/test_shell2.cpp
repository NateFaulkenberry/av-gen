// SHELL phase 2 (Effect Library Wave 3): Charge-Up, Light Beam, Halo, Bubble, Portal, Reality Tear on
// the CPU -- the gate, each type's shading and status, the budgets and the reasons they leave, the
// secondary DF and EMIT hooks, the Light owner, the phases (charge, pop, open), the tear's outline,
// and play = scrub on a keyed owner. The pixels are in tests/rendering/test_shell2_gpu.cpp.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "scene/particles.hpp"
#include "world/effects/distortion_frame.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_lights.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"
#include "world/effects/effect_timing.hpp"
#include "world/effects/effect_trigger.hpp"
#include "world/effects/particle_emitter.hpp"
#include "world/effects/shell_frame.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>

#include <bit>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace avgen::world {
float chargeUpCharge(const EffectInstance& e, const EffectContext& ctx); // kinds/charge_up_effect.cpp
}

namespace {

using world::EffectKind;
using world::EffectOwner;
using world::EffectStatus;

// One node, `craft`, drawn with a box from (-2,-1,-2) to (2,1,2) round (0, 6, 0), and one spot light,
// `spot`, 10 m up pointing down with a 20 degree cone and a 15 m range.
class FakeScene final : public world::EffectSceneQuery {
public:
    glm::vec3 centre{0.0f, 6.0f, 0.0f};
    glm::vec3 half{2.0f, 1.0f, 2.0f};
    float lightIntensity = 500.0f;
    [[nodiscard]] bool nodePosition(std::string_view name, glm::vec3& out) const override {
        if (name != "craft") {
            return false;
        }
        out = centre;
        return true;
    }
    [[nodiscard]] bool nodeView(std::string_view name, world::NodeView& out) const override {
        if (name != "craft") {
            return false;
        }
        out = world::NodeView{};
        out.world[3] = glm::vec4(centre, 1.0f);
        out.boundsMin = centre - half;
        out.boundsMax = centre + half;
        out.hasBounds = true;
        out.entityCount = 1;
        return true;
    }
    [[nodiscard]] bool lightView(std::string_view id, world::LightView& out) const override {
        if (id != "spot") {
            return false;
        }
        out = world::LightView{};
        out.position = glm::vec3(3.0f, 10.0f, -1.0f);
        out.direction = glm::vec3(0.0f, -1.0f, 0.0f);
        out.color = glm::vec3(1.0f, 0.5f, 0.25f);
        out.intensity = lightIntensity;
        out.range = 15.0f;
        out.outerCone = glm::radians(20.0f);
        out.spot = true;
        return true;
    }
};

world::EffectInstance make(EffectKind kind, std::string id, EffectOwner owner) {
    world::EffectInstance e = world::makeEffect(kind, id);
    e.id = std::move(id);
    e.owner = std::move(owner);
    return e;
}

world::EffectInstance always(EffectKind kind, std::string id, EffectOwner owner) {
    world::EffectInstance e = make(kind, std::move(id), std::move(owner));
    e.activation = world::Activation::Always;
    e.timing = world::Timing{};
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    return e;
}

world::Trigger repeat(double period, double phase) {
    world::Trigger t;
    t.source = world::TriggerSource::Repeat;
    t.period = period;
    t.phase = phase;
    return t;
}

struct Built {
    world::ShellFrame frame;
    world::EffectLightFrame lights;
    world::DistortionFrame distortion;
    std::vector<scene::ParticleSystem> particles;
    std::vector<EffectStatus> status;
    std::vector<std::string> reasons;
};

// The Particles-and-later builders in the evaluator's order: DF, EMIT, then SHELL.
Built build(const std::vector<world::EffectInstance>& effects, const world::EffectContext& ctx) {
    Built b;
    b.status.assign(effects.size(), EffectStatus::Dormant);
    b.reasons.assign(effects.size(), std::string());
    world::buildDistortionFrame(effects, ctx, b.distortion, {}, b.status, b.reasons);
    world::buildParticleFrame(effects, ctx, b.particles, {}, b.status, b.reasons);
    world::buildShellFrame(effects, ctx, b.frame, b.lights, {}, b.status, b.reasons);
    return b;
}

const std::vector<EffectKind> kPhase2 = {EffectKind::ChargeUp, EffectKind::LightBeam, EffectKind::Halo,
                                         EffectKind::Bubble,   EffectKind::Portal,    EffectKind::RealityTear};

template <typename T>
bool sameBytes(const std::vector<T>& a, const std::vector<T>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
}

} // namespace

TEST_CASE("the shell2 gate: disabled phase-2 types add no shell, no proxy, no particle system",
          "[shell2][effects][gate]") {
    FakeScene scene;
    world::EffectContext ctx;
    ctx.seconds = 2.0;
    ctx.scene = &scene;
    std::vector<world::EffectInstance> effects;
    for (const EffectKind k : kPhase2) {
        world::EffectInstance e = always(k, world::effectSchema(k)->key, EffectOwner::world());
        e.enabled = false;
        effects.push_back(e);
    }
    Built b = build(effects, ctx);
    CHECK(b.frame.empty());
    CHECK(b.frame.instances.capacity() == 0);
    CHECK(b.distortion.count == 0);
    CHECK(b.particles.empty());
    CHECK(b.lights.count == 0);
    for (std::size_t i = 0; i < effects.size(); ++i) {
        INFO(effects[i].id);
        CHECK(b.status[i] == EffectStatus::Disabled);
    }
    // The control: enabled, every one of them draws.
    for (world::EffectInstance& e : effects) {
        e.enabled = true;
    }
    Built on = build(effects, ctx);
    for (std::size_t i = 0; i < effects.size(); ++i) {
        INFO(effects[i].id << ": " << on.reasons[i]);
        CHECK(on.status[i] == EffectStatus::Drawn);
    }
    CHECK(on.frame.instances.size() == 7); // Charge-Up is two shells: its core and its flare
    CHECK(on.distortion.count >= 3);       // Bubble 1, Portal 1, Reality Tear 6
    CHECK_FALSE(on.particles.empty());
}

TEST_CASE("each phase-2 type draws through the shading and mesh its look needs", "[shell2][effects]") {
    FakeScene scene;
    world::EffectContext ctx;
    ctx.seconds = 2.0;
    ctx.scene = &scene;
    struct Want {
        EffectKind kind;
        const char* style;
        world::ShellShading shading;
        world::ShellMesh mesh;
    };
    const Want wants[] = {
        {EffectKind::LightBeam, "UFO Tractor", world::ShellShading::Beam, world::ShellMesh::Box},
        {EffectKind::Halo, "Lamp Glare", world::ShellShading::Glare, world::ShellMesh::Quad},
        {EffectKind::Halo, "Saint Ring", world::ShellShading::Ring, world::ShellMesh::Box},
        {EffectKind::Bubble, "Soap Bubble", world::ShellShading::Bubble, world::ShellMesh::Sphere},
        {EffectKind::Portal, "Magic Portal", world::ShellShading::Portal, world::ShellMesh::Disc},
        {EffectKind::RealityTear, "Void Rift", world::ShellShading::Tear, world::ShellMesh::Quad},
    };
    for (const Want& w : wants) {
        world::EffectInstance e = always(w.kind, "fx", EffectOwner::world());
        REQUIRE(world::applyEffectStyle(e, w.kind, w.style));
        Built b = build({e}, ctx);
        INFO(w.style << ": " << b.reasons[0]);
        CHECK(b.status[0] == EffectStatus::Drawn);
        REQUIRE(b.frame.batches.size() == 1);
        CHECK(b.frame.batches[0].shading == w.shading);
        CHECK(b.frame.batches[0].mesh == w.mesh);
        CHECK(world::effectSchema(w.kind)->resolve.records(e, ctx) == 1);
    }
    SECTION("a Charge-Up is a plasma core and a glare") {
        world::EffectInstance e = make(EffectKind::ChargeUp, "charge", EffectOwner::entity("craft"));
        world::TriggerClock clock;
        clock.setFrame(2.0);
        ctx.triggers = &clock;
        Built b = build({e}, ctx);
        INFO(b.reasons[0]);
        CHECK(b.status[0] == EffectStatus::Drawn);
        REQUIRE(b.frame.batches.size() == 2);
        CHECK(b.frame.batches[0].shading == world::ShellShading::Plasma);
        CHECK(b.frame.batches[1].shading == world::ShellShading::Glare);
        CHECK(b.frame.instances[0].model[3].y == Approx(6.0f)); // at the owner's centre
    }
}

TEST_CASE("the shell budget refuses a phase-2 type by name", "[shell2][effects][capacity]") {
    world::EffectContext ctx;
    ctx.seconds = 1.0;
    std::vector<world::EffectInstance> effects;
    for (std::uint32_t i = 0; i < world::kMaxShells; ++i) {
        world::EffectInstance e = always(EffectKind::ForceField, "wall-" + std::to_string(i), EffectOwner::world());
        effects.push_back(e);
    }
    const std::size_t firstMine = effects.size();
    for (const EffectKind k : kPhase2) {
        effects.push_back(always(k, world::effectSchema(k)->key, EffectOwner::world()));
    }
    world::TriggerClock clock;
    clock.setFrame(1.0);
    ctx.triggers = &clock;
    Built b = build(effects, ctx);
    CHECK(b.frame.instances.size() == world::kMaxShells);
    for (std::size_t i = firstMine; i < effects.size(); ++i) {
        INFO(effects[i].id << ": " << b.reasons[i]);
        CHECK(b.status[i] == EffectStatus::Dropped);
        CHECK(b.reasons[i].find("128") != std::string::npos);
    }
}

TEST_CASE("a phase-2 type whose DF or EMIT part did not fit is Partial, and says which",
          "[shell2][effects][capacity]") {
    world::EffectContext ctx;
    ctx.seconds = 1.0;
    std::vector<world::EffectInstance> effects;
    SECTION("the distortion budget") {
        for (std::size_t i = 0; i < world::kMaxDistortionProxies; ++i) {
            world::EffectInstance w = always(EffectKind::SpaceWarp, "warp-" + std::to_string(i), EffectOwner::world());
            effects.push_back(w);
        }
        effects.push_back(always(EffectKind::Portal, "portal", EffectOwner::world()));
        // The portal is first in evaluation order in the engine (the Particles stage is before the
        // ScreenSpace one); here the list order is the walk, so the warps fill the budget first.
        Built b = build(effects, ctx);
        CHECK(b.distortion.count == world::kMaxDistortionProxies);
        INFO(b.reasons.back());
        CHECK(b.status.back() == EffectStatus::Partial);
        CHECK(b.reasons.back().find("distortion") != std::string::npos);
        CHECK(b.frame.instances.size() == 1); // it still draws
    }
    SECTION("the particle-system budget") {
        for (std::size_t i = 0; i < world::kMaxEffectParticleSystems; ++i) {
            world::EffectInstance p = always(EffectKind::ParticleEmitter, "motes-" + std::to_string(i), EffectOwner::world());
            effects.push_back(p);
        }
        effects.push_back(always(EffectKind::Portal, "portal", EffectOwner::world()));
        Built b = build(effects, ctx);
        CHECK(b.particles.size() == world::kMaxEffectParticleSystems);
        INFO(b.reasons.back());
        CHECK(b.status.back() == EffectStatus::Partial);
        CHECK(b.reasons.back().find("particle") != std::string::npos);
    }
}

TEST_CASE("Light Beam from a spot light matches its cone, range and colour", "[shell2][effects][beam]") {
    FakeScene scene;
    world::EffectContext ctx;
    ctx.seconds = 2.0;
    ctx.scene = &scene;
    world::EffectInstance beam = always(EffectKind::LightBeam, "beam", EffectOwner::light("spot"));
    beam.values.setFloat("lightBeam/aperture", 0.0f);
    Built b = build({beam}, ctx);
    INFO(b.reasons[0]);
    REQUIRE(b.status[0] == EffectStatus::Drawn);
    REQUIRE(b.frame.instances.size() == 1);
    const world::ShellInstance& s = b.frame.instances[0];
    // The box's +Y is the source: centre = apex + axis * range / 2, col1 = -axis * range / 2.
    CHECK(s.model[3].x == Approx(3.0f));
    CHECK(s.model[3].y == Approx(10.0f - 7.5f));
    CHECK(s.model[1].y == Approx(7.5f));
    const float baseR = 15.0f * std::tan(glm::radians(20.0f));
    CHECK(glm::length(glm::vec3(s.model[0])) == Approx(baseR).epsilon(1e-4));
    CHECK(s.params[4].w == Approx(15.0f));
    // Its colour is tinted by the light's; the light itself lights the ground, so no pool light.
    CHECK(s.params[0].g == Approx(0.86f * 0.5f).epsilon(1e-4)); // Stage Spot's colour times the light's
    CHECK(b.lights.count == 0);

    SECTION("a light that is off shows no beam, and the panel says why") {
        scene.lightIntensity = 0.0f;
        Built off = build({beam}, ctx);
        CHECK(off.status[0] == EffectStatus::Dormant);
        CHECK(off.reasons[0].find("light") != std::string::npos);
        CHECK(off.frame.empty());
    }
    SECTION("from an entity it leaves the bottom of the owner and asks the pool for a light") {
        world::EffectInstance ufo = always(EffectKind::LightBeam, "tractor", EffectOwner::entity("craft"));
        Built e = build({ufo}, ctx);
        REQUIRE(e.frame.instances.size() == 1);
        const float length = ufo.values.getFloat("lightBeam/length", 0.0f);
        CHECK(e.frame.instances[0].model[3].y == Approx(5.0f - 0.5f * length)); // bottom at y = 5
        CHECK(e.lights.count == 1);
        CHECK(e.lights.lights[0].position.y < 5.0f);
    }
}

TEST_CASE("Halo stands at its light, or floats over its owner's head", "[shell2][effects][halo]") {
    FakeScene scene;
    world::EffectContext ctx;
    ctx.seconds = 1.25;
    ctx.scene = &scene;
    world::EffectInstance glare = always(EffectKind::Halo, "glare", EffectOwner::light("spot"));
    Built g = build({glare}, ctx);
    REQUIRE(g.frame.instances.size() == 1);
    CHECK(g.frame.instances[0].params[2].x == Approx(3.0f));
    CHECK(g.frame.instances[0].params[2].y == Approx(10.0f));

    world::EffectInstance ring = always(EffectKind::Halo, "ring", EffectOwner::entity("craft"));
    REQUIRE(world::applyEffectStyle(ring, EffectKind::Halo, "Saint Ring"));
    Built r = build({ring}, ctx);
    REQUIRE(r.frame.instances.size() == 1);
    // Top of the bounds (7) plus the height (0.3), plus a bob of at most 3 cm.
    CHECK(r.frame.instances[0].model[3].y == Approx(7.3f).margin(0.031f));
    CHECK(r.frame.batches[0].shading == world::ShellShading::Ring);
}

TEST_CASE("Charge-Up: charges, holds, releases once, then waits -- a function of the second",
          "[shell2][effects][charge][seek]") {
    FakeScene scene;
    world::TriggerClock clock;
    world::EffectContext ctx;
    ctx.scene = &scene;
    ctx.triggers = &clock;
    // Power Gather: every 4 s from 0.5 -- 2.5 s charge (ease 1.6), 0.5 s hold, 0.45 s release.
    world::EffectInstance e = make(EffectKind::ChargeUp, "charge", EffectOwner::entity("craft"));
    REQUIRE(e.activation == world::Activation::Trigger);
    const auto at = [&](double t) {
        ctx.seconds = t;
        clock.setFrame(t);
        return world::chargeUpCharge(e, ctx);
    };
    CHECK(at(0.25) == 0.0f); // before the first event
    CHECK(at(0.5 + 1.25) == Approx(std::pow(0.5, 1.6)).epsilon(1e-4));
    float last = 0.0f;
    for (double t = 0.5; t < 3.0; t += 0.05) {
        const float c = at(t);
        CHECK(c >= last);
        last = c;
    }
    CHECK(at(3.2) == 1.0f);  // holding (the release is at 3.5)
    CHECK(at(3.7) == 1.0f);  // releasing
    CHECK(at(4.1) == 0.0f);  // released: dormant until the next event at 4.5
    CHECK(at(4.5 + 2.6) == 1.0f);

    // The release is drawn bigger than the held core; after it, nothing.
    const auto shells = [&](double t) {
        ctx.seconds = t;
        clock.setFrame(t);
        return build({e}, ctx);
    };
    Built held = shells(3.3);
    Built releasing = shells(3.7);
    Built after = shells(4.1);
    REQUIRE(held.frame.instances.size() == 2);
    REQUIRE(releasing.frame.instances.size() == 2);
    CHECK(glm::length(glm::vec3(releasing.frame.instances[0].model[0])) >
          glm::length(glm::vec3(held.frame.instances[0].model[0])));
    CHECK(after.frame.empty());
    CHECK(after.status[0] == EffectStatus::Dormant);

    SECTION("the release burst fires on the frame that crosses it, and not after a seek past it") {
        std::vector<scene::ParticleSystem> particles;
        std::vector<EffectStatus> status(1, EffectStatus::Dormant);
        std::vector<std::string> reasons(1);
        float burstSeen = 0.0f;
        for (double t = 3.3; t < 3.7; t += 1.0 / 60.0) {
            ctx.seconds = t;
            clock.setFrame(t);
            world::buildParticleFrame({&e, 1}, ctx, particles, {}, status, reasons);
            REQUIRE(particles.size() == 1);
            burstSeen += particles[0].burst;
        }
        CHECK(burstSeen == Approx(300.0f)); // exactly one frame's worth
        world::TriggerClock jumped;
        jumped.setFrame(1.0);
        jumped.setFrame(3.6); // a jump: no interval, no edge
        ctx.triggers = &jumped;
        ctx.seconds = 3.6;
        world::buildParticleFrame({&e, 1}, ctx, particles, {}, status, reasons);
        CHECK(particles[0].burst == 0.0f);
    }
}

TEST_CASE("Bubble: each trigger pops it from a hashed point, and a new one inflates",
          "[shell2][effects][bubble][seek]") {
    world::TriggerClock clock;
    world::EffectContext ctx;
    ctx.triggers = &clock;
    world::EffectInstance e = always(EffectKind::Bubble, "bubble", EffectOwner::world());
    e.activation = world::Activation::Trigger;
    e.timing.trigger = repeat(4.0, 1.0); // pops at 1, 5, 9 ...
    const auto at = [&](double t) {
        ctx.seconds = t;
        clock.setFrame(t);
        return build({e}, ctx);
    };
    Built whole = at(0.5);
    REQUIRE(whole.status[0] == EffectStatus::Drawn);
    CHECK(whole.frame.instances[0].params[3].z == 0.0f);
    CHECK(whole.distortion.count == 1);

    Built popping = at(1.08);
    REQUIRE(popping.status[0] == EffectStatus::Drawn);
    const float progress = popping.frame.instances[0].params[3].z;
    CHECK(progress > 0.0f);
    CHECK(progress < 1.0f);
    CHECK(popping.distortion.count == 0); // no lens while it pops
    const glm::vec3 from(popping.frame.instances[0].params[4]);
    CHECK(glm::length(from) == Approx(1.0f).margin(1e-4));
    CHECK(from == world::shellHitDirection(popping.frame.instances[0].params[1].x, 1.0));

    Built gone = at(1.5);
    CHECK(gone.status[0] == EffectStatus::Dormant);
    CHECK(gone.reasons[0].find("Popped") != std::string::npos);
    CHECK(gone.frame.empty());

    Built inflating = at(1.0 + 0.16 + 1.2 + 0.2);
    REQUIRE(inflating.status[0] == EffectStatus::Drawn);
    CHECK(glm::length(glm::vec3(inflating.frame.instances[0].model[0])) < glm::length(glm::vec3(whole.frame.instances[0].model[0])));

    // The droplets fly on the frame the pop starts.
    std::vector<scene::ParticleSystem> particles;
    std::vector<EffectStatus> status(1);
    std::vector<std::string> reasons(1);
    float seen = 0.0f;
    for (double t = 0.9; t < 1.1; t += 1.0 / 60.0) {
        ctx.seconds = t;
        clock.setFrame(t);
        world::buildParticleFrame({&e, 1}, ctx, particles, {}, status, reasons);
        REQUIRE(particles.size() == 1);
        seen += particles[0].burst;
    }
    CHECK(seen == Approx(80.0f));

    // Stateless: a frame straight at 1.08 is the played one, byte for byte.
    world::TriggerClock fresh;
    ctx.triggers = &fresh;
    ctx.seconds = 1.08;
    fresh.setFrame(1.08);
    Built scrubbed = build({e}, ctx);
    CHECK(sameBytes(scrubbed.frame.instances, popping.frame.instances));
}

TEST_CASE("Reality Tear: a BOLT outline that never folds back, a new crack each opening",
          "[shell2][effects][tear][seek]") {
    world::TriggerClock clock;
    world::EffectContext ctx;
    ctx.triggers = &clock;
    world::EffectInstance e = always(EffectKind::RealityTear, "tear", EffectOwner::world());
    e.activation = world::Activation::Trigger;
    e.timing.trigger = repeat(3.0, 0.0);
    e.timing.lifetime = 2.5;
    e.timing.fadeIn = 0.3;
    e.timing.fadeOut = 0.4;
    const auto at = [&](double t) {
        ctx.seconds = t;
        clock.setFrame(t);
        return build({e}, ctx);
    };
    const auto points = [](const Built& b) {
        std::vector<glm::vec2> out;
        const auto first = static_cast<std::size_t>(b.frame.instances[0].params[1].z);
        for (std::size_t k = 0; k < 32; ++k) {
            const float lane = b.frame.extra[first + k / 4][static_cast<glm::length_t>(k % 4)];
            out.push_back(glm::unpackHalf2x16(std::bit_cast<std::uint32_t>(lane)));
        }
        return out;
    };
    Built a = at(1.0);
    Built a2 = at(2.0);
    Built b = at(4.0);
    REQUIRE(a.status[0] == EffectStatus::Drawn);
    REQUIRE(b.status[0] == EffectStatus::Drawn);
    const std::vector<glm::vec2> pa = points(a);
    // Along the crack, every point is further than the last: it advances, never folds back.
    const float length = e.values.getFloat("realityTear/length", 6.0f);
    CHECK(pa.front().y == Approx(-0.5f * length).margin(0.01f));
    CHECK(pa.back().y == Approx(0.5f * length).margin(0.01f));
    for (std::size_t k = 1; k < pa.size(); ++k) {
        CHECK(pa[k].y > pa[k - 1].y);
    }
    CHECK(points(a2) == pa); // one opening, one crack
    CHECK(points(b) != pa);  // the next opening is a new one
    CHECK(a.distortion.count == 6);
    CHECK(at(2.9).frame.empty()); // closed between openings
}

TEST_CASE("Portal: open follows the activation, and the swirl band sits outside the rim",
          "[shell2][effects][portal]") {
    world::EffectContext ctx;
    world::EffectInstance e = always(EffectKind::Portal, "portal", EffectOwner::world());
    e.activation = world::Activation::Window;
    e.timing.windowStart = 2.0;
    e.timing.windowSeconds = 6.0;
    e.timing.fadeIn = 1.2;
    e.timing.fadeOut = 0.8;
    const auto at = [&](double t) {
        ctx.seconds = t;
        return build({e}, ctx);
    };
    CHECK(at(1.0).frame.empty());
    Built opening = at(2.3);
    Built open = at(4.0);
    REQUIRE(opening.frame.instances.size() == 1);
    REQUIRE(open.frame.instances.size() == 1);
    CHECK(opening.frame.instances[0].params[3].x > 0.0f);
    CHECK(opening.frame.instances[0].params[3].x < 0.5f);
    CHECK(open.frame.instances[0].params[3].x == Approx(1.0f));
    REQUIRE(open.distortion.count == 1);
    const world::DistortionProxy& p = open.distortion.proxies[0];
    CHECK(p.axis0.w == Approx(static_cast<float>(world::DistortionShape::Disc)));
    CHECK(p.rim.w == Approx(1.0f / 1.5f).epsilon(1e-4)); // the opening over the band's outer radius
    CHECK(open.lights.count == 1);
    REQUIRE(open.particles.size() == 1);
    CHECK(open.particles[0].spawnRate > 0.0f);
    // Keyed shut, it is Dormant and its particles stop.
    e.values.setFloat("portal/open", 0.0f);
    Built shut = at(4.0);
    CHECK(shut.frame.empty());
    CHECK(shut.status[0] == EffectStatus::Dormant);
}

// ---- play = scrub on a keyed owner -----------------------------------------------------------------

namespace {

void frameAt(app::Engine& engine, long long frame) {
    engine.update(FrameTime{static_cast<double>(frame) / 60.0, frame == 0 ? 0.0 : 1.0 / 60.0,
                            static_cast<std::uint64_t>(frame)});
}

// A craft with no simulation, flown by a keyed position track (motion the Engine's seek replays
// exactly), carrying the four TRIGGER-driven phase-2 types, each on a schedule.
void installChargedCraft(app::Engine& engine) {
    REQUIRE(engine.setCompositionJson(nlohmann::json::parse(R"({ "format": "avgen-scene", "version": 1,
        "name": "flown", "nodes": [ { "kind": "orb", "name": "craft", "position": [0, 5, 0] } ] })"))
                .has_value());
    const EffectOwner craft = EffectOwner::entity("craft");
    std::vector<world::EffectInstance> list;
    world::EffectInstance charge = world::makeEffect(EffectKind::ChargeUp, "Charge");
    charge.owner = craft;
    world::EffectInstance bubble = world::makeEffect(EffectKind::Bubble, "Bubble");
    bubble.owner = craft;
    bubble.activation = world::Activation::Trigger;
    bubble.timing.trigger = repeat(2.3, 0.7);
    world::EffectInstance portal = world::makeEffect(EffectKind::Portal, "Portal");
    portal.owner = craft;
    portal.activation = world::Activation::Trigger;
    portal.timing.trigger = repeat(3.1, 0.2);
    portal.timing.lifetime = 2.6;
    world::EffectInstance tear = world::makeEffect(EffectKind::RealityTear, "Tear");
    tear.owner = craft;
    tear.activation = world::Activation::Trigger;
    tear.timing.trigger = repeat(1.7, 0.4);
    tear.timing.lifetime = 1.4;
    for (world::EffectInstance e : {charge, bubble, portal, tear}) {
        e.id.clear();
        REQUIRE(world::insertEffect(list, std::move(e)).has_value());
    }
    REQUIRE(engine.setEffects(list).has_value());
    for (const world::EffectInstance& e : engine.effects()) {
        engine.addDefaultEffectRoutes(e.id);
    }
    params::Track fly;
    fly.target = "nodes/craft/position";
    fly.keys.push_back(params::Key{.time = 0.0, .value = {0.0f, 5.0f, 0.0f, 0.0f}});
    fly.keys.push_back(params::Key{.time = 4.0, .value = {30.0f, 9.0f, -10.0f, 0.0f}});
    fly.keys.push_back(params::Key{.time = 8.0, .value = {-20.0f, 6.0f, 25.0f, 0.0f}});
    engine.timeline().addTrack(fly);
    REQUIRE(engine.timeline().bind(engine.params()).has_value());
}

} // namespace

TEST_CASE("the TRIGGER-driven phase-2 types on a moving owner are the same played and scrubbed",
          "[shell2][effects][seek][determinism]") {
    // Frames chosen so each type is mid-phase: the charge charging, the portal open, the tear open,
    // the bubble whole or popping.
    for (const long long target : {137LL, 262LL, 331LL}) {
        INFO("frame " << target);
        app::Engine played(app::EngineMode::Offline);
        installChargedCraft(played);
        for (long long f = 0; f <= target + 1; ++f) {
            frameAt(played, f);
        }
        app::Engine scrubbed(app::EngineMode::Offline);
        installChargedCraft(scrubbed);
        frameAt(scrubbed, 0);
        scrubbed.seekSeconds(static_cast<double>(target) / 60.0);
        frameAt(scrubbed, target + 1);

        const scene::Scene& a = played.scene();
        const scene::Scene& b = scrubbed.scene();
        REQUIRE(a.shells.instances.size() >= 3); // the control: several are live
        for (const world::EffectInstance& e : played.effects()) {
            INFO(e.id << ": " << played.effectStatusReason(e.id));
            CHECK(played.effectStatus(e.id) == scrubbed.effectStatus(e.id));
        }
        CHECK(sameBytes(a.shells.instances, b.shells.instances));
        CHECK(sameBytes(a.shells.extra, b.shells.extra));
        REQUIRE(a.distortion.count == b.distortion.count);
        CHECK(std::memcmp(a.distortion.proxies.data(), b.distortion.proxies.data(),
                          a.distortion.count * sizeof(world::DistortionProxy)) == 0);
    }
}
