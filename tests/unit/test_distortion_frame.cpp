// DF's CPU half (Effect Library Wave 1, roadmap 1.7): the proxy builder, its capacity and status
// honesty, and Space Warp's producer. The pixels are in tests/rendering/test_distortion_gpu.cpp.
//
// Everything here is a question the frame block can answer without a device: where a proxy is, how
// big, which way it is stretched, what it excludes, whether it exists at all this second, and what
// the 65th live warp is told.

#include "world/effects/distortion_frame.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>

#include <cstring>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

// A scene with one node, `ufo`, drawn with a box from (-3,-1,-3) to (3,1,3) around (10, 5, -20).
class FakeScene final : public world::EffectSceneQuery {
public:
    glm::vec3 centre{10.0f, 5.0f, -20.0f};
    glm::vec3 half{3.0f, 1.0f, 3.0f};
    bool hasVelocity = false;
    glm::vec3 velocity{0.0f};

    [[nodiscard]] bool nodePosition(std::string_view name, glm::vec3& out) const override {
        if (name != "ufo") {
            return false;
        }
        out = centre;
        return true;
    }
    [[nodiscard]] bool nodeView(std::string_view name, world::NodeView& out) const override {
        if (name != "ufo") {
            return false;
        }
        out = world::NodeView{};
        out.world = glm::mat4(1.0f);
        out.world[3] = glm::vec4(centre, 1.0f);
        out.boundsMin = centre - half;
        out.boundsMax = centre + half;
        out.hasBounds = true;
        out.firstEntity = 3;
        out.entityCount = 2;
        return true;
    }
    [[nodiscard]] bool nodeVelocity(std::string_view name, glm::vec3& out) const override {
        if (name != "ufo" || !hasVelocity) {
            return false;
        }
        out = velocity;
        return true;
    }
};

void setStored(world::EffectInstance& e, const char* leaf, float v) {
    e.values.setFloat(std::string("spaceWarp/") + leaf, v);
}

world::EffectInstance liveWarp(std::string id) {
    world::EffectInstance e = world::makeEffect(world::EffectKind::SpaceWarp, id);
    e.id = std::move(id);
    e.activation = world::Activation::Always;
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    return e;
}

world::EffectContext contextAt(double seconds, const world::EffectSceneQuery* scene) {
    world::EffectContext ctx;
    ctx.seconds = seconds;
    ctx.scene = scene;
    return ctx;
}

// Builds with the evaluator's own order, as the engine does.
struct Built {
    world::DistortionFrame frame;
    std::vector<world::EffectStatus> status;
    std::vector<std::string> reasons;
};
Built build(const std::vector<world::EffectInstance>& effects, const world::EffectContext& ctx) {
    Built b;
    std::vector<std::uint32_t> order;
    world::effectEvaluationOrder(effects, order);
    b.status.assign(effects.size(), world::EffectStatus::Dormant);
    b.reasons.assign(effects.size(), std::string());
    world::buildDistortionFrame(effects, ctx, b.frame, order, b.status, b.reasons);
    return b;
}

} // namespace

TEST_CASE("every Distortion-bucket type has a DF producer, and every producer is a Distortion type",
          "[effects][distortion][registry]") {
    // The builder finds a type's proxies through `distortionProducers()`, not through the schema, so
    // this is the guard that a new distortion type cannot register, show in the menu, and silently
    // never draw because nobody added its row.
    for (const world::EffectSchema* s : world::effectSchemas()) {
        const bool distortion = s->resolve.bucket == world::EffectBucket::Distortion;
        const bool producer = world::distortionProducer(s->kind) != nullptr;
        INFO("kind " << s->key);
        CHECK(distortion == producer);
        if (distortion) {
            CHECK(s->resolve.records != nullptr);
            CHECK(s->stage == world::RenderStage::ScreenSpace);
        }
    }
    REQUIRE(world::distortionProducer(world::EffectKind::SpaceWarp) != nullptr);
}

TEST_CASE("a World-owned Space Warp is one proxy at its offset, sized by its radius",
          "[effects][distortion]") {
    world::EffectInstance e = liveWarp("well");
    setStored(e, "offsetX", 4.0f);
    setStored(e, "offsetY", 2.0f);
    setStored(e, "offsetZ", -30.0f);
    setStored(e, "radius", 12.0f);
    const Built b = build({e}, contextAt(1.0, nullptr));
    REQUIRE(b.frame.count == 1);
    CHECK(b.status[0] == world::EffectStatus::Drawn);
    const world::DistortionProxy& p = b.frame.proxies[0];
    CHECK(glm::vec3(p.centre) == glm::vec3(4.0f, 2.0f, -30.0f));
    CHECK(p.centre.w == 0.0f); // nothing to exclude: a warp at a point
    CHECK(glm::length(glm::vec3(p.axis1)) == Approx(12.0f));
    CHECK(glm::length(glm::vec3(p.axis2)) == Approx(12.0f));
    CHECK(glm::length(glm::vec3(p.axis0)) == Approx(12.0f)); // no velocity, no stretch
    CHECK(p.rim.w == 0.0f);
    CHECK(p.terms.w > 0.0f);
    CHECK(static_cast<int>(p.axis0.w) == static_cast<int>(world::DistortionShape::Ellipsoid));
    // Unused slots are zero, so the uploaded block is a function of the live proxies alone.
    const world::DistortionProxy zero{};
    CHECK(std::memcmp(&b.frame.proxies[1], &zero, sizeof(world::DistortionProxy)) == 0);
}

