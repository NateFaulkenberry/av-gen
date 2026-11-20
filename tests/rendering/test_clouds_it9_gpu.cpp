// ADR-2111..2113 (Neon Dreams iteration 9): the march's per-frame jitter is a spread at the common frame rates (not a
// ramp), bobbing formations move their lobes (and their bins keep them), and the wisp layer draws thin, tattered
// cloud that a wake parts.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/cloud_renderer.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/clouds.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <nlohmann/json.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <filesystem>
#include <memory>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

// the wrapped distance between two values on [0, 1)
float circ(float a, float b) {
    const float d = std::fabs(a - b);
    return std::min(d, 1.0f - d);
}

} // namespace

TEST_CASE("the cloud march's frame jitters spread every few frames at 24, 30 and 60 fps",
          "[renderer][clouds][adr2111]") {
    for (const double fps : {24.0, 30.0, 60.0}) {
        for (int component = 0; component < 2; ++component) {
            std::vector<float> v;
            for (int k = 0; k < 240; ++k) {
                FrameTime t{};
                t.renderTime = static_cast<double>(k) / fps;
                const glm::vec2 j = rendering::cloudFrameJitter(t.frameNonce());
                v.push_back(component == 0 ? j.x : j.y);
            }
            // no slow ramp: the mean step between frames is a large fraction of the range (the ADR-2100 light jitter
            // stepped 0.11 a frame at 30 fps and its march jitter 0.056)
            double step = 0.0;
            for (std::size_t k = 1; k < v.size(); ++k) step += circ(v[k], v[k - 1]);
            step /= static_cast<double>(v.size() - 1);
            INFO("fps " << fps << " component " << component << " mean step " << step);
            CHECK(step > 0.2);
            // and any eight frames in a row cover the range with no gap over a third of it
            float worst = 0.0f;
            for (std::size_t k = 0; k + 8 <= v.size(); ++k) {
                std::vector<float> w(v.begin() + static_cast<std::ptrdiff_t>(k),
                                     v.begin() + static_cast<std::ptrdiff_t>(k + 8));
                std::sort(w.begin(), w.end());
                float gap = w.front() + 1.0f - w.back();
                for (std::size_t i = 1; i < w.size(); ++i) gap = std::max(gap, w[i] - w[i - 1]);
                worst = std::max(worst, gap);
            }
            CHECK(worst < 0.34f);
        }
    }
}

namespace {

std::unique_ptr<gpu::Context> makeContext9() {
    log::init(log::Level::Warn);
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

// lit grey cloud on a black sky, no temporal accumulation, no key
scene::Scene greyClouds() {
    scene::Scene s;
    s.camera.position = {0.0f, 0.0f, 0.0f};
    s.camera.target = {0.0f, 0.0f, -100.0f};
    s.camera.fovYRadians = 0.9f;
    s.camera.nearPlane = 0.5f;
    s.camera.farPlane = 60000.0f;
    s.clouds.extinction = 0.05f;
    s.clouds.lightIntensity = 0.0f;
    s.clouds.ambientTop = {0.6f, 0.6f, 0.6f};
    s.clouds.ambientBottom = {0.6f, 0.6f, 0.6f};
    s.clouds.hazeColor = {0.6f, 0.6f, 0.6f};
    s.clouds.hazeDistance = 100000.0f;
    s.clouds.temporal = 0.0f;
    s.clouds.resolution = 1.0f;
    s.clouds.minStep = 1.0f;
    s.clouds.stepGrowth = 0.01f;
    s.clouds.steps = 192;
    s.clouds.noiseAmount = 0.0f;
    s.clouds.detailAmount = 0.0f;
    return s;
}

// the luma-weighted mean row of the bright pixels (the cloud's height on screen), or -1 with none
double brightRow(const gpu::Image8& img) {
    double sum = 0.0, w = 0.0;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const auto* p = img.pixel(x, y);
            const double l = 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
            if (l > 80.0) { // the sky is ~30
                sum += l * y;
                w += l;
            }
        }
    }
    return w > 0.0 ? sum / w : -1.0;
}

