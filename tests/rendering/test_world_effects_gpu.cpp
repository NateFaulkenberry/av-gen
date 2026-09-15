// World effects on the GPU (ADR-207): the shader half, on pixels.
//
// The CPU tests in tests/unit/test_world_effects.cpp cover resolution, activation, timing and the
// packing. What they cannot answer is whether the packed block reaches `pbr_shade.wgsl` at all,
// whether the off switch is genuinely off, and whether the wave lands where its distance metric says
// it should. Those are pixel questions, and the frame this renders is deliberately tiny -- a lit
// ground plane with a camera over it -- so the answers are about the effect and not about Glowmere.
//
// Every claim here is the kind ADR-182 asks for: an arm that produced a byte-identical frame to its
// baseline would be reported as vacuous, so each one is checked against a frame that it must differ
// from *and* one that it must equal.

#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "world/effects.hpp"

#include <glm/gtc/constants.hpp>
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

constexpr std::uint32_t kWidth = 256;
constexpr std::uint32_t kHeight = 160;

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

// A lit ground plane, looked down at from a corner. Big enough that a 40 m wave front has somewhere
// to be, small enough that the whole frame is one surface and a change in it is unambiguous.
scene::Scene groundScene() {
    scene::Scene scene;
    const scene::MeshId plane = scene.addMesh(scene::makePlane(120.0f, 1));
    scene::Entity& ground = scene.addEntity("ground", plane);
    ground.material.baseColor = glm::vec4(0.35f, 0.38f, 0.42f, 1.0f);
    ground.material.roughness = 0.9f;

    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    key.color = glm::vec3(1.0f, 0.97f, 0.9f);
    key.intensity = 2.0f;
    scene.addLight(key);

    scene.camera.position = glm::vec3(0.0f, 45.0f, 90.0f);
    scene.camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
    scene.camera.fovYRadians = glm::radians(45.0f);
    scene.environment.backgroundColor = glm::vec3(0.02f, 0.03f, 0.05f);
    return scene;
}

// A radial wave centred on the origin, packed as the engine packs one. The front is placed rather
// than advanced, so the frame under test depends on nothing but this number.
world::WorldEffectFrame radialAt(float frontDistance, float intensity = 6.0f) {
    world::WorldEffect e = world::heroGroundPulse("probe");
    e.appearance.intensity = intensity;
    e.appearance.edgeIntensity = intensity * 1.5f;
    e.appearance.color = glm::vec3(0.1f, 1.0f, 0.4f);
    e.sparkle.enabled = false;
    e.propagation.range = 120.0f;
    e.propagation.frontWidth = 4.0f;
    e.propagation.trailLength = 14.0f;
    e.propagation.verticalExtent = 20.0f;
    e.propagation.verticalGrowth = 0.0f;
    e.propagation.ringCount = 0.0f;
    e.response = world::MaterialResponse{1.0f, 1.0f, 1.0f, 0.0f};

    world::ResolvedEffect r;
    r.effect = &e;
    r.origin = glm::vec3(0.0f);
    r.axis = glm::vec3(0.0f, 1.0f, 0.0f);
    r.frontDistance = frontDistance;
    r.envelope = 1.0f;
    r.elapsed = 0.0;
    r.color = e.appearance.color;

    world::WorldEffectFrame frame;
    frame.count = 1;
    frame.effects[0] = world::packWorldEffect(r);
    return frame;
}

std::size_t differingPixels(const gpu::Image8& a, const gpu::Image8& b) {
    REQUIRE(a.rgba.size() == b.rgba.size());
    std::size_t differ = 0;
    for (std::size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        if (a.rgba[i] != b.rgba[i] || a.rgba[i + 1] != b.rgba[i + 1] ||
            a.rgba[i + 2] != b.rgba[i + 2]) {
            ++differ;
        }
    }
    return differ;
}

