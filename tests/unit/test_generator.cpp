// ADR-1117 / ADR-1118: the generator distribution's CPU mirror, its scene-file form and the points list.
#include "params/parameter_set.hpp"
#include "scene/generator.hpp"
#include "scene/procedural.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <set>
#include <utility>

using namespace avgen;
using namespace avgen::scene;
using Catch::Approx;

namespace {

GeneratorSpec meadow() {
    GeneratorSpec g;
    g.cellSize = 0.5f;
    g.viewDistance = 20.0f;
    g.presence = 0.6f;
    g.jitter = 0.8f;
    g.sizeMin = 0.5f;
    g.sizeMax = 1.5f;
    g.tilt = 0.2f;
    g.groundAmplitude = 3.0f;
    g.groundFrequency = 0.05f;
    return g;
}

} // namespace

TEST_CASE("A generator cell is a pure function of its integer coordinates and the seed", "[unit][generator]") {
    const GeneratorSpec g = meadow();
    const GeneratorVariation v{0.2f, 0.3f, 0.1f};
    int present = 0;
    int differ = 0;
    for (int iz = -40; iz < 40; ++iz) {
        for (int ix = -40; ix < 40; ++ix) {
            const auto a = generatorElement(g, 7u, v, ix, iz);
            const auto b = generatorElement(g, 7u, v, ix, iz);
            REQUIRE(a.has_value() == b.has_value());
            if (a) {
                ++present;
                CHECK(a->position == b->position);
                CHECK(a->random == b->random);
                // Inside its own cell (jitter < 1) and on the ground.
                CHECK(std::floor(a->position.x / g.cellSize) == static_cast<float>(ix));
                CHECK(std::floor(a->position.z / g.cellSize) == static_cast<float>(iz));
                CHECK(a->position.y == Approx(generatorGroundHeight(g, 7u, a->position.x, a->position.z)));
                CHECK(a->size >= g.sizeMin);
                CHECK(a->size <= g.sizeMax);
            }
            differ += a.has_value() != generatorElement(g, 8u, v, ix, iz).has_value() ? 1 : 0;
        }
    }
    // Presence 0.6 over 6,400 cells.
    CHECK(present > 3600);
    CHECK(present < 4100);
    CHECK(differ > 1000); // a different seed is a different world
    CHECK(generatorFloorDiv(-1, 4) == -1);
    CHECK(generatorFloorDiv(-4, 4) == -1);
    CHECK(generatorFloorDiv(-5, 4) == -2);
    CHECK(generatorFloorDiv(3, 4) == 0);
}

TEST_CASE("Clusters make regions of agreement, and contrast empties the barren ones", "[unit][generator]") {
    GeneratorSpec g = meadow();
    g.presence = 1.0f;
    g.clusterSize = 20.0f; // 40 cells
    g.clusterContrast = 1.0f;
    // Count presence per 40x40 cluster block over a large area: the blocks must disagree strongly.
    int emptyish = 0;
    int full = 0;
    for (int bz = 0; bz < 6; ++bz) {
        for (int bx = 0; bx < 6; ++bx) {
            int n = 0;
            for (int z = 0; z < 40; z += 4) {
                for (int x = 0; x < 40; x += 4) {
                    n += generatorElement(g, 3u, {}, bx * 40 + x, bz * 40 + z) ? 1 : 0;
                }
            }
            emptyish += n < 30 ? 1 : 0;
            full += n > 70 ? 1 : 0;
        }
    }
    CHECK(emptyish > 0);
    CHECK(full > 0);
}

TEST_CASE("A bounded generator exists only in its region, and its window clamps to it", "[unit][generator]") {
    GeneratorSpec g = meadow();
    g.bounded = true;
    g.regionMin = {-10.0f, -5.0f};
    g.regionMax = {10.0f, 5.0f};
    const GeneratorRegionCells r = generatorRegionCells(g);
    CHECK(r.minX == -20);
    CHECK(r.maxX == 19);
    CHECK(r.minZ == -10);
    CHECK(r.maxZ == 9);
    CHECK_FALSE(generatorElement(g, 1u, {}, -21, 0).has_value());
    CHECK_FALSE(generatorElement(g, 1u, {}, 0, 10).has_value());
    // A camera far outside sees an empty window; one at the centre sees the whole region.
    CHECK(generatorWindow(g, glm::vec3(500.0f, 0.0f, 0.0f)).cells() == 0);
    const GeneratorWindow w = generatorWindow(g, glm::vec3(0.0f));
    CHECK(w.countX == 40);
    CHECK(w.countZ == 20);
    CHECK(generatorCapacity(g).cells() == 800);
    // The disc trims the corners.
    g.regionRadius = 5.0f;
    CHECK_FALSE(generatorElement(g, 1u, {}, -19, -9).has_value());
    // The present count is the elements the region query finds.
    const auto found = generatorQueryRegion(g, 1u, {}, g.regionMin, g.regionMax);
    CHECK(found.size() == generatorPresentCount(g, 1u, w));
}

