// Effect Library Wave 3, the lens slice: Heat Shimmer and Gravitational Lens on DF, as the frame
// block sees them. The pixels are in tests/rendering/test_lens_gpu.cpp.

#include "world/effects/distortion_frame.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"

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

// One node, `fire`, drawn with a box from (-1,0,-1) to (1,2,1) around (4, 1, -10), facing +X.
class FakeScene final : public world::EffectSceneQuery {
public:
    glm::vec3 centre{4.0f, 1.0f, -10.0f};
    glm::vec3 half{1.0f, 1.0f, 1.0f};
    glm::vec3 forward{1.0f, 0.0f, 0.0f};

    [[nodiscard]] bool nodePosition(std::string_view name, glm::vec3& out) const override {
        if (name != "fire") {
            return false;
        }
        out = centre;
        return true;
    }
    [[nodiscard]] bool nodeForward(std::string_view name, glm::vec3& out) const override {
        if (name != "fire") {
            return false;
        }
        out = forward;
        return true;
    }
    [[nodiscard]] bool nodeView(std::string_view name, world::NodeView& out) const override {
        if (name != "fire") {
            return false;
        }
        out = world::NodeView{};
        out.world = glm::mat4(1.0f);
        out.world[3] = glm::vec4(centre, 1.0f);
        out.boundsMin = centre - half;
        out.boundsMax = centre + half;
        out.hasBounds = true;
        out.entityCount = 1;
        return true;
    }
};

world::EffectInstance live(world::EffectKind kind, std::string id) {
    world::EffectInstance e = world::makeEffect(kind, id);
    e.id = std::move(id);
    e.activation = world::Activation::Always;
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    return e;
}

void set(world::EffectInstance& e, const char* key, const char* leaf, float v) {
    e.values.setFloat(std::string(key) + "/" + leaf, v);
}

world::DistortionFrame frameOf(const std::vector<world::EffectInstance>& effects, double seconds,
                               const world::EffectSceneQuery* scene = nullptr,
                               std::vector<world::EffectStatus>* statusOut = nullptr) {
    world::EffectContext ctx;
    ctx.seconds = seconds;
    ctx.scene = scene;
    std::vector<std::uint32_t> order;
    world::effectEvaluationOrder(effects, order);
    std::vector<world::EffectStatus> status(effects.size(), world::EffectStatus::Dormant);
    std::vector<std::string> reasons(effects.size());
    world::DistortionFrame frame;
    world::buildDistortionFrame(effects, ctx, frame, order, status, reasons);
    if (statusOut != nullptr) {
        *statusOut = status;
    }
    return frame;
}

int shapeOf(const world::DistortionProxy& p) { return static_cast<int>(p.axis0.w + 0.5f); }
int fieldOf(const world::DistortionProxy& p) { return static_cast<int>(p.axis1.w + 0.5f); }

} // namespace

// ---- Heat Shimmer ------------------------------------------------------------------------------------

