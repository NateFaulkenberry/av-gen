// ADR-702 on pixels: several effects on one owner, and several instances of one type, render
// together without taking each other's place.
//
// The question ADR-702 was asked to settle was whether World -> Tornado + Aurora can be active at
// once. The CPU half (every instance's `EffectStatus` is `Drawn`) is in tests/unit; what only a GPU
// can answer is whether each one actually reaches the picture while the other is there. So every
// claim here is a four-arm comparison, ADR-182's shape: neither, A only, B only, both -- and the
// requirement is that A changes the frame WITH B present exactly as it does without, i.e. that
// `both` differs from `B only` and from `A only`. An arm that came back byte-identical to its
// baseline would mean one effect had silently displaced the other, which is the defect class
// ADR-560 found once already (a fog bank and a vortex rendered identically to whichever came first).
//
// The same four-arm shape is then asked of two instances of ONE type -- two Ground Pulses owned by
// two different entities, two fog banks on the World -- because "one effect of a class" is the
// assumption ADR-702 §11 names.

#include "app/engine.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"
#include "world/wave_effect.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 192;
constexpr std::uint32_t kHeight = 108;

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

// One arm's frame, from a fresh temporal history and drawn twice: a renderer reused across arms
// carries the previous arm's history into this one otherwise (ADR-182).
gpu::Image8 render(rendering::SceneRenderer& renderer, const scene::Scene& s, double seconds) {
    FrameTime t{};
    t.renderTime = seconds;
    renderer.resetTemporalHistory();
    auto first = renderer.renderToImage(s, t, kWidth, kHeight);
    REQUIRE(first.has_value());
    auto img = renderer.renderToImage(s, t, kWidth, kHeight);
    REQUIRE(img.has_value());
    return std::move(*img);
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

// A few pixels is noise in principle and a real contribution in practice; the floor is set well
// below what any arm here produces (hundreds to thousands of pixels at 192x108) and well above zero.
constexpr std::size_t kVisible = 40;

struct Arms {
    gpu::Image8 none, a, b, both;
};

// The four-arm requirement, with the numbers in the failure message.
void requireCoexist(const Arms& arms, const char* nameA, const char* nameB) {
    const std::size_t aAlone = differingPixels(arms.none, arms.a);
    const std::size_t bAlone = differingPixels(arms.none, arms.b);
    const std::size_t aOverB = differingPixels(arms.b, arms.both);
    const std::size_t bOverA = differingPixels(arms.a, arms.both);
    INFO(nameA << " alone changes " << aAlone << " px; " << nameB << " alone " << bAlone << " px; "
               << nameA << " with " << nameB << " present " << aOverB << " px; " << nameB << " with "
               << nameA << " present " << bOverA << " px");
    // The controls: each effect on its own is visible, or the comparisons below prove nothing.
    REQUIRE(aAlone > kVisible);
    REQUIRE(bAlone > kVisible);
    // The invariant: each still reaches the picture while the other is there.
    CHECK(aOverB > kVisible);
    CHECK(bOverA > kVisible);
}

// ---- World -> Tornado + Aurora, through the whole engine path ------------------------------------

// A tornado 150 m down the camera's view axis (inside the march's default 200 m reach), about 20 m
// across and 110 m tall, glowing a little so it reads against a dark sky without a key light.
// Placed from the camera rather than at fixed coordinates, so the test does not depend on what a new
// composition's default camera happens to be -- but at a fixed DISTANCE, because every tornado row
// has a hard range and a column scaled to a camera 2.5 m from its target is clamped back up around
// the lens.
world::EffectInstance tornadoFacing(const scene::Camera& camera) {
    world::EffectInstance t = world::makeEffect(world::EffectKind::Tornado, "Tornado");
    const glm::vec3 forward = glm::normalize(camera.target - camera.position);
    const glm::vec3 ahead = camera.position + forward * 150.0f;
    t.tornado.field.base = glm::vec3(ahead.x, ahead.y - 40.0f, ahead.z);
    t.tornado.field.height = 110.0f;
    t.tornado.field.radiusBottom = 8.0f;
    t.tornado.field.radiusMid = 11.0f;
    t.tornado.field.radiusTop = 18.0f;
    t.tornado.density = 0.3f;
    t.tornado.emission = 0.6f;
    t.activation = world::Activation::Always;
    t.timing.fadeIn = 0.0;
    return t;
}

world::EffectInstance loudAurora() {
    world::EffectInstance e = world::makeEffect(world::EffectKind::Aurora, "Aurora");
    e.activation = world::Activation::Always;
    e.timing.fadeIn = 0.0;
    e.aurora.appearance.intensity = 6.0f;
    e.aurora.appearance.opacity = 1.0f;
    e.aurora.appearance.horizonGlow = 1.5f;
    e.aurora.shape.curtainCount = 1.0f;
    e.ground.mode = world::GroundGlow::Off;
    return e;
}

const scene::Scene& sceneAt(app::Engine& engine, double seconds) {
    engine.setViewport(kWidth, kHeight);
    engine.update(FrameTime{seconds, 1.0 / 60.0, static_cast<std::uint64_t>(seconds * 60.0)});
    return engine.scene();
}

void setEnabled(app::Engine& engine, const std::string& id, bool on) {
    auto* p = engine.params().find(world::effectParameterPrefix(id) + "enabled");
    REQUIRE(p != nullptr);
    p->setBaseComponent(0, on ? 1.0f : 0.0f);
}

} // namespace

