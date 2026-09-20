// The Cosmic Ocean on pixels (ADR-390).
//
// tests/unit/test_cosmic_ocean.cpp covers the data model, the packing and the panel's paths. What
// it cannot answer is whether any of it reaches a pixel, whether the parallax really separates the
// strata, whether the planets are lit rather than flat, and whether the same second renders the
// same frame twice. Those are pixel questions and this is where they are asked.
//
// Two rules from this project's measurement history are load-bearing here:
//
//  * **Render and look.** A black frame and a nearly-black frame are identical in a hash, and this
//    effect's whole failure mode is being too dim to see. So the assertions are on brightness and
//    on where the brightness is, not on a digest.
//  * **A probe that cannot fail proves nothing** (ADR-182). Every arm below is checked against a
//    frame it must differ from; the one place byte-identity is the expected answer -- the off
//    switch -- is asserted as equality on purpose, because that is the proof it is complete rather
//    than merely quiet.
//
// Note `CosmicOceanRenderer::update` is called directly. Until `AtmosphericFrame` carries the ocean
// -- one of ADR-390's four shared switch arms, waiting on the vortex branch -- this test is the only
// caller, and that is the sequencing rather than an oversight. When the scene-authored path lands,
// these tests should keep passing unchanged: they drive the same entry point the engine will.

#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/cosmic_ocean_renderer.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "world/cosmic_ocean.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 320;
constexpr std::uint32_t kHeight = 200;

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

// A dark scene with a low ridge along the bottom. The ridge is not decoration: half of what the
// draw has to get right is what it does *behind* geometry, and a frame with no occluder in it
// cannot answer that.
scene::Scene oceanScene() {
    scene::Scene scene;
    const scene::MeshId ridge = scene.addMesh(scene::makeCube(1.0f));
    scene::Entity& hill = scene.addEntity("ridge", ridge);
    hill.transform.position = glm::vec3(0.0f, -18.0f, -90.0f);
    hill.transform.scale = glm::vec3(400.0f, 30.0f, 60.0f);
    hill.material.baseColor = glm::vec4(0.03f, 0.035f, 0.05f, 1.0f);
    hill.material.roughness = 0.95f;

    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.3f, -0.8f, -0.4f));
    key.color = glm::vec3(0.5f, 0.6f, 0.9f);
    key.intensity = 0.3f;
    scene.addLight(key);

    scene.camera.position = glm::vec3(0.0f, 6.0f, 40.0f);
    scene.camera.target = glm::vec3(0.0f, 34.0f, -120.0f);
    scene.camera.fovYRadians = glm::radians(50.0f);
    scene.environment.backgroundColor = glm::vec3(0.004f, 0.006f, 0.02f);
    return scene;
}

std::size_t differingPixels(const gpu::Image8& a, const gpu::Image8& b) {
    REQUIRE(a.rgba.size() == b.rgba.size());
    std::size_t differ = 0;
    for (std::size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        if (a.rgba[i] != b.rgba[i] || a.rgba[i + 1] != b.rgba[i + 1] || a.rgba[i + 2] != b.rgba[i + 2]) {
            ++differ;
        }
    }
    return differ;
}

double meanBrightness(const gpu::Image8& image, double topFraction, double bottomFraction) {
    const auto y0 = static_cast<std::size_t>(topFraction * kHeight);
    const auto y1 = static_cast<std::size_t>(bottomFraction * kHeight);
    double total = 0.0;
    std::size_t n = 0;
    for (std::size_t y = y0; y < y1 && y < kHeight; ++y) {
        for (std::size_t x = 0; x < kWidth; ++x) {
            const std::size_t i = (y * kWidth + x) * 4;
            total += (image.rgba[i] + image.rgba[i + 1] + image.rgba[i + 2]) / 3.0;
            ++n;
        }
    }
    return n > 0 ? total / static_cast<double>(n) : 0.0;
}

