// SHELL on pixels (Effect Library Wave 3) and the three types drawn through it.
//
// Claims only a GPU can settle:
//   * the GATE: with no live shell the frame is byte-identical -- a disabled Energy Shield renders
//     the same bytes as a scene with no effect, and the renderer issues no shell draw;
//   * the GROUND LINE lies where the shell meets the floor: a shield sunk into a floor, with every
//     term but the intersection glow off, changes pixels only along the projected circle where the
//     sphere cuts the floor plane;
//   * the PREPASS-OFF FALLBACK: without this frame's linear depth the ground line is off -- even
//     with last frame's depth still in the target -- and the builder says so on the next frame;
//   * a plasma orb is DEPTH-TESTED (a wall in front hides it, and only there) and WRITES EMISSION;
//   * the CAPACITY: 131 walls draw 128 shells in one instanced draw.
//
// With `AVGEN_EFFECT_DUMP=<dir>` every arm is written there as a PNG. The hidden `[shell-review]`
// cases render the review set on the Glowmere film.

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

#include <glm/gtc/constants.hpp>

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

bool pixelDiffers(const gpu::Image8& a, const gpu::Image8& b, std::uint32_t x, std::uint32_t y, int threshold = 24) {
    int sum = 0;
    for (int c = 0; c < 3; ++c) {
        sum += std::abs(static_cast<int>(a.pixel(x, y)[c]) - static_cast<int>(b.pixel(x, y)[c]));
    }
    return sum > threshold;
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

float distanceToPolyline(glm::vec2 p, const std::vector<glm::vec2>& line) {
    float best = 1e9f;
    for (std::size_t i = 0; i + 1 < line.size(); ++i) {
        const glm::vec2 a = line[i];
        const glm::vec2 ab = line[i + 1] - a;
        const float t = glm::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
        best = std::min(best, glm::length(p - (a + ab * t)));
    }
    return best;
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

// Builds `effects` into `scene.shells` as the engine's evaluator would (World owners need no scene
// query; statuses are returned).
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

// A shield sunk into the floor with every term but the ground line switched off.
world::EffectInstance groundLineOnly(float radius) {
    world::EffectInstance e = world::makeEffect(world::EffectKind::EnergyShield, "line");
    e.id = "line";
    e.owner = world::EffectOwner::world();
    e.activation = world::Activation::Always;
    e.timing = world::Timing{};
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    const auto set = [&](const char* leaf, float v) { e.values.setFloat(std::string("energyShield/") + leaf, v); };
    set("radius", radius);
    set("rimIntensity", 0.0f);
    set("pattern", 0.0f);
    set("idleReveal", 0.0f);
    set("hitIntensity", 0.0f);
    set("reflection", 0.0f);
    set("intersectWidth", 0.3f);
    set("intersectIntensity", 6.0f);
    return e;
}

std::vector<glm::vec2> projectedCircle(const scene::Camera& camera, float radius) {
    std::vector<glm::vec2> out;
    for (int i = 0; i <= 256; ++i) {
        const float a = static_cast<float>(i) / 256.0f * glm::two_pi<float>();
        out.push_back(toPixels(camera, glm::vec3(std::cos(a) * radius, 0.0f, std::sin(a) * radius)));
    }
    return out;
}

struct LineCount {
    std::size_t changed = 0;
    std::size_t onLine = 0;
};
LineCount countAlongCircle(const gpu::Image8& off, const gpu::Image8& on, const std::vector<glm::vec2>& circle) {
    LineCount c;
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            if (!pixelDiffers(off, on, x, y)) {
                continue;
            }
            ++c.changed;
            const glm::vec2 p(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
            c.onLine += distanceToPolyline(p, circle) < 5.0f ? 1u : 0u;
        }
    }
    return c;
}

rendering::QualitySettings withoutPrepass(rendering::QualitySettings q) {
    q.contactShadows = false;
    q.ambientOcclusion = false;
    q.shadowMaskScale = 1.0f;
    return q;
}

} // namespace

