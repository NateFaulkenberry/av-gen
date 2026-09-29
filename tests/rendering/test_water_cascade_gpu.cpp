// Water on a steep course is a cascade (ADR-985).
//
// What these hold the renderer to:
//   * flat water is byte-identical to the shader with every cascade block removed, tears on or off and
//     with the whitewater turned up -- on a synthetic sheet and on the QA scene's real river;
//   * a steep sheet is shaded about its own plane: the part of it above the camera's height is drawn as
//     the part below it is, not as a ceiling seen from under the water;
//   * the tears fade out on a slope;
//   * `cascade` whitens a steep sheet in streaks that run along the flow, and never a flat one.
//
// The comparison arm is the live shader with the `cascade (ADR-985)` blocks stripped
// (water_shader_variant.hpp), so it guards exactly those blocks whatever else the file becomes.

#include "support/water_bench.hpp"
#include "support/water_shader_variant.hpp"

#include "gpu/readback.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::testsupport;
namespace fs = std::filesystem;

namespace {

constexpr const char* kCascadeBegin = "---- cascade (ADR-985) begin ----";
constexpr const char* kCascadeEnd = "---- cascade (ADR-985) end ----";

// A water quad and its bed: `half` wide in X, `length` long, rising at `degrees` toward +Z from its near
// edge at (0, y0, z0). The bed is the same quad `depth` metres under it, across the sheet. The flow runs
// down the slope (toward -Z) at 0.4 of the fastest body's speed; a flat sheet (0 degrees) flows toward -Z.
struct Slab {
    float half = 20.0f;
    float length = 40.0f;
    float degrees = 0.0f;
    float y0 = 0.0f;
    float z0 = -20.0f;

    glm::vec3 up() const {
        const float a = glm::radians(degrees);
        return {0.0f, std::cos(a), -std::sin(a)};
    }
    glm::vec3 along() const {
        const float a = glm::radians(degrees);
        return {0.0f, std::sin(a), std::cos(a)};
    }
    glm::vec3 at(float x, float s) const { return glm::vec3(x, y0, z0) + along() * s; }
    glm::vec3 centre() const { return at(0.0f, length * 0.5f); }

    scene::MeshData mesh(float depth, bool water) const {
        scene::MeshData m;
        const glm::vec3 lane = water ? glm::vec3(0.0f, 0.4f, -1.0f) : up();
        const glm::vec2 uv = water ? glm::vec2(3.0f, 1.0f) : glm::vec2(0.0f);
        const glm::vec3 drop = -up() * depth;
        m.vertices.push_back({at(-half, 0.0f) + drop, lane, uv});
        m.vertices.push_back({at(half, 0.0f) + drop, lane, uv});
        m.vertices.push_back({at(half, length) + drop, lane, uv});
        m.vertices.push_back({at(-half, length) + drop, lane, uv});
        m.indices = {0, 2, 1, 0, 3, 2};
        return m;
    }
};

scene::WaterSettings cascadeSettings() {
    scene::WaterSettings w;
    w.shallowColor = {0.03f, 0.10f, 0.12f};
    w.deepColor = {0.006f, 0.035f, 0.075f};
    w.clarity = 1.25f;
    w.maxOpacity = 0.93f;
    w.fresnel = 0.2f;
    w.reflection = 1.6f;
    w.specular = 1.2f;
    w.roughness = 0.12f;
    w.ripple = 0.08f;
    w.rippleScale = 1.0f;
    w.foam = 0.0f;
    w.glow = 0.0f;
    w.sparkle = 0.0f;
    w.refraction = 0.0f;
    w.edgeFade = 0.8f;
    w.tears = 0.0f;
    w.cascade = 0.0f;
    return w;
}

void withTears(scene::WaterSettings& w, float amount) {
    w.tears = amount;
    w.tearShear = 5.0f;
    w.tearCoverage = 0.9f;
    w.tearCell = 1.2f;
    w.tearSpacing = 5.0f;
    w.tearStretch = 3.0f;
    w.tearFollowsWind = false;
    w.tearAngle = 0.35f;
    w.tearDrift = 0.0f;
    w.tearWind = 0.0f;
}

struct View {
    glm::vec3 eye;
    glm::vec3 target;
};

scene::Scene slabScene(const Slab& slab, const scene::WaterSettings& settings, const View& view) {
    scene::Scene s;
    s.environment.backgroundColor = {0.01f, 0.015f, 0.03f};
    s.camera.position = view.eye;
    s.camera.target = view.target;
    s.camera.farPlane = 400.0f;
    scene::PunctualLight moon;
    moon.direction = glm::normalize(glm::vec3(-0.35f, -0.8f, 0.45f));
    moon.intensity = 2.0f;
    s.addLight(moon);
    scene::Entity& bed = s.addEntity("bed", s.addMesh(slab.mesh(3.0f, false)));
    bed.material.baseColor = {0.02f, 0.03f, 0.03f};
    bed.material.roughness = 1.0f;
    scene::Entity& water = s.addEntity("sea", s.addMesh(slab.mesh(0.0f, true)));
    water.style = scene::MeshStyle::Water;
    water.material.program = "sea";
    water.material.doubleSided = true;
    s.waters.push_back({"sea", settings, 0.5f});
    return s;
}

struct Arm {
    std::unique_ptr<gpu::ShaderLibrary> shaders;
    std::unique_ptr<rendering::SceneRenderer> renderer;
};

struct Bench {
    std::unique_ptr<gpu::Context> ctx;
    std::uint32_t size = 256;

