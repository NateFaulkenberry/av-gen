// FXL Wave 2 on the CPU: the surface slice's sub-blocks (clip, displacement, patterns), their fold
// rules, the owner frame, and Worley F2.
//
// What the builder has to get right without a GPU:
//
//   * the §7 rules for the new sub-blocks: two clips of one kind take the max; a clip of another
//     kind, a second inflate, a second pattern of one kind are EXCLUSIVE and Dropped naming the holder;
//     the displacement modes (inflate, travelling bulge, smear) sum -- all three on one owner are Drawn;
//   * a neutral amount sets no flag: a Dissolve at 0 and a Growth at 1 clip nothing and draw no edge;
//   * the clip threshold spans the keep-value's whole range, so 0 hides nothing and 1 hides all;
//   * the owner frame rides the owner: a point fixed to the owner has the same owner-space position
//     wherever the owner is moved, turned or scaled to;
//   * the pattern blocks go to an extension record at index + 1, and only when one is live;
//   * Motion Smear follows the owner's measured velocity, with its minimum speed and its cap;
//   * Worley F2 >= F1 everywhere, F1 is `voronoiF1`, and F2 - F1 vanishes on the cell edges.

#include "core/noise.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/entity_fx.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;
using world::EffectKind;
using world::EffectStatus;

namespace {

class FakeScene final : public world::EffectSceneQuery {
public:
    struct Node {
        std::uint32_t first = 0;
        std::uint32_t count = 1;
        glm::mat4 world{1.0f};
        glm::vec3 localMin{-1.0f};
        glm::vec3 localMax{1.0f};
        bool hasVelocity = false;
        glm::vec3 velocity{0.0f};
    };
    std::map<std::string, Node, std::less<>> nodes;

    [[nodiscard]] bool nodePosition(std::string_view name, glm::vec3& out) const override {
        const auto it = nodes.find(name);
        if (it == nodes.end()) {
            return false;
        }
        out = glm::vec3(it->second.world[3]);
        return true;
    }
    [[nodiscard]] bool nodeView(std::string_view name, world::NodeView& out) const override {
        const auto it = nodes.find(name);
        if (it == nodes.end()) {
            return false;
        }
        const Node& n = it->second;
        out = world::NodeView{};
        out.world = n.world;
        // The world AABB of the node's local box, as Composition::nodeView reports it.
        glm::vec3 lo(1e30f);
        glm::vec3 hi(-1e30f);
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 c((i & 1) ? n.localMax.x : n.localMin.x, (i & 2) ? n.localMax.y : n.localMin.y,
                              (i & 4) ? n.localMax.z : n.localMin.z);
            const glm::vec3 w = glm::vec3(n.world * glm::vec4(c, 1.0f));
            lo = glm::min(lo, w);
            hi = glm::max(hi, w);
        }
        out.boundsMin = lo;
        out.boundsMax = hi;
        out.hasBounds = true;
        out.firstEntity = n.first;
        out.entityCount = n.count;
        return true;
    }
    [[nodiscard]] bool nodeVelocity(std::string_view name, glm::vec3& out) const override {
        const auto it = nodes.find(name);
        if (it == nodes.end() || !it->second.hasVelocity) {
            return false;
        }
        out = it->second.velocity;
        return true;
    }
};

world::EffectInstance make(EffectKind kind, const std::string& owner, const std::string& id) {
    world::EffectInstance e = world::makeEffect(kind, id);
    e.id = id;
    e.name = id;
    e.owner = world::EffectOwner::entity(owner);
    e.activation = world::Activation::Always;
    e.timing = world::Timing{};
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    return e;
}

struct Built {
    world::EntityFxFrame frame;
    std::vector<EffectStatus> status;
    std::vector<std::string> reasons;
};

Built build(const FakeScene& scene, const std::vector<world::EffectInstance>& effects, double seconds = 1.0) {
    world::EffectContext ctx;
    ctx.seconds = seconds;
    ctx.scene = &scene;
    Built b;
    b.status.assign(effects.size(), EffectStatus::Dormant);
    b.reasons.assign(effects.size(), std::string());
    world::buildEntityFxFrame(effects, ctx, b.frame, {}, b.status, b.reasons);
    return b;
}

FakeScene oneOwner() {
    FakeScene s;
    FakeScene::Node n;
    n.world = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.0f, 0.0f));
    n.localMin = glm::vec3(-1.0f, -2.0f, -1.0f);
    n.localMax = glm::vec3(1.0f, 2.0f, 1.0f);
    s.nodes["pod"] = n;
    return s;
}

