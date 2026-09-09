// Lighting maths without a GPU (ADR-033/034): colour temperature, light packing, the froxel grid
// against a brute-force reference, the cascade fit against the frustum it is supposed to cover,
// area-light irradiance, and the linearly-transformed-cone table.

#include "rendering/light_data.hpp"
#include "rendering/shadow_math.hpp"
#include "scene/scene_types.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

using namespace avgen;
using Catch::Approx;

namespace {

glm::mat4 cameraViewProj(const glm::vec3& eye, const glm::vec3& target, float fovY, float aspect, float near,
                         float far) {
    return glm::perspective(fovY, aspect, near, far) * glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
}

float luminance(const glm::vec3& c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

} // namespace

// ---- colour temperature -------------------------------------------------------------------------

TEST_CASE("colour temperature is neutral at 6500 K and warm below it", "[lighting][temperature]") {
    const glm::vec3 neutral = scene::colorTemperatureToRgb(6500.0f);
    // D65 is the white point of linear sRGB, so 6500 K must leave a white light white.
    CHECK(neutral.r == Approx(1.0f).margin(0.03f));
    CHECK(neutral.g == Approx(1.0f).margin(0.03f));
    CHECK(neutral.b == Approx(1.0f).margin(0.05f));

    const glm::vec3 warm = scene::colorTemperatureToRgb(2700.0f);
    CHECK(warm.r > warm.g);
    CHECK(warm.g > warm.b);
    CHECK(warm.r > 1.2f);  // tungsten is strongly red-weighted once normalised
    CHECK(warm.b < 0.55f);

    const glm::vec3 cool = scene::colorTemperatureToRgb(10000.0f);
    CHECK(cool.b > cool.r);
    CHECK(cool.r < 1.0f);

    // Every temperature keeps luminance 1, so changing Kelvin never changes exposure.
    for (const float kelvin : {1500.0f, 2700.0f, 4000.0f, 5600.0f, 6500.0f, 9000.0f, 12000.0f}) {
        CHECK(luminance(scene::colorTemperatureToRgb(kelvin)) == Approx(1.0f).margin(1e-3f));
    }
    // The range is clamped, not extrapolated.
    CHECK(scene::colorTemperatureToRgb(100.0f) == scene::colorTemperatureToRgb(1500.0f));
    CHECK(scene::colorTemperatureToRgb(50000.0f) == scene::colorTemperatureToRgb(12000.0f));

    // Tint moves perpendicular to the locus without changing the warm/cool relationship much.
    const glm::vec3 magenta = scene::colorTemperatureToRgb(6500.0f, 1.0f);
    const glm::vec3 green = scene::colorTemperatureToRgb(6500.0f, -1.0f);
    CHECK(magenta.g < green.g);
}

TEST_CASE("packing a light applies its temperature and its shadow flags", "[lighting][packing]") {
    scene::PunctualLight light;
    light.type = scene::PunctualLight::Type::Spot;
    light.color = glm::vec3(1.0f);
    light.intensity = 4.0f;
    light.temperature = 2700.0f;
    light.castsShadow = true;
    light.range = 12.0f;

    const rendering::GpuLight packed = rendering::packLight(light, 2, false);
    const glm::vec3 tint = scene::colorTemperatureToRgb(2700.0f);
    CHECK(packed.colorIntensity.r == Approx(tint.r * 4.0f).epsilon(1e-4));
    CHECK(packed.colorIntensity.b == Approx(tint.b * 4.0f).epsilon(1e-4));
    CHECK(packed.cone.z == 2.0f);
    const auto flags = static_cast<std::uint32_t>(packed.cone.w + 0.5f);
    CHECK((flags & rendering::kLightFlagCastsShadow) != 0u);
    CHECK((flags & rendering::kLightFlagCascaded) == 0u);
    CHECK((flags & rendering::kLightFlagArea) == 0u);
    CHECK(packed.colorIntensity.w == Approx(12.0f)); // an explicit range wins over the derived one

    // Without a shadow view there is no shadow flag, whatever the light asked for.
    CHECK((static_cast<std::uint32_t>(rendering::packLight(light, -1).cone.w + 0.5f) &
           rendering::kLightFlagCastsShadow) == 0u);

    // A rect light is flagged as an area light, and its basis is orthonormal.
    scene::PunctualLight rect;
    rect.type = scene::PunctualLight::Type::Rect;
    rect.direction = glm::normalize(glm::vec3(0.3f, -1.0f, 0.2f));
    const rendering::GpuLight area = rendering::packLight(rect);
    CHECK((static_cast<std::uint32_t>(area.cone.w + 0.5f) & rendering::kLightFlagArea) != 0u);
    const glm::vec3 up(area.up);
    const glm::vec3 right(area.tangent);
    const glm::vec3 forward(area.directionRange);
    CHECK(glm::length(up) == Approx(1.0f).margin(1e-4f));
    CHECK(glm::length(right) == Approx(1.0f).margin(1e-4f));
    CHECK(glm::dot(up, right) == Approx(0.0f).margin(1e-4f));
    CHECK(glm::dot(up, forward) == Approx(0.0f).margin(1e-4f));
}

TEST_CASE("directional lights are ordered before local ones", "[lighting][packing]") {
    std::vector<scene::PunctualLight> lights(4);
    lights[0].type = scene::PunctualLight::Type::Point;
    lights[0].name = "point";
    lights[1].type = scene::PunctualLight::Type::Directional;
    lights[1].name = "sun";
    lights[2].type = scene::PunctualLight::Type::Rect;
    lights[2].name = "panel";
    lights[2].enabled = false;
    lights[3].type = scene::PunctualLight::Type::Directional;
    lights[3].name = "sky";

    std::vector<const scene::PunctualLight*> order;
    const std::uint32_t directional = rendering::orderLightsForShading(lights, order);
    CHECK(directional == 2);
    REQUIRE(order.size() == 3); // the disabled rect is dropped
    CHECK(order[0]->name == "sun");
    CHECK(order[1]->name == "sky");
    CHECK(order[2]->name == "point");
}

// ---- clusters -------------------------------------------------------------------------------------

TEST_CASE("the froxel grid slices depth exponentially and round-trips", "[lighting][clusters]") {
    rendering::ClusterGrid grid;
    grid.zNear = 0.5f;
    grid.zFar = 400.0f;
    CHECK(grid.count() == rendering::kClusterCount);
    CHECK(grid.sliceNear(0) == Approx(0.5f));
    CHECK(grid.sliceNear(grid.z) == Approx(400.0f).epsilon(1e-4));
    // Every slice is the same ratio of the one before it.
    const float ratio = grid.sliceNear(1) / grid.sliceNear(0);
    for (std::uint32_t k = 1; k < grid.z; ++k) {
        CHECK(grid.sliceNear(k + 1) / grid.sliceNear(k) == Approx(ratio).epsilon(1e-3));
    }
    // A depth inside slice k must be classified as slice k.
    for (std::uint32_t k = 0; k < grid.z; ++k) {
        const float middle = std::sqrt(grid.sliceNear(k) * grid.sliceNear(k + 1));
        CHECK(grid.sliceOf(middle) == k);
    }
    CHECK(grid.sliceOf(0.01f) == 0);            // clamped at the near end
    CHECK(grid.sliceOf(1.0e6f) == grid.z - 1);  // and at the far end
}

TEST_CASE("cluster assignment matches a brute-force reference", "[lighting][clusters]") {
    rendering::ClusterGrid grid;
    grid.x = 8;
    grid.y = 4;
    grid.z = 6;
    grid.zNear = 0.5f;
    grid.zFar = 120.0f;
    grid.tanHalfFovY = std::tan(0.45f);
    grid.aspect = 16.0f / 9.0f;

    const std::vector<glm::vec3> positions = {
        {0.0f, 0.0f, -10.0f}, {6.0f, 2.0f, -30.0f}, {-20.0f, -8.0f, -80.0f}, {0.0f, 0.0f, -0.6f},
    };
    const std::vector<float> radii = {4.0f, 12.0f, 25.0f, 1.0f};
    const auto lists = rendering::assignClusters(grid, positions, radii);
    REQUIRE(lists.size() == grid.count());

    std::size_t assigned = 0;
    for (std::uint32_t k = 0; k < grid.z; ++k) {
        for (std::uint32_t j = 0; j < grid.y; ++j) {
            for (std::uint32_t i = 0; i < grid.x; ++i) {
                const auto& list = lists[grid.indexOf(i, j, k)];
                // The reference: a light belongs to a froxel exactly when its sphere touches the
                // froxel's box, which is the test the compute pass performs per thread.
                std::vector<std::uint32_t> expected;
                for (std::uint32_t n = 0; n < positions.size(); ++n) {
                    glm::vec3 lo;
                    glm::vec3 hi;
                    grid.bounds(i, j, k, lo, hi);
                    const glm::vec3 closest = glm::clamp(positions[n], lo, hi);
                    if (glm::length(positions[n] - closest) <= radii[n]) {
                        expected.push_back(n);
                    }
                }
                CHECK(list == expected);
                assigned += list.size();
            }
        }
    }
    CHECK(assigned > 0); // the lights really do reach some froxels
    // A light with no reach is in no cluster at all.
    const auto none = rendering::assignClusters(grid, {{0.0f, 0.0f, -10.0f}}, {0.0f});
    for (const auto& list : none) {
        CHECK(list.empty());
    }
}

TEST_CASE("froxel bounds cover their screen tile and depth slice", "[lighting][clusters]") {
    rendering::ClusterGrid grid;
    grid.zNear = 1.0f;
    grid.zFar = 100.0f;
    glm::vec3 lo;
    glm::vec3 hi;
    grid.bounds(0, 0, 0, lo, hi);
    CHECK(hi.z == Approx(-grid.sliceNear(0)));
    CHECK(lo.z == Approx(-grid.sliceNear(1)));
    CHECK(lo.x <= hi.x);
    CHECK(lo.y <= hi.y);
    CHECK(lo.z <= hi.z);
    // Neighbouring froxels overlap rather than leave a gap: a box that bounds a truncated pyramid
    // is conservative, and a gap would let a light fall between two clusters.
    glm::vec3 lo2;
    glm::vec3 hi2;
    grid.bounds(1, 0, 0, lo2, hi2);
    CHECK(lo2.x <= hi.x + 1e-4f);
    CHECK(hi2.x > hi.x);
    // The outermost froxels reach the edges of the frustum at the far plane of their slice.
    glm::vec3 loEdge;
    glm::vec3 hiEdge;
    grid.bounds(grid.x - 1, grid.y - 1, grid.z - 1, loEdge, hiEdge);
    CHECK(hiEdge.x == Approx(grid.tanHalfFovY * grid.aspect * grid.zFar).epsilon(1e-3));
}

// ---- cascades -------------------------------------------------------------------------------------

TEST_CASE("cascade splits blend the logarithmic and uniform schemes", "[lighting][shadows]") {
    const auto splits = rendering::cascadeSplits(0.5f, 100.0f, 3, 0.85f);
    REQUIRE(splits.size() == 3);
    CHECK(splits[0] < splits[1]);
    CHECK(splits[1] < splits[2]);
    CHECK(splits.back() == Approx(100.0f));
    // lambda 0 is the uniform scheme exactly.
    const auto uniform = rendering::cascadeSplits(0.0f, 90.0f, 3, 0.0f);
    CHECK(uniform[0] == Approx(30.0f).epsilon(1e-3));
    CHECK(uniform[1] == Approx(60.0f).epsilon(1e-3));
    // lambda 1 is the logarithmic scheme exactly.
    const auto logarithmic = rendering::cascadeSplits(1.0f, 1000.0f, 3, 1.0f);
    CHECK(logarithmic[0] == Approx(10.0f).epsilon(1e-3));
    CHECK(logarithmic[1] == Approx(100.0f).epsilon(1e-3));
    // Degenerate inputs stay sane.
    CHECK(rendering::cascadeSplits(1.0f, 1.0f, 4).size() == 4);
    CHECK(rendering::cascadeSplits(1.0f, 10.0f, 99).size() == rendering::kMaxCascades);
}

TEST_CASE("a fitted cascade contains its slice of the camera frustum", "[lighting][shadows]") {
    constexpr float kNear = 0.5f;
    constexpr float kFar = 200.0f;
    const glm::mat4 viewProj =
        cameraViewProj({4.0f, 6.0f, 12.0f}, {0.0f, 1.0f, 0.0f}, 0.9f, 16.0f / 9.0f, kNear, kFar);
    const glm::mat4 invViewProj = glm::inverse(viewProj);
    const glm::vec3 lightDir = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.35f));
    const auto splits = rendering::cascadeSplits(kNear, 60.0f, 3);

    float nearDepth = kNear;
    for (std::uint32_t c = 0; c < splits.size(); ++c) {
        const rendering::ShadowView view = rendering::fitDirectionalCascade(
            invViewProj, kNear, kFar, nearDepth, splits[c], lightDir, 2048, 40.0f);
        // Every corner of the sub-frustum must land inside the cascade's clip volume, or the
        // shadow simply stops at an invisible edge.
        const auto corners = rendering::frustumCorners(invViewProj);
        const float t0 = (nearDepth - kNear) / (kFar - kNear);
        const float t1 = (splits[c] - kNear) / (kFar - kNear);
        for (std::size_t i = 0; i < 4; ++i) {
            for (const float t : {t0, t1}) {
                const glm::vec3 p = corners[i] + (corners[i + 4] - corners[i]) * t;
                const glm::vec4 clip = view.viewProj * glm::vec4(p, 1.0f);
                REQUIRE(clip.w > 0.0f);
                const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                INFO("cascade " << c << " corner " << i << " ndc " << ndc.x << ", " << ndc.y << ", " << ndc.z);
                CHECK(std::abs(ndc.x) <= 1.001f);
                CHECK(std::abs(ndc.y) <= 1.001f);
                CHECK(ndc.z >= -0.001f);
                CHECK(ndc.z <= 1.001f);
            }
        }
        CHECK(view.cascade);
        CHECK(view.texelWorldSize > 0.0f);
        CHECK(view.depthRange > 0.0f);
        CHECK(view.farDistance == Approx(splits[c]));
        nearDepth = splits[c];
    }
}

