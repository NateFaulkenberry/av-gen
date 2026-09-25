// RIBBON on pixels (Effect Library Wave 1, package 1.6) and the Trail that draws through it.
//
// Four claims only a GPU can settle:
//   * a strip is depth-TESTED: a straight ribbon 20 m away is hidden where a wall 10 m away stands in
//     front of it, and only there;
//   * a strip WRITES EMISSION, so the selective bloom treats it as the light it is;
//   * the GATE: with no strip the frame is byte-identical -- a disabled Trail renders the same bytes
//     as a scene with no effect at all, and the renderer issues no ribbon draw;
//   * a Trail on a MOVING entity draws along where the entity has been, not where it is going.
//
// With `AVGEN_EFFECT_DUMP=<dir>` every arm is written there as a PNG, so the pictures the numbers
// describe can be looked at (the Wave 1 visual review).

#include "app/engine.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"
#include "world/effects/history_bank.hpp"
#include "world/effects/ribbon_frame.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 240;
constexpr std::uint32_t kHeight = 135;

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

bool pixelDiffers(const gpu::Image8& a, const gpu::Image8& b, std::uint32_t x, std::uint32_t y) {
    int sum = 0;
    for (int c = 0; c < 3; ++c) {
        sum += std::abs(static_cast<int>(a.pixel(x, y)[c]) - static_cast<int>(b.pixel(x, y)[c]));
    }
    return sum > 24;
}

void dump(const gpu::Image8& image, const std::string& stem) {
    const char* dir = std::getenv("AVGEN_EFFECT_DUMP");
    if (dir == nullptr || dir[0] == '\0') {
        return;
    }
    static_cast<void>(assets::writePng(fs::path(dir) / (stem + ".png"), image.width, image.height, image.rgba));
}

// Camera at the origin looking down -Z; a key light; a dark background.
scene::Scene darkStage() {
    scene::Scene scene;
    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.5f));
    key.intensity = 2.0f;
    scene.addLight(key);
    scene.camera.position = glm::vec3(0.0f, 0.0f, 0.0f);
    scene.camera.target = glm::vec3(0.0f, 0.0f, -1.0f);
    scene.camera.fovYRadians = glm::radians(60.0f);
    scene.environment.backgroundColor = glm::vec3(0.01f, 0.012f, 0.02f);
    return scene;
}

// A straight strip across the frame at depth 20 m.
void straightRibbon(scene::Scene& scene, float intensity) {
    std::vector<world::RibbonPoint> points;
    for (int i = 0; i <= 8; ++i) {
        world::RibbonPoint p;
        p.position = glm::vec3(-12.0f + 3.0f * static_cast<float>(i), 0.0f, -20.0f);
        p.width = 1.2f;
        p.color = glm::vec3(0.3f, 1.0f, 0.6f) * intensity;
        p.opacity = 1.0f;
        points.push_back(p);
    }
    std::vector<world::RibbonPoint> filtered;
    std::vector<world::RibbonPoint> dense;
    world::RibbonSink sink(scene.ribbons, filtered, dense);
    world::RibbonStyle style;
    style.blend = world::RibbonBlend::Additive;
    REQUIRE(sink.appendStrip(points, 1, style) == world::RibbonFit::Written);
}

} // namespace