TEST_CASE("an Entity-owned Space Warp fits its owner's drawn bounds and excludes them",
          "[effects][distortion]") {
    FakeScene scene;
    world::EffectInstance e = liveWarp("ufo-warp");
    e.owner = world::EffectOwner::entity("ufo");
    setStored(e, "boundsScale", 2.5f);
    const float ownerRadius = glm::length(scene.half); // half the box diagonal

    SECTION("at rest: round, centred on the bounds, the owner excluded") {
        const Built b = build({e}, contextAt(2.0, &scene));
        REQUIRE(b.frame.count == 1);
        const world::DistortionProxy& p = b.frame.proxies[0];
        CHECK(glm::vec3(p.centre) == scene.centre);
        CHECK(p.centre.w == Approx(ownerRadius));
        CHECK(glm::length(glm::vec3(p.axis0)) == Approx(ownerRadius * 2.5f));
        CHECK(glm::length(glm::vec3(p.axis1)) == Approx(ownerRadius * 2.5f));
        CHECK(p.rim.w == Approx(1.0f / 2.5f)); // the field starts at the owner's edge
        CHECK(p.motion.w == 0.0f);
    }
    SECTION("moving: stretched along its velocity, the bow wave weighted in") {
        scene.hasVelocity = true;
        scene.velocity = glm::vec3(0.0f, 0.0f, -40.0f); // well past speedForFull
        setStored(e, "velocityStretch", 1.0f);
        const Built b = build({e}, contextAt(2.0, &scene));
        REQUIRE(b.frame.count == 1);
        const world::DistortionProxy& p = b.frame.proxies[0];
        CHECK(glm::length(glm::vec3(p.axis0)) == Approx(ownerRadius * 2.5f * 2.0f));
        CHECK(glm::length(glm::vec3(p.axis1)) == Approx(ownerRadius * 2.5f));
        CHECK(glm::dot(glm::normalize(glm::vec3(p.axis0)), glm::vec3(0.0f, 0.0f, -1.0f)) == Approx(1.0f));
        CHECK(p.motion.w == Approx(1.0f));
        // Orthogonal semi-axes: the shader inverts the frame by dividing by squared lengths.
        CHECK(std::abs(glm::dot(glm::vec3(p.axis0), glm::vec3(p.axis1))) < 1e-3f);
        CHECK(std::abs(glm::dot(glm::vec3(p.axis1), glm::vec3(p.axis2))) < 1e-3f);
        CHECK(std::abs(glm::dot(glm::vec3(p.axis0), glm::vec3(p.axis2))) < 1e-3f);
    }
    SECTION("velocity the scene cannot answer is zero velocity, not no warp") {
        scene.hasVelocity = false;
        const Built b = build({e}, contextAt(2.0, &scene));
        CHECK(b.frame.count == 1);
        CHECK(b.frame.proxies[0].motion.w == 0.0f);
    }
    SECTION("an owner that is not drawn contributes nothing, and says Dormant") {
        e.owner = world::EffectOwner::entity("nobody");
        const Built b = build({e}, contextAt(2.0, &scene));
        CHECK(b.frame.count == 0);
        CHECK(b.status[0] == world::EffectStatus::Dormant);
    }
}

TEST_CASE("Space Warp honours activation and timing", "[effects][distortion][timing]") {
    world::EffectInstance e = liveWarp("timed");
    SECTION("disabled") {
        e.enabled = false;
        const Built b = build({e}, contextAt(1.0, nullptr));
        CHECK(b.frame.count == 0);
        CHECK(b.status[0] == world::EffectStatus::Disabled);
        CHECK(world::distortionRecords(e, contextAt(1.0, nullptr)) == 0);
    }
    SECTION("outside its window") {
        e.activation = world::Activation::Window;
        e.timing.windowStart = 5.0;
        e.timing.windowSeconds = 2.0;
        CHECK(build({e}, contextAt(4.0, nullptr)).status[0] == world::EffectStatus::Dormant);
        CHECK(build({e}, contextAt(6.0, nullptr)).status[0] == world::EffectStatus::Drawn);
        CHECK(build({e}, contextAt(7.5, nullptr)).status[0] == world::EffectStatus::Dormant);
    }
    SECTION("the envelope scales the displacement and the rim, not the size") {
        e.timing.fadeIn = 2.0;
        const Built half = build({e}, contextAt(1.0, nullptr));
        const Built full = build({e}, contextAt(3.0, nullptr));
        REQUIRE(half.frame.count == 1);
        REQUIRE(full.frame.count == 1);
        CHECK(half.frame.proxies[0].terms.w < full.frame.proxies[0].terms.w);
        CHECK(half.frame.proxies[0].terms.w > 0.0f);
        CHECK(glm::length(glm::vec3(half.frame.proxies[0].rim)) < glm::length(glm::vec3(full.frame.proxies[0].rim)));
        CHECK(half.frame.proxies[0].axis0 == full.frame.proxies[0].axis0);
    }
}