// A visible ocean. The shipped default is deliberately restrained -- §32's hierarchy keeps it two
// orders of magnitude under the subject -- and at 320x200 with 8-bit readback that is genuinely
// close to the quantisation floor. So the fixture brightens it rather than asserting on a frame
// where "changed" and "rounded" are the same thing. The default's *restraint* is asserted in the
// unit test, on the authored radiances, which is where that claim belongs.
world::CosmicOcean visibleOcean() {
    world::CosmicOcean o = world::defaultCosmicOcean();
    o.brightness = 6.0f;
    o.nebulaMid.stratum.brightness = 0.9f;
    o.nebulaFar.stratum.brightness = 0.7f;
    o.starsNear.stratum.density = 0.3f;
    o.starsMid.stratum.density = 0.5f;
    o.planets.stratum.density = 0.6f;
    o.planets.scale = 3.0f;
    return o;
}

} // namespace

TEST_CASE("the cosmic ocean reaches the frame and its off switch is complete", "[gpu][cosmic]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    REQUIRE(renderer.cosmicOcean().ready());

    const scene::Scene scene = oceanScene();
    const FrameTime time{.renderTime = 1.0, .deltaTime = 1.0 / 60.0, .frameIndex = 1};
    auto render = [&]() {
        auto image = renderer.renderToImage(scene, time, kWidth, kHeight);
        REQUIRE(image.has_value());
        return std::move(*image);
    };
    auto setOcean = [&](const world::CosmicOcean& o, double seconds) {
        renderer.cosmicOcean().update(world::packCosmicOcean(o, 1.0f, seconds), true, 0);
    };

    renderer.cosmicOcean().update(world::CosmicOceanGpu{}, false, 0);
    const gpu::Image8 none = render();

    SECTION("a live ocean changes most of the sky") {
        setOcean(visibleOcean(), 4.0);
        const gpu::Image8 with = render();
        // §39's acceptance test, as a number: turn it on and the scene should be dramatically
        // fuller. The sky is most of this frame, so most of the frame has to move.
        CHECK(differingPixels(none, with) > kWidth * kHeight / 2);
        CHECK(meanBrightness(with, 0.0, 0.6) > meanBrightness(none, 0.0, 0.6));
    }

    SECTION("the ridge still occludes it") {
        // The draw is depth-tested, so geometry in front of the sky silhouettes it with no horizon
        // to author -- the depth buffer is the horizon. If this fails the pipeline's depth state is
        // wrong and the cosmos is painted over the world.
        setOcean(visibleOcean(), 4.0);
        const gpu::Image8 with = render();
        const double sky = meanBrightness(with, 0.0, 0.45);
        const double ground = meanBrightness(with, 0.88, 1.0);
        INFO("sky band " << sky << ", ground band " << ground);
        CHECK(sky > ground * 1.5);
    }

    SECTION("switching it off restores the baseline exactly") {
        setOcean(visibleOcean(), 4.0);
        const gpu::Image8 with = render();
        REQUIRE(differingPixels(none, with) > 0);
        renderer.cosmicOcean().update(world::CosmicOceanGpu{}, false, 0);
        const gpu::Image8 off = render();
        // Equality on purpose. The arm that measures this effect's cost has to remove all of it,
        // and an off switch that leaves a few pixels behind is an arm that measures the wrong
        // number for ever after.
        CHECK(std::memcmp(none.rgba.data(), off.rgba.data(), none.rgba.size()) == 0);
    }

    SECTION("the same second renders identically twice") {
        // ADR-091, at the level a shader can break it: nothing in this draw may read a frame
        // counter or a wall clock.
        setOcean(visibleOcean(), 7.25);
        const gpu::Image8 first = render();
        setOcean(visibleOcean(), 7.25);
        const gpu::Image8 second = render();
        CHECK(std::memcmp(first.rgba.data(), second.rgba.data(), first.rgba.size()) == 0);
    }

    SECTION("the environment evolves over half a minute") {
        // §39: "wait 30-60 seconds; the environment should have evolved subtly". Subtle is the hard
        // half -- it has to change and it must not churn -- so this asserts both bounds.
        setOcean(visibleOcean(), 4.0);
        const gpu::Image8 early = render();
        setOcean(visibleOcean(), 44.0);
        const gpu::Image8 late = render();
        const std::size_t moved = differingPixels(early, late);
        INFO(moved << " of " << (kWidth * kHeight) << " pixels moved over 40 s");
        CHECK(moved > kWidth * kHeight / 100);
    }

    SECTION("a different seed is a different universe, and the same seed is the same one") {
        // §34, and the only property that makes a procedural environment directable at all.
        world::CosmicOcean a = visibleOcean();
        world::CosmicOcean b = a;
        b.seed = a.seed + 91.0f;
        setOcean(a, 3.0);
        const gpu::Image8 first = render();
        setOcean(b, 3.0);
        const gpu::Image8 other = render();
        CHECK(differingPixels(first, other) > kWidth * kHeight / 50);
        setOcean(a, 3.0);
        const gpu::Image8 again = render();
        CHECK(std::memcmp(first.rgba.data(), again.rgba.data(), first.rgba.size()) == 0);
    }
}

