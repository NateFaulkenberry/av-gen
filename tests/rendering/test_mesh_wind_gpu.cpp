// ADR-360: wind deformation for imported meshes, and the guarantee that it is opt-in.
//
// The byte-identity claim that made this mergeable used to live in a pair of rendered arms of the
// Tree of Life project. That was the wrong place for it: the moment that scene ships with its wind
// switched ON -- which is what the owner actually wants -- the arm has to be weakened or deleted,
// and the guarantee evaporates exactly when someone might rely on it.
//
// The guarantee is a property of the ENGINE, not of any one scene: a mesh that does not declare a
// wind body renders identically to before the deformation existed, however hard the world's wind is
// blowing. That is what this asserts, and it stays true no matter what the shipped scene asks for.

#include "core/log.hpp"
#include "core/wind.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"

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

wind::WindParams blowingHard() {
    wind::WindParams w;
    w.enabled = true;
    w.speed = 3.0f;
    w.direction = 0.6f;
    w.gustAmount = 1.5f;
    w.gustSpeed = 12.0f;
    w.turbulence = 0.5f;
    return w;
}

// A tall subdivided box standing on the origin: something with enough vertices up its height for a
// height-profiled deformation to have somewhere to go.
scene::Scene treeish(const wind::WindParams& w, bool declareBody) {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.wind = w;
    s.camera.position = {0.0f, 3.0f, 12.0f};
    s.camera.target = {0.0f, 3.0f, 0.0f};

    scene::Entity e;
    e.name = "trunk";
    e.mesh = s.addMesh(scene::makeIcosphere(1.0f, 3));
    e.transform.position = {0.0f, 3.0f, 0.0f};
    e.transform.scale = {1.0f, 3.0f, 1.0f};
    e.material.baseColor = {0.7f, 0.7f, 0.7f};
    e.material.emissiveColor = {0.6f, 0.9f, 0.6f};
    e.material.emissiveIntensity = 2.0f;
    if (declareBody) {
        e.wind.origin = {0.0f, 0.0f, 0.0f};
        e.wind.height = 6.0f;
        e.wind.radius = 1.0f;
        e.wind.strength = 1.0f;
    }
    s.entities.push_back(std::move(e));
    return s;
}

gpu::Image8 render(gpu::Context& ctx, gpu::ShaderLibrary& shaders, const scene::Scene& scene, double t) {
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FixedStepClock clock(60.0);
    clock.restartAt(t);
    gpu::Image8 last;
    for (int i = 0; i < 3; ++i) {
        auto img = renderer.renderToImage(scene, clock.tick(), 96, 96);
        REQUIRE(img.has_value());
        last = std::move(*img);
    }
    return last;
}

} // namespace

TEST_CASE("A mesh that declares no wind body is untouched by the wind", "[gpu][wind][mesh][determinism]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});

    // THE GUARANTEE. Every mesh draw in the engine now evaluates a wind gate, and with no body
    // declared that gate must return before it does any arithmetic -- so a gale changes nothing.
    const scene::Scene calm = treeish(wind::WindParams{}, false);
    const scene::Scene gale = treeish(blowingHard(), false);
    const gpu::Image8 a = render(*ctx, shaders, calm, 4.0);
    const gpu::Image8 b = render(*ctx, shaders, gale, 4.0);
    CHECK(a.rgba == b.rgba);

    // ...and still true at another second, because "nothing moved" and "nothing moves" are
    // different claims and only the second is the guarantee. Both arms move to the new time: the
    // first draft advanced only the gale arm and compared it against the calm arm at the old one,
    // which failed -- correctly, and for a reason that has nothing to do with wind. Something else
    // in the frame is a function of render time, so a probe that changes two variables at once
    // cannot attribute the difference to either.
    const gpu::Image8 calmLater = render(*ctx, shaders, calm, 9.5);
    const gpu::Image8 galeLater = render(*ctx, shaders, gale, 9.5);
    CHECK(calmLater.rgba == galeLater.rgba);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("A mesh that declares a wind body moves in it", "[gpu][wind][mesh]") {
    // THE CONTROL for the test above: same geometry, same wind, body declared. If this did not
    // differ, the identity above would be measuring a deformation that never runs at all rather
    // than one that is correctly gated -- which is precisely the defect ADR-360 was written about.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});

    const scene::Scene still = treeish(wind::WindParams{}, true);
    const scene::Scene blown = treeish(blowingHard(), true);
    const gpu::Image8 a = render(*ctx, shaders, still, 4.0);
    const gpu::Image8 b = render(*ctx, shaders, blown, 4.0);
    CHECK(a.rgba != b.rgba);

    // Wind that is switched off is not wind, whatever its speed says.
    wind::WindParams disabled = blowingHard();
    disabled.enabled = false;
    const gpu::Image8 off = render(*ctx, shaders, treeish(disabled, true), 4.0);
    CHECK(a.rgba == off.rgba);

    // And the deformation is a pure function of time (ADR-091): the same second twice, from a
    // fresh renderer, is the same frame. This is the half the owner's particle relaxation does not
    // cover, and a hero asset in a different pose after a scrub is the reason it does not.
    const gpu::Image8 t1 = render(*ctx, shaders, blown, 7.25);
    const gpu::Image8 t2 = render(*ctx, shaders, blown, 7.25);
    CHECK(t1.rgba == t2.rgba);
    CHECK(t1.rgba != b.rgba); // ...and a different second really is a different pose
    CHECK(ctx->errorCount() == 0);
}
