#include "scene/tree.hpp"
#include "spatial/point_grid.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <vector>

using namespace avgen;
using namespace avgen::scene;

namespace {

// A COARSER tree, not a smaller one.
//
// Halving every length is the obvious way to make a cheap test tree and it is the wrong one: the
// model is scale-invariant, so a half-size tree with an eighth of the markers has the same marker
// density, the same competition and therefore the same node count and the same cost. It also needs
// the same number of iterations, because the trunk covers half the distance in half-length steps.
// Cutting the iterations instead just leaves the trunk stalled below the crown.
//
// What actually makes a cheap tree is lowering the RESOLUTION: a longer internode, which enlarges
// the occupancy sphere and so reduces how many tips the same crown can support, with the marker
// count reduced to keep roughly the same number of markers per node. The result is structurally the
// same tree -- four tiers, a bole, self-pruning -- drawn with a coarser pen.
TreeParams testParams(std::uint32_t seed = 7) {
    TreeParams p;
    p.seed = seed;
    p.internodeLength = 0.62f;
    p.markerCount = 7000;
    p.iterations = 30;
    p.tipRadius = 0.046f;
    return p;
}

} // namespace

TEST_CASE("point grid finds exactly the points a linear scan finds", "[tree][spatial]") {
    std::vector<glm::vec3> points;
    for (int i = 0; i < 2000; ++i) {
        const auto f = static_cast<float>(i);
        points.emplace_back(std::sin(f * 0.7f) * 12.0f, std::cos(f * 0.31f) * 9.0f, std::sin(f * 1.13f) * 15.0f);
    }
    spatial::PointGrid grid;
    grid.build(points, 2.5f);
    REQUIRE(grid.size() == points.size());

    std::vector<std::uint32_t> hits;
    for (int t = 0; t < 25; ++t) {
        const glm::vec3 probe = points[static_cast<std::size_t>(t) * 37];
        const float radius = 1.0f + static_cast<float>(t) * 0.2f;
        grid.query(probe, radius, hits);

        std::vector<std::uint32_t> expected;
        for (std::uint32_t i = 0; i < points.size(); ++i) {
            if (glm::distance(points[i], probe) <= radius) {
                expected.push_back(i);
            }
        }
        std::vector<std::uint32_t> sorted = hits;
        std::sort(sorted.begin(), sorted.end());
        CHECK(sorted == expected);
    }

    // The query order itself must be reproducible, not merely the set: a generator that sums
    // directions over the returned points gets a different float if the order moves.
    std::vector<std::uint32_t> again;
    spatial::PointGrid rebuilt;
    rebuilt.build(points, 2.5f);
    rebuilt.query(points[11], 4.0f, again);
    grid.query(points[11], 4.0f, hits);
    CHECK(again == hits);
}

TEST_CASE("the same parameters produce the same tree", "[tree][determinism]") {
    const TreeParams params = testParams();
    const auto first = generateTree(params);
    const auto second = generateTree(params);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());

    REQUIRE(first->nodes.size() == second->nodes.size());
    CHECK(first->contentHash() == second->contentHash());
    for (std::size_t i = 0; i < first->nodes.size(); ++i) {
        const TreeNode& a = first->nodes[i];
        const TreeNode& b = second->nodes[i];
        REQUIRE(a.parent == b.parent);
        REQUIRE(a.axis == b.axis);
        REQUIRE(a.order == b.order);
        REQUIRE(a.position.x == b.position.x);
        REQUIRE(a.position.y == b.position.y);
        REQUIRE(a.position.z == b.position.z);
        REQUIRE(a.radius == b.radius);
    }
    REQUIRE(first->foliage.size() == second->foliage.size());
    REQUIRE(first->roots.size() == second->roots.size());
}

TEST_CASE("a different seed produces a different tree", "[tree][determinism]") {
    // The complement of the determinism test, and the one that actually catches a generator that
    // has stopped reading its seed -- which is a failure the determinism test alone would pass.
    const auto a = generateTree(testParams(7));
    const auto b = generateTree(testParams(8));
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->contentHash() != b->contentHash());
}

TEST_CASE("the graph is a valid rooted acyclic hierarchy", "[tree][topology]") {
    for (std::uint32_t seed : {1u, 7u, 42u, 1337u}) {
        const auto tree = generateTree(testParams(seed));
        REQUIRE(tree.has_value());
        const auto ok = tree->validateTopology();
        INFO("seed " << seed << (ok ? "" : ": " + ok.error().message));
        REQUIRE(ok.has_value());
    }
}

TEST_CASE("a child branch is never thicker than its parent", "[tree][topology]") {
    // Stated separately from validateTopology because it is the pipe model's own invariant and the
    // one most likely to break when the accumulation or the flare changes.
    const auto tree = generateTree(testParams(23));
    REQUIRE(tree.has_value());
    for (const TreeNode& node : tree->nodes) {
        if (node.parent == kNoNode) {
            continue;
        }
        const float parent = tree->nodes[node.parent].radius;
        INFO("node " << node.id << " r=" << node.radius << " parent r=" << parent);
        CHECK(node.radius <= parent * 1.02f + 1e-4f);
    }
}

TEST_CASE("the tree stays inside the volume it was asked to fill", "[tree][bounds]") {
    TreeParams params = testParams(11);
    const auto tree = generateTree(params);
    REQUIRE(tree.has_value());

    // The crown envelope bounds growth but does not bound it exactly: a bud one internode short of
    // the envelope's edge still grows a whole internode, and the tropism term can carry it further.
    // The margin is that overshoot, and it is checked rather than assumed.
    const float margin = params.internodeLength * 4.0f + params.crown.radius * 0.25f;
    const float top = params.crown.centreHeight + params.crown.halfHeight + margin;
    const float side = params.crown.radius + margin;
    CHECK(tree->boundsMax.y <= top);
    CHECK(tree->boundsMax.x <= side);
    CHECK(tree->boundsMin.x >= -side);
    CHECK(tree->boundsMax.z <= side);
    CHECK(tree->boundsMin.z >= -side);
    // Roots go below zero and nothing else does.
    CHECK(tree->boundsMin.y <= 0.0f);
}

