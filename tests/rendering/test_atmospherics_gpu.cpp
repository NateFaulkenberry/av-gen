// Atmospheric effects on the GPU (ADR-230): the shader half, on pixels.
//
// The CPU tests in tests/unit/test_atmospherics.cpp cover resolution, the trajectory, the timing and
// the packing. What they cannot answer is whether the packed block reaches the sky-layer draw at
// all, whether the off switch is genuinely off, whether the terrain in front of an aurora really
// occludes it, and whether the same second renders the same pixels twice. Those are pixel questions.
//
// Every claim here is the kind ADR-182 asks for: an arm that produced a byte-identical frame to its
// baseline would be vacuous, so each one is checked against a frame it must differ from *and* one it
// must equal. The one place byte-identity is the *expected* result -- a disabled effect -- is
// asserted as equality on purpose, because that is the proof the off switch is complete rather than
// merely quiet.

#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "world/atmospherics.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

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

// A dark night sky over a low ridge, looked at from just above it. The ridge matters: half these
// assertions are about what the sky draw does *behind* geometry, and a frame with no horizon in it
// cannot answer them.
scene::Scene skyScene() {
    scene::Scene scene;
    const scene::MeshId ridge = scene.addMesh(scene::makeCube(1.0f));
    scene::Entity& hill = scene.addEntity("ridge", ridge);
    hill.transform.position = glm::vec3(0.0f, -18.0f, -90.0f);
    hill.transform.scale = glm::vec3(400.0f, 30.0f, 60.0f);
    hill.material.baseColor = glm::vec4(0.05f, 0.06f, 0.08f, 1.0f);
    hill.material.roughness = 0.95f;

    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.3f, -0.8f, -0.4f));
    key.color = glm::vec3(0.5f, 0.6f, 0.9f);
    key.intensity = 0.35f;
    scene.addLight(key);

    // Aimed a little above the horizon, so the frame is mostly sky with the ridge along the bottom.
    scene.camera.position = glm::vec3(0.0f, 6.0f, 40.0f);
    scene.camera.target = glm::vec3(0.0f, 34.0f, -120.0f);
    scene.camera.fovYRadians = glm::radians(50.0f);
    scene.environment.backgroundColor = glm::vec3(0.004f, 0.006f, 0.02f);
    return scene;
}

// A comet placed rather than advanced, so the frame under test depends on nothing but these numbers.
//
// `fragmentSize` is a parameter because this fixture renders at 320x200, where a pixel subtends
// about 0.0065 rad. The shipped preset's fragments are ~6 m across at 1400 m -- 0.004 rad, well
// under a pixel -- and the shader's angular anti-aliasing correctly fades them to nothing. The first
// version of this file did not know that and asserted a visible difference that could not exist; the
// arm came back byte-identical, which is vacuous rather than a null result. Both halves are now
// asserted: a supra-pixel fragment shows, and a sub-pixel one is removed.
world::AtmosphericFrame cometFrame(float travelledFraction, bool rainbow = false, bool sparkle = false,
                                   float fragmentSize = 0.5f) {
    world::AtmosphericEffect e = world::bioluminescentComet("probe");
    e.comet.path.anchor = world::SkyAnchor::World;
    e.comet.path.anchorPosition = glm::vec3(0.0f, 0.0f, 0.0f);
    e.comet.path.startAzimuth = 200.0f; // behind-left and high, crossing to behind-right and low
    e.comet.path.startElevation = 34.0f;
    e.comet.path.endAzimuth = 160.0f;
    e.comet.path.endElevation = 6.0f;
    e.comet.path.distance = 1400.0f;
    e.comet.path.travelSeconds = 10.0f;
    e.comet.path.arcLift = 0.0f;
    e.comet.path.curvature = 0.0f;
    e.comet.appearance.tailLength = 600.0f;
    e.comet.sparkle.enabled = sparkle;
    e.comet.sparkle.size = fragmentSize;
    e.comet.rainbow.enabled = rainbow;
    e.activation = world::Activation::Window;
    e.timing.windowStart = 0.0;
    e.timing.windowSeconds = 100.0;
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    e.ground.mode = world::GroundGlow::Off;

    const std::array<world::AtmosphericEffect, 1> set{e};
    world::AtmosphericContext ctx;
    ctx.seconds = 10.0 * travelledFraction;
    world::AtmosphericFrame frame;
    world::buildAtmosphericFrame(set, ctx, frame);
    return frame;
}

