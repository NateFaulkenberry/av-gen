// ADR-1820: glossy screen-space reflections. A glossy floor reflects an emissive block standing on it when the scene asks
// for glossy reflections and the tier allows them; a scene that does not ask, or a tier that does not allow them, draws
// exactly the frame it drew before (the same hash); the reflection is the same every run (no jitter, no history).
//
// `AVGEN_EFFECT_DUMP=<dir>` writes the frames as PNGs.

#include "assets/image.hpp"
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
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

constexpr std::uint32_t kWidth = 480;
constexpr std::uint32_t kHeight = 270;

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

// A dark glossy floor, a glowing block standing on it, a dim procedural sky for the environment reflection.
scene::Scene lab(bool asks) {
    scene::Scene s;
    s.environment.backgroundColor = {0.01f, 0.01f, 0.015f};
    s.environment.showSkybox = false;
    s.environment.sky.enabled = true;
    s.environment.sky.showBackground = false;
    s.environment.sky.zenithColor = {0.01f, 0.015f, 0.03f};
    s.environment.sky.horizonColor = {0.03f, 0.04f, 0.06f};
    s.environment.environmentIntensity = 1.0f;
    s.environment.glossyReflections = asks;
    s.camera.position = {0.0f, 1.2f, 4.0f};
    s.camera.target = {0.0f, 0.4f, -2.0f};
    scene::PunctualLight sun;
    sun.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.6f));
    sun.intensity = 1.0f;
    s.addLight(sun);
    scene::Entity& floor = s.addEntity("floor", s.addMesh(scene::makePlane(8.0f, 1)));
    floor.material.baseColor = {0.03f, 0.04f, 0.05f};
    floor.material.metallic = 0.0f;
    floor.material.roughness = 0.05f;
    scene::Entity& block = s.addEntity("block", s.addMesh(scene::makeCube(0.5f)));
    block.transform.position = {0.0f, 0.5f, -2.0f};
    block.material.baseColor = {0.0f, 0.0f, 0.0f};
    block.material.roughness = 0.9f; // not glossy: only the floor reflects
    block.material.emissiveColor = {1.0f, 0.5f, 0.1f};
    block.material.emissiveIntensity = 6.0f;
    return s;
}

gpu::Image8 render(gpu::Context& ctx, gpu::ShaderLibrary& shaders, const scene::Scene& s, bool glossySsr = true) {
    rendering::SceneRenderer r(ctx, shaders);
    REQUIRE(r.init().has_value());
    rendering::QualitySettings q = r.qualitySettings();
    q.glossySsr = glossySsr;
    r.setQualitySettings(q);
    FrameTime t{};
    t.renderTime = 1.0;
    auto img = r.renderToImage(s, t, kWidth, kHeight);
    REQUIRE(img.has_value());
    return std::move(*img);
}

double luma(const gpu::Image8& img, std::uint32_t x, std::uint32_t y) {
    const auto* p = img.pixel(x, y);
    return 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
}

} // namespace

TEST_CASE("A glossy floor reflects the glowing block in front of it, only when the scene asks",
          "[gpu][ssr][adr1820]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    const scene::Scene asks = lab(true);
    const scene::Scene plain = lab(false);

    const gpu::Image8 on = render(*ctx, shaders, asks);
    const gpu::Image8 again = render(*ctx, shaders, asks);
    const gpu::Image8 lever = render(*ctx, shaders, asks, /*glossySsr=*/false);
    const gpu::Image8 before = render(*ctx, shaders, plain);
    dump(on, "ssr-on");
    dump(before, "ssr-plain");

    // A scene that does not ask, and the tier's lever off, are the same frame exactly.
    CHECK(gpu::hashImage(lever) == gpu::hashImage(before));
    // The reflection is the same every run.
    CHECK(gpu::hashImage(on) == gpu::hashImage(again));

    // The block's mirror image: below the floor's horizon, pixels that brighten with an orange cast.
    int reflected = 0;
    int above = 0;
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            const double gain = luma(on, x, y) - luma(before, x, y);
            if (gain > 20.0 && on.pixel(x, y)[0] > on.pixel(x, y)[2] + 20) {
                ++reflected;
                if (y < kHeight / 2) {
                    ++above;
                }
            }
        }
    }
    INFO("reflected pixels " << reflected << " (" << above << " in the upper half)");
    CHECK(reflected > 400);
    CHECK(above < reflected / 4); // the image lies on the floor, under the block
    CHECK(ctx->errorCount() == 0);
}
