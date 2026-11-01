// ADR-1931: the ring with a fixed capacity and a stride, and the sampler (slit scan as a time map) that reads it.
//
// The fixture is ADR-410's: a bright mover crossing a dark frame, placed by its absolute frame, so a frame's history is
// genuinely different from the frame and a broken sampler cannot pass by returning the current image.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "rendering/temporal_history.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "support/image_diff.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <memory>

using namespace avgen;

#define CHECK_IDENTICAL(a, b)                                                                      \
    do {                                                                                           \
        const auto d__ = ::avgen::testing::byteDiff((a).rgba, (b).rgba);                           \
        INFO(d__.describe());                                                                      \
        CHECK(d__.identical());                                                                    \
    } while (false)
#define CHECK_DIFFERS(a, b)                                                                        \
    do {                                                                                           \
        const auto d__ = ::avgen::testing::byteDiff((a).rgba, (b).rgba);                           \
        INFO(d__.describe());                                                                      \
        CHECK_FALSE(d__.identical());                                                              \
    } while (false)

namespace {

constexpr std::uint32_t kSize = 160;

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

scene::Scene movingLight() {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.camera.position = {0.0f, 0.0f, 12.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    scene::Entity e;
    e.name = "mover";
    e.mesh = s.addMesh(scene::makeIcosphere(0.6f, 2));
    e.material.baseColor = {1.0f, 1.0f, 1.0f};
    e.material.emissiveColor = {1.0f, 0.85f, 0.6f};
    e.material.emissiveIntensity = 6.0f;
    s.entities.push_back(std::move(e));
    return s;
}

// The mover sweeps across and back, so every frame of a long history differs from its neighbours.
void placeAt(scene::Scene& s, int frame) {
    const float x = -4.5f + 0.3f * static_cast<float>(frame % 30);
    const float y = -2.0f + 0.13f * static_cast<float>(frame % 31);
    s.entities.front().transform.position = {x, y, 0.0f};
}

struct Shot {
    gpu::Image8 image;
    rendering::TemporalStats stats;
};

// Frames [first, last) on one fresh renderer: a renderer that starts late is a seek that lands at `first`.
// `perFrame` may change the scene by absolute frame (a routed parameter).
template <class PerFrame>
Shot playRange(gpu::Context& ctx, gpu::ShaderLibrary& shaders, scene::Scene s, int first, int last, PerFrame&& perFrame) {
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FixedStepClock clock(60.0);
    clock.restartAt(static_cast<double>(first) / 60.0);
    Shot out;
    for (int i = first; i < last; ++i) {
        placeAt(s, i);
        perFrame(s, i);
        FrameTime t = clock.tick();
        t.frameIndex = static_cast<std::uint64_t>(i);
        auto img = renderer.renderToImage(s, t, kSize, kSize);
        REQUIRE(img.has_value());
        out.image = std::move(*img);
    }
    out.stats = renderer.stats().temporal;
    return out;
}
Shot playRange(gpu::Context& ctx, gpu::ShaderLibrary& shaders, scene::Scene s, int first, int last) {
    return playRange(ctx, shaders, std::move(s), first, last, [](scene::Scene&, int) {});
}

scene::Scene withSlit(scene::Scene s, float span, float angle, int capacity = 0, int stride = 1) {
    s.temporal.slit.enabled = true;
    s.temporal.slit.amount = 1.0f;
    s.temporal.slit.span = span;
    s.temporal.slit.mode = 0.0f;
    s.temporal.slit.angle = angle;
    s.temporal.history.capacity = capacity;
    s.temporal.history.stride = stride;
    return s;
}

// Mean absolute difference of one column (or row) of two images, 0..255.
double lineDiff(const gpu::Image8& a, const gpu::Image8& b, bool column, std::uint32_t at) {
    double sum = 0.0;
    for (std::uint32_t i = 0; i < kSize; ++i) {
        const std::uint32_t x = column ? at : i, y = column ? i : at;
        const std::size_t o = (static_cast<std::size_t>(y) * kSize + x) * 4;
        for (int c = 0; c < 3; ++c) sum += std::abs(static_cast<int>(a.rgba[o + c]) - static_cast<int>(b.rgba[o + c]));
    }
    return sum / (kSize * 3.0);
}

} // namespace

TEST_CASE("a slit at amount 0 is the frame without it, and a long span lands where a play does", "[gpu][temporal][adr1931]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const scene::Scene plain = movingLight();
    const int last = 80;
    const Shot off = playRange(*ctx, shaders, plain, 0, last);
    scene::Scene idle = withSlit(plain, 60.0f, 0.0f, 64);
    idle.temporal.slit.amount = 0.0f;
    const Shot idleShot = playRange(*ctx, shaders, idle, 0, last);
    CHECK_IDENTICAL(off.image, idleShot.image); // enabled at 0: the ring is warm (64 layers), the picture untouched
    CHECK(idleShot.stats.historyLayers == 64u);

    const scene::Scene on = withSlit(plain, 60.0f, 0.0f, 64);
    const Shot played = playRange(*ctx, shaders, on, 0, last);
    CHECK_DIFFERS(off.image, played.image);
    // FIR, at more than ADR-410's old 32-frame ceiling: a renderer that starts 64 frames before the last has the same ring.
    const Shot late = playRange(*ctx, shaders, on, last - 64, last);
    CHECK_IDENTICAL(played.image, late.image);
    // ...and one that starts short of it does not (the history is shorter), so the check above can fail.
    const Shot short_ = playRange(*ctx, shaders, on, last - 20, last);
    CHECK_DIFFERS(played.image, short_.image);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("vertical and horizontal slit scans put now at one edge and the past across the frame", "[gpu][temporal][adr1931]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const scene::Scene plain = movingLight();
    const int last = 70;
    const Shot off = playRange(*ctx, shaders, plain, 0, last);
    // Quantised into 8 hard strips, strip 0 is age exactly 0: the current frame, untouched. So, measured a few pixels
    // inside the edge strip (away from the post chain's neighbourhood at the strip boundary):
    // Bloom off for these: a bright history copy in the next strip would otherwise bloom into the edge strip.
    scene::Scene unbloomed = plain;
    unbloomed.post.bloomIntensity = 0.0f;
    const Shot offFlat = playRange(*ctx, shaders, unbloomed, 0, last);
    const auto edged = [&](float angle, bool reverse) {
        scene::Scene sc = withSlit(unbloomed, 40.0f, angle);
        sc.temporal.slit.strips = 8.0f;
        sc.temporal.slit.blend = 0.0f;
        sc.temporal.slit.reverse = reverse ? 1.0f : 0.0f;
        return playRange(*ctx, shaders, sc, 0, last);
    };
    // angle 0: age grows left to right, so the left strip IS the current frame and the right strip is 40 frames old
    const Shot vertical = edged(0.0f, false);
    for (std::uint32_t x : {2u, 8u}) CHECK(lineDiff(offFlat.image, vertical.image, true, x) < 0.25);
    CHECK_DIFFERS(offFlat.image, vertical.image);
    // angle 90: the top strip is now
    const Shot horizontal = edged(90.0f, false);
    for (std::uint32_t y : {2u, 8u}) CHECK(lineDiff(offFlat.image, horizontal.image, false, y) < 0.25);
    CHECK_DIFFERS(vertical.image, horizontal.image);
    // reversed, the right strip is now
    const Shot reversed = edged(0.0f, true);
    for (std::uint32_t x : {kSize - 3, kSize - 9}) CHECK(lineDiff(offFlat.image, reversed.image, true, x) < 0.25);
    CHECK_DIFFERS(vertical.image, reversed.image);
    const Shot continuous = playRange(*ctx, shaders, withSlit(plain, 40.0f, 0.0f), 0, last);
    // strips quantise the map: a different picture from the continuous one
    scene::Scene stripped = withSlit(plain, 40.0f, 0.0f);
    stripped.temporal.slit.strips = 8.0f;
    stripped.temporal.slit.blend = 0.0f;
    const Shot strips = playRange(*ctx, shaders, stripped, 0, last);
    CHECK_DIFFERS(continuous.image, strips.image);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("a routed span at a fixed capacity never reallocates the ring", "[gpu][temporal][adr1931]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const scene::Scene plain = movingLight();
    const auto wobble = [](scene::Scene& s, int i) { s.temporal.slit.span = 10.0f + static_cast<float>((i * 7) % 50); };
    const Shot fixed = playRange(*ctx, shaders, withSlit(plain, 10.0f, 0.0f, 64), 0, 60, wobble);
    CHECK(fixed.stats.historyAllocations == 1u);
    CHECK(fixed.stats.historyLayers == 64u);
    // the memory is the ring's own measure: 64 layers of 80x80 (half of 160) at 4 bytes (RG11B10Ufloat), or 8 where
    // that format is not renderable
    const std::uint64_t layerTexels = 80ull * 80ull * 64ull;
    CHECK((fixed.stats.historyBytes == layerTexels * 4 || fixed.stats.historyBytes == layerTexels * 8));
    CHECK(fixed.stats.framesValid == 60u); // never dropped
    // the old behaviour, kept for capacity 0: the ring follows the bound, and each change reallocates and drops
    const Shot autoSized = playRange(*ctx, shaders, withSlit(plain, 10.0f, 0.0f, 0), 0, 60, wobble);
    CHECK(autoSized.stats.historyAllocations > 10u);
    CHECK(autoSized.stats.framesValid < 60u);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("a stride captures every Nth timeline frame and still lands where a play does", "[gpu][temporal][adr1931]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const scene::Scene plain = movingLight();
    const scene::Scene strided = withSlit(plain, 60.0f, 0.0f, 16, 4); // 16 layers x 4 = 64 frames
    const int last = 101; // not a multiple of the stride: the newest layer is a frame old
    const Shot played = playRange(*ctx, shaders, strided, 0, last);
    CHECK(played.stats.historyStride == 4u);
    CHECK(played.stats.framesValid == 16u);
    const Shot late = playRange(*ctx, shaders, strided, last - 64, last);
    CHECK_IDENTICAL(played.image, late.image);
    // a stride 1 ring of the same layers is a quarter of the span: a different picture
    const Shot dense = playRange(*ctx, shaders, withSlit(plain, 60.0f, 0.0f, 16, 1), 0, last);
    CHECK_DIFFERS(played.image, dense.image);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("re-rendering a strided frame reproduces it", "[gpu][temporal][determinism][adr1931]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    scene::Scene s = withSlit(movingLight(), 30.0f, 0.0f, 16, 3);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    gpu::Image8 first, second;
    for (int i = 0; i <= 48; ++i) {
        placeAt(s, i);
        FrameTime t;
        t.frameIndex = static_cast<std::uint64_t>(i);
        t.renderTime = i / 60.0;
        t.deltaTime = i == 0 ? 0.0 : 1.0 / 60.0;
        auto img = renderer.renderToImage(s, t, kSize, kSize);
        REQUIRE(img.has_value());
        if (i == 48) {
            first = std::move(*img);
            auto again = renderer.renderToImage(s, t, kSize, kSize); // frame 48 (a capture frame) twice
            REQUIRE(again.has_value());
            second = std::move(*again);
        }
    }
    CHECK_IDENTICAL(first, second);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("debug geometry is drawn after the capture and never enters the history", "[gpu][temporal][adr1931]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const scene::Scene s = withSlit(movingLight(), 30.0f, 0.0f, 32);
    // Two runs; one queues a bright debug box on every frame but the last. If the overlay reached the ring, the last
    // frame's slit (which shows only history on its right side) would carry it.
    const auto run = [&](bool overlay) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        scene::Scene sc = s;
        gpu::Image8 out;
        for (int i = 0; i < 40; ++i) {
            placeAt(sc, i);
            if (overlay && i < 39) {
                renderer.debugDraw().box(glm::vec3(-5.0f, -3.0f, -1.0f), glm::vec3(5.0f, 3.0f, 1.0f),
                                         glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
            }
            FrameTime t;
            t.frameIndex = static_cast<std::uint64_t>(i);
            t.renderTime = i / 60.0;
            auto img = renderer.renderToImage(sc, t, kSize, kSize);
            REQUIRE(img.has_value());
            out = std::move(*img);
        }
        return out;
    };
    const gpu::Image8 clean = run(false);
    const gpu::Image8 withOverlay = run(true);
    CHECK_IDENTICAL(clean, withOverlay);
    CHECK(ctx->errorCount() == 0);
}