TEST_CASE("a ribbon is depth-tested: a wall in front of it hides it there and only there",
          "[gpu][ribbon][effects]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    // The wall: x in [-12, 0] at 10 m, which covers the whole LEFT half of the strip behind it.
    scene::Scene walled = darkStage();
    const scene::MeshId cube = walled.addMesh(scene::makeCube(1.0f));
    scene::Entity& wall = walled.addEntity("wall", cube);
    wall.transform.position = glm::vec3(-6.0f, 0.0f, -10.0f);
    wall.transform.scale = glm::vec3(6.0f, 6.0f, 0.2f);
    wall.material.baseColor = glm::vec4(0.25f, 0.25f, 0.28f, 1.0f);

    const gpu::Image8 none = render(renderer, walled, 1.0);
    scene::Scene withRibbon = walled;
    straightRibbon(withRibbon, 4.0f);
    const gpu::Image8 lit = render(renderer, withRibbon, 1.0);
    CHECK(renderer.ribbons().stats().draws == 1);
    dump(none, "ribbon-occlusion-none");
    dump(lit, "ribbon-occlusion-ribbon");

    // The wall's screen extent: everything left of centre, a little margin for the edge.
    std::size_t hiddenSide = 0;
    std::size_t openSide = 0;
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            if (!pixelDiffers(none, lit, x, y)) {
                continue;
            }
            if (x < kWidth / 2 - 3) {
                ++hiddenSide;
            } else if (x > kWidth / 2 + 3) {
                ++openSide;
            }
        }
    }
    INFO("changed pixels behind the wall " << hiddenSide << ", in the open " << openSide);
    CHECK(openSide > 150); // the control: the strip is there, and visible where nothing hides it
    CHECK(hiddenSide == 0);
}

TEST_CASE("a ribbon writes the emission target, so the bloom treats it as light",
          "[gpu][ribbon][effects]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const auto emissionAtCentreRow = [&](const scene::Scene& s) {
        static_cast<void>(render(renderer, s, 1.0));
        auto e = gpu::readTextureF16(*ctx, renderer.emissionTexture(), kWidth, kHeight);
        REQUIRE(e.has_value());
        float sum = 0.0f;
        for (std::uint32_t x = kWidth / 4; x < 3 * kWidth / 4; ++x) {
            const float* p = e->pixel(x, kHeight / 2);
            sum += p[0] + p[1] + p[2];
        }
        return sum;
    };
    const scene::Scene dark = darkStage();
    scene::Scene strip = darkStage();
    straightRibbon(strip, 4.0f);
    const float without = emissionAtCentreRow(dark);
    const float with = emissionAtCentreRow(strip);
    INFO("emission along the strip's row: " << with << " with it, " << without << " without");
    CHECK(without < 1e-3f);
    CHECK(with > 100.0f); // 120 pixels of a 4x-intensity core
}

namespace {

// The engine fixture: a craft on a simulated orbit (it MOVES), a fixed camera looking down on it, a
// marker pillar, and surface fog so the trail is fogged like everything else.
constexpr const char* kScene = R"({ "format": "avgen-scene", "version": 1, "name": "trail-fixture",
  "camera": { "mode": 1, "position": [0.0, 26.0, 46.0], "target": [0.0, 4.0, 0.0], "fov": 50.0, "orbitSpeed": 0.0 },
  "environment": { "background": [0.01, 0.014, 0.03], "fogColor": [0.02, 0.03, 0.06], "volumeDensity": 0.0067, "volumeMaxDistance": 0.0 },
  "nodes": [ { "kind": "orb", "name": "craft", "position": [0, 6, 0], "scale": [0.6, 0.6, 0.6] },
             { "kind": "orb", "name": "pillar", "position": [0, 2, 4], "scale": [0.5, 3.0, 0.5] } ],
  "entities": [ { "name": "craft", "node": "craft", "seed": 7,
                  "behaviors": [ { "kind": "orbit", "radius": 14.0, "rate": 40.0, "authority": "simulation" },
                                 { "kind": "hover", "amplitude": 1.2, "rate": 0.35 } ] } ] })";

world::EffectInstance trailOn(const std::string& owner, const char* style) {
    world::EffectInstance e = world::makeEffect(world::EffectKind::Trail, "Trail");
    e.id = "craft-trail";
    e.owner = world::EffectOwner::entity(owner);
    REQUIRE(world::applyEffectStyle(e, world::EffectKind::Trail, style));
    e.timing = world::Timing{};
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    return e;
}

// The frame after a seek to `seconds`: exact, whatever frames were evaluated before (ADR-700), so
// arms that differ only in their effects differ only in their effects.
const scene::Scene& frameAfterSeek(app::Engine& engine, double seconds) {
    engine.setViewport(kWidth, kHeight);
    engine.seekSeconds(seconds);
    const double t = seconds + 1.0 / 60.0;
    engine.update(FrameTime{t, 1.0 / 60.0, static_cast<std::uint64_t>(std::llround(t * 60.0))});
    return engine.scene();
}

