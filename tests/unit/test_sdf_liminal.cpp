// ADR-1040: the liminal vocabulary nodes -- stairs, screw, warp -- on the CPU reference and the packed
// interpreter. GPU parity is in tests/rendering/test_sdf_gpu.cpp.

#include "spatial/sdf.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <glm/gtc/constants.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

using namespace avgen;
using namespace avgen::spatial;
using Catch::Matchers::WithinAbs;

namespace {

SdfNode node(SdfNodeKind kind) {
    SdfNode n;
    n.kind = kind;
    return n;
}

SdfNode unary(SdfNodeKind kind, SdfNode child) {
    SdfNode n = node(kind);
    n.children.push_back(std::move(child));
    return n;
}

SdfNode box(glm::vec3 size) {
    SdfNode n = node(SdfNodeKind::Box);
    n.size = size;
    return n;
}

SdfNode translate(glm::vec3 t, SdfNode child) {
    SdfNode n = unary(SdfNodeKind::Translate, std::move(child));
    n.translation = t;
    return n;
}

SdfTree treeOf(SdfNode root) {
    SdfTree t;
    t.root = std::move(root);
    return t;
}

SdfNode stairs(float run, float rise, float halfWidth, int count, float thickness = 0.0f) {
    SdfNode s = node(SdfNodeKind::Stairs);
    s.size = glm::vec3(run, rise, halfWidth);
    s.count = count;
    s.height = thickness;
    return s;
}

// A deterministic spread of points.
std::vector<glm::vec3> points(glm::vec3 lo, glm::vec3 hi, int perAxis) {
    std::vector<glm::vec3> out;
    for (int i = 0; i < perAxis; ++i) {
        for (int j = 0; j < perAxis; ++j) {
            for (int k = 0; k < perAxis; ++k) {
                const glm::vec3 t((static_cast<float>(i) + 0.37f) / static_cast<float>(perAxis),
                                  (static_cast<float>(j) + 0.61f) / static_cast<float>(perAxis),
                                  (static_cast<float>(k) + 0.13f) / static_cast<float>(perAxis));
                out.push_back(lo + (hi - lo) * t);
            }
        }
    }
    return out;
}

void checkPackedMatches(const SdfTree& tree, const std::vector<glm::vec3>& pts) {
    REQUIRE(tree.validate());
    std::vector<SdfNodeGpu> packed;
    packSdfTree(tree, packed);
    for (const glm::vec3& p : pts) {
        CHECK_THAT(static_cast<double>(evaluatePacked(packed, p, 0.0)),
                   WithinAbs(static_cast<double>(tree.evaluate(p, 0.0)), 1e-5));
    }
}

} // namespace

TEST_CASE("Stairs: treads and risers are the surface, the block is solid, the flight is finite", "[sdf][liminal]") {
    const SdfTree t = treeOf(stairs(0.3f, 0.2f, 0.5f, 4));
    REQUIRE(t.validate());
    // On each tread (the top of step i at y = (i + 1) * rise), mid-depth: the surface.
    for (int i = 0; i < 4; ++i) {
        const float x = (static_cast<float>(i) + 0.5f) * 0.3f;
        const float y = static_cast<float>(i + 1) * 0.2f;
        CHECK_THAT(static_cast<double>(t.evaluate({x, y, 0.0f}, 0.0)), WithinAbs(0.0, 1e-5));
        // 5 cm above the tread: 5 cm away (nearest is the tread below, not the next riser).
        CHECK_THAT(static_cast<double>(t.evaluate({x - 0.1f, y + 0.05f, 0.0f}, 0.0)), WithinAbs(0.05, 1e-5));
        // Inside the block below the tread.
        CHECK(t.evaluate({x, y - 0.05f, 0.0f}, 0.0) < 0.0f);
    }
    // Beyond the top step, above the last tread's height: outside; past the width: outside by the overhang.
    CHECK(t.evaluate({1.5f, 0.5f, 0.0f}, 0.0) > 0.0f);
    CHECK_THAT(static_cast<double>(t.evaluate({0.45f, 0.1f, 0.7f}, 0.0)), WithinAbs(0.2, 1e-5));
    // Below the floor of a block flight: outside.
    CHECK(t.evaluate({0.5f, -0.1f, 0.0f}, 0.0) > 0.0f);
}

