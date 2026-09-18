// The Lighting Lab's CPU half (§7 of docs/engineering-labs.md, lab #7).
//
// The question is "which lights reached this pixel, and with how much?", and the half of it that
// does not need a device is: **what reach was this light packed with, and which froxels was it
// given to?** Both are arithmetic over a light, a camera and a grid, and both are decisions the
// renderer makes before a fragment exists.
//
// ---- the shape of the fixtures ---------------------------------------------------------------
//
// ADR-182, and the cull bug of 2026-09-16 that it is about: every culling fixture in the tree was a
// centred box, for which the right and the wrong radius rule are bit-identical, so the bug was in
// the shape of the fixtures rather than in any assertion. The equivalent here is a lamp over a
// sphere at the origin with a white 6500 K colour and no size -- for which a great many wrong
// answers are also the right one. So the lights below are off-origin, coloured, warm, cold,
// oversized, degenerate and placed at froxel boundaries, and the plain white point light at the
// centre of the grid is present as the control that says the others are being measured at all.
//
// ---- what "the light still contributes here" means -------------------------------------------
//
// §37: the renderer is the source of truth. So the reach assertions below do not compare
// `lightInfluenceRadius` against a formula invented here -- they compare it against the radiance
// `shaders/lighting.wgsl` itself computes at that distance, mirrored term for term, using the same
// `polygonIrradiance` the header states is what `ltcEvaluate` computes with the identity
// transform. A light cut off while the shader would still have given it radiance is a hard edge in
// the image, and that is the defect this file exists to state as an invariant.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "rendering/light_data.hpp"
#include "scene/composition.hpp"
#include "scene/scene_types.hpp"
#include "signals/signal_bus.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {
fs::path repoRoot() { return fs::path(AVGEN_SOURCE_DIR); }
}

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// The cutoff `lightInfluenceRadius` is documented against, named once so the probe and the
// implementation cannot drift apart silently.
constexpr float kCutoff = 0.004f;

scene::PunctualLight point(const glm::vec3& at, float intensity) {
    scene::PunctualLight l;
    l.name = "point";
    l.type = scene::PunctualLight::Type::Point;
    l.position = at;
    l.intensity = intensity;
    l.range = 0.0f;
    return l;
}

// The radiance the shading pass multiplies the BRDF by, for a surface at `distance` from the light
// and facing it squarely -- expressed in the units the punctual path uses, so every kind can be
// held to one cutoff.
//
//   punctual   `evaluateLight`: colorIntensity * (1 / d^2)
//   rect/disk  `shadeArea`:     out.diffuse = albedo * colorIntensity * F, against the punctual
//                               path's albedo/PI * colorIntensity / d^2 -- so the equivalent is
//                               PI * colorIntensity * F, and F is `polygonIrradiance`
//   tube/sphere `shadeRepresentative`: colorIntensity * (area / d^2), the CPU packing nits
float shaderRadianceAt(const scene::PunctualLight& light, float distance) {
    const rendering::GpuLight packed = rendering::packLight(light);
    const float peak = std::max({packed.colorIntensity.r, packed.colorIntensity.g, packed.colorIntensity.b, 0.0f});
    const float d2 = std::max(distance * distance, 1e-4f);
    // The range window, identical in all three shader paths.
    float window = 1.0f;
    if (light.range > 0.0f) {
        const float r = distance / light.range;
        const float w = std::clamp(1.0f - r * r * r * r, 0.0f, 1.0f);
        window = w * w;
    }
    switch (light.type) {
    case scene::PunctualLight::Type::Directional:
        return peak;
    case scene::PunctualLight::Type::Point:
    case scene::PunctualLight::Type::Spot:
        return peak * window / d2;
    case scene::PunctualLight::Type::Rect:
    case scene::PunctualLight::Type::Disk: {
        glm::vec3 corners[4];
        rendering::areaLightCorners(light, corners);
        // A receiver on the emitter's axis, facing it: the brightest point at this distance.
        const glm::vec3 dir = glm::normalize(light.direction);
        const glm::vec3 p = light.position + dir * distance;
        const float f = rendering::polygonIrradiance(p, -dir, corners[0], corners[1], corners[2], corners[3]);
        return kPi * peak * window * f;
    }
    case scene::PunctualLight::Type::Tube:
    case scene::PunctualLight::Type::Sphere: {
        const float area = light.type == scene::PunctualLight::Type::Tube
                               ? 2.0f * std::max(light.radius, 1e-3f) * std::max(light.width, 1e-3f)
                               : kPi * std::max(light.radius, 1e-3f) * std::max(light.radius, 1e-3f);
        return peak * window * area / d2;
    }
    }
    return 0.0f;
}

