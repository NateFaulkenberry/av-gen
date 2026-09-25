// SHELL phase 2 on pixels (Effect Library Wave 3): Charge-Up, Light Beam, Halo, Bubble, Portal and
// Reality Tear.
//
// Claims only a GPU can settle:
//   * the GATE: with every phase-2 instance disabled the frame is byte-identical to one with no effect
//     at all -- no shell draw, no proxy, no particle system;
//   * a PORTAL's interior hides what stands behind its plane, and not what stands in front of it;
//   * a HALO's glare fades as its source is hidden (the linear depth at the source, not a per-pixel
//     depth test), and without the depth prepass it cannot, and the builder says so.
//
// With `AVGEN_EFFECT_DUMP=<dir>` every arm is written there as a PNG. The hidden `[shell2-review]`
// cases render the review set: a Portal on the Glowmere valley floor, a Light Beam from the UFO
// saucer, a Bubble over the river, a Reality Tear in the night sky, a Charge-Up and Halos.

#include "app/engine.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/render_quality.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "world/effects/effect_lights.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"
#include "world/effects/shell_frame.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 320;
constexpr std::uint32_t kHeight = 180;

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

gpu::Image8 render(rendering::SceneRenderer& renderer, const scene::Scene& s, double seconds,
                   std::uint32_t w = kWidth, std::uint32_t h = kHeight) {
    FrameTime t{};
    t.renderTime = seconds;
    renderer.resetTemporalHistory();
    auto first = renderer.renderToImage(s, t, w, h);
    REQUIRE(first.has_value());
    auto img = renderer.renderToImage(s, t, w, h);
    REQUIRE(img.has_value());
    return std::move(*img);
}

void dump(const gpu::Image8& image, const std::string& stem) {
    const char* dir = std::getenv("AVGEN_EFFECT_DUMP");
    if (dir == nullptr || dir[0] == '\0') {
        return;
    }
    static_cast<void>(assets::writePng(fs::path(dir) / (stem + ".png"), image.width, image.height, image.rgba));
}

glm::vec2 toPixels(const scene::Camera& camera, const glm::vec3& p, std::uint32_t w = kWidth, std::uint32_t h = kHeight) {
    const glm::vec4 clip = camera.projection(static_cast<float>(w) / static_cast<float>(h)) * camera.view() * glm::vec4(p, 1.0f);
    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
    return {(ndc.x * 0.5f + 0.5f) * static_cast<float>(w), (0.5f - ndc.y * 0.5f) * static_cast<float>(h)};
}

int channelSum(const gpu::Image8& img, glm::vec2 at, int c) {
    const auto x = static_cast<std::uint32_t>(std::clamp(at.x, 0.0f, static_cast<float>(img.width - 1)));
    const auto y = static_cast<std::uint32_t>(std::clamp(at.y, 0.0f, static_cast<float>(img.height - 1)));
    return img.pixel(x, y)[c];
}

// Summed absolute change of every pixel within `radius` px of `at`.
double changeAround(const gpu::Image8& a, const gpu::Image8& b, glm::vec2 at, float radius) {
    double sum = 0.0;
    for (std::uint32_t y = 0; y < a.height; ++y) {
        for (std::uint32_t x = 0; x < a.width; ++x) {
            if (glm::length(glm::vec2(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f) - at) > radius) {
                continue;
            }
            for (int c = 0; c < 3; ++c) {
                sum += std::abs(static_cast<int>(a.pixel(x, y)[c]) - static_cast<int>(b.pixel(x, y)[c]));
            }
        }
    }
    return sum;
}

rendering::QualitySettings withoutPrepass(rendering::QualitySettings q) {
    q.contactShadows = false;
    q.ambientOcclusion = false;
    q.shadowMaskScale = 1.0f;
    return q;
}

// A floor whose top is y = 0, a key light, a dark sky; the camera looks down at the origin.
scene::Scene floorStage() {
    scene::Scene scene;
    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.5f));
    key.intensity = 1.5f;
    scene.addLight(key);
    scene.camera.position = glm::vec3(0.0f, 9.0f, 18.0f);
    scene.camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
    scene.camera.fovYRadians = glm::radians(55.0f);
    scene.environment.backgroundColor = glm::vec3(0.01f, 0.012f, 0.02f);
    const scene::MeshId cube = scene.addMesh(scene::makeCube(1.0f));
    scene::Entity& floor = scene.addEntity("floor", cube);
    floor.transform.position = glm::vec3(0.0f, -0.5f, 0.0f);
    floor.transform.scale = glm::vec3(40.0f, 0.5f, 40.0f);
    floor.material.baseColor = glm::vec4(0.2f, 0.2f, 0.22f, 1.0f);
    return scene;
}

scene::Entity& addBox(scene::Scene& s, const char* name, glm::vec3 at, glm::vec3 half, glm::vec4 color) {
    const scene::MeshId cube = s.addMesh(scene::makeCube(1.0f));
    scene::Entity& e = s.addEntity(name, cube);
    e.transform.position = at;
    e.transform.scale = half;
    e.material.baseColor = color;
    return e;
}