std::uint32_t flagsOf(const Built& b, std::size_t entity = 0) {
    const std::uint32_t r = b.frame.recordFor(entity);
    if (r == 0 || r >= b.frame.records.size()) {
        return 0;
    }
    return static_cast<std::uint32_t>(b.frame.records[r].lanes[world::kFxLaneA].z + 0.5f);
}

} // namespace

TEST_CASE("FXL clip: two Dissolves take the larger progress; a Growth beside them is refused by name",
          "[effects][fxl][clip]") {
    const FakeScene scene = oneOwner();
    auto a = make(EffectKind::Dissolve, "pod", "burn-a");
    a.values.setFloat("dissolve/progress", 0.3f);
    auto b = make(EffectKind::Dissolve, "pod", "burn-b");
    b.values.setFloat("dissolve/progress", 0.7f);
    auto g = make(EffectKind::Growth, "pod", "sprout");
    const Built built = build(scene, {a, b, g});
    CHECK(built.status[0] == EffectStatus::Drawn);
    CHECK(built.status[1] == EffectStatus::Drawn);
    REQUIRE(built.status[2] == EffectStatus::Dropped);
    CHECK(built.reasons[2].find("clip") != std::string::npos);
    CHECK(built.reasons[2].find("burn-a") != std::string::npos);
    // The record carries the larger hidden fraction: its threshold is the 0.7 one.
    const std::uint32_t r = built.frame.recordFor(0);
    REQUIRE(r != 0);
    const glm::vec4 clip = built.frame.records[r].lanes[world::kFxLaneClip];
    CHECK(clip.y == Approx(world::entityFxClipThreshold(world::EntityFxClipMode::Noise, 0.7f, clip.z, 0.0f)));
    // Listed the other way round, the same answer: the max does not depend on order.
    const Built swapped = build(scene, {b, a});
    CHECK(swapped.frame.records[swapped.frame.recordFor(0)].lanes[world::kFxLaneClip].y == Approx(clip.y));
}

TEST_CASE("FXL displacement: the modes sum -- Breathing, Organic Pulsation and Motion Smear all act on one owner",
          "[effects][fxl][displace]") {
    FakeScene scene = oneOwner();
    scene.nodes["pod"].hasVelocity = true;
    scene.nodes["pod"].velocity = glm::vec3(10.0f, 0.0f, 0.0f);
    const auto breathe = make(EffectKind::Breathing, "pod", "breathe");
    const auto swallow = make(EffectKind::OrganicPulsation, "pod", "swallow");
    const auto smear = make(EffectKind::MotionSmear, "pod", "smear");
    const auto again = make(EffectKind::Breathing, "pod", "breathe-2");
    const Built b = build(scene, {breathe, swallow, smear, again});
    CHECK(b.status[0] == EffectStatus::Drawn);
    CHECK(b.status[1] == EffectStatus::Drawn);
    CHECK(b.status[2] == EffectStatus::Drawn);
    // One inflate per owner: the second Breathing says whose the slot is.
    REQUIRE(b.status[3] == EffectStatus::Dropped);
    CHECK(b.reasons[3].find("inflate") != std::string::npos);
    CHECK(b.reasons[3].find("breathe") != std::string::npos);
    const std::uint32_t f = flagsOf(b);
    CHECK((f & world::kFxInflate) != 0);
    CHECK((f & world::kFxTravel) != 0);
    CHECK((f & world::kFxSmear) != 0);
    CHECK((f & world::kFxExt) == 0); // no pattern: one record, no extension
    CHECK(b.frame.records.size() == 2);
}

TEST_CASE("FXL: a neutral amount sets no sub-block -- Dissolve at 0, Growth at 1, Breathing of 0",
          "[effects][fxl][gate]") {
    const FakeScene scene = oneOwner();
    auto d = make(EffectKind::Dissolve, "pod", "d");
    d.values.setFloat("dissolve/progress", 0.0f);
    CHECK(flagsOf(build(scene, {d})) == world::kFxOn);
    auto g = make(EffectKind::Growth, "pod", "g");
    g.values.setFloat("growth/progress", 1.0f);
    CHECK(flagsOf(build(scene, {g})) == world::kFxOn);
    auto br = make(EffectKind::Breathing, "pod", "b");
    br.values.setFloat("breathing/amplitude", 0.0f);
    CHECK(flagsOf(build(scene, {br})) == world::kFxOn);
    // The control: each at a real amount sets its bit.
    d.values.setFloat("dissolve/progress", 0.4f);
    CHECK((flagsOf(build(scene, {d})) & world::kFxClip) != 0);
}