TEST_CASE("Heat Shimmer on the World is one cylinder standing on its offset", "[effects][distortion][shimmer]") {
    world::EffectInstance e = live(world::EffectKind::HeatShimmer, "heat");
    set(e, "heatShimmer", "offsetX", 2.0f);
    set(e, "heatShimmer", "offsetY", -1.0f);
    set(e, "heatShimmer", "offsetZ", -12.0f);
    set(e, "heatShimmer", "radius", 1.5f);
    set(e, "heatShimmer", "height", 6.0f);
    set(e, "heatShimmer", "strength", 0.05f);
    std::vector<world::EffectStatus> status;
    const world::DistortionFrame f = frameOf({e}, 3.0, nullptr, &status);
    REQUIRE(f.count == 1);
    CHECK(status[0] == world::EffectStatus::Drawn);
    const world::DistortionProxy& p = f.proxies[0];
    CHECK(shapeOf(p) == static_cast<int>(world::DistortionShape::Cylinder));
    CHECK(fieldOf(p) == static_cast<int>(world::DistortionField::Shimmer));
    // The base at the offset, so the centre is half the height above it, and the axis is up.
    CHECK(p.centre.x == Approx(2.0f));
    CHECK(p.centre.y == Approx(2.0f));
    CHECK(p.centre.z == Approx(-12.0f));
    CHECK(glm::vec3(p.axis1).y == Approx(3.0f));
    CHECK(glm::length(glm::vec3(p.axis0)) == Approx(1.5f));
    CHECK(glm::length(glm::vec3(p.axis2)) == Approx(1.5f));
    CHECK(p.centre.w == 0.0f); // no exclusion radius: the lens plane is found per ray
    CHECK(p.terms.w == Approx(0.05f));
    // The rect bound's contract: every term but the weighted peak is zero.
    CHECK(p.terms.y == 0.0f);
    CHECK(p.terms.z == 0.0f);
    CHECK(p.shape.z == 0.0f);
    CHECK(p.motion.w == 0.0f);
    // It emits nothing: the rim lanes carry parameters, and the shader's glow for the field is 0.
    CHECK(world::distortionHullScale(p.axis0.w) == Approx(1.41422f));
}

TEST_CASE("Heat Shimmer on an entity rises from the bottom of its bounds, or streams out behind it",
          "[effects][distortion][shimmer]") {
    FakeScene scene;
    world::EffectInstance e = live(world::EffectKind::HeatShimmer, "heat");
    e.owner = world::EffectOwner::entity("fire");
    set(e, "heatShimmer", "height", 4.0f);
    {
        const world::DistortionFrame f = frameOf({e}, 1.0, &scene);
        REQUIRE(f.count == 1);
        // Bounds bottom is y = 0: the centre is 2 m up, straight above the owner's centre.
        CHECK(f.proxies[0].centre.x == Approx(4.0f));
        CHECK(f.proxies[0].centre.y == Approx(2.0f));
        CHECK(f.proxies[0].centre.z == Approx(-10.0f));
        CHECK(glm::normalize(glm::vec3(f.proxies[0].axis1)).y == Approx(1.0f));
    }
    set(e, "heatShimmer", "rise", 1.0f); // behind owner
    {
        const world::DistortionFrame f = frameOf({e}, 1.0, &scene);
        REQUIRE(f.count == 1);
        const glm::vec3 axis = glm::normalize(glm::vec3(f.proxies[0].axis1));
        CHECK(axis.x == Approx(-1.0f)); // the owner faces +X, so the exhaust streams to -X
        CHECK(f.proxies[0].centre.x < scene.centre.x - 1.0f);
    }
    // An owner the scene cannot show draws nothing (and is not an error).
    e.owner = world::EffectOwner::entity("nobody");
    CHECK(frameOf({e}, 1.0, &scene).count == 0);
}

TEST_CASE("Heat Shimmer's two flow layers crossfade at constant energy and change seed only when unseen",
          "[effects][distortion][shimmer][determinism]") {
    world::EffectInstance e = live(world::EffectKind::HeatShimmer, "heat");
    const double dt = 1.0 / 240.0;
    std::size_t seedChanges = 0;
    world::DistortionProxy prev = frameOf({e}, 0.0).proxies[0];
    for (int i = 1; i < 240 * 20; ++i) {
        const double t = i * dt;
        const world::DistortionProxy p = frameOf({e}, t).proxies[0];
        const float wa = std::sin(3.14159265f * p.noise.x);
        const float wb = std::sin(3.14159265f * p.noise.z);
        INFO("t " << t);
        REQUIRE(wa * wa + wb * wb == Approx(1.0f).margin(1e-4));
        // A layer's seed may change only across the instant its weight is zero.
        if (p.noise.w != prev.noise.w) {
            ++seedChanges;
            CHECK(std::sin(3.14159265f * prev.noise.x) < 0.02f + 1e-3f);
            CHECK(wa < 0.02f);
        }
        if (p.rim.z != prev.rim.z) {
            ++seedChanges;
            CHECK(std::sin(3.14159265f * prev.noise.z) < 0.02f);
            CHECK(wb < 0.02f);
        }
        prev = p;
    }
    CHECK(seedChanges >= 8); // 20 s over two layers of a 4 s cycle: ten rebirths
    // A pure function of the transport second: the same second built twice, and after other seconds.
    const world::DistortionFrame a = frameOf({e}, 7.25);
    frameOf({e}, 100.0);
    const world::DistortionFrame b = frameOf({e}, 7.25);
    CHECK(std::memcmp(&a.proxies[0], &b.proxies[0], sizeof(world::DistortionProxy)) == 0);
}