// A set of lights that is deliberately awkward: off-origin, coloured, warm and cold, large and
// tiny. The white point light at the origin is the control.
std::vector<scene::PunctualLight> awkwardLights() {
    std::vector<scene::PunctualLight> out;

    scene::PunctualLight control = point(glm::vec3(0.0f), 4.0f);
    control.name = "control-white-point-at-origin";
    out.push_back(control);

    scene::PunctualLight warm = point(glm::vec3(-31.4f, 6.5f, 18.2f), 4.0f);
    warm.name = "warm-point-off-origin";
    warm.temperature = 2000.0f; // a sodium practical: the red channel leaves 1.0 well behind
    out.push_back(warm);

    scene::PunctualLight cold = point(glm::vec3(7.0f, -3.0f, -41.0f), 4.0f);
    cold.name = "cold-point";
    cold.temperature = 11000.0f;
    out.push_back(cold);

    scene::PunctualLight tinted = point(glm::vec3(12.0f, 1.0f, 9.0f), 2.0f);
    tinted.name = "magenta-tinted-point";
    tinted.tint = 0.9f;
    tinted.color = glm::vec3(0.4f, 0.9f, 0.3f);
    out.push_back(tinted);

    scene::PunctualLight spot;
    spot.name = "narrow-spot";
    spot.type = scene::PunctualLight::Type::Spot;
    spot.position = glm::vec3(-5.0f, 14.0f, -22.0f);
    spot.direction = glm::normalize(glm::vec3(0.2f, -1.0f, 0.1f));
    spot.intensity = 60.0f;
    spot.innerConeAngle = 0.12f;
    spot.outerConeAngle = 0.18f;
    out.push_back(spot);

    scene::PunctualLight rect;
    rect.name = "large-rect-softbox";
    rect.type = scene::PunctualLight::Type::Rect;
    rect.position = glm::vec3(3.0f, 9.0f, -14.0f);
    rect.direction = glm::normalize(glm::vec3(-0.2f, -1.0f, 0.3f));
    rect.intensity = 2.0f; // nits over the emitter, not candela
    rect.width = 6.0f;
    rect.height = 4.0f;
    out.push_back(rect);

    scene::PunctualLight small = rect;
    small.name = "tiny-rect";
    small.position = glm::vec3(-2.0f, 1.2f, 4.0f);
    small.width = 0.08f;
    small.height = 0.05f;
    small.intensity = 40.0f;
    out.push_back(small);

    scene::PunctualLight disk;
    disk.name = "disk";
    disk.type = scene::PunctualLight::Type::Disk;
    disk.position = glm::vec3(18.0f, 4.0f, 3.0f);
    disk.direction = glm::normalize(glm::vec3(0.0f, -1.0f, -0.4f));
    disk.intensity = 3.0f;
    disk.radius = 2.5f;
    out.push_back(disk);

    scene::PunctualLight sphere;
    sphere.name = "sphere-practical";
    sphere.type = scene::PunctualLight::Type::Sphere;
    sphere.position = glm::vec3(-9.0f, 2.0f, 11.0f);
    sphere.intensity = 5.0f;
    sphere.radius = 1.4f;
    out.push_back(sphere);

    scene::PunctualLight tube;
    tube.name = "tube-strip";
    tube.type = scene::PunctualLight::Type::Tube;
    tube.position = glm::vec3(0.0f, 5.0f, 27.0f);
    tube.direction = glm::normalize(glm::vec3(0.0f, -1.0f, 0.0f));
    tube.intensity = 4.0f;
    tube.width = 8.0f;  // the tube's length
    tube.radius = 0.15f;
    out.push_back(tube);

    return out;
}

} // namespace

// ---- reach ---------------------------------------------------------------------------------------