std::vector<world::EffectStatus> buildShells(scene::Scene& scene, const std::vector<world::EffectInstance>& effects,
                                             double seconds, std::vector<std::string>* reasons = nullptr) {
    world::EffectContext ctx;
    ctx.seconds = seconds;
    ctx.cameraPosition = scene.camera.position;
    std::vector<world::EffectStatus> status(effects.size(), world::EffectStatus::Dormant);
    std::vector<std::string> said(effects.size());
    world::EffectLightFrame lights;
    world::buildShellFrame(effects, ctx, scene.shells, lights, {}, status, said);
    scene.entityFx.lights = lights;
    if (reasons != nullptr) {
        *reasons = said;
    }
    return status;
}

world::EffectInstance placed(world::EffectKind kind, const char* id, const char* style) {
    world::EffectInstance e = world::makeEffect(kind, id);
    e.id = id;
    e.owner = world::EffectOwner::world();
    e.activation = world::Activation::Always;
    e.timing = world::Timing{};
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    if (style != nullptr) {
        REQUIRE(world::applyEffectStyle(e, kind, style));
        e.activation = world::Activation::Always;
    }
    return e;
}

// The engine fixture: a craft on a simulated orbit, a camera looking down on it, a floor.
constexpr const char* kScene = R"({ "format": "avgen-scene", "version": 1, "name": "shell2-fixture",
  "camera": { "mode": 1, "position": [0.0, 20.0, 40.0], "target": [0.0, 4.0, 0.0], "fov": 50.0, "orbitSpeed": 0.0 },
  "environment": { "background": [0.01, 0.014, 0.03], "fogColor": [0.02, 0.03, 0.06], "volumeDensity": 0.0067, "volumeMaxDistance": 0.0 },
  "nodes": [ { "kind": "orb", "name": "craft", "position": [0, 6, 0], "scale": [1.2, 0.5, 1.2] },
             { "kind": "orb", "name": "floor", "position": [0, -0.6, 0], "scale": [30.0, 0.15, 30.0] } ],
  "entities": [ { "name": "craft", "node": "craft", "seed": 7,
                  "behaviors": [ { "kind": "orbit", "radius": 10.0, "rate": 30.0, "authority": "simulation" } ] } ] })";

const scene::Scene& frameAfterSeek(app::Engine& engine, double seconds, std::uint32_t w = kWidth, std::uint32_t h = kHeight) {
    engine.setViewport(w, h);
    engine.seekSeconds(seconds);
    const double t = seconds + 1.0 / 60.0;
    engine.update(FrameTime{t, 1.0 / 60.0, static_cast<std::uint64_t>(std::llround(t * 60.0))});
    return engine.scene();
}

void setEnabled(app::Engine& engine, const std::string& id, bool on) {
    auto* p = engine.params().find("fx/" + id + "/enabled");
    REQUIRE(p != nullptr);
    p->setBaseComponent(0, on ? 1.0f : 0.0f);
}

std::size_t effectSystems(const scene::Scene& s) {
    return static_cast<std::size_t>(std::count_if(s.particles.begin(), s.particles.end(), [](const scene::ParticleSystem& p) {
        return std::string_view(p.name).starts_with("fx:");
    }));
}

} // namespace

TEST_CASE("the shell2 gate: six disabled phase-2 types render the same bytes as no effect",
          "[gpu][shell2][effects][gate]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    world::reportShellLinearDepth(true);
    constexpr double kSecond = 2.0;

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.setCompositionJson(nlohmann::json::parse(kScene)).has_value());
    const gpu::Image8 bare = render(renderer, frameAfterSeek(engine, kSecond), kSecond);
    CHECK(renderer.shells().stats().draws == 0);

    std::vector<world::EffectInstance> list;
    std::vector<std::string> ids;
    for (const world::EffectKind k : {world::EffectKind::ChargeUp, world::EffectKind::LightBeam, world::EffectKind::Halo,
                                      world::EffectKind::Bubble, world::EffectKind::Portal, world::EffectKind::RealityTear}) {
        world::EffectInstance e = world::makeEffect(k, world::effectSchema(k)->displayName);
        e.id = std::string("craft-") + world::effectSchema(k)->key;
        e.owner = world::EffectOwner::entity("craft");
        ids.push_back(e.id);
        list.push_back(e);
    }
    REQUIRE(engine.setEffects(list).has_value());
    for (const std::string& id : ids) {
        setEnabled(engine, id, false);
    }
    const scene::Scene& gated = frameAfterSeek(engine, kSecond);
    CHECK(gated.shells.empty());
    CHECK(gated.distortion.count == 0);
    CHECK(effectSystems(gated) == 0);
    for (const std::string& id : ids) {
        INFO(id);
        CHECK(engine.effectStatus(id) == world::EffectStatus::Disabled);
    }
    const gpu::Image8 disabled = render(renderer, gated, kSecond);
    CHECK(renderer.shells().stats().draws == 0);
    REQUIRE(bare.rgba.size() == disabled.rgba.size());
    CHECK(bare.rgba == disabled.rgba); // byte-identical

    // The control: switched on, all six draw and the frame changes.
    for (const std::string& id : ids) {
        setEnabled(engine, id, true);
    }
    const scene::Scene& live = frameAfterSeek(engine, kSecond);
    for (const std::string& id : ids) {
        INFO(id << ": " << engine.effectStatusReason(id));
        const world::EffectStatus s = engine.effectStatus(id);
        CHECK((s == world::EffectStatus::Drawn || s == world::EffectStatus::Partial));
    }
    CHECK(live.distortion.count > 0);
    CHECK(effectSystems(live) > 0);
    const gpu::Image8 on = render(renderer, live, kSecond);
    CHECK(renderer.shells().stats().draws >= 6);
    CHECK(on.rgba != bare.rgba);
    dump(bare, "shell2-gate-bare");
    dump(on, "shell2-gate-on");
}