    Arm arm(std::vector<fs::path> dirs = {}) {
        dirs.emplace_back(AVGEN_SHADER_SOURCE_DIR);
        Arm a;
        a.shaders = std::make_unique<gpu::ShaderLibrary>(*ctx, dirs);
        a.renderer = std::make_unique<rendering::SceneRenderer>(*ctx, *a.shaders);
        REQUIRE(a.renderer->init().has_value());
        a.renderer->setPassToggles(waterQuantitativeToggles());
        return a;
    }

    gpu::ImageF render(Arm& a, const Slab& slab, const scene::WaterSettings& settings, const View& view,
                       double seconds = 6.0, bool water = true) {
        scene::Scene s = slabScene(slab, settings, view);
        auto toggles = waterQuantitativeToggles();
        toggles.water = water;
        a.renderer->setPassToggles(toggles);
        FrameTime time{};
        time.renderTime = seconds;
        time.deltaTime = 1.0 / 60.0;
        a.renderer->resetTemporalHistory();
        auto image = a.renderer->renderToImageFloat(s, time, size, size);
        a.renderer->setPassToggles(waterQuantitativeToggles());
        REQUIRE(image.has_value());
        return std::move(*image);
    }
};

Bench makeBench() {
    Bench b;
    b.ctx = makeWaterContext();
    return b;
}

fs::path strippedCascadeDir() {
    int blocks = 0;
    const std::string stripped = stripMarkedBlocks(readWaterShaderSource(), kCascadeBegin, kCascadeEnd, &blocks);
    INFO(blocks << " marked cascade blocks removed");
    REQUIRE(blocks >= 6);
    for (const char* name : {"cascadeTurn", "cascadeWhite", "cascadeUp", "steep", "shore.w"}) {
        INFO(name);
        REQUIRE(stripped.find(name) == std::string::npos);
    }
    return writeWaterShaderVariant("no-cascade", stripped);
}

// Mean luminance of the rows [y0, y1) over the columns [x0, x1).
double bandMean(const gpu::ImageF& img, std::uint32_t y0, std::uint32_t y1, std::uint32_t x0, std::uint32_t x1) {
    double sum = 0.0;
    std::size_t n = 0;
    for (std::uint32_t y = y0; y < y1; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            const float* p = img.pixel(x, y);
            sum += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
            ++n;
        }
    }
    return n > 0 ? sum / static_cast<double>(n) : 0.0;
}

