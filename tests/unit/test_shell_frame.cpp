// SHELL's CPU half (Effect Library Wave 3): the frame builder, its capacity and status honesty, the
// batching the renderer's one-draw-per-kind rests on, LIGHTMOD's shared pool, the depth-prepass
// report, and the three types' producers (Plasma, Energy Shield, Force Field). The pixels are in
// tests/rendering/test_shell_gpu.cpp.

#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_lights.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"
#include "world/effects/effect_timing.hpp"
#include "world/effects/effect_trigger.hpp"
#include "world/effects/shell_frame.hpp"
#include "world/hero.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

// One node, `craft`, drawn with a box from (-2,-1,-2) to (2,1,2) around (0, 6, 0).
class FakeScene final : public world::EffectSceneQuery {
public:
    glm::vec3 centre{0.0f, 6.0f, 0.0f};
    glm::vec3 half{2.0f, 1.0f, 2.0f};
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
};

world::EffectInstance make(world::EffectKind kind, std::string id, world::EffectOwner owner) {
    world::EffectInstance e = world::makeEffect(kind, id);
    e.id = std::move(id);
    e.owner = std::move(owner);
    return e;
}

world::EffectInstance always(world::EffectKind kind, std::string id, world::EffectOwner owner) {
    world::EffectInstance e = make(kind, std::move(id), std::move(owner));
    e.activation = world::Activation::Always;
    e.timing = world::Timing{};
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    return e;
}

struct Built {
    world::ShellFrame frame;
    world::EffectLightFrame lights;
    std::vector<world::EffectStatus> status;
    std::vector<std::string> reasons;
};

Built build(const std::vector<world::EffectInstance>& effects, const world::EffectContext& ctx,
            std::uint32_t lightsAlreadyTaken = 0) {
    Built b;
    b.status.assign(effects.size(), world::EffectStatus::Dormant);
    b.reasons.assign(effects.size(), std::string());
    b.lights.count = lightsAlreadyTaken;
    world::buildShellFrame(effects, ctx, b.frame, b.lights, {}, b.status, b.reasons);
    return b;
}

// Holds the renderer's depth report at `state` for one scope and puts back "unknown" after, so no
// case leaks it into another.
struct DepthReport {
    explicit DepthReport(bool available) { world::reportShellLinearDepth(available); }
    DepthReport(const DepthReport&) = delete;
    DepthReport& operator=(const DepthReport&) = delete;
    ~DepthReport() { world::reportShellLinearDepth(true); }
};

} // namespace

TEST_CASE("the shell gate: no shell type, an empty frame and no storage", "[shell][effects][gate]") {
    FakeScene scene;
    world::EffectContext ctx;
    ctx.seconds = 3.0;
    ctx.scene = &scene;
    // A Glow is not a shell: the builder walks past it and reserves nothing.
    const std::vector<world::EffectInstance> effects{
        always(world::EffectKind::Glow, "glow", world::EffectOwner::entity("craft"))};
    Built b = build(effects, ctx);
    CHECK(b.frame.empty());
    CHECK(b.frame.batches.empty());
    CHECK(b.frame.instances.capacity() == 0);
    CHECK(b.lights.count == 0);
    CHECK(b.status[0] == world::EffectStatus::Dormant); // untouched: not this builder's

    // The same with the shell switched off: Disabled, and still no record.
    std::vector<world::EffectInstance> off{always(world::EffectKind::EnergyShield, "shield", world::EffectOwner::entity("craft"))};
    off[0].enabled = false;
    Built o = build(off, ctx);
    CHECK(o.frame.empty());
    CHECK(o.status[0] == world::EffectStatus::Disabled);
}

