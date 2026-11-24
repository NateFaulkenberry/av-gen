// ADR-2138: a change to a procedural's `distributionTransform` alone moves the records it already has instead of
// regenerating them -- for the kinds where the transform only ever moves the records rigidly. Every case compares the
// moved object against a fresh one generated from scratch at the same transform, bit for bit (records, bounds, tight
// bounds, the kept cloud), because an offline render has to be bit-identical: "close" is a failure here.
//
// Each class is held both ways. A transform-invariant kind reuses from its second move on (the first move is the one
// that captures); a kind where the transform reaches the records any other way -- a hue field or a point op sampled at
// the world position, self-recursion, a Procedural source, a generator -- regenerates on every move, as it always did.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/grammar.hpp"
#include "scene/procedural.hpp"
#include "spatial/spline.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::scene;

namespace {

bool sameBits(const glm::vec3& a, const glm::vec3& b) {
    return std::memcmp(&a, &b, sizeof(glm::vec3)) == 0;
}

// Everything the renderer, the culler and the editor read from a rebuild.
void requireIdentical(const ProceduralGeometry& moved, const ProceduralGeometry& fresh) {
    REQUIRE(moved.instances.size() == fresh.instances.size());
    CHECK(std::memcmp(moved.instances.data(), fresh.instances.data(),
                      moved.instances.size() * sizeof(spatial::InstanceRecord)) == 0);
    CHECK(sameBits(moved.boundsMin, fresh.boundsMin));
    CHECK(sameBits(moved.boundsMax, fresh.boundsMax));
    CHECK(sameBits(moved.tightMin, fresh.tightMin));
    CHECK(sameBits(moved.tightMax, fresh.tightMax));
    CHECK(moved.meshHash == fresh.meshHash);
    CHECK(moved.builtHash == fresh.builtHash);
    CHECK(moved.cloud.count() == fresh.cloud.count());
    CHECK(moved.cloud.contentHash() == fresh.cloud.contentHash());
}

ProceduralGeometry freshAt(const ProceduralGeometry& g, const Transform& t, const GenerationContext& ctx) {
    ProceduralGeometry f = g;
    f.instances.clear();
    f.cloud.clear();
    f.structureVersion = 0;
    f.builtHash = 0;
    f.transformReuse = {};
    f.distributionTransform = t;
    f.rebuild(ctx);
    return f;
}

Transform makeTransform(glm::vec3 position, glm::vec3 eulerRadians, glm::vec3 scale) {
    Transform t;
    t.position = position;
    t.rotation = glm::quat(eulerRadians);
    t.scale = scale;
    return t;
}

// A bob, a turn, a uniform scale (compose's exact branch) and a non-uniform one (its matrix branch).
std::vector<Transform> moves() {
    return {makeTransform({3.0f, 1.25f, -2.0f}, {0.0f, 0.3f, 0.0f}, glm::vec3(1.0f)),
            makeTransform({3.0f, 1.31f, -2.0f}, {0.0f, 0.3f, 0.0f}, glm::vec3(1.0f)),
            makeTransform({-7.5f, 0.2f, 11.0f}, {0.1f, -1.2f, 0.05f}, glm::vec3(1.7f)),
            makeTransform({0.5f, -4.0f, 2.25f}, {0.0f, 2.0f, 0.4f}, glm::vec3(0.8f, 1.3f, 1.1f)),
            makeTransform({0.5f, -4.1f, 2.25f}, {0.0f, 2.0f, 0.4f}, glm::vec3(0.8f, 1.3f, 1.1f))};
}

// Moves `g` through moves(), checking each result against a fresh generation and counting what each move did.
struct MoveCounts {
    std::uint64_t regenerated = 0;
    std::uint64_t reused = 0;
};
MoveCounts moveThrough(ProceduralGeometry& g, const GenerationContext& ctx = {}) {
    g.rebuild(ctx);
    MoveCounts counts;
    for (const Transform& t : moves()) {
        const std::uint64_t rebuildsBefore = proceduralRebuildCount();
        const std::uint64_t reusesBefore = proceduralTransformReuseCount();
        const std::uint64_t version = g.structureVersion;
        g.distributionTransform = t;
        CHECK(g.rebuild(ctx));
        CHECK(g.structureVersion == version + 1); // the renderer's upload key moves either way
        counts.regenerated += proceduralRebuildCount() - rebuildsBefore;
        counts.reused += proceduralTransformReuseCount() - reusesBefore;
        requireIdentical(g, freshAt(g, t, ctx));
    }
    return counts;
}

ProceduralGeometry boxObject(DistributionKind kind) {
    ProceduralGeometry g;
    g.name = "subject";
    g.source.kind = PrimitiveKind::Box;
    g.source.size = glm::vec3(0.6f, 2.0f, 0.4f);
    g.sourceTransform.position = glm::vec3(0.0f, 1.0f, 0.0f); // off-centre, so the tight box's centre is exercised
    g.sourceTransform.scale = glm::vec3(1.2f);
    g.distribution.kind = kind;
    g.distribution.count = 9;
    g.distribution.gridCount = glm::ivec3(3, 2, 3);
    g.variation.seed = 17;
    g.variation.randomPosition = glm::vec3(0.3f, 0.1f, 0.3f);
    g.variation.randomRotation = glm::vec3(0.2f, 1.5f, 0.1f);
    g.variation.randomScale = glm::vec3(0.2f);
    g.variation.randomUniformScale = 0.25f;
    g.materialVariation.hueShift = 0.2f;
    g.materialVariation.valueRandom = 0.3f;
    g.materialVariation.emissiveRandom = 0.4f;
    g.materialVariation.emissiveSparsity = 0.3f;
    g.material.baseColor = glm::vec3(0.8f, 0.3f, 0.2f);
    g.material.emissiveColor = glm::vec3(0.1f, 0.6f, 0.9f);
    return g;
}

std::vector<Transform> pointList() {
    std::vector<Transform> pts;
    for (int i = 0; i < 6; ++i) {
        pts.push_back(makeTransform({static_cast<float>(i) * 4.0f, 0.5f * static_cast<float>(i % 2), -1.0f},
                                    {0.0f, 0.4f * static_cast<float>(i), 0.0f}, glm::vec3(1.0f + 0.1f * i)));
    }
    return pts;
}

} // namespace

