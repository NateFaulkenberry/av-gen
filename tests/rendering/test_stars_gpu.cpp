// Stars (Effect Library Wave 2) on pixels: the effect reaches the sky, a disabled instance changes
// nothing, and the field is a function of the second.
//
// `AVGEN_EFFECT_DUMP=<dir>` writes each arm as a PNG, for judging the field by eye.

#include "app/engine.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "world/atmospherics.hpp"
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

void dump(const gpu::Image8& img, const std::string& stem) {
    const char* dir = std::getenv("AVGEN_EFFECT_DUMP");
    if (dir != nullptr && dir[0] != '\0') {
        static_cast<void>(assets::writePng(fs::path(dir) / (stem + ".png"), img.width, img.height, img.rgba));
    }
}

// A fresh engine looking up at the sky, with `style` Stars (or none; `disabled` adds it switched
// off), rendered at `seconds`.
gpu::Image8 run(gpu::Context& ctx, const char* style, double seconds, bool disabled = false) {
    gpu::ShaderLibrary shaders(ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    engine.setViewport(kWidth, kHeight);
    if (style != nullptr) {
        world::EffectInstance e = world::makeEffect(world::EffectKind::Stars, "Stars");
        e.id.clear();
        REQUIRE(world::applyEffectStyle(e, world::EffectKind::Stars, style));
        e.enabled = !disabled;
        // Seen against a dark sky whatever the default environment's brightness is.
        e.values.setFloat("stars/daylight", 0.0f);
        e.timing.fadeIn = 0.0;
        std::vector<world::EffectInstance> list;
        REQUIRE(world::insertEffect(list, e).has_value());
        REQUIRE(engine.setEffects(list).has_value());
    }
    const FrameTime ft{seconds, 1.0 / 60.0, static_cast<std::uint64_t>(seconds * 60.0)};
    engine.update(ft);
    scene::Scene scene = engine.scene();
    // A night sky standing behind the scene: a new composition draws the flat background colour.
    scene.environment.sky.enabled = true;
    scene.environment.sky.showBackground = true;
    scene.environment.sky.zenithColor = glm::vec3(0.004f, 0.006f, 0.016f);
    scene.environment.sky.horizonColor = glm::vec3(0.012f, 0.016f, 0.03f);
    // Look well above the horizon so the frame is mostly sky.
    scene.camera.target = scene.camera.position + glm::vec3(0.0f, 0.8f, -1.0f);
    auto img = renderer.renderToImage(scene, ft, kWidth, kHeight);
    REQUIRE(img.has_value());
    CHECK(ctx.errorCount() == 0);
    return std::move(*img);
}

} // namespace

TEST_CASE("Stars reach the sky, a disabled one changes nothing, and twinkle is a function of time",
          "[gpu][effects][stars]") {
    auto ctx = makeContext();
    const gpu::Image8 none = run(*ctx, nullptr, 2.0);
    dump(none, "stars-none");

    // The gate: an instance that is switched off leaves the frame byte-identical.
    const gpu::Image8 disabled = run(*ctx, "Deep Space", 2.0, /*disabled=*/true);
    CHECK(gpu::hashImage(disabled) == gpu::hashImage(none));

    const gpu::Image8 deep = run(*ctx, "Deep Space", 2.0);
    dump(deep, "stars-deep-space");
    const std::size_t changed = visiblyDifferent(none, deep);
    INFO("visibly changed pixels, Deep Space vs none: " << changed);
    CHECK(changed > 200);

    const gpu::Image8 clear = run(*ctx, "Clear Night", 2.0);
    dump(clear, "stars-clear-night");

    // Twinkle moves with the second, and the same second draws the same frame.
    const gpu::Image8 t1 = run(*ctx, "Twinkling Horizon", 2.0);
    const gpu::Image8 t2 = run(*ctx, "Twinkling Horizon", 2.5);
    const gpu::Image8 t1again = run(*ctx, "Twinkling Horizon", 2.0);
    dump(t1, "stars-twinkling-a");
    dump(t2, "stars-twinkling-b");
    CHECK(gpu::hashImage(t1) == gpu::hashImage(t1again));
    CHECK(gpu::hashImage(t1) != gpu::hashImage(t2));
}