TEST_CASE("World -> Tornado + Aurora: both are drawn, and each reaches the frame while the other is on",
          "[gpu][effects][acceptance]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    constexpr double kSecond = 2.0;
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    const scene::Camera camera = sceneAt(engine, kSecond).camera;

    // Built exactly as the Add Effect menu builds them: two additions to the World's stack.
    std::vector<world::EffectInstance> effects;
    REQUIRE(world::insertEffect(effects, tornadoFacing(camera)).has_value());
    REQUIRE(world::insertEffect(effects, loudAurora()).has_value());
    REQUIRE(engine.setEffects(effects).has_value());
    const std::string tornado = engine.effects()[0].id;
    const std::string aurora = engine.effects()[1].id;
    REQUIRE(engine.effects()[0].owner.isWorld());
    REQUIRE(engine.effects()[1].owner.isWorld());

    Arms arms;
    setEnabled(engine, tornado, false);
    setEnabled(engine, aurora, false);
    arms.none = render(renderer, sceneAt(engine, kSecond), kSecond);
    setEnabled(engine, aurora, true);
    arms.a = render(renderer, sceneAt(engine, kSecond), kSecond);
    setEnabled(engine, aurora, false);
    setEnabled(engine, tornado, true);
    arms.b = render(renderer, sceneAt(engine, kSecond), kSecond);
    setEnabled(engine, aurora, true);
    const scene::Scene& both = sceneAt(engine, kSecond);
    arms.both = render(renderer, both, kSecond);

    // What the evaluator says, per instance, and what reached the frame blocks.
    CHECK(engine.effectStatus(tornado) == world::EffectStatus::Drawn);
    CHECK(engine.effectStatus(aurora) == world::EffectStatus::Drawn);
    CHECK(both.atmospherics.auroraCount == 1);
    CHECK(both.atmospherics.mediumCount == 1);
    CHECK(both.atmospherics.mediaDropped == 0);
    CHECK(renderer.stats().auroras == 1);
    CHECK(renderer.stats().volume.media == 1);

    requireCoexist(arms, "the aurora", "the tornado");

    SECTION("each responds to its own parameters while both are on") {
        auto* intensity = engine.params().find(world::effectParameterPrefix(aurora) + "intensity");
        REQUIRE(intensity != nullptr);
        intensity->setBaseComponent(0, 1.0f);
        const gpu::Image8 dimAurora = render(renderer, sceneAt(engine, kSecond), kSecond);
        CHECK(differingPixels(arms.both, dimAurora) > kVisible);
        intensity->setBaseComponent(0, 6.0f);

        auto* density = engine.params().find(world::effectParameterPrefix(tornado) + "density");
        REQUIRE(density != nullptr);
        density->setBaseComponent(0, density->baseComponent(0) * 0.1f);
        const gpu::Image8 thinTornado = render(renderer, sceneAt(engine, kSecond), kSecond);
        CHECK(differingPixels(arms.both, thinTornado) > kVisible);
    }

    SECTION("both animate: a later second is a different frame, with both still drawn") {
        const gpu::Image8 later = render(renderer, sceneAt(engine, kSecond + 3.0), kSecond + 3.0);
        CHECK(engine.effectStatus(tornado) == world::EffectStatus::Drawn);
        CHECK(engine.effectStatus(aurora) == world::EffectStatus::Drawn);
        CHECK(differingPixels(arms.both, later) > kVisible);
    }
}

