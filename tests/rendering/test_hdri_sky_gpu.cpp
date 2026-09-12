// HDRI sky on the GPU (ADR-049). The four claims the feature is made of, each checked against
// what the pixels actually do: the sky sits at infinity, it is drawn at the map's own resolution
// rather than the IBL cube's, its brightness is separable from the light it casts, and a disc
// brighter than the half-float range does not poison the frame.
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

using namespace avgen;

namespace {

constexpr float kPi = 3.14159265358979323846f;

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

// An equirect: a dim uniform sky with one bright disc of angular radius `radius` about `dir`.
// `width` is the only thing that changes between the resolution cases, so a difference in the
// render is a difference in how much of the map reached the screen.
scene::TextureData discMap(std::uint32_t width, const glm::vec3& dir, float radius, float peak,
                           float background) {
    const std::uint32_t height = width / 2;
    scene::TextureData tex;
    tex.name = "sky";
    tex.width = width;
    tex.height = height;
    tex.format = scene::TextureFormat::Rgba32Float;
    tex.data.resize(static_cast<std::size_t>(width) * height * 16);
    auto* px = reinterpret_cast<float*>(tex.data.data());
    const glm::vec3 centre = glm::normalize(dir);
    for (std::uint32_t y = 0; y < height; ++y) {
        const float theta = ((static_cast<float>(y) + 0.5f) / static_cast<float>(height)) * kPi;
        for (std::uint32_t x = 0; x < width; ++x) {
            const float phi = (((static_cast<float>(x) + 0.5f) / static_cast<float>(width)) - 0.5f) * 2.0f * kPi;
            const glm::vec3 d(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
            const float value = std::acos(std::clamp(glm::dot(d, centre), -1.0f, 1.0f)) <= radius ? peak : background;
            const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
            px[i + 0] = px[i + 1] = px[i + 2] = value;
            px[i + 3] = 1.0f;
        }
    }
    return tex;
}

// A small unlit-by-punctual-light diffuse slab low in frame, with sky above it: the frame then has
// a region lit only by the environment and a region that is the environment.
scene::Scene skyScene(scene::TextureData map) {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.gridIntensity = 0.0f;
    s.environment.showSkybox = true;
    s.environment.environmentIntensity = 1.0f;
    s.environment.skyIntensity = 1.0f;
    s.post.bloomEnabled = false;
    s.post.tonemap = scene::TonemapOperator::Clamp;
    s.camera.position = {0.0f, 0.0f, 6.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.environment.environmentMap = s.addTexture(std::move(map));

    const scene::MeshId id = s.addMesh(scene::makePlane(6.0f, 2));
    scene::Entity& e = s.addEntity("slab", id);
    e.transform.position = {0.0f, -1.6f, 0.0f};
    e.material.baseColor = {0.8f, 0.8f, 0.8f};
    e.material.metallic = 0.0f;
    e.material.roughness = 0.9f;
    return s;
}

gpu::Image8 renderWith(const std::unique_ptr<rendering::SceneRenderer>& renderer, const scene::Scene& s, std::uint32_t w = 256,
                       std::uint32_t h = 256) {
    FrameTime t{};
    t.renderTime = 0.0;
    auto img = renderer->renderToImage(s, t, w, h);
    REQUIRE(img.has_value());
    return *img;
}

float luminance8(const std::uint8_t* p) {
    return (0.2126f * static_cast<float>(p[0]) + 0.7152f * static_cast<float>(p[1]) +
            0.0722f * static_cast<float>(p[2])) /
           255.0f;
}

// Mean luminance over a rectangle, in pixels.
float meanLuminance(const gpu::Image8& img, std::uint32_t x0, std::uint32_t y0, std::uint32_t x1,
                    std::uint32_t y1) {
    double sum = 0.0;
    int n = 0;
    for (std::uint32_t y = y0; y < y1; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            sum += static_cast<double>(luminance8(img.pixel(x, y)));
            ++n;
        }
    }
    return n > 0 ? static_cast<float>(sum / n) : 0.0f;
}

float peakLuminance(const gpu::Image8& img, std::uint32_t x0, std::uint32_t y0, std::uint32_t x1,
                    std::uint32_t y1) {
    float peak = 0.0f;
    for (std::uint32_t y = y0; y < y1; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            peak = std::max(peak, luminance8(img.pixel(x, y)));
        }
    }
    return peak;
}

std::unique_ptr<rendering::SceneRenderer> makeRenderer(gpu::Context& ctx, gpu::ShaderLibrary& shaders) {
    auto renderer = std::make_unique<rendering::SceneRenderer>(ctx, shaders);
    if (auto init = renderer->init(); !init) {
        FAIL(init.error().message);
    }
    return renderer;
}

} // namespace