double meanAbsDifference(const gpu::ImageF& a, const gpu::ImageF& b) {
    const std::vector<float> la = luminance(a);
    const std::vector<float> lb = luminance(b);
    double sum = 0.0;
    for (std::size_t i = 0; i < la.size(); ++i) {
        sum += std::fabs(static_cast<double>(la[i]) - lb[i]);
    }
    return sum / static_cast<double>(la.size());
}

} // namespace

// ---- flat water is untouched ----------------------------------------------------------------------

TEST_CASE("flat water is byte-identical to the shader with no cascade code",
          "[gpu][renderer][water][cascade][identity]") {
    const fs::path strippedDir = strippedCascadeDir();
    Bench bench = makeBench();
    Arm live = bench.arm();
    Arm none = bench.arm({strippedDir});

    const Slab flat{};
    const std::array<View, 2> views{{{{0.0f, 12.0f, -45.0f}, {0.0f, 0.0f, 5.0f}},
                                     {{6.0f, 30.0f, -8.0f}, {0.0f, 0.0f, 2.0f}}}};
    std::size_t cases = 0;
    std::size_t identical = 0;
    for (const View& view : views) {
        for (const float tears : {0.0f, 0.6f}) {
            for (const double seconds : {3.0, 47.5}) {
                scene::WaterSettings w = cascadeSettings();
                withTears(w, tears);
                w.cascade = 1.5f; // the whitewater's uniform is live; flat water must not see it
                const gpu::ImageF withCode = bench.render(live, flat, w, view, seconds);
                const gpu::ImageF withoutCode = bench.render(none, flat, w, view, seconds);
                const gpu::ImageF dry = bench.render(live, flat, w, view, seconds, false);
                INFO(fmt::format("view ({:.0f},{:.0f},{:.0f}), tears {}, {} s: {} water pixels", view.eye.x,
                                 view.eye.y, view.eye.z, tears, seconds, countMask(waterMask(withCode, dry))));
                REQUIRE(countMask(waterMask(withCode, dry)) > 5000);
                const bool same = gpu::hashImage(withCode) == gpu::hashImage(withoutCode);
                CHECK(same);
                identical += same ? 1 : 0;
                ++cases;
            }
        }
    }
    INFO(identical << " of " << cases << " identical");
    // THE CONTROL: the same comparison on a steep sheet does differ, so the equality above is not two
    // renders that could never have differed.
    Slab steep{};
    steep.degrees = 35.0f;
    const View facing{steep.centre() + steep.up() * 25.0f, steep.centre()};
    scene::WaterSettings w = cascadeSettings();
    w.cascade = 1.0f;
    CHECK(gpu::hashImage(bench.render(live, steep, w, facing)) != gpu::hashImage(bench.render(none, steep, w, facing)));
    CHECK(bench.ctx->errorCount() == 0);
}