TEST_CASE("the shell ground line lies where the shell meets the floor", "[gpu][shell][effects][depth]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    // The renderer's depth report is process-wide (a status sentence one frame late); a case that
    // rendered without the prepass before this one would otherwise make this one's first build Partial.
    world::reportShellLinearDepth(true);

    constexpr float kRadius = 4.0f;
    const scene::Scene bare = floorStage();
    scene::Scene shielded = floorStage();
    const auto status = buildShells(shielded, {groundLineOnly(kRadius)}, 1.0);
    REQUIRE(status[0] == world::EffectStatus::Drawn);
    const gpu::Image8 off = render(renderer, bare, 1.0);
    const gpu::Image8 on = render(renderer, shielded, 1.0);
    CHECK(renderer.shells().stats().draws == 1);
    CHECK(renderer.shells().stats().linearDepth); // the default quality runs the prepass
    dump(off, "shell-groundline-off");
    dump(on, "shell-groundline-on");

    const LineCount c = countAlongCircle(off, on, projectedCircle(shielded.camera, kRadius));
    INFO(c.changed << " changed px, " << c.onLine << " within 5 px of where the sphere cuts the floor");
    // The control: the line is there, and long -- the circle is ~250 px round on screen.
    CHECK(c.changed > 150);
    // The invariant: it is THERE, and nowhere else.
    CHECK(c.onLine * 100 >= c.changed * 95);
}

TEST_CASE("without this frame's linear depth the ground line is off, and the builder says why",
          "[gpu][shell][effects][depth][fallback]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    // The renderer's depth report is process-wide (a status sentence one frame late); a case that
    // rendered without the prepass before this one would otherwise make this one's first build Partial.
    world::reportShellLinearDepth(true);
    const rendering::QualitySettings full = renderer.qualitySettings();

    constexpr float kRadius = 4.0f;
    const scene::Scene bare = floorStage();
    scene::Scene shielded = floorStage();
    REQUIRE(buildShells(shielded, {groundLineOnly(kRadius)}, 1.0)[0] == world::EffectStatus::Drawn);

    // First WITH the prepass, so the linear-depth target holds this very scene's depth -- the stale
    // depth a shader that ignored the flag would find, and draw the line from.
    static_cast<void>(render(renderer, shielded, 1.0));
    REQUIRE(renderer.shells().stats().linearDepth);

    renderer.setQualitySettings(withoutPrepass(full));
    const gpu::Image8 off = render(renderer, bare, 1.0);
    const gpu::Image8 on = render(renderer, shielded, 1.0);
    CHECK(renderer.shells().stats().draws == 1);
    CHECK_FALSE(renderer.shells().stats().linearDepth);
    dump(on, "shell-groundline-noprepass");
    const LineCount c = countAlongCircle(off, on, projectedCircle(shielded.camera, kRadius));
    INFO(c.changed << " changed px without the prepass");
    CHECK(c.changed == 0);

    // The report reaches the next frame's status.
    CHECK(world::shellLinearDepthState() == world::ShellDepthState::Missing);
    std::vector<std::string> reasons;
    scene::Scene next = floorStage();
    const auto status = buildShells(next, {groundLineOnly(kRadius)}, 1.0, &reasons);
    CHECK(status[0] == world::EffectStatus::Partial);
    CHECK(reasons[0].find("depth prepass") != std::string::npos);

    // And back: with the prepass the line returns (the control for the zero above).
    renderer.setQualitySettings(full);
    const gpu::Image8 back = render(renderer, shielded, 1.0);
    CHECK(renderer.shells().stats().linearDepth);
    CHECK(countAlongCircle(off, back, projectedCircle(shielded.camera, kRadius)).changed > 150);
}

