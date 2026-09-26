// ADR-919: the offline tier raises the sample counts a scene authored below its floors.
//
// Offline used to scale the authored counts by 1 -- which is to say it rendered a final with the
// march steps, texture filtering and sky resolution the preview was tuned with. The floors here
// raise what is below them and leave alone what is above: the volumetric march's steps, material
// textures' anisotropic filtering, and the procedural sky's cube -- whose prefiltered first mip is
// the visible sky, magnified about 38x across a 4K frame at the default 128 px a face.

#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/render_quality.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "scene/sky.hpp"

#include "support/post_bench.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

scene::Scene foggyScene(int steps) {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.02f, 0.03f, 0.06f);
    s.environment.showSkybox = false;
    s.environment.volumeDensity = 0.05f;
    s.environment.volumeMaxDistance = 60.0f; // the march runs
    s.environment.volumeSteps = steps;
    s.camera.position = {0.0f, 2.0f, 10.0f};
    s.camera.target = {0.0f, 1.0f, 0.0f};
    const auto mesh = s.addMesh(scene::makeCube(1.0f));
    auto& e = s.addEntity("block", mesh);
    e.material.baseColor = glm::vec3(0.6f);
    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.3f, -0.8f, -0.4f));
    key.intensity = 2.0f;
    s.addLight(key);
    return s;
}

} // namespace

TEST_CASE("the offline tier's step floor reaches the march, raising and never lowering", "[gpu][quality][offline]") {
    auto ctx = testsupport::gpuContextOrSkip();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    const FrameTime time{.renderTime = 1.0, .deltaTime = 1.0 / 60.0, .frameIndex = 1};

    auto marched = [&](rendering::QualityTier tier, int authored) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        renderer.setQuality(tier);
        REQUIRE(renderer.renderToImage(foggyScene(authored), time, 160, 100).has_value());
        return renderer.stats().volume;
    };
    // Glowmere Valley 3's 12 steps: 32 at offline, 12 at realtime -- the control, which is also
    // what offline did before ADR-919.
    const rendering::VolumeStats offline = marched(rendering::QualityTier::Offline, 12);
    const rendering::VolumeStats realtime = marched(rendering::QualityTier::Realtime, 12);
    CHECK(offline.authoredSteps == 12u);
    CHECK(offline.steps == 32u);
    CHECK(realtime.steps == 12u);
    // A scene that authored more than the floor keeps what it authored.
    CHECK(marched(rendering::QualityTier::Offline, 48).steps == 48u);
    CHECK(ctx->errorCount() == 0);
}

// ---- anisotropic filtering ------------------------------------------------------------------------

