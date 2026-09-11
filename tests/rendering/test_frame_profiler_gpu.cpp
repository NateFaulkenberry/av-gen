// Does the profiler measure what it claims to? (ADR-077, docs/renderer-2-benchmark.md)
//
// The renderer 2.0 brief puts it plainly: if changing a known GPU workload does not change the
// reported GPU time, the measurement is wrong. Everything downstream of this phase -- visibility,
// LOD, instancing, all of it -- is evaluated by diffing these numbers, so the numbers are what is
// tested here, against workloads whose direction of change is known before the timer is read.
//
// Each case moves exactly one thing and names the label that must respond. A pass whose number
// does not move with its own workload is not a slow pass; it is a broken instrument, and the
// instrument this replaced failed precisely that way -- the volumetric pass claimed 39.4 ms of a
// 46 ms frame and did not budge when its march steps were halved.
//
// These are timing tests on whatever GPU is present, on a machine doing whatever else it is doing.
// The bars are deliberately loose, the estimator is a median of medians, and where a bar could be
// made relative it was: an absolute "this frame costs under 4 ms" assertion measured 3.7 ms alone
// and 10.4 ms with another process on the same GPU, so what is compared is two arms of the same
// run against each other. What is asserted is direction and rough magnitude, never a millisecond.

#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/render_stats.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <numeric>
#include <string>
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

std::unique_ptr<rendering::SceneRenderer> makeRenderer(gpu::Context& ctx, gpu::ShaderLibrary& shaders) {
    auto renderer = std::make_unique<rendering::SceneRenderer>(ctx, shaders);
    REQUIRE(renderer->init().has_value());
    return renderer;
}

// ---- workloads ------------------------------------------------------------------------------

// An n x n grid of quads on the XY plane, 2n^2 triangles, so a case can ask for a known triangle
// count rather than for "a dense mesh" and hope.
scene::MeshData gridMesh(int n) {
    scene::MeshData m;
    const float step = 2.0f / static_cast<float>(n);
    for (int y = 0; y <= n; ++y) {
        for (int x = 0; x <= n; ++x) {
            const float px = -1.0f + step * static_cast<float>(x);
            const float py = -1.0f + step * static_cast<float>(y);
            m.vertices.push_back({{px, py, 0.0f},
                                  {0.0f, 0.0f, 1.0f},
                                  {static_cast<float>(x) / static_cast<float>(n),
                                   static_cast<float>(y) / static_cast<float>(n)}});
        }
    }
    const auto stride = static_cast<std::uint32_t>(n + 1);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const auto a = static_cast<std::uint32_t>(y) * stride + static_cast<std::uint32_t>(x);
            m.indices.insert(m.indices.end(), {a, a + 1, a + stride + 1, a, a + stride + 1, a + stride});
        }
    }
    return m;
}

scene::Scene baseScene() {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.camera.position = {0.0f, 0.0f, 6.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.camera.nearPlane = 0.1f;
    s.camera.farPlane = 200.0f;
    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    key.intensity = 3.0f;
    s.addLight(key);
    return s;
}

// `count` copies of one mesh, spread across the view. Triangle count is `count * 2 * n * n` and
// screen coverage barely changes with `n`, so varying `n` alone varies vertices and nothing else.
scene::Scene geometryScene(int count, int n, bool casters = false) {
    scene::Scene s = baseScene();
    const auto mesh = s.addMesh(gridMesh(n));
    // The renderer re-uploads meshes only when `meshVersion` changes, and every freshly built
    // Scene starts that counter in the same place. Two different scenes with one mesh each,
    // rendered through one renderer, therefore both draw whichever one was uploaded first -- so a
    // geometry A/B silently measures the same mesh twice. Stamping a version derived from the
    // shape is what makes these two arms actually differ. (The renderer is not fixed here; that is
    // a behaviour change and this phase only measures. See docs/renderer-2-benchmark.md.)
    s.meshVersion = static_cast<std::uint64_t>(n) * 1000u + static_cast<std::uint64_t>(count) + 1u;
    for (int i = 0; i < count; ++i) {
        auto& e = s.addEntity("g" + std::to_string(i), mesh);
        const auto f = static_cast<float>(i);
        e.transform.position = {std::sin(f) * 3.0f, std::cos(f * 1.7f) * 2.0f, -f * 0.05f};
        e.transform.scale = glm::vec3(0.6f);
        e.material.baseColor = glm::vec3(0.7f);
        e.castsShadow = casters;
    }
    if (casters) {
        s.lights[0].castsShadow = true;
    }
    return s;
}

// `count` large translucent quads stacked towards the camera. Two triangles each, so the geometry
// barely moves; what moves is how many times every pixel is shaded and blended.
scene::Scene overdrawScene(int count) {
    scene::Scene s = baseScene();
    const auto mesh = s.addMesh(gridMesh(1));
    s.meshVersion = 900'000u + static_cast<std::uint64_t>(count); // see geometryScene()
    for (int i = 0; i < count; ++i) {
        auto& e = s.addEntity("q" + std::to_string(i), mesh);
        e.transform.position = {0.0f, 0.0f, -static_cast<float>(i) * 0.05f};
        e.transform.scale = glm::vec3(20.0f); // well past the frame edges at this camera distance
        e.material.baseColor = glm::vec3(0.5f, 0.6f, 0.8f);
        e.material.alphaMode = scene::AlphaMode::Blend;
        e.material.opacity = 0.2f;
        e.material.doubleSided = true;
    }
    return s;
}

std::uint64_t specHash(const scene::SourceSpec& s) {
    std::uint64_t h = 0xD15EA5E01ull;
    const auto mix = [&](std::uint64_t v) { h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2); };
    mix(static_cast<std::uint64_t>(s.kind));
    mix(static_cast<std::uint64_t>(s.subdivisions));
    return h == 0 ? 1 : h;
}

