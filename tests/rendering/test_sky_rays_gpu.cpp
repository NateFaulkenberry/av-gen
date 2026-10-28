// ADR-1807: the sky's view ray is exact at any far plane and any distance from the origin.
//
// The sky is at infinity: it depends on where the camera LOOKS, never on where it STANDS or on how
// far its far plane is. So a camera 20 km from the origin with a 1e6 m far plane must draw the same
// star field, pixel for pixel, as one at the origin with a 2 km far plane, looking the same way.
//
// Before ADR-1807 the skybox built its ray by unprojecting two points through `invViewProj` in f32.
// The audit's model (docs/research/render-audit/scripts/f32_ray_error.py on research/render-audit)
// puts that ray 10.6 px wrong at the median and 39 px at p99 in exactly this stress case, and a star
// field is the content that shows it: every star lands somewhere else. Measured when this test was
// written (960x540, 423 star pixels): with the old unprojection the two frames differed visibly on
// 6,910 pixels; with the camera-basis ray, on 0.
//
// `AVGEN_EFFECT_DUMP=<dir>` writes both frames as PNGs.

#include "app/engine.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 960;
constexpr std::uint32_t kHeight = 540;

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

std::size_t visiblyDifferent(const gpu::Image8& a, const gpu::Image8& b) {
    std::size_t n = 0;
    for (std::size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        int sum = 0;
        for (int c = 0; c < 3; ++c) {
            sum += std::abs(static_cast<int>(a.rgba[i + c]) - static_cast<int>(b.rgba[i + c]));
        }
        n += sum > 24 ? 1u : 0u;
    }
    return n;
}

std::size_t litPixels(const gpu::Image8& a) {
    std::size_t n = 0;
    for (std::size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        n += (a.rgba[i] + a.rgba[i + 1] + a.rgba[i + 2]) > 240 ? 1u : 0u; // the sky itself is about 80
    }
    return n;
}

void dump(const gpu::Image8& img, const std::string& stem) {
    const char* dir = std::getenv("AVGEN_EFFECT_DUMP");
    if (dir != nullptr && dir[0] != '\0') {
        static_cast<void>(assets::writePng(fs::path(dir) / (stem + ".png"), img.width, img.height, img.rgba));
    }
}

struct SkyCamera {
    glm::vec3 position;
    float nearPlane;
    float farPlane;
};

// A night sky with the Deep Space star field, looked at from `cam` in a fixed direction.
gpu::Image8 run(gpu::Context& ctx, const SkyCamera& cam) {
    gpu::ShaderLibrary shaders(ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    engine.setViewport(kWidth, kHeight);
    world::EffectInstance e = world::makeEffect(world::EffectKind::Stars, "Stars");
    e.id.clear();
    REQUIRE(world::applyEffectStyle(e, world::EffectKind::Stars, "Deep Space"));
    e.values.setFloat("stars/daylight", 0.0f);
    e.timing.fadeIn = 0.0;
    std::vector<world::EffectInstance> list;
    REQUIRE(world::insertEffect(list, e).has_value());
    REQUIRE(engine.setEffects(list).has_value());
    const FrameTime ft{2.0, 1.0 / 60.0, 120};
    engine.update(ft);
    scene::Scene scene = engine.scene();
    scene.environment.sky.enabled = true;
    scene.environment.sky.showBackground = true;
    scene.environment.sky.zenithColor = glm::vec3(0.004f, 0.006f, 0.016f);
    scene.environment.sky.horizonColor = glm::vec3(0.012f, 0.016f, 0.03f);
    // Well above the horizon (the frame's bottom edge is still 10+ degrees up), so the frame is all
    // sky and nothing at a finite distance can tell the two cameras apart.
    scene.camera.position = cam.position;
    // The target 10 km down the view: `target - position` is how the camera finds its forward axis, and with a
    // target 1 m away from a camera 20 km out that subtraction alone is off by a milliradian in f32 -- a camera
    // error, not the sky's, and one this test must not charge to the shader.
    scene.camera.target = cam.position + 10000.0f * glm::vec3(0.35f, 0.8f, -1.0f);
    scene.camera.up = glm::vec3(0.0f, 1.0f, 0.0f);
    scene.camera.nearPlane = cam.nearPlane;
    scene.camera.farPlane = cam.farPlane;
    auto img = renderer.renderToImage(scene, ft, kWidth, kHeight);
    REQUIRE(img.has_value());
    CHECK(ctx.errorCount() == 0);
    return std::move(*img);
}

} // namespace

TEST_CASE("The sky's view ray depends on where the camera looks, not where it stands or its far plane",
          "[gpu][sky][stars]") {
    auto ctx = makeContext();
    // Today's cameras: the default free camera's planes, at the origin.
    const gpu::Image8 home = run(*ctx, SkyCamera{glm::vec3(0.0f, 2.0f, 0.0f), 0.1f, 2000.0f});
    // The audit's stress case: 20 km out, near 0.05, far 1e6. Same height, so nothing that
    // legitimately depends on altitude can differ.
    const gpu::Image8 far = run(*ctx, SkyCamera{glm::vec3(20000.0f, 2.0f, 8000.0f), 0.05f, 1.0e6f});
    dump(home, "sky-rays-home");
    dump(far, "sky-rays-far");

    // The frame has stars in it, or this proves nothing.
    const std::size_t stars = litPixels(home);
    INFO("lit (star) pixels in the home frame: " << stars);
    REQUIRE(stars > 200);

    const std::size_t changed = visiblyDifferent(home, far);
    INFO("visibly different pixels between the home and the far camera: " << changed);
    // A rounding difference in the camera basis between the two view matrices may move a star edge
    // by a fraction of a pixel; a wrong ray moves whole stars by pixels (6,910 with the old ray).
    CHECK(changed <= 20);
}
