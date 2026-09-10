// Placing assets by hand (ADR-069). The layout maths is separated from the UI precisely so these
// questions have answers without a window: does a brush respect its own spacing, is a cluster
// reproducible from its seed, does a brush painted on a hillside land on the hillside.

#include "app/placement.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>

using namespace avgen;
using app::Placement;
using app::PlacementMode;
using app::PlacementSettings;

namespace {
float closestPair(const std::vector<Placement>& ps) {
    float best = 1e9f;
    for (std::size_t i = 0; i < ps.size(); ++i) {
        for (std::size_t j = i + 1; j < ps.size(); ++j) {
            best = std::min(best, glm::length(ps[i].position - ps[j].position));
        }
    }
    return best;
}
} // namespace

TEST_CASE("A single placement is one object where you clicked", "[placement]") {
    PlacementSettings settings;
    settings.mode = PlacementMode::Single;
    settings.scaleJitter = 0.0f;
    const glm::vec3 where(3.0f, 1.5f, -7.0f);
    const auto plan = app::planPlacements(settings, where, glm::vec3(0.0f, 1.0f, 0.0f), 5u);
    REQUIRE(plan.size() == 1);
    CHECK_THAT(glm::length(plan[0].position - where), Catch::Matchers::WithinAbs(0.0, 1e-6));
    CHECK_THAT(plan[0].scale, Catch::Matchers::WithinAbs(1.0, 1e-6));
}

TEST_CASE("Sink pushes a placement into the ground along the surface", "[placement]") {
    PlacementSettings settings;
    settings.mode = PlacementMode::Single;
    settings.sink = 0.4f;
    const glm::vec3 where(0.0f, 10.0f, 0.0f);
    const auto flat = app::planPlacements(settings, where, glm::vec3(0.0f, 1.0f, 0.0f), 1u);
    REQUIRE(flat.size() == 1);
    CHECK_THAT(flat[0].position.y, Catch::Matchers::WithinAbs(9.6, 1e-5));

    // Into the *surface*, not straight down. On a wall, sinking downward would leave the object
    // hanging in the air in front of it.
    const glm::vec3 wall = glm::normalize(glm::vec3(1.0f, 0.0f, 0.0f));
    const auto onWall = app::planPlacements(settings, where, wall, 1u);
    REQUIRE(onWall.size() == 1);
    CHECK_THAT(onWall[0].position.x, Catch::Matchers::WithinAbs(-0.4, 1e-5));
    CHECK_THAT(onWall[0].position.y, Catch::Matchers::WithinAbs(10.0, 1e-5));
}

TEST_CASE("A brush respects its own spacing and stays inside its radius", "[placement]") {
    PlacementSettings settings;
    settings.mode = PlacementMode::Brush;
    settings.brushRadius = 6.0f;
    settings.spacing = 1.5f;
    const glm::vec3 centre(0.0f, 0.0f, 0.0f);
    const auto plan = app::planPlacements(settings, centre, glm::vec3(0.0f, 1.0f, 0.0f), 42u);

    REQUIRE(plan.size() > 4);
    // The whole reason for dart throwing rather than a uniform scatter: a uniform scatter over a
    // disc clumps, and clumping is the one thing a brush must not do, because the user is already
    // deciding where the density goes by moving the mouse.
    CHECK(closestPair(plan) >= settings.spacing - 1e-4f);
    for (const Placement& p : plan) {
        CHECK(glm::length(p.position - centre) <= settings.brushRadius + 1e-3f);
    }
}

TEST_CASE("A tighter spacing fits more in the same brush", "[placement]") {
    PlacementSettings settings;
    settings.mode = PlacementMode::Brush;
    settings.brushRadius = 8.0f;

    settings.spacing = 3.0f;
    const auto sparse = app::planPlacements(settings, glm::vec3(0.0f), glm::vec3(0, 1, 0), 7u);
    settings.spacing = 1.0f;
    const auto dense = app::planPlacements(settings, glm::vec3(0.0f), glm::vec3(0, 1, 0), 7u);
    CHECK(dense.size() > sparse.size());
    CHECK(closestPair(dense) >= 1.0f - 1e-4f);
    CHECK(closestPair(sparse) >= 3.0f - 1e-4f);
}

TEST_CASE("A brush paints along the surface, not along the world floor", "[placement]") {
    PlacementSettings settings;
    settings.mode = PlacementMode::Brush;
    settings.brushRadius = 5.0f;
    settings.spacing = 1.0f;

    // A 45-degree hillside. Painting on the XZ plane instead of the surface would put half the
    // brush underground and half in the air; on the surface, every placement stays on the plane.
    const glm::vec3 normal = glm::normalize(glm::vec3(0.0f, 1.0f, 1.0f));
    const glm::vec3 centre(0.0f, 4.0f, 0.0f);
    const auto plan = app::planPlacements(settings, centre, normal, 11u);
    REQUIRE(plan.size() > 4);
    for (const Placement& p : plan) {
        INFO("at " << p.position.x << "," << p.position.y << "," << p.position.z);
        CHECK_THAT(glm::dot(p.position - centre, normal), Catch::Matchers::WithinAbs(0.0, 1e-4));
    }
}

