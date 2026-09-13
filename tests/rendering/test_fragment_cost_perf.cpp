// Phase A of the renderer professionalization plan: what is the scene pass actually spending its
// fragment time on?  Hidden behind `[.perf]` so it never runs in CI.
//
//   avgen_render_tests "[.perf][fragment]"
//
// **The question.** Glowmere's scene pass is 84% of its GPU frame and is fragment-bound: the same
// 430k triangles cost 0.26 ms through the depth-only shader and 15.73 ms through the scene shader.
// But eight times the pixels buys only 2.1x the time, so roughly 56% of that cost does not scale
// with resolution at all. Two explanations fit, and they imply different architectures:
//
//   (a) **Quad overdraw.** A triangle smaller than a pixel still forces a 2x2 quad of fragment
//       invocations, so invocations track *triangle count* rather than pixel count -- which is
//       exactly a cost that is fixed under a resolution change. The fix is representation:
//       fewer, larger triangles at distance (LOD, HLOD, impostors).
//
//   (b) **Expensive per-invocation shading.** Invocations track pixels, and each one is simply
//       costly (clustered lights, five attachments, procedural materials). The fix is per-pixel
//       cost: material tiers, cheaper light evaluation, fewer attachment writes.
//
// Apple's overdraw counter (fragment-shader invocations / pixels stored) would distinguish these in
// one GPU capture, but this repository does not use Xcode tooling, so the question is answered from
// inside the engine instead. That is the better instrument anyway: it is reproducible, it is
// committed, and it runs on demand rather than living in somebody's screenshot.
//
// **The experiment.** Hold the covered pixels *constant* and vary only how many triangles cover
// them. A single plane fills the viewport and is tessellated into N triangles; N sweeps across five
// orders of magnitude. Coverage never changes, so:
//
//   - if the scene pass is flat in N, invocations track pixels: explanation (b);
//   - if it climbs once triangles fall below a few pixels of area, invocations track triangles:
//     explanation (a), and the knee says where the threshold is.
//
// The control is the same sweep rendered through the depth-only pipeline, which pays the same
// vertex and binning cost with no fragment work. Subtracting it separates geometry cost from
// fragment cost without assuming either.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <vector>

using namespace avgen;

namespace {

constexpr std::uint32_t kWidth = 1280;
constexpr std::uint32_t kHeight = 800;
constexpr int kWarmup = 15;
constexpr int kMeasured = 40;

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available");
    }
    return std::move(*ctx);
}