TEST_CASE("a portal's interior hides what stands behind it, not what stands in front",
          "[gpu][shell2][effects][portal]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    world::reportShellLinearDepth(true);

    // An upright portal at (0, 3, 0) facing the camera; a red box behind its plane, a green one in
    // front of it, both inside its outline on screen.
    const auto stage = [](bool behind) {
        scene::Scene s = floorStage();
        s.camera.position = glm::vec3(0.0f, 3.0f, 14.0f);
        s.camera.target = glm::vec3(0.0f, 3.0f, 0.0f);
        if (behind) {
            addBox(s, "behind", glm::vec3(-0.9f, 3.0f, -2.5f), glm::vec3(0.5f), glm::vec4(0.9f, 0.05f, 0.05f, 1.0f));
        }
        addBox(s, "front", glm::vec3(0.9f, 3.0f, 2.0f), glm::vec3(0.4f), glm::vec4(0.05f, 0.9f, 0.05f, 1.0f));
        return s;
    };
    world::EffectInstance portal = placed(world::EffectKind::Portal, "portal", "Magic Portal");
    portal.values.setFloat("portal/offsetY", 3.0f);
    portal.values.setFloat("portal/radius", 2.4f);
    portal.values.setFloat("portal/light", 0.0f); // this case is about what it hides

    const scene::Scene bare = stage(true);
    scene::Scene opened = stage(true);
    scene::Scene openedEmpty = stage(false);
    REQUIRE(buildShells(opened, {portal}, 2.0)[0] == world::EffectStatus::Drawn);
    REQUIRE(buildShells(openedEmpty, {portal}, 2.0)[0] == world::EffectStatus::Drawn);
    const gpu::Image8 off = render(renderer, bare, 2.0);
    const gpu::Image8 on = render(renderer, opened, 2.0);
    const gpu::Image8 onEmpty = render(renderer, openedEmpty, 2.0);
    dump(off, "shell2-portal-occlusion-off");
    dump(on, "shell2-portal-occlusion-on");
    const glm::vec2 behind = toPixels(bare.camera, glm::vec3(-0.9f, 3.0f, -2.0f));
    const glm::vec2 front = toPixels(bare.camera, glm::vec3(0.9f, 3.0f, 2.4f));
    // The control: the red box is plainly visible without the portal.
    INFO("behind at " << behind.x << "," << behind.y << ": off rgb " << channelSum(off, behind, 0) << ","
                      << channelSum(off, behind, 1) << "," << channelSum(off, behind, 2));
    REQUIRE(channelSum(off, behind, 0) > channelSum(off, behind, 1) + 40);
    // With the portal, the box behind it makes no difference at all: the interior covers it.
    double hidden = 0.0;
    for (int c = 0; c < 3; ++c) {
        INFO("channel " << c << ": with the box " << channelSum(on, behind, c) << ", without " << channelSum(onEmpty, behind, c));
        CHECK(std::abs(channelSum(on, behind, c) - channelSum(onEmpty, behind, c)) <= 3);
    }
    hidden = changeAround(on, onEmpty, behind, 12.0f);
    INFO("summed change round the hidden box: " << hidden);
    CHECK(hidden < 300.0);
    // The green box in front is untouched.
    INFO("front at " << front.x << "," << front.y);
    for (int c = 0; c < 3; ++c) {
        CHECK(std::abs(channelSum(on, front, c) - channelSum(off, front, c)) <= 6);
    }
}

TEST_CASE("a halo's glare fades as its source is hidden, and needs the prepass to know it",
          "[gpu][shell2][effects][halo][depth]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    world::reportShellLinearDepth(true);
    const rendering::QualitySettings full = renderer.qualitySettings();

    world::EffectInstance glare = placed(world::EffectKind::Halo, "glare", "Lamp Glare");
    glare.values.setFloat("halo/offsetY", 2.0f);
    glare.values.setFloat("halo/radius", 3.0f);
    const auto stage = [](bool wall) {
        scene::Scene s = floorStage();
        if (wall) {
            // A thin wall between the camera and the source, well in front of it.
            addBox(s, "wall", glm::vec3(0.0f, 3.0f, 8.0f), glm::vec3(2.0f, 3.0f, 0.1f), glm::vec4(0.2f, 0.2f, 0.2f, 1.0f));
        }
        return s;
    };
    const glm::vec2 at = toPixels(floorStage().camera, glm::vec3(0.0f, 2.0f, 0.0f));
    const auto measure = [&](bool wall) {
        const scene::Scene bare = stage(wall);
        scene::Scene lit = stage(wall);
        REQUIRE(buildShells(lit, {glare}, 1.0)[0] == world::EffectStatus::Drawn);
        const gpu::Image8 off = render(renderer, bare, 1.0);
        const gpu::Image8 on = render(renderer, lit, 1.0);
        dump(on, std::string("shell2-glare-") + (wall ? "hidden" : "open") +
                     (renderer.shells().stats().linearDepth ? "" : "-noprepass"));
        return changeAround(off, on, at, 60.0f);
    };
    const double open = measure(false);
    const double hidden = measure(true);
    INFO("glare change round the source: open " << open << ", behind the wall " << hidden);
    CHECK(open > 20000.0); // the control: the glare is there
    CHECK(hidden < open * 0.1);

    // Without the prepass the glare cannot know it is hidden: it draws, and the builder says so.
    renderer.setQualitySettings(withoutPrepass(full));
    const double blind = measure(true);
    INFO("behind the wall without the prepass: " << blind);
    CHECK(blind > open * 0.5);
    std::vector<std::string> reasons;
    scene::Scene next = floorStage();
    CHECK(buildShells(next, {glare}, 1.0, &reasons)[0] == world::EffectStatus::Partial);
    CHECK(reasons[0].find("depth prepass") != std::string::npos);
    renderer.setQualitySettings(full);
    world::reportShellLinearDepth(true);
}