// A scattered procedural object with culling and four LOD levels: the only path in the renderer
// whose instance counts are decided on the GPU, and therefore the only one whose submitted-geometry
// counters can be stale rather than exact.
scene::Scene scatterScene(int columns, int rows, bool cull) {
    scene::Scene s = baseScene();
    s.camera.position = {0.0f, 8.0f, 30.0f};
    scene::ProceduralGeometry g;
    g.name = "scatter";
    g.source.kind = scene::PrimitiveKind::Box;
    g.source.size = {1.0f, 1.0f, 1.0f};
    g.source.subdivisions = 1;
    g.meshHash = specHash(g.source);
    g.structureVersion = 1;
    g.material.baseColor = {0.8f, 0.7f, 0.6f};
    g.lod.cull = cull;
    g.lod.lodCount = cull ? 4 : 1;
    g.lod.lodByScreenSize = false;
    g.lod.lodDistances[0] = 20.0f;
    g.lod.lodDistances[1] = 45.0f;
    g.lod.lodDistances[2] = 80.0f;
    g.instances.reserve(static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows));
    for (int z = 0; z < rows; ++z) {
        for (int x = 0; x < columns; ++x) {
            scene::InstanceRecord r{};
            const float px = (static_cast<float>(x) - static_cast<float>(columns - 1) * 0.5f) * 2.0f;
            const float pz = (static_cast<float>(z) - static_cast<float>(rows - 1) * 0.5f) * 2.0f;
            r.position = {px, 0.0f, pz, 1.0f};
            r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
            r.scale = {1.0f, 1.0f, 1.0f, 0.0f};
            r.random = {0.25f, 0.5f, 0.75f, 0.125f};
            r.color = {1.0f, 1.0f, 1.0f, static_cast<float>(g.instances.size())};
            r.emissive = {1.0f, 1.0f, 1.0f, 0.0f};
            g.instances.push_back(r);
        }
    }
    s.procedurals.push_back(std::move(g));
    return s;
}

// ---- sampling -------------------------------------------------------------------------------

struct Profile {
    std::vector<gpu::TimelineInterval> passes; // per-label medians over the sampled frames
    double frameMs = -1.0;                     // median GPU frame
    int samples = 0;
    rendering::RenderStats stats;              // the last frame's counters
    std::uint32_t unwritten = 0;
    std::vector<std::string> unwrittenLabels;
    std::vector<std::uint64_t> lastTimestamps;
};