double median(std::vector<double> v) {
    if (v.empty()) {
        return -1.0;
    }
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// A plane tessellated into `rows * cols * 2` triangles, sized and placed so it exactly fills the
// viewport from the camera below. Coverage is therefore identical for every tessellation, which is
// the whole point: the only variable is triangle size.
scene::MeshData tessellatedPlane(std::uint32_t rows, std::uint32_t cols, float halfWidth, float halfHeight) {
    scene::MeshData mesh;
    mesh.name = "plane";
    for (std::uint32_t r = 0; r <= rows; ++r) {
        for (std::uint32_t c = 0; c <= cols; ++c) {
            const float u = static_cast<float>(c) / static_cast<float>(cols);
            const float v = static_cast<float>(r) / static_cast<float>(rows);
            scene::Vertex vert{};
            vert.position = {(u * 2.0f - 1.0f) * halfWidth, (v * 2.0f - 1.0f) * halfHeight, 0.0f};
            vert.normal = {0.0f, 0.0f, 1.0f};
            vert.uv = {u, v};
            mesh.vertices.push_back(vert);
        }
    }
    const std::uint32_t stride = cols + 1;
    for (std::uint32_t r = 0; r < rows; ++r) {
        for (std::uint32_t c = 0; c < cols; ++c) {
            const std::uint32_t i0 = r * stride + c;
            const std::uint32_t i1 = i0 + 1;
            const std::uint32_t i2 = i0 + stride;
            const std::uint32_t i3 = i2 + 1;
            // Counter-clockwise as seen from +z, i.e. facing the camera. The first version of
            // this wound the other way and the whole plane was backface-culled in the scene pass --
            // which produced a perfectly flat, perfectly wrong result at every tessellation.
            mesh.indices.insert(mesh.indices.end(), {i0, i1, i2, i1, i3, i2});
        }
    }
    return mesh;
}

struct Sample {
    std::uint64_t triangles = 0;
    std::uint64_t submittedTris = 0;
    std::uint32_t drawCalls = 0;
    double sceneMs = -1.0;
    double depthMs = -1.0;
};

FrameTime frameAt(std::uint64_t index) {
    FrameTime t;
    t.frameIndex = index;
    t.renderTime = static_cast<double>(index) / 60.0;
    t.deltaTime = 1.0 / 60.0;
    return t;
}

Sample measureAt(gpu::Context& ctx, gpu::ShaderLibrary& shaders, std::uint32_t side) {
    // The plane sits at z = 0 facing +z; the camera is pulled back far enough that a half-extent of
    // 1 exactly fills the vertical field of view, and the horizontal half-extent follows the aspect.
    constexpr float kFov = 0.87f;
    constexpr float kDistance = 2.0f;
    const float halfHeight = std::tan(kFov * 0.5f) * kDistance;
    const float halfWidth = halfHeight * static_cast<float>(kWidth) / static_cast<float>(kHeight);

    scene::Scene s;
    const auto mesh = s.addMesh(tessellatedPlane(side, side, halfWidth * 1.02f, halfHeight * 1.02f));
    scene::Entity& e = s.addEntity("plane", mesh);
    e.material.baseColor = {0.55f, 0.5f, 0.45f};
    e.material.roughness = 0.6f;
    s.camera.position = {0.0f, 0.0f, kDistance};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.camera.fovYRadians = kFov;
    s.camera.lens.useExplicitFov = true;
    scene::PunctualLight& key = s.lights.emplace_back();
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.3f, -0.6f, -0.7f));
    key.intensity = 3.0f;

    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());

    std::vector<double> scene;
    std::vector<double> depth;
    for (int i = 0; i < kWarmup + kMeasured; ++i) {
        auto image = renderer.renderToImage(s, frameAt(static_cast<std::uint64_t>(i)), kWidth, kHeight);
        REQUIRE(image.has_value());
        if (i < kWarmup) {
            continue;
        }
        const double sceneMs = renderer.timeline().msFor("scene");
        const double depthMs = renderer.timeline().msFor("depth");
        if (sceneMs >= 0.0) {
            scene.push_back(sceneMs);
        }
        if (depthMs >= 0.0) {
            depth.push_back(depthMs);
        }
    }
    // Proof the plane is actually on screen: without this the sweep silently measures an empty
    // frame, which is exactly what a flat result would look like.
    const rendering::RenderStats& st = renderer.stats();
    Sample out;
    out.drawCalls = st.drawCalls;
    out.submittedTris = st.geometry.camera.triangles;
    out.triangles = static_cast<std::uint64_t>(side) * side * 2ull;
    out.sceneMs = median(scene);
    out.depthMs = median(depth);
    return out;
}

} // namespace

TEST_CASE("scene-pass cost against triangle density at constant coverage", "[.perf][fragment]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});

    const double pixels = static_cast<double>(kWidth) * kHeight;
    std::printf("\n%ux%u, one full-screen plane, coverage constant, tessellation swept\n", kWidth, kHeight);
    std::printf("  %10s  %12s  %10s  %10s  %8s %8s\n", "triangles", "px/triangle", "scene ms", "depth ms",
                "draws", "submitted");

    // Sides chosen so triangle area crosses the quad-overdraw threshold (4 px) in the middle of the
    // sweep: at 1.02 Mpx, 4 px per triangle is ~256k triangles, i.e. side ~358.
    //
    // Sides 2, 4 and 16 (8, 32 and 512 triangles) fill the sparse region to the RIGHT of the
    // minimum, which ADR-124 flagged as its weakest evidence: the rise there rested on a single
    // point, and a ceiling resting on one sample is a guess with a number attached. Note ADR-124
    // overstates the gap as "no samples at all" between 500 and 512,000 px/triangle -- side 8 sits
    // inside it at 8,000 -- and its quoted px/triangle for these arms are computed from half the
    // real pixel count. The gap is real; it was one point wide, not empty.
    for (const std::uint32_t side : {1u, 2u, 4u, 8u, 16u, 32u, 90u, 180u, 256u, 360u, 512u, 720u, 1024u}) {
        const Sample s = measureAt(*ctx, shaders, side);
        const double perTriangle = pixels / static_cast<double>(s.triangles);
        std::printf("  %10llu  %12.3f  %10.3f  %10.3f  %8u %8llu\n",
                    static_cast<unsigned long long>(s.triangles), perTriangle, s.sceneMs, s.depthMs,
                    s.drawCalls, static_cast<unsigned long long>(s.submittedTris));
    }
    std::printf("\n  A flat 'scene-depth' column means invocations track pixels: per-invocation cost\n"
                "  dominates and representation work would not help. A column that climbs as\n"
                "  px/triangle falls below ~4 means invocations track triangles: quad overdraw.\n");
    CHECK(ctx->errorCount() == 0);
}