TEST_CASE("Stairs: a floating flight has a sloped underside", "[sdf][liminal]") {
    const SdfTree t = treeOf(stairs(0.3f, 0.2f, 0.5f, 6, 0.15f));
    REQUIRE(t.validate());
    // Under step 4 near its riser line, well above y = 0: a block flight would be solid there; this one is not.
    CHECK(t.evaluate({1.2f, 0.2f, 0.0f}, 0.0) > 0.0f);
    CHECK(treeOf(stairs(0.3f, 0.2f, 0.5f, 6)).evaluate({1.2f, 0.2f, 0.0f}, 0.0) < 0.0f);
    // Just under the tread: still inside the slab.
    CHECK(t.evaluate({1.35f, 0.95f, 0.0f}, 0.0) < 0.0f);
}

TEST_CASE("Stairs: the field is a distance bound (1-Lipschitz), so a march cannot overstep it", "[sdf][liminal]") {
    for (const float thickness : {0.0f, 0.2f}) {
        const SdfTree t = treeOf(stairs(0.28f, 0.17f, 0.6f, 8, thickness));
        const auto pts = points({-1.0f, -0.8f, -1.0f}, {3.2f, 2.2f, 1.0f}, 9);
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            const glm::vec3 a = pts[i];
            const glm::vec3 b = pts[(i * 7 + 3) % pts.size()];
            const float da = t.evaluate(a, 0.0);
            const float db = t.evaluate(b, 0.0);
            CHECK(std::fabs(da - db) <= glm::length(a - b) * 1.0001f + 1e-5f);
        }
    }
}

TEST_CASE("Stairs: invalid dimensions are refused", "[sdf][liminal]") {
    CHECK_FALSE(treeOf(stairs(0.0f, 0.2f, 0.5f, 4)).validate());
    CHECK_FALSE(treeOf(stairs(0.3f, 0.0f, 0.5f, 4)).validate());
    CHECK_FALSE(treeOf(stairs(0.3f, 0.2f, 0.5f, 0)).validate());
}

TEST_CASE("Screw (translation): the world is invariant under the cell step", "[sdf][liminal]") {
    SdfNode screw = unary(SdfNodeKind::Screw, translate({0.2f, 0.0f, 0.0f}, box({0.6f, 0.2f, 0.5f})));
    screw.translation = glm::vec3(3.0f, 1.2f, 0.0f);
    const SdfTree t = treeOf(std::move(screw));
    REQUIRE(t.validate());
    for (const glm::vec3& p : points({-1.0f, -0.6f, -1.0f}, {1.0f, 0.6f, 1.0f}, 5)) {
        const float d0 = t.evaluate(p, 0.0);
        for (int k : {-3, 1, 2, 17}) {
            CHECK_THAT(static_cast<double>(t.evaluate(p + static_cast<float>(k) * glm::vec3(3.0f, 1.2f, 0.0f), 0.0)),
                       WithinAbs(static_cast<double>(d0), 1e-4));
        }
    }
    // Cell 0 is the child itself near the origin.
    CHECK_THAT(static_cast<double>(t.evaluate({0.2f, 0.0f, 0.0f}, 0.0)), WithinAbs(-0.2, 1e-5));
}

TEST_CASE("Screw seam guard: with an offset the field never overstates the distance to the next cell",
          "[sdf][liminal]") {
    // A wall that reaches the cell boundary: the classic repeat hazard (only the own cell is evaluated).
    const SdfNode content = translate({1.2f, 0.0f, 0.0f}, box({0.3f, 1.0f, 1.0f})); // x in [0.9, 1.5]
    const glm::vec3 T(3.0f, 0.0f, 0.0f);
    SdfNode guarded = unary(SdfNodeKind::Screw, content);
    guarded.translation = T;
    guarded.offset = 0.05f;
    SdfNode bare = guarded;
    bare.offset = 0.0f;
    const SdfTree g = treeOf(guarded);
    const SdfTree b = treeOf(bare);
    const SdfTree child = treeOf(content);
    int overstatedBare = 0;
    for (const glm::vec3& p : points({-1.5f, -0.5f, -0.5f}, {1.5f, 0.5f, 0.5f}, 9)) {
        // The true distance: every nearby cell's copy.
        float truth = 1e9f;
        for (int k = -2; k <= 2; ++k) {
            truth = std::min(truth, child.evaluate(p - static_cast<float>(k) * T, 0.0));
        }
        CHECK(g.evaluate(p, 0.0) <= truth + 0.05f + 1e-5f);
        overstatedBare += b.evaluate(p, 0.0) > truth + 0.1f ? 1 : 0;
    }
    CHECK(overstatedBare > 0); // the hazard is real without the guard
    checkPackedMatches(g, points({-4.0f, -1.0f, -1.0f}, {4.0f, 1.0f, 1.0f}, 7));
}