double meanLuma(const gpu::Image8& img, std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1) {
    double s = 0.0;
    for (std::uint32_t y = y0; y < y1; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            const auto* p = img.pixel(x, y);
            s += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
        }
    }
    return s / static_cast<double>((x1 - x0) * (y1 - y0));
}

} // namespace

TEST_CASE("a bobbing formation's offset is a bounded function of the second, and the lift is clamped",
          "[renderer][clouds][adr2112]") {
    scene::CloudBob b;
    b.amplitude = {3.0f, 5.0f, 2.0f};
    b.period = 10.0f;
    b.phase = 0.25f;
    b.liftMax = 4.0f;
    // the vertical bob is a sine of the period: at phase 0.25 it starts at the top
    CHECK(scene::cloudBobOffset(b, 0.0).y == Catch::Approx(5.0f).margin(1e-4));
    CHECK(scene::cloudBobOffset(b, 5.0).y == Catch::Approx(-5.0f).margin(1e-4));
    CHECK(scene::cloudBobOffset(b, 10.0).y == Catch::Approx(5.0f).margin(1e-4));
    // a seek lands where play would: the same second, the same offset
    CHECK(scene::cloudBobOffset(b, 37.25).x == scene::cloudBobOffset(b, 37.25).x);
    float most = 0.0f;
    for (int k = 0; k < 2000; ++k) {
        const glm::vec3 o = scene::cloudBobOffset(b, k * 0.05);
        most = std::max({most, std::fabs(o.x) - 3.0f, std::fabs(o.y) - 5.0f, std::fabs(o.z) - 2.0f});
    }
    CHECK(most <= 1e-4f);
    b.lift = 100.0f; // clamped to liftMax
    CHECK(scene::cloudBobOffset(b, 0.0).y == Catch::Approx(9.0f).margin(1e-4));
    CHECK(scene::cloudBobReach(b) == glm::vec3(3.0f, 9.0f, 2.0f));
}

TEST_CASE("the lobe bins hold a riding lobe wherever its formation can take it", "[renderer][clouds][adr2112]") {
    std::vector<scene::CloudLobe> lobes(3);
    lobes[0].centre = {0.0f, 0.0f, 0.0f};
    lobes[0].radius = 20.0f;
    lobes[0].bobIndex = 0;
    lobes[1].centre = {300.0f, 0.0f, 0.0f};
    lobes[1].radius = 20.0f;
    lobes[2].centre = {-300.0f, 0.0f, 300.0f};
    lobes[2].radius = 20.0f;
    const std::vector<glm::vec3> reach = {glm::vec3(15.0f, 12.0f, 15.0f)};
    const rendering::CloudLobeBins bins = rendering::binCloudLobes(lobes, reach);
    REQUIRE(bins.nx > 0);
    // the formation's lobe at its farthest reach in x: still in that cell's list, and in its height range
    const glm::vec2 at(0.0f + 20.0f + 14.0f, 0.0f);
    const glm::ivec2 cell = glm::ivec2(glm::floor((at - bins.origin) / bins.cellSize));
    const std::size_t c = static_cast<std::size_t>(cell.y) * bins.nx + static_cast<std::size_t>(cell.x);
    const std::uint32_t offset = bins.words[2 * c];
    const std::uint32_t count = bins.words[2 * c + 1] & rendering::kBinCountMask;
    bool found = false;
    for (std::uint32_t j = 0; j < count; ++j) found = found || bins.words[offset + j] == 0u;
    CHECK(found);
    CHECK(bins.yHi >= 20.0f + 12.0f - 1e-3f);
    CHECK(bins.yLo <= -20.0f - 12.0f + 1e-3f);
}