TEST_CASE("HDR environment cache is isolated across same-version scenes", "[sky][hdri][gpu][forensics]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = makeRenderer(*ctx, shaders);
    auto fresh = makeRenderer(*ctx, shaders);

    auto bright = skyScene(discMap(256, glm::vec3(0.5f, 0.35f, -0.8f), 0.05f, 30.0f, 0.05f));
    auto dim = skyScene(discMap(256, glm::vec3(-0.5f, 0.35f, 0.8f), 0.05f, 0.2f, 0.01f));
    REQUIRE(bright.textureVersion == dim.textureVersion);
    REQUIRE(bright.environment.environmentMap == dim.environment.environmentMap);

    FrameTime time{};
    const auto first = renderer->renderToImage(bright, time, 128, 128);
    REQUIRE(first.has_value());
    const auto reused = renderer->renderToImage(dim, time, 128, 128);
    REQUIRE(reused.has_value());
    const auto expected = fresh->renderToImage(dim, time, 128, 128);
    REQUIRE(expected.has_value());
    CHECK(gpu::hashImage(*reused) == gpu::hashImage(*expected));
    CHECK(gpu::hashImage(*first) != gpu::hashImage(*reused));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("the HDRI sky does not move when the camera does", "[sky][hdri][gpu]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = makeRenderer(*ctx, shaders);

    // A disc off to one side, so a sky that translated with the camera would visibly slide.
    scene::Scene s = skyScene(discMap(1024, glm::vec3(0.5f, 0.35f, -0.8f), 0.05f, 30.0f, 0.05f));
    s.entities.clear(); // sky only: every pixel is the background pass
    const gpu::Image8 here = renderWith(renderer, s);

    // The same orientation, 500 units away -- far enough that any position-dependent term would
    // dominate. Only the camera's translation changes.
    const glm::vec3 delta(500.0f, -220.0f, 370.0f);
    s.camera.position += delta;
    s.camera.target += delta;
    const gpu::Image8 there = renderWith(renderer, s);

    int differing = 0;
    int maxChannelDelta = 0;
    for (std::uint32_t y = 0; y < here.height; ++y) {
        for (std::uint32_t x = 0; x < here.width; ++x) {
            for (int c = 0; c < 3; ++c) {
                const int d = std::abs(static_cast<int>(here.pixel(x, y)[c]) - static_cast<int>(there.pixel(x, y)[c]));
                maxChannelDelta = std::max(maxChannelDelta, d);
                if (d > 1) {
                    ++differing;
                }
            }
        }
    }
    INFO("max channel delta " << maxChannelDelta << ", channels differing by more than 1: " << differing);
    CHECK(maxChannelDelta <= 1); // rounding only
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("the background reads the HDRI's own resolution, not the IBL cube's", "[sky][hdri][gpu]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});

    // One small disc dead ahead, at the same angular size in both maps. The IBL chain resamples
    // every map to a 256 px cube and then to a 128 px prefiltered cube, so if the background came
    // from there these two renders would agree. They must not.
    const glm::vec3 ahead(0.0f, 0.0f, -1.0f);
    const float radius = 0.004f; // ~0.23 degrees: under one texel of a 256 px cube face
    auto sceneAt = [&](std::uint32_t width) {
        scene::Scene s = skyScene(discMap(width, ahead, radius, 200.0f, 0.02f));
        s.entities.clear();
        s.environment.environmentIntensity = 0.0f;
        return s;
    };

    // A renderer each: SceneRenderer caches its IBL by texture id and version, and two freshly
    // built Scenes both call their only map id 0 at version 1.
    const gpu::Image8 coarse = renderWith(makeRenderer(*ctx, shaders), sceneAt(256));
    const gpu::Image8 fine = renderWith(makeRenderer(*ctx, shaders), sceneAt(4096));
    const float coarsePeak = peakLuminance(coarse, 100, 100, 156, 156);
    const float finePeak = peakLuminance(fine, 100, 100, 156, 156);
    INFO("peak at 256 px map: " << coarsePeak << ", at 4096 px map: " << finePeak);
    CHECK(finePeak > coarsePeak + 0.2f);
    CHECK(finePeak > 0.5f);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("sky brightness and environment lighting are separate controls", "[sky][hdri][gpu]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = makeRenderer(*ctx, shaders);

    // A uniform sky, so the two regions each measure one thing: the top band is the background
    // pass, the bottom band is a diffuse slab lit by the environment and nothing else.
    auto sceneWith = [](float skyIntensity, float envIntensity) {
        scene::Scene s = skyScene(discMap(512, glm::vec3(0.0f, 1.0f, 0.0f), 0.0f, 0.0f, 0.35f));
        s.environment.skyIntensity = skyIntensity;
        s.environment.environmentIntensity = envIntensity;
        return s;
    };
    const scene::Scene base = sceneWith(1.0f, 1.0f);
    const gpu::Image8 baseImg = renderWith(renderer, base);
    const float baseSky = meanLuminance(baseImg, 40, 10, 216, 50);
    const float baseSlab = meanLuminance(baseImg, 100, 220, 156, 250);
    REQUIRE(baseSky > 0.05f);
    REQUIRE(baseSlab > 0.02f);

    SECTION("dimming the sky leaves the lighting alone") {
        const gpu::Image8 img = renderWith(renderer, sceneWith(0.15f, 1.0f));
        const float sky = meanLuminance(img, 40, 10, 216, 50);
        const float slab = meanLuminance(img, 100, 220, 156, 250);
        INFO("sky " << baseSky << " -> " << sky << ", slab " << baseSlab << " -> " << slab);
        CHECK(sky < baseSky * 0.6f);
        CHECK(std::abs(slab - baseSlab) < baseSlab * 0.05f);
    }

    SECTION("dimming the lighting leaves the sky alone") {
        const gpu::Image8 img = renderWith(renderer, sceneWith(1.0f, 0.15f));
        const float sky = meanLuminance(img, 40, 10, 216, 50);
        const float slab = meanLuminance(img, 100, 220, 156, 250);
        INFO("sky " << baseSky << " -> " << sky << ", slab " << baseSlab << " -> " << slab);
        CHECK(slab < baseSlab * 0.6f);
        CHECK(std::abs(sky - baseSky) < baseSky * 0.05f);
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("a disc past the half-float range does not blacken the frame", "[sky][hdri][gpu]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = makeRenderer(*ctx, shaders);

    // Real skies do this: Kloppenheim 02's moon peaks at 1.0e5 at 4K, past the 65504 a half can
    // hold. Before ADR-049 those texels became +inf, survived the bloom downsample, and came back
    // as a black rectangle hundreds of pixels wide.
    scene::Scene s = skyScene(discMap(1024, glm::vec3(0.0f, 0.2f, -1.0f), 0.03f, 1.0e6f, 0.1f));
    s.entities.clear();
    s.post.bloomEnabled = true;
    s.post.bloomIntensity = 0.6f;
    const gpu::Image8 img = renderWith(renderer, s);

    // A NaN or inf reaching the composite reads back as an all-zero block; nothing in this frame
    // should be black, because the dimmest thing in it is a lit sky.
    int black = 0;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            if (luminance8(img.pixel(x, y)) < 0.01f) {
                ++black;
            }
        }
    }
    INFO("black pixels: " << black << " of " << img.width * img.height);
    CHECK(black == 0);
    CHECK(meanLuminance(img, 0, 0, img.width, img.height) > 0.05f);
    CHECK(ctx->errorCount() == 0);
}