void setEnabled(app::Engine& engine, bool on) {
    auto* p = engine.params().find("fx/craft-trail/enabled");
    REQUIRE(p != nullptr);
    p->setBaseComponent(0, on ? 1.0f : 0.0f);
}

glm::vec2 toPixels(const scene::Camera& camera, const glm::vec3& p) {
    const glm::vec4 clip = camera.projection(static_cast<float>(kWidth) / static_cast<float>(kHeight)) *
                           camera.view() * glm::vec4(p, 1.0f);
    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
    return {(ndc.x * 0.5f + 0.5f) * static_cast<float>(kWidth), (0.5f - ndc.y * 0.5f) * static_cast<float>(kHeight)};
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

} // namespace

TEST_CASE("the gate: a disabled Trail renders the same bytes as no effect, and draws no ribbon",
          "[gpu][ribbon][effects][gate]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    constexpr double kSecond = 4.0;

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.setCompositionJson(nlohmann::json::parse(kScene)).has_value());
    const gpu::Image8 bare = render(renderer, frameAfterSeek(engine, kSecond), kSecond);
    CHECK(renderer.ribbons().stats().draws == 0);

    REQUIRE(engine.setEffects({trailOn("craft", "Light Trail")}).has_value());
    setEnabled(engine, false);
    const scene::Scene& gated = frameAfterSeek(engine, kSecond);
    CHECK(gated.ribbons.strips.empty());
    CHECK(engine.effectStatus("craft-trail") == world::EffectStatus::Disabled);
    const gpu::Image8 disabled = render(renderer, gated, kSecond);
    CHECK(renderer.ribbons().stats().draws == 0);
    REQUIRE(bare.rgba.size() == disabled.rgba.size());
    CHECK(bare.rgba == disabled.rgba); // byte-identical

    // The control: the same trail switched on changes the frame, so the equality above is the gate
    // and not a renderer that draws nothing whatever it is given.
    setEnabled(engine, true);
    const gpu::Image8 on = render(renderer, frameAfterSeek(engine, kSecond), kSecond);
    CHECK(engine.effectStatus("craft-trail") == world::EffectStatus::Drawn);
    CHECK(renderer.ribbons().stats().draws == 1);
    CHECK(on.rgba != bare.rgba);
    dump(bare, "trail-gate-bare");
    dump(disabled, "trail-gate-disabled");
}