TEST_CASE("FXL clip threshold: hidden 0 keeps every k with no edge, hidden 1 keeps none", "[effects][fxl][clip]") {
    using world::EntityFxClipMode;
    for (const EntityFxClipMode mode : {EntityFxClipMode::Noise, EntityFxClipMode::Height, EntityFxClipMode::Radial}) {
        for (const float breakup : {0.0f, 0.5f, 1.0f}) {
            const float edge = 0.05f;
            // The keep-value's range for the mode (see `fxClipKeep` in pbr_shade.wgsl).
            const bool front = mode != EntityFxClipMode::Noise;
            const float lo = front ? -0.25f * breakup - 0.0f : 0.0f;
            const float hi = front ? 1.0f + 0.25f * breakup : 1.0f;
            const float none = world::entityFxClipThreshold(mode, 0.0f, edge, breakup);
            const float all = world::entityFxClipThreshold(mode, 1.0f, edge, breakup);
            INFO("mode " << static_cast<int>(mode) << ", breakup " << breakup);
            CHECK(lo - none >= edge);      // nothing discarded, and every k is past the edge band
            CHECK(all > hi);               // everything discarded
            CHECK(world::entityFxClipThreshold(mode, 0.5f, edge, breakup) > none);
            CHECK(world::entityFxClipThreshold(mode, 0.5f, edge, breakup) < all);
        }
    }
}

TEST_CASE("FXL owner frame: a point fixed to the owner keeps its owner-space position however it moves",
          "[effects][fxl][frame]") {
    world::NodeView view;
    view.hasBounds = true;
    const glm::vec3 localMin(-1.0f, 0.0f, -0.5f);
    const glm::vec3 localMax(1.0f, 3.0f, 0.5f);
    const glm::vec3 marker(0.4f, 2.2f, -0.3f); // a point on the owner, in its node space
    const auto place = [&](const glm::mat4& world) {
        view.world = world;
        glm::vec3 lo(1e30f);
        glm::vec3 hi(-1e30f);
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 c((i & 1) ? localMax.x : localMin.x, (i & 2) ? localMax.y : localMin.y,
                              (i & 4) ? localMax.z : localMin.z);
            const glm::vec3 w = glm::vec3(world * glm::vec4(c, 1.0f));
            lo = glm::min(lo, w);
            hi = glm::max(hi, w);
        }
        view.boundsMin = lo;
        view.boundsMax = hi;
        world::EntityLaneContribution c;
        world::setEntityFxFrame(c, view);
        return c;
    };
    const world::EntityLaneContribution rest = place(glm::mat4(1.0f));
    const glm::vec3 q0 = world::entityFxOwnerSpace(rest, marker);
    // The bounds' centre is the origin of q, and a corner is at the half diagonal's length: 1.
    CHECK(glm::length(world::entityFxOwnerSpace(rest, (localMin + localMax) * 0.5f)) < 1e-5f);
    CHECK(glm::length(world::entityFxOwnerSpace(rest, localMax)) == Approx(1.0f).margin(1e-5));
    // h = q.y * gy + 0.5 runs 0 at the bottom of the bounds to 1 at the top.
    CHECK(world::entityFxOwnerSpace(rest, localMin).y * rest.shape.w + 0.5f == Approx(0.0f).margin(1e-5));
    CHECK(world::entityFxOwnerSpace(rest, localMax).y * rest.shape.w + 0.5f == Approx(1.0f).margin(1e-5));
    // Moved and scaled (no turn): the same point of the owner, the same q.
    const glm::mat4 moved = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(7.0f, -2.0f, 3.0f)), glm::vec3(2.5f));
    const world::EntityLaneContribution m = place(moved);
    const glm::vec3 q1 = world::entityFxOwnerSpace(m, glm::vec3(moved * glm::vec4(marker, 1.0f)));
    CHECK(glm::length(q1 - q0) < 1e-4f);
    // Turned about its up axis by a quarter turn: the world bounds of a quarter-turned box are the
    // same box turned, so still the same q.
    const glm::mat4 turned = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 1.0f, 0.0f)),
                                         glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
    const world::EntityLaneContribution t = place(turned);
    const glm::vec3 q2 = world::entityFxOwnerSpace(t, glm::vec3(turned * glm::vec4(marker, 1.0f)));
    CHECK(glm::length(q2 - q0) < 1e-4f);
}

