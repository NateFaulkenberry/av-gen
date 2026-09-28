// ADR-950 (QA pass W1): the ground a sightline marches over, and what one costs.
//
// `heroSightline` asks "where is the ground" at every step of nine rays. It used to ask with
// `WorldMap::sample`, which also derives a normal (four more height evaluations), slope, altitude
// and moisture, and then used only `height` and `waterSurface`. Since ADR-834 publishes a
// `visibility` for every in-shot character on every frame, that waste was most of Glowmere Valley
// 3's main thread on its wide shots (docs/qa-pass/perf.md).
//
// The fix asks for the two fields directly. It is only a fix if the answer is bit-identical, and it
// is bit-identical exactly when `sample(p).height == height(p)` and `sample(p).waterSurface ==
// waterSurface(p)` for every p -- so that is what the first case pins, on a generated terrain and
// on a map with two overlapping waters, where `waterSurface` is not trivially the sea level.
//
// The second case is a hidden benchmark (`[.perf]`): it prints what one sightline costs at three
// distances, and a digest of every result so two builds can be compared for identical output.

#include "world/camera_clearance.hpp"
#include "world/world_composer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace avgen;

namespace {

std::uint32_t bits(float f) {
    std::uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

world::WorldMap generatedMap() {
    world::WorldRecipe recipe;
    recipe.world = "sightline";
    recipe.seed = 20260917u;
    recipe.extent = 600.0f;
    return world::terrainFor(recipe);
}

world::WorldMap watersMap() {
    world::WorldMap map;
    map.size = {200.0f, 200.0f};
    map.baseHeight = 5.0f;
    world::Feature river;
    river.name = "river";
    river.kind = world::FeatureKind::River;
    river.path = {{-20.0f, 0.0f, -100.0f}, {10.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 100.0f}};
    river.width = 26.0f;
    river.amplitude = 3.6f;
    river.water = true;
    map.features.push_back(river);
    world::Feature pool;
    pool.name = "pool";
    pool.kind = world::FeatureKind::Flat;
    pool.path = {{0.0f, 1.1f, 0.0f}};
    pool.width = 36.0f;
    pool.flatten = 0.8f;
    pool.falloff = 0.9f;
    pool.water = true;
    pool.waterDepth = 1.3f;
    map.features.push_back(pool);
    map.prepare();
    return map;
}

// Returns how many lattice points had water above the ground, so the caller can prove the lattice
// reached the case `waterSurface` exists for.
int requireSampleAgrees(const world::WorldMap& map, int n) {
    int wet = 0;
    for (int j = 0; j <= n; ++j) {
        for (int i = 0; i <= n; ++i) {
            // Off the lattice's own axes by an irrational fraction, so points do not all land on the
            // grid a cache or a feature's bounding box would be aligned with.
            const glm::vec2 p = map.min() + map.size * glm::vec2((i + 0.318f) / (n + 1.0f), (j + 0.577f) / (n + 1.0f));
            const world::Sample s = map.sample(p, 0.5f);
            const float h = map.height(p);
            const float w = map.waterSurface(p);
            if (bits(s.height) != bits(h) || bits(s.waterSurface) != bits(w)) {
                FAIL_CHECK("sample disagrees with height/waterSurface at (" << p.x << ", " << p.y << ")");
                return wet;
            }
            wet += w > h ? 1 : 0;
        }
    }
    return wet;
}

} // namespace

TEST_CASE("a sightline's ground from height and waterSurface is sample's ground, bit for bit",
          "[unit][world][sightline][adr950]") {
    SECTION("a generated terrain") {
        const world::WorldMap map = generatedMap();
        requireSampleAgrees(map, 60);
    }
    SECTION("two overlapping waters, where the water line is not the sea level") {
        const world::WorldMap map = watersMap();
        REQUIRE(map.validate().has_value());
        const int wet = requireSampleAgrees(map, 80);
        INFO("lattice points under water: " << wet);
        REQUIRE(wet > 20); // the control: the lattice did reach the water it is meant to test
    }
}

TEST_CASE("what one hero sightline costs, by distance", "[.perf][sightline][adr950]") {
    const world::WorldMap map = generatedMap();
    world::ClearanceField field;
    field.map = &map;
    const glm::vec2 c = map.min() + map.size * 0.5f;
    std::uint64_t digest = 1469598103934665603ull;
    const auto mix = [&digest](float f) { digest = (digest ^ bits(f)) * 1099511628211ull; };
    for (const float distance : {20.0f, 60.0f, 200.0f}) {
        constexpr int kSubjects = 24;
        double totalUs = 0.0;
        for (int k = 0; k < kSubjects; ++k) {
            const float a = 0.9f + 0.26f * static_cast<float>(k);
            const glm::vec2 at = c + glm::vec2(std::cos(a * 1.7f), std::sin(a * 1.3f)) * 120.0f;
            world::SubjectCapsule subject;
            subject.name = "subject";
            subject.position = glm::vec3(at.x, map.height(at), at.y);
            subject.height = 1.8f;
            subject.radius = 0.36f;
            const glm::vec2 e = at + glm::vec2(std::cos(a), std::sin(a)) * distance;
            const glm::vec3 eye(e.x, map.height(e) + 6.0f, e.y);
            const auto t0 = std::chrono::steady_clock::now();
            const world::Sightline s = world::heroSightline(field, eye, subject, 2.0f);
            totalUs += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
            mix(s.visible);
            mix(s.requiredLift);
            mix(s.requiredPush.x);
            mix(s.requiredPush.y);
            mix(s.canopyMetres);
        }
        std::printf("heroSightline @ %5.0f m: %8.1f us per call (mean of %d)\n", distance, totalUs / kSubjects,
                    kSubjects);
    }
    std::printf("heroSightline result digest: %016llx\n", static_cast<unsigned long long>(digest));
    SUCCEED();
}
