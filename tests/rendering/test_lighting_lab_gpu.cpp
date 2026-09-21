// The Lighting Lab's GPU half (§7 of docs/engineering-labs.md, lab #7).
//
// §37: the renderer is the source of truth. Everything in tests/unit/test_lighting_lab.cpp is an
// assertion about a number the CPU computes *before* a fragment exists -- a reach, a froxel index,
// an assignment. This file is the half that asks the only question that settles those: **did the
// pixel change?**
//
// Two probes, and each has a control that says it can fail.
//
//   1. A local light's reach is a hard edge. Past `lightInfluenceRadius` the froxel pass does not
//      assign the light at all, so whatever it was contributing stops in the width of one froxel.
//      The probe walks a floor away from an area light and asks whether the fall-off has a cliff
//      in it. The control is the same walk with an explicit `range`, where the shader's own window
//      has closed to zero by the time the reach runs out and there is nothing to fall off.
//
//   2. The froxel a fragment reads is the froxel the light was put in. The existing clustered
//      test (`test_shadows_gpu.cpp`) compares the clustered path with the uniform fallback for one
//      **directional** light -- which never enters the grid at all, so it exercises none of this.
//      That is this suite's centred-box fixture (ADR-182). The probe here is the same comparison
//      with an off-centre, off-axis **point** light, which is the case a grid with its rows the
//      wrong way up gets wrong and the directional case cannot.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/light_data.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;

