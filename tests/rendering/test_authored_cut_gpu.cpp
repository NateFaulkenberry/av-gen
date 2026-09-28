// ADR-947 on pixels: a Hero Pulse gated by an AUTHORED cut rings out from its hero during that
// hero's shot, and nowhere else.
//
// tests/unit/test_authored_cut_focus.cpp proves the gating on the engine's status and packed waves.
// This asks what a viewer sees: the frame with the pulse against the frame without it, a ring at the
// radius the front has reached (centred on the hero), gone again in the other hero's shot, and --
// the control arm -- nothing at all when the effects read only the director's spans, which an
// authored cut does not have.

#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/authored_cut.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/wave_effect.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
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

// A lit ground plane seen from a corner (test_wave_effects_gpu.cpp's frame): alpha stands at its
// centre, beta 400 m off it, beyond any ring of beta's reaching the plane.
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

world::HeroPoint hero(const char* name, glm::vec3 at) {
    world::HeroPoint h;
    h.name = name;
    h.position = at;
    h.radius = 2.0f;
    h.height = 6.0f;
    return h;
}

// The shipped Ground Pulse on an entity (GV2 multicam's Hero Pulse: Owner source, `heroFocus`),
// brightened so a band on a grey plane is unambiguous.
world::EffectInstance pulseOn(const char* owner) {
    world::EffectInstance e = world::makeEffect(world::EffectKind::GroundPulse, std::string("Hero Pulse ") + owner);
    e.owner = world::EffectOwner::entity(owner);
    e.wave.source.kind = world::SourceKind::Owner;
    e.wave.appearance.intensity = 6.0f;
    e.wave.appearance.edgeIntensity = 9.0f;
    e.wave.appearance.color = glm::vec3(0.1f, 1.0f, 0.4f);
    e.wave.sparkle.enabled = false;
    e.wave.propagation.ringCount = 0.0f;
    e.wave.propagation.verticalGrowth = 0.0f;
    e.wave.propagation.verticalExtent = 20.0f;
    e.wave.propagation.frontWidth = 4.0f;
    e.wave.propagation.trailLength = 8.0f;
    return e;
}