TEST_CASE("a plasma orb is depth-tested against the world and writes emission",
          "[gpu][shell][effects][plasma]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    // The renderer's depth report is process-wide (a status sentence one frame late); a case that
    // rendered without the prepass before this one would otherwise make this one's first build Partial.
    world::reportShellLinearDepth(true);

    // Camera at the origin looking down -Z; an orb 20 m away; a wall 10 m away covering the left half.
    const auto stage = [] {
        scene::Scene s;
        s.camera.position = glm::vec3(0.0f);
        s.camera.target = glm::vec3(0.0f, 0.0f, -1.0f);
        s.camera.fovYRadians = glm::radians(50.0f);
        s.environment.backgroundColor = glm::vec3(0.01f, 0.012f, 0.02f);
        const scene::MeshId cube = s.addMesh(scene::makeCube(1.0f));
        scene::Entity& wall = s.addEntity("wall", cube);
        wall.transform.position = glm::vec3(-6.0f, 0.0f, -10.0f);
        wall.transform.scale = glm::vec3(6.0f, 6.0f, 0.2f);
        wall.material.baseColor = glm::vec4(0.25f, 0.25f, 0.28f, 1.0f);
        return s;
    };
    world::EffectInstance orb = world::makeEffect(world::EffectKind::Plasma, "orb");
    orb.id = "orb";
    orb.owner = world::EffectOwner::world();
    orb.activation = world::Activation::Always;
    orb.timing = world::Timing{};
    orb.timing.fadeIn = 0.0;
    orb.values.setFloat("plasma/radius", 4.0f);
    orb.values.setFloat("plasma/offsetZ", -20.0f);
    orb.values.setFloat("plasma/light", 0.0f); // this case is about the orb, not what it lights

    const scene::Scene bare = stage();
    scene::Scene lit = stage();
    REQUIRE(buildShells(lit, {orb}, 2.0)[0] == world::EffectStatus::Drawn);
    // Twice: with the prepass, where the march is also clamped to the scene's depth, and without it,
    // where the depth test alone must hide the orb behind the wall.
    const rendering::QualitySettings full = renderer.qualitySettings();
    for (const bool prepass : {true, false}) {
        INFO((prepass ? "with" : "without") << " the depth prepass");
        renderer.setQualitySettings(prepass ? full : withoutPrepass(full));
        const gpu::Image8 off = render(renderer, bare, 2.0);
        const gpu::Image8 on = render(renderer, lit, 2.0);
        CHECK(renderer.shells().stats().linearDepth == prepass);
        dump(off, std::string("shell-plasma-occlusion-off") + (prepass ? "" : "-noprepass"));
        dump(on, std::string("shell-plasma-occlusion-on") + (prepass ? "" : "-noprepass"));
        std::size_t hidden = 0;
        std::size_t open = 0;
        for (std::uint32_t y = 0; y < kHeight; ++y) {
            for (std::uint32_t x = 0; x < kWidth; ++x) {
                if (!pixelDiffers(off, on, x, y)) {
                    continue;
                }
                // Clear of the wall's edge by the bloom's reach: the orb's halo is screen-space
                // light and rightly spills a few pixels over the edge; what must not show is the
                // orb itself.
                if (x < kWidth / 2 - 24) {
                    ++hidden;
                } else if (x > kWidth / 2 + 3) {
                    ++open;
                }
            }
        }
        INFO("changed px behind the wall (24 px clear of its edge) " << hidden << ", in the open " << open);
        CHECK(open > 400); // the control: the orb's right half is there
        CHECK(hidden == 0);

        auto emission = gpu::readTextureF16(*ctx, renderer.emissionTexture(), kWidth, kHeight);
        REQUIRE(emission.has_value());
        float right = 0.0f;
        for (std::uint32_t x = kWidth / 2 + 4; x < kWidth; ++x) {
            const float* p = emission->pixel(x, kHeight / 2);
            right += p[0] + p[1] + p[2];
        }
        INFO("emission along the orb's row, right of the wall: " << right);
        // The orb feeds the bloom mostly from its hot core (the strands at a fifth, so the bloom does
        // not wash them out): about 14 along this row, and nothing at all without the orb.
        CHECK(right > 5.0f);
    }
}