TEST_CASE("a light's packed reach outlives its contribution", "[lighting][lab][reach]") {
    for (const scene::PunctualLight& light : awkwardLights()) {
        INFO("light: " << light.name);
        const float radius = rendering::lightInfluenceRadius(light, kCutoff);
        REQUIRE(std::isfinite(radius));
        REQUIRE(radius > 0.0f);

        // The control that says this probe can fail: well inside the reach, the light is still
        // brighter than the cutoff. Without it "the light is dark at the boundary" is a sentence
        // that a reach of zero would also satisfy.
        CHECK(shaderRadianceAt(light, radius * 0.25f) > kCutoff);

        // The invariant: past the reach it is given, the light is not assigned to any froxel, so
        // its contribution stops dead. That is only not a visible edge if the contribution it
        // stops was already below the cutoff.
        CHECK(shaderRadianceAt(light, radius) <= kCutoff);
    }
}

TEST_CASE("an explicit range is the reach, and the window closes there", "[lighting][lab][reach]") {
    // A light with an authored range is the consistent case and the reason the defect above is
    // latent rather than constant: every rig light gets `range = distance * 6` and every ecology
    // light `range = radius * 4`, so the heuristic is reached only by hand-built and
    // glTF-imported lights (KHR_lights_punctual's `range` is optional and usually absent).
    for (scene::PunctualLight light : awkwardLights()) {
        INFO("light: " << light.name);
        const float unconstrained = rendering::lightInfluenceRadius(light, kCutoff);
        light.range = 17.5f;
        CHECK(rendering::lightInfluenceRadius(light, kCutoff) == Approx(17.5f));
        CHECK(shaderRadianceAt(light, 17.5f) == Approx(0.0f).margin(1e-6));
        // The control, measured where this particular light is still above the cutoff rather than
        // at a distance picked for the set: `tiny-rect` is 8 cm x 5 cm and is under the cutoff by
        // 8 m, so a fixed probe distance would be asserting something about the fixture.
        CHECK(shaderRadianceAt(light, std::min(8.0f, unconstrained * 0.25f)) > kCutoff);
    }
}

TEST_CASE("a light nobody can describe reaches nothing", "[lighting][lab][stability]") {
    // `packLight` already refuses to upload a non-finite light and says so with the value in it.
    // `lightInfluenceRadius` is called separately, on the *scene* light, by
    // `SceneRenderer::updateLights` -- so the two have to agree about what a broken light reaches,
    // or a NaN goes into the cluster uniform's radius while the light itself was dropped.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    scene::PunctualLight l = point(glm::vec3(2.0f, 1.0f, -3.0f), 5.0f);
    l.intensity = nan;
    CHECK(rendering::lightInfluenceRadius(l) == 0.0f);

    l = point(glm::vec3(2.0f, 1.0f, -3.0f), 5.0f);
    l.color = glm::vec3(1.0f, nan, 1.0f);
    CHECK(rendering::lightInfluenceRadius(l) == 0.0f);

    l = point(glm::vec3(nan, 1.0f, -3.0f), 5.0f);
    CHECK(rendering::lightInfluenceRadius(l) == 0.0f);

    l = point(glm::vec3(2.0f, 1.0f, -3.0f), inf);
    CHECK(rendering::lightInfluenceRadius(l) == 0.0f);

    l = point(glm::vec3(2.0f, 1.0f, -3.0f), 5.0f);
    l.range = nan;
    CHECK(rendering::lightInfluenceRadius(l) == 0.0f);

    // The control: the same light, finite, reaches something.
    CHECK(rendering::lightInfluenceRadius(point(glm::vec3(2.0f, 1.0f, -3.0f), 5.0f)) > 1.0f);
}

TEST_CASE("extreme but valid intensities stay finite and bounded", "[lighting][lab][stability]") {
    // The stability limits §7 asks for, stated as numbers rather than as "it did not crash".
    for (const float intensity : {1e-6f, 1.0f, 1e4f, 1e9f, 3.4e38f}) {
        INFO("intensity: " << intensity);
        const scene::PunctualLight l = point(glm::vec3(4.0f, 2.0f, -7.0f), intensity);
        const float r = rendering::lightInfluenceRadius(l);
        CHECK(std::isfinite(r));
        CHECK(r >= 0.01f);
        CHECK(r <= 10000.0f);
        const rendering::GpuLight g = rendering::packLight(l);
        CHECK(std::isfinite(g.colorIntensity.r));
        CHECK(std::isfinite(g.colorIntensity.w));
    }
    // A negative intensity is clamped to darkness rather than inverted into a light that removes
    // radiance, and a light that emits nothing reaches nothing.
    scene::PunctualLight negative = point(glm::vec3(1.0f, 1.0f, 1.0f), -8.0f);
    CHECK(rendering::packLight(negative).colorIntensity.r == 0.0f);
    CHECK(rendering::lightInfluenceRadius(negative) == 0.0f);
}