TEST_CASE("FXL: the pattern blocks go to an extension record, only when one is live", "[effects][fxl][ext]") {
    const FakeScene scene = oneOwner();
    const auto bio = make(EffectKind::Bioluminescence, "pod", "bio");
    const Built b = build(scene, {bio});
    REQUIRE(b.status[0] == EffectStatus::Drawn);
    // Record 0 (neutral), the owner's record, and its extension right after it.
    REQUIRE(b.frame.records.size() == 3);
    const std::uint32_t r = b.frame.recordFor(0);
    CHECK(r == 1);
    const std::uint32_t f = flagsOf(b);
    CHECK((f & world::kFxBio) != 0);
    CHECK((f & world::kFxExt) != 0);
    const glm::vec4 bio1 = b.frame.records[r + 1].lanes[world::kFxExtBio1];
    CHECK(bio1.g > 1.0f); // the colour times the brightness
    // A second Bioluminescence is refused; a Pulsing Veins beside it is its own block.
    const auto bio2 = make(EffectKind::Bioluminescence, "pod", "bio-2");
    const auto veins = make(EffectKind::PulsingVeins, "pod", "veins");
    const Built two = build(scene, {bio, bio2, veins});
    CHECK(two.status[1] == EffectStatus::Dropped);
    CHECK(two.reasons[1].find("bio") != std::string::npos);
    CHECK(two.status[2] == EffectStatus::Drawn);
    CHECK((flagsOf(two) & world::kFxVeins) != 0);
    // A Bioluminescence faded to nothing by its envelope writes no extension.
    auto faded = bio;
    faded.values.setFloat("bioluminescence/intensity", 0.0f);
    CHECK(build(scene, {faded}).frame.records.size() == 2);
}

TEST_CASE("FXL: Fresnel shares Glow's rim lane and sums with it", "[effects][fxl]") {
    const FakeScene scene = oneOwner();
    auto fresnel = make(EffectKind::Fresnel, "pod", "fresnel");
    fresnel.values.setFloat("fresnel/scale", 2.0f);
    fresnel.values.setColor("fresnel/color", glm::vec3(1.0f, 0.0f, 0.0f));
    auto glow = make(EffectKind::Glow, "pod", "glow");
    glow.values.setFloat("glow/rim", 3.0f);
    glow.values.setColor("glow/rimColor", glm::vec3(0.0f, 1.0f, 0.0f));
    glow.values.setFloat("glow/gain", 1.0f);
    const Built b = build(scene, {fresnel, glow});
    CHECK(b.status[0] == EffectStatus::Drawn);
    CHECK(b.status[1] == EffectStatus::Drawn);
    const glm::vec4 rim = b.frame.records[b.frame.recordFor(0)].lanes[world::kFxLaneRim];
    CHECK(rim.r == Approx(2.0f));
    CHECK(rim.g == Approx(3.0f));
}

TEST_CASE("Motion Smear follows the owner's measured velocity, above its minimum and under its cap",
          "[effects][fxl][displace]") {
    FakeScene scene = oneOwner();
    auto smear = make(EffectKind::MotionSmear, "pod", "smear");
    SECTION("no recorded motion: Dormant, nothing written") {
        const Built b = build(scene, {smear});
        CHECK(b.status[0] == EffectStatus::Dormant);
        CHECK(b.frame.empty());
    }
    scene.nodes["pod"].hasVelocity = true;
    SECTION("fast: the smear points back along the path, seconds x speed") {
        scene.nodes["pod"].velocity = glm::vec3(0.0f, 0.0f, -10.0f);
        const Built b = build(scene, {smear});
        REQUIRE(b.status[0] == EffectStatus::Drawn);
        const glm::vec4 s = b.frame.records[b.frame.recordFor(0)].lanes[world::kFxLaneSmear];
        CHECK(s.z == Approx(10.0f * 0.08f));
        CHECK(s.x == Approx(0.0f));
    }
    SECTION("under the minimum speed: no smear") {
        scene.nodes["pod"].velocity = glm::vec3(1.5f, 0.0f, 0.0f);
        CHECK((flagsOf(build(scene, {smear})) & world::kFxSmear) == 0);
    }
    SECTION("very fast: capped at the maximum stretch") {
        scene.nodes["pod"].velocity = glm::vec3(500.0f, 0.0f, 0.0f);
        const Built b = build(scene, {smear});
        const glm::vec4 s = b.frame.records[b.frame.recordFor(0)].lanes[world::kFxLaneSmear];
        CHECK(glm::length(glm::vec3(s)) == Approx(3.0f));
    }
}