namespace {

// ---- two instances of one type -------------------------------------------------------------------

scene::Scene groundScene() {
    scene::Scene scene;
    const scene::MeshId plane = scene.addMesh(scene::makePlane(160.0f, 1));
    scene::Entity& ground = scene.addEntity("ground", plane);
    ground.material.baseColor = glm::vec4(0.35f, 0.38f, 0.42f, 1.0f);
    ground.material.roughness = 0.9f;
    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    key.intensity = 2.0f;
    scene.addLight(key);
    scene.camera.position = glm::vec3(0.0f, 70.0f, 110.0f);
    scene.camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
    scene.camera.fovYRadians = glm::radians(50.0f);
    scene.environment.backgroundColor = glm::vec3(0.02f, 0.03f, 0.05f);
    return scene;
}

// A Ground Pulse ATTACHED TO an entity, riding it through the `owner` source.
world::EffectInstance pulseOn(const std::string& entity, glm::vec3 color) {
    world::EffectInstance e = world::makeEffect(world::EffectKind::GroundPulse, "Ground Pulse");
    e.id.clear();
    e.owner = world::EffectOwner::entity(entity);
    e.wave.source.kind = world::SourceKind::Owner;
    e.activation = world::Activation::Always;
    e.timing = world::Timing{};
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    e.timing.repeatSeconds = 0.0;
    e.wave.propagation.speed = 10.0f;
    e.wave.propagation.range = 60.0f;
    e.wave.propagation.frontWidth = 4.0f;
    e.wave.propagation.trailLength = 12.0f;
    e.wave.propagation.ringCount = 0.0f;
    e.wave.propagation.verticalExtent = 20.0f;
    e.wave.sparkle.enabled = false;
    e.wave.appearance.color = color;
    e.wave.appearance.intensity = 6.0f;
    e.wave.appearance.edgeIntensity = 9.0f;
    e.wave.response = world::MaterialResponse{1.0f, 1.0f, 1.0f, 0.0f};
    return e;
}

world::WaveFrame waveFrame(const std::vector<world::EffectInstance>& effects,
                           std::span<const world::HeroPoint> heroes, std::vector<world::EffectStatus>& status) {
    world::EffectContext ctx;
    ctx.seconds = 2.5; // fronts 25 m out: two rings that do not yet overlap
    ctx.heroes = heroes;
    status.assign(effects.size(), world::EffectStatus::Dormant);
    world::WaveFrame frame;
    world::buildWaveFrame(effects, ctx, frame, {}, status);
    return frame;
}

} // namespace