// ---- the froxel grid, both halves ----------------------------------------------------------------

TEST_CASE("the froxel a fragment reads is the froxel that point is in", "[lighting][lab][clusters]") {
    // The half of the grid nothing checked. `tests/rendering/test_shadows_gpu.cpp` compares the
    // lists the compute pass *builds* against `assignClusters` index for index -- but that pass is
    // indexed by its own invocation id, so a grid whose rows ran the wrong way up would pass that
    // comparison exactly and light the wrong half of the screen. The lookup is the other half, and
    // this is the round trip that ties the two together.
    rendering::ClusterGrid grid;
    grid.zNear = 0.1f;
    grid.zFar = 220.0f;
    grid.tanHalfFovY = std::tan(0.44f);
    grid.aspect = 16.0f / 9.0f;

    // Off-centre in every direction and at every depth, which is the point: a point on the view
    // axis at the middle depth lands in the same froxel whichever way the rows run.
    const auto project = [&](const glm::vec3& viewPos) {
        const float depth = -viewPos.z;
        const glm::vec2 ndc(viewPos.x / (grid.tanHalfFovY * grid.aspect * depth),
                            viewPos.y / (grid.tanHalfFovY * depth));
        return glm::vec2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f); // y down, as the target is
    };

    std::size_t checked = 0;
    std::size_t upperHalf = 0;
    for (const float depth : {0.4f, 1.3f, 5.0f, 21.0f, 96.0f, 199.0f}) {
        for (const float u : {0.03f, 0.19f, 0.5f, 0.71f, 0.97f}) {
            for (const float v : {0.04f, 0.23f, 0.5f, 0.66f, 0.95f}) {
                const glm::vec3 viewPos((u * 2.0f - 1.0f) * grid.tanHalfFovY * grid.aspect * depth,
                                        (v * 2.0f - 1.0f) * grid.tanHalfFovY * depth, -depth);
                const glm::vec2 uv = project(viewPos);
                const std::uint32_t cluster = grid.clusterOf(uv, depth);
                REQUIRE(cluster < grid.count());
                // Decompose and check the box the build pass would have given that index.
                const std::uint32_t i = cluster % grid.x;
                const std::uint32_t j = (cluster / grid.x) % grid.y;
                const std::uint32_t k = cluster / (grid.x * grid.y);
                glm::vec3 lo;
                glm::vec3 hi;
                grid.bounds(i, j, k, lo, hi);
                INFO("uv " << uv.x << ", " << uv.y << " depth " << depth << " -> froxel " << i << ","
                           << j << "," << k);
                CHECK(viewPos.x >= lo.x - 1e-3f);
                CHECK(viewPos.x <= hi.x + 1e-3f);
                CHECK(viewPos.y >= lo.y - 1e-3f);
                CHECK(viewPos.y <= hi.y + 1e-3f);
                CHECK(viewPos.z >= lo.z - 1e-3f);
                CHECK(viewPos.z <= hi.z + 1e-3f);
                ++checked;
                if (v > 0.5f) {
                    // `v` here is NDC-ish -- above the view axis in the world -- which the
                    // projection turns into a screen uv *below* 0.5, because the target's y runs
                    // down. The grid's rows run bottom-up, so it must come back to a high `j`.
                    upperHalf += j >= grid.y / 2 ? 1 : 0;
                }
            }
        }
    }
    CHECK(checked == 150);
    // The control that says the flip is being exercised rather than cancelling out. Every one of
    // the 60 samples above the view axis must land in the grid's *upper* rows; an implementation
    // that dropped the flip would put all 60 in the lower half and every containment check above
    // would still pass, because `bounds()` would be consulted for the froxel it chose.
    CHECK(upperHalf == 60);
    // And the edges clamp instead of running off the end. Top-right is the last froxel of the last
    // slice; bottom-left at zero depth is the first of the first. The two corners are on opposite
    // ends of `j` precisely because of the flip.
    CHECK(grid.clusterOf(glm::vec2(1.0f, 0.0f), 1.0e6f) == grid.count() - 1);
    CHECK(grid.clusterOf(glm::vec2(0.0f, 1.0f), 0.0f) == 0);
    // Bottom-right of the last slice is the same column and the *first* row.
    CHECK(grid.clusterOf(glm::vec2(1.0f, 1.0f), 1.0e6f) ==
          grid.indexOf(grid.x - 1, 0, grid.z - 1));
}