// ---- the review set (hidden) -------------------------------------------------------------------
//
// Run with `AVGEN_EFFECT_DUMP=<dir> avgen_render_tests "[shell2-review]"`.

namespace {

void addEffect(app::Engine& engine, world::EffectInstance e) {
    const std::string id = e.id;
    REQUIRE(engine
                .editEffects([&](std::vector<world::EffectInstance>& list) -> Result<void> {
                    list.push_back(e);
                    world::normaliseEffectOrder(list);
                    return {};
                })
                .has_value());
    static_cast<void>(engine.addDefaultEffectRoutes(id));
}

void setAll(app::Engine& engine, const std::vector<std::string>& ids, bool on) {
    for (const std::string& id : ids) {
        setEnabled(engine, id, on);
    }
}

float envFloat(const char* name, float fallback) {
    const char* v = std::getenv(name);
    return v != nullptr ? static_cast<float>(std::atof(v)) : fallback;
}

// Renders `ids` off at `second`, then on: played from `warm` seconds before (so particles are in the
// air) and shot on the last three frames. `camera` overrides the directed one when given.
void reviewArms(app::Engine& engine, rendering::SceneRenderer& renderer, const std::vector<std::string>& ids,
                double second, const std::string& stem, const scene::Camera* camera, std::uint32_t w, std::uint32_t h,
                double warm = 1.5) {
    const auto shoot = [&](double t, const std::string& name) {
        engine.update(FrameTime{t, 1.0 / 60.0, static_cast<std::uint64_t>(std::llround(t * 60.0))});
        if (name.empty()) {
            return;
        }
        scene::Scene s = engine.scene();
        if (camera != nullptr) {
            s.camera = *camera;
        }
        dump(render(renderer, s, t, w, h), name);
    };
    // Both arms are PLAYED from the same second, so the directed camera and the film's characters are
    // where they are in a play, and the only difference between them is the effect.
    const double from = std::max(second - warm, 0.0);
    const auto frames = static_cast<int>(std::llround((second - from) * 60.0));
    setAll(engine, ids, false);
    engine.seekSeconds(from);
    for (int f = 1; f <= frames + 1; ++f) {
        shoot(from + f / 60.0, f > frames ? stem + "-off" : std::string());
    }
    setAll(engine, ids, true);
    engine.seekSeconds(from);
    for (int f = 1; f <= frames + 3; ++f) {
        const double t = from + f / 60.0;
        shoot(t, f > frames ? stem + "-on-f" + std::to_string(f - frames) : std::string());
    }
    for (const std::string& id : ids) {
        INFO(id << ": " << engine.effectStatusReason(id));
        std::printf("  %s: status %d  %s\n", id.c_str(), static_cast<int>(engine.effectStatus(id)),
                    std::string(engine.effectStatusReason(id)).c_str());
        CHECK((engine.effectStatus(id) == world::EffectStatus::Drawn || engine.effectStatus(id) == world::EffectStatus::Partial));
    }
}

void glowmereNight(app::Engine& engine, float phase) {
    auto* enabled = engine.params().findAs<bool>("env/dayNight/enabled");
    auto* paused = engine.params().findAs<bool>("env/dayNight/paused");
    auto* dayPhase = engine.params().findAs<float>("env/dayNight/dayPhase");
    REQUIRE(enabled != nullptr);
    REQUIRE(paused != nullptr);
    REQUIRE(dayPhase != nullptr);
    enabled->setBase(true);
    paused->setBase(true);
    dayPhase->setBase(phase);
}

struct Glowmere {
    std::unique_ptr<gpu::Context> ctx;
    std::unique_ptr<gpu::ShaderLibrary> shaders;
    std::unique_ptr<rendering::SceneRenderer> renderer;
    app::Engine engine{app::EngineMode::Offline};
};

void openProject(Glowmere& g, const fs::path& project, std::uint32_t w, std::uint32_t h) {
    g.ctx = makeContext();
    g.shaders = std::make_unique<gpu::ShaderLibrary>(*g.ctx, std::vector<fs::path>{fs::path(AVGEN_SHADER_SOURCE_DIR)});
    g.renderer = std::make_unique<rendering::SceneRenderer>(*g.ctx, *g.shaders);
    REQUIRE(g.renderer->init().has_value());
    // Each review frame is rendered fresh (temporal history reset), so the particle pools are pre-rolled
    // (ADR-360) to show the motes, shards and droplets in the air rather than an empty pool.
    g.renderer->setParticleWarmUpFrames(90);
    world::reportShellLinearDepth(true);
    REQUIRE(g.engine.loadProject(project).has_value());
    g.engine.setViewport(w, h);
}

float groundAt(const scene::Scene& s, glm::vec3 p) {
    return s.terrainGround.valid() ? s.terrainGround.groundAt(glm::vec2(p.x, p.z)) : 0.0f;
}

// The water nearest `near` in plan: every water vertex above the ground.
glm::vec3 riverNear(const scene::Scene& s, glm::vec3 near) {
    glm::vec3 best(0.0f);
    float bestD = 1e30f;
    for (const scene::Entity& entity : s.entities) {
        if (entity.style != scene::MeshStyle::Water || entity.mesh == scene::kInvalidMesh) {
            continue;
        }
        const glm::mat4 m = entity.transform.matrix();
        for (const scene::Vertex& v : s.meshes[entity.mesh].vertices) {
            const glm::vec3 w = glm::vec3(m * glm::vec4(v.position, 1.0f));
            if (s.terrainGround.valid() && s.terrainGround.groundAt(glm::vec2(w.x, w.z)) >= w.y - 0.3f) {
                continue;
            }
            const float d = glm::length(glm::vec2(w.x - near.x, w.z - near.z));
            if (d < bestD) {
                bestD = d;
                best = w;
            }
        }
    }
    return best;
}

constexpr std::uint32_t kW = 960;
constexpr std::uint32_t kH = 540;

} // namespace

