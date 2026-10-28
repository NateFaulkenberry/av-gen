// ADR-1813: GTAO takes its normals from the depth prepass, and long, thin triangles stop drawing fins.
//
// The canyon (ADR-1857/1859 on scene/strange-attractor) switched GTAO off because it drew dark fins along its long RTIN
// wall triangles. The mechanism: GTAO rebuilt each pixel's normal from depth differences, and on a long, thin, nearly
// edge-on triangle those differences mix two facets, so the normal flips between them and the horizon search reads the
// facet edge as an occluder. The lit pass shades with the interpolated vertex normal, which has no such edge.
//
// The fixture is that geometry at landscape scale: a wall 3 km long made of 5 m-tall strips, each one triangle pair along
// the whole length, with a few metres of undulating relief and smooth analytic vertex normals, seen along its length at a
// grazing angle. The occlusion debug view (white = unoccluded) is rendered with the prepass normals and with the
// `aodepthnormals` arm (the pre-ADR-1813 normals). A smooth wall should read nearly white; the fins are dark lines.
//
// `AVGEN_EFFECT_DUMP=<dir>` writes both views as PNGs.

#include "assets/image.hpp"
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 640;
constexpr std::uint32_t kHeight = 360;

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

void dump(const gpu::Image8& img, const std::string& stem) {
    const char* dir = std::getenv("AVGEN_EFFECT_DUMP");
    if (dir != nullptr && dir[0] != '\0') {
        static_cast<void>(assets::writePng(fs::path(dir) / (stem + ".png"), img.width, img.height, img.rgba));
    }
}

constexpr float kStripHeight = 5.0f;
constexpr int kStrips = 40;
constexpr float kLength = 3000.0f;
// x(y): a few metres of relief over the wall's height, as a canyon wall has, sampled once per strip.
float wallX(float y) {
    return 3.0f * std::sin(y * 0.26f) + 1.8f * std::sin(y * 0.093f + 1.0f);
}
float wallSlope(float y) {
    return 3.0f * 0.26f * std::cos(y * 0.26f) + 1.8f * 0.093f * std::cos(y * 0.093f + 1.0f);
}

scene::MeshData wallMesh() {
    scene::MeshData m;
    for (int i = 0; i <= kStrips; ++i) {
        const float y = static_cast<float>(i) * kStripHeight;
        // the face looks along -x; its normal tilts with the profile's slope
        const glm::vec3 n = glm::normalize(glm::vec3(-1.0f, wallSlope(y), 0.0f));
        for (const float z : {0.0f, -kLength}) {
            scene::Vertex v{};
            v.position = glm::vec3(wallX(y), y, z);
            v.normal = n;
            v.uv = glm::vec2(z / kLength, y / kStrips);
            m.vertices.push_back(v);
        }
    }
    for (int i = 0; i < kStrips; ++i) {
        const std::uint32_t a = static_cast<std::uint32_t>(i) * 2;
        // two triangles per strip, each 240 m long and 1 m tall
        m.indices.insert(m.indices.end(), {a, a + 2, a + 1, a + 1, a + 2, a + 3});
    }
    return m;
}

scene::Scene wallScene() {
    scene::Scene s;
    s.environment.backgroundColor = {0.5f, 0.6f, 0.7f};
    s.environment.showSkybox = false;
    s.environment.ambientOcclusion = true;
    s.camera.position = {-60.0f, 100.0f, 40.0f};
    s.camera.target = {0.0f, 95.0f, -500.0f};
    s.camera.farPlane = 5000.0f;
    const scene::MeshId mesh = s.addMesh(wallMesh());
    scene::Entity& wall = s.addEntity("wall", mesh);
    wall.material.baseColor = {0.7f, 0.6f, 0.5f};
    wall.material.roughness = 0.8f;
    wall.material.doubleSided = true;
    scene::PunctualLight sun;
    sun.direction = glm::normalize(glm::vec3(1.0f, -1.0f, -0.3f));
    sun.intensity = 2.0f;
    s.addLight(sun);
    return s;
}

struct Occlusion {
    double meanDarkness = 0.0; // 1 - visibility, averaged over the wall's pixels
    int darkPixels = 0;        // wall pixels below 75% visibility
    int wallPixels = 0;
};

// The occlusion view over the wall's pixels (the background is drawn as unoccluded white in this view, so a pixel
// is "wall" where the plain frame differs from the background colour).
Occlusion measure(const gpu::Image8& occlusion, const gpu::Image8& frame) {
    Occlusion o;
    const gpu::Image8& bg = frame;
    const auto* corner = bg.pixel(0, 0);
    for (std::uint32_t y = 0; y < occlusion.height; ++y) {
        for (std::uint32_t x = 0; x < occlusion.width; ++x) {
            const auto* f = frame.pixel(x, y);
            const bool wall = std::abs(f[0] - corner[0]) + std::abs(f[1] - corner[1]) + std::abs(f[2] - corner[2]) > 6;
            if (!wall) {
                continue;
            }
            const double v = occlusion.pixel(x, y)[0] / 255.0;
            ++o.wallPixels;
            o.meanDarkness += 1.0 - v;
            o.darkPixels += v < 0.75 ? 1 : 0;
        }
    }
    o.meanDarkness /= std::max(o.wallPixels, 1);
    return o;
}

} // namespace

TEST_CASE("GTAO's prepass normals do not draw fins along long, thin triangles (ADR-1813)", "[gpu][ao][adr1813]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    renderer.setQuality(rendering::QualityTier::Offline);
    const scene::Scene s = wallScene();
    FrameTime t{};
    t.renderTime = 1.0;

    auto frame = renderer.renderToImage(s, t, kWidth, kHeight);
    REQUIRE(frame.has_value());
    renderer.setAuxDebugView(rendering::AuxDebugView::Occlusion);
    auto withNormals = renderer.renderToImage(s, t, kWidth, kHeight);
    REQUIRE(withNormals.has_value());
    rendering::QualitySettings q = renderer.qualitySettings();
    q.aoDepthNormals = true; // the arm: the picture before ADR-1813
    renderer.setQualitySettings(q);
    auto fromDepth = renderer.renderToImage(s, t, kWidth, kHeight);
    REQUIRE(fromDepth.has_value());
    renderer.setAuxDebugView(rendering::AuxDebugView::None);
    auto frameBefore = renderer.renderToImage(s, t, kWidth, kHeight);
    REQUIRE(frameBefore.has_value());
    dump(*frame, "ao-wall-lit-AFTER");
    dump(*frameBefore, "ao-wall-lit-BEFORE");
    dump(*withNormals, "ao-wall-occlusion-AFTER-prepass-normals");
    dump(*fromDepth, "ao-wall-occlusion-BEFORE-depth-normals");

    const Occlusion before = measure(*fromDepth, *frame);
    const Occlusion after = measure(*withNormals, *frame);
    INFO("wall pixels " << after.wallPixels << "; mean darkness depth-normals " << before.meanDarkness
                        << ", prepass normals " << after.meanDarkness << "; pixels under 75% visibility "
                        << before.darkPixels << " -> " << after.darkPixels);
    REQUIRE(after.wallPixels > 20000);
    CHECK(ctx->errorCount() == 0);
    // Measured when this was written: mean darkness 0.024 -> 0.013, pixels under 75% visibility 392 -> 73. The
    // remaining darkness is the wall's real relief (it has a few metres of it) plus a few strip-edge pixels where the
    // prepass normal is not the visible surface's and GTAO falls back.
    CHECK(after.meanDarkness < 0.7 * before.meanDarkness);
    CHECK(after.darkPixels * 3 < before.darkPixels);
}
