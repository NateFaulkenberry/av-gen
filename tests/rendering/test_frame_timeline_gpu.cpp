// The frame GPU timeline on a real device (gpu/frame_timeline.hpp).
//
// The instrument this replaced gave each pass its own begin/end pair. That does not measure the
// pass: the begin timestamp is written when the pass is reached, not when its work starts, so a
// pass behind a heavy one absorbed the drain of everything still in flight. On the world scene the
// volumetric pass reported 39.4 ms of a 46 ms frame while removing volumetrics entirely moved the
// frame from 53.1 ms to 47.7 -- the pass costs 5.4 ms. The tell was that the number did not
// respond to its own workload.
//
// What is pinned here is what makes the replacement trustworthy on a device: the passes partition
// the frame (they sum to it), each phase appears under its own label only when it actually ran,
// and a phase's number moves with that phase's own workload.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;

namespace {

constexpr std::uint32_t kSize = 512;

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

scene::MeshData boxMesh(float h) {
    scene::MeshData m;
    const glm::vec3 normals[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (const glm::vec3 normal : normals) {
        const glm::vec3 u = std::abs(normal.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::cross(glm::vec3(0, 1, 0), normal);
        const glm::vec3 v = glm::cross(normal, u);
        const auto base = static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({normal * h - u * h - v * h, normal, {0, 0}});
        m.vertices.push_back({normal * h + u * h - v * h, normal, {1, 0}});
        m.vertices.push_back({normal * h + u * h + v * h, normal, {1, 1}});
        m.vertices.push_back({normal * h - u * h + v * h, normal, {0, 1}});
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return m;
}

scene::Scene boxScene() {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.camera.position = {0.0f, 1.5f, 6.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.camera.nearPlane = 0.1f;
    s.camera.farPlane = 200.0f;
    const auto mesh = s.addMesh(boxMesh(1.0f));
    for (int i = -2; i <= 2; ++i) {
        auto& e = s.addEntity("box" + std::to_string(i), mesh);
        e.transform.position = {static_cast<float>(i) * 2.5f, 0.0f, 0.0f};
        e.material.baseColor = glm::vec3(0.7f);
        e.castsShadow = true;
    }
    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    key.intensity = 3.0f;
    key.castsShadow = true;
    s.addLight(key);
    return s;
}

// Renders `frames` frames and returns the last completed timeline. The readback ring is
// deliberately non-stalling, so the numbers lag the frame that produced them by two or three;
// a handful of frames is enough for them to land.
struct Timings {
    std::vector<gpu::TimelineInterval> passes;
    double frameMs = -1.0;
    std::uint32_t unwritten = 0;
};

Timings run(rendering::SceneRenderer& renderer, const scene::Scene& s, int frames = 12) {
    for (int i = 0; i < frames; ++i) {
        FrameTime time{};
        time.renderTime = static_cast<double>(i) / 60.0;
        time.deltaTime = 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(i);
        REQUIRE(renderer.renderFrame(s, time, kSize, kSize).has_value());
    }
    Timings t;
    t.passes = renderer.timeline().passes();
    t.frameMs = renderer.timeline().frameMs();
    t.unwritten = renderer.timeline().unwritten();
    return t;
}

double sumOf(const Timings& t) {
    double total = 0.0;
    for (const auto& p : t.passes) {
        total += p.ms;
    }
    return total;
}

bool hasLabel(const Timings& t, const std::string& label) {
    return std::any_of(t.passes.begin(), t.passes.end(), [&](const auto& p) { return p.label == label; });
}

double msFor(const Timings& t, const std::string& label) {
    double total = 0.0;
    for (const auto& p : t.passes) {
        if (p.label == label) {
            total += p.ms;
        }
    }
    return total;
}

std::unique_ptr<rendering::SceneRenderer> makeRenderer(gpu::Context& ctx, gpu::ShaderLibrary& shaders) {
    auto renderer = std::make_unique<rendering::SceneRenderer>(ctx, shaders);
    REQUIRE(renderer->init().has_value());
    return renderer;
}

} // namespace

TEST_CASE("the frame timeline's passes partition the frame", "[timeline][gpu]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = makeRenderer(*ctx, shaders);
    if (!renderer->timeline().available()) {
        SKIP("timestamp queries unavailable on this adapter");
    }
    const scene::Scene s = boxScene();
    const Timings t = run(*renderer, s);

    REQUIRE(t.passes.size() > 4);
    REQUIRE(t.frameMs >= 0.0);
    // Consecutive pass ends on one timeline are contiguous intervals, so they add up to the frame
    // exactly. This is the property the old per-pass begin/end pairs never had: their numbers
    // overlapped each other and overshot the frame, which is how one pass came to claim 39.4 ms
    // of a 46 ms frame.
    CHECK(std::abs(sumOf(t) - t.frameMs) < 1e-6);

    // Every phase of a lit, shadowed frame is named.
    CHECK(hasLabel(t, "shadow"));
    CHECK(hasLabel(t, "scene"));
    CHECK(hasLabel(t, "tonemap"));
    // No pass may be charged more than the whole frame.
    for (const auto& p : t.passes) {
        CHECK(p.ms <= t.frameMs + 1e-9);
        CHECK(p.ms >= 0.0);
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("a phase that does not run is absent from the timeline", "[timeline][gpu]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = makeRenderer(*ctx, shaders);
    if (!renderer->timeline().available()) {
        SKIP("timestamp queries unavailable on this adapter");
    }
    scene::Scene s = boxScene();

    // Fog off: no volume label at all, and the stat stays -1 rather than reporting a stale number.
    const Timings without = run(*renderer, s);
    CHECK_FALSE(hasLabel(without, "volume"));
    CHECK(renderer->stats().volume.volumeMs < 0.0);

    s.environment.volumeDensity = 0.04f;
    s.environment.volumeScattering = 0.6f;
    s.environment.volumeSteps = 48;
    s.environment.volumeMaxDistance = 120.0f;
    const Timings with = run(*renderer, s);
    CHECK(hasLabel(with, "volume"));
    CHECK(renderer->stats().volume.volumeMs >= 0.0);
    CHECK(std::abs(sumOf(with) - with.frameMs) < 1e-6);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("a pass's timeline number responds to that pass's own workload", "[timeline][gpu]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = makeRenderer(*ctx, shaders);
    if (!renderer->timeline().available()) {
        SKIP("timestamp queries unavailable on this adapter");
    }
    scene::Scene s = boxScene();
    s.environment.volumeDensity = 0.04f;
    s.environment.volumeScattering = 0.6f;
    s.environment.volumeMaxDistance = 200.0f;

    // The march's cost is linear in its step count and nothing else in the frame changes with it.
    // The old instrument's headline failure was exactly this: halving the march steps moved its
    // reported number by under 2 ms out of a claimed 39.4.
    const auto marchMs = [&](std::uint32_t steps) {
        s.environment.volumeSteps = steps;
        // One run discarded before measuring. The first encode of a given configuration pays
        // pipeline compilation, and that lands inside the measured interval: the 16-step arm, which
        // runs first, reported 40.6 ms for a pass whose real cost is under one.
        (void)msFor(run(*renderer, s, 20), "volume");
        // The *least* of several, not the greatest. Taking the maximum keeps whichever sample was
        // most disturbed -- by compilation, by another process on the GPU, by anything -- which is
        // the opposite of what a cost measurement wants, and it fails in both directions: a spike
        // in the light arm fails the test, and a spike in the heavy arm passes it for the wrong
        // reason. The minimum is the closest observation to an undisturbed one, which is the
        // estimator the rest of this project's benchmarking uses on a shared machine.
        double best = std::numeric_limits<double>::max();
        for (int attempt = 0; attempt < 3; ++attempt) {
            best = std::min(best, msFor(run(*renderer, s, 20), "volume"));
        }
        return best;
    };
    const double light = marchMs(16);
    const double heavy = marchMs(256);

    INFO("volume march: 16 steps = " << light << " ms, 256 steps = " << heavy << " ms");
    // Sixteen times the marching work. The bar is deliberately loose -- this runs on whatever GPU
    // is present, alongside whatever else the machine is doing -- but a number that does not move
    // at all with its own workload is the defect this instrument exists to have removed.
    CHECK(heavy > light * 2.0);
    CHECK(ctx->errorCount() == 0);
}