TEST_CASE("cascades are stabilised against camera motion", "[lighting][shadows]") {
    constexpr float kNear = 0.5f;
    constexpr float kFar = 200.0f;
    const glm::vec3 lightDir = glm::normalize(glm::vec3(0.2f, -1.0f, 0.1f));
    const auto fit = [&](const glm::vec3& eye, const glm::vec3& target) {
        const glm::mat4 vp = cameraViewProj(eye, target, 0.9f, 16.0f / 9.0f, kNear, kFar);
        return rendering::fitDirectionalCascade(glm::inverse(vp), kNear, kFar, kNear, 40.0f, lightDir, 1024,
                                                30.0f);
    };
    const rendering::ShadowView base = fit({0.0f, 5.0f, 20.0f}, {0.0f, 0.0f, 0.0f});
    // A pure rotation about the camera keeps the sub-frustum's bounding sphere the same size, so
    // the map's texel size must not change: that is what stops the shadow edges crawling.
    const rendering::ShadowView rotated = fit({14.14f, 5.0f, 14.14f}, {0.0f, 0.0f, 0.0f});
    CHECK(rotated.texelWorldSize == Approx(base.texelWorldSize).epsilon(0.02));
    // Translating by less than a texel must not move the projection at all.
    const rendering::ShadowView nudged =
        fit({base.texelWorldSize * 0.1f, 5.0f, 20.0f}, {base.texelWorldSize * 0.1f, 0.0f, 0.0f});
    CHECK(nudged.texelWorldSize == Approx(base.texelWorldSize).epsilon(1e-4));
}