TEST_CASE("the shell budget: the 129th shell is Dropped and says why", "[shell][effects][capacity]") {
    world::EffectContext ctx;
    ctx.seconds = 1.0;
    std::vector<world::EffectInstance> effects;
    for (std::uint32_t i = 0; i < world::kMaxShells + 3; ++i) {
        world::EffectInstance e = always(world::EffectKind::ForceField, "wall-" + std::to_string(i), world::EffectOwner::world());
        e.values.setFloat("forceField/positionX", static_cast<float>(i) * 10.0f);
        effects.push_back(e);
    }
    Built b = build(effects, ctx);
    CHECK(b.frame.instances.size() == world::kMaxShells);
    CHECK(b.frame.dropped == 3);
    std::size_t drawn = 0;
    for (std::size_t i = 0; i < effects.size(); ++i) {
        if (i < world::kMaxShells) {
            drawn += b.status[i] == world::EffectStatus::Drawn ? 1u : 0u;
        } else {
            INFO("instance " << i << ": " << b.reasons[i]);
            CHECK(b.status[i] == world::EffectStatus::Dropped);
            CHECK(b.reasons[i].find("128") != std::string::npos);
        }
    }
    CHECK(drawn == world::kMaxShells);
}

TEST_CASE("shells are batched by shading kind and mesh, stack order kept within each",
          "[shell][effects]") {
    FakeScene scene;
    world::EffectContext ctx;
    ctx.seconds = 2.0;
    ctx.scene = &scene;
    // Interleaved on purpose: plasma, wall, shield, plasma, box, wall.
    std::vector<world::EffectInstance> effects{
        always(world::EffectKind::Plasma, "p0", world::EffectOwner::world()),
        always(world::EffectKind::ForceField, "w0", world::EffectOwner::world()),
        always(world::EffectKind::EnergyShield, "s0", world::EffectOwner::entity("craft")),
        always(world::EffectKind::Plasma, "p1", world::EffectOwner::world()),
        always(world::EffectKind::ForceField, "b0", world::EffectOwner::world()),
        always(world::EffectKind::ForceField, "w1", world::EffectOwner::world()),
    };
    effects[3].values.setFloat("plasma/offsetX", 7.0f);
    effects[4].values.setFloat("forceField/shape", 1.0f); // Box
    effects[5].values.setFloat("forceField/positionX", 30.0f);
    Built b = build(effects, ctx);
    REQUIRE(b.frame.instances.size() == 6);
    for (const world::EffectStatus s : b.status) {
        CHECK(s == world::EffectStatus::Drawn);
    }
    // Plasma/Sphere, Shield/Sphere, Barrier/Box, Barrier/Quad -- in that order, contiguous.
    REQUIRE(b.frame.batches.size() == 4);
    CHECK(b.frame.batches[0].shading == world::ShellShading::Plasma);
    CHECK(b.frame.batches[0].count == 2);
    CHECK(b.frame.batches[1].shading == world::ShellShading::Shield);
    CHECK(b.frame.batches[2].shading == world::ShellShading::Barrier);
    CHECK(b.frame.batches[2].mesh == world::ShellMesh::Box);
    CHECK(b.frame.batches[3].mesh == world::ShellMesh::Quad);
    CHECK(b.frame.batches[3].count == 2);
    std::uint32_t next = 0;
    for (const world::ShellBatch& batch : b.frame.batches) {
        CHECK(batch.first == next);
        next += batch.count;
    }
    CHECK(next == 6);
    // Stack order within a batch: p0 (origin) before p1 (x = 7); w0 (x = 0) before w1 (x = 30).
    CHECK(b.frame.instances[0].model[3].x == Approx(0.0f));
    CHECK(b.frame.instances[1].model[3].x == Approx(7.0f));
    CHECK(b.frame.instances[4].model[3].x == Approx(0.0f));
    CHECK(b.frame.instances[5].model[3].x == Approx(30.0f));
}