TEST_CASE("parameters round-trip through JSON and reproduce the tree", "[tree][serialisation]") {
    const TreeParams params = testParams(99);
    const nlohmann::json j = params.toJson();
    const auto restored = TreeParams::fromJson(j);
    REQUIRE(restored.has_value());

    const auto original = generateTree(params);
    const auto reloaded = generateTree(*restored);
    REQUIRE(original.has_value());
    REQUIRE(reloaded.has_value());
    // The claim being made is not "the JSON has the same fields" but "the saved file regenerates
    // the identical tree", which is quality gate 12. Only the second can be checked by hashing.
    CHECK(original->contentHash() == reloaded->contentHash());
}

TEST_CASE("generation refuses parameters it cannot honour", "[tree][validation]") {
    TreeParams p = testParams();
    p.perceptionDistance = p.occupancyRadius * 0.5f;
    CHECK_FALSE(generateTree(p).has_value());

    TreeParams q = testParams();
    q.crown.radius = 0.0f;
    CHECK_FALSE(generateTree(q).has_value());
}

TEST_CASE("the generated tree has the hierarchy the scene needs", "[tree][structure]") {
    const auto tree = generateTree(testParams(7));
    REQUIRE(tree.has_value());
    const TreeStats& s = tree->stats;
    INFO(fmt::format("nodes {} axes {} trunk {} primary {}/{} secondary {}/{} tertiary {}/{} "
                     "foliage {} roots {} shed {} height {:.2f} width {:.2f} trunkH {:.2f} baseR {:.3f} {:.1f}ms",
                     s.nodeCount, s.axisCount, s.trunkNodes, s.primaryAxes, s.primaryNodes, s.secondaryAxes,
                     s.secondaryNodes, s.tertiaryAxes, s.tertiaryNodes, s.foliageSites, s.rootStrands,
                     s.shedNodes, s.height, s.crownWidth, s.trunkHeight, s.trunkBaseRadius, s.generationMs));

    // The four tiers must all exist and must be ordered: a tree with no secondaries is a broom, and
    // a tree with more primaries than secondaries has not branched.
    CHECK(s.trunkNodes > 0);
    CHECK(s.primaryAxes > 0);
    CHECK(s.secondaryAxes > s.primaryAxes);
    CHECK(s.tertiaryAxes > s.secondaryAxes);
    CHECK(s.foliageSites > 0);
    CHECK(s.rootStrands >= tree->params.rootCount);
    CHECK(s.maxOrder >= 3);

    // The trunk must be a trunk: substantially thicker than the limbs it carries.
    float widestPrimary = 0.0f;
    for (const TreeAxis& axis : tree->axes) {
        if (axis.tier == BranchTier::Primary) {
            widestPrimary = std::max(widestPrimary, axis.baseRadius);
        }
    }
    INFO("base radius " << s.trunkBaseRadius << " widest primary " << widestPrimary);
    CHECK(s.trunkBaseRadius > widestPrimary * 1.4f);
}

TEST_CASE("animation weights increase outward through the hierarchy", "[tree][animation]") {
    const auto tree = generateTree(testParams(7));
    REQUIRE(tree.has_value());

    double sum[4] = {0, 0, 0, 0};
    int count[4] = {0, 0, 0, 0};
    for (const TreeNode& node : tree->nodes) {
        const auto tier = static_cast<int>(node.tier);
        sum[tier] += node.animationWeight;
        ++count[tier];
    }
    std::vector<double> mean;
    for (int i = 0; i < 4; ++i) {
        REQUIRE(count[i] > 0);
        mean.push_back(sum[i] / count[i]);
    }
    INFO(fmt::format("mean animation weight: trunk {:.3f} primary {:.3f} secondary {:.3f} tertiary {:.3f}",
                     mean[0], mean[1], mean[2], mean[3]));
    // The whole point of section 20 of the brief: amplitude grows outward. The trunk must be nearly
    // still, or the tree bends like grass.
    CHECK(mean[0] < mean[1]);
    CHECK(mean[1] < mean[2]);
    CHECK(mean[2] < mean[3]);
    CHECK(mean[0] < 0.12);
}

TEST_CASE("tree probe: showcase-scale generation", "[.tree-probe]") {
    // Hidden by default (the leading dot): this is a probe that prints what the shipping parameter
    // set actually produces, not an assertion about it. Run with `avgen_tests "[.tree-probe]"`.
    for (std::uint32_t seed : {1u, 2u, 3u, 4u, 5u, 6u}) {
        TreeParams p;
        p.seed = seed;
        const auto tree = generateTree(p);
        REQUIRE(tree.has_value());
        const TreeStats& s = tree->stats;
        WARN(fmt::format(
            "seed {:4}  nodes {:6}  axes {:4}  P/S/T axes {:3}/{:4}/{:5}  foliage {:5}  shed {:5}  "
            "H {:5.1f}  W {:5.1f}  boleH {:5.1f}  baseR {:5.2f}  len {:7.0f}  {:6.1f} ms",
            seed, s.nodeCount, s.axisCount, s.primaryAxes, s.secondaryAxes, s.tertiaryAxes, s.foliageSites,
            s.shedNodes, s.height, s.crownWidth, s.trunkHeight, s.trunkBaseRadius, s.totalBranchLength,
            s.generationMs));
    }
}