TEST_CASE("a spot shadow map covers the light's cone", "[lighting][shadows]") {
    scene::PunctualLight spot;
    spot.type = scene::PunctualLight::Type::Spot;
    spot.position = {0.0f, 8.0f, 0.0f};
    spot.direction = {0.0f, -1.0f, 0.0f};
    spot.outerConeAngle = 0.6f;
    const rendering::ShadowView view = rendering::fitSpotShadow(spot, 1024, 20.0f);
    CHECK_FALSE(view.cascade);
    CHECK(view.depthRange > 0.0f);
    // The point straight below the light projects to the centre of the map.
    const glm::vec4 centre = view.viewProj * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK((centre.x / centre.w) == Approx(0.0f).margin(1e-4f));
    CHECK((centre.y / centre.w) == Approx(0.0f).margin(1e-4f));
    // A point at the edge of the cone is still inside the map.
    const float edge = 8.0f * std::tan(spot.outerConeAngle);
    const glm::vec4 rim = view.viewProj * glm::vec4(edge, 0.0f, 0.0f, 1.0f);
    CHECK(std::abs(rim.x / rim.w) < 1.0f);
    // A point far outside it is not.
    const glm::vec4 outside = view.viewProj * glm::vec4(edge * 4.0f, 0.0f, 0.0f, 1.0f);
    CHECK(std::abs(outside.x / outside.w) > 1.0f);
}