TEST_CASE("a plasma orb up close shows its strands: not clipped, and structured inside",
          "[gpu][shell][effects][plasma][look]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    world::reportShellLinearDepth(true);

    // The camera at the origin, the orb 6 m ahead filling most of the frame's height, a dark sky.
    scene::Scene s;
    s.camera.position = glm::vec3(0.0f);
    s.camera.target = glm::vec3(0.0f, 0.0f, -1.0f);
    s.camera.fovYRadians = glm::radians(50.0f);
    s.environment.backgroundColor = glm::vec3(0.01f, 0.012f, 0.02f);
    constexpr float kRadius = 2.0f;
    const glm::vec3 centre(0.0f, 0.0f, -6.0f);
    const auto disc = [&](std::uint32_t x, std::uint32_t y, float within) {
        const glm::vec2 c = toPixels(s.camera, centre);
        const glm::vec2 edge = toPixels(s.camera, centre + glm::vec3(0.0f, kRadius, 0.0f));
        return glm::length(glm::vec2(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f) - c) <
               within * glm::length(edge - c);
    };

    for (const char* look : {"Plasma Ball", "Fireball Core", "Ball Lightning", "Arcane Orb"}) {
        INFO("look: " << look);
        world::EffectInstance orb = world::makeEffect(world::EffectKind::Plasma, "orb");
        orb.id = "orb";
        orb.owner = world::EffectOwner::world();
        REQUIRE(world::applyEffectStyle(orb, world::EffectKind::Plasma, look));
        orb.activation = world::Activation::Always;
        orb.timing = world::Timing{};
        orb.timing.fadeIn = 0.0;
        orb.values.setFloat("plasma/radius", kRadius);
        orb.values.setFloat("plasma/offsetZ", centre.z);
        scene::Scene lit = s;
        REQUIRE(buildShells(lit, {orb}, 3.0)[0] == world::EffectStatus::Drawn);
        const gpu::Image8 img = render(renderer, lit, 3.0);
        dump(img, std::string("shell-plasma-look-") + look);

        // Inside 90 % of the orb's projected radius: how much is washed to white at the tone-mapped
        // clip (AgX compresses an over-bright colour to white rather than clipping one channel, so
        // "every channel high" is the clip), how much the brightness varies (strands and gaps, not a
        // flat blown disc), and how much colour survives.
        std::size_t inside = 0;
        std::size_t clipped = 0;
        double sum = 0.0;
        double sumSq = 0.0;
        double saturation = 0.0;
        for (std::uint32_t y = 0; y < kHeight; ++y) {
            for (std::uint32_t x = 0; x < kWidth; ++x) {
                if (!disc(x, y, 0.9f)) {
                    continue;
                }
                const std::uint8_t* p = img.pixel(x, y);
                const double l = 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
                ++inside;
                const int lo = std::min({p[0], p[1], p[2]});
                const int hi = std::max({p[0], p[1], p[2]});
                clipped += lo >= 200 ? 1u : 0u;
                saturation += static_cast<double>(hi - lo) / static_cast<double>(std::max(hi, 1));
                sum += l;
                sumSq += l * l;
            }
        }
        REQUIRE(inside > 1000);
        const double mean = sum / static_cast<double>(inside);
        const double spread = std::sqrt(std::max(sumSq / static_cast<double>(inside) - mean * mean, 0.0));
        const double clipFraction = static_cast<double>(clipped) / static_cast<double>(inside);
        saturation /= static_cast<double>(inside);
        INFO("inside " << inside << " px: " << clipFraction * 100.0 << " % at the clip, mean luma " << mean
                       << ", spread " << spread << ", saturation " << saturation);
        std::printf("plasma look %s: white %.1f %%, mean %.1f, spread %.1f, saturation %.3f\n", look,
                    clipFraction * 100.0, mean, spread, saturation);
        // Measured on the Wave 3 review's first look (a blown, white-washed orb): 17-37 % white,
        // saturation 0.15-0.27. After: under 4 % white (the hot core), saturation 0.27-0.48.
        CHECK(clipFraction < 0.08); // a small hot core may wash out; the orb may not
        CHECK(mean > 40.0);         // the control: it is a glowing ball, not a dim one
        CHECK(spread > 18.0);       // strands and gaps, not a flat disc
        CHECK(saturation > 0.22);   // its colour survives the tone mapping
    }
}

TEST_CASE("the shell capacity on the GPU: 131 walls draw 128 shells in one instanced draw",
          "[gpu][shell][effects][capacity]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    // The renderer's depth report is process-wide (a status sentence one frame late); a case that
    // rendered without the prepass before this one would otherwise make this one's first build Partial.
    world::reportShellLinearDepth(true);
    scene::Scene s = floorStage();
    std::vector<world::EffectInstance> walls;
    for (int i = 0; i < 131; ++i) {
        world::EffectInstance e = world::makeEffect(world::EffectKind::ForceField, "wall");
        e.id = "wall-" + std::to_string(i);
        e.owner = world::EffectOwner::world();
        e.values.setFloat("forceField/positionX", static_cast<float>(i % 13) * 3.0f - 18.0f);
        e.values.setFloat("forceField/positionZ", -static_cast<float>(i / 13) * 3.0f);
        e.values.setFloat("forceField/sizeX", 2.0f);
        e.values.setFloat("forceField/sizeY", 2.0f);
        walls.push_back(e);
    }
    const auto status = buildShells(s, walls, 1.0);
    CHECK(status[130] == world::EffectStatus::Dropped);
    static_cast<void>(render(renderer, s, 1.0));
    CHECK(renderer.shells().stats().shells == world::kMaxShells);
    CHECK(renderer.shells().stats().draws == 1);
}

