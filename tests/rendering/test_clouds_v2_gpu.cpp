// ADR-2100: Cloud Environment v2 -- the sea runs to the horizon with no lobes, a glow colours the cloud around it, the
// lobe bins hold every lobe that covers a point, and the temporally accumulated march is deterministic across fresh
// renderers.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/cloud_renderer.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <memory>
#include <random>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

std::unique_ptr<gpu::Context> makeContext() {
    log::init(log::Level::Warn);
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

// above a sea whose top is at y = 0, looking at the horizon and a little down
scene::Scene seaScene() {
    scene::Scene s;
    s.camera.position = {0.0f, 300.0f, 0.0f};
    s.camera.target = {0.0f, 150.0f, -2000.0f};
    s.camera.fovYRadians = 0.87f;
    s.camera.nearPlane = 0.5f;
    s.camera.farPlane = 60000.0f;
    s.clouds.sea.enabled = true;
    s.clouds.sea.base = -300.0f;
    s.clouds.sea.top = 0.0f;
    s.clouds.sea.coverage = 1.0f; // no breaks: every downward ray finds cloud
    s.clouds.sea.density = 2.0f;
    s.clouds.extinction = 0.02f;
    s.clouds.lightIntensity = 0.0f;
    s.clouds.ambientTop = {0.4f, 0.4f, 0.4f};
    s.clouds.ambientBottom = {0.4f, 0.4f, 0.4f};
    s.clouds.hazeColor = {0.4f, 0.4f, 0.4f};
    s.clouds.temporal = 0.0f;
    return s;
}

struct Rgb {
    double r = 0, g = 0, b = 0;
};

Rgb meanRgb(const gpu::Image8& img, std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1) {
    Rgb m;
    std::size_t n = 0;
    for (std::uint32_t y = y0; y < y1; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            const auto* p = img.pixel(x, y);
            m.r += p[0];
            m.g += p[1];
            m.b += p[2];
            ++n;
        }
    }
    m.r /= static_cast<double>(n);
    m.g /= static_cast<double>(n);
    m.b /= static_cast<double>(n);
    return m;
}

double luma(const Rgb& c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }

} // namespace