TEST_CASE("an overfull lobe bin is split into finer cells, not trimmed", "[renderer][clouds][adr2114]") {
    // a dense 14 x 14 patch of small lobes (3 m apart) in a set 4 km across: at 128 cells a side one 31 m cell holds
    // ~190 of them, more than kMaxLobesPerCell; the grid must split until every cell fits (ADR-2114)
    std::vector<scene::CloudLobe> lobes;
    for (int z = 0; z < 14; ++z) {
        for (int x = 0; x < 14; ++x) {
            scene::CloudLobe l;
            l.centre = {3.0f * static_cast<float>(x), 0.0f, 3.0f * static_cast<float>(z)};
            l.radius = 5.0f;
            lobes.push_back(l);
        }
    }
    for (const float x : {-2000.0f, 2000.0f}) {
        scene::CloudLobe l;
        l.centre = {x, 0.0f, x};
        l.radius = 5.0f;
        lobes.push_back(l);
    }
    const rendering::CloudLobeBins bins = rendering::binCloudLobes(lobes);
    CHECK(bins.truncated == 0);
    CHECK(std::max(bins.nx, bins.nz) > rendering::kMaxBinCellsPerSide);
    CHECK(std::max(bins.nx, bins.nz) <= rendering::kMaxBinCellsPerSideSplit + 1);
    // every lobe is still listed in the cell under its centre
    for (std::size_t i = 0; i < lobes.size(); ++i) {
        const glm::vec2 at(lobes[i].centre.x, lobes[i].centre.z);
        const glm::ivec2 cell = glm::ivec2(glm::floor((at - bins.origin) / bins.cellSize));
        const std::size_t c = static_cast<std::size_t>(cell.y) * bins.nx + static_cast<std::size_t>(cell.x);
        const std::uint32_t offset = bins.words[2 * c];
        const std::uint32_t count = bins.words[2 * c + 1] & rendering::kBinCountMask;
        REQUIRE(count <= rendering::kMaxLobesPerCell);
        bool found = false;
        for (std::uint32_t j = 0; j < count; ++j) found = found || bins.words[offset + j] == i;
        CHECK(found);
    }
}

TEST_CASE("a clouds block reads and writes bobs, riders and wisps, and rejects an unknown formation",
          "[scene][clouds][adr2112][adr2113]") {
    const nlohmann::json j = nlohmann::json::parse(R"({
        "lobes": [{"c": [0, 0, 0], "r": 10, "bob": "isle"}, {"c": [50, 0, 0], "r": 10}],
        "bobs": [{"name": "isle", "amplitude": [1, 3, 1], "period": 9, "phase": 0.3, "liftMax": 2}],
        "glows": [{"position": [0, 0, 0], "bob": "isle"}],
        "wisps": {"base": 2, "top": 14, "density": 0.4, "wakes": ["alien*"],
                  "zones": [{"name": "quay", "centre": [0, 0], "radii": [30, 12], "floor": 10, "yaw": 0.2,
                             "below": 6, "above": 1.5, "density": 0.8, "bob": "isle"}]}
    })");
    auto c = scene::Clouds::fromJson(j, ".");
    REQUIRE(c.has_value());
    CHECK(c->lobes[0].bobIndex == 0);
    CHECK(c->lobes[1].bobIndex == -1);
    CHECK(c->glows[0].bobIndex == 0);
    CHECK(c->wisps.enabled);
    CHECK(c->wisps.zones[0].bobIndex == 0);
    const nlohmann::json out = c->toJson();
    auto again = scene::Clouds::fromJson(out, ".");
    REQUIRE(again.has_value());
    CHECK(again->toJson()["bobs"] == out["bobs"]);
    CHECK(again->toJson()["wisps"] == out["wisps"]);
    CHECK(again->toJson()["lobes"] == out["lobes"]);
    CHECK(again->toJson()["glows"] == out["glows"]);
    nlohmann::json bad = j;
    bad["lobes"][1]["bob"] = "nowhere";
    CHECK_FALSE(scene::Clouds::fromJson(bad, ".").has_value());
}