// The mean brightness inside an annulus of world radius, projected back through the camera. Cheaper
// and steadier than picking one pixel: what is being asked is "did the band land here", and a band
// four metres wide is several pixels at this resolution.
double meanAtRadius(const gpu::Image8& image, const scene::Scene& scene, float radius) {
    const glm::mat4 view = glm::lookAt(scene.camera.position, scene.camera.target, glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspective(scene.camera.effectiveFovY(),
                                            static_cast<float>(kWidth) / static_cast<float>(kHeight),
                                            0.1f, 500.0f);
    double total = 0.0;
    int samples = 0;
    for (int i = 0; i < 64; ++i) {
        const float a = glm::two_pi<float>() * static_cast<float>(i) / 64.0f;
        const glm::vec4 clip =
            proj * view * glm::vec4(std::cos(a) * radius, 0.0f, std::sin(a) * radius, 1.0f);
        if (clip.w <= 0.0f) {
            continue;
        }
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        const int x = static_cast<int>((ndc.x * 0.5f + 0.5f) * static_cast<float>(kWidth));
        const int y = static_cast<int>((0.5f - ndc.y * 0.5f) * static_cast<float>(kHeight));
        if (x < 1 || y < 1 || x + 1 >= static_cast<int>(kWidth) || y + 1 >= static_cast<int>(kHeight)) {
            continue;
        }
        const std::size_t at = (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) * 4;
        total += (image.rgba[at] + image.rgba[at + 1] + image.rgba[at + 2]) / 3.0;
        ++samples;
    }
    REQUIRE(samples > 8); // a radius that fell off the frame would answer a different question
    return total / samples;
}

} // namespace

TEST_CASE("a world effect reaches the shared surface shader, and its off switch is complete",
          "[gpu][worldeffects]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene scene = groundScene();
    const FrameTime time{.renderTime = 1.0, .deltaTime = 1.0 / 60.0, .frameIndex = 1};

    auto render = [&](const scene::Scene& s) {
        auto image = renderer.renderToImage(s, time, kWidth, kHeight);
        REQUIRE(image.has_value());
        return std::move(*image);
    };

    const gpu::Image8 none = render(scene);

    SECTION("a live effect changes the frame") {
        scene.worldEffects = radialAt(40.0f);
        const gpu::Image8 lit = render(scene);
        const std::size_t changed = differingPixels(lit, none);
        // Not "some pixels changed": a 4 m front with a 14 m trail on a 240 m plane seen from 90 m
        // is a visible band, and a handful of changed pixels would mean something else moved.
        CHECK(changed > kWidth * kHeight / 100);
        INFO("pixels changed: " << changed);

        SECTION("and the renderer's own toggle puts it back byte for byte") {
            // ADR-207's idle arm. Not "nearly the same" -- the whole claim is that a frame with the
            // system switched off is the frame that existed before the system did.
            rendering::SceneRenderer::PassToggles off;
            off.worldEffects = false;
            renderer.setPassToggles(off);
            const gpu::Image8 disabled = render(scene);
            renderer.setPassToggles(rendering::SceneRenderer::PassToggles{});
            CHECK(differingPixels(disabled, none) == 0);
        }

        SECTION("as does a count of zero") {
            scene.worldEffects = world::WorldEffectFrame{};
            CHECK(differingPixels(render(scene), none) == 0);
        }
    }

    SECTION("the same state renders identically twice") {
        // The determinism claim, on pixels. Nothing in the effect path may read a frame counter or a
        // wall clock, so two renders of one state are one image.
        scene.worldEffects = radialAt(40.0f);
        const gpu::Image8 first = render(scene);
        const gpu::Image8 second = render(scene);
        CHECK(std::memcmp(first.rgba.data(), second.rgba.data(), first.rgba.size()) == 0);
    }

    SECTION("the band lands at the radius the front is at, and travels with it") {
        // The point of a world-space propagation: the wave is where its distance metric says, on the
        // ground, not somewhere in screen space. Two fronts, and the brightness has to follow.
        scene.worldEffects = radialAt(30.0f);
        const gpu::Image8 near = render(scene);
        scene.worldEffects = radialAt(70.0f);
        const gpu::Image8 far = render(scene);

        const double baseAt30 = meanAtRadius(none, scene, 30.0f);
        const double baseAt70 = meanAtRadius(none, scene, 70.0f);
        // Bright where the front is...
        CHECK(meanAtRadius(near, scene, 30.0f) > baseAt30 + 12.0);
        CHECK(meanAtRadius(far, scene, 70.0f) > baseAt70 + 12.0);
        // ...and, 40 m past a 4 m front with a 14 m trail, back to the ground it was.
        CHECK(meanAtRadius(near, scene, 70.0f) < baseAt70 + 2.0);
        CHECK(meanAtRadius(far, scene, 30.0f) < baseAt30 + 2.0);
    }

    SECTION("intensity is a gain on the contribution, not a switch") {
        scene.worldEffects = radialAt(40.0f, 2.0f);
        const double dim = meanAtRadius(render(scene), scene, 40.0f);
        scene.worldEffects = radialAt(40.0f, 8.0f);
        const double bright = meanAtRadius(render(scene), scene, 40.0f);
        const double base = meanAtRadius(none, scene, 40.0f);
        CHECK(dim > base + 4.0);
        CHECK(bright > dim + 4.0);
    }
}