TEST_CASE("Energy Shield: fitted round its owner, hits from its trigger, stateless under seek",
          "[shell][effects][shield][seek]") {
    FakeScene scene;
    world::TriggerClock clock;
    world::EffectContext ctx;
    ctx.scene = &scene;
    ctx.triggers = &clock;
    world::EffectInstance shield = make(world::EffectKind::EnergyShield, "shield", world::EffectOwner::entity("craft"));
    // The plain type fires on a 1.6 s schedule, so it needs no audio.
    REQUIRE(shield.activation == world::Activation::Trigger);
    REQUIRE(shield.timing.trigger.source == world::TriggerSource::Repeat);
    const std::vector<world::EffectInstance> effects{shield};

    const auto at = [&](double t) {
        ctx.seconds = t;
        clock.setFrame(t);
        return build(effects, ctx);
    };
    const double t = 5.0; // events at 0, 1.6, 3.2, 4.8
    Built played = at(0.0);
    for (double s = 0.0; s <= t + 1e-9; s += 1.0 / 60.0) {
        played = at(s);
    }
    played = at(t);
    const Built scrubbed = at(t); // straight to t, nothing before
    REQUIRE(played.status[0] == world::EffectStatus::Drawn);
    REQUIRE(played.frame.instances.size() == 1);
    const world::ShellInstance& shell = played.frame.instances[0];

    // Fitted: a sphere through the rim of the owner's widest circle in plan (2 m) and its top (1 m),
    // plus the 20 % padding.
    CHECK(shell.model[3].y == Approx(6.0f));
    CHECK(glm::length(glm::vec3(shell.model[0])) == Approx(std::sqrt(5.0f) * 1.2f).epsilon(1e-4));

    // Hits: every event still above 1 % of its brightness (decay 1.4/s: 4.6 / 1.4 = 3.3 s back).
    const auto count = static_cast<std::size_t>(shell.params[1].w);
    const auto first = static_cast<std::size_t>(shell.params[1].z);
    REQUIRE(count == 2); // 4.8 (0.2 s old) and 3.2 (1.8 s); 1.6 is 3.4 s old and gone
    CHECK(played.frame.extra[first].w == Approx(0.2f).margin(1e-4));
    CHECK(played.frame.extra[first + 1].w == Approx(1.8f).margin(1e-4));
    CHECK(glm::length(glm::vec3(played.frame.extra[first])) == Approx(1.0f).margin(1e-5));
    // Two hits land in two different places.
    CHECK(glm::length(glm::vec3(played.frame.extra[first]) - glm::vec3(played.frame.extra[first + 1])) > 0.05f);

    // Play = scrub, to the byte: the frame is a function of the second, not of the frames before it.
    REQUIRE(scrubbed.frame.instances.size() == 1);
    CHECK(std::memcmp(&scrubbed.frame.instances[0], &shell, sizeof(world::ShellInstance)) == 0);
    REQUIRE(scrubbed.frame.extra.size() == played.frame.extra.size());
    CHECK(std::memcmp(scrubbed.frame.extra.data(), played.frame.extra.data(),
                      played.frame.extra.size() * sizeof(glm::vec4)) == 0);

    SECTION("a hit's direction is its event's, the same on every run") {
        const glm::vec3 a = world::shellHitDirection(311.0f, 4.8);
        CHECK(a == world::shellHitDirection(311.0f, 4.8));
        CHECK(a != world::shellHitDirection(311.0f, 3.2));
        CHECK(a != world::shellHitDirection(312.0f, 4.8));
    }
    SECTION("before its first event it is Dormant, with nothing drawn") {
        world::EffectInstance late = shield;
        late.timing.trigger.phase = 10.0;
        const Built b = build({late}, ctx);
        CHECK(b.status[0] == world::EffectStatus::Dormant);
        CHECK(b.frame.empty());
    }
}

TEST_CASE("Plasma asks LIGHTMOD's pool for its core light and shares it with the lanes",
          "[shell][effects][plasma][lightmod]") {
    world::EffectContext ctx;
    ctx.seconds = 1.0;
    world::EffectInstance orb = always(world::EffectKind::Plasma, "orb", world::EffectOwner::world());
    orb.values.setFloat("plasma/offsetY", 4.0f);
    orb.values.setFloat("plasma/light", 90.0f);

    SECTION("room in the pool: the light is appended after the lanes' spills") {
        const Built b = build({orb}, ctx, 5);
        CHECK(b.status[0] == world::EffectStatus::Drawn);
        REQUIRE(b.lights.count == 6);
        CHECK(b.lights.lights[5].position.y == Approx(4.0f));
        CHECK(b.lights.lights[5].intensity == Approx(90.0f));
    }
    SECTION("a full pool: the orb still draws, Partial, and says why") {
        const Built b = build({orb}, ctx, static_cast<std::uint32_t>(world::kEffectLightBudget));
        CHECK(b.frame.instances.size() == 1);
        CHECK(b.status[0] == world::EffectStatus::Partial);
        CHECK(b.reasons[0].find("light") != std::string::npos);
        CHECK(b.lights.count == world::kEffectLightBudget);
    }
    SECTION("light 0 asks for nothing") {
        world::EffectInstance dark = orb;
        dark.values.setFloat("plasma/light", 0.0f);
        const Built b = build({dark}, ctx, 3);
        CHECK(b.lights.count == 3);
        CHECK(b.status[0] == world::EffectStatus::Drawn);
    }
}