TEST_CASE("the QA scene's river is byte-identical to the shader with no cascade code wherever it is flat",
          "[gpu][renderer][water][cascade][identity]") {
    // The shipped world's river is not flat everywhere: a few slivers of its mesh, at the bank, are steeper
    // than 12 degrees and take the cascade path by design. So a third arm -- the live shader painting every
    // pixel that path takes -- marks them, and every pixel the code changes must be one of those.
    if (!fs::is_regular_file(qaWaterScene())) {
        SKIP("the water QA scene is not present");
    }
    const fs::path strippedDir = strippedCascadeDir();
    std::string flagged = readWaterShaderSource();
    const std::string anchor = "    out.color = vec4<f32>(color, alpha);\n";
    const std::size_t at = flagged.find(anchor);
    REQUIRE(at != std::string::npos);
    flagged.insert(at + anchor.size(), "    if (steep > 0.0) { out.color = vec4<f32>(1.0e4, 0.0, 0.0, 1.0); }\n");
    const fs::path flaggedDir = writeWaterShaderVariant("cascade-flagged", flagged);

    WaterBench bench = WaterBench::make(256, 176);
    WaterBench::Arm live = bench.arm();
    WaterBench::Arm none = bench.arm({strippedDir});
    WaterBench::Arm flag = bench.arm({flaggedDir});
    const std::array<View, 2> poses{{{{40.0f, 6.0f, -86.0f}, {12.0f, -1.0f, -108.0f}},
                                     {{20.0f, 34.0f, -96.0f}, {14.0f, 0.0f, -110.0f}}}};
    std::size_t steepSeen = 0;
    for (const View& pose : poses) {
        bench.aim(pose.eye, pose.target);
        for (const double seconds : {3.0, 181.0}) {
            bench.hold(seconds);
            bench.scene().waters.at(0).settings.cascade = 1.5f;
            const gpu::ImageF withCode = bench.render(live);
            const gpu::ImageF withoutCode = bench.render(none);
            const gpu::ImageF steepPixels = bench.render(flag);
            const std::size_t water = countMask(waterMask(withCode, bench.renderDry(live)));
            std::size_t moved = 0;
            std::size_t movedFlat = 0;
            std::size_t steep = 0;
            for (std::size_t i = 0; i < static_cast<std::size_t>(withCode.width) * withCode.height; ++i) {
                const bool isSteep = steepPixels.rgba[i * 4] > 5000.0f;
                bool differs = false;
                for (int c = 0; c < 4; ++c) {
                    differs = differs || withCode.rgba[i * 4 + c] != withoutCode.rgba[i * 4 + c];
                }
                steep += isSteep ? 1 : 0;
                moved += differs ? 1 : 0;
                movedFlat += (differs && !isSteep) ? 1 : 0;
            }
            steepSeen += steep;
            INFO(fmt::format("pose ({:.0f},{:.0f},{:.0f}) at {} s: {} water pixels, {} on steep triangles, {} "
                             "changed, {} of them flat",
                             pose.eye.x, pose.eye.y, pose.eye.z, seconds, water, steep, moved, movedFlat));
            REQUIRE(water > 500);
            CHECK(movedFlat == 0);
        }
    }
    // Whether the poses see any steep sliver is the world's business, not this test's; say which it was.
    INFO(steepSeen << " steep pixels over the four frames");
    CHECK(bench.ctx->errorCount() == 0);
}

// ---- a steep sheet is shaded about its own plane ----------------------------------------------------

TEST_CASE("a falls above the camera's height is drawn as the part below it is, not as a ceiling",
          "[gpu][renderer][water][cascade]") {
    // A level camera looks up a 35-degree sheet that crosses its own height mid-frame: everything above
    // the frame's middle row is higher than the eye, everything below it lower. The eye is on the
    // sheet's upper side throughout, so nothing about the surface changes at that row.
    const fs::path strippedDir = strippedCascadeDir();
    Bench bench = makeBench();
    Arm live = bench.arm();
    Arm none = bench.arm({strippedDir});
    Slab steep{};
    steep.degrees = 35.0f;
    steep.y0 = -5.0f;
    steep.z0 = -10.0f;
    steep.length = 50.0f;
    const View level{{0.0f, 5.0f, -40.0f}, {0.0f, 5.0f, 10.0f}};
    const scene::WaterSettings w = cascadeSettings();
    const std::uint32_t mid = bench.size / 2;
    const std::uint32_t x0 = bench.size / 4;
    const std::uint32_t x1 = bench.size * 3 / 4;
    const auto jump = [&](const gpu::ImageF& img) {
        const double above = bandMean(img, mid - 14, mid - 4, x0, x1);
        const double below = bandMean(img, mid + 4, mid + 14, x0, x1);
        return std::fabs(std::log(std::max(above, 1e-6) / std::max(below, 1e-6)));
    };
    const gpu::ImageF withCode = bench.render(live, steep, w, level);
    const gpu::ImageF withoutCode = bench.render(none, steep, w, level);
    dumpWater(withCode, "cascade-level-live", 8.0f);
    dumpWater(withoutCode, "cascade-level-stripped", 8.0f);
    const double liveJump = jump(withCode);
    const double oldJump = jump(withoutCode);
    INFO(fmt::format("log ratio across the eye's height: {:.3f} live, {:.3f} without the cascade code", liveJump,
                     oldJump));
    CHECK(liveJump < 0.15);
    // The control: without the code the rows above the eye were shaded from under the water.
    CHECK(oldJump > 3.0 * liveJump);
    CHECK(oldJump > 0.3);
    CHECK(bench.ctx->errorCount() == 0);
}