TEST_CASE("a Trail on a moving entity draws along where it has been, not where it is going",
          "[gpu][ribbon][effects][trail]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    for (const char* style : {"Light Trail", "Comet Tail", "UFO Wake", "Subtle"}) {
        INFO("style: " << style);
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.setCompositionJson(nlohmann::json::parse(kScene)).has_value());
        REQUIRE(engine.setEffects({trailOn("craft", style)}).has_value());
        const float length = engine.effects()[0].values.getFloat("trail/length", 1.0f);
        const std::string stem = std::string("trail-") + style;

        for (const double second : {3.0, 5.5, 8.25}) {
            INFO("t = " << second << " s");
            setEnabled(engine, false);
            const gpu::Image8 off = render(renderer, frameAfterSeek(engine, second), second);
            setEnabled(engine, true);
            const scene::Scene& s = frameAfterSeek(engine, second);
            REQUIRE(engine.effectStatus("craft-trail") == world::EffectStatus::Drawn);
            REQUIRE(s.ribbons.strips.size() == 1);
            const gpu::Image8 on = render(renderer, s, second);
            dump(off, stem + "-" + std::to_string(static_cast<int>(second * 100)) + "-off");
            dump(on, stem + "-" + std::to_string(static_cast<int>(second * 100)) + "-on");

            // Where it has been: the ring, from `length` back to now, projected.
            const world::HistoryBank& bank = engine.historyBank();
            const std::size_t ring = bank.find("craft");
            REQUIRE(ring < bank.ringCount());
            const double now = bank.sample(ring, bank.sampleCount(ring) - 1).t;
            std::vector<glm::vec2> past;
            for (double t = now; t >= now - static_cast<double>(length); t -= 1.0 / 60.0) {
                world::HistorySample h;
                if (bank.sampleAt(ring, t, h)) {
                    past.push_back(toPixels(s.camera, h.position));
                }
            }
            REQUIRE(past.size() > 10);
            // Where it is going: play on for the same length and read the ring's new samples.
            std::vector<glm::vec2> future;
            {
                app::Engine ahead(app::EngineMode::Offline);
                REQUIRE(ahead.setCompositionJson(nlohmann::json::parse(kScene)).has_value());
                REQUIRE(ahead.setEffects({trailOn("craft", style)}).has_value());
                static_cast<void>(frameAfterSeek(ahead, second + static_cast<double>(length)));
                const world::HistoryBank& later = ahead.historyBank();
                const std::size_t r = later.find("craft");
                for (double t = now + 0.25; t <= now + static_cast<double>(length); t += 1.0 / 60.0) {
                    world::HistorySample h;
                    if (later.sampleAt(r, t, h)) {
                        future.push_back(toPixels(s.camera, h.position));
                    }
                }
            }
            REQUIRE(future.size() > 10);

            std::size_t changed = 0;
            std::size_t alongPast = 0;
            std::size_t onlyAhead = 0;
            for (std::uint32_t y = 0; y < kHeight; ++y) {
                for (std::uint32_t x = 0; x < kWidth; ++x) {
                    if (!pixelDiffers(off, on, x, y)) {
                        continue;
                    }
                    ++changed;
                    const glm::vec2 p(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
                    const float dPast = distanceToPolyline(p, past);
                    const float dFuture = distanceToPolyline(p, future);
                    alongPast += dPast < 14.0f ? 1u : 0u;
                    onlyAhead += (dFuture < 3.0f && dPast > 14.0f) ? 1u : 0u;
                }
            }
            INFO(changed << " changed px, " << alongPast << " along the past path, " << onlyAhead
                         << " only on the path ahead");
            // "Subtle" is meant to be barely there: a handful of pixels past the visible threshold.
            CHECK(changed > (std::string(style) == "Subtle" ? 3u : 25u));
            CHECK(alongPast * 10 >= changed * 9);
            CHECK(onlyAhead * 50 <= changed);
        }
    }
}