// Two cameras, one aiming at alpha (0-6 s), one following beta (6-12 s): the authored cut.
scene::CameraDirection twoShotCut() {
    scene::CameraDirection d;
    d.ensureMainCamera();
    scene::CameraRig a;
    a.id = 2;
    a.name = "On Alpha";
    a.slug = "onalpha";
    a.aimNode = "alpha";
    scene::CameraRig b;
    b.id = 3;
    b.name = "On Beta";
    b.slug = "onbeta";
    b.followNode = "beta";
    d.cameras.push_back(a);
    d.cameras.push_back(b);
    d.nextId = 4;
    scene::CameraShot s1;
    s1.camera = 2;
    s1.startSeconds = 0.0;
    s1.endSeconds = 6.0;
    scene::CameraShot s2 = s1;
    s2.camera = 3;
    s2.startSeconds = 6.0;
    s2.endSeconds = 12.0;
    d.shots = {s1, s2};
    return d;
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

// Mean brightness on a ground circle of world `radius` about the origin (where alpha stands).
double meanAtRadius(const gpu::Image8& image, const scene::Scene& scene, float radius) {
    const glm::mat4 view = glm::lookAt(scene.camera.position, scene.camera.target, glm::vec3(0, 1, 0));
    const glm::mat4 proj = glm::perspective(scene.camera.effectiveFovY(),
                                            static_cast<float>(kWidth) / static_cast<float>(kHeight), 0.1f, 500.0f);
    double total = 0.0;
    int samples = 0;
    for (int i = 0; i < 64; ++i) {
        const float a = glm::two_pi<float>() * static_cast<float>(i) / 64.0f;
        const glm::vec4 clip = proj * view * glm::vec4(std::cos(a) * radius, 0.0f, std::sin(a) * radius, 1.0f);
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
    REQUIRE(samples > 8);
    return total / samples;
}

} // namespace

TEST_CASE("an authored cut's Hero Pulse rings out from its hero during its own shot, on pixels",
          "[gpu][waves][authoredcut][adr947]") {
    auto gpuCtx = makeContext();
    gpu::ShaderLibrary shaders(*gpuCtx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*gpuCtx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene scene = groundScene();
    const FrameTime time{.renderTime = 1.0, .deltaTime = 1.0 / 60.0, .frameIndex = 1};
    auto render = [&](const scene::Scene& s) {
        auto image = renderer.renderToImage(s, time, kWidth, kHeight);
        REQUIRE(image.has_value());
        return std::move(*image);
    };
    const gpu::Image8 none = render(scene);

    const std::vector<world::EffectInstance> effects{pulseOn("alpha"), pulseOn("beta")};
    const std::vector<world::HeroPoint> heroes{hero("alpha", glm::vec3(0.0f)), hero("beta", glm::vec3(400.0f, 0.0f, 0.0f))};
    const std::vector<world::ShotSpan> authored = scene::authoredShotSpans(twoShotCut());

    // The frame the engine would draw at `t`, with the effects reading `shots`.
    auto frameAt = [&](double t, std::span<const world::ShotSpan> shots, float* front = nullptr) {
        world::EffectContext ctx;
        ctx.seconds = t;
        ctx.cameraPosition = scene.camera.position;
        ctx.cameraTarget = scene.camera.target;
        ctx.cameraForward = glm::normalize(scene.camera.target - scene.camera.position);
        ctx.shots = shots;
        ctx.heroes = heroes;
        scene::Scene s = scene;
        world::buildWaveFrame(effects, ctx, s.waves);
        if (front != nullptr) {
            const auto r = world::resolveWave(effects[0], ctx);
            *front = r ? r->frontDistance : -1.0f;
        }
        return std::pair{render(s), s.waves.count};
    };

    SECTION("in alpha's shot: a ring about alpha, at the radius its front has reached, and spreading") {
        float frontEarly = 0.0f;
        float frontLate = 0.0f;
        const auto [early, earlyCount] = frameAt(1.2, authored, &frontEarly);
        const auto [late, lateCount] = frameAt(2.2, authored, &frontLate);
        REQUIRE(earlyCount == 1); // alpha's alone: beta is not held
        REQUIRE(lateCount == 1);
        INFO("front at 1.2 s: " << frontEarly << " m, at 2.2 s: " << frontLate << " m");
        REQUIRE(frontEarly > 8.0f);
        REQUIRE(frontLate > frontEarly + 8.0f);
        const std::size_t changed = differingPixels(early, none);
        INFO("pixels changed: " << changed);
        CHECK(changed > kWidth * kHeight / 100);
        // Bright where the front is, about the hero...
        CHECK(meanAtRadius(early, scene, frontEarly) > meanAtRadius(none, scene, frontEarly) + 10.0);
        CHECK(meanAtRadius(late, scene, frontLate) > meanAtRadius(none, scene, frontLate) + 10.0);
        // ...and the ground past the front untouched: the ring is spreading outward from alpha.
        CHECK(meanAtRadius(early, scene, frontLate) < meanAtRadius(none, scene, frontLate) + 2.0);
    }

    SECTION("in beta's shot, alpha's ring is gone: the frame is the frame without effects") {
        const auto [image, count] = frameAt(8.2, authored);
        CHECK(count == 1); // beta's ring, 400 m away
        CHECK(differingPixels(image, none) == 0);
    }

    SECTION("control arm: on the director's spans alone -- none, for an authored cut -- nothing rings") {
        const auto [image, count] = frameAt(1.2, std::span<const world::ShotSpan>{});
        CHECK(count == 0);
        CHECK(differingPixels(image, none) == 0);
    }
}
