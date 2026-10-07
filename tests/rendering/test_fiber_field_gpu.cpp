// ADR-1180 (the Fiber source) and ADR-1181 (the Streamline deformer) on the GPU: the vertex stage
// builds the centre line scene::fiberCentreLine builds, and the pixel floor keeps a hair-thin fiber
// on screen. The CPU half is tests/unit/test_fiber_field.cpp.
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>
#include <glm/glm.hpp>

#include <algorithm>
#include <filesystem>
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

constexpr std::uint32_t kSize = 256;

// One upright fiber at the origin, emissive white on black, seen from +Z.
scene::Scene fiberScene(float steer, float width, float minPixels, bool withField = true) {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    s.camera.position = {0.0f, 0.0f, 8.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.camera.fovYRadians = 0.9f;
    if (withField) {
        spatial::FieldSpec flow;
        flow.name = "flow";
        flow.kind = spatial::FieldKind::Direction;
        flow.axis = {1.0f, 0.0f, 0.0f};
        s.fields.fields.push_back(flow);
    }
    scene::ProceduralGeometry g;
    g.name = "fiber";
    g.source.kind = scene::PrimitiveKind::Fiber;
    g.source.fiberLength = 3.0f;
    g.source.fiberWidth = width;
    g.source.fiberTaper = 1.0f;
    g.source.fiberSegments = 12;
    g.source.fiberMinPixels = minPixels;
    scene::Deformer d;
    d.kind = scene::DeformerKind::Streamline;
    d.space = scene::DeformSpace::World;
    d.field = "flow";
    d.amount = steer;
    g.deformers.push_back(d);
    scene::InstanceRecord r{};
    r.position = {-1.0f, -1.0f, 0.0f, 1.0f};
    r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    r.scale = {1.0f, 1.0f, 1.0f, 0.0f};
    r.random = {0.5f, 0.5f, 0.5f, 0.5f};
    r.color = {1.0f, 1.0f, 1.0f, 0.0f};
    r.emissive = {1.0f, 1.0f, 1.0f, 0.0f};
    g.instances = {r};
    g.structureVersion = 1;
    g.meshHash = g.source.structuralHash() | 1u;
    g.material.baseColor = {0.0f, 0.0f, 0.0f};
    g.material.emissiveColor = {1.0f, 1.0f, 1.0f};
    g.material.emissiveIntensity = 2.0f;
    g.material.roughness = 1.0f;
    s.procedurals.push_back(g);
    return s;
}

gpu::Image8 render(gpu::Context& ctx, const scene::Scene& s) {
    gpu::ShaderLibrary shaders(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FrameTime t{};
    auto img = renderer.renderToImage(s, t, kSize, kSize);
    REQUIRE(img.has_value());
    CHECK(ctx.errorCount() == 0);
    return *img;
}

int luma(const gpu::Image8& img, int x, int y) {
    if (x < 0 || y < 0 || x >= static_cast<int>(img.width) || y >= static_cast<int>(img.height)) {
        return 0;
    }
    const auto* p = img.pixel(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    return p[0] + p[1] + p[2];
}

// The brightest pixel within `r` of the projection of world point w.
int litNear(const gpu::Image8& img, const scene::Camera& cam, glm::vec3 w, int r = 3) {
    const glm::vec4 c = cam.projection(1.0f) * cam.view() * glm::vec4(w, 1.0f);
    const glm::vec2 ndc = glm::vec2(c) / c.w;
    const int px = static_cast<int>((ndc.x * 0.5f + 0.5f) * kSize);
    const int py = static_cast<int>((0.5f - ndc.y * 0.5f) * kSize);
    int best = 0;
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            best = std::max(best, luma(img, px + dx, py + dy));
        }
    }
    return best;
}

int litPixels(const gpu::Image8& img) {
    int n = 0;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            n += luma(img, static_cast<int>(x), static_cast<int>(y)) > 30 ? 1 : 0;
        }
    }
    return n;
}

} // namespace

TEST_CASE("a fiber's centre line on the GPU is the CPU reference's", "[gpu][fiber]") {
    auto ctx = makeContext();
    for (const float steer : {0.0f, 1.5f, 1000.0f}) {
        INFO("steer " << steer);
        const scene::Scene s = fiberScene(steer, 0.05f, 0.0f);
        const auto img = render(*ctx, s);
        const auto& g = s.procedurals[0];
        const auto pts = scene::fiberCentreLine(&g.deformers[0], {-1.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
                                                g.source.fiberLength, g.source.fiberSegments, 0.0, &s.fields);
        // Every point of the reference is lit, the tip included.
        for (std::size_t k = 0; k < pts.size(); ++k) {
            INFO("point " << k);
            CHECK(litNear(img, s.camera, pts[k]) > 60);
        }
        // And the control: the straight fiber's tip, which a bent fiber must not reach.
        if (steer > 0.0f) {
            CHECK(litNear(img, s.camera, {-1.0f, 2.0f, 0.0f}, 1) < 30);
        }
    }
}

TEST_CASE("a hair-thin fiber stays on screen, paid for in brightness", "[gpu][fiber]") {
    auto ctx = makeContext();
    // 0.2 mm at 8 m is a few hundredths of a pixel: without the floor it rasterises to (almost) nothing.
    const auto bare = render(*ctx, fiberScene(0.0f, 0.0002f, 0.0f));
    const auto held = render(*ctx, fiberScene(0.0f, 0.0002f, 1.0f));
    const int bareLit = litPixels(bare);
    const int heldLit = litPixels(held);
    CHECK(heldLit > 40);           // a continuous thread of ~1 pixel down the fiber's length
    CHECK(heldLit > 4 * bareLit);  // the floor is what keeps it
    // Coverage: the held fiber is dimmer than a fiber that really is a pixel wide.
    const auto wide = render(*ctx, fiberScene(0.0f, 0.02f, 1.0f));
    int heldPeak = 0;
    int widePeak = 0;
    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 0; x < kSize; ++x) {
            heldPeak = std::max(heldPeak, luma(held, static_cast<int>(x), static_cast<int>(y)));
            widePeak = std::max(widePeak, luma(wide, static_cast<int>(x), static_cast<int>(y)));
        }
    }
    CHECK(heldPeak < widePeak);
}

TEST_CASE("a streamline with no such field leaves the fiber straight", "[gpu][fiber]") {
    auto ctx = makeContext();
    const scene::Scene s = fiberScene(1000.0f, 0.05f, 0.0f, /*withField=*/false);
    const auto img = render(*ctx, s);
    CHECK(litNear(img, s.camera, {-1.0f, 2.0f, 0.0f}) > 60);
}