namespace {

// The engine fixture: a craft on a simulated orbit, a camera looking down on it, a floor.
constexpr const char* kScene = R"({ "format": "avgen-scene", "version": 1, "name": "shell-fixture",
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

} // namespace

TEST_CASE("the shell gate: a disabled Energy Shield renders the same bytes as no effect",
          "[gpu][shell][effects][gate]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    // The renderer's depth report is process-wide (a status sentence one frame late); a case that
    // rendered without the prepass before this one would otherwise make this one's first build Partial.
    world::reportShellLinearDepth(true);
    constexpr double kSecond = 4.0;

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.setCompositionJson(nlohmann::json::parse(kScene)).has_value());
    const gpu::Image8 bare = render(renderer, frameAfterSeek(engine, kSecond), kSecond);
    CHECK(renderer.shells().stats().draws == 0);

    world::EffectInstance shield = world::makeEffect(world::EffectKind::EnergyShield, "Shield");
    shield.id = "craft-shield";
    shield.owner = world::EffectOwner::entity("craft");
    REQUIRE(engine.setEffects({shield}).has_value());
    setEnabled(engine, "craft-shield", false);
    const scene::Scene& gated = frameAfterSeek(engine, kSecond);
    CHECK(gated.shells.empty());
    CHECK(engine.effectStatus("craft-shield") == world::EffectStatus::Disabled);
    const gpu::Image8 disabled = render(renderer, gated, kSecond);
    CHECK(renderer.shells().stats().draws == 0);
    REQUIRE(bare.rgba.size() == disabled.rgba.size());
    CHECK(bare.rgba == disabled.rgba); // byte-identical

    // The control: switched on, the frame changes, so the equality above is the gate and not a
    // renderer that draws nothing whatever it is given.
    setEnabled(engine, "craft-shield", true);
    const gpu::Image8 on = render(renderer, frameAfterSeek(engine, kSecond), kSecond);
    CHECK(engine.effectStatus("craft-shield") == world::EffectStatus::Drawn);
    CHECK(renderer.shells().stats().draws == 1);
    CHECK(on.rgba != bare.rgba);
    dump(bare, "shell-gate-bare");
    dump(on, "shell-gate-on");
}

// ---- the review set (hidden) -------------------------------------------------------------------
//
// Run with `AVGEN_EFFECT_DUMP=<dir> avgen_render_tests "[shell-review]"`.

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

// Renders `ids` off, then on for three consecutive frames at `second`, with `camera` overriding the
// directed one when given.
void reviewArms(app::Engine& engine, rendering::SceneRenderer& renderer, const std::vector<std::string>& ids,
                double second, const std::string& stem, const scene::Camera* camera, std::uint32_t w, std::uint32_t h) {
    const auto shoot = [&](double t, std::uint64_t frame, const std::string& name) {
        engine.update(FrameTime{t, 1.0 / 60.0, frame});
        if (camera == nullptr) {
            dump(render(renderer, engine.scene(), t, w, h), name);
            return;
        }
        scene::Scene s = engine.scene();
        s.camera = *camera;
        dump(render(renderer, s, t, w, h), name);
    };
    setAll(engine, ids, false);
    engine.seekSeconds(second);
    shoot(second + 1.0 / 60.0, 0, stem + "-off");
    setAll(engine, ids, true);
    engine.seekSeconds(second);
    for (int f = 1; f <= 3; ++f) {
        const double t = second + f / 60.0;
        shoot(t, static_cast<std::uint64_t>(std::llround(t * 60.0)), stem + "-on-f" + std::to_string(f));
        if (f == 1) {
            for (const std::string& id : ids) {
                INFO(id << ": " << engine.effectStatusReason(id));
                CHECK((engine.effectStatus(id) == world::EffectStatus::Drawn ||
                       engine.effectStatus(id) == world::EffectStatus::Partial));
            }
        }
    }
}

} // namespace

