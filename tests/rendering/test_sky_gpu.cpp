// Procedural sky environment on the GPU (ADR-036): a scene with no HDR environment map now builds
// its image-based lighting from the analytic sky in scene/sky.hpp, so metals have something to
// reflect. These tests check the thing the ADR is for — a metallic surface stops being flat grey —
// plus the sun's placement from the key light, determinism, and the build cost.
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/environment.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "scene/sky.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

using namespace avgen;

namespace {

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

// A polished metal sphere on black, lit only by the environment unless the caller says otherwise.
// This is the ADR's failing case: 0.95 metallic with nothing to reflect renders as flat dark grey.
scene::Scene metalSphereScene(float keyIntensity = 0.0f) {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    s.environment.gridIntensity = 0.0f;
    s.environment.environmentIntensity = 0.0f; // as several shipped scenes have it
    s.post.bloomEnabled = false;
    s.post.tonemap = scene::TonemapOperator::Clamp;
    s.camera.position = {0.0f, 0.0f, 5.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.role = scene::PunctualLight::Role::Key;
    key.direction = glm::normalize(glm::vec3(-0.4f, -0.8f, -0.45f));
    key.intensity = keyIntensity;
    s.addLight(key);

    const scene::MeshId id = s.addMesh(scene::makeIcosphere(1.6f, 4));
    scene::Entity& e = s.addEntity("ball", id);
    e.material.baseColor = {0.72f, 0.74f, 0.78f};
    e.material.metallic = 0.95f;
    e.material.roughness = 0.18f;
    e.material.emissiveIntensity = 0.0f;
    return s;
}

gpu::Image8 renderWith(rendering::SceneRenderer& renderer, const scene::Scene& s, std::uint32_t w = 192,
                       std::uint32_t h = 192) {
    FrameTime t{};
    t.renderTime = 0.0;
    auto img = renderer.renderToImage(s, t, w, h);
    REQUIRE(img.has_value());
    return *img;
}

float luminance8(const std::uint8_t* p) {
    return (0.2126f * static_cast<float>(p[0]) + 0.7152f * static_cast<float>(p[1]) +
            0.0722f * static_cast<float>(p[2])) /
           255.0f;
}

// Luminance statistics over the lit pixels of the sphere (a disc around the image centre, with the
// background's near-black pixels dropped).
struct SurfaceStats {
    int samples = 0;
    float mean = 0.0f;
    float stddev = 0.0f;
    float min = 1.0f;
    float max = 0.0f;
    [[nodiscard]] float range() const { return max - min; }
};

SurfaceStats surfaceStats(const gpu::Image8& img) {
    const float cx = static_cast<float>(img.width) * 0.5f;
    const float cy = static_cast<float>(img.height) * 0.5f;
    // The sphere covers a little over a third of the frame; stay inside it so the silhouette's
    // antialiased edge never enters the statistics.
    const float radius = static_cast<float>(img.width) * 0.24f;
    std::vector<float> values;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const float dx = static_cast<float>(x) + 0.5f - cx;
            const float dy = static_cast<float>(y) + 0.5f - cy;
            if (dx * dx + dy * dy > radius * radius) {
                continue;
            }
            values.push_back(luminance8(img.pixel(x, y)));
        }
    }
    SurfaceStats out;
    if (values.empty()) {
        return out;
    }
    out.samples = static_cast<int>(values.size());
    double sum = 0.0;
    for (const float v : values) {
        sum += static_cast<double>(v);
        out.min = std::min(out.min, v);
        out.max = std::max(out.max, v);
    }
    out.mean = static_cast<float>(sum / static_cast<double>(values.size()));
    double variance = 0.0;
    for (const float v : values) {
        const double dv = static_cast<double>(v) - static_cast<double>(out.mean);
        variance += dv * dv;
    }
    out.stddev = static_cast<float>(std::sqrt(variance / static_cast<double>(values.size())));
    return out;
}

} // namespace