TEST_CASE("the per-light assignment report agrees with the lists it is a summary of",
          "[lighting][lab][clusters]") {
    rendering::ClusterGrid grid;
    grid.x = 8;
    grid.y = 4;
    grid.z = 6;
    grid.zNear = 0.5f;
    grid.zFar = 140.0f;
    grid.tanHalfFovY = std::tan(0.45f);
    grid.aspect = 16.0f / 9.0f;

    // Deliberately lopsided: one light that reaches nearly everything, several that reach a
    // little, and one with no reach at all. A set of equal lights cannot show that the cap is
    // resolved in buffer order.
    std::vector<glm::vec3> positions;
    std::vector<float> radii;
    positions.emplace_back(0.0f, 0.0f, -40.0f);
    radii.push_back(500.0f); // reaches every froxel
    for (int n = 0; n < 40; ++n) {
        const auto f = static_cast<float>(n);
        positions.emplace_back(std::sin(f) * 6.0f, std::cos(f) * 4.0f, -6.0f - f * 0.4f);
        radii.push_back(9.0f);
    }
    positions.emplace_back(3.0f, 1.0f, -12.0f);
    radii.push_back(0.0f); // reaches nothing, and must be reported as reaching nothing

    const auto lists = rendering::assignClusters(grid, positions, radii);
    const auto report = rendering::lightAssignments(grid, positions, radii);
    const auto occupancy = rendering::clusterOccupancy(grid, positions, radii);
    REQUIRE(report.size() == positions.size());

    // `admitted` is exactly how many capped lists hold this light: the report is a transposition
    // of the lists, not a second opinion about them.
    std::vector<std::uint32_t> fromLists(positions.size(), 0);
    for (const auto& list : lists) {
        for (const std::uint32_t index : list) {
            ++fromLists[index];
        }
    }
    std::uint64_t admitted = 0;
    std::uint64_t touched = 0;
    for (std::size_t n = 0; n < report.size(); ++n) {
        INFO("light " << n);
        CHECK(report[n].admitted == fromLists[n]);
        CHECK(report[n].touched == report[n].admitted + report[n].crowdedOut);
        CHECK(report[n].radius == radii[n]);
        admitted += report[n].admitted;
        touched += report[n].touched;
    }
    // And the totals are the per-cluster measurement's, seen from the other side.
    CHECK(touched == occupancy.demand);
    CHECK(touched - admitted == occupancy.dropped);

    // The findings the report exists to make, and the controls that say it can make them.
    CHECK(report.front().touched == grid.count());       // the big light reaches everything
    CHECK(report.back().touched == 0);                   // the dark one reaches nothing
    CHECK(report.back().crowdedOut == 0);
    CHECK(occupancy.overflowed > 0);                     // the cap really is biting somewhere
    // Buffer order decides who is dropped, not brightness: light 0 is first in the buffer and is
    // never the one crowded out, however many lights pile into a froxel.
    CHECK(report.front().crowdedOut == 0);

}

// ---- the fixture ---------------------------------------------------------------------------------