TEST_CASE("VISUAL Energy Shield around the UFO saucer, with hits", "[.visual][shell-review][shield]") {
    if (std::getenv("AVGEN_EFFECT_DUMP") == nullptr) {
        SKIP("set AVGEN_EFFECT_DUMP to a directory to write the review frames");
    }
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "effects" / "ufo-stack.json";
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    // The renderer's depth report is process-wide (a status sentence one frame late); a case that
    // rendered without the prepass before this one would otherwise make this one's first build Partial.
    world::reportShellLinearDepth(true);
    constexpr std::uint32_t kW = 960;
    constexpr std::uint32_t kH = 540;

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(project).has_value());
    engine.setViewport(kW, kH);
    world::EffectInstance shield = world::makeEffect(world::EffectKind::EnergyShield, "Shield");
    shield.id = "visitor-shield";
    shield.owner = world::EffectOwner::entity("visitor");
    REQUIRE(world::applyEffectStyle(shield, world::EffectKind::EnergyShield, "Sci-Fi Bubble Shield"));
    // A few hits on a schedule, so the review shows rings at several ages whatever the music does.
    shield.timing.trigger.source = world::TriggerSource::Repeat;
    shield.timing.trigger.period = 1.1;
    addEffect(engine, shield);
    const char* seconds = std::getenv("AVGEN_SHELL_SECONDS");
    std::vector<double> picked{26.0, 120.0, 154.0};
    if (seconds != nullptr) {
        picked = {std::atof(seconds)};
    }
    for (const double second : picked) {
        reviewArms(engine, renderer, {"visitor-shield"}, second,
                   "shield-ufo-" + std::to_string(static_cast<int>(second)), nullptr, kW, kH);
        const world::ShellFrame& shells = engine.scene().shells;
        REQUIRE(shells.instances.size() == 1);
        const glm::mat4& m = shells.instances[0].model;
        const glm::vec3 cam = engine.scene().camera.position;
        std::printf("shield at %.0f s: centre (%.1f, %.1f, %.1f), semi-axes %.2f %.2f %.2f m, camera %.1f m from its centre\n",
                    second, static_cast<double>(m[3].x), static_cast<double>(m[3].y), static_cast<double>(m[3].z),
                    static_cast<double>(glm::length(glm::vec3(m[0]))), static_cast<double>(glm::length(glm::vec3(m[1]))),
                    static_cast<double>(glm::length(glm::vec3(m[2]))),
                    static_cast<double>(glm::length(cam - glm::vec3(m[3]))));
        world::NodeView view;
        if (engine.composition() != nullptr && engine.composition()->nodeView("visitor", view)) {
            std::printf("  visitor origin (%.1f, %.1f, %.1f), bounds (%.1f, %.1f, %.1f)..(%.1f, %.1f, %.1f)\n",
                        static_cast<double>(view.world[3].x), static_cast<double>(view.world[3].y),
                        static_cast<double>(view.world[3].z), static_cast<double>(view.boundsMin.x),
                        static_cast<double>(view.boundsMin.y), static_cast<double>(view.boundsMin.z),
                        static_cast<double>(view.boundsMax.x), static_cast<double>(view.boundsMax.y),
                        static_cast<double>(view.boundsMax.z));
        }
    }
}

namespace {

// Glowmere at night: the film's project with the day/night cycle on, paused at `phase`.
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

float envFloat(const char* name, float fallback) {
    const char* v = std::getenv(name);
    return v != nullptr ? static_cast<float>(std::atof(v)) : fallback;
}

} // namespace