TEST_CASE("An unbounded window follows the camera and never grows", "[unit][generator]") {
    const GeneratorSpec g = meadow();
    const std::uint64_t capacity = generatorCapacity(g).cells();
    for (const glm::vec3 camera : {glm::vec3(0.0f), glm::vec3(1234.5f, 3.0f, -987.25f), glm::vec3(-1.0e6f, 0.0f, 3.0e5f)}) {
        const GeneratorWindow w = generatorWindow(g, camera);
        CHECK(w.cells() == capacity);
        CHECK(w.originX <= static_cast<std::int32_t>(std::floor(camera.x / g.cellSize)));
        CHECK(w.originX + w.countX > static_cast<std::int32_t>(std::floor(camera.x / g.cellSize)));
    }
    // The live lever shrinks the window, never the world.
    CHECK(generatorWindow(g, glm::vec3(0.0f), 0.5f).cells() < capacity);
}

TEST_CASE("The mirror picks the element a ray hits without materialising the population", "[unit][generator]") {
    const GeneratorSpec g = meadow();
    // Find a present element near the origin and shoot straight down at it.
    std::optional<GeneratedElement> target;
    for (int ix = 0; ix < 20 && !target; ++ix) {
        target = generatorElement(g, 5u, {}, ix, 3);
    }
    REQUIRE(target.has_value());
    const glm::vec3 above = target->position + glm::vec3(0.0f, 30.0f, 0.0f);
    const auto hit = generatorRaycast(g, 5u, {}, above, glm::vec3(0.0f, -1.0f, 0.0f), 100.0f, 0.2f);
    REQUIRE(hit.has_value());
    CHECK(hit->element.ix == target->ix);
    CHECK(hit->element.iz == target->iz);
    CHECK(hit->distance == Approx(30.0f - 0.2f * target->size).margin(1e-3));
    // A ray into the sky hits nothing.
    CHECK_FALSE(generatorRaycast(g, 5u, {}, above, glm::vec3(0.0f, 1.0f, 0.0f), 100.0f, 0.2f).has_value());
}

TEST_CASE("A generator distribution round-trips, refuses what it cannot hold and rebuilds no CPU records",
          "[unit][generator]") {
    ProceduralGeometry pg;
    pg.name = "meadow";
    pg.source.kind = PrimitiveKind::Cylinder;
    pg.distribution.kind = DistributionKind::Generator;
    pg.distribution.generator = meadow();
    pg.distribution.generator.bounded = true;
    pg.distribution.generator.regionMin = {-30.0f, -30.0f};
    pg.distribution.generator.regionMax = {30.0f, 30.0f};
    REQUIRE(pg.validate().has_value());
    const nlohmann::json j = pg.toJson();
    REQUIRE(j.at("distribution").contains("generator"));
    auto back = ProceduralGeometry::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->distribution.kind == DistributionKind::Generator);
    CHECK(back->distribution.generator.hash() == pg.distribution.generator.hash());
    CHECK(back->structuralHash() == pg.structuralHash());

    REQUIRE(pg.rebuild());
    CHECK(pg.instances.empty());
    CHECK(pg.meshHash != 0);
    CHECK(pg.boundsMax.x >= 30.0f); // the region is the bounds

    // Presence is a per-frame uniform: it does not change the structure.
    ProceduralGeometry denser = pg;
    denser.distribution.generator.presence = 0.9f;
    CHECK(denser.structuralHash() == pg.structuralHash());
    ProceduralGeometry finer = pg;
    finer.distribution.generator.cellSize = 0.25f;
    CHECK(finer.structuralHash() != pg.structuralHash());

    ProceduralGeometry bad = pg;
    bad.distribution.generator.bounded = false;
    bad.distribution.generator.cellSize = 0.01f;
    bad.distribution.generator.viewDistance = 500.0f; // 100k x 100k cells
    CHECK_FALSE(bad.validate().has_value());
    bad = pg;
    bad.distribution.generator.name = "meadow";
    CHECK_FALSE(bad.validate().has_value());
    bad = pg;
    bad.hierarchy.recursionDepth = 1;
    CHECK_FALSE(bad.validate().has_value());

    // Parameters: registered for a generator, and they reach the live object.
    params::ParameterSet params;
    const ProceduralParameters p = registerProceduralParameters(params, pg, "procedural/meadow/");
    REQUIRE(params.find("procedural/meadow/distribution/generator/presence") != nullptr);
    params.findAs<float>("procedural/meadow/distribution/generator/presence")->setBase(0.25f);
    params.findAs<float>("procedural/meadow/distribution/generator/presence")->resetFinal();
    ProceduralGeometry live = pg;
    applyProceduralParameterValues(p, pg, live);
    CHECK(live.distribution.generator.presence == Approx(0.25f));
}