namespace {

constexpr std::uint32_t kSize = 256;

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

gpu::ShaderLibrary makeShaders(gpu::Context& ctx) {
    return gpu::ShaderLibrary(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
}

std::unique_ptr<rendering::SceneRenderer> makeRenderer(gpu::Context& ctx, gpu::ShaderLibrary& shaders) {
    auto renderer = std::make_unique<rendering::SceneRenderer>(ctx, shaders);
    REQUIRE(renderer->init().has_value());
    return renderer;
}

scene::MeshData boxMesh(glm::vec3 half) {
    scene::MeshData m;
    const glm::vec3 n[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (const glm::vec3 normal : n) {
        const glm::vec3 u = std::abs(normal.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::cross(glm::vec3(0, 1, 0), normal);
        const glm::vec3 v = glm::cross(normal, u);
        const glm::vec3 c = normal * half;
        const glm::vec3 du = u * half;
        const glm::vec3 dv = v * half;
        const auto base = static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({c - du - dv, normal, {0, 0}});
        m.vertices.push_back({c + du - dv, normal, {1, 0}});
        m.vertices.push_back({c + du + dv, normal, {1, 1}});
        m.vertices.push_back({c - du + dv, normal, {0, 1}});
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return m;
}

FrameTime frameAt(std::uint64_t index) {
    FrameTime t;
    t.frameIndex = index;
    t.renderTime = static_cast<double>(index) / 60.0;
    t.deltaTime = 1.0 / 60.0;
    return t;
}

float luminanceAt(const gpu::Image8& image, std::uint32_t x, std::uint32_t y) {
    const std::size_t index = (static_cast<std::size_t>(y) * image.width + x) * 4;
    REQUIRE(index + 2 < image.rgba.size());
    return (0.2126f * static_cast<float>(image.rgba[index]) + 0.7152f * static_cast<float>(image.rgba[index + 1]) +
            0.0722f * static_cast<float>(image.rgba[index + 2])) /
           255.0f;
}

// A long corridor of floor seen edge-on from one end, with one local light at the near end and
// nothing else in the frame: no sky, no environment, no ambient, no second light. Every pixel down
// the middle column of the image is a point on the floor at a known depth, so the image *is* the
// light's fall-off curve and a cliff in it is a cliff in the curve.
scene::Scene corridorScene(scene::PunctualLight light) {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.environmentIntensity = 0.0f;
    s.environment.skyIntensity = 0.0f;
    s.environment.lightFromEnvironment = false;

    // Looking down the corridor from just above the floor: the far plane is past the end of the
    // floor so the fall-off runs out before the geometry does.
    s.camera.position = {0.0f, 2.2f, 6.0f};
    s.camera.target = {0.0f, 0.0f, -90.0f};
    s.camera.fovYRadians = 0.9f;
    s.camera.nearPlane = 0.25f;
    s.camera.farPlane = 400.0f;

    const auto floor = s.addMesh(boxMesh({14.0f, 0.25f, 150.0f}));
    auto& e = s.addEntity("floor", floor);
    e.transform.position = {0.0f, -0.25f, -140.0f};
    e.material.baseColor = glm::vec3(0.8f);
    e.material.roughness = 1.0f;  // pure diffuse: the probe is about irradiance, not a highlight
    e.material.metallic = 0.0f;
    s.addLight(std::move(light));
    return s;
}

// The luminance of the floor down the middle of the image, from the horizon end back towards the
// camera, with the sky rows dropped.
std::vector<float> falloffProfile(const gpu::Image8& image) {
    std::vector<float> out;
    for (std::uint32_t y = 0; y < image.height; ++y) {
        const float l = luminanceAt(image, image.width / 2, y);
        if (l > 0.0f) {
            out.push_back(l);
        }
    }
    return out;
}

// The sharpest step anywhere in a profile, as a ratio between neighbouring samples, ignoring
// samples too dark to be a ratio about. A smooth inverse-square fall-off over 256 rows steps by a
// few per cent; a light switching off steps by everything it was contributing.
float sharpestStep(const std::vector<float>& profile, float floorValue, std::size_t& whereOut) {
    float worst = 1.0f;
    whereOut = 0;
    for (std::size_t i = 1; i < profile.size(); ++i) {
        const float a = profile[i - 1];
        const float b = profile[i];
        if (a < floorValue && b < floorValue) {
            continue;
        }
        const float ratio = std::max(a, b) / std::max(std::min(a, b), 1e-4f);
        if (ratio > worst) {
            worst = ratio;
            whereOut = i;
        }
    }
    return worst;
}

} // namespace

TEST_CASE("an area light with no authored range reaches as far as one that has it",
          "[gpu][lighting][lab]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);

    // The probe is an A/B on the reach itself, and the control is the arm that *defines* the right
    // answer rather than merely being different from it.
    //
    //   arm     `range = 0`: the reach comes from `lightInfluenceRadius`'s heuristic, which is the
    //           path a glTF-imported light takes (`range` is optional in KHR_lights_punctual and is
    //           usually absent) and the path every light built in code takes. `LightRig::expand`
    //           sets a range on everything it makes, so no rig reaches it.
    //   control an identical light with an explicit range far past the end of the corridor. Its
    //           froxel reach is that range, and its own window `(1 - (d/range)^4)^2` is within
    //           0.05% of 1 everywhere in frame -- so the control is what the arm *would* look like
    //           if its reach were right, measured rather than asserted.
    //
    // Comparing two frames rather than hunting a step inside one is what makes this legible: the
    // difference between them is exactly the light the heuristic threw away, with no ambient floor
    // and no tone curve in it.
    scene::PunctualLight rect;
    rect.name = "sky-panel";
    rect.type = scene::PunctualLight::Type::Rect;
    rect.position = {0.0f, 30.0f, 0.0f};
    rect.direction = glm::normalize(glm::vec3(0.0f, -1.0f, -0.4f));
    rect.intensity = 3.0f; // nits over the emitter
    // Large, because the size of the defect is the size of the emitter and nothing else: the
    // radiance a rect light still carries where a reach computed from its radiance alone runs out
    // is `cutoff * area / pi`, which is independent of how bright it is.
    rect.width = 40.0f;
    rect.height = 28.0f;
    rect.range = 0.0f;
    rect.castsShadow = false;
    rect.contactShadow = false;

    scene::PunctualLight ranged = rect;
    ranged.range = 600.0f; // past the far end of the corridor

    const float heuristic = rendering::lightInfluenceRadius(rect);
    INFO("heuristic reach " << heuristic << " m against an authored " << ranged.range << " m");

    // **Post off.** This probe is about radiance, and the post chain is the HDR Lab's subject: AgX
    // plus the exposure meter compress the step this is looking for into a fraction of itself, and
    // the meter adapts *per frame*, so the two arms would also differ by a global exposure change
    // that has nothing to do with either light. `--disable post` is the arm the repository already
    // has for exactly this, and the boundary between the two labs is drawn here rather than argued
    // about.
    rendering::SceneRenderer::PassToggles toggles;
    REQUIRE(rendering::SceneRenderer::setPassArm(toggles, "post", false));

    auto armRenderer = makeRenderer(*ctx, shaders);
    armRenderer->setPassToggles(toggles);
    auto arm = armRenderer->renderToImage(corridorScene(rect), frameAt(4), kSize, kSize);
    REQUIRE(arm.has_value());

    auto controlRenderer = makeRenderer(*ctx, shaders);
    controlRenderer->setPassToggles(toggles);
    auto control = controlRenderer->renderToImage(corridorScene(ranged), frameAt(4), kSize, kSize);
    REQUIRE(control.has_value());

    double sum = 0.0;
    double worst = 0.0;
    double level = 0.0;
    for (std::size_t i = 0; i + 3 < arm->rgba.size(); i += 4) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const double d = std::abs(static_cast<double>(arm->rgba[i + channel]) -
                                      static_cast<double>(control->rgba[i + channel]));
            sum += d;
            worst = std::max(worst, d);
            level += static_cast<double>(control->rgba[i + channel]);
        }
    }
    const double samples = static_cast<double>(arm->rgba.size()) * 0.75;
    const double mean = sum / samples;
    const double meanLevel = level / samples;
    INFO("mean channel difference " << mean << ", worst " << worst << ", over a control level of "
                                    << meanLevel);
    // The fall-off curve itself, on demand. It is behind an environment variable rather than an
    // assertion because it is the instrument this probe was *designed* with -- the first two
    // versions passed with the defect present and reading the curve is what showed why -- and a
    // 256-row dump in every run is noise nobody reads.
    if (std::getenv("AVGEN_LAB_DUMP") != nullptr) {
        const std::vector<float> a = falloffProfile(*arm);
        const std::vector<float> b = falloffProfile(*control);
        for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
            std::fprintf(stderr, "PROFILE %zu %.6f %.6f\n", i, static_cast<double>(a[i]),
                         static_cast<double>(b[i]));
        }
    }

    // The control that says the comparison is about a lit frame rather than two dark ones: the
    // corridor really is lit, and the nearest floor is bright.
    CHECK(meanLevel > 20.0);
    // One profile, held. This took `begin()` of one temporary and `end()` of another, a range
    // spanning two unrelated allocations, and scanning it read off the end of the heap: SIGBUS on
    // 2026-09-21, and a pass only when the two allocations happened to fall in a benign order.
    const std::vector<float> controlProfile = falloffProfile(*control);
    REQUIRE_FALSE(controlProfile.empty());
    CHECK(*std::max_element(controlProfile.begin(), controlProfile.end()) > 0.5f);

    // The invariant. A reach that stops a light while it is still contributing shows up here as
    // the whole of that contribution, over the whole of the far half of the corridor.
    //
    // Both numbers are stated because they say different things and both had teeth. Before ADR-272
    // the worst channel difference was **48** of 255 and the mean over the frame **0.261**; after
    // it, 1 and 0.00034. A threshold on the mean alone would have passed with the defect in place,
    // because the far half of a corridor seen edge-on is a small share of the image -- which is
    // exactly the mistake a whole-frame metric invites.
    CHECK(worst < 6.0);
    CHECK(mean < 0.02);
}

TEST_CASE("the clustered path and the uniform fallback agree on an off-centre point light",
          "[gpu][lighting][lab][clusters]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);

    // Off-centre in x, off-centre in y and off the view axis in z: the three coordinates a grid
    // with a flipped or transposed lookup gets wrong. A light on the view axis is this suite's
    // centred box -- the wrong grid and the right grid put it in the same froxel.
    scene::PunctualLight lamp;
    lamp.name = "off-centre-lamp";
    lamp.type = scene::PunctualLight::Type::Point;
    lamp.position = {-7.5f, 5.5f, -22.0f};
    lamp.intensity = 260.0f;
    lamp.range = 60.0f;
    lamp.color = glm::vec3(1.0f, 0.85f, 0.7f);
    lamp.castsShadow = false;
    lamp.contactShadow = false;

    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.environmentIntensity = 0.0f;
    s.environment.skyIntensity = 0.0f;
    s.environment.lightFromEnvironment = false;
    s.camera.position = {0.0f, 8.0f, 18.0f};
    s.camera.target = {0.0f, 1.0f, -30.0f};
    s.camera.fovYRadians = 0.9f;
    s.camera.nearPlane = 0.5f;
    s.camera.farPlane = 200.0f;
    const auto floor = s.addMesh(boxMesh({40.0f, 0.25f, 60.0f}));
    {
        auto& e = s.addEntity("floor", floor);
        e.transform.position = {0.0f, -0.25f, -30.0f};
        e.material.baseColor = glm::vec3(0.8f);
        e.material.roughness = 1.0f;
    }
    // A back wall, so the lit pool is not only on one plane: a y-flip that happened to cancel on a
    // horizontal floor cannot cancel on a vertical surface too.
    const auto wall = s.addMesh(boxMesh({40.0f, 14.0f, 0.25f}));
    {
        auto& e = s.addEntity("wall", wall);
        e.transform.position = {0.0f, 14.0f, -62.0f};
        e.material.baseColor = glm::vec3(0.8f);
        e.material.roughness = 1.0f;
    }
    s.addLight(lamp);

    rendering::QualitySettings quality = rendering::QualitySettings::forTier(rendering::QualityTier::Realtime);
    quality.ambientOcclusion = false;
    quality.contactShadows = false;

    auto clustered = makeRenderer(*ctx, shaders);
    clustered->setQualitySettings(quality);
    auto a = clustered->renderToImage(s, frameAt(5), kSize, kSize);
    REQUIRE(a.has_value());

    rendering::QualitySettings fallbackQuality = quality;
    fallbackQuality.clusteredLighting = false; // the only difference
    auto fallback = makeRenderer(*ctx, shaders);
    fallback->setQualitySettings(fallbackQuality);
    auto b = fallback->renderToImage(s, frameAt(5), kSize, kSize);
    REQUIRE(b.has_value());

    double diff = 0.0;
    double clusteredSum = 0.0;
    for (std::size_t i = 0; i + 3 < a->rgba.size(); i += 4) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            diff += std::abs(static_cast<double>(a->rgba[i + channel]) - static_cast<double>(b->rgba[i + channel]));
            clusteredSum += static_cast<double>(a->rgba[i + channel]);
        }
    }
    const double samples = static_cast<double>(a->rgba.size()) * 0.75;
    const double mean = diff / samples;
    const double meanLevel = clusteredSum / samples;
    INFO("mean channel difference " << mean << " over a mean level of " << meanLevel);
    // The control that says the comparison is about a lit frame: two black images agree perfectly.
    CHECK(meanLevel > 8.0);
    CHECK(mean < 3.0);
}