namespace {

// A camera beside the saucer at `second`: back from it along the directed camera's heading, a little
// above, aimed `drop` metres below its centre. Also returns its drawn centre and half-extents.
scene::Camera besideSaucer(app::Engine& engine, double second, float back, float up, float drop, glm::vec3& centre,
                           glm::vec3& half) {
    const scene::Scene& s = frameAfterSeek(engine, second, kW, kH);
    world::NodeView view;
    REQUIRE(engine.composition() != nullptr);
    REQUIRE(engine.composition()->nodeView("visitor", view));
    centre = 0.5f * (view.boundsMin + view.boundsMax);
    half = 0.5f * (view.boundsMax - view.boundsMin);
    glm::vec3 heading = centre - s.camera.position;
    heading.y = 0.0f;
    heading = glm::normalize(heading);
    scene::Camera camera = s.camera;
    camera.position = centre - heading * back + glm::vec3(0.0f, up, 0.0f);
    camera.target = centre - glm::vec3(0.0f, drop, 0.0f);
    return camera;
}

// The river's local course near `at`: the principal axis of the water within 20 m, in plan.
glm::vec2 riverCourse(const scene::Scene& s, glm::vec3 at) {
    glm::vec2 mean(0.0f);
    std::vector<glm::vec2> near;
    for (const scene::Entity& entity : s.entities) {
        if (entity.style != scene::MeshStyle::Water || entity.mesh == scene::kInvalidMesh) {
            continue;
        }
        const glm::mat4 m = entity.transform.matrix();
        for (const scene::Vertex& v : s.meshes[entity.mesh].vertices) {
            const glm::vec3 w = glm::vec3(m * glm::vec4(v.position, 1.0f));
            if (s.terrainGround.valid() && s.terrainGround.groundAt(glm::vec2(w.x, w.z)) >= w.y - 0.3f) {
                continue;
            }
            if (glm::length(glm::vec2(w.x - at.x, w.z - at.z)) < 20.0f) {
                near.emplace_back(w.x, w.z);
                mean += glm::vec2(w.x, w.z);
            }
        }
    }
    mean /= static_cast<float>(std::max<std::size_t>(near.size(), 1));
    float sxx = 0.0f;
    float sxz = 0.0f;
    float szz = 0.0f;
    for (const glm::vec2& w : near) {
        const glm::vec2 d = w - mean;
        sxx += d.x * d.x;
        sxz += d.x * d.y;
        szz += d.y * d.y;
    }
    const float course = 0.5f * std::atan2(2.0f * sxz, sxx - szz);
    return {std::cos(course), std::sin(course)};
}

} // namespace

