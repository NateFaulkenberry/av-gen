// BOLT's four types on pixels (Effect Library Wave 3): Lightning, Arc, Electric Field, Discharge.
//
// The claim only a GPU settles is the GATE: with every bolt instance disabled, or waiting for a
// trigger that has not fired, the frame is byte-identical to one with no effect at all, the ribbon
// renderer issues no draw and the light pool is empty. The control shows the same instances enabled
// change the frame.
//
// The hidden `[bolt-review]` cases are the visual review: each type on and off on a night fixture
// over consecutive frames (with and without fog), a strike sequence over Glowmere at night, and an
// Arc from the Glowmere saucer to a point on the ground. Run with
// `AVGEN_EFFECT_DUMP=<dir> avgen_render_tests "[bolt-review]"`.

#include "app/engine.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"
#include "scene/ground_query.hpp"
#include "scene/scene.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"

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

void dump(const gpu::Image8& image, const std::string& stem) {
    const char* dir = std::getenv("AVGEN_EFFECT_DUMP");
    if (dir == nullptr || dir[0] == '\0') {
        return;
    }
    static_cast<void>(assets::writePng(fs::path(dir) / (stem + ".png"), image.width, image.height, image.rgba));
}

// Night: a craft hovering over a floor, a pillar beside it, a dim blue ambient.
constexpr const char* kScene = R"({ "format": "avgen-scene", "version": 1, "name": "bolt-fixture",
  "camera": { "mode": 1, "position": [0.0, 9.0, 40.0], "target": [0.0, 7.0, 0.0], "fov": 55.0, "orbitSpeed": 0.0 },
  "environment": { "background": [0.004, 0.006, 0.018], "fogColor": [0.02, 0.03, 0.06], "volumeDensity": 0.0, "volumeMaxDistance": 0.0 },
  "nodes": [ { "kind": "orb", "name": "craft", "position": [0, 8, 0], "scale": [2.2, 0.7, 2.2] },
             { "kind": "orb", "name": "pillar", "position": [7, 3, -4], "scale": [0.8, 3.0, 0.8] },
             { "kind": "orb", "name": "floor", "position": [0, -1.0, 0], "scale": [400.0, 1.0, 400.0] } ] })";

const scene::Scene& frameAt(app::Engine& engine, double seconds, std::uint32_t w = kWidth, std::uint32_t h = kHeight) {
    engine.setViewport(w, h);
    engine.seekSeconds(seconds);
    const double t = seconds + 1.0 / 60.0;
    engine.update(FrameTime{t, 1.0 / 60.0, static_cast<std::uint64_t>(std::llround(t * 60.0))});
    return engine.scene();
}

world::EffectInstance styled(world::EffectKind kind, const char* style, world::EffectOwner owner, std::string id) {
    world::EffectInstance e = world::makeEffect(kind, world::effectSchema(kind)->displayName);
    e.id = std::move(id);
    const bool entity = owner.kind == world::EffectTarget::Entity;
    e.owner = std::move(owner);
    if (entity) {
        e.wave.source.kind = world::SourceKind::Owner; // made for the World, re-attached (see adaptEffectToOwner)
    }
    REQUIRE(world::applyEffectStyle(e, kind, style));
    return e;
}

world::Trigger repeatAt(double period, double phase) {
    world::Trigger t;
    t.source = world::TriggerSource::Repeat;
    t.period = period;
    t.phase = phase;
    return t;
}

// The four types on the fixture, each firing (or running) near second 2.5.
std::vector<world::EffectInstance> fixtureBolts() {
    const world::EffectOwner craft = world::EffectOwner::entity("craft");
    world::EffectInstance strike = styled(world::EffectKind::Lightning, "Storm Strike", world::EffectOwner::world(), "strike");
    strike.wave.source.kind = world::SourceKind::World;
    strike.wave.source.position = glm::vec3(-10.0f, 0.0f, -6.0f);
    strike.values.setFloat("lightning/scatter", 0.0f);
    strike.values.setFloat("lightning/height", 32.0f);
    strike.values.setFloat("lightning/lean", 8.0f);
    strike.timing.trigger = repeatAt(10.0, 2.5);
    world::EffectInstance arc = styled(world::EffectKind::Arc, "UFO Link", craft, "arc");
    arc.wave.hasTarget = true;
    arc.wave.target.kind = world::SourceKind::World;
    arc.wave.target.position = glm::vec3(5.0f, 0.0f, 4.0f);
    world::EffectInstance field = styled(world::EffectKind::ElectricField, "Overcharged", craft, "field");
    world::EffectInstance burst = styled(world::EffectKind::Discharge, "Overload", craft, "burst");
    burst.timing.trigger = repeatAt(10.0, 2.45);
    std::vector<world::EffectInstance> list{strike, arc, field, burst};
    for (std::size_t i = 0; i < list.size(); ++i) {
        list[i].order = static_cast<int>(i);
    }
    return list;
}