TEST_CASE("the records hook answers through the builder's own path", "[effects][distortion][conformance]") {
    world::EffectInstance e = liveWarp("probe");
    const world::EffectContext ctx = contextAt(1.0, nullptr);
    const world::EffectSchema* schema = world::effectSchema(world::EffectKind::SpaceWarp);
    REQUIRE(schema != nullptr);
    REQUIRE(schema->resolve.records != nullptr);
    CHECK(schema->resolve.records(e, ctx) == 1);
    CHECK(schema->resolve.records(e, ctx) == build({e}, ctx).frame.count);
    setStored(e, "radius", 0.1f); // the smallest legal field still draws
    CHECK(schema->resolve.records(e, ctx) == 1);
}

TEST_CASE("the 65th live distortion proxy is Dropped, with a reason, lowest priority first",
          "[effects][distortion][capacity]") {
    std::vector<world::EffectInstance> effects;
    for (int i = 0; i < 65; ++i) {
        world::EffectInstance e = liveWarp("warp-" + std::to_string(i));
        e.order = i;
        setStored(e, "offsetX", static_cast<float>(i));
        effects.push_back(e);
    }
    const Built b = build(effects, contextAt(1.0, nullptr));
    CHECK(b.frame.count == world::kMaxDistortionProxies);
    CHECK(b.frame.dropped == 1);
    std::size_t drawn = 0;
    std::size_t dropped = 0;
    std::size_t droppedAt = effects.size();
    for (std::size_t i = 0; i < effects.size(); ++i) {
        drawn += b.status[i] == world::EffectStatus::Drawn ? 1u : 0u;
        if (b.status[i] == world::EffectStatus::Dropped) {
            ++dropped;
            droppedAt = i;
        }
    }
    CHECK(drawn == 64);
    REQUIRE(dropped == 1);
    // The evaluation order is stage, priority, then stack position: the last in the stack goes.
    CHECK(droppedAt == 64);
    INFO("reason: " << b.reasons[droppedAt]);
    CHECK(b.reasons[droppedAt].find("64") != std::string::npos);
    // And the 64 that drew are the first 64, in order.
    CHECK(b.frame.proxies[63].centre.x == Approx(63.0f));

    // Disabling one of the 64 makes room: the 65th draws.
    effects[10].enabled = false;
    const Built again = build(effects, contextAt(1.0, nullptr));
    CHECK(again.frame.dropped == 0);
    CHECK(again.status[64] == world::EffectStatus::Drawn);
    CHECK(again.status[10] == world::EffectStatus::Disabled);
}

TEST_CASE("a distortion frame is a pure function of the transport second", "[effects][distortion][determinism]") {
    FakeScene scene;
    scene.hasVelocity = true;
    scene.velocity = glm::vec3(6.0f, 0.0f, 0.0f);
    world::EffectInstance e = liveWarp("det");
    e.owner = world::EffectOwner::entity("ufo");
    const Built a = build({e}, contextAt(12.25, &scene));
    const Built b = build({e}, contextAt(12.25, &scene));
    REQUIRE(a.frame.count == 1);
    CHECK(std::memcmp(&a.frame.proxies[0], &b.frame.proxies[0], sizeof(world::DistortionProxy)) == 0);
    // The turbulence moves with time and nothing else.
    const Built later = build({e}, contextAt(13.25, &scene));
    CHECK(later.frame.proxies[0].noise.x != a.frame.proxies[0].noise.x);
    CHECK(later.frame.proxies[0].centre == a.frame.proxies[0].centre);
}

TEST_CASE("every Space Warp style is a complete look that names itself", "[effects][distortion][styles]") {
    const world::EffectSchema* schema = world::effectSchema(world::EffectKind::SpaceWarp);
    REQUIRE(schema != nullptr);
    REQUIRE(schema->styles.size() == 4);
    for (const world::EffectStyle& style : schema->styles) {
        world::EffectInstance e = liveWarp("styled");
        // Start from a scribbled-on instance: a style must not depend on what was there before.
        for (const world::EffectField& f : schema->fields) {
            if (f.type == world::FieldType::Float) {
                world::setFieldFloat(f, *schema, e, f.hardMin);
            }
        }
        style.apply(e);
        CHECK(e.style == style.name);
        world::EffectInstance fresh = liveWarp("styled");
        style.apply(fresh);
        for (const char* leaf : {"strength", "radialWeight", "bowWeight", "swirl", "chroma", "rimIntensity"}) {
            INFO(style.name << " " << leaf);
            CHECK(e.values.getFloat(std::string("spaceWarp/") + leaf, -1.0f) ==
                  fresh.values.getFloat(std::string("spaceWarp/") + leaf, -2.0f));
        }
    }
}