TEST_CASE("Two entities, a Ground Pulse each: two instances of one type both reach the frame",
          "[gpu][effects][waves]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    std::vector<world::HeroPoint> heroes(2);
    heroes[0].name = "ufo-a";
    heroes[0].position = glm::vec3(-35.0f, 0.0f, 0.0f);
    heroes[1].name = "ufo-b";
    heroes[1].position = glm::vec3(35.0f, 0.0f, 0.0f);

    std::vector<world::EffectInstance> list;
    REQUIRE(world::insertEffect(list, pulseOn("ufo-a", {0.1f, 1.0f, 0.4f})).has_value());
    REQUIRE(world::insertEffect(list, pulseOn("ufo-b", {1.0f, 0.3f, 0.1f})).has_value());
    REQUIRE(world::validateEffects(list).has_value());
    REQUIRE(list[0].id != list[1].id); // two instances, two identities, two parameter prefixes

    const auto frameWith = [&](bool a, bool b, std::vector<world::EffectStatus>& status) {
        std::vector<world::EffectInstance> copy = list;
        copy[0].enabled = a;
        copy[1].enabled = b;
        scene::Scene s = groundScene();
        s.waves = waveFrame(copy, heroes, status);
        return s;
    };

    std::vector<world::EffectStatus> status;
    Arms arms;
    arms.none = render(renderer, frameWith(false, false, status), 2.5);
    arms.a = render(renderer, frameWith(true, false, status), 2.5);
    arms.b = render(renderer, frameWith(false, true, status), 2.5);
    const scene::Scene both = frameWith(true, true, status);
    CHECK(both.waves.count == 2);
    CHECK(status[0] == world::EffectStatus::Drawn);
    CHECK(status[1] == world::EffectStatus::Drawn);
    // Each ring stands on its own owner, not on a shared origin.
    arms.both = render(renderer, both, 2.5);
    requireCoexist(arms, "ufo-a's pulse", "ufo-b's pulse");
}

TEST_CASE("World -> two fog banks: two placed media of one type are both marched",
          "[gpu][effects][volumetric]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    constexpr double kSecond = 1.0;
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    const scene::Camera camera = sceneAt(engine, kSecond).camera;
    const glm::vec3 look = camera.target - camera.position;
    const float distance = glm::length(look);
    const glm::vec3 right = glm::normalize(glm::cross(look, glm::vec3(0.0f, 1.0f, 0.0f)));

    // Two banks, left and right of where the camera looks, each small enough to leave the other's
    // half of the frame alone.
    const auto bank = [&](float side) {
        world::EffectInstance f = world::makeEffect(world::EffectKind::VolumetricFog, "Fog Bank");
        f.id.clear();
        f.vortex.field.center = camera.target + right * (side * distance * 0.35f);
        f.vortex.field.radius = distance * 0.2f;
        f.vortex.field.thickness = distance * 0.3f;
        f.vortex.density = 4.0f / std::max(distance * 0.2f, 1.0f);
        f.vortex.emission = 1.0f / std::max(distance * 0.2f, 1.0f);
        f.activation = world::Activation::Always;
        f.timing.fadeIn = 0.0;
        return f;
    };
    std::vector<world::EffectInstance> effects;
    REQUIRE(world::insertEffect(effects, bank(-1.0f)).has_value());
    REQUIRE(world::insertEffect(effects, bank(1.0f)).has_value());
    REQUIRE(engine.setEffects(effects).has_value());
    const std::string left = engine.effects()[0].id;
    const std::string rightId = engine.effects()[1].id;
    REQUIRE(left != rightId);

    Arms arms;
    setEnabled(engine, left, false);
    setEnabled(engine, rightId, false);
    arms.none = render(renderer, sceneAt(engine, kSecond), kSecond);
    setEnabled(engine, left, true);
    arms.a = render(renderer, sceneAt(engine, kSecond), kSecond);
    setEnabled(engine, left, false);
    setEnabled(engine, rightId, true);
    arms.b = render(renderer, sceneAt(engine, kSecond), kSecond);
    setEnabled(engine, left, true);
    const scene::Scene& both = sceneAt(engine, kSecond);
    arms.both = render(renderer, both, kSecond);

    CHECK(both.atmospherics.mediumCount == 2);
    CHECK(engine.effectStatus(left) == world::EffectStatus::Drawn);
    CHECK(engine.effectStatus(rightId) == world::EffectStatus::Drawn);
    CHECK(renderer.stats().volume.media == 2);
    requireCoexist(arms, "the left bank", "the right bank");
}

