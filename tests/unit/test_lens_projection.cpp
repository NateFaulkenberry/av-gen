// ADR-1540: the fisheye remap's reference mapping (scene/lens_projection.hpp), which shaders/post.wgsl's fisheyeUv
// transliterates. These hold the properties the lens depends on: off is the identity, the centre and the corners are
// fixed (the diagonal holds the whole source), the radius is monotonic (no fold), the centre is magnified and the
// edges compressed, and the blend is linear in the sample position.
#include "scene/lens_projection.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace avgen::scene;
using Catch::Approx;

namespace {
// A 16:9 camera with a 100-degree vertical field of view.
const float kTanY = std::tan(0.5f * 100.0f * 3.14159265f / 180.0f);
const float kTanX = kTanY * 16.0f / 9.0f;
} // namespace

TEST_CASE("the fisheye remap is the identity when off", "[lens][adr1540]") {
    for (float x : {0.0f, 0.13f, 0.5f, 0.92f, 1.0f}) {
        for (float y : {0.0f, 0.4f, 1.0f}) {
            const glm::vec2 uv{x, y};
            const glm::vec2 out = fisheyeSourceUv(uv, kTanX, kTanY, FisheyeModel::Stereographic, 0.0f);
            CHECK(out.x == uv.x);
            CHECK(out.y == uv.y);
        }
    }
}

TEST_CASE("the fisheye remap fixes the centre and the corners, for every model", "[lens][adr1540]") {
    for (auto model : {FisheyeModel::Stereographic, FisheyeModel::Equisolid, FisheyeModel::Equidistant}) {
        const glm::vec2 c = fisheyeSourceUv({0.5f, 0.5f}, kTanX, kTanY, model, 1.0f);
        CHECK(c.x == Approx(0.5f).margin(1e-6));
        CHECK(c.y == Approx(0.5f).margin(1e-6));
        for (glm::vec2 corner : {glm::vec2{0, 0}, glm::vec2{1, 0}, glm::vec2{0, 1}, glm::vec2{1, 1}}) {
            const glm::vec2 s = fisheyeSourceUv(corner, kTanX, kTanY, model, 1.0f);
            CHECK(s.x == Approx(corner.x).margin(1e-4));
            CHECK(s.y == Approx(corner.y).margin(1e-4));
        }
    }
}

TEST_CASE("the fisheye remap magnifies the centre, compresses the edge and never folds", "[lens][adr1540]") {
    for (auto model : {FisheyeModel::Stereographic, FisheyeModel::Equisolid, FisheyeModel::Equidistant}) {
        float prev = 0.5f;
        // Along the horizontal axis from the centre outward: the sample moves outward monotonically, and
        // stays inside the source.
        for (int i = 1; i <= 50; ++i) {
            const float x = 0.5f + 0.5f * static_cast<float>(i) / 50.0f;
            const glm::vec2 s = fisheyeSourceUv({x, 0.5f}, kTanX, kTanY, model, 1.0f);
            CHECK(s.x > prev);
            CHECK(s.x <= 1.0f + 1e-5f);
            CHECK(s.y == Approx(0.5f).margin(1e-6));
            prev = s.x;
        }
        // Near the centre an output step covers less source than itself (magnified)...
        const glm::vec2 near = fisheyeSourceUv({0.51f, 0.5f}, kTanX, kTanY, model, 1.0f);
        CHECK(near.x - 0.5f < 0.01f);
        // ...and at the mid-edge of the frame (not a corner) the sample is still inside the source: the horizontal
        // half-angle is less than the diagonal's, so the edge midpoint reads inside the frame.
        const glm::vec2 edge = fisheyeSourceUv({1.0f, 0.5f}, kTanX, kTanY, model, 1.0f);
        CHECK(edge.x < 1.0f);
        CHECK(edge.x > 0.75f);
    }
}

TEST_CASE("the fisheye amount blends the sample position linearly", "[lens][adr1540]") {
    const glm::vec2 uv{0.8f, 0.3f};
    const glm::vec2 full = fisheyeSourceUv(uv, kTanX, kTanY, FisheyeModel::Equisolid, 1.0f);
    const glm::vec2 half = fisheyeSourceUv(uv, kTanX, kTanY, FisheyeModel::Equisolid, 0.5f);
    CHECK(half.x == Approx(0.5f * (uv.x + full.x)).margin(1e-6));
    CHECK(half.y == Approx(0.5f * (uv.y + full.y)).margin(1e-6));
    CHECK(fisheyeModelFrom(-3.0f) == FisheyeModel::Stereographic);
    CHECK(fisheyeModelFrom(1.2f) == FisheyeModel::Equisolid);
    CHECK(fisheyeModelFrom(7.0f) == FisheyeModel::Equidistant);
}