TEST_CASE("the cloud sea draws below the horizon to both edges of the frame, with no lobes",
          "[gpu][renderer][clouds][adr2100]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    constexpr std::uint32_t W = 192, H = 108;
    FrameTime t{};

    scene::Scene none = seaScene();
    none.clouds.enabled = false;
    auto a = renderer.renderToImage(none, t, W, H);
    REQUIRE(a.has_value());
    scene::Scene sea = seaScene();
    REQUIRE(sea.clouds.active());
    REQUIRE(sea.clouds.lobes.empty());
    auto b = renderer.renderToImage(sea, t, W, H);
    REQUIRE(b.has_value());

    // the bottom strip, at the left edge, the middle and the right edge, is cloud; the top strip (sky) is untouched
    for (std::uint32_t x0 : {0u, W / 2 - 8, W - 16}) {
        const double before = luma(meanRgb(*a, x0, H - 12, x0 + 16, H));
        const double after = luma(meanRgb(*b, x0, H - 12, x0 + 16, H));
        INFO("bottom strip at x " << x0 << ": " << before << " -> " << after);
        CHECK(after > before + 15.0);
    }
    const double skyA = luma(meanRgb(*a, 0, 0, W, 10));
    const double skyB = luma(meanRgb(*b, 0, 0, W, 10));
    INFO("sky " << skyA << " -> " << skyB);
    CHECK(std::abs(skyB - skyA) < 1.0);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("a cloud glow colours the cloud around it and leaves the cloud far from it alone",
          "[gpu][renderer][clouds][adr2100]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    constexpr std::uint32_t W = 192, H = 108;
    FrameTime t{};

    scene::Scene plain = seaScene();
    plain.clouds.ambientTop = plain.clouds.ambientBottom = plain.clouds.hazeColor = {0.05f, 0.05f, 0.05f};
    auto a = renderer.renderToImage(plain, t, W, H);
    REQUIRE(a.has_value());
    // a red glow just inside the sea, under the left third of the frame
    scene::Scene glowing = plain;
    const glm::vec3 fwd = glm::normalize(plain.camera.target - plain.camera.position);
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    const float dist = 900.0f; // where a ray a little below the frame's centre meets the sea's top
    scene::CloudGlow g;
    g.position = plain.camera.position + fwd * dist - right * (dist * 0.35f);
    g.position.y = -20.0f;
    g.color = {1.0f, 0.0f, 0.0f};
    g.radius = 150.0f;
    g.intensity = 4.0f;
    glowing.clouds.glows = {g};
    auto b = renderer.renderToImage(glowing, t, W, H);
    REQUIRE(b.has_value());

    // the left half reddens; the right edge, far from the glow, does not
    Rgb leftA = meanRgb(*a, 0, H / 2, W / 2, H);
    Rgb leftB = meanRgb(*b, 0, H / 2, W / 2, H);
    Rgb rightA = meanRgb(*a, W - 20, H / 2, W, H);
    Rgb rightB = meanRgb(*b, W - 20, H / 2, W, H);
    INFO("left r-b " << (leftA.r - leftA.b) << " -> " << (leftB.r - leftB.b) << "; right r " << rightA.r << " -> "
                     << rightB.r);
    CHECK((leftB.r - leftB.b) > (leftA.r - leftA.b) + 4.0);
    CHECK(std::abs(rightB.r - rightA.r) < 0.25 * (leftB.r - leftA.r)); // (the bloom may carry a little across)
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("the temporally accumulated cloud is the same sequence from two fresh renderers",
          "[gpu][renderer][clouds][adr2100][determinism]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    constexpr std::uint32_t W = 160, H = 90;
    scene::Scene s = seaScene();
    s.clouds.temporal = 0.85f;
    s.clouds.sea.coverage = 0.6f; // breaks, so the jitter has edges to disagree about
    s.clouds.drift = {20.0f, 0.0f, 0.0f};
    std::uint64_t hashes[2] = {0, 0};
    for (int run = 0; run < 2; ++run) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        for (int k = 0; k < 4; ++k) {
            FrameTime t{};
            t.renderTime = k / 30.0;
            t.deltaTime = k == 0 ? 0.0 : 1.0 / 30.0;
            t.frameIndex = static_cast<std::uint64_t>(k);
            s.camera.position.z = -10.0f * static_cast<float>(k);
            s.camera.target.z = -2000.0f - 10.0f * static_cast<float>(k);
            auto img = renderer.renderToImage(s, t, W, H);
            REQUIRE(img.has_value());
            hashes[run] = gpu::hashImage(*img);
        }
    }
    CHECK(hashes[0] == hashes[1]);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("the lobe bins hold every lobe that covers a point, in that point's cell", "[renderer][clouds][adr2100]") {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> pos(-2000.0f, 2000.0f), rad(40.0f, 400.0f), sc(0.5f, 1.5f);
    std::vector<scene::CloudLobe> lobes(600);
    for (scene::CloudLobe& l : lobes) {
        l.centre = {pos(rng), pos(rng) * 0.1f, pos(rng)};
        l.radius = rad(rng);
        l.scale = {sc(rng), sc(rng), sc(rng)};
    }
    const rendering::CloudLobeBins bins = rendering::binCloudLobes(lobes);
    REQUIRE(bins.nx > 0);
    REQUIRE(bins.nz > 0);
    CHECK(bins.nx <= 65);
    CHECK(bins.nz <= 65);
    CHECK(bins.truncated == 0);
    std::uniform_real_distribution<float> probe(-2200.0f, 2200.0f);
    int checked = 0;
    for (int k = 0; k < 4000; ++k) {
        const glm::vec3 p(probe(rng), 0.0f, probe(rng));
        const glm::ivec2 cell = glm::ivec2(glm::floor((glm::vec2(p.x, p.z) - bins.origin) / bins.cellSize));
        const bool inside = cell.x >= 0 && cell.y >= 0 && cell.x < static_cast<int>(bins.nx) &&
                            cell.y < static_cast<int>(bins.nz);
        for (std::size_t i = 0; i < lobes.size(); ++i) {
            const scene::CloudLobe& l = lobes[i];
            glm::vec3 d = (p - l.centre) / (l.radius * l.scale);
            d.y = 0.0f; // the xz footprint
            if (glm::dot(d, d) >= 1.0f) continue;
            ++checked;
            REQUIRE(inside); // a covered point is inside the grid
            const std::size_t c = static_cast<std::size_t>(cell.y) * bins.nx + static_cast<std::size_t>(cell.x);
            const std::uint32_t offset = bins.words[2 * c];
            const std::uint32_t count = bins.words[2 * c + 1];
            bool found = false;
            for (std::uint32_t j = 0; j < count; ++j) found = found || bins.words[offset + j] == i;
            REQUIRE(found);
        }
    }
    CHECK(checked > 100);
}
