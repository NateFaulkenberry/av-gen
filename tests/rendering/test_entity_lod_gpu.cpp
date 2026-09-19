// Entity LOD against the pixels the renderer actually produced (ADR-351).
//
// The trap this file avoids is the one test_lod_gpu.cpp names: it is easy to "verify" a ladder by
// re-deriving the expected rung from the same numbers the selector used, which agrees by
// construction and proves nothing. So every arm here is either **the renderer's own record of what
// it submitted** -- `RenderStats::entityLod`, filled where the draw was recorded -- or the drawn
// image, and each has a control that fails when the mechanism is taken away.
//
// The fixture is a mesh the chain builder can actually reduce, built here rather than loaded so the
// test runs in a checkout with no assets. What it cannot test is the *foliage* case, whose whole
// point is geometry no synthetic fixture reproduces; that lives in test_mesh_lod.cpp's real-asset
// arm and in the rendered study.
#include "assets/mesh_lod.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/mesh_metrics.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 640;
constexpr std::uint32_t kHeight = 480;

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

// A scene with one sphere in it, a chain on that sphere, and the camera `distance` away.
struct Fixture {
    scene::Scene scene;
    std::vector<std::uint32_t> rungTriangles;
};

Fixture sphereWithChain(float distance, float maxScreenError = -1.0f) {
    Fixture out;
    const scene::MeshData sphere = scene::makeUvSphere(1.0f, 96, 64);
    const scene::MeshId mesh = out.scene.addMesh(sphere);
    scene::Entity& e = out.scene.addEntity("ball", mesh);
    e.material.baseColor = glm::vec3(0.8f);
    e.material.roughness = 0.5f;

    assets::LodChainSettings settings = assets::heroLodSettings();
    settings.ratios = {1.0f, 0.5f, 0.2f, 0.07f};
    const auto built = assets::buildLodChain(sphere, settings);
    REQUIRE(built);
    scene::MeshLodChain chain;
    chain.base = mesh;
    chain.sourceTriangles = built->sourceTriangles;
    chain.sourceSurfaceArea = scene::meshMetrics(sphere).surfaceArea;
    chain.maxScreenError = maxScreenError;
    out.rungTriangles.push_back(built->sourceTriangles);
    for (std::size_t level = 1; level < built->levels.size(); ++level) {
        scene::MeshLodLevel out_level;
        out_level.mesh = built->levels[level].mesh;
        out_level.targetRatio = built->levels[level].targetRatio;
        out_level.achievedRatio = built->levels[level].achievedRatio;
        out_level.error = built->levels[level].error;
        out_level.triangles = static_cast<std::uint32_t>(out_level.mesh.indices.size() / 3);
        out_level.surfaceArea = scene::meshMetrics(out_level.mesh).surfaceArea;
        out.rungTriangles.push_back(out_level.triangles);
        chain.levels.push_back(std::move(out_level));
    }
    out.scene.meshLods.push_back(std::move(chain));
    ++out.scene.meshVersion;

    out.scene.camera.position = {0.0f, 0.0f, distance};
    out.scene.camera.target = {0.0f, 0.0f, 0.0f};
    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.4f, -0.7f, -0.6f));
    key.intensity = 4.0f;
    out.scene.addLight(key);
    return out;
}

// The fraction of the frame the object covers, so "the same object is still there" is a measurement
// rather than a hope. Anything above black.
double coverage(const gpu::Image8& image) {
    std::size_t lit = 0;
    const std::size_t pixels = static_cast<std::size_t>(image.width) * image.height;
    for (std::size_t i = 0; i < pixels; ++i) {
        const std::uint8_t* p = image.rgba.data() + i * 4;
        if (p[0] > 8 || p[1] > 8 || p[2] > 8) {
            ++lit;
        }
    }
    return static_cast<double>(lit) / static_cast<double>(pixels);
}

} // namespace

TEST_CASE("the scene pass draws the rung the selector chose", "[gpu][entitylod]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    renderer.setQuality(rendering::QualityTier::High);
    FrameTime t{};

    // Far away: the quality floor lifted, so the cost rule takes the coarsest rung and the renderer
    // must have submitted that rung's triangles and not the source's.
    Fixture far = sphereWithChain(400.0f, /*maxScreenError=*/1.0e9f);
    const auto farImage = renderer.renderToImage(far.scene, t, kWidth, kHeight);
    REQUIRE(farImage);
    const rendering::RenderStats farStats = renderer.stats();
    CHECK(farStats.entityLod.drawables == 1);
    CHECK(farStats.entityLod.demoted == 1);
    CHECK(farStats.entityLod.sourceTriangles == far.rungTriangles.front());
    // The number that matters: the renderer counted a rung's worth of triangles, not the source's.
    CHECK(farStats.entityLod.drawnTriangles == far.rungTriangles.back());
    CHECK(farStats.entityLod.drawnTriangles < farStats.entityLod.sourceTriangles);

    // The control. The same scene with no chain at all draws the source, and the whole selection
    // block reports nothing -- which is what "a scene that does not ask pays nothing" means.
    Fixture bare = sphereWithChain(400.0f, 1.0e9f);
    bare.scene.meshLods.clear();
    ++bare.scene.meshVersion;
    const auto bareImage = renderer.renderToImage(bare.scene, t, kWidth, kHeight);
    REQUIRE(bareImage);
    CHECK(renderer.stats().entityLod.drawables == 0);
    CHECK(renderer.stats().entityLod.drawnTriangles == 0);

    // ...and the object is still drawn, at both. A LOD that reaches its triangle count by not
    // drawing the object passes every counter in this test and is the failure it exists to catch.
    CHECK(coverage(*farImage) > 0.0);
    CHECK(coverage(*bareImage) > 0.0);
}