namespace {

// A checkerboard floor seen at a grazing angle, where a texel's footprint is far longer than it is
// wide: exactly the case anisotropic filtering exists for, and past 8:1 the case 16x differs from 8x.
scene::Scene grazingFloor() {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.environmentIntensity = 0.0f;
    s.camera.position = {0.0f, 0.6f, 0.0f};
    s.camera.target = {0.0f, 0.45f, -20.0f};
    scene::TextureData checker;
    checker.name = "checker";
    checker.width = 256;
    checker.height = 256;
    checker.format = scene::TextureFormat::Rgba8Unorm;
    checker.data.resize(256 * 256 * 4);
    for (std::uint32_t y = 0; y < 256; ++y) {
        for (std::uint32_t x = 0; x < 256; ++x) {
            const std::uint8_t v = ((x / 8 + y / 8) % 2 == 0) ? 255 : 0;
            std::uint8_t* p = checker.data.data() + (static_cast<std::size_t>(y) * 256 + x) * 4;
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    }
    const scene::TextureId texture = s.addTexture(std::move(checker));
    scene::MeshData floor;
    floor.name = "floor";
    const float half = 200.0f;
    const float repeats = 400.0f; // one checker texture per metre
    floor.vertices = {{{-half, 0.0f, -half}, {0, 1, 0}, {0.0f, 0.0f}},
                      {{half, 0.0f, -half}, {0, 1, 0}, {repeats, 0.0f}},
                      {{half, 0.0f, half}, {0, 1, 0}, {repeats, repeats}},
                      {{-half, 0.0f, half}, {0, 1, 0}, {0.0f, repeats}}};
    floor.indices = {0, 2, 1, 0, 3, 2};
    const auto mesh = s.addMesh(std::move(floor));
    auto& e = s.addEntity("floor", mesh);
    e.material.baseColor = glm::vec3(1.0f);
    e.material.unlit = true;
    e.material.baseColorTexture.texture = texture;
    e.material.doubleSided = true; // whichever way the quad winds, it is seen
    s.post.bloomEnabled = false;
    s.post.bloomIntensity = 0.0f;
    return s;
}

} // namespace

TEST_CASE("the offline tier's anisotropy floor reaches the picture", "[gpu][quality][offline]") {
    auto ctx = testsupport::gpuContextOrSkip();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    const FrameTime time{.renderTime = 0.0, .deltaTime = 1.0 / 60.0, .frameIndex = 0};

    // The same tier, so nothing moves but the one setting: realtime at its own 8x, then with the
    // offline floor's 16x.
    auto render = [&](std::uint32_t anisotropy) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        renderer.setQuality(rendering::QualityTier::Realtime);
        rendering::QualitySettings q = renderer.qualitySettings();
        q.textureAnisotropy = anisotropy;
        renderer.setQualitySettings(q);
        auto image = renderer.renderToImage(grazingFloor(), time, 320, 200);
        REQUIRE(image.has_value());
        return std::move(*image);
    };
    const gpu::Image8 eight = render(8);
    const gpu::Image8 eightAgain = render(8);
    const gpu::Image8 sixteen = render(16);
    std::size_t changed = 0;
    std::size_t repeatChanged = 0;
    for (std::size_t i = 0; i + 3 < eight.rgba.size(); i += 4) {
        changed += (eight.rgba[i] != sixteen.rgba[i]) ? 1 : 0;
        repeatChanged += (eight.rgba[i] != eightAgain.rgba[i]) ? 1 : 0;
    }
    UNSCOPED_INFO("pixels that 16x anisotropy changes against 8x: " << changed << " of " << eight.rgba.size() / 4);
    CHECK(repeatChanged == 0); // the control: the same setting twice is the same picture
    CHECK(changed > 100);      // and the floor's setting is a different one
    CHECK(rendering::QualitySettings::forTier(rendering::QualityTier::Offline).textureAnisotropy == 16u);
    CHECK(rendering::QualitySettings::forTier(rendering::QualityTier::Realtime).textureAnisotropy == 8u);
    CHECK(ctx->errorCount() == 0);
}

// ---- the visible sky at 4K ------------------------------------------------------------------------