TEST_CASE("the lab's fixture delivers every light kind to the renderer", "[lighting][lab][fixture]") {
    // This is not ceremony. **A scene file cannot author a light** -- `Composition::fromJson` reads
    // `camera`, `environment`, `lightRig`, `nodes` and eleven other keys, and `lights` is not one
    // of them; there is no `NodeKind::Light`; and unknown top-level keys are ignored rather than
    // refused. So the only routes into `scene::Scene::lights` are a light rig, a glTF asset that
    // carries KHR_lights_punctual (no asset in this repository does), the procedural ecology lights
    // and the single default key `Composition` adds when a scene has neither. A lighting fixture is
    // therefore its rig, and the thing most worth asserting about it is that the rig arrived.
    //
    // `examples/labs/lod-geometry-lab.scene.json` carries a top-level "lights" array that is read
    // by nothing; that scene is lit by `defaultKeyLight()` instead. That is what this test would
    // have caught, and it is why it is here rather than in a comment.
    const fs::path file = repoRoot() / "examples/labs/lighting-lab.scene.json";
    REQUIRE(fs::is_regular_file(file));
    assets::AssetRegistry registry(file.parent_path());
    auto loaded = scene::Composition::loadFile(file, registry);
    REQUIRE(loaded.has_value());
    std::unique_ptr<scene::Composition> comp = std::move(*loaded);

    params::ParameterSet parameters;
    params::Modulator modulator;
    signals::SignalBus bus;
    comp->attach(parameters, modulator);
    comp->setViewport(1280, 720);

    FrameTime time;
    time.renderTime = 0.5;
    time.deltaTime = 1.0 / 60.0;
    time.frameIndex = 30;
    parameters.resetFinals();
    comp->updateFields(time, bus, modulator);
    modulator.applyRoutes(bus, parameters, time.deltaTime);
    comp->updateBehaviour(time, bus);
    comp->update(time);

    const scene::Scene& s = comp->scene();
    std::map<scene::PunctualLight::Type, int> kinds;
    for (const scene::PunctualLight& l : s.lights) {
        if (l.enabled) {
            ++kinds[l.type];
        }
    }
    INFO("lights: " << s.lights.size());
    // All seven kinds the engine has, in one frame, which is what makes the fixture a lighting
    // fixture rather than a scene that happens to be lit.
    CHECK(kinds[scene::PunctualLight::Type::Directional] == 2);
    CHECK(kinds[scene::PunctualLight::Type::Point] == 2);
    CHECK(kinds[scene::PunctualLight::Type::Spot] == 1);
    CHECK(kinds[scene::PunctualLight::Type::Rect] == 2);
    CHECK(kinds[scene::PunctualLight::Type::Disk] == 1);
    CHECK(kinds[scene::PunctualLight::Type::Tube] == 1);
    CHECK(kinds[scene::PunctualLight::Type::Sphere] == 1);

    const auto find = [&](std::string_view suffix) -> const scene::PunctualLight* {
        for (const scene::PunctualLight& l : s.lights) {
            if (l.name.size() >= suffix.size() &&
                l.name.compare(l.name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                return &l;
            }
        }
        return nullptr;
    };

    // The rig is sized from the fixture's focal point, not from the 256 m terrain. Without the
    // `composition.focalPoints` block every local light would sit hundreds of metres out and the
    // fixture would measure nothing -- so the distance is asserted, not assumed.
    const scene::PunctualLight* a = find("point-a");
    const scene::PunctualLight* b = find("point-b");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    const glm::vec3 subject(0.0f, 1.5f, 0.0f);
    CHECK(glm::length(a->position - subject) == Approx(8.0f).epsilon(1e-3)); // 2.0 radii x 4 m
    CHECK(a->range == Approx(48.0f).epsilon(1e-3));                          // distance x 6
    // Overlapping, by construction: 15 degrees apart at 8 m is 2.09 m, well inside either reach.
    CHECK(glm::length(a->position - b->position) < a->range);
    CHECK(glm::length(a->position - b->position) > 1.0f);

    // The small/large pair differs in emitter area and in nothing else that matters to the LTC
    // path: the rig's nits-per-area conversion has already divided each by its own area, so the
    // *reach* is the thing that separates them.
    const scene::PunctualLight* large = find("rect-large");
    const scene::PunctualLight* small = find("rect-small");
    REQUIRE(large != nullptr);
    REQUIRE(small != nullptr);
    CHECK(scene::emitterArea(*large) > scene::emitterArea(*small) * 100.0f);

    // Every local light has an explicit range, so every one of them takes the well-behaved reach
    // path. That is a property of `LightRig::expand` and it is what the CPU probes above contrast
    // with, so it is checked here rather than assumed there.
    for (const scene::PunctualLight& l : s.lights) {
        if (l.type == scene::PunctualLight::Type::Directional) {
            continue;
        }
        INFO("light: " << l.name);
        CHECK(l.range > 0.0f);
        CHECK(rendering::lightInfluenceRadius(l) == Approx(l.range));
    }
}