TEST_CASE("without the depth prepass, a shell that reads depth is Partial and says why",
          "[shell][effects][depth]") {
    FakeScene scene;
    world::TriggerClock clock;
    world::EffectContext ctx;
    ctx.seconds = 2.0;
    ctx.scene = &scene;
    ctx.triggers = &clock;
    clock.setFrame(ctx.seconds);
    const world::EffectInstance shield = make(world::EffectKind::EnergyShield, "shield", world::EffectOwner::entity("craft"));
    world::EffectInstance noLine = shield;
    noLine.values.setFloat("energyShield/intersectWidth", 0.0f);
    {
        const DepthReport missing(false);
        const Built b = build({shield, noLine}, ctx);
        CHECK(b.status[0] == world::EffectStatus::Partial);
        CHECK(b.reasons[0].find("depth prepass") != std::string::npos);
        // A shell with no depth term has nothing to lose: it is simply Drawn.
        CHECK(b.status[1] == world::EffectStatus::Drawn);
        CHECK(b.frame.instances.size() == 2);
    }
    const DepthReport available(true);
    const Built b = build({shield}, ctx);
    CHECK(b.status[0] == world::EffectStatus::Drawn);
    CHECK(b.reasons[0].empty());
}

TEST_CASE("Force Field: placed on its base, revealed by the camera and the nearest heroes",
          "[shell][effects][forcefield]") {
    std::vector<world::HeroPoint> heroes(4);
    heroes[0].name = "far";
    heroes[0].position = glm::vec3(100.0f, 0.0f, 0.0f);
    heroes[1].name = "near";
    heroes[1].position = glm::vec3(2.0f, 0.0f, 1.0f);
    heroes[2].name = "middle";
    heroes[2].position = glm::vec3(10.0f, 0.0f, 0.0f);
    heroes[3].name = "nearer";
    heroes[3].position = glm::vec3(1.0f, 0.0f, 0.0f);
    world::EffectContext ctx;
    ctx.seconds = 1.0;
    ctx.cameraPosition = glm::vec3(0.0f, 2.0f, 20.0f);
    ctx.heroes = heroes;
    world::EffectInstance wall = always(world::EffectKind::ForceField, "wall", world::EffectOwner::world());
    wall.values.setFloat("forceField/revealHeroes", 2.0f);
    const Built b = build({wall}, ctx);
    REQUIRE(b.status[0] == world::EffectStatus::Drawn);
    const world::ShellInstance& s = b.frame.instances[0];
    CHECK(b.frame.batches[0].mesh == world::ShellMesh::Quad);
    // 8 x 4 m wall standing on the origin: centred 2 m up, half-extents 4 and 2.
    CHECK(s.model[3].y == Approx(2.0f));
    CHECK(glm::length(glm::vec3(s.model[0])) == Approx(4.0f));
    CHECK(glm::length(glm::vec3(s.model[1])) == Approx(2.0f));
    // The camera, then the two heroes nearest the wall's centre, nearest first.
    REQUIRE(static_cast<int>(s.params[1].w) == 3);
    const auto first = static_cast<std::size_t>(s.params[1].z);
    CHECK(glm::vec3(b.frame.extra[first]) == ctx.cameraPosition);
    CHECK(glm::vec3(b.frame.extra[first + 1]) == heroes[3].position);
    CHECK(glm::vec3(b.frame.extra[first + 2]) == heroes[1].position);
}