TEST_CASE("VISUAL a Portal opening on the Glowmere valley floor", "[.visual][shell2-review][portal]") {
    if (std::getenv("AVGEN_EFFECT_DUMP") == nullptr) {
        SKIP("set AVGEN_EFFECT_DUMP to a directory to write the review frames");
    }
    Glowmere g;
    openProject(g, fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json", kW, kH);
    glowmereNight(g.engine, envFloat("AVGEN_SHELL_PHASE", 0.02f));
    const double second = envFloat("AVGEN_SHELL_SECOND", 40.0f);
    const scene::Scene& s = frameAfterSeek(g.engine, second, kW, kH);
    const scene::Camera camera = s.camera;
    glm::vec3 fwd = camera.target - camera.position;
    fwd.y = 0.0f;
    fwd = glm::normalize(fwd);
    const float radius = envFloat("AVGEN_SHELL_RADIUS", 2.2f);
    constexpr float kAspect = 1.25f;
    glm::vec3 at = camera.position + fwd * envFloat("AVGEN_SHELL_AHEAD", 16.0f);
    at.y = groundAt(s, at) + radius * kAspect * 0.97f; // its foot just into the ground
    const glm::vec3 toCam = camera.position - at;
    // The camera stands back from it, a little above, looking at its middle.
    scene::Camera view = camera;
    view.target = at;
    for (const char* look : {"Magic Portal", "Sci-Fi Gate", "Rift to Space"}) {
        world::EffectInstance portal = world::makeEffect(world::EffectKind::Portal, "Portal");
        portal.id = std::string("valley-portal-") + (look[0] == 'M' ? "magic" : look[1] == 'c' ? "gate" : "rift");
        portal.owner = world::EffectOwner::world();
        REQUIRE(world::applyEffectStyle(portal, world::EffectKind::Portal, look));
        portal.values.setFloat("portal/offsetX", at.x);
        portal.values.setFloat("portal/offsetY", at.y);
        portal.values.setFloat("portal/offsetZ", at.z);
        portal.values.setFloat("portal/radius", radius);
        portal.values.setFloat("portal/aspect", kAspect);
        portal.values.setFloat("portal/yaw", glm::degrees(std::atan2(toCam.x, toCam.z)) + 20.0f);
        addEffect(g.engine, portal);
        reviewArms(g.engine, *g.renderer, {portal.id}, second, portal.id + "-" + std::to_string(static_cast<int>(second)),
                   &view, kW, kH);
        setEnabled(g.engine, portal.id, false);
        if (look[0] == 'M') {
            // Opening: part-way through its fade-in.
            world::EffectInstance opening = portal;
            opening.id = "valley-portal-opening";
            opening.activation = world::Activation::Window;
            opening.timing.windowStart = second - 0.5;
            opening.timing.windowSeconds = 10.0;
            opening.timing.fadeIn = 1.2;
            addEffect(g.engine, opening);
            reviewArms(g.engine, *g.renderer, {opening.id}, second, "valley-portal-opening", &view, kW, kH, 0.5);
            setEnabled(g.engine, opening.id, false);
        }
    }
}

TEST_CASE("VISUAL a Light Beam from the UFO saucer, and a Charge-Up and a ring on it", "[.visual][shell2-review][beam]") {
    if (std::getenv("AVGEN_EFFECT_DUMP") == nullptr) {
        SKIP("set AVGEN_EFFECT_DUMP to a directory to write the review frames");
    }
    Glowmere g;
    openProject(g, fs::path(AVGEN_SOURCE_DIR) / "examples" / "effects" / "ufo-stack.json", kW, kH);
    const double second = envFloat("AVGEN_SHELL_SECOND", 26.0f);
    glm::vec3 centre;
    glm::vec3 half;
    // The directed camera, then one beside the saucer looking down its beam.
    world::EffectInstance beam = world::makeEffect(world::EffectKind::LightBeam, "Light Beam");
    beam.id = "visitor-beam";
    beam.owner = world::EffectOwner::entity("visitor");
    REQUIRE(world::applyEffectStyle(beam, world::EffectKind::LightBeam, "UFO Tractor"));
    beam.values.setFloat("lightBeam/length", envFloat("AVGEN_SHELL_LENGTH", 30.0f));
    addEffect(g.engine, beam);
    reviewArms(g.engine, *g.renderer, {beam.id}, second, "beam-ufo-" + std::to_string(static_cast<int>(second)), nullptr,
               kW, kH);
    const scene::Camera side = besideSaucer(g.engine, second, envFloat("AVGEN_SHELL_BACK", 38.0f), 4.0f, 9.0f, centre, half);
    std::printf("saucer at (%.1f, %.1f, %.1f), half (%.1f, %.1f, %.1f)\n", static_cast<double>(centre.x),
                static_cast<double>(centre.y), static_cast<double>(centre.z), static_cast<double>(half.x),
                static_cast<double>(half.y), static_cast<double>(half.z));
    reviewArms(g.engine, *g.renderer, {beam.id}, second, "beam-ufo-side", &side, kW, kH);
    setEnabled(g.engine, beam.id, false);

    // A Charge-Up gathering under the hull, near full charge at `second`, and 0.15 s into its release.
    const scene::Camera close = besideSaucer(g.engine, second, 3.2f * std::max(half.x, half.z), 1.0f, half.y + 2.0f,
                                             centre, half);
    world::EffectInstance charge = world::makeEffect(world::EffectKind::ChargeUp, "Charge-Up");
    charge.id = "visitor-charge";
    charge.owner = world::EffectOwner::entity("visitor");
    charge.timing.trigger.phase = second - 2.3;
    charge.values.setFloat("chargeUp/radius", 7.0f);
    charge.values.setFloat("chargeUp/coreRadius", 1.1f);
    charge.values.setFloat("chargeUp/offsetY", -(half.y + 2.2f));
    addEffect(g.engine, charge);
    reviewArms(g.engine, *g.renderer, {charge.id}, second, "charge-ufo-" + std::to_string(static_cast<int>(second)), &close,
               kW, kH, 2.2);
    setEnabled(g.engine, charge.id, false);
    // The same moment, a third of the way through a release (its charge began 3.15 s before).
    world::EffectInstance released = charge;
    released.id = "visitor-charge-release";
    released.timing.trigger.phase = second - 3.15;
    addEffect(g.engine, released);
    reviewArms(g.engine, *g.renderer, {released.id}, second, "charge-ufo-release", &close, kW, kH, 3.3);
    setEnabled(g.engine, released.id, false);

    // A ring floating over the dome.
    world::EffectInstance halo = world::makeEffect(world::EffectKind::Halo, "Halo");
    halo.id = "visitor-halo-ring";
    halo.owner = world::EffectOwner::entity("visitor");
    REQUIRE(world::applyEffectStyle(halo, world::EffectKind::Halo, "Saint Ring"));
    halo.values.setFloat("halo/radius", 0.6f * std::max(half.x, half.z));
    halo.values.setFloat("halo/thickness", 0.14f);
    halo.values.setFloat("halo/glowWidth", 0.35f);
    halo.values.setFloat("halo/height", 1.5f);
    addEffect(g.engine, halo);
    const scene::Camera over = besideSaucer(g.engine, second, 3.0f * std::max(half.x, half.z), half.y + 22.0f, -half.y,
                                            centre, half);
    reviewArms(g.engine, *g.renderer, {halo.id}, second, "halo-ring-ufo", &over, kW, kH);
}

TEST_CASE("VISUAL Halo glare round glowing orbs over Glowmere at night", "[.visual][shell2-review][halo]") {
    if (std::getenv("AVGEN_EFFECT_DUMP") == nullptr) {
        SKIP("set AVGEN_EFFECT_DUMP to a directory to write the review frames");
    }
    Glowmere g;
    openProject(g, fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json", kW, kH);
    glowmereNight(g.engine, envFloat("AVGEN_SHELL_PHASE", 0.02f));
    const double second = envFloat("AVGEN_SHELL_SECOND", 40.0f);
    const scene::Camera camera = frameAfterSeek(g.engine, second, kW, kH).camera;
    const glm::vec3 forward = glm::normalize(camera.target - camera.position);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    std::vector<std::string> ids;
    const float distances[] = {10.0f, 18.0f};
    const float across[] = {-3.0f, 3.5f};
    for (int i = 0; i < 2; ++i) {
        const glm::vec3 at = camera.position + forward * distances[i] + right * across[i] + glm::vec3(0.0f, 1.5f, 0.0f);
        world::EffectInstance orb = world::makeEffect(world::EffectKind::Plasma, "Plasma");
        orb.id = "lamp-orb-" + std::to_string(i);
        orb.owner = world::EffectOwner::world();
        orb.values.setFloat("plasma/offsetX", at.x);
        orb.values.setFloat("plasma/offsetY", at.y);
        orb.values.setFloat("plasma/offsetZ", at.z);
        orb.values.setFloat("plasma/radius", 0.35f);
        addEffect(g.engine, orb);
        world::EffectInstance glare = world::makeEffect(world::EffectKind::Halo, "Halo");
        glare.id = "lamp-glare-" + std::to_string(i);
        glare.owner = world::EffectOwner::world();
        REQUIRE(world::applyEffectStyle(glare, world::EffectKind::Halo, i == 0 ? "Lamp Glare" : "Moon Corona"));
        glare.values.setFloat("halo/offsetX", at.x);
        glare.values.setFloat("halo/offsetY", at.y);
        glare.values.setFloat("halo/offsetZ", at.z);
        glare.values.setFloat("halo/occlusionSlack", 0.4f);
        if (i == 1) {
            glare.values.setFloat("halo/radius", 6.0f); // degrees: a corona
        } else {
            glare.values.setFloat("halo/radius", 1.6f);
        }
        addEffect(g.engine, glare);
        ids.push_back(glare.id);
    }
    reviewArms(g.engine, *g.renderer, ids, second, "halo-glare-glowmere", &camera, kW, kH);
}

TEST_CASE("VISUAL a Bubble over the Glowmere river", "[.visual][shell2-review][bubble]") {
    if (std::getenv("AVGEN_EFFECT_DUMP") == nullptr) {
        SKIP("set AVGEN_EFFECT_DUMP to a directory to write the review frames");
    }
    Glowmere g;
    openProject(g, fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json", kW, kH);
    glowmereNight(g.engine, envFloat("AVGEN_SHELL_PHASE", 0.5f));
    const double second = envFloat("AVGEN_SHELL_SECOND", 40.0f);
    const scene::Scene& s = frameAfterSeek(g.engine, second, kW, kH);
    const glm::vec3 eye = s.camera.position;
    const glm::vec3 look = eye + glm::normalize(s.camera.target - eye) * envFloat("AVGEN_SHELL_AHEAD", 18.0f);
    const glm::vec3 water = riverNear(s, look);
    const glm::vec2 flow = riverCourse(s, water);
    const float radius = envFloat("AVGEN_SHELL_RADIUS", 1.2f);
    const glm::vec3 at = water + glm::vec3(0.0f, envFloat("AVGEN_SHELL_UP", 1.8f), 0.0f);
    const glm::vec3 along(flow.x, 0.0f, flow.y);
    std::vector<std::string> ids;
    const char* looks[] = {"Soap Bubble", "Water Orb", "Soap Bubble"};
    const float offsets[] = {0.0f, 3.2f, -2.4f};
    const float lift[] = {0.0f, 0.5f, 1.4f};
    const float sizes[] = {1.0f, 0.75f, 0.45f};
    for (int i = 0; i < 3; ++i) {
        world::EffectInstance bubble = world::makeEffect(world::EffectKind::Bubble, "Bubble");
        bubble.id = std::string("river-bubble-") + std::to_string(i);
        bubble.owner = world::EffectOwner::world();
        REQUIRE(world::applyEffectStyle(bubble, world::EffectKind::Bubble, looks[i]));
        const glm::vec3 p = at + along * offsets[i] + glm::vec3(0.0f, lift[i], 0.0f);
        bubble.values.setFloat("bubble/offsetX", p.x);
        bubble.values.setFloat("bubble/offsetY", p.y);
        bubble.values.setFloat("bubble/offsetZ", p.z);
        bubble.values.setFloat("bubble/radius", radius * sizes[i]);
        addEffect(g.engine, bubble);
        ids.push_back(bubble.id);
    }
    // Over the water, up or down the river from the bubbles (whichever way the channel is clear),
    // looking along it at them: nothing on the banks stands between.
    scene::Camera camera = s.camera;
    const float back = envFloat("AVGEN_SHELL_BACK", 9.0f);
    glm::vec3 stand = at + along * back;
    const glm::vec3 other = at - along * back;
    const auto clearance = [&](glm::vec3 p) { return water.y - groundAt(s, p); };
    if (clearance(other) > clearance(stand)) {
        stand = other;
    }
    camera.position = glm::vec3(stand.x, std::max(water.y, groundAt(s, stand)) + envFloat("AVGEN_SHELL_EYE", 3.5f), stand.z);
    camera.target = at + glm::vec3(0.0f, 0.2f, 0.0f);
    std::printf("bubbles at (%.1f, %.1f, %.1f), camera (%.1f, %.1f, %.1f), water %.1f\n", static_cast<double>(at.x),
                static_cast<double>(at.y), static_cast<double>(at.z), static_cast<double>(camera.position.x),
                static_cast<double>(camera.position.y), static_cast<double>(camera.position.z), static_cast<double>(water.y));
    reviewArms(g.engine, *g.renderer, ids, second, "bubble-river-" + std::to_string(static_cast<int>(second)), &camera, kW, kH);

    // The pop: a trigger 0.05 s before the shot.
    world::EffectInstance pop = world::makeEffect(world::EffectKind::Bubble, "Bubble");
    pop.id = "river-bubble-pop";
    pop.owner = world::EffectOwner::world();
    pop.values.setFloat("bubble/offsetX", at.x);
    pop.values.setFloat("bubble/offsetY", at.y);
    pop.values.setFloat("bubble/offsetZ", at.z);
    pop.values.setFloat("bubble/radius", radius);
    pop.activation = world::Activation::Trigger;
    pop.timing.trigger.source = world::TriggerSource::Repeat;
    pop.timing.trigger.period = 30.0;
    pop.timing.trigger.phase = second - 0.05;
    setAll(g.engine, ids, false);
    addEffect(g.engine, pop);
    reviewArms(g.engine, *g.renderer, {pop.id}, second, "bubble-river-pop", &camera, kW, kH, 0.6);
}

TEST_CASE("VISUAL a Reality Tear in the Glowmere night sky", "[.visual][shell2-review][tear]") {
    if (std::getenv("AVGEN_EFFECT_DUMP") == nullptr) {
        SKIP("set AVGEN_EFFECT_DUMP to a directory to write the review frames");
    }
    Glowmere g;
    openProject(g, fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json", kW, kH);
    glowmereNight(g.engine, envFloat("AVGEN_SHELL_PHASE", 0.02f));
    const double second = envFloat("AVGEN_SHELL_SECOND", 40.0f);
    const scene::Scene& s = frameAfterSeek(g.engine, second, kW, kH);
    glm::vec3 fwd = s.camera.target - s.camera.position;
    fwd.y = 0.0f;
    fwd = glm::normalize(fwd);
    // Looking up into the sky over the valley.
    scene::Camera camera = s.camera;
    camera.target = camera.position + fwd * 100.0f + glm::vec3(0.0f, envFloat("AVGEN_SHELL_PITCH", 18.0f), 0.0f);
    const float ahead = envFloat("AVGEN_SHELL_AHEAD", 80.0f);
    const glm::vec3 at = camera.position + fwd * ahead + glm::vec3(0.0f, envFloat("AVGEN_SHELL_UP", 18.0f), 0.0f);
    const glm::vec3 toCam = camera.position - at;
    for (const char* look : {"Void Rift", "Glitch Tear", "Crystal Crack"}) {
        world::EffectInstance tear = world::makeEffect(world::EffectKind::RealityTear, "Reality Tear");
        tear.id = std::string("sky-tear-") + (look[0] == 'V' ? "void" : look[0] == 'G' ? "glitch" : "crystal");
        tear.owner = world::EffectOwner::world();
        REQUIRE(world::applyEffectStyle(tear, world::EffectKind::RealityTear, look));
        const float scale = envFloat("AVGEN_SHELL_LENGTH", 30.0f) / 6.0f; // the looks are authored at 6 m
        tear.values.setFloat("realityTear/offsetX", at.x);
        tear.values.setFloat("realityTear/offsetY", at.y);
        tear.values.setFloat("realityTear/offsetZ", at.z);
        tear.values.setFloat("realityTear/length", 6.0f * scale);
        for (const char* leaf : {"width", "edgeWidth", "shear"}) { // each look sets these
            const std::string key = std::string("realityTear/") + leaf;
            tear.values.setFloat(key, tear.values.getFloat(key, 0.1f) * scale);
        }
        tear.values.setFloat("realityTear/coreWidth", 0.02f * scale);
        tear.values.setFloat("realityTear/shearBand", 1.2f * scale);
        tear.values.setFloat("realityTear/roll", 14.0f);
        tear.values.setFloat("realityTear/yaw", glm::degrees(std::atan2(toCam.x, toCam.z)));
        addEffect(g.engine, tear);
        reviewArms(g.engine, *g.renderer, {tear.id}, second, tear.id + "-" + std::to_string(static_cast<int>(second)),
                   &camera, kW, kH);
        setEnabled(g.engine, tear.id, false);
    }
}