namespace {

// Looking along the horizon through a narrow lens: 512 px over 4.75 degrees is 108 px a degree, the
// pixel density of a 2160-line frame at supersample 2 behind a 40 degree lens -- Glowmere Valley 3's
// final. The horizon band and the haze are the steepest part of the sky, where a coarse cube's
// linear interpolation between texels shows as creases.
constexpr std::uint32_t kSkySide = 512;
constexpr float kSkyFovDegrees = 4.75f;

scene::Scene skyAtFourK() {
    scene::Scene s;
    s.environment.showSkybox = true;
    s.environment.environmentIntensity = 1.0f;
    s.environment.sky.enabled = true;
    s.environment.sky.showBackground = true;
    s.environment.sky.zenithColor = {0.008f, 0.016f, 0.048f}; // Glowmere's night sky
    s.environment.sky.horizonColor = {0.04f, 0.08f, 0.17f};
    s.environment.sky.groundColor = {0.0012f, 0.0021f, 0.0058f};
    s.environment.sky.hazeWidth = 0.24f;
    s.environment.sky.sunIntensity = 0.0f;
    s.camera.position = {0.0f, 0.0f, 0.0f};
    // 1.5 degrees above the horizon, so the column crosses the horizon band into the haze.
    s.camera.target = {0.0f, std::tan(glm::radians(1.5f)) * 100.0f, -100.0f};
    s.camera.fovYRadians = glm::radians(kSkyFovDegrees);
    s.post.bloomEnabled = false;
    s.post.bloomIntensity = 0.0f;
    return s;
}

struct Banding {
    double reconstruction = 0.0; // worst |rendered / analytic - 1| down the column, after the scale
    double creases = 0.0;        // the worst jump in slope, as a multiple of the column's mean slope
};

// The centre column of the rendered sky, against the analytic sky it was built from.
Banding measure(const gpu::ImageF& image, const scene::Scene& s) {
    const scene::SkyRuntime sky = scene::resolveSky(s.environment.sky, s.lights);
    const glm::vec3 forward = glm::normalize(s.camera.target - s.camera.position);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::cross(right, forward);
    const float t = std::tan(s.camera.fovYRadians * 0.5f);
    const std::uint32_t x = image.width / 2;
    std::vector<double> rendered;
    std::vector<double> ratio;
    for (std::uint32_t y = 4; y + 4 < image.height; ++y) {
        const float ndcY = 1.0f - 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(image.height);
        const glm::vec3 dir = glm::normalize(forward + up * (ndcY * t));
        const glm::vec3 truth = scene::skyRadiance(sky, dir);
        const float* p = image.pixel(x, y);
        const double l = 0.2126 * static_cast<double>(p[0]) + 0.7152 * static_cast<double>(p[1]) +
                         0.0722 * static_cast<double>(p[2]);
        const double lt = 0.2126 * static_cast<double>(truth.r) + 0.7152 * static_cast<double>(truth.g) +
                          0.0722 * static_cast<double>(truth.b);
        rendered.push_back(l);
        ratio.push_back(lt > 0.0 ? l / lt : 1.0);
    }
    std::vector<double> sorted = ratio;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<long>(sorted.size() / 2), sorted.end());
    const double median = sorted[sorted.size() / 2];
    Banding out;
    for (const double r : ratio) {
        out.reconstruction = std::max(out.reconstruction, std::abs(r / median - 1.0));
    }
    double meanSlope = 0.0;
    for (std::size_t i = 1; i < rendered.size(); ++i) {
        meanSlope += std::abs(rendered[i] - rendered[i - 1]);
    }
    meanSlope /= static_cast<double>(rendered.size() - 1);
    for (std::size_t i = 2; i < rendered.size(); ++i) {
        const double jump = std::abs((rendered[i] - rendered[i - 1]) - (rendered[i - 1] - rendered[i - 2]));
        out.creases = std::max(out.creases, meanSlope > 0.0 ? jump / meanSlope : 0.0);
    }
    return out;
}

} // namespace

TEST_CASE("the offline sky floor takes the banding out of the visible sky at 4K", "[gpu][quality][offline][sky]") {
    auto ctx = testsupport::gpuContextOrSkip();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    const FrameTime time{.renderTime = 0.0, .deltaTime = 1.0 / 60.0, .frameIndex = 0};
    const scene::Scene s = skyAtFourK();

    auto render = [&](rendering::QualityTier tier, std::uint32_t& prefiltered, std::uint32_t& source) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        renderer.setQuality(tier);
        auto image = renderer.renderToImageFloat(s, time, kSkySide, kSkySide);
        REQUIRE(image.has_value());
        prefiltered = renderer.ibl().prefilteredSize;
        source = renderer.ibl().sourceCubeSize;
        return std::move(*image);
    };
    std::uint32_t realtimePrefiltered = 0;
    std::uint32_t realtimeSource = 0;
    std::uint32_t offlinePrefiltered = 0;
    std::uint32_t offlineSource = 0;
    const gpu::ImageF realtime = render(rendering::QualityTier::Realtime, realtimePrefiltered, realtimeSource);
    const gpu::ImageF offline = render(rendering::QualityTier::Offline, offlinePrefiltered, offlineSource);

    // The floor reached the sky's build...
    CHECK(realtimeSource == 256u);
    CHECK(realtimePrefiltered == 128u);
    CHECK(offlineSource == 1024u);
    CHECK(offlinePrefiltered == 1024u);

    // ...and the picture. The realtime arm is the control: at 128 px a face the column is a string
    // of straight segments about 76 px long, and the creases between them are what reads as bands.
    const Banding before = measure(realtime, s);
    const Banding after = measure(offline, s);
    UNSCOPED_INFO("worst deviation from the analytic sky: 128 px faces " << before.reconstruction << ", 1024 px faces "
                                                                         << after.reconstruction);
    UNSCOPED_INFO("worst crease (slope jump / mean slope): 128 px faces " << before.creases << ", 1024 px faces "
                                                                          << after.creases);
    CHECK(before.reconstruction > 0.01);           // the control can fail: the coarse sky is measurably wrong
    CHECK(after.reconstruction < before.reconstruction / 4.0);
    CHECK(after.creases < before.creases / 2.0);
    CHECK(ctx->errorCount() == 0);
}