TEST_CASE("A cluster is a few things together and fills its disc evenly", "[placement]") {
    PlacementSettings settings;
    settings.mode = PlacementMode::Cluster;
    settings.clusterCount = 200;   // many, so the distribution is measurable
    settings.clusterRadius = 4.0f;
    const auto plan = app::planPlacements(settings, glm::vec3(0.0f), glm::vec3(0, 1, 0), 3u);
    REQUIRE(plan.size() == 200);

    // sqrt-distributed, so the disc fills evenly. Without it a cluster is dense in the middle and
    // thins toward its edge, which reads as a target rather than a patch of something growing: half
    // the area is outside r/sqrt(2), so about half the instances should be.
    const float half = settings.clusterRadius / std::sqrt(2.0f);
    const auto outer = std::count_if(plan.begin(), plan.end(), [&](const Placement& p) {
        return glm::length(p.position) > half;
    });
    const double fraction = static_cast<double>(outer) / static_cast<double>(plan.size());
    CHECK(fraction > 0.38);
    CHECK(fraction < 0.62);
    for (const Placement& p : plan) {
        CHECK(glm::length(p.position) <= settings.clusterRadius + 1e-3f);
    }
}

TEST_CASE("Placement is reproducible from its seed", "[placement]") {
    PlacementSettings settings;
    settings.mode = PlacementMode::Brush;
    settings.brushRadius = 5.0f;
    const auto a = app::planPlacements(settings, glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(0, 1, 0), 99u);
    const auto b = app::planPlacements(settings, glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(0, 1, 0), 99u);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK_THAT(glm::length(a[i].position - b[i].position), Catch::Matchers::WithinAbs(0.0, 1e-9));
        CHECK_THAT(a[i].yaw, Catch::Matchers::WithinAbs(static_cast<double>(b[i].yaw), 1e-9));
        CHECK_THAT(a[i].scale, Catch::Matchers::WithinAbs(static_cast<double>(b[i].scale), 1e-9));
    }
    const auto other = app::planPlacements(settings, glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(0, 1, 0), 100u);
    // A different seed is a different brush stroke, or the seed is decoration.
    bool differs = other.size() != a.size();
    for (std::size_t i = 0; i < a.size() && i < other.size() && !differs; ++i) {
        differs = glm::length(a[i].position - other[i].position) > 1e-4f;
    }
    CHECK(differs);
}

TEST_CASE("A landmark is enormous on top of its jitter", "[placement]") {
    PlacementSettings settings;
    settings.mode = PlacementMode::Landmark;
    settings.landmarkScale = 6.0f;
    settings.scaleJitter = 0.0f;
    const auto plan = app::planPlacements(settings, glm::vec3(0.0f), glm::vec3(0, 1, 0), 2u);
    REQUIRE(plan.size() == 1);
    CHECK_THAT(plan[0].scale, Catch::Matchers::WithinRel(6.0f, 1e-5f));

    // With jitter it is still about six times, not six times *instead of* the variation: a landmark
    // is a different decision from the population, not a big member of it.
    settings.scaleJitter = 0.2f;
    const auto jittered = app::planPlacements(settings, glm::vec3(0.0f), glm::vec3(0, 1, 0), 2u);
    CHECK(jittered[0].scale > 6.0f * 0.79f);
    CHECK(jittered[0].scale < 6.0f * 1.21f);
}

TEST_CASE("Scale normalises an asset to the height the library says it wants", "[placement]") {
    assets::AssetDescriptor asset;
    asset.id = "tree";
    asset.naturalSize = glm::vec3(0.4f, 2.0f, 0.4f);
    asset.preferredScale = 9.0f;
    CHECK_THAT(app::normalisingScale(asset), Catch::Matchers::WithinRel(4.5f, 1e-5f));
    CHECK_THAT(app::normalisingScale(asset, 20.0f), Catch::Matchers::WithinRel(10.0f, 1e-5f));

    // An asset with no authored height keeps its own size, which is the documented fallback.
    assets::AssetDescriptor plain;
    plain.naturalSize = glm::vec3(1.0f, 3.0f, 1.0f);
    CHECK_THAT(app::normalisingScale(plain), Catch::Matchers::WithinRel(1.0f, 1e-5f));

    // Degenerate bounds do not produce an infinity that silently becomes an invisible or
    // universe-sized object.
    assets::AssetDescriptor flat;
    flat.naturalSize = glm::vec3(1.0f, 0.0f, 1.0f);
    flat.preferredScale = 5.0f;
    CHECK_THAT(app::normalisingScale(flat), Catch::Matchers::WithinAbs(1.0, 1e-6));
}

TEST_CASE("Placed nodes get names nobody has to invent", "[placement]") {
    std::vector<std::string> taken;
    CHECK(app::uniquePlacementName("fern", taken) == "fern");
    taken.emplace_back("fern");
    CHECK(app::uniquePlacementName("fern", taken) == "fern_1");
    taken.emplace_back("fern_1");
    CHECK(app::uniquePlacementName("fern", taken) == "fern_2");
    // An empty id still produces a legal name rather than an empty one, which addNode would rename
    // anyway and less predictably.
    CHECK(!app::uniquePlacementName("", taken).empty());
}