TEST_CASE("VISUAL Plasma orbs over Glowmere at night", "[.visual][shell-review][plasma]") {
    if (std::getenv("AVGEN_EFFECT_DUMP") == nullptr) {
        SKIP("set AVGEN_EFFECT_DUMP to a directory to write the review frames");
    }
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json";
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    // The renderer's depth report is process-wide (a status sentence one frame late); a case that
    // rendered without the prepass before this one would otherwise make this one's first build Partial.
    world::reportShellLinearDepth(true);
    constexpr std::uint32_t kW = 960;
    constexpr std::uint32_t kH = 540;
    const double second = static_cast<double>(envFloat("AVGEN_SHELL_SECOND", 40.0f));

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(project).has_value());
    engine.setViewport(kW, kH);
    glowmereNight(engine, envFloat("AVGEN_SHELL_PHASE", 0.02f));
    // Where the directed camera looks at `second`: the orbs are placed along its view, a little
    // above the line of sight, at three distances.
    const scene::Camera camera = frameAfterSeek(engine, second, kW, kH).camera;
    const glm::vec3 forward = glm::normalize(camera.target - camera.position);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const char* looks[] = {"Plasma Ball", "Fireball Core", "Arcane Orb"};
    const float distances[] = {9.0f, 13.0f, 19.0f};
    const float across[] = {-3.0f, 2.5f, -0.5f};
    std::vector<std::string> ids;
    for (int i = 0; i < 3; ++i) {
        world::EffectInstance orb = world::makeEffect(world::EffectKind::Plasma, "Plasma");
        orb.id = "orb-" + std::to_string(i);
        orb.owner = world::EffectOwner::world();
        REQUIRE(world::applyEffectStyle(orb, world::EffectKind::Plasma, looks[i]));
        const glm::vec3 at = camera.position + forward * distances[i] + right * across[i] + glm::vec3(0.0f, 1.0f, 0.0f);
        orb.values.setFloat("plasma/offsetX", at.x);
        orb.values.setFloat("plasma/offsetY", at.y);
        orb.values.setFloat("plasma/offsetZ", at.z);
        orb.values.setFloat("plasma/radius", 0.9f + 0.4f * static_cast<float>(i));
        addEffect(engine, orb);
        ids.push_back(orb.id);
    }
    reviewArms(engine, renderer, ids, second, "plasma-glowmere-night-" + std::to_string(static_cast<int>(second)),
               &camera, kW, kH);
    // A close-up of the first orb, 4.5 m from it, to see its strands rather than its bloom.
    scene::Camera close = camera;
    close.position = camera.position + forward * 4.5f + glm::vec3(0.0f, 0.5f, 0.0f);
    close.target = camera.position + forward * distances[0] + right * across[0] + glm::vec3(0.0f, 1.0f, 0.0f);
    reviewArms(engine, renderer, {ids[0]}, second, "plasma-closeup-" + std::to_string(static_cast<int>(second)), &close,
               kW, kH);
}