TEST_CASE("past 30 degrees the tears reach nothing", "[gpu][renderer][water][cascade]") {
    // Both arms draw with the torn pipeline (a surface at tears 0 is drawn with the tearless one, which
    // the compiler builds differently): a trace of tears against a full set of them. On a 35-degree
    // sheet the live shader scales every tear term by 0, so the two are the same bytes; without the
    // cascade code the tears are drawn on the slope.
    const fs::path strippedDir = strippedCascadeDir();
    Bench bench = makeBench();
    Arm live = bench.arm();
    Arm none = bench.arm({strippedDir});
    Slab steep{};
    steep.degrees = 35.0f;
    const View facing{steep.centre() + steep.up() * 25.0f, steep.centre()};
    scene::WaterSettings trace = cascadeSettings();
    withTears(trace, 1e-6f);
    scene::WaterSettings torn = cascadeSettings();
    withTears(torn, 2.0f);
    const gpu::ImageF liveTrace = bench.render(live, steep, trace, facing);
    const gpu::ImageF liveTorn = bench.render(live, steep, torn, facing);
    const double oldTears =
        meanAbsDifference(bench.render(none, steep, torn, facing), bench.render(none, steep, trace, facing));
    INFO(fmt::format("what the tears change on a 35-degree sheet without the cascade code: {:.3g}", oldTears));
    CHECK(gpu::hashImage(liveTorn) == gpu::hashImage(liveTrace));
    CHECK(oldTears > 1e-4);
    CHECK(bench.ctx->errorCount() == 0);
}

TEST_CASE("cascade whitens a steep sheet in streaks along the flow, and never a flat one",
          "[gpu][renderer][water][cascade]") {
    Bench bench = makeBench();
    Arm live = bench.arm();
    Slab steep{};
    steep.degrees = 35.0f;
    // Straight at the sheet from 12 m, so the slope -- and the flow down it -- runs up the frame and a
    // whitewater grain spans several pixels. No ripples and no glint: what changes is the whitewater alone.
    const View facing{steep.centre() + steep.up() * 12.0f, steep.centre()};
    scene::WaterSettings off = cascadeSettings();
    off.ripple = 0.0f;
    off.specular = 0.0f; // and no moon glint, whose highlight would outweigh every streak
    scene::WaterSettings on = off;
    on.cascade = 1.0f;
    const gpu::ImageF plain = bench.render(live, steep, off, facing);
    const gpu::ImageF white = bench.render(live, steep, on, facing);
    dumpWater(white, "cascade-streaks", 4.0f);
    const std::uint32_t n = bench.size;
    const double gain = bandMean(white, 0, n, 0, n) - bandMean(plain, 0, n, 0, n);
    // Across the flow (along a row) against along it (down a column).
    const std::vector<float> l = luminance(white);
    double across = 0.0;
    double along = 0.0;
    for (std::uint32_t y = 8; y + 9 < n; ++y) {
        for (std::uint32_t x = 8; x + 9 < n; ++x) {
            const float c = l[static_cast<std::size_t>(y) * n + x];
            across += std::fabs(l[static_cast<std::size_t>(y) * n + x + 1] - c);
            along += std::fabs(l[static_cast<std::size_t>(y + 1) * n + x] - c);
        }
    }
    INFO(fmt::format("mean luminance +{:.3f}; gradient across the flow {:.1f}, along it {:.1f}", gain, across, along));
    CHECK(gain > 0.1);
    CHECK(across > 2.0 * along);

    // And the same settings on flat water change nothing at all.
    const Slab flat{};
    const View above{{0.0f, 12.0f, -45.0f}, {0.0f, 0.0f, 5.0f}};
    CHECK(gpu::hashImage(bench.render(live, flat, off, above)) == gpu::hashImage(bench.render(live, flat, on, above)));
    CHECK(bench.ctx->errorCount() == 0);
}