// The review's night: the fixture's key light down to moonlight and its surfaces grey, so the bolts
// and their flashes are what lights the frame. (The gate compares the fixture as it is.)
scene::Scene nightOf(const scene::Scene& in) {
    scene::Scene s = in;
    for (scene::PunctualLight& l : s.lights) {
        l.intensity *= 0.08f;
    }
    for (scene::Entity& e : s.entities) {
        e.material.baseColor = e.name == "floor" ? glm::vec3(0.18f, 0.19f, 0.2f) : glm::vec3(0.45f, 0.47f, 0.52f);
    }
    return s;
}

} // namespace

TEST_CASE("the gate: disabled or waiting bolt types render the same bytes as no effect", "[gpu][bolt][effects][gate]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    constexpr double kSecond = 2.55;

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.setCompositionJson(nlohmann::json::parse(kScene)).has_value());
    const gpu::Image8 bare = render(renderer, frameAt(engine, kSecond), kSecond);
    CHECK(renderer.ribbons().stats().draws == 0);

    // Every type present and disabled.
    std::vector<world::EffectInstance> off = fixtureBolts();
    for (world::EffectInstance& e : off) {
        e.enabled = false;
    }
    REQUIRE(engine.setEffects(off).has_value());
    const scene::Scene& gated = frameAt(engine, kSecond);
    CHECK(gated.ribbons.strips.empty());
    CHECK(gated.entityFx.lights.count == 0);
    CHECK(gated.entityFx.records.empty());
    const gpu::Image8 disabled = render(renderer, gated, kSecond);
    CHECK(renderer.ribbons().stats().draws == 0);
    REQUIRE(bare.rgba.size() == disabled.rgba.size());
    CHECK(bare.rgba == disabled.rgba);

    // Enabled, but the triggered two waiting for a beat the fixture has no audio to give, and the
    // continuous two outside a window that has not opened.
    std::vector<world::EffectInstance> waiting = fixtureBolts();
    waiting[0].timing.trigger = world::Trigger{}; // Beat
    waiting[3].timing.trigger = world::Trigger{};
    for (const std::size_t i : {std::size_t{1}, std::size_t{2}}) {
        waiting[i].activation = world::Activation::Window;
        waiting[i].timing.windowStart = 100.0;
        waiting[i].timing.windowSeconds = 5.0;
    }
    REQUIRE(engine.setEffects(waiting).has_value());
    const scene::Scene& dormant = frameAt(engine, kSecond);
    for (const world::EffectInstance& e : engine.effects()) {
        INFO(e.id);
        CHECK(engine.effectStatus(e.id) == world::EffectStatus::Dormant);
    }
    CHECK(dormant.ribbons.strips.empty());
    CHECK(dormant.entityFx.lights.count == 0);
    const gpu::Image8 asleep = render(renderer, dormant, kSecond);
    CHECK(renderer.ribbons().stats().draws == 0);
    CHECK(bare.rgba == asleep.rgba);

    // The control: the same four enabled and firing change the frame.
    REQUIRE(engine.setEffects(fixtureBolts()).has_value());
    const scene::Scene& live = frameAt(engine, kSecond);
    for (const world::EffectInstance& e : engine.effects()) {
        INFO(e.id << ": " << engine.effectStatusReason(e.id));
        CHECK(engine.effectStatus(e.id) == world::EffectStatus::Drawn);
    }
    const gpu::Image8 on = render(renderer, live, kSecond);
    CHECK(renderer.ribbons().stats().draws >= 8);
    CHECK(live.entityFx.lights.count >= 2);
    CHECK(on.rgba != bare.rgba);
    dump(bare, "bolt-gate-bare");
    dump(disabled, "bolt-gate-disabled");
    dump(on, "bolt-gate-on");
}