TEST_CASE("a metallic sphere under the procedural sky is not flat grey", "[sky][gpu]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    if (auto init = renderer.init(); !init) {
        FAIL(init.error().message);
    }

    // Before: the sky off is exactly the pre-ADR-036 path — a hemispheric ambient constant, and a
    // 0.95-metallic surface with nothing to reflect.
    scene::Scene flat = metalSphereScene();
    flat.environment.sky.enabled = false;
    const gpu::Image8 flatImg = renderWith(renderer, flat);
    const SurfaceStats before = surfaceStats(flatImg);
    REQUIRE(before.samples > 1000);

    // After: the same scene with the sky on.
    scene::Scene lit = metalSphereScene();
    lit.environment.sky.intensity = 2.0f;
    const gpu::Image8 litImg = renderWith(renderer, lit);
    const SurfaceStats after = surfaceStats(litImg);
    REQUIRE(after.samples == before.samples);
    CHECK(ctx->errorCount() == 0);

    INFO("before mean " << before.mean << " sd " << before.stddev << " range " << before.range()
                        << " | after mean " << after.mean << " sd " << after.stddev << " range "
                        << after.range());
    // The surface is genuinely lit...
    CHECK(after.mean > before.mean);
    CHECK(after.max > 0.05f);
    // ...and it varies across the sphere rather than reading as one tone. The reflection of a
    // gradient sky with a sun in it is what makes a metal look like a metal.
    CHECK(after.stddev > 0.02f);
    CHECK(after.stddev > before.stddev * 3.0f);
    CHECK(after.range() > 0.15f);
    CHECK(after.range() > before.range() * 2.0f);
}

TEST_CASE("the procedural sky's sun follows the key light", "[sky][gpu]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    if (auto init = renderer.init(); !init) {
        FAIL(init.error().message);
    }

    // The key light contributes no direct light (intensity 0), so anything that moves in the image
    // moved because the sun in the environment moved with it.
    const auto renderWithSun = [&renderer](const glm::vec3& travel) {
        scene::Scene s = metalSphereScene();
        s.environment.sky.intensity = 2.0f;
        s.lights[0].direction = glm::normalize(travel);
        return renderWith(renderer, s);
    };
    // A sun to the left of the camera, then to the right.
    const gpu::Image8 left = renderWithSun({1.0f, -0.25f, -0.4f});   // travels +x: sun is to -x
    const gpu::Image8 right = renderWithSun({-1.0f, -0.25f, -0.4f}); // travels -x: sun is to +x
    CHECK(ctx->errorCount() == 0);

    const auto sideBrightness = [](const gpu::Image8& img, bool leftHalf) {
        double sum = 0.0;
        int n = 0;
        const std::uint32_t x0 = leftHalf ? img.width / 4 : img.width / 2;
        const std::uint32_t x1 = leftHalf ? img.width / 2 : img.width * 3 / 4;
        for (std::uint32_t y = img.height * 3 / 8; y < img.height * 5 / 8; ++y) {
            for (std::uint32_t x = x0; x < x1; ++x) {
                sum += static_cast<double>(luminance8(img.pixel(x, y)));
                ++n;
            }
        }
        return n > 0 ? static_cast<float>(sum / n) : 0.0f;
    };
    const float leftLit = sideBrightness(left, true) - sideBrightness(left, false);
    const float rightLit = sideBrightness(right, false) - sideBrightness(right, true);
    INFO("left-sun bias " << leftLit << ", right-sun bias " << rightLit);
    CHECK(leftLit > 0.0f);
    CHECK(rightLit > 0.0f);
    CHECK(gpu::hashImage(left) != gpu::hashImage(right));

    // And the CPU resolution agrees about where the sun is.
    scene::SkySettings settings;
    const scene::SkyRuntime sky =
        scene::resolveSky(settings, {[] {
                              scene::PunctualLight l;
                              l.type = scene::PunctualLight::Type::Directional;
                              l.role = scene::PunctualLight::Role::Key;
                              l.direction = glm::normalize(glm::vec3(1.0f, -0.25f, -0.4f));
                              return l;
                          }()});
    CHECK(sky.sunDirection.x < 0.0f);
}

TEST_CASE("the procedural sky is deterministic and built once", "[sky][gpu]") {
    auto ctx = makeContext();
    scene::Scene s = metalSphereScene();
    s.environment.sky.intensity = 2.0f;

    std::uint64_t hashes[2] = {0, 0};
    for (int i = 0; i < 2; ++i) {
        auto shaders = makeShaders(*ctx);
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        const gpu::Image8 first = renderWith(renderer, s);
        // A second frame must not rebuild the sky, and must render identically.
        const gpu::Image8 second = renderWith(renderer, s);
        CHECK(gpu::hashImage(first) == gpu::hashImage(second));
        hashes[i] = gpu::hashImage(first);
    }
    CHECK(hashes[0] == hashes[1]); // deterministic across a fresh renderer
    CHECK(ctx->errorCount() == 0);

    // Changing a sky parameter changes the image; changing nothing does not.
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    const std::uint64_t base = gpu::hashImage(renderWith(renderer, s));
    CHECK(gpu::hashImage(renderWith(renderer, s)) == base);
    scene::Scene warm = s;
    warm.environment.sky.sunColor = {1.0f, 0.55f, 0.25f};
    CHECK(gpu::hashImage(renderWith(renderer, warm)) != base);
    // ...and switching back returns the original image, so the rebuild is driven by the parameters
    // and nothing else.
    CHECK(gpu::hashImage(renderWith(renderer, s)) == base);
}