// The readback ring is deliberately non-stalling, so several renderFrame() calls in a row report
// the same completed frame. Sampling per render call would count one frame's numbers five times
// and make a median of five copies of one sample; `completedFrames()` is what distinguishes a new
// reading from a repeat of the last.
Profile profile(rendering::SceneRenderer& renderer, const scene::Scene& scene, std::uint32_t width,
                std::uint32_t height, int frames = 32, int warmup = 8) {
    std::vector<std::vector<gpu::TimelineInterval>> collected;
    std::vector<double> frameSamples;
    std::uint64_t seen = renderer.timeline().completedFrames();
    Profile out;
    for (int i = 0; i < frames; ++i) {
        FrameTime time{};
        time.renderTime = static_cast<double>(i) / 60.0;
        time.deltaTime = 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(i);
        REQUIRE(renderer.renderFrame(scene, time, width, height).has_value());
        const std::uint64_t now = renderer.timeline().completedFrames();
        if (now == seen) {
            continue;
        }
        seen = now;
        // The first frames build pipelines and shadow maps and settle the empty-LOD suppression.
        if (i < warmup) {
            continue;
        }
        collected.push_back(renderer.timeline().passes());
        frameSamples.push_back(renderer.timeline().frameMs());
    }
    out.stats = renderer.stats();
    out.unwritten = renderer.timeline().unwritten();
    out.unwrittenLabels = renderer.timeline().unwrittenLabels();
    out.lastTimestamps = renderer.timeline().timestamps();
    out.samples = static_cast<int>(frameSamples.size());
    if (frameSamples.empty()) {
        return out;
    }
    std::sort(frameSamples.begin(), frameSamples.end());
    out.frameMs = frameSamples[frameSamples.size() / 2];
    out.passes = rendering::medianByLabel(collected);
    return out;
}

double labelMs(const Profile& p, std::string_view label) {
    const auto it = std::find_if(p.passes.begin(), p.passes.end(),
                                 [&](const auto& e) { return e.label == label; });
    return it == p.passes.end() ? -1.0 : it->ms;
}

bool hasLabel(const Profile& p, std::string_view label) { return labelMs(p, label) >= 0.0; }

double sumOf(const Profile& p) {
    return std::accumulate(p.passes.begin(), p.passes.end(), 0.0,
                           [](double acc, const auto& e) { return acc + e.ms; });
}

// One label's reading for an A/B arm: the median over repeats of the median over frames. The
// minimum was tried first and is worse -- the counter ticks every 65,536 ns, so at three or four
// ticks a pass the minimum of three runs collapses two genuinely different workloads onto the same
// tick and the A/B reads as a null result.
double stableMs(rendering::SceneRenderer& renderer, const scene::Scene& scene, std::uint32_t width,
                std::uint32_t height, std::string_view label, int repeats = 3, int frames = 32) {
    std::vector<double> samples;
    for (int i = 0; i < repeats; ++i) {
        const double ms = labelMs(profile(renderer, scene, width, height, frames), label);
        if (ms >= 0.0) {
            samples.push_back(ms);
        }
    }
    if (samples.empty()) {
        return -1.0;
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

std::string table(const Profile& p) {
    std::vector<gpu::TimelineInterval> sorted = p.passes;
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.ms > b.ms; });
    std::string out;
    for (const auto& e : sorted) {
        out += " " + e.label + "=" + std::to_string(e.ms).substr(0, 5);
    }
    return out;
}

} // namespace