TEST_CASE("the renderer honours the authored quality floor", "[gpu][entitylod]") {
    // What this measures and what it deliberately does not.
    //
    // The floor's *height* is calibrated on the Tree of Life and tested against that asset's real
    // rung errors in tests/unit/test_representation.cpp. It cannot be tested here, because a UV
    // sphere is the friendliest geometry a simplifier will ever meet: the coarsest rung of this one
    // deviates by about a hundredth of a unit, which projects to a couple of pixels at three metres
    // and is admitted by the default eight -- correctly. A floor that refused it would be refusing
    // a rung that is the same picture.
    //
    // So what is tested is that the *authored* floor reaches the renderer and decides the draw: the
    // same camera, the same chain, two floors, two answers.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    renderer.setQuality(rendering::QualityTier::High);
    FrameTime t{};

    // A floor of zero tolerates no deviation at all, so only LOD0 -- whose error is zero by
    // definition -- is admitted.
    Fixture strict = sphereWithChain(3.0f, /*maxScreenError=*/0.0f);
    REQUIRE(renderer.renderToImage(strict.scene, t, kWidth, kHeight).has_value());
    CHECK(renderer.stats().entityLod.drawables == 1);
    CHECK(renderer.stats().entityLod.demoted == 0);
    CHECK(renderer.stats().entityLod.drawnTriangles == strict.rungTriangles.front());

    // The control: the same camera with the floor lifted demotes to the bottom of the ladder.
    // Without it the arm above would pass on a renderer that had simply failed to build a chain.
    Fixture unfloored = sphereWithChain(3.0f, 1.0e9f);
    REQUIRE(renderer.renderToImage(unfloored.scene, t, kWidth, kHeight).has_value());
    CHECK(renderer.stats().entityLod.demoted == 1);
    CHECK(renderer.stats().entityLod.drawnTriangles == unfloored.rungTriangles.back());
}

TEST_CASE("an offline render is not given a rung", "[gpu][entitylod][offline]") {
    // §5.9, and §14 of the asset-LOD brief. A render is a deliverable and must not be a quietly
    // lower-fidelity version of the preview it was approved from. `RepresentationPolicy::forTier`
    // already promised this; nothing had ever been able to check it, because nothing consumed the
    // policy.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FrameTime t{};
    Fixture far = sphereWithChain(400.0f, 1.0e9f);

    renderer.setQuality(rendering::QualityTier::Offline);
    REQUIRE(renderer.renderToImage(far.scene, t, kWidth, kHeight).has_value());
    CHECK(renderer.stats().entityLod.demoted == 0);
    CHECK(renderer.stats().entityLod.drawnTriangles == far.rungTriangles.front());

    // The control, on the same renderer and the same scene: every other tier demotes it. So the
    // arm above is the tier and not the camera.
    for (const rendering::QualityTier tier :
         {rendering::QualityTier::Preview, rendering::QualityTier::Realtime,
          rendering::QualityTier::High}) {
        renderer.setQuality(tier);
        REQUIRE(renderer.renderToImage(far.scene, t, kWidth, kHeight).has_value());
        CHECK(renderer.stats().entityLod.demoted == 1);
    }

    // And `--render-limits unlimited`, which says the same thing through the scene rather than the
    // tier, does too.
    renderer.setQuality(rendering::QualityTier::High);
    far.scene.detailLimits = scene::DetailLimits::unlimited();
    REQUIRE(renderer.renderToImage(far.scene, t, kWidth, kHeight).has_value());
    CHECK(renderer.stats().entityLod.demoted == 0);
}

TEST_CASE("a rung still draws the object it replaced", "[gpu][entitylod]") {
    // The silhouette test. A triangle count says a cheaper mesh was submitted; only the frame says
    // it was a mesh of the same object. Measured as coverage, at a distance where the sphere is a
    // comfortable fraction of the frame and every rung of it is admitted.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    renderer.setQuality(rendering::QualityTier::High);
    FrameTime t{};

    Fixture source = sphereWithChain(6.0f, /*maxScreenError=*/0.0f);
    const auto lod0 = renderer.renderToImage(source.scene, t, kWidth, kHeight);
    REQUIRE(lod0);
    REQUIRE(renderer.stats().entityLod.demoted == 0);
    const double baseCoverage = coverage(*lod0);
    CHECK(baseCoverage > 0.02);

    Fixture demoted = sphereWithChain(6.0f, 1.0e9f);
    const auto lod = renderer.renderToImage(demoted.scene, t, kWidth, kHeight);
    REQUIRE(lod);
    REQUIRE(renderer.stats().entityLod.demoted == 1);
    const double lodCoverage = coverage(*lod);

    // A band, not a floor. The coarsest rung of a sphere is a faceted sphere: it covers slightly
    // less, because every facet chord lies inside the arc it replaced. What it must not do is cover
    // a tenth as much, or nothing, or the whole frame.
    CHECK(lodCoverage > baseCoverage * 0.9);
    CHECK(lodCoverage < baseCoverage * 1.1);
    // ...and it really did draw something cheaper, or the band above would be a tautology.
    CHECK(renderer.stats().entityLod.drawnTriangles < renderer.stats().entityLod.sourceTriangles / 2);
}