TEST_CASE("transform-invariant kinds move their records instead of regenerating, bit-identically",
          "[procedural][performance][adr2138]") {
    SECTION("single, linear, grid, radial and spiral") {
        for (const DistributionKind kind : {DistributionKind::Single, DistributionKind::Linear, DistributionKind::Grid,
                                            DistributionKind::Radial, DistributionKind::Spiral}) {
            INFO("distribution " << distributionKindName(kind));
            ProceduralGeometry g = boxObject(kind);
            REQUIRE(g.transformInvariant());
            const MoveCounts c = moveThrough(g);
            CHECK(c.regenerated == 1); // the first move captures
            CHECK(c.reused == moves().size() - 1);
        }
    }
    SECTION("a points list on an imported mesh (the Neon Dreams kit's case)") {
        ProceduralGeometry g = boxObject(DistributionKind::Points);
        g.distribution.setPoints(pointList());
        auto mesh = std::make_shared<MeshData>(makeSourceMesh(g.source).value());
        for (Vertex& v : mesh->vertices) {
            v.position += glm::vec3(0.0f, 2.0f, 0.5f); // not centred on its origin
        }
        g.source.kind = PrimitiveKind::Mesh;
        g.source.asset = "kit/tower.glb";
        g.source.assetMesh = mesh;
        REQUIRE(g.transformInvariant());
        const MoveCounts c = moveThrough(g);
        CHECK(c.regenerated == 1);
        CHECK(c.reused == moves().size() - 1);
    }
    SECTION("a supplied scatter cloud") {
        ProceduralGeometry g = boxObject(DistributionKind::Scatter);
        auto cloud = std::make_shared<spatial::PointCloud>(5);
        for (std::size_t i = 0; i < 5; ++i) {
            cloud->positions()[i] = glm::vec3(static_cast<float>(i), 0.2f * static_cast<float>(i), 3.0f);
            cloud->scales()[i] = glm::vec3(0.5f + 0.1f * static_cast<float>(i));
        }
        g.distribution.scatterCloud = cloud;
        g.distribution.scatterHash = 0x5ca77e5ULL;
        const MoveCounts c = moveThrough(g);
        CHECK(c.regenerated == 1);
        CHECK(c.reused == moves().size() - 1);
    }
    SECTION("a spline distribution (the curve is in the context; moving the object does not move it)") {
        spatial::SplineSet splines;
        spatial::Spline s;
        s.name = "rail";
        s.kind = spatial::SplineKind::Polyline;
        s.generator = spatial::SplineGenerator::Line;
        s.start = glm::vec3(0.0f);
        s.end = glm::vec3(2.0f, 1.0f, 10.0f);
        s.count = 8;
        splines.splines.push_back(s);
        ProceduralGeometry g = boxObject(DistributionKind::Spline);
        g.distribution.spline = "rail";
        g.distribution.alignToSpline = true;
        const GenerationContext ctx{nullptr, &splines, 0};
        const MoveCounts c = moveThrough(g, ctx);
        CHECK(c.regenerated == 1);
        CHECK(c.reused == moves().size() - 1);
    }
    SECTION("a grammar") {
        ProceduralGeometry g = boxObject(DistributionKind::Grammar);
        g.grammar.axiom = "row";
        GrammarRule row;
        row.name = "row";
        row.op = GrammarOp::Repeat;
        row.count = 4;
        row.step.position = glm::vec3(0.0f, 0.5f, 1.5f);
        row.step.rotation = glm::quat(glm::vec3(0.0f, 0.3f, 0.0f));
        row.children = {"p"};
        GrammarRule place;
        place.name = "p";
        place.op = GrammarOp::Place;
        g.grammar.rules = {row, place};
        g.extraLane = "depth"; // a column only the grammar writes rides through untouched
        REQUIRE(g.validate().has_value());
        const MoveCounts c = moveThrough(g);
        CHECK(c.regenerated == 1);
        CHECK(c.reused == moves().size() - 1);
    }
}

