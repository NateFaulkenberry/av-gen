// ADR-1070: the live sky. A performance that routes the procedural sky's colours moves it on every
// frame; live, the background is drawn from the frame's own values and the lighting cube is rebuilt
// at a capped rate, its passes spread across frames. These pin the three things that has to keep:
// the live background is the sky the offline cube shows, the lighting a live build lands is the
// lighting the blocking build makes, and the cap holds.
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/environment.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>

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

// A dusk sky standing behind a polished sphere, the sun low in frame: the background, the disc and
// the reflected lighting all in one picture.
scene::Scene duskScene() {
    scene::Scene s;
    s.environment.showSkybox = true;
    s.environment.sky.showBackground = true;
    s.environment.gridIntensity = 0.0f;
    s.environment.sky.zenithColor = {0.10f, 0.12f, 0.30f};
    s.environment.sky.horizonColor = {0.90f, 0.45f, 0.30f};
    s.environment.sky.intensity = 1.3f;
    s.environment.sky.sunIntensity = 6.0f;
    s.post.bloomEnabled = false;
    s.post.tonemap = scene::TonemapOperator::Clamp;
    s.camera.position = {0.0f, 0.4f, 5.0f};
    s.camera.target = {0.0f, 0.6f, 0.0f};
    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.role = scene::PunctualLight::Role::Key;
    key.direction = glm::normalize(glm::vec3(0.25f, -0.12f, 1.0f)); // the sun ahead, low
    key.intensity = 0.0f;
    s.addLight(key);
    const scene::MeshId id = s.addMesh(scene::makeIcosphere(1.0f, 4));
    scene::Entity& e = s.addEntity("ball", id);
    e.material.baseColor = {0.72f, 0.74f, 0.78f};
    e.material.metallic = 0.95f;
    e.material.roughness = 0.25f;
    return s;
}

gpu::Image8 render(rendering::SceneRenderer& renderer, const scene::Scene& s) {
    FrameTime t{};
    auto img = renderer.renderToImage(s, t, 192, 192);
    REQUIRE(img.has_value());
    return *img;
}

struct Diff {
    double mean = 0.0; // mean absolute difference per channel, 8-bit levels
    int max = 0;
    int over4 = 0;     // channels more than 4 levels apart
};

Diff diff(const gpu::Image8& a, const gpu::Image8& b, bool sphereOnly = false, bool backgroundOnly = false) {
    Diff d;
    long n = 0;
    double sum = 0.0;
    for (std::uint32_t y = 0; y < a.height; ++y) {
        for (std::uint32_t x = 0; x < a.width; ++x) {
            const float dx = static_cast<float>(x) + 0.5f - 96.0f;
            const float dy = static_cast<float>(y) + 0.5f - 120.0f; // the sphere's centre in the frame
            const bool inSphere = dx * dx + dy * dy < 30.0f * 30.0f;
            const bool clearOfSphere = dx * dx + dy * dy > 50.0f * 50.0f;
            if ((sphereOnly && !inSphere) || (backgroundOnly && !clearOfSphere)) {
                continue;
            }
            for (int c = 0; c < 3; ++c) {
                const int v = std::abs(int(a.pixel(x, y)[c]) - int(b.pixel(x, y)[c]));
                sum += v;
                d.max = std::max(d.max, v);
                d.over4 += v > 4 ? 1 : 0;
                ++n;
            }
        }
    }
    d.mean = n > 0 ? sum / static_cast<double>(n) : 0.0;
    return d;
}

} // namespace