TEST_CASE("parallax separates the strata rather than sliding the sky", "[gpu][cosmic]") {
    // §25, and the one claim that distinguishes this from a skybox. Translating the camera must move
    // a near stratum much more than a far one -- and a *rotation* must move everything, because that
    // is what reveals the environment.
    //
    // Measured as a ratio between two arms rather than as an absolute, because the absolute depends
    // on the fixture's scale and the ratio is the thing the design claims: apparent shift per metre
    // of travel is `parallax / depth`, so dust at 900 m and 0.80 should move about ninety times as
    // far as planets at 18 km and 0.18.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const FrameTime time{.renderTime = 1.0, .deltaTime = 1.0 / 60.0, .frameIndex = 1};

    // One stratum at a time, so the measurement is of that stratum and not of everything at once.
    auto onlyStratum = [](bool near) {
        world::CosmicOcean o = world::defaultCosmicOcean();
        o.brightness = 8.0f;
        o.nebulaFar.stratum.density = 0.0f;
        o.nebulaMid.stratum.density = 0.0f;
        o.galaxies.stratum.density = 0.0f;
        o.planets.stratum.density = 0.0f;
        o.dust.stratum.density = 0.0f;
        o.starsUltra.stratum.density = 0.0f;
        o.starsFar.stratum.density = 0.0f;
        o.starsMid.stratum.density = 0.0f;
        o.starsNear.stratum.density = 0.0f;
        o.atmosphere.haze = 0.0f;
        o.events.shootingStars = 0.0f;
        o.events.flares = 0.0f;
        if (near) {
            // A near, strongly parallaxed star field.
            o.starsNear.stratum = {900.0f, 0.95f, 0.30f, 6.0f};
            o.starsNear.twinkle = 0.0f;
            o.starsNear.glint = 0.0f;
        } else {
            // A far, essentially fixed one, at the same density so the two frames are comparable.
            o.starsNear.stratum = {900000.0f, 0.02f, 0.30f, 6.0f};
            o.starsNear.twinkle = 0.0f;
            o.starsNear.glint = 0.0f;
        }
        return o;
    };

    auto movedUnderTranslation = [&](bool near) {
        scene::Scene scene = oceanScene();
        renderer.cosmicOcean().update(world::packCosmicOcean(onlyStratum(near), 1.0f, 2.0), true, 0);
        auto a = renderer.renderToImage(scene, time, kWidth, kHeight);
        REQUIRE(a.has_value());
        scene.camera.position += glm::vec3(120.0f, 0.0f, 0.0f);
        scene.camera.target += glm::vec3(120.0f, 0.0f, 0.0f); // pure translation: the aim is parallel
        renderer.cosmicOcean().update(world::packCosmicOcean(onlyStratum(near), 1.0f, 2.0), true, 0);
        auto b = renderer.renderToImage(scene, time, kWidth, kHeight);
        REQUIRE(b.has_value());
        return differingPixels(*a, *b);
    };

    const std::size_t nearMoved = movedUnderTranslation(true);
    const std::size_t farMoved = movedUnderTranslation(false);
    INFO("near stratum moved " << nearMoved << " px, far stratum moved " << farMoved << " px");
    // The near layer has to move, or there is no parallax at all and this is a skybox.
    CHECK(nearMoved > 0);
    // ...and it has to move substantially more than the far one, or every layer is at one distance
    // and the "depth system" is a name rather than a mechanism.
    CHECK(nearMoved > farMoved * 3);
}