TEST_CASE("Screw (helix): a quarter turn and a rise is a symmetry of the world", "[sdf][liminal]") {
    SdfNode screw = unary(SdfNodeKind::Screw, translate({4.0f, 0.0f, 0.0f}, box({1.0f, 0.3f, 1.2f})));
    screw.count = 4;
    screw.translation = glm::vec3(0.0f, 2.5f, 0.0f);
    const SdfTree t = treeOf(std::move(screw));
    REQUIRE(t.validate());
    // S: turn +90 degrees in the atan2(z, x) direction, rise 2.5.
    const auto S = [](glm::vec3 p) { return glm::vec3(-p.z, p.y + 2.5f, p.x); };
    for (const glm::vec3& p : points({2.8f, -1.0f, -1.4f}, {5.2f, 1.0f, 1.4f}, 5)) {
        const float d0 = t.evaluate(p, 0.0);
        glm::vec3 q = p;
        for (int k = 1; k <= 9; ++k) {
            q = S(q);
            INFO("cell " << k);
            CHECK_THAT(static_cast<double>(t.evaluate(q, 0.0)), WithinAbs(static_cast<double>(d0), 1e-4));
        }
    }
    // One full turn up is a pure translation by 4 * 2.5 = 10.
    CHECK_THAT(static_cast<double>(t.evaluate({4.0f, 10.0f, 0.0f}, 0.0)),
               WithinAbs(static_cast<double>(t.evaluate({4.0f, 0.0f, 0.0f}, 0.0)), 1e-4));
}

TEST_CASE("Warp: zero amount is the identity, a masked axis is untouched, the phase moves it continuously",
          "[sdf][liminal]") {
    SdfNode floor = node(SdfNodeKind::Plane);
    floor.axis = glm::vec3(0.0f, 1.0f, 0.0f);
    floor.offset = 0.0f;
    SdfNode warp = unary(SdfNodeKind::Warp, floor);
    warp.frequency = 0.4f;
    warp.size = glm::vec3(1.0f, 0.0f, 1.0f); // horizontal only: a floor stays a floor
    warp.amount = 0.8f;
    const SdfTree horizontal = treeOf(warp);
    REQUIRE(horizontal.validate());
    for (const glm::vec3& p : points({-5.0f, -1.0f, -5.0f}, {5.0f, 1.0f, 5.0f}, 6)) {
        CHECK_THAT(static_cast<double>(horizontal.evaluate(p, 0.0)), WithinAbs(static_cast<double>(p.y), 1e-5));
    }
    SdfNode wall = node(SdfNodeKind::Plane);
    wall.axis = glm::vec3(1.0f, 0.0f, 0.0f);
    SdfNode w2 = unary(SdfNodeKind::Warp, wall);
    w2.frequency = 0.4f;
    w2.amount = 0.0f;
    CHECK_THAT(static_cast<double>(treeOf(w2).evaluate({0.3f, 1.0f, 2.0f}, 0.0)), WithinAbs(0.3, 1e-6));
    // A wall bows by at most `amount`, and a small phase step moves it a small distance.
    w2.amount = 0.5f;
    float worst = 0.0f;
    float maxStep = 0.0f;
    for (const glm::vec3& p : points({-0.2f, -3.0f, -3.0f}, {0.2f, 3.0f, 3.0f}, 6)) {
        SdfNode a = w2;
        SdfNode b = w2;
        b.translation = glm::vec3(0.01f, 0.0f, 0.0f);
        const float da = treeOf(a).evaluate(p, 0.0);
        const float db = treeOf(b).evaluate(p, 0.0);
        worst = std::max(worst, std::fabs(da - p.x));
        maxStep = std::max(maxStep, std::fabs(da - db));
    }
    CHECK(worst <= 0.5f + 1e-5f);
    CHECK(worst > 0.05f);
    CHECK(maxStep < 0.05f);
    // The window: with offset 2 along z and a 1 m fade, the warp is exactly the identity beyond |z| = 2.
    SdfNode windowed = w2;
    windowed.axis = glm::vec3(0.0f, 0.0f, 1.0f);
    windowed.offset = 2.0f;
    windowed.rounding = 1.0f;
    for (const glm::vec3& p : points({-0.2f, -2.0f, 2.0f}, {0.2f, 2.0f, 3.0f}, 5)) {
        CHECK_THAT(static_cast<double>(treeOf(windowed).evaluate(p, 0.0)), WithinAbs(static_cast<double>(p.x), 1e-6));
    }
}