TEST_CASE("where the transform reaches the records another way, every move regenerates as before",
          "[procedural][performance][adr2138]") {
    SECTION("a hue field is sampled at the world position") {
        ProceduralGeometry g = boxObject(DistributionKind::Grid);
        g.materialVariation.hueField = 0.5f;
        CHECK_FALSE(g.transformInvariant());
        const MoveCounts c = moveThrough(g);
        CHECK(c.regenerated == moves().size());
        CHECK(c.reused == 0);
    }
    SECTION("a point op may sample fields or noise at the world position") {
        ProceduralGeometry g = boxObject(DistributionKind::Grid);
        spatial::PointOp noise;
        noise.kind = spatial::PointOpKind::Noise;
        noise.amount = 0.5f;
        g.pointOps.push_back(noise);
        CHECK_FALSE(g.transformInvariant());
        const MoveCounts c = moveThrough(g);
        CHECK(c.regenerated == moves().size());
        CHECK(c.reused == 0);
    }
    SECTION("self-recursion") {
        ProceduralGeometry g = boxObject(DistributionKind::Radial);
        g.hierarchy.recursionDepth = 2;
        g.hierarchy.scalePerLevel = 0.6f;
        g.hierarchy.offsetPerLevel = glm::vec3(0.0f, 1.0f, 0.0f);
        CHECK_FALSE(g.transformInvariant());
        const MoveCounts c = moveThrough(g);
        CHECK(c.regenerated == moves().size());
        CHECK(c.reused == 0);
    }
    SECTION("a Procedural source (the referenced object's cloud under every point)") {
        std::vector<ProceduralGeometry> objects;
        objects.push_back(boxObject(DistributionKind::Linear));
        objects.back().name = "leaf";
        ProceduralGeometry g = boxObject(DistributionKind::Radial);
        g.source.kind = PrimitiveKind::Procedural;
        g.source.reference = "leaf";
        CHECK_FALSE(g.transformInvariant());
        const GenerationContext ctx{&objects, nullptr, 0};
        const MoveCounts c = moveThrough(g, ctx);
        CHECK(c.regenerated == moves().size());
        CHECK(c.reused == 0);
    }
    SECTION("a generator (its records are written on the GPU)") {
        ProceduralGeometry g = boxObject(DistributionKind::Generator);
        CHECK_FALSE(g.transformInvariant());
        const MoveCounts c = moveThrough(g);
        CHECK(c.regenerated == moves().size());
        CHECK(c.reused == 0);
    }
}