TEST_CASE("the frame timeline reports nanoseconds, not an arbitrary counter", "[profiler][gpu]") {
    // Nothing downstream can be sanity-checked against a stopwatch if the unit is wrong, and the
    // unit is an assumption: gpu/timeline_math.cpp multiplies the raw delta by 1e-6 and calls the
    // result milliseconds. renderFrame() submits and waits for the queue, so its wall clock is an
    // upper bound on the GPU frame it contains and a loose lower bound on it for a GPU-bound one.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = makeRenderer(*ctx, shaders);
    if (!renderer->timeline().available()) {
        SKIP("timestamp queries unavailable on this adapter");
    }
    const scene::Scene s = overdrawScene(48);

    double bestWall = 1e9;
    double gpuAtBest = -1.0;
    for (int i = 0; i < 24; ++i) {
        FrameTime time{};
        time.renderTime = static_cast<double>(i) / 60.0;
        time.deltaTime = 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(i);
        const auto start = std::chrono::steady_clock::now();
        REQUIRE(renderer->renderFrame(s, time, 1024, 1024).has_value());
        const double wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (i >= 8 && wall < bestWall && renderer->stats().gpuFrameMs >= 0.0) {
            bestWall = wall;
            gpuAtBest = renderer->stats().gpuFrameMs;
        }
    }
    INFO("fastest submitted frame: wall " << bestWall << " ms, timeline " << gpuAtBest << " ms");
    REQUIRE(gpuAtBest >= 0.0);
    // The GPU frame is contained in the wall clock of a submit-and-wait, with a little slack for
    // the clocks not being the same clock. A microsecond or a tick-count unit would be out by
    // three orders of magnitude and fail both of these.
    CHECK(gpuAtBest < bestWall * 1.25);
    CHECK(gpuAtBest > bestWall * 0.10);

    // Raw timestamps, for the same reason: a frame's slots must ascend, and the span must be the
    // frame. An unwritten slot is a zero and is excluded, which is exactly what timelineIntervals
    // does with it.
    const auto& raw = renderer->timeline().timestamps();
    REQUIRE(raw.size() >= 2);
    std::uint64_t previous = raw[0];
    for (std::size_t i = 1; i < raw.size(); ++i) {
        if (raw[i] == 0) {
            continue;
        }
        CHECK(raw[i] >= previous);
        previous = raw[i];
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("the timestamp counter's resolution is small enough to measure a pass", "[profiler][gpu]") {
    // What the per-pass numbers can be trusted to. A tenth-of-a-millisecond pass reported by a
    // counter that ticks every 0.066 ms is one or two ticks of quantisation noise, and no A/B on
    // it means anything. This measures the floor rather than assuming one.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = makeRenderer(*ctx, shaders);
    if (!renderer->timeline().available()) {
        SKIP("timestamp queries unavailable on this adapter");
    }
    const scene::Scene s = geometryScene(16, 8);

    std::vector<std::uint64_t> gaps;
    std::uint64_t seen = renderer->timeline().completedFrames();
    for (int i = 0; i < 48; ++i) {
        FrameTime time{};
        time.renderTime = static_cast<double>(i) / 60.0;
        time.deltaTime = 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(i);
        REQUIRE(renderer->renderFrame(s, time, 256, 256).has_value());
        if (renderer->timeline().completedFrames() == seen || i < 8) {
            continue;
        }
        seen = renderer->timeline().completedFrames();
        const auto& raw = renderer->timeline().timestamps();
        for (std::size_t k = 1; k < raw.size(); ++k) {
            if (raw[k] != 0 && raw[k - 1] != 0 && raw[k] > raw[k - 1]) {
                gaps.push_back(raw[k] - raw[k - 1]);
            }
        }
    }
    REQUIRE(!gaps.empty());
    std::sort(gaps.begin(), gaps.end());
    // A quantised counter makes every reading a multiple of one tick; an unquantised one does not.
    // The greatest common divisor of a few hundred readings tells the two apart without assuming
    // which it is.
    const std::uint64_t quantum = std::accumulate(gaps.begin(), gaps.end(), std::uint64_t{0},
                                                  [](std::uint64_t a, std::uint64_t b) { return std::gcd(a, b); });
    INFO("smallest non-zero interval " << gaps.front() << " ns, gcd of " << gaps.size() << " intervals "
                                       << quantum << " ns, median " << gaps[gaps.size() / 2] << " ns");
    // Sub-millisecond passes are the ones at risk, so the floor has to be well under a millisecond
    // for any of the small labels to mean anything at all.
    CHECK(gaps.front() < 1'000'000ull);
    CHECK(quantum < 100'000ull);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("an empty frame is measured as an empty frame", "[profiler][gpu]") {
    // The zero end of the scale. If a frame with nothing in it reports the same as a frame with
    // something in it, every number above it is that much too large and the fixed overhead is
    // being charged to whatever pass happened to be first.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = makeRenderer(*ctx, shaders);
    if (!renderer->timeline().available()) {
        SKIP("timestamp queries unavailable on this adapter");
    }
    scene::Scene empty = baseScene();
    empty.lights.clear();
    const Profile nothing = profile(*renderer, empty, 768, 768);
    const Profile something = profile(*renderer, overdrawScene(48), 768, 768);
    REQUIRE(nothing.samples > 0);
    REQUIRE(something.samples > 0);
    INFO("empty " << nothing.frameMs << " ms:" << table(nothing) << "\nloaded " << something.frameMs
                  << " ms:" << table(something));
    CHECK(nothing.frameMs >= 0.0);
    // Relative, because an absolute bar measures the machine's other tenants: this frame reads
    // 3.7 ms on a quiet GPU and 10.4 ms with a second process on it.
    CHECK(nothing.frameMs < something.frameMs * 0.6);
    // Nothing was drawn, and the counters agree with the clock about that.
    CHECK(nothing.stats.geometry.total().triangles == 0ull);
    CHECK(nothing.stats.geometry.total().draws == 0u);
    CHECK(labelMs(nothing, "scene") < 0.5);
    // The property the whole instrument rests on: the intervals partition the frame, so per-label
    // medians land close to the frame median even though independent medians need not sum exactly.
    CHECK(std::abs(sumOf(nothing) - nothing.frameMs) < std::max(0.5, nothing.frameMs * 0.5));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("the scene pass responds to geometry", "[profiler][gpu]") {
    // The workload the whole renderer-2.0 brief turns on: Glowmere's scene pass is 85% of its frame
    // and does not move with resolution, so it is being charged for vertices. If the scene label
    // does not move when the vertices do, that conclusion is unsupported.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = makeRenderer(*ctx, shaders);
    if (!renderer->timeline().available()) {
        SKIP("timestamp queries unavailable on this adapter");
    }
    // Same draw count, same screen coverage, 1024x the triangles.
    const scene::Scene light = geometryScene(64, 2);
    const scene::Scene heavy = geometryScene(64, 64);

    const Profile lightProfile = profile(*renderer, light, 512, 512);
    const Profile heavyProfile = profile(*renderer, heavy, 512, 512);
    // The counter and the clock have to agree about which arm is bigger, or one of them is lying.
    INFO("light: " << lightProfile.stats.geometry.camera.triangles << " tris in "
                   << lightProfile.stats.geometry.camera.draws << " draws;"
                   << table(lightProfile) << "\nheavy: " << heavyProfile.stats.geometry.camera.triangles
                   << " tris in " << heavyProfile.stats.geometry.camera.draws << " draws;"
                   << table(heavyProfile));
    CHECK(lightProfile.stats.geometry.camera.draws == heavyProfile.stats.geometry.camera.draws);
    CHECK(heavyProfile.stats.geometry.camera.triangles > lightProfile.stats.geometry.camera.triangles * 100);

    const double lightMs = stableMs(*renderer, light, 512, 512, "scene");
    const double heavyMs = stableMs(*renderer, heavy, 512, 512, "scene");
    INFO("scene pass: light " << lightMs << " ms, heavy " << heavyMs << " ms");
    REQUIRE(lightMs >= 0.0);
    REQUIRE(heavyMs >= 0.0);
    CHECK(heavyMs > lightMs * 2.0);
}

TEST_CASE("the scene pass responds to overdraw", "[profiler][gpu]") {
    // The other half of the same question. A pass that moves with vertices and not with fragments
    // is measuring something real; one that moves with neither is measuring nothing.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = makeRenderer(*ctx, shaders);
    if (!renderer->timeline().available()) {
        SKIP("timestamp queries unavailable on this adapter");
    }
    const scene::Scene thin = overdrawScene(2);
    const scene::Scene thick = overdrawScene(64);

    const double thinMs = stableMs(*renderer, thin, 768, 768, "scene");
    const double thickMs = stableMs(*renderer, thick, 768, 768, "scene");
    INFO("scene pass at 768x768: 2 layers " << thinMs << " ms, 64 layers " << thickMs << " ms");
    REQUIRE(thinMs >= 0.0);
    REQUIRE(thickMs >= 0.0);
    // 32x the blended fullscreen coverage against 128 triangles either way.
    CHECK(thickMs > thinMs * 3.0);
}

TEST_CASE("the fullscreen passes respond to resolution", "[profiler][gpu]") {
    // The fragment-side check, and the one that decides whether dynamic resolution could ever pay:
    // a pass whose cost is pixels must scale with pixels. The volumetric march is a fullscreen
    // shader at half resolution and is the cleanest pixel-bound pass in the frame; GTAO is the same
    // shape and is reported alongside it because it does *not* scale nearly as well, which is a
    // result rather than a failure -- most of its cost at small targets is fixed.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = makeRenderer(*ctx, shaders);
    if (!renderer->timeline().available()) {
        SKIP("timestamp queries unavailable on this adapter");
    }
    scene::Scene s = geometryScene(32, 16);
    s.environment.volumeDensity = 0.05f;
    s.environment.volumeScattering = 0.6f;
    s.environment.volumeSteps = 64;
    s.environment.volumeMaxDistance = 180.0f;

    const Profile small = profile(*renderer, s, 512, 512, 40, 12);
    const Profile large = profile(*renderer, s, 2048, 2048, 40, 12);
    INFO("512x512:" << table(small) << "\n2048x2048:" << table(large));
    REQUIRE(hasLabel(small, "volume"));
    REQUIRE(hasLabel(large, "volume"));

    const double smallVolume = stableMs(*renderer, s, 512, 512, "volume");
    const double largeVolume = stableMs(*renderer, s, 2048, 2048, "volume");
    const double smallAo = stableMs(*renderer, s, 512, 512, "ao");
    const double largeAo = stableMs(*renderer, s, 2048, 2048, "ao");
    INFO("16x the pixels: volume " << smallVolume << " -> " << largeVolume << " ms (x"
                                   << (smallVolume > 0.0 ? largeVolume / smallVolume : 0.0) << "), ao "
                                   << smallAo << " -> " << largeAo << " ms (x"
                                   << (smallAo > 0.0 ? largeAo / smallAo : 0.0) << ")");
    REQUIRE(smallVolume > 0.0);
    CHECK(largeVolume > smallVolume * 4.0);
    // AO is reported, not pinned to a ratio: what has to hold is that it moves at all.
    CHECK(largeAo > smallAo);
}

TEST_CASE("the shadow passes respond to shadow workload", "[profiler][gpu]") {
    // Cascades are the shadow phase's own workload and nothing else in the frame changes with them:
    // the same casters are drawn into one depth map or into four.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = makeRenderer(*ctx, shaders);
    if (!renderer->timeline().available()) {
        SKIP("timestamp queries unavailable on this adapter");
    }
    scene::Scene one = geometryScene(200, 32, true);
    one.environment.shadowCascades = 1;
    scene::Scene four = one;
    four.environment.shadowCascades = 4;

    const Profile onePass = profile(*renderer, one, 512, 512);
    const Profile fourPass = profile(*renderer, four, 512, 512);
    INFO("1 cascade: " << onePass.stats.shadows.views << " views, "
                       << onePass.stats.geometry.shadow.triangles << " tris, "
                       << onePass.stats.shadowDraws << " draws;" << table(onePass)
                       << "\n4 cascades: " << fourPass.stats.shadows.views << " views, "
                       << fourPass.stats.geometry.shadow.triangles << " tris, "
                       << fourPass.stats.shadowDraws << " draws;" << table(fourPass));
    // The submitted-geometry counter has to see the extra work too, or the timing has nothing to
    // be checked against.
    CHECK(fourPass.stats.shadows.views > onePass.stats.shadows.views);
    CHECK(fourPass.stats.geometry.shadow.triangles > onePass.stats.geometry.shadow.triangles);
    CHECK(onePass.stats.shadowCasters > 0);

    const double oneMs = stableMs(*renderer, one, 512, 512, "shadow");
    const double fourMs = stableMs(*renderer, four, 512, 512, "shadow");
    INFO("shadow pass: 1 cascade " << oneMs << " ms, 4 cascades " << fourMs << " ms");
    REQUIRE(oneMs >= 0.0);
    REQUIRE(fourMs >= 0.0);
    CHECK(fourMs > oneMs * 1.8);
}

TEST_CASE("the volumetric pass responds to its march length", "[profiler][gpu]") {
    // The pass the brief suspected outright, kept in the battery because leaving it out would make
    // the table of what measures correctly incomplete. test_frame_timeline_gpu.cpp pins the same
    // property as a regression guard against the instrument this one replaced.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = makeRenderer(*ctx, shaders);
    if (!renderer->timeline().available()) {
        SKIP("timestamp queries unavailable on this adapter");
    }
    scene::Scene s = geometryScene(16, 8);
    s.environment.volumeDensity = 0.05f;
    s.environment.volumeScattering = 0.6f;
    s.environment.volumeMaxDistance = 180.0f;

    s.environment.volumeSteps = 16;
    const double lightMs = stableMs(*renderer, s, 512, 512, "volume");
    s.environment.volumeSteps = 256;
    const double heavyMs = stableMs(*renderer, s, 512, 512, "volume");
    INFO("volume pass: 16 steps " << lightMs << " ms, 256 steps " << heavyMs << " ms");
    REQUIRE(lightMs >= 0.0);
    REQUIRE(heavyMs >= 0.0);
    CHECK(heavyMs > lightMs * 2.0);
}

TEST_CASE("removing a phase is attributed across the whole frame, not just to its own label",
          "[profiler][gpu]") {
    // The measurement the Phase 0 audit could not explain: `--disable volume` saved 1.44 ms while
    // the volume pass reported 0.85. Because the intervals partition the frame, that saving cannot
    // have gone missing -- it is in the other labels, and this finds out which. The A/B is run in
    // one process against one renderer so the two arms cannot differ in anything else.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = makeRenderer(*ctx, shaders);
    if (!renderer->timeline().available()) {
        SKIP("timestamp queries unavailable on this adapter");
    }
    scene::Scene s = geometryScene(48, 16);
    s.environment.volumeDensity = 0.05f;
    s.environment.volumeScattering = 0.6f;
    s.environment.volumeSteps = 96;
    s.environment.volumeMaxDistance = 180.0f;

    const Profile with = profile(*renderer, s, 1024, 1024, 48, 12);
    rendering::SceneRenderer::PassToggles off;
    off.volume = false;
    renderer->setPassToggles(off);
    const Profile without = profile(*renderer, s, 1024, 1024, 48, 12);
    renderer->setPassToggles(rendering::SceneRenderer::PassToggles{});

    REQUIRE(with.samples > 0);
    REQUIRE(without.samples > 0);
    // The arm really is the arm: no volume label at all, not a volume label reading zero.
    CHECK(hasLabel(with, "volume"));
    CHECK_FALSE(hasLabel(without, "volume"));

    const auto attribution = rendering::attributeRemoval(with.passes, without.passes, "volume");
    std::string breakdown;
    for (const auto& e : attribution.byLabel) {
        breakdown += " " + e.label + "=" + std::to_string(e.ms).substr(0, 6);
    }
    INFO("frame " << with.frameMs << " -> " << without.frameMs << " ms; volume reported "
                  << attribution.removedPassMs << " ms; saved " << attribution.frameDeltaMs
                  << " ms; unattributed to the pass itself " << attribution.elsewhereMs
                  << " ms\nper-label delta:" << breakdown);
    // What is asserted is the bookkeeping, not the machine: the per-label deltas account for the
    // whole frame delta, so wherever the discrepancy is, it is visible above and not missing.
    double summed = 0.0;
    for (const auto& e : attribution.byLabel) {
        summed += e.ms;
    }
    CHECK(std::abs(summed - attribution.frameDeltaMs) < 1e-9);
    CHECK(attribution.removedPassMs > 0.0);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("submitted geometry is counted apart from the geometry the world contains",
          "[profiler][gpu]") {
    // The headline defect this phase exists to fix. Glowmere reported `tris=15154902` -- source
    // triangles times instance records, before LOD and before culling -- and nothing at all
    // reported what the frame drew, so no culling or LOD change could be shown to have done
    // anything. Here the scatter is placed so that most of it is behind the camera and beyond the
    // far LOD distance: the logical count must not move, and the submitted count must collapse.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = makeRenderer(*ctx, shaders);

    const scene::Scene direct = scatterScene(40, 40, false);
    const scene::Scene culled = scatterScene(40, 40, true);
    const Profile uncut = profile(*renderer, direct, 512, 512, 24, 6);
    const Profile cut = profile(*renderer, culled, 512, 512, 24, 6);

    const auto& a = uncut.stats;
    const auto& b = cut.stats;
    INFO("direct: logical " << a.geometry.logicalTriangles << ", submitted " << a.geometry.camera.triangles
                            << " over " << a.geometry.camera.draws << " draws ("
                            << a.geometry.camera.estimatedDraws << " estimated, "
                            << a.geometry.camera.unmeasuredDraws << " unmeasured)"
                            << "\nculled: logical " << b.geometry.logicalTriangles << ", submitted "
                            << b.geometry.camera.triangles << " over " << b.geometry.camera.draws
                            << " draws (" << b.geometry.camera.estimatedDraws << " estimated, "
                            << b.geometry.camera.unmeasuredDraws << " unmeasured), visible "
                            << b.visibleInstances << " culled " << b.culledInstances);

    // 1600 unit boxes of 12 triangles each: the content of the world, identical in both arms
    // because culling does not change what the world holds.
    CHECK(a.geometry.logicalTriangles == b.geometry.logicalTriangles);
    CHECK(a.geometry.logicalInstances == 1600ull);
    // Without culling every record is submitted, so submitted equals logical.
    CHECK(a.geometry.camera.triangles == a.geometry.logicalTriangles);
    CHECK(a.geometry.camera.estimatedDraws == 0u); // a direct draw's instance count is the CPU's own
    // With culling the cull pass decides, and it rejects most of a 80x80-metre field seen from one
    // corner of it. The pre-cull number is unchanged; the submitted one is a fraction of it.
    CHECK(b.culledInstances > 0ull);
    CHECK(b.geometry.camera.triangles < b.geometry.logicalTriangles);
    CHECK(b.geometry.camera.estimatedDraws > 0u); // and every indirect draw is flagged as stale
    // The counter that used to be reported as `tris` now carries the submitted figure.
    CHECK(static_cast<std::uint64_t>(b.triangles) == b.geometry.camera.triangles);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("the CPU stages account for the frame they were measured inside", "[profiler][gpu]") {
    // A breakdown whose parts do not add up to the whole is not a breakdown. The stages roll
    // across the whole of render(), so the residual is whatever falls after the last mark, and it
    // should be microseconds; a large one means a stage boundary is missing.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = makeRenderer(*ctx, shaders);
    scene::Scene s = geometryScene(64, 16, true);
    s.environment.volumeDensity = 0.03f;
    s.environment.volumeSteps = 32;

    const Profile p = profile(*renderer, s, 512, 512, 16, 4);
    const auto& cpu = p.stats.cpu;
    INFO("cpu total " << cpu.totalMs << " ms = uploads " << cpu.uploadsMs << " + lights " << cpu.lightsMs
                      << " + objects " << cpu.objectsMs << " + fields " << cpu.fieldsMs << " + sim "
                      << cpu.simulationMs << " + particles " << cpu.particlesMs << " + procedural "
                      << cpu.proceduralMs << " + sdf " << cpu.sdfMs << " + shadow " << cpu.shadowEncodeMs
                      << " + background " << cpu.backgroundEncodeMs << " + depth " << cpu.depthEncodeMs
                      << " + scene " << cpu.sceneEncodeMs << " + volume " << cpu.volumeEncodeMs
                      << " + post " << cpu.postEncodeMs << " + tonemap " << cpu.tonemapEncodeMs
                      << " + finish " << cpu.finishMs << " + submit " << cpu.submitMs << " + wait "
                      << cpu.queueWaitMs << "; unattributed " << cpu.unattributedMs());
    CHECK(cpu.totalMs > 0.0);
    CHECK(cpu.stagesMs() <= cpu.totalMs + 1e-6);
    CHECK(cpu.unattributedMs() < std::max(0.05, cpu.totalMs * 0.02));
    // The offline path waits for the queue, and that wait is the GPU frame. It is charged to its
    // own stage rather than being smeared over the encode stages, which is the whole point of
    // having it in the list.
    CHECK(cpu.queueWaitMs > 0.0);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("every pass of the frame is classified or counted as unclassified", "[profiler][gpu]") {
    // "How many render-target switches does this frame make" is a question the brief asks for, and
    // the honest answer here has a hole in it: only the scene and procedural renderers say which
    // kind of pass they are marking. What must not happen is the hole being papered over -- the
    // classified counts plus the unclassified count have to equal the passes the timeline saw.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = makeRenderer(*ctx, shaders);
    if (!renderer->timeline().available()) {
        SKIP("timestamp queries unavailable on this adapter");
    }
    scene::Scene s = geometryScene(32, 8, true);
    s.environment.volumeDensity = 0.03f;
    s.environment.volumeSteps = 32;
    const Profile p = profile(*renderer, s, 512, 512, 16, 4);

    const auto& st = p.stats;
    INFO("passes: " << st.state.renderPasses << " render + " << st.state.computePasses << " compute + "
                    << st.unclassifiedPasses << " unclassified; timeline measured " << st.gpuPasses
                    << "; unwritten " << p.unwritten);
    CHECK(st.state.renderPasses > 0u);
    CHECK(st.state.renderPasses + st.state.computePasses + st.unclassifiedPasses == st.gpuPasses);
    CHECK(ctx->errorCount() == 0);
}