TEST_CASE("FXL Wave 2: the envelope fades every sub-block towards nothing", "[effects][fxl]") {
    world::EntityLaneContribution c;
    c.hasClip = true;
    c.clipHidden = 0.8f;
    c.clipEdge = glm::vec3(4.0f);
    c.hasInflate = true;
    c.inflateAmp = 0.2f;
    c.hasSmear = true;
    c.smear = glm::vec3(1.0f, 0.0f, 0.0f);
    c.hasBio = true;
    c.bioColor = glm::vec3(2.0f);
    c.hasHue = true;
    c.hueRange = 1.0f;
    world::applyEntityEnvelope(c, 0.25f);
    CHECK(c.clipHidden == Approx(0.2f));
    CHECK(c.clipEdge.x == Approx(1.0f));
    CHECK(c.inflateAmp == Approx(0.05f));
    CHECK(c.smear.x == Approx(0.25f));
    CHECK(c.bioColor.x == Approx(0.5f));
    CHECK(c.hueRange == Approx(0.25f));
    world::applyEntityEnvelope(c, 0.0f);
    CHECK(world::entityFxSurfaceFlags(c) == 0u);
}

TEST_CASE("The Wave 2 surface types attach to entities and to nothing else", "[effects][fxl][targets]") {
    for (const EffectKind k : {EffectKind::Dissolve, EffectKind::Growth, EffectKind::Breathing,
                               EffectKind::OrganicPulsation, EffectKind::Bioluminescence, EffectKind::PulsingVeins,
                               EffectKind::Fresnel, EffectKind::RimLight, EffectKind::ColorCycling,
                               EffectKind::MotionSmear}) {
        const world::EffectSchema* s = world::effectSchema(k);
        REQUIRE(s != nullptr);
        INFO(s->key);
        CHECK(s->targets == world::targetBit(world::EffectTarget::Entity));
        CHECK(s->resolve.bucket == world::EffectBucket::EntityLanes);
        CHECK(s->resolve.lanes != nullptr);
    }
}

TEST_CASE("Worley F2: F2 >= F1 everywhere, F1 is voronoiF1, and F2 - F1 vanishes on the cell edges",
          "[effects][fxl][worley]") {
    std::mt19937 rng(11);
    std::uniform_real_distribution<float> u(-50.0f, 50.0f);
    float smallestGap = 1e9f;
    for (int i = 0; i < 20000; ++i) {
        const glm::vec3 p(u(rng), u(rng), u(rng));
        const glm::vec3 w = world::worleyF1F2(p, 23u);
        REQUIRE(w.y >= w.x);
        REQUIRE(w.x == noise::voronoiF1(p, 23u));
        REQUIRE(w.z >= 0.0f);
        REQUIRE(w.z < 1.0f);
        smallestGap = std::min(smallestGap, w.y - w.x);
    }
    // Edges: walk from a point towards the feature point that is second-nearest to it. F2 - F1 falls
    // to zero where the nearest point changes (the bisector between the two) -- the cell wall.
    const auto feature = [](glm::ivec3 cell, std::uint32_t seed) {
        return glm::vec3(cell) + glm::vec3(noise::hash01(cell.x, cell.y, cell.z, seed),
                                           noise::hash01(cell.x, cell.y, cell.z, seed + 1u),
                                           noise::hash01(cell.x, cell.y, cell.z, seed + 2u));
    };
    int walls = 0;
    for (int i = 0; i < 200; ++i) {
        const glm::vec3 p(u(rng), u(rng), u(rng));
        const glm::ivec3 c(glm::floor(p));
        // The two nearest feature points by brute force over the 27 cells.
        glm::vec3 a(0.0f);
        glm::vec3 b(0.0f);
        float da = 1e9f;
        float db = 1e9f;
        for (int z = -1; z <= 1; ++z) {
            for (int y = -1; y <= 1; ++y) {
                for (int x = -1; x <= 1; ++x) {
                    const glm::vec3 f = feature(c + glm::ivec3(x, y, z), 23u);
                    const float d = glm::length(f - p);
                    if (d < da) {
                        db = da;
                        b = a;
                        da = d;
                        a = f;
                    } else if (d < db) {
                        db = d;
                        b = f;
                    }
                }
            }
        }
        const glm::vec3 mid = (a + b) * 0.5f;
        const glm::vec3 w = world::worleyF1F2(mid, 23u);
        // At the midpoint of the two nearest, unless a third point is nearer there, F1 == F2.
        if (std::abs(w.x - glm::length(a - mid)) < 1e-4f) {
            CHECK(w.y - w.x < 1e-4f);
            ++walls;
        }
        // And well inside the cell (at the feature point itself) the gap is large.
        const glm::vec3 inside = world::worleyF1F2(a, 23u);
        CHECK(inside.x < 1e-5f);
        CHECK(inside.y - inside.x > 0.05f);
    }
    CHECK(walls > 100);
    CHECK(smallestGap < 0.01f);
}