TEST_CASE("a bobbing formation moves its lobes on screen with the timeline second",
          "[gpu][renderer][clouds][adr2112]") {
    auto ctx = makeContext9();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    constexpr std::uint32_t W = 160, H = 120;
    scene::Scene s = greyClouds();
    scene::CloudLobe l;
    l.centre = {0.0f, 0.0f, -150.0f};
    l.radius = 25.0f;
    l.bob = "isle";
    l.bobIndex = 0;
    s.clouds.lobes = {l};
    scene::CloudBob b;
    b.name = "isle";
    b.amplitude = {0.0f, 20.0f, 0.0f};
    b.period = 8.0f;
    s.clouds.bobs = {b};
    double rows[2] = {0.0, 0.0};
    for (int k = 0; k < 2; ++k) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        FrameTime t{};
        t.renderTime = k == 0 ? 2.0 : 6.0; // the top of the bob, then the bottom
        auto img = renderer.renderToImage(s, t, W, H);
        REQUIRE(img.has_value());
        rows[k] = brightRow(*img);
        if (std::getenv("AVGEN_TEST_DUMP")) (void)gpu::writePpm(*img, std::string(std::getenv("AVGEN_TEST_DUMP")) + "/bob" + std::to_string(k) + ".ppm");
    }
    INFO("cloud row at the top " << rows[0] << ", at the bottom " << rows[1]);
    REQUIRE(rows[0] > 0.0);
    REQUIRE(rows[1] > 0.0);
    CHECK(rows[1] - rows[0] > 15.0); // 40 m of travel at 150 m is ~30 rows
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("the wisp layer draws thin torn cloud in its band, and a wake parts it", "[gpu][renderer][clouds][adr2113]") {
    auto ctx = makeContext9();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    constexpr std::uint32_t W = 160, H = 90;
    scene::Scene s = greyClouds();
    // looking a little down at a region of wisps 40 m off (no band)
    s.camera.position = {0.0f, 8.0f, 0.0f};
    s.camera.target = {0.0f, 4.0f, -40.0f};
    s.clouds.wisps.enabled = true;
    scene::CloudWispZone zone;
    zone.centre = {0.0f, -40.0f};
    zone.floor = 4.0f;
    zone.radii = {20.0f, 16.0f};
    zone.below = 8.0f;
    zone.above = 6.0f;
    s.clouds.wisps.zones = {zone};
    s.clouds.wisps.density = 0.5f;
    s.clouds.wisps.scale = 8.0f;
    s.clouds.wisps.coverage = 0.6f;
    s.clouds.wisps.tatter = 0.5f;
    s.clouds.extinction = 0.08f;
    scene::CloudLobe far; // keeps the block active with the wisps off: one small lobe far behind the camera
    far.centre = {0.0f, 0.0f, 5000.0f};
    far.radius = 10.0f;
    s.clouds.lobes = {far};
    scene::Scene off = s;
    off.clouds.wisps.enabled = false;
    scene::Scene woke = s;
    for (int k = 0; k < 8; ++k) { // a crowd standing across the middle of the view
        woke.clouds.wisps.wakePoints.emplace_back(-14.0f + 4.0f * k, -1.0f, -40.0f, 0.0f);
        woke.clouds.wisps.wakeVelocities.emplace_back(1.2f, 0.0f, 0.0f, 0.0f);
    }
    woke.clouds.wisps.wakeRadius = 3.0f;
    woke.clouds.wisps.wakeStrength = 1.0f;
    woke.clouds.wisps.wakeHeight = 12.0f;
    double l[3] = {0, 0, 0};
    double spread = 0.0;
    int k = 0;
    for (const scene::Scene* sc : {&off, &s, &woke}) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        FrameTime t{};
        t.renderTime = 3.0;
        auto img = renderer.renderToImage(*sc, t, W, H);
        REQUIRE(img.has_value());
        l[k] = meanLuma(*img, W / 4, H / 3, 3 * W / 4, 5 * H / 6);
        if (std::getenv("AVGEN_TEST_DUMP")) (void)gpu::writePpm(*img, std::string(std::getenv("AVGEN_TEST_DUMP")) + "/wisp" + std::to_string(k) + ".ppm");
        if (k == 1) {
            // torn, not a uniform grey: the luma varies across the band
            double lo = 1e9, hi = -1e9;
            for (std::uint32_t x = 0; x + 8 <= W; x += 8) {
                const double m = meanLuma(*img, x, H / 2, x + 8, H / 2 + 8);
                lo = std::min(lo, m);
                hi = std::max(hi, m);
            }
            spread = hi - lo;
        }
        ++k;
    }
    INFO("off " << l[0] << ", wisps " << l[1] << ", parted " << l[2] << ", spread across the band " << spread);
    CHECK(l[1] > l[0] + 5.0);   // the wisps are there
    CHECK(spread > 8.0);        // and broken up
    CHECK(l[2] < l[1] - 2.0);   // the wakes clear some of it
    CHECK(ctx->errorCount() == 0);
}
