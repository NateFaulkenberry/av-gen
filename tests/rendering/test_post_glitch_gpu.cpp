// ADR-1065: post effects as instruments and the glitch vocabulary. Each is off and byte-identical at its defaults
// (and with only its shape parameters moved), acts when turned up, and is a function of the frame and the clock alone
// (two fresh renderers agree; a block glitch re-rolls with the epoch and holds within it).

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "support/image_diff.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <functional>
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

// A lit grid of bright and dark cubes on a mid background: structure for every effect to move.
scene::Scene structuredScene() {
    scene::Scene s;
    s.environment.backgroundColor = {0.12f, 0.1f, 0.18f};
    s.camera.position = {0.0f, 0.0f, 9.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    const auto mesh = s.addMesh(scene::makeCube(0.5f));
    for (int y = -2; y <= 2; ++y) {
        for (int x = -3; x <= 3; ++x) {
            auto& e = s.addEntity("c", mesh);
            e.transform.position = {x * 1.3f, y * 1.3f, 0.0f};
            e.material.baseColor = {0.0f, 0.0f, 0.0f};
            const bool bright = (x + y) % 2 == 0;
            e.material.emissiveColor = bright ? glm::vec3(1.0f, 0.7f, 0.3f) : glm::vec3(0.2f, 0.4f, 1.0f);
            e.material.emissiveIntensity = bright ? 4.0f : 0.8f;
        }
    }
    s.post.bloomEnabled = false;
    s.post.bloomIntensity = 0.0f;
    return s;
}

struct Effect {
    const char* name;
    std::function<void(scene::PostGlitchSettings&)> shapeOnly; // moves non-gating parameters
    std::function<void(scene::PostGlitchSettings&)> on;
};

} // namespace

TEST_CASE("Each post instrument is off at its defaults, acts when on, and is deterministic", "[gpu][post][adr1065]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    const scene::Scene base = structuredScene();
    FrameTime time{};
    time.renderTime = 1.25;
    auto plain = renderer.renderToImage(base, time, 160, 96);
    REQUIRE(plain.has_value());

    const Effect effects[] = {
        {"shock", [](auto& g) { g.shockRadius = 0.3f; g.shockWidth = 0.2f; g.shockChroma = 1.0f; },
         [](auto& g) { g.shockAmount = 60.0f; g.shockRadius = 0.4f; g.shockWidth = 0.2f; }},
        {"glitch", [](auto& g) { g.glitchBlock = 16.0f; g.glitchRate = 30.0f; g.glitchSwap = 1.0f; g.glitchDrift = 90.0f; },
         [](auto& g) { g.glitchAmount = 0.5f; g.glitchSwap = 0.5f; }},
        {"tear", [](auto& g) { g.glitchTearShift = 200.0f; }, [](auto& g) { g.glitchTear = 0.5f; }},
        {"split", [](auto& g) { g.splitAngle = 30.0f; g.splitSpectral = 1.0f; }, [](auto& g) { g.splitAmount = 20.0f; }},
        {"spectral split", [](auto&) {}, [](auto& g) { g.splitAmount = 20.0f; g.splitSpectral = 1.0f; }},
        {"sort", [](auto& g) { g.sortThreshold = 0.2f; g.sortLength = 300.0f; g.sortInvert = 1.0f; },
         [](auto& g) { g.sortAmount = 1.0f; g.sortThreshold = 0.5f; g.sortLength = 200.0f; }},
        {"radial", [](auto& g) { g.radialCenterX = 0.2f; }, [](auto& g) { g.radialAmount = 0.3f; }},
        {"scanlines", [](auto& g) { g.displayLines = 30.0f; }, [](auto& g) { g.displayScanlines = 0.8f; g.displayLines = 24.0f; }},
        {"pixelate", [](auto& g) { g.displayPixelate = 0.5f; }, [](auto& g) { g.displayPixelate = 40.0f; }},
        {"posterize", [](auto& g) { g.displayPosterize = 1.0f; g.displayDither = 1.0f; },
         [](auto& g) { g.displayPosterize = 3.0f; g.displayDither = 0.5f; }},
    };
    for (const Effect& fx : effects) {
        INFO(fx.name);
        scene::Scene s = base;
        fx.shapeOnly(s.post.glitch);
        auto idle = renderer.renderToImage(s, time, 160, 96);
        REQUIRE(idle.has_value());
        {
            const auto d = testing::byteDiff(plain->rgba, idle->rgba);
            INFO("shape only: " << d.describe());
            CHECK(d.identical());
        }
        fx.on(s.post.glitch);
        auto active = renderer.renderToImage(s, time, 160, 96);
        REQUIRE(active.has_value());
        const auto d = testing::byteDiff(plain->rgba, active->rgba);
        INFO("on: " << d.describe());
        CHECK_FALSE(d.identical());
        // A fresh renderer at the same instant draws the same bytes.
        rendering::SceneRenderer fresh(*ctx, shaders);
        REQUIRE(fresh.init().has_value());
        auto again = fresh.renderToImage(s, time, 160, 96);
        REQUIRE(again.has_value());
        CHECK(testing::byteDiff(active->rgba, again->rgba).identical());
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("The block glitch holds within an epoch and re-rolls at the next", "[gpu][post][adr1065]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    scene::Scene off = structuredScene();
    scene::Scene s = off;
    s.post.glitch.glitchAmount = 0.4f;
    s.post.glitch.glitchRate = 4.0f; // epochs of 0.25 s
    // Which pixels the glitch changes, against the same instant without it: the epoch's pattern, whatever else in
    // the frame moves with time.
    const auto pattern = [&](double t) {
        FrameTime time{};
        time.renderTime = t;
        auto on = renderer.renderToImage(s, time, 160, 96);
        auto ref = renderer.renderToImage(off, time, 160, 96);
        REQUIRE(on.has_value());
        REQUIRE(ref.has_value());
        std::vector<std::uint8_t> mask(on->rgba.size() / 4);
        for (std::size_t i = 0; i < mask.size(); ++i) {
            mask[i] = on->rgba[4 * i] != ref->rgba[4 * i] || on->rgba[4 * i + 1] != ref->rgba[4 * i + 1] ||
                      on->rgba[4 * i + 2] != ref->rgba[4 * i + 2];
        }
        return mask;
    };
    const auto a = pattern(1.01);
    const auto b = pattern(1.20); // the same epoch (floor(4 t) = 4)
    const auto c = pattern(1.30); // the next
    std::size_t ab = 0, ac = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        ab += a[i] != b[i];
        ac += a[i] != c[i];
    }
    INFO("pattern differences within an epoch " << ab << ", across " << ac << " of " << a.size());
    CHECK(ab <= a.size() / 50); // the same blocks (edges of moving content aside)
    CHECK(ac > a.size() / 20);
    CHECK(ctx->errorCount() == 0);
}