TEST_CASE("live, the background drawn from the frame is the sky the offline cube shows", "[sky][gpu][adr1070]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer offline(*ctx, shaders);
    rendering::SceneRenderer live(*ctx, shaders);
    REQUIRE(offline.init().has_value());
    REQUIRE(live.init().has_value());
    live.setLiveSkyLighting({.enabled = true});
    const scene::Scene s = duskScene();
    const gpu::Image8 a = render(offline, s);
    const gpu::Image8 b = render(live, s);
    if (const char* dump = std::getenv("AVGEN_LIVE_SKY_DUMP")) {
        (void)gpu::writePpm(a, std::filesystem::path(dump) / "offline.ppm");
        (void)gpu::writePpm(b, std::filesystem::path(dump) / "live.ppm");
    }
    const Diff all = diff(a, b);
    INFO("mean " << all.mean << " max " << all.max << " channels over 4 levels " << all.over4);
    // The two differ by the cube's resampling: a 128 px prefiltered face is about three of these
    // pixels a texel, so the cube softens the sun disc's rim and the horizon band and the exact sky
    // does not. Measured: mean 0.29 levels, max 48 on the rim, 1,938 of 110,592 channels (1.8%)
    // more than 4 levels apart, all on the rim and the band.
    CHECK(all.mean < 0.5);
    CHECK(all.over4 < 3 * 192 * 192 * 3 / 100);
    // The lighting is the same cube in both.
    CHECK(diff(a, b, true).max <= 1);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("live, a moving sky's background follows every frame and its lighting lands as the blocking build's",
          "[sky][gpu][adr1070]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer offline(*ctx, shaders);
    rendering::SceneRenderer live(*ctx, shaders);
    REQUIRE(offline.init().has_value());
    REQUIRE(live.init().has_value());
    // A small budget so the build spans several frames; a rate high enough not to wait.
    live.setLiveSkyLighting({.enabled = true, .maxRateHz = 1000.0, .passBudget = 2.0e6});
    scene::Scene s = duskScene();
    (void)render(live, s); // the first sky is built at once, blocking
    REQUIRE(live.liveSkyBuilds() == 0);
    const gpu::Image8 oldOffline = render(offline, s);

    // The chord changes: a cold dusk.
    s.environment.sky.zenithColor = {0.02f, 0.05f, 0.25f};
    s.environment.sky.horizonColor = {0.30f, 0.40f, 0.85f};
    const gpu::Image8 newOffline = render(offline, s);
    const std::uint64_t builds = rendering::environmentBuildCount();
    const gpu::Image8 first = render(live, s);
    if (const char* dump = std::getenv("AVGEN_LIVE_SKY_DUMP")) {
        (void)gpu::writePpm(first, std::filesystem::path(dump) / "first.ppm");
        (void)gpu::writePpm(oldOffline, std::filesystem::path(dump) / "old.ppm");
        (void)gpu::writePpm(newOffline, std::filesystem::path(dump) / "new.ppm");
    }
    CHECK(rendering::environmentBuildCount() == builds + 1); // a build has started...
    CHECK(live.liveSkyBuildInFlight());                      // ...and is not finished
    // On that very frame the background is already the new sky, and the lighting still the old.
    CHECK(diff(first, newOffline, false, true).mean < 0.5);
    CHECK(diff(first, oldOffline, true).max <= 1);

    int frames = 1;
    gpu::Image8 last = first;
    while (live.liveSkyBuildInFlight() && frames < 64) {
        last = render(live, s);
        ++frames;
    }
    INFO("the build took " << frames << " frames");
    CHECK(frames > 2);   // spread, not one frame's stall
    CHECK(frames < 20);
    CHECK(live.liveSkyBuilds() == 1);
    last = render(live, s);
    CHECK(diff(last, newOffline, true).max <= 1); // the lighting the blocking build makes
    CHECK(diff(last, newOffline).mean < 0.5);
    CHECK(rendering::environmentBuildCount() == builds + 1);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("live, the lighting rebuild waits for the rate cap and a drift inside the tolerance never starts one",
          "[sky][gpu][adr1070][adr1022]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer live(*ctx, shaders);
    REQUIRE(live.init().has_value());
    live.setLiveSkyLighting({.enabled = true, .maxRateHz = 1000.0, .passBudget = 1.0e9});
    scene::Scene s = duskScene();
    (void)render(live, s);
    s.environment.sky.zenithColor = {0.30f, 0.12f, 0.30f};
    (void)render(live, s); // one budget holds the whole chain: started and landed on this frame
    CHECK(live.liveSkyBuilds() == 1);
    CHECK_FALSE(live.liveSkyBuildInFlight());

    // ADR-1022 still holds live: a settling drift is the sky on screen.
    const std::uint64_t before = rendering::environmentBuildCount();
    for (int i = 0; i < 30; ++i) {
        s.environment.sky.zenithColor *= 1.0001f;
        (void)render(live, s);
    }
    CHECK(rendering::environmentBuildCount() == before);

    // A real change inside the cap's gap waits...
    live.setLiveSkyLighting({.enabled = true, .maxRateHz = 0.01, .passBudget = 1.0e9});
    s.environment.sky.zenithColor = {0.05f, 0.30f, 0.10f};
    for (int i = 0; i < 5; ++i) {
        (void)render(live, s);
    }
    CHECK(rendering::environmentBuildCount() == before);
    CHECK(live.liveSkyBuilds() == 1);
    // ...and lands once the cap allows it.
    live.setLiveSkyLighting({.enabled = true, .maxRateHz = 1000.0, .passBudget = 1.0e9});
    (void)render(live, s);
    CHECK(rendering::environmentBuildCount() == before + 1);
    CHECK(live.liveSkyBuilds() == 2);
    CHECK(ctx->errorCount() == 0);
}