// The Wave 1 visual review, not a gate: the fixture at 960x540 with a floor for depth, every style on
// and off, and three consecutive frames of each so stepping, kinks and flicker between frames can be
// looked for. Hidden; run with `AVGEN_EFFECT_DUMP=<dir> avgen_render_tests "[trail-review]"`.
TEST_CASE("VISUAL Trail styles on a moving craft, consecutive frames", "[.visual][trail-review]") {
    if (std::getenv("AVGEN_EFFECT_DUMP") == nullptr) {
        SKIP("set AVGEN_EFFECT_DUMP to a directory to write the review frames");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    constexpr std::uint32_t kW = 960;
    constexpr std::uint32_t kH = 540;

    nlohmann::json doc = nlohmann::json::parse(kScene);
    doc["camera"]["position"] = {0.0, 14.0, 30.0};
    doc["camera"]["target"] = {0.0, 5.0, 0.0};
    doc["nodes"].push_back({{"kind", "orb"}, {"name", "floor"}, {"position", {0.0, -0.6, 0.0}},
                            {"scale", {30.0, 0.15, 30.0}}});
    for (const char* style : {"Light Trail", "UFO Wake", "Comet Tail", "Subtle"}) {
        for (const bool fog : {false, true}) {
            nlohmann::json d = doc;
            if (fog) {
                // ADR-705: the one density, surface pass only (was exp-squared `fogDensity` 0.03).
                d["environment"]["volumeDensity"] = 0.05;
                d["environment"]["volumeMaxDistance"] = 0.0;
            }
            app::Engine engine(app::EngineMode::Offline);
            REQUIRE(engine.setCompositionJson(d).has_value());
            REQUIRE(engine.setEffects({trailOn("craft", style)}).has_value());
            engine.setViewport(kW, kH);
            const std::string stem = std::string("visual-") + style + (fog ? "-fog" : "");
            setEnabled(engine, false);
            engine.seekSeconds(5.5);
            engine.update(FrameTime{5.5 + 1.0 / 60.0, 1.0 / 60.0, 331});
            dump(render(renderer, engine.scene(), 5.5, kW, kH), stem + "-off");
            setEnabled(engine, true);
            engine.seekSeconds(5.5);
            for (int f = 1; f <= 3; ++f) {
                const double t = 5.5 + f / 60.0;
                engine.update(FrameTime{t, 1.0 / 60.0, static_cast<std::uint64_t>(330 + f)});
                dump(render(renderer, engine.scene(), t, kW, kH), stem + "-on-f" + std::to_string(f));
            }
        }
    }
}

// The Wave 1 visual review on the owner's film: a Trail on Glowmere's `visitor` saucer, which the
// director flies. Scans the film for seconds where the saucer is in shot AND moving, then renders
// each on and off, three consecutive frames. Hidden; run with
// `AVGEN_EFFECT_DUMP=<dir> avgen_render_tests "[trail-review][glowmere]"`.
TEST_CASE("VISUAL Trail on the Glowmere visitor", "[.visual][trail-review][glowmere]") {
    if (std::getenv("AVGEN_EFFECT_DUMP") == nullptr) {
        SKIP("set AVGEN_EFFECT_DUMP to a directory to write the review frames");
    }
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json";
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    constexpr std::uint32_t kW = 960;
    constexpr std::uint32_t kH = 540;

    for (const char* style : {"UFO Wake", "Light Trail"}) {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadProject(project).has_value());
        engine.setViewport(kW, kH);
        REQUIRE(engine
                    .editEffects([&](std::vector<world::EffectInstance>& list) -> Result<void> {
                        world::EffectInstance t = trailOn("visitor", style);
                        t.id = "visitor-trail";
                        list.push_back(t);
                        world::normaliseEffectOrder(list);
                        return {};
                    })
                    .has_value());
        REQUIRE(engine.addDefaultEffectRoutes("visitor-trail") > 0);
        auto* enabled = engine.params().find("fx/visitor-trail/enabled");
        REQUIRE(enabled != nullptr);

        // Seconds where the saucer is in shot and moving fast, found by scanning the film in 2 s steps
        // (speed from HIST, position projected through the directed camera): at 26 s it crosses the
        // frame at ~89 px/s, 35 m from the camera; at 94 s it flies at 57 m/s 145 m away; at 154 s
        // the camera rides with it at 71 m/s.
        const std::vector<double> picked{26.0, 94.0, 154.0};
        for (const double second : picked) {
            const std::string stem = std::string("glowmere-") + style + "-" + std::to_string(static_cast<int>(second));
            enabled->setBaseComponent(0, 0.0f);
            engine.seekSeconds(second);
            engine.update(FrameTime{second + 1.0 / 60.0, 1.0 / 60.0, 0});
            dump(render(renderer, engine.scene(), second, kW, kH), stem + "-off");
            enabled->setBaseComponent(0, 1.0f);
            engine.seekSeconds(second);
            for (int f = 1; f <= 3; ++f) {
                const double t = second + f / 60.0;
                engine.update(FrameTime{t, 1.0 / 60.0, static_cast<std::uint64_t>(std::llround(t * 60.0))});
                dump(render(renderer, engine.scene(), t, kW, kH), stem + "-on-f" + std::to_string(f));
                if (f == 1) {
                    CHECK(engine.effectStatus("visitor-trail") == world::EffectStatus::Drawn);
                    CHECK(renderer.ribbons().stats().draws == 1);
                }
            }
        }
    }
}