TEST_CASE("Heat Shimmer and Gravitational Lens honour enabled, activation and timing",
          "[effects][distortion][shimmer][lens][timing]") {
    for (const world::EffectKind kind : {world::EffectKind::HeatShimmer, world::EffectKind::GravitationalLens}) {
        world::EffectInstance e = live(kind, "x");
        INFO("kind " << world::effectKindName(kind));
        std::vector<world::EffectStatus> status;
        CHECK(frameOf({e}, 2.0, nullptr, &status).count == 1);
        CHECK(status[0] == world::EffectStatus::Drawn);
        e.enabled = false;
        CHECK(frameOf({e}, 2.0, nullptr, &status).count == 0);
        CHECK(status[0] == world::EffectStatus::Disabled);
        e.enabled = true;
        e.timing.fadeIn = 2.0;
        const world::DistortionFrame half = frameOf({e}, 1.0);
        const world::DistortionFrame full = frameOf({e}, 5.0);
        REQUIRE(half.count == 1);
        REQUIRE(full.count == 1);
        CHECK(half.proxies[0].terms.w < full.proxies[0].terms.w); // the envelope scales the field
        CHECK(world::distortionRecords(e, [] {
                  world::EffectContext c;
                  c.seconds = 5.0;
                  return c;
              }()) == 1);
    }
}

// ---- Gravitational Lens --------------------------------------------------------------------------------

TEST_CASE("Gravitational Lens is one camera-facing lens; a horizon only in Black hole mode",
          "[effects][distortion][lens]") {
    world::EffectInstance e = live(world::EffectKind::GravitationalLens, "mass");
    set(e, "gravLens", "offsetX", -3.0f);
    set(e, "gravLens", "offsetY", 20.0f);
    set(e, "gravLens", "offsetZ", -80.0f);
    set(e, "gravLens", "einsteinRadius", 5.0f);
    set(e, "gravLens", "falloffRadius", 3.0f);
    set(e, "gravLens", "mode", 1.0f);
    set(e, "gravLens", "horizonScale", 0.4f);
    world::DistortionFrame f = frameOf({e}, 1.0);
    REQUIRE(f.count == 1);
    const world::DistortionProxy& p = f.proxies[0];
    CHECK(shapeOf(p) == static_cast<int>(world::DistortionShape::Facing));
    CHECK(fieldOf(p) == static_cast<int>(world::DistortionField::Lens));
    CHECK(glm::vec3(p.centre) == glm::vec3(-3.0f, 20.0f, -80.0f));
    CHECK(p.centre.w == 0.0f);
    // A sphere hull of the reach, so any orientation of the facing plane is covered.
    CHECK(glm::length(glm::vec3(p.axis0)) == Approx(15.0f));
    CHECK(glm::length(glm::vec3(p.axis1)) == Approx(15.0f));
    CHECK(glm::length(glm::vec3(p.axis2)) == Approx(15.0f));
    CHECK(p.shape.x == Approx(5.0f));
    CHECK(p.shape.y == Approx(0.4f));
    CHECK(p.motion.y == Approx(1.0f)); // the horizon is opaque
    // The displacement bound the renderer's copy rect relies on: 2 theta_E.
    CHECK(p.terms.w == Approx(10.0f));
    CHECK(world::distortionHullScale(p.axis0.w) == 1.0f);

    set(e, "gravLens", "mode", 0.0f);
    f = frameOf({e}, 1.0);
    REQUIRE(f.count == 1);
    CHECK(f.proxies[0].shape.y == 0.0f);
    CHECK(f.proxies[0].motion.y == 0.0f);
}

