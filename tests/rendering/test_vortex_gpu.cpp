// ADR-371: the cosmic vortex, as a world-space term in the volumetric march.
//
// Two properties. The vortex is off unless asked for, byte-for-byte -- it shares a pass with the
// fog and must not perturb any scene that only wanted fog. And when it is on it is genuinely
// world-space: moving the camera changes what it looks like, which is the whole difference between
// this and a backdrop, and is the brief's section 20 stated as a test.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "rendering/volume_renderer.hpp"
#include "scene/scene.hpp"
#include "support/image_diff.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>

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

scene::Scene voidWorld(bool vortexOn, float fogDensity, glm::vec3 eye) {
    scene::Scene s;
    s.environment.backgroundColor = {0.004f, 0.006f, 0.018f};
    s.environment.volumeDensity = fogDensity;
    s.environment.volumeMaxDistance = 4000.0f;
    s.environment.volumeSteps = 48;
    s.camera.position = eye;
    s.camera.target = {0.0f, -200.0f, 0.0f};
    if (vortexOn) {
        // ADR-387: the vortex is an authored atmospheric effect, and what the renderer reads is the
        // resolved frame. Built here directly rather than through `resolveAtmospherics`, so this
        // test still states the vortex it is rendering rather than an effect list and a second.
        s.atmospherics.hasVortex = true;
        world::Vortex& v = s.atmospherics.vortex;
        v.field.center = {0.0f, -600.0f, 0.0f};
        v.field.radius = 1500.0f;
        v.field.thickness = 300.0f;
        v.density = 0.0012f;
        v.emission = 0.006f;
        v.field.contrast = 3.6f;
        v.field.innerVoid = 0.30f;
    }
    return s;
}

gpu::Image8 shot(gpu::Context& ctx, gpu::ShaderLibrary& shaders, const scene::Scene& s) {
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FixedStepClock clock(30.0);
    clock.restartAt(4.0);
    auto img = renderer.renderToImage(s, clock.tick(), 128, 72);
    REQUIRE(img.has_value());
    return std::move(*img);
}

long brightness(const gpu::Image8& i) {
    long n = 0;
    for (std::size_t k = 0; k < i.rgba.size(); k += 4) {
        n += i.rgba[k] + i.rgba[k + 1] + i.rgba[k + 2];
    }
    return n;
}

} // namespace

TEST_CASE("A scene without a vortex marches exactly what it always marched", "[gpu][vortex][determinism]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const glm::vec3 eye{300.0f, 0.0f, 300.0f};

    // Radius 0 is the gate, and it is the default.
    CHECK(world::Vortex{}.field.radius == 0.0f);
    CHECK_FALSE(world::Vortex{}.active());
    // ...and a scene that authors none says so, which is the gate one level above the radius.
    CHECK_FALSE(scene::Scene{}.atmospherics.hasVortex);

    // With fog and no vortex, against the same scene built without ever touching the vortex struct.
    const scene::Scene fogOnly = voidWorld(false, 0.02f, eye);
    const auto a = shot(*ctx, shaders, fogOnly);
    const auto b = shot(*ctx, shaders, fogOnly);
    {
        const auto d = ::avgen::testing::byteDiff(a.rgba, b.rgba);
        INFO(d.describe());
        CHECK(d.identical());
    }

    // ...and the vortex can switch the march ON where the fog alone would have left it off, which
    // is the reason `VolumeRenderer::enabled` had to change: the scene this is for runs with no fog
    // at all, and gating the vortex behind the fog's density would have made it unreachable there.
    //
    // ADR-387 moved the vortex off the environment, so the gate that knows about it is the one that
    // takes the whole SCENE. The `Environment` overload is kept for the fog-only callers and this
    // case now asserts the difference between them, because a caller reading the wrong one is
    // exactly the defect that shipped for one commit: the particle fog coupling kept asking the
    // environment and silently stopped being filled.
    CHECK_FALSE(rendering::VolumeRenderer::enabled(voidWorld(false, 0.0f, eye)));
    CHECK(rendering::VolumeRenderer::enabled(voidWorld(true, 0.0f, eye)));
    CHECK_FALSE(rendering::VolumeRenderer::enabled(voidWorld(true, 0.0f, eye).environment));
    CHECK(rendering::VolumeRenderer::enabled(voidWorld(false, 0.02f, eye).environment));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("The vortex is world-space, not a backdrop", "[gpu][vortex]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});

    const glm::vec3 high{300.0f, 250.0f, 300.0f};
    const glm::vec3 low{300.0f, -250.0f, 300.0f};

    const auto offHigh = shot(*ctx, shaders, voidWorld(false, 0.0f, high));
    const auto onHigh = shot(*ctx, shaders, voidWorld(true, 0.0f, high));
    const auto onLow = shot(*ctx, shaders, voidWorld(true, 0.0f, low));

    // It draws something.
    INFO("off " << brightness(offHigh) << " on " << brightness(onHigh));
    CHECK(brightness(onHigh) > brightness(offHigh));

    // THE CONTROL that separates "world-space phenomenon" from "backdrop": drop the camera below
    // the disc and the picture must change. A skybox or a screen-space wash would not care.
    {
        const auto d = ::avgen::testing::byteDiff(onHigh.rgba, onLow.rgba);
        INFO(d.describe());
        CHECK_FALSE(d.identical());
    }
    // ...and the control for THAT control: the same camera move with no vortex must leave the
    // difference to the rest of the scene, so the change above is attributable to the vortex.
    const auto offLow = shot(*ctx, shaders, voidWorld(false, 0.0f, low));
    const auto dOff = ::avgen::testing::byteDiff(offHigh.rgba, offLow.rgba);
    const auto dOn = ::avgen::testing::byteDiff(onHigh.rgba, onLow.rgba);
    INFO("camera move without vortex: " << dOff.describe() << "; with: " << dOn.describe());
    CHECK(dOn.differing > dOff.differing);
    CHECK(ctx->errorCount() == 0);
}