// ---- area lights ------------------------------------------------------------------------------------

TEST_CASE("area-light irradiance falls off with distance and grows with size", "[lighting][area]") {
    scene::PunctualLight rect;
    rect.type = scene::PunctualLight::Type::Rect;
    rect.width = 2.0f;
    rect.height = 2.0f;
    rect.direction = {0.0f, -1.0f, 0.0f}; // pointing down at the receiver
    const glm::vec3 receiver(0.0f);
    const glm::vec3 normal(0.0f, 1.0f, 0.0f);

    const auto irradianceAt = [&](float distance, float size) {
        scene::PunctualLight l = rect;
        l.width = size;
        l.height = size;
        l.position = {0.0f, distance, 0.0f};
        glm::vec3 corners[4];
        rendering::areaLightCorners(l, corners);
        return rendering::polygonIrradiance(receiver, normal, corners[0], corners[1], corners[2], corners[3]);
    };

    // Inverse square in the far field: an emitter of area A at distance d subtends A / d^2.
    const float far4 = irradianceAt(8.0f, 2.0f);
    const float far8 = irradianceAt(16.0f, 2.0f);
    CHECK(far4 > far8);
    CHECK(far4 / far8 == Approx(4.0f).epsilon(0.05)); // doubling the distance quarters it
    // The far-field value matches the analytic A cos / (pi d^2) of a small Lambertian emitter.
    CHECK(far4 == Approx(4.0f / (3.14159265f * 64.0f)).epsilon(0.05));

    // Twice the area at the same distance and radiance is twice the irradiance.
    CHECK(irradianceAt(16.0f, 2.0f * std::sqrt(2.0f)) == Approx(2.0f * far8).epsilon(0.05));

    // Monotonic in distance, and it saturates below 1 as the emitter fills the hemisphere.
    float previous = 2.0f;
    for (const float d : {0.05f, 0.2f, 1.0f, 4.0f, 16.0f, 64.0f}) {
        const float e = irradianceAt(d, 2.0f);
        CHECK(e < previous);
        CHECK(e <= 1.0f);
        previous = e;
    }
    // An emitter behind the surface contributes nothing.
    scene::PunctualLight below = rect;
    below.position = {0.0f, -4.0f, 0.0f};
    glm::vec3 corners[4];
    rendering::areaLightCorners(below, corners);
    CHECK(rendering::polygonIrradiance(receiver, normal, corners[0], corners[1], corners[2], corners[3]) ==
          Approx(0.0f).margin(1e-5f));
}