TEST_CASE("the reuse checks the inputs no hash covers, and steps aside when one moved",
          "[procedural][performance][adr2138]") {
    ProceduralGeometry g = boxObject(DistributionKind::Points);
    g.distribution.setPoints(pointList());
    auto mesh = std::make_shared<MeshData>(makeSourceMesh(g.source).value());
    g.source.kind = PrimitiveKind::Mesh;
    g.source.asset = "kit/tower.glb";
    g.source.assetMesh = mesh;
    const std::vector<Transform> t = moves();
    g.rebuild();
    g.distributionTransform = t[0];
    g.rebuild(); // captures
    g.distributionTransform = t[1];
    const std::uint64_t reusesBefore = proceduralTransformReuseCount();
    g.rebuild();
    REQUIRE(proceduralTransformReuseCount() == reusesBefore + 1); // armed

    const auto movesAndRegenerates = [&](const Transform& to) {
        const std::uint64_t rebuildsBefore = proceduralRebuildCount();
        g.distributionTransform = to;
        CHECK(g.rebuild());
        CHECK(proceduralRebuildCount() == rebuildsBefore + 1);
        requireIdentical(g, freshAt(g, to, {}));
    };
    SECTION("the material colour the hue multipliers were made from (not structural, but in the records)") {
        g.material.baseColor = glm::vec3(0.2f, 0.9f, 0.4f);
        movesAndRegenerates(t[2]);
    }
    SECTION("the imported mesh, swapped for another under the same asset path (its bounds differ)") {
        auto bigger = std::make_shared<MeshData>(*mesh);
        for (Vertex& v : bigger->vertices) {
            v.position *= 3.0f;
        }
        g.source.assetMesh = bigger;
        movesAndRegenerates(t[2]);
    }
    SECTION("someone else wrote the records (a floating layer bumps structureVersion itself)") {
        g.instances[0].position.x += 100.0f;
        ++g.structureVersion;
        movesAndRegenerates(t[2]);
    }
    SECTION("a structural change in the same frame as the move") {
        g.variation.seed = 99;
        movesAndRegenerates(t[2]);
    }
    SECTION("and once regenerated it reuses again from the next move") {
        g.material.baseColor = glm::vec3(0.2f, 0.9f, 0.4f);
        movesAndRegenerates(t[2]);
        const std::uint64_t before = proceduralTransformReuseCount();
        g.distributionTransform = t[3];
        g.rebuild();
        CHECK(proceduralTransformReuseCount() == before + 1);
        requireIdentical(g, freshAt(g, t[3], {}));
    }
}

// The composition end to end: a procedural node whose position moves every frame (as a bob does) regenerates once
// and then only moves.
TEST_CASE("a procedural node moved every frame regenerates once, then only moves its records",
          "[scene][composition][performance][adr2138]") {
    assets::AssetRegistry registry;
    registry.setBaseDirectory(testsupport::processTempDir());
    params::ParameterSet params;
    params::Modulator modulator;
    Composition comp(registry, "composition");
    comp.attach(params, modulator);

    CompositionNode node;
    node.name = "grid";
    node.kind = NodeKind::Procedural;
    node.procedural = boxObject(DistributionKind::Grid);
    node.procedural.name = "grid";
    node.transform.position = glm::vec3(3.0f, 1.0f, -2.0f);
    REQUIRE(comp.addNode(std::move(node)));

    FrameTime time{};
    params.resetFinals();
    comp.update(time);
    REQUIRE(comp.scene().procedurals.size() == 1);
    auto* position = params.findAs<glm::vec3>("nodes/grid/position");
    REQUIRE(position != nullptr);

    std::uint64_t regenerated = 0;
    std::uint64_t reused = 0;
    for (int frame = 0; frame < 10; ++frame) {
        position->setBase(glm::vec3(3.0f, 1.0f + 0.05f * static_cast<float>(frame + 1), -2.0f));
        params.resetFinals();
        const std::uint64_t rebuildsBefore = proceduralRebuildCount();
        const std::uint64_t reusesBefore = proceduralTransformReuseCount();
        comp.update(time);
        regenerated += proceduralRebuildCount() - rebuildsBefore;
        reused += proceduralTransformReuseCount() - reusesBefore;
        const ProceduralGeometry& live = comp.scene().procedurals[0];
        requireIdentical(live, freshAt(live, live.distributionTransform,
                                       GenerationContext{&comp.scene().procedurals, &comp.scene().splines, 0}));
    }
    CHECK(regenerated == 1);
    CHECK(reused == 9);
}