TEST_CASE("A points distribution is serialised placements", "[unit][generator]") {
    ProceduralGeometry pg;
    pg.name = "baked";
    pg.source.kind = PrimitiveKind::Box;
    pg.distribution.kind = DistributionKind::Points;
    std::vector<Transform> list(3);
    list[1].position = {1.0f, 2.0f, 3.0f};
    list[2].scale = glm::vec3(2.0f);
    list[2].rotation = glm::angleAxis(0.7f, glm::vec3(0.0f, 1.0f, 0.0f));
    pg.distribution.setPoints(list);
    REQUIRE(pg.validate().has_value());
    auto back = ProceduralGeometry::fromJson(pg.toJson());
    REQUIRE(back.has_value());
    REQUIRE(back->distribution.points);
    CHECK(back->distribution.points->size() == 3);
    CHECK(back->distribution.pointsHash == pg.distribution.pointsHash);
    REQUIRE(back->rebuild());
    REQUIRE(back->instances.size() == 3);
    CHECK(glm::vec3(back->instances[1].position) == glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(back->instances[2].scale.x == Approx(2.0f));
}

#include "assets/asset_registry.hpp"
#include "scene/composition.hpp"

#include <filesystem>

TEST_CASE("Bake to points turns a generator region into a saved, editable points node", "[unit][generator]") {
    assets::AssetRegistry registry{std::filesystem::temp_directory_path()};
    const nlohmann::json doc = nlohmann::json::parse(R"({
      "format": "avgen-scene", "version": 1, "name": "bake",
      "nodes": [{"name": "reeds", "kind": "procedural", "procedural": {
        "source": {"kind": "cylinder", "radius": 0.05, "height": 1.0},
        "variation": {"seed": 77},
        "distribution": {"kind": "generator", "generator": {
          "cellSize": 0.5, "viewDistance": 40.0, "presence": 0.5, "bounded": true,
          "regionMin": [-10, -10], "regionMax": [10, 10], "groundAmplitude": 1.0}}}}]})");
    auto loaded = Composition::fromJson(doc, registry);
    REQUIRE(loaded.has_value());
    Composition& comp = **loaded;
    const CompositionNode* reeds = comp.findNode("reeds");
    REQUIRE(reeds != nullptr);
    REQUIRE(reeds->procedural.isGenerator());
    const auto expected = generatorQueryRegion(reeds->procedural.distribution.generator, 77u, {}, glm::vec2(-5.0f),
                                               glm::vec2(5.0f));
    auto baked = comp.bakeGeneratorToPoints("reeds", glm::vec2(0.0f), 5.0f);
    REQUIRE(baked.has_value());
    const CompositionNode* node = comp.findNode(*baked);
    REQUIRE(node != nullptr);
    CHECK(node->procedural.distribution.kind == DistributionKind::Points);
    REQUIRE(node->procedural.distribution.points);
    CHECK(node->procedural.distribution.points->size() == expected.size());
    CHECK_FALSE(comp.findNode("reeds")->visible);
    CHECK_FALSE(comp.bakeGeneratorToPoints(*baked, glm::vec2(0.0f), 5.0f).has_value()); // not a generator
    // It saves and loads as points, with every placement.
    auto again = Composition::fromJson(comp.toJson(), registry);
    REQUIRE(again.has_value());
    const CompositionNode* reloaded = (*again)->findNode(*baked);
    REQUIRE(reloaded != nullptr);
    REQUIRE(reloaded->procedural.distribution.points);
    CHECK(reloaded->procedural.distribution.points->size() == expected.size());
    CHECK((*(reloaded->procedural.distribution.points))[0].position == expected[0].position);
}
