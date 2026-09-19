// ADR-370: leaf cards, the measured canopy emitter, and particles that read the wind.
//
// Three properties, each with a control that can fail the other way:
//   1. A Leaf system does not draw what a Round one draws -- and a Round one is unchanged.
//   2. `windInfluence = 0` is byte-identical however hard the world's wind blows.
//   3. `windInfluence > 0` moves the particles, and reversing the wind's DIRECTION moves them
//      differently again, which is the brief's acceptance criterion and not the same claim as
//      "responds to speed".

#include "core/log.hpp"
#include "core/wind.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"
#include "support/image_diff.hpp"

#include <catch2/catch_test_macros.hpp>

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

wind::WindParams gale(float direction) {
    wind::WindParams w;
    w.enabled = true;
    w.speed = 3.0f;
    w.direction = direction;
    w.gustAmount = 1.2f;
    w.gustSpeed = 10.0f;
    return w;
}

scene::Scene shedding(scene::ParticleShape shape, float windInfluence, const wind::WindParams& w) {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.wind = w;
    s.camera.position = {0.0f, 0.0f, 26.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};

    scene::ParticleSystem p;
    p.name = "leaves";
    p.capacity = 2048;
    p.seed = 7717;
    p.shape = scene::EmitterShape::Box;
    p.position = {0.0f, 6.0f, 0.0f};
    p.extent = {5.0f, 2.0f, 5.0f};
    p.spawnRate = 400.0f;
    p.lifetimeMin = 6.0f;
    p.lifetimeMax = 6.0f;
    p.speedMin = 0.5f;
    p.speedMax = 1.5f;
    p.gravity = {0.0f, -3.0f, 0.0f};
    p.drag = 0.2f;
    p.turbulence = 0.0f;
    p.sizeStart = 0.5f;
    p.sizeEnd = 0.5f;
    p.colorStart = {0.4f, 0.8f, 0.4f, 1.0f};
    p.colorEnd = {0.4f, 0.8f, 0.4f, 1.0f};
    p.emissive = 2.0f;
    p.blend = scene::ParticleBlend::Alpha;
    p.shape2d = shape;
    p.windInfluence = windInfluence;
    s.particles.push_back(std::move(p));
    return s;
}

gpu::Image8 run(gpu::Context& ctx, gpu::ShaderLibrary& shaders, const scene::Scene& s, int frames = 60) {
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FixedStepClock clock(60.0);
    gpu::Image8 last;
    for (int i = 0; i < frames; ++i) {
        auto img = renderer.renderToImage(s, clock.tick(), 128, 128);
        REQUIRE(img.has_value());
        last = std::move(*img);
    }
    return last;
}

long lit(const gpu::Image8& img) {
    long n = 0;
    for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
        if (img.rgba[i] + img.rgba[i + 1] + img.rgba[i + 2] > 24) {
            ++n;
        }
    }
    return n;
}

} // namespace

TEST_CASE("A leaf card is not a round dot", "[gpu][particles][leaf]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});

    const scene::Scene roundScene = shedding(scene::ParticleShape::Round, 0.0f, wind::WindParams{});
    const scene::Scene leafScene = shedding(scene::ParticleShape::Leaf, 0.0f, wind::WindParams{});
    const gpu::Image8 round = run(*ctx, shaders, roundScene);
    const gpu::Image8 leaf = run(*ctx, shaders, leafScene);

    CHECK(lit(round) > 0); // or the comparison below is between two blank frames
    CHECK(lit(leaf) > 0);
    CHECK_DIFFERS(round, leaf);
    // A leaf is a card narrower than it is long, so at the same `size` it covers less. This is the
    // assertion that would catch the silhouette silently falling back to the round falloff.
    INFO("round lit " << lit(round) << ", leaf lit " << lit(leaf));
    CHECK(lit(leaf) < lit(round));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Particles ignore the wind unless they are told to catch it", "[gpu][particles][leaf][determinism]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});

    // THE GUARANTEE: `windInfluence` defaults to 0, so every particle system written before ADR-370
    // is unmoved by a field that did not exist when it was authored.
    const scene::Scene calm = shedding(scene::ParticleShape::Round, 0.0f, wind::WindParams{});
    const scene::Scene blown = shedding(scene::ParticleShape::Round, 0.0f, gale(0.35f));
    CHECK_IDENTICAL(run(*ctx, shaders, calm), run(*ctx, shaders, blown));
    CHECK(scene::ParticleSystem{}.windInfluence == 0.0f);

    // THE CONTROL: switched on, the same gale must move them.
    const scene::Scene catching = shedding(scene::ParticleShape::Leaf, 1.5f, gale(0.35f));
    const scene::Scene notCatching = shedding(scene::ParticleShape::Leaf, 0.0f, gale(0.35f));
    CHECK_DIFFERS(run(*ctx, shaders, catching), run(*ctx, shaders, notCatching));

    // ...and DIRECTION is a separate claim from speed. Same strength, opposite bearing.
    const scene::Scene other = shedding(scene::ParticleShape::Leaf, 1.5f, gale(0.35f + 3.14159265f));
    CHECK_DIFFERS(run(*ctx, shaders, catching), run(*ctx, shaders, other));
    CHECK(ctx->errorCount() == 0);
}