// The visual review on the fixture: each type alone, on and off, over consecutive frames, clear and
// in fog. Hidden.
TEST_CASE("VISUAL bolt types on a night fixture", "[.visual][bolt-review]") {
    if (std::getenv("AVGEN_EFFECT_DUMP") == nullptr) {
        SKIP("set AVGEN_EFFECT_DUMP to a directory to write the review frames");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    constexpr std::uint32_t kW = 960;
    constexpr std::uint32_t kH = 540;
    const std::vector<world::EffectInstance> all = fixtureBolts();
    const std::vector<double> seconds{2.52, 2.53, 2.56, 2.62, 2.7, 2.9};
    for (std::size_t which = 0; which < all.size(); ++which) {
        for (const bool fog : {false, true}) {
            nlohmann::json doc = nlohmann::json::parse(kScene);
            if (fog) {
                doc["environment"]["volumeDensity"] = 0.03;
            }
            if (all[which].id == "field" || all[which].id == "burst") {
                // The surface types, from near enough to see a surface.
                doc["camera"]["position"] = {9.0, 12.0, 17.0};
                doc["camera"]["target"] = {0.0, 6.5, 0.0};
            }
            app::Engine engine(app::EngineMode::Offline);
            REQUIRE(engine.setCompositionJson(doc).has_value());
            const std::string stem = "bolt-" + all[which].id + (fog ? "-fog" : "");
            dump(render(renderer, nightOf(frameAt(engine, 2.52, kW, kH)), 2.52, kW, kH), stem + "-off");
            REQUIRE(engine.setEffects({all[which]}).has_value());
            for (std::size_t f = 0; f < seconds.size(); ++f) {
                const scene::Scene& s = frameAt(engine, seconds[f], kW, kH);
                INFO(all[which].id << " at " << seconds[f] << ": " << engine.effectStatusReason(all[which].id));
                dump(render(renderer, nightOf(s), seconds[f], kW, kH), stem + "-on-" + std::to_string(f));
            }
        }
    }
}

namespace {

// The ground under `p` in the loaded world, or `p` itself when there is no answer.
glm::vec3 groundUnder(app::Engine& engine, glm::vec3 p) {
    if (const scene::Composition* comp = engine.composition()) {
        const scene::GroundSample g = comp->groundQuery().sampleAt(p + glm::vec3(0.0f, 500.0f, 0.0f));
        if (g.valid) {
            return g.point;
        }
    }
    return p;
}

} // namespace

// A lightning strike sequence over Glowmere at night, and an Arc from the saucer to a point on the
// ground, on the owner's film. Hidden.
TEST_CASE("VISUAL bolt types over Glowmere at night", "[.visual][bolt-review][glowmere]") {
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
    const double second = std::getenv("AVGEN_BOLT_SECOND") != nullptr ? std::atof(std::getenv("AVGEN_BOLT_SECOND")) : 26.0;

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(project).has_value());
    const scene::Scene& before = frameAt(engine, second, kW, kH);
    const std::string tag = std::to_string(static_cast<int>(second));
    dump(render(renderer, before, second, kW, kH), "glowmere-bolt-" + tag + "-off");
    // Where to strike: the ground 70% of the way from the camera to what it looks at, so it is in shot.
    const glm::vec3 eye = before.camera.position;
    const glm::vec3 aim = before.camera.target;
    const glm::vec3 landing = groundUnder(engine, glm::mix(eye, aim, 0.9f));
    world::NodeView saucer;
    const bool haveSaucer = engine.composition() != nullptr && engine.composition()->nodeView("visitor", saucer);
    const glm::vec3 saucerAt = haveSaucer ? 0.5f * (saucer.boundsMin + saucer.boundsMax) : aim;
    const glm::vec3 below = groundUnder(engine, saucerAt + glm::vec3(4.0f, 0.0f, 3.0f));
    INFO("camera " << eye.x << "," << eye.y << "," << eye.z << " landing " << landing.x << "," << landing.y << ","
                   << landing.z << " saucer " << saucerAt.x << "," << saucerAt.y << "," << saucerAt.z);

    world::EffectInstance strike = styled(world::EffectKind::Lightning, "Storm Strike", world::EffectOwner::world(), "strike");
    strike.wave.source.kind = world::SourceKind::World;
    strike.wave.source.position = landing;
    strike.values.setFloat("lightning/scatter", 0.0f);
    strike.timing.trigger = repeatAt(100.0, second + 0.1);
    world::EffectInstance arc = styled(world::EffectKind::Arc, "UFO Link", world::EffectOwner::entity("visitor"), "link");
    arc.wave.hasTarget = true;
    arc.wave.target.kind = world::SourceKind::World;
    arc.wave.target.position = below;
    REQUIRE(engine
                .editEffects([&](std::vector<world::EffectInstance>& list) -> Result<void> {
                    list.push_back(strike);
                    list.push_back(arc);
                    world::normaliseEffectOrder(list);
                    return {};
                })
                .has_value());
    const std::vector<double> offsets{0.12, 0.14, 0.16, 0.2, 0.26, 0.33, 0.45, 0.7};
    for (std::size_t f = 0; f < offsets.size(); ++f) {
        const double t = second + offsets[f];
        const scene::Scene& s = frameAt(engine, t, kW, kH);
        CHECK(engine.effectStatus("link") == world::EffectStatus::Drawn);
        dump(render(renderer, s, t, kW, kH), "glowmere-bolt-" + tag + "-on-" + std::to_string(f));
        if (f == 3) {
            // Where a thin core's coloured fringe comes from: the same frame with the film's lens
            // chromatic aberration and its halation (a red-weighted bloom tier) switched off.
            INFO("post: chromatic aberration " << s.post.chromaticAberration << ", halation "
                 << (s.post.halationEnabled ? "on" : "off") << " at " << s.post.halationIntensity);
            CHECK(true);
            scene::Scene plain = s;
            plain.post.chromaticAberration = 0.0f;
            dump(render(renderer, plain, t, kW, kH), "glowmere-bolt-" + tag + "-on-3-no-aberration");
            plain.post.halationEnabled = false;
            dump(render(renderer, plain, t, kW, kH), "glowmere-bolt-" + tag + "-on-3-no-aberration-no-halation");
        }
    }
}