TEST_CASE("Gravitational Lens on an entity sits at its bounds' centre and excludes them",
          "[effects][distortion][lens]") {
    FakeScene scene;
    world::EffectInstance e = live(world::EffectKind::GravitationalLens, "mass");
    e.owner = world::EffectOwner::entity("fire");
    const world::DistortionFrame f = frameOf({e}, 1.0, &scene);
    REQUIRE(f.count == 1);
    CHECK(glm::vec3(f.proxies[0].centre) == scene.centre);
    CHECK(f.proxies[0].centre.w == Approx(std::sqrt(3.0f))); // the bounds' half-diagonal
}

// The thin-lens mapping the shader evaluates, held against the physics it claims: the Einstein ring is
// where the image of the point behind the mass lies, the inner image is mirrored, and the softened
// displacement never exceeds the 2 theta_E the producer promises the renderer's rects.
TEST_CASE("the softened point-mass remap: an Einstein ring, a mirrored inner image, a bounded bend",
          "[effects][distortion][lens]") {
    const float tE = 1.0f;
    const float c = 0.25f * tE;
    const auto beta = [&](float th) { return th * (1.0f - tE * tE / (th * th + c * c)); };
    // The ring: beta = 0 at theta = sqrt(tE^2 - c^2), the point directly behind the mass.
    CHECK(beta(std::sqrt(tE * tE - c * c)) == Approx(0.0f).margin(1e-6));
    CHECK(beta(0.5f) < 0.0f);  // inside the ring the image is of the far side
    CHECK(beta(2.0f) > 0.0f);  // outside, of the near side, pulled toward the mass (magnified)
    CHECK(beta(2.0f) < 2.0f);
    float worst = 0.0f;
    for (int i = 1; i < 4000; ++i) {
        const float th = static_cast<float>(i) * 0.001f;
        worst = std::max(worst, std::abs(beta(th) - th));
    }
    CHECK(worst <= 2.0f * tE + 1e-4f);
    CHECK(worst > 1.9f * tE); // and it is the bound, not a loose one
}

TEST_CASE("every Heat Shimmer and Gravitational Lens style is a complete look that names itself",
          "[effects][distortion][shimmer][lens][styles]") {
    for (const world::EffectKind kind : {world::EffectKind::HeatShimmer, world::EffectKind::GravitationalLens}) {
        const world::EffectSchema* schema = world::effectSchema(kind);
        REQUIRE(schema != nullptr);
        REQUIRE(schema->styles.size() == 4);
        for (const world::EffectStyle& style : schema->styles) {
            INFO(schema->key << " / " << style.name);
            world::EffectInstance a = live(kind, "a");
            world::EffectInstance b = live(kind, "b");
            b.values.setFloat(std::string(schema->key) + "/strength", 1.7f);
            b.values.setFloat(std::string(schema->key) + "/photonRing", 33.0f);
            style.apply(a);
            style.apply(b);
            CHECK(a.style == style.name);
            // Every row a style sets is the same whatever the instance held before.
            for (const world::EffectField& f : schema->fields) {
                const std::string key = std::string(schema->key) + "/" + f.leaf;
                if (f.type == world::FieldType::Float || f.type == world::FieldType::Choice) {
                    INFO(key);
                    CHECK(a.values.getFloat(key, -1e9f) == b.values.getFloat(key, -1e9f));
                }
            }
            // And every style draws.
            CHECK(frameOf({a}, 2.0).count == 1);
        }
    }
}