TEST_CASE("Shell: a box becomes a room with walls of the given thickness", "[sdf][liminal]") {
    SdfNode shell = unary(SdfNodeKind::Shell, box({2.15f, 1.65f, 3.15f})); // interior 4 x 3 x 6, walls 0.3
    shell.offset = 0.3f;
    const SdfTree t = treeOf(shell);
    REQUIRE(t.validate());
    CHECK_THAT(static_cast<double>(t.evaluate({0.0f, 0.0f, 0.0f}, 0.0)), WithinAbs(1.5, 1e-5));   // to the ceiling/floor
    CHECK_THAT(static_cast<double>(t.evaluate({1.0f, 0.0f, 0.0f}, 0.0)), WithinAbs(1.0, 1e-5));   // to the +x wall
    CHECK(t.evaluate({2.15f, 0.0f, 0.0f}, 0.0) < 0.0f);                                          // inside the wall
    CHECK_THAT(static_cast<double>(t.evaluate({2.5f, 0.0f, 0.0f}, 0.0)), WithinAbs(0.2, 1e-5));   // outside it
    SdfNode bad = shell;
    bad.offset = -0.1f;
    CHECK_FALSE(treeOf(bad).validate());
    checkPackedMatches(t, points({-3.0f, -2.0f, -4.0f}, {3.0f, 2.0f, 4.0f}, 6));
}

TEST_CASE("Liminal kinds: the packed interpreter equals the tree, and JSON round-trips", "[sdf][liminal]") {
    SdfNode flight = translate({-1.0f, 0.0f, 0.0f}, stairs(0.3f, 0.18f, 0.7f, 7, 0.1f));
    SdfNode landing = translate({1.4f, 1.26f, 0.0f}, box({0.5f, 0.05f, 0.7f}));
    SdfNode cell = node(SdfNodeKind::Union);
    cell.children.push_back(std::move(flight));
    cell.children.push_back(std::move(landing));
    SdfNode warp = unary(SdfNodeKind::Warp, std::move(cell));
    warp.name = "breath";
    warp.amount = 0.1f;
    warp.frequency = 0.7f;
    warp.translation = glm::vec3(0.3f, 0.0f, 1.1f);
    warp.seed = 4;
    SdfNode screw = unary(SdfNodeKind::Screw, std::move(warp));
    screw.name = "climb";
    screw.translation = glm::vec3(3.1f, 1.26f, 0.0f);
    const SdfTree t = treeOf(screw);
    checkPackedMatches(t, points({-4.0f, -2.0f, -1.5f}, {4.0f, 3.0f, 1.5f}, 7));

    SdfNode helix = screw;
    helix.count = 5;
    helix.translation = glm::vec3(0.0f, 0.8f, 0.0f);
    checkPackedMatches(treeOf(translate({0.0f, 0.0f, 0.0f}, helix)), points({-4.0f, -2.0f, -4.0f}, {4.0f, 3.0f, 4.0f}, 7));

    const nlohmann::json j = t.toJson();
    auto back = SdfTree::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->structuralHash() == t.structuralHash());
    CHECK(j["root"]["kind"] == "screw");
    CHECK(j["root"]["children"][0]["kind"] == "warp");
    CHECK(sdfNodeKindFromName("stairs") == SdfNodeKind::Stairs);
    CHECK(sdfNodeIsPrimitive(SdfNodeKind::Stairs));
    CHECK_FALSE(sdfNodeIsPrimitive(SdfNodeKind::Warp));
    CHECK(sdfNodeMaxChildren(SdfNodeKind::Screw) == 1);
    CHECK(sdfNodeMaxChildren(SdfNodeKind::Stairs) == 0);
}
