// EMIT (ADR-703) on pixels: each Particle Emitter look reaches the frame through the engine's own
// particle renderer, off is nothing, and a run is reproducible.
//
// `AVGEN_EFFECT_DUMP=<dir>` writes each look's frame as a PNG so the looks can be judged by eye --
// which an art-facing effect has to be, and a pixel count cannot do.

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

constexpr std::uint32_t kWidth = 256;
constexpr std::uint32_t kHeight = 144;

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

// Runs a fresh engine and renderer for `frames` at 60 Hz with one World-owned emitter in `style`
// (or none), the emitter centred where the new composition's camera looks, and returns the last frame.
gpu::Image8 run(gpu::Context& ctx, const char* style, int frames = 120) {
    gpu::ShaderLibrary shaders(ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    engine.setViewport(kWidth, kHeight);
    if (style != nullptr) {
        world::EffectInstance e = world::makeEffect(world::EffectKind::ParticleEmitter, "Particles");
        e.id.clear();
        REQUIRE(world::applyEffectStyle(e, world::EffectKind::ParticleEmitter, style));
        std::vector<world::EffectInstance> list;
        REQUIRE(world::insertEffect(list, e).has_value());
        REQUIRE(engine.setEffects(list).has_value());
        const std::string id = engine.effects()[0].id;
        // Placed at the camera's target and sized to its distance, so every look is in shot
        // whatever a new composition's camera is: a 2.5 m view wants a half-metre swarm.
        engine.update(FrameTime{0.0, 1.0 / 60.0, 0});
        const scene::Camera cam = engine.scene().camera;
        const float d = glm::length(cam.target - cam.position);
        const auto set = [&](const char* leaf, float v) {
            auto* p = engine.params().find(world::effectParameterPrefix(id) + leaf);
            REQUIRE(p != nullptr);
            p->setBaseComponent(0, v);
        };
        set("centerX", cam.target.x);
        set("centerY", cam.target.y - d * 0.15f);
        set("centerZ", cam.target.z);
        set("radius", d * 0.18f);
        set("size", d * 0.012f);
        set("speed", d * 0.12f);
        set("rise", std::string(style).find("Ember") != std::string::npos ? d * 0.25f : 0.0f);
        set("rate", 400.0f);
    }
    gpu::Image8 last;
    for (int i = 0; i <= frames; ++i) {
        const double t = i / 60.0;
        const FrameTime ft{t, 1.0 / 60.0, static_cast<std::uint64_t>(i)};
        engine.update(ft);
        auto img = renderer.renderToImage(engine.scene(), ft, kWidth, kHeight);
        REQUIRE(img.has_value());
        last = std::move(*img);
    }
    CHECK(ctx.errorCount() == 0);
    return last;
}

} // namespace

TEST_CASE("every Particle Emitter look reaches the frame, and a run reproduces", "[gpu][effects][emit]") {
    auto ctx = makeContext();
    const gpu::Image8 off = run(*ctx, nullptr);
    dump(off, "emitter-off");
    for (const char* style : {"Meadow Fireflies", "Campfire Embers", "Arcane Swirl"}) {
        INFO(style);
        const gpu::Image8 on = run(*ctx, style);
        dump(on, std::string("emitter-") + style);
        const std::size_t changed = visiblyDifferent(off, on);
        INFO("visibly changed pixels: " << changed);
        CHECK(changed > 60);
    }
    // Reproducible: the seed is the instance id's and the simulation is on the fixed clock, so two
    // fresh engines and renderers draw the same frame.
    const gpu::Image8 a = run(*ctx, "Campfire Embers", 60);
    const gpu::Image8 b = run(*ctx, "Campfire Embers", 60);
    CHECK(gpu::hashImage(a) == gpu::hashImage(b));
}