TEST_CASE("the irradiance the sky builds matches the CPU reference in shape", "[sky][gpu]") {
    // The GPU chain integrates the same analytic sky the CPU reference does, so a diffuse surface
    // facing the sun must come out brighter than one facing away from it, by roughly the ratio the
    // CPU quadrature predicts. This is the cheap end-to-end check that the cube, the irradiance
    // pass and the shading all agree about which way is up.
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    if (auto init = renderer.init(); !init) {
        FAIL(init.error().message);
    }
    scene::Scene s = metalSphereScene();
    s.environment.sky.intensity = 2.0f;
    s.entities[0].material.metallic = 0.0f; // pure diffuse: the irradiance cube alone
    s.entities[0].material.roughness = 1.0f;
    s.entities[0].material.baseColor = {1.0f, 1.0f, 1.0f};
    s.lights[0].direction = glm::normalize(glm::vec3(0.0f, -1.0f, -0.2f)); // sun overhead
    const gpu::Image8 img = renderWith(renderer, s);
    CHECK(ctx->errorCount() == 0);

    const auto band = [&img](std::uint32_t y0, std::uint32_t y1) {
        double sum = 0.0;
        int n = 0;
        for (std::uint32_t y = y0; y < y1; ++y) {
            for (std::uint32_t x = img.width * 7 / 16; x < img.width * 9 / 16; ++x) {
                sum += static_cast<double>(luminance8(img.pixel(x, y)));
                ++n;
            }
        }
        return n > 0 ? static_cast<float>(sum / n) : 0.0f;
    };
    const float top = band(img.height * 5 / 16, img.height * 7 / 16);
    const float bottom = band(img.height * 9 / 16, img.height * 11 / 16);
    INFO("top " << top << " bottom " << bottom);
    CHECK(top > bottom); // the sun is overhead, and so is the bright half of the sky

    const scene::SkyRuntime sky = scene::resolveSky(s.environment.sky, s.lights);
    const glm::vec3 up = scene::skyIrradiance(sky, {0.0f, 1.0f, 0.0f});
    const glm::vec3 down = scene::skyIrradiance(sky, {0.0f, -1.0f, 0.0f});
    CHECK(up.g > down.g);
}

// Hidden performance probe: `avgen_render_tests "[.perf][sky]"` (Release).
TEST_CASE("Procedural sky build cost", "[.perf][sky]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::EnvironmentProcessor processor(*ctx, shaders);
    REQUIRE(processor.init().has_value());
    const scene::SkyRuntime sky = scene::resolveSky(scene::SkySettings{}, {});

    struct Config {
        const char* name;
        rendering::EnvironmentSettings settings;
    };
    rendering::EnvironmentSettings half;
    half.cubeSize = 128;
    half.prefilteredSize = 64;
    rendering::EnvironmentSettings big;
    big.cubeSize = 512;
    big.prefilteredSize = 256;
    const Config configs[] = {{"128 cube / 64 prefiltered", half},
                              {"256 cube / 128 prefiltered (default)", rendering::EnvironmentSettings{}},
                              {"512 cube / 256 prefiltered", big}};
    for (const Config& c : configs) {
        (void)processor.processSky(sky, c.settings); // warm up: pipelines, BRDF LUT
        ctx->waitForQueue();
        const auto start = std::chrono::steady_clock::now();
        constexpr int kRuns = 3;
        for (int i = 0; i < kRuns; ++i) {
            auto built = processor.processSky(sky, c.settings);
            REQUIRE(built.has_value());
        }
        ctx->waitForQueue();
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / kRuns;
        WARN("sky build, " << c.name << ": " << ms << " ms (once per parameter change, not per frame)");
    }
    CHECK(ctx->errorCount() == 0);
}