TEST_CASE("the LTC table is finite, smooth and energy-bounded", "[lighting][area]") {
    const rendering::LtcTable table = rendering::buildLtcTable(16, 16);
    REQUIRE(table.matrix.size() == 16 * 16);
    REQUIRE(table.terms.size() == 16 * 16);
    for (std::size_t i = 0; i < table.matrix.size(); ++i) {
        const glm::vec4 m = table.matrix[i];
        for (int c = 0; c < 4; ++c) {
            CHECK(std::isfinite(m[c]));
        }
        CHECK(m.x > 0.0f);  // 1 / s1 is always positive
        CHECK(m.z > 0.0f);  // 1 / (s2 cos) too
        const glm::vec4 t = table.terms[i];
        CHECK(t.x >= 0.0f);
        CHECK(t.y >= 0.0f);
        CHECK(t.x + t.y <= 1.15f); // the split-sum terms cannot create energy
    }
    // A smoother surface has a narrower lobe, so its inverse transform scales the cone up more.
    const auto entry = [&](std::uint32_t roughnessRow, std::uint32_t nDotVColumn) {
        return table.matrix[static_cast<std::size_t>(roughnessRow) * table.size + nDotVColumn];
    };
    CHECK(entry(0, 15).x > entry(15, 15).x);
    // Deterministic: two builds agree bit for bit.
    const rendering::LtcTable again = rendering::buildLtcTable(16, 16);
    for (std::size_t i = 0; i < table.matrix.size(); ++i) {
        CHECK(table.matrix[i] == again.matrix[i]);
        CHECK(table.terms[i] == again.terms[i]);
    }
}

// ---- influence radius --------------------------------------------------------------------------------

TEST_CASE("influence radius grows with intensity and is infinite for directional lights",
          "[lighting][clusters]") {
    scene::PunctualLight point;
    point.type = scene::PunctualLight::Type::Point;
    point.intensity = 1.0f;
    const float dim = rendering::lightInfluenceRadius(point);
    point.intensity = 100.0f;
    const float bright = rendering::lightInfluenceRadius(point);
    CHECK(bright > dim);
    CHECK(bright / dim == Approx(10.0f).epsilon(0.05)); // sqrt of the intensity ratio

    scene::PunctualLight sun;
    sun.type = scene::PunctualLight::Type::Directional;
    CHECK(rendering::lightInfluenceRadius(sun) == 0.0f);

    scene::PunctualLight dark;
    dark.type = scene::PunctualLight::Type::Point;
    dark.intensity = 0.0f;
    CHECK(rendering::lightInfluenceRadius(dark) == 0.0f);
}
