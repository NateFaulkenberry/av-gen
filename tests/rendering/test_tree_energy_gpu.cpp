// ADR-376: tree energy and canopy shimmer.
//
// Both are emissive terms on the mesh path, gated by two intensities that default to zero. The
// guarantee is the one every addition in this branch has carried: an entity that has not asked for
// them renders exactly as it did before they existed. The control is an entity that has.
//
// And both are pure functions of time (ADR-091). The owner's relaxation covers particles; it does
// not cover a hero asset's own light, and a tree that is a different brightness after a scrub than
// after playing to the same second is a defect somebody would notice immediately.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
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

scene::Scene tree(float intensity, float shimmer) {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.camera.position = {0.0f, 5.0f, 13.0f};
    s.camera.target = {0.0f, 5.0f, 0.0f};

    scene::Entity e;
    e.name = "trunk";
    e.mesh = s.addMesh(scene::makeIcosphere(1.0f, 3));
    e.transform.position = {0.0f, 5.0f, 0.0f};
    e.transform.scale = {2.2f, 5.0f, 2.2f};
    e.material.baseColor = {0.4f, 0.4f, 0.4f};
    // The body frame the energy is keyed on. It exists independently of the wind's strength: a tree
    // can conduct while standing perfectly still, which is what `wind.strength = 0` here checks.
    e.wind.origin = {0.0f, 0.0f, 0.0f};
    e.wind.height = 10.0f;
    e.wind.radius = 2.2f;
    e.wind.strength = 0.0f;
    e.energy.intensity = intensity;
    e.energy.shimmer = shimmer;
    // A band that crosses the whole body in a second. The default 0.16 takes six, and two sample
    // times three and a half seconds apart happened to put the band at two heights this fixture
    // renders alike -- which cost an hour of probing a feature that was working. When the claim is
    // "it moves", pick a rate where a difference is unmissable rather than one where it is subtle.
    e.energy.propagation = 1.0f;
    // `shimmerScale` and `noiseScale` are in WORLD UNITS and the shipped defaults are tuned for a
    // 138 m tree: 0.035 puts this 4 m fixture entirely inside one noise cell, so the "wave" is a
    // single constant across the whole object -- and when that constant is negative the shimmer
    // clamps to zero and renders identically to no shimmer at all. That is what the failing arm
    // was telling me, and it is a real property of the effect rather than a fixture quirk: a scale
    // has to suit the body it is applied to.
    e.energy.shimmerScale = 1.5f;
    e.energy.noiseScale = 1.2f;
    s.entities.push_back(std::move(e));
    return s;
}

gpu::Image8 shot(gpu::Context& ctx, gpu::ShaderLibrary& shaders, const scene::Scene& s, double t) {
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FixedStepClock clock(60.0);
    clock.restartAt(t);
    gpu::Image8 last;
    for (int i = 0; i < 3; ++i) {
        auto img = renderer.renderToImage(s, clock.tick(), 192, 192);
        REQUIRE(img.has_value());
        last = std::move(*img);
    }
    return last;
}

} // namespace

TEST_CASE("A tree that asks for no energy is unchanged by it", "[gpu][energy][determinism]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});

    CHECK(scene::Entity::TreeEnergy{}.intensity == 0.0f);
    CHECK(scene::Entity::TreeEnergy{}.shimmer == 0.0f);
    CHECK_FALSE(scene::Entity::TreeEnergy{}.active());

    // Two separate seconds, both arms moved together -- moving only one would conflate time with
    // the effect, which is the mistake test_mesh_wind_gpu caught me making.
    const scene::Scene dark = tree(0.0f, 0.0f);
    CHECK_IDENTICAL(shot(*ctx, shaders, dark, 2.0), shot(*ctx, shaders, dark, 2.0));
    CHECK_IDENTICAL(shot(*ctx, shaders, dark, 9.0), shot(*ctx, shaders, dark, 9.0));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Energy conducts, shimmer travels, and both are functions of time", "[gpu][energy]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});

    const scene::Scene dark = tree(0.0f, 0.0f);
    // Modest intensities, and that is a fixture decision worth recording. At 2.0 against an
    // unlit black scene the emission saturates, every lit texel maps to white, and two DIFFERENT
    // pulse positions render identically -- so the "the pulse travels" assertion failed against a
    // feature that works. The tone map ate the evidence. Measured in the application at the same
    // settings the effect varies by a mean of 27.9 luminance levels over four seconds; the fault
    // was a test scene with nothing else in it to keep the exposure honest.
    const scene::Scene lit = tree(0.35f, 0.0f);
    const scene::Scene shimmering = tree(0.0f, 0.5f);

    // THE CONTROLS: each one on its own must change the frame, or the identity above is measuring
    // a feature that never runs rather than one that is correctly gated.
    CHECK_DIFFERS(shot(*ctx, shaders, dark, 2.0), shot(*ctx, shaders, lit, 2.0));
    CHECK_DIFFERS(shot(*ctx, shaders, dark, 2.0), shot(*ctx, shaders, shimmering, 2.0));

    // The pulse travels, so two seconds apart are two different pictures.
    CHECK_DIFFERS(shot(*ctx, shaders, lit, 2.0), shot(*ctx, shaders, lit, 5.5));

    // ...and ADR-091: the same second twice, from a fresh renderer, is the same frame. The owner's
    // particle relaxation does not reach the tree's own light.
    CHECK_IDENTICAL(shot(*ctx, shaders, lit, 5.5), shot(*ctx, shaders, lit, 5.5));
    CHECK_IDENTICAL(shot(*ctx, shaders, shimmering, 5.5), shot(*ctx, shaders, shimmering, 5.5));
    CHECK(ctx->errorCount() == 0);
}