// ---- ADR-702 §21: the migrated Glowmere Valley 2 multicam, World -> Tornado + Aurora ---------------
//
// The acceptance the refactor was asked for, on the owner's own film rather than a fixture: the
// migrated project already carries an aurora on the World (and a Ground Pulse on each of its 16
// heroes); a tornado is added to the World the way the Effects panel adds one, and both have to be
// drawn, each has to reach the frame while the other is on, and the hero pulse the cut is holding at
// that second has to still fire. Nothing is written back to the project.
TEST_CASE("Glowmere Valley 2 multicam: World -> Tornado + Aurora render together, and the hero pulse "
          "still fires",
          "[gpu][effects][acceptance][glowmere]") {
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json";
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    constexpr double kSecond = 20.0; // the cut is holding a hero, its pulse is live (checked below)
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(project).has_value());
    const scene::Camera camera = sceneAt(engine, kSecond).camera;

    // The migrated representation: the World has the aurora; heroes own their pulses.
    const std::size_t auroraAt = world::findEffect(engine.effects(), "aurora");
    REQUIRE(auroraAt < engine.effects().size());
    REQUIRE(engine.effects()[auroraAt].owner.isWorld());
    std::size_t heroPulses = 0;
    for (const world::EffectInstance& e : engine.effects()) {
        heroPulses += e.kind == world::EffectKind::GroundPulse && e.owner.kind == world::EffectTarget::Entity;
    }
    REQUIRE(heroPulses == 16);

    // Add the tornado to the World's stack through the editor's own path.
    std::string tornado;
    REQUIRE(engine
                .editEffects([&](std::vector<world::EffectInstance>& list) -> Result<void> {
                    auto added = world::insertEffect(list, tornadoFacing(camera));
                    if (!added) {
                        return std::unexpected(added.error());
                    }
                    tornado = *added;
                    return {};
                })
                .has_value());
    REQUIRE(world::findEffect(engine.effects(), tornado) < engine.effects().size());

    Arms arms;
    setEnabled(engine, tornado, false);
    setEnabled(engine, "aurora", false);
    arms.none = render(renderer, sceneAt(engine, kSecond), kSecond);
    setEnabled(engine, "aurora", true);
    arms.a = render(renderer, sceneAt(engine, kSecond), kSecond);
    setEnabled(engine, "aurora", false);
    setEnabled(engine, tornado, true);
    arms.b = render(renderer, sceneAt(engine, kSecond), kSecond);
    setEnabled(engine, "aurora", true);
    const scene::Scene& both = sceneAt(engine, kSecond);
    arms.both = render(renderer, both, kSecond);

    CHECK(engine.effectStatus("aurora") == world::EffectStatus::Drawn);
    CHECK(engine.effectStatus(tornado) == world::EffectStatus::Drawn);
    CHECK(both.atmospherics.auroraCount == 1);
    CHECK(both.atmospherics.mediumCount == 1);
    // The cut is on a hero at this second, and exactly that hero's pulse is the one live wave.
    std::size_t drawnPulses = 0;
    for (const world::EffectInstance& e : engine.effects()) {
        if (e.kind == world::EffectKind::GroundPulse) {
            drawnPulses += engine.effectStatus(e.id) == world::EffectStatus::Drawn;
            CHECK(engine.effectStatus(e.id) != world::EffectStatus::Orphaned);
        }
    }
    CHECK(drawnPulses == 1);
    CHECK(both.waves.count >= 1);

    requireCoexist(arms, "the aurora", "the tornado");

    // Both animate: a second later the picture has moved, and both are still drawn.
    const gpu::Image8 later = render(renderer, sceneAt(engine, kSecond + 1.0), kSecond + 1.0);
    CHECK(engine.effectStatus("aurora") == world::EffectStatus::Drawn);
    CHECK(engine.effectStatus(tornado) == world::EffectStatus::Drawn);
    CHECK(differingPixels(arms.both, later) > kVisible);
}