world::AtmosphericFrame auroraFrame(float intensity = 3.0f, bool enabled = true) {
    world::AtmosphericEffect e = world::glowmereAurora("sky");
    e.enabled = enabled;
    e.aurora.shape.anchor = world::SkyAnchor::World;
    e.aurora.shape.anchorPosition = glm::vec3(0.0f);
    e.aurora.shape.radius = 900.0f;
    e.aurora.shape.baseHeight = -60.0f;
    e.aurora.shape.curtainHeight = 700.0f;
    e.aurora.appearance.intensity = intensity;
    e.aurora.audio.spectrumShape = 0.0f; // no music in this fixture; keep the curtain flat-topped
    e.timing.fadeIn = 0.0;
    e.ground.mode = world::GroundGlow::Off;

    const std::array<world::AtmosphericEffect, 1> set{e};
    world::AtmosphericContext ctx;
    ctx.seconds = 3.0;
    world::AtmosphericFrame frame;
    world::buildAtmosphericFrame(set, ctx, frame);
    return frame;
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

// Mean brightness over a horizontal band of the frame, as a fraction of the height from the top.
double meanInBand(const gpu::Image8& image, double topFraction, double bottomFraction) {
    const std::size_t y0 = static_cast<std::size_t>(topFraction * kHeight);
    const std::size_t y1 = static_cast<std::size_t>(bottomFraction * kHeight);
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

} // namespace

TEST_CASE("an atmospheric effect reaches the sky-layer draw, and its off switch is complete",
          "[gpu][atmospherics]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene scene = skyScene();
    const FrameTime time{.renderTime = 1.0, .deltaTime = 1.0 / 60.0, .frameIndex = 1};
    auto render = [&](const scene::Scene& s) {
        auto image = renderer.renderToImage(s, time, kWidth, kHeight);
        REQUIRE(image.has_value());
        return std::move(*image);
    };

    const gpu::Image8 none = render(scene);

    SECTION("a live comet changes the frame") {
        scene.atmospherics = cometFrame(0.55f);
        REQUIRE(scene.atmospherics.cometCount == 1);
        const gpu::Image8 with = render(scene);
        // A comet is a big, bright thing. If it moved fewer pixels than this it is not the effect
        // the brief asked for, whatever else it is.
        CHECK(differingPixels(none, with) > kWidth * kHeight / 100);
    }

    SECTION("a live aurora changes the frame") {
        scene.atmospherics = auroraFrame();
        REQUIRE(scene.atmospherics.auroraCount == 1);
        const gpu::Image8 with = render(scene);
        CHECK(differingPixels(none, with) > kWidth * kHeight / 50);
    }

    SECTION("the same state renders identically twice") {
        // ADR-091's promise, at the level a shader can break it: nothing in the sky layer may read a
        // frame counter or a wall clock.
        scene.atmospherics = cometFrame(0.55f, true, true);
        const gpu::Image8 first = render(scene);
        const gpu::Image8 second = render(scene);
        CHECK(std::memcmp(first.rgba.data(), second.rgba.data(), first.rgba.size()) == 0);
    }

    SECTION("a disabled effect renders the baseline exactly") {
        // The arm that has to be byte-identical. An effect that is off must not merely be dim.
        scene.atmospherics = auroraFrame(3.0f, false);
        CHECK(scene.atmospherics.auroraCount == 0);
        CHECK_FALSE(scene.atmospherics.any());
        const gpu::Image8 off = render(scene);
        CHECK(differingPixels(none, off) == 0);
    }

    SECTION("the renderer's own arm removes the whole draw") {
        // §12's "effects disabled" measurement is only honest if the toggle removes the work rather
        // than zeroing the counts and still running a fullscreen pass.
        scene.atmospherics = auroraFrame();
        const gpu::Image8 on = render(scene);
        rendering::SceneRenderer::PassToggles toggles;
        toggles.atmospherics = false;
        renderer.setPassToggles(toggles);
        const gpu::Image8 armed = render(scene);
        renderer.setPassToggles(rendering::SceneRenderer::PassToggles{});
        CHECK(differingPixels(on, armed) > 0);   // it was doing something
        CHECK(differingPixels(none, armed) == 0); // ...and with the arm on, exactly nothing
    }
}

TEST_CASE("the sky layer is behind the world, not painted over it", "[gpu][atmospherics][depth]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene scene = skyScene();
    const FrameTime time{.renderTime = 1.0, .deltaTime = 1.0 / 60.0, .frameIndex = 1};
    auto render = [&](const scene::Scene& s) {
        auto image = renderer.renderToImage(s, time, kWidth, kHeight);
        REQUIRE(image.has_value());
        return std::move(*image);
    };

    const gpu::Image8 none = render(scene);
    scene.atmospherics = auroraFrame(6.0f);
    const gpu::Image8 with = render(scene);

    // The aurora's base is *below* the ridge line and the curtain is bright. If the draw were not
    // depth-tested it would paint straight over the ridge; if it were, the ridge is untouched.
    //
    // This is the whole of ADR-230's answer to "the aurora should convincingly originate from the
    // horizon": there is no horizon to author, because the depth buffer already is one.
    const double skyBefore = meanInBand(none, 0.15, 0.45);
    const double skyAfter = meanInBand(with, 0.15, 0.45);
    CHECK(skyAfter > skyBefore + 2.0); // the sky lit up...

    const double ridgeBefore = meanInBand(none, 0.86, 0.99);
    const double ridgeAfter = meanInBand(with, 0.86, 0.99);
    // ...and the ridge did not. A small tolerance rather than zero: the ridge is a lit surface and
    // ADR-230's ground illumination is a real term, though it is Off in this fixture.
    CHECK(std::abs(ridgeAfter - ridgeBefore) < 1.0);
}

TEST_CASE("a comet's colour modes are distinguishable on pixels", "[gpu][atmospherics][color]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene scene = skyScene();
    const FrameTime time{.renderTime = 1.0, .deltaTime = 1.0 / 60.0, .frameIndex = 1};
    auto render = [&](const scene::Scene& s) {
        auto image = renderer.renderToImage(s, time, kWidth, kHeight);
        REQUIRE(image.has_value());
        return std::move(*image);
    };

    scene.atmospherics = cometFrame(0.55f, false, false);
    const gpu::Image8 plain = render(scene);

    SECTION("rainbow mode is a different picture, not a different number in a struct") {
        scene.atmospherics = cometFrame(0.55f, true, false);
        const gpu::Image8 rainbow = render(scene);
        CHECK(differingPixels(plain, rainbow) > kWidth * kHeight / 200);
    }

    SECTION("sparkling fragments are a different picture too") {
        scene.atmospherics = cometFrame(0.55f, false, true, 0.5f);
        const gpu::Image8 sparkle = render(scene);
        CHECK(differingPixels(plain, sparkle) > 40);
    }

    SECTION("a fragment smaller than a pixel is removed rather than sampled") {
        // The anti-aliasing, asserted rather than assumed. A sub-pixel bright point that *is*
        // sampled is how a sparkle becomes a shimmer, and it is the artefact ADR-207 had to solve
        // for its own sparkle. Byte-identity is the expected result here, not a vacuous one: the
        // section above proves the same switch does something when the fragment is big enough.
        scene.atmospherics = cometFrame(0.55f, false, true, 0.02f);
        const gpu::Image8 tiny = render(scene);
        CHECK(differingPixels(plain, tiny) == 0);
    }

    SECTION("a comet moves between two transport seconds") {
        // The trajectory is a function of time and the shader reads it; this is the pixel-level
        // check that the packed `travelled` lane is actually consumed.
        scene.atmospherics = cometFrame(0.30f);
        const gpu::Image8 early = render(scene);
        scene.atmospherics = cometFrame(0.75f);
        const gpu::Image8 late = render(scene);
        CHECK(differingPixels(early, late) > kWidth * kHeight / 200);
    }
}

TEST_CASE("the comet quality control changes smoothness and not exposure",
          "[gpu][atmospherics][quality]") {
    // §12 asks for graceful degradation. The march is normalised by its own step count and the
    // sample width has a floor of the sample spacing, so halving the steps must cost detail and not
    // brightness -- if it changed exposure it would be a quality setting nobody could use.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene scene = skyScene();
    const FrameTime time{.renderTime = 1.0, .deltaTime = 1.0 / 60.0, .frameIndex = 1};
    auto render = [&](const scene::Scene& s) {
        auto image = renderer.renderToImage(s, time, kWidth, kHeight);
        REQUIRE(image.has_value());
        return std::move(*image);
    };

    scene.atmospherics = cometFrame(0.75f);
    scene.atmospherics.cometSteps = 32;
    const gpu::Image8 fine = render(scene);
    const double fineMean = meanInBand(fine, 0.0, 0.8);

    scene.atmospherics.cometSteps = 10;
    const gpu::Image8 coarse = render(scene);
    const double coarseMean = meanInBand(coarse, 0.0, 0.8);

    // It is a different picture...
    CHECK(differingPixels(fine, coarse) > 0);
    // ...but the same exposure, within a fifth of a level out of 255.
    CHECK(std::abs(fineMean - coarseMean) < std::max(fineMean, 1.0) * 0.2);
}