TEST_CASE("VISUAL Force Field wall across the Glowmere river", "[.visual][shell-review][forcefield]") {
    if (std::getenv("AVGEN_EFFECT_DUMP") == nullptr) {
        SKIP("set AVGEN_EFFECT_DUMP to a directory to write the review frames");
    }
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json";
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    // The renderer's depth report is process-wide (a status sentence one frame late); a case that
    // rendered without the prepass before this one would otherwise make this one's first build Partial.
    world::reportShellLinearDepth(true);
    constexpr std::uint32_t kW = 960;
    constexpr std::uint32_t kH = 540;
    const double second = static_cast<double>(envFloat("AVGEN_SHELL_SECOND", 40.0f));

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(project).has_value());
    engine.setViewport(kW, kH);
    glowmereNight(engine, envFloat("AVGEN_SHELL_PHASE", 0.02f));
    const scene::Scene& s = frameAfterSeek(engine, second, kW, kH);

    // The river: every water vertex in the world. The wall stands on the water nearest a point along
    // the directed camera's view, turned so that it spans the river's local course (the principal
    // axis of the water within 20 m), and the camera is the directed one, aimed at it.
    std::vector<glm::vec3> water;
    for (const scene::Entity& entity : s.entities) {
        if (entity.style != scene::MeshStyle::Water || entity.mesh == scene::kInvalidMesh) {
            continue;
        }
        const glm::mat4 m = entity.transform.matrix();
        for (const scene::Vertex& v : s.meshes[entity.mesh].vertices) {
            const glm::vec3 w = glm::vec3(m * glm::vec4(v.position, 1.0f));
            // A water plane runs under the whole terrain; the river is where it is above the ground.
            if (!s.terrainGround.valid() || s.terrainGround.groundAt(glm::vec2(w.x, w.z)) < w.y - 0.3f) {
                water.push_back(w);
            }
        }
    }
    REQUIRE(!water.empty());
    const glm::vec3 eye = s.camera.position;
    const glm::vec3 look = eye + glm::normalize(s.camera.target - eye) * envFloat("AVGEN_SHELL_AHEAD", 30.0f);
    glm::vec3 at = water.front();
    for (const glm::vec3& w : water) {
        if (glm::length(glm::vec2(w.x - look.x, w.z - look.z)) < glm::length(glm::vec2(at.x - look.x, at.z - look.z))) {
            at = w;
        }
    }
    // The course: the principal axis of the nearby water in plan.
    glm::vec2 mean(0.0f);
    std::size_t near = 0;
    for (const glm::vec3& w : water) {
        if (glm::length(glm::vec2(w.x - at.x, w.z - at.z)) < 20.0f) {
            mean += glm::vec2(w.x, w.z);
            ++near;
        }
    }
    mean /= static_cast<float>(std::max<std::size_t>(near, 1));
    float sxx = 0.0f;
    float sxz = 0.0f;
    float szz = 0.0f;
    for (const glm::vec3& w : water) {
        const glm::vec2 d(w.x - mean.x, w.z - mean.y);
        if (glm::length(glm::vec2(w.x - at.x, w.z - at.z)) < 20.0f) {
            sxx += d.x * d.x;
            sxz += d.x * d.y;
            szz += d.y * d.y;
        }
    }
    const float course = 0.5f * std::atan2(2.0f * sxz, sxx - szz); // angle of the long axis from +X
    const glm::vec2 flow(std::cos(course), std::sin(course));
    // The wall's normal (sin yaw, cos yaw) along the flow, so the wall spans across it.
    const float yaw = glm::degrees(std::atan2(flow.x, flow.y)) + envFloat("AVGEN_SHELL_YAW", 0.0f);
    INFO("wall at " << at.x << "," << at.y << "," << at.z << " yaw " << yaw << " from " << near << " water vertices");
    world::EffectInstance wall = world::makeEffect(world::EffectKind::ForceField, "Force Field");
    wall.id = "river-ward";
    wall.owner = world::EffectOwner::world();
    REQUIRE(world::applyEffectStyle(wall, world::EffectKind::ForceField, envFloat("AVGEN_SHELL_LOOK", 1.0f) > 0.5f
                                                                            ? "Laser Grid"
                                                                            : "Glowmere Ward"));
    wall.values.setFloat("forceField/shape", 0.0f);
    wall.values.setFloat("forceField/positionX", at.x);
    wall.values.setFloat("forceField/positionY", at.y - 1.5f); // its foot under the water and the banks
    wall.values.setFloat("forceField/positionZ", at.z);
    wall.values.setFloat("forceField/yaw", yaw);
    wall.values.setFloat("forceField/sizeX", envFloat("AVGEN_SHELL_W", 22.0f));
    wall.values.setFloat("forceField/sizeY", envFloat("AVGEN_SHELL_H", 8.0f));
    addEffect(engine, wall);

    // The camera stands over the river downstream (or upstream) of the wall, looking along it.
    scene::Camera camera = s.camera;
    const float back = envFloat("AVGEN_SHELL_BACK", 26.0f);
    camera.position = at + glm::vec3(flow.x, 0.0f, flow.y) * back + glm::vec3(0.0f, envFloat("AVGEN_SHELL_UP", 6.0f), 0.0f);
    if (s.terrainGround.valid()) {
        camera.position.y = std::max(camera.position.y,
                                     s.terrainGround.groundAt(glm::vec2(camera.position.x, camera.position.z)) + 3.0f);
    }
    camera.target = at + glm::vec3(0.0f, 2.5f, 0.0f);
    std::printf("force field review: wall at (%.1f, %.1f, %.1f) yaw %.0f, camera %.1f m away\n", static_cast<double>(at.x),
                static_cast<double>(at.y), static_cast<double>(at.z), static_cast<double>(yaw),
                static_cast<double>(glm::length(camera.position - at)));
    reviewArms(engine, renderer, {"river-ward"}, second, "forcefield-river-" + std::to_string(static_cast<int>(second)),
               &camera, kW, kH);
}
