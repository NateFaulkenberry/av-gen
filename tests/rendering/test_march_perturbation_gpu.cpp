// ADR-709: the "whole-frame perturbation" ADR-702 recorded -- switching the volumetric march on for
// any placed medium, even one behind the camera, moved up to ~70% of the frame by at most 7/255.
//
// It was two things, and neither was the march:
//
//   * **Almost all of it was the harness.** ADR-702's arms reach each scene through
//     `Engine::update` at the SAME second, with a non-zero `dt`, once per arm. The engine advances
//     the scene's content by `dt` on every update whether or not time moved, so each arm was a
//     later scene than the one before: with the medium switched OFF in both, two updates apart,
//     91% of the frame differs by up to 48 levels. Reached through a seek -- which is what an
//     offline render does -- two arms at one second are one scene.
//   * **The rest was the composite.** With the environment fog off the march is skipped entirely
//     until a medium exists, and then every pixel whose rays all missed it still went through a
//     normalisation that is 1 in exact arithmetic and 0.99999994 on the GPU. 0.35% of the frame,
//     one level. `fs_composite` now leaves such a pixel exactly as drawn.
//
// The probe below is the bisection, kept hidden (`[.probe]`, it prints its table). The guard is
// the one claim that now holds exactly.

#include "app/engine.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

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

struct Diff {
    double fraction = 0.0; // of pixels with any channel differing
    int maxChannel = 0;
    double meanChannel = 0.0; // over the pixels that differ
};

Diff diff(const gpu::Image8& a, const gpu::Image8& b) {
    REQUIRE(a.rgba.size() == b.rgba.size());
    Diff d;
    std::size_t differ = 0;
    double sum = 0.0;
    std::size_t channels = 0;
    for (std::size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        bool any = false;
        for (int c = 0; c < 3; ++c) {
            const int x = std::abs(static_cast<int>(a.rgba[i + c]) - static_cast<int>(b.rgba[i + c]));
            if (x > 0) {
                any = true;
                d.maxChannel = std::max(d.maxChannel, x);
                sum += x;
                ++channels;
            }
        }
        differ += any ? 1u : 0u;
    }
    d.fraction = static_cast<double>(differ) / static_cast<double>(a.rgba.size() / 4);
    d.meanChannel = channels > 0 ? sum / static_cast<double>(channels) : 0.0;
    return d;
}

void print(const char* label, const Diff& d) {
    std::fprintf(stderr, "  %-44s %6.2f%% of px differ, max %d, mean %.2f\n", label, d.fraction * 100.0,
                 d.maxChannel, d.meanChannel);
}

// A tornado 150 m BEHIND the camera: nothing of it can be in shot, and the march's per-slot
// interval for every camera ray is empty.
world::EffectInstance tornadoBehind(const scene::Camera& camera, float metres) {
    world::EffectInstance t = world::makeEffect(world::EffectKind::Tornado, "Tornado");
    const glm::vec3 forward = glm::normalize(camera.target - camera.position);
    const glm::vec3 behind = camera.position - forward * metres;
    t.tornado.field.base = glm::vec3(behind.x, behind.y - 40.0f, behind.z);
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

const scene::Scene& sceneAt(app::Engine& engine, double seconds) {
    engine.setViewport(kWidth, kHeight);
    engine.update(FrameTime{seconds, 1.0 / 60.0, static_cast<std::uint64_t>(seconds * 60.0)});
    return engine.scene();
}

// What a `--range t:t` render does: a seek, one tick, one update. Idempotent by construction --
// the seek restores the simulation checkpoint (ADR-700) -- so two calls at one second are one scene.
const scene::Scene& sceneBySeek(app::Engine& engine, double seconds) {
    engine.seekSeconds(seconds);
    FixedStepClock clock(30.0);
    clock.restartAt(seconds);
    const FrameTime time = engine.tick(clock);
    engine.setViewport(kWidth, kHeight);
    engine.update(time);
    return engine.scene();
}

void setEnabled(app::Engine& engine, const std::string& id, bool on) {
    auto* p = engine.params().find(world::effectParameterPrefix(id) + "enabled");
    REQUIRE(p != nullptr);
    p->setBaseComponent(0, on ? 1.0f : 0.0f);
}

struct Bisection {
    Diff repeat, behind, behindMediaStripped, noneWithMediaGrafted, noneUpdatedAgain, noneThirdUpdate;
    Diff seekNoneRepeat, seekBehind, seekGraftedNoGlow, noneLast, plainCopy, graftedZeroSlot, graftedCountOnly;
};

// The arms, on the film ADR-702 measured it on.
Bisection bisect(double seconds, float metres) {
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json";
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(project).has_value());
    const scene::Camera camera = sceneAt(engine, seconds).camera;
    std::string tornado;
    REQUIRE(engine
                .editEffects([&](std::vector<world::EffectInstance>& list) -> Result<void> {
                    auto added = world::insertEffect(list, tornadoBehind(camera, metres));
                    if (!added) {
                        return std::unexpected(added.error());
                    }
                    tornado = *added;
                    return {};
                })
                .has_value());

    setEnabled(engine, tornado, false);
    const scene::Scene none = sceneAt(engine, seconds);
    REQUIRE(none.atmospherics.mediumCount == 0);
    const gpu::Image8 noneA = render(renderer, none, seconds);
    const gpu::Image8 noneB = render(renderer, none, seconds);

    setEnabled(engine, tornado, true);
    const scene::Scene with = sceneAt(engine, seconds);
    REQUIRE(with.atmospherics.mediumCount == 1);
    const gpu::Image8 behind = render(renderer, with, seconds);

    // The engine's scene WITH the tornado, and only the placed media removed before the renderer
    // sees it: if this matches `none`, the cause is in the renderer's handling of the medium; if it
    // matches `behind`, the engine changed something else when the tornado was switched on.
    scene::Scene stripped = with;
    stripped.atmospherics.mediumCount = 0;
    const gpu::Image8 strippedImg = render(renderer, stripped, seconds);

    // And the other way: the `none` scene with only the medium slots grafted in.
    scene::Scene grafted = none;
    grafted.atmospherics.mediumCount = with.atmospherics.mediumCount;
    for (std::size_t k = 0; k < world::kMaxMedia; ++k) {
        grafted.atmospherics.media[k] = with.atmospherics.media[k];
    }
    const gpu::Image8 graftedImg = render(renderer, grafted, seconds);

    // THE ENGINE CONTROL: the same `none` arm, asked of the engine again. `sceneAt` is an
    // `Engine::update` at the same second; if a second update at the same time is not the same
    // scene, the "tornado" arm is measuring the engine's second update rather than the tornado.
    setEnabled(engine, tornado, false);
    const scene::Scene noneAgain = sceneAt(engine, seconds);
    const gpu::Image8 noneAgainImg = render(renderer, noneAgain, seconds);
    std::fprintf(stderr, "  camera moved %.4f m between the first and third update\n",
                 static_cast<double>(glm::distance(none.camera.position, noneAgain.camera.position)));
    const scene::Scene noneThird = sceneAt(engine, seconds);
    const gpu::Image8 noneThirdImg = render(renderer, noneThird, seconds);

    // The same arms through a seek, which is how every offline render reaches a frame.
    setEnabled(engine, tornado, false);
    const scene::Scene seekNone = sceneBySeek(engine, seconds);
    REQUIRE(seekNone.atmospherics.mediumCount == 0);
    const gpu::Image8 seekNoneImg = render(renderer, seekNone, seconds);
    const gpu::Image8 seekNoneAgain = render(renderer, sceneBySeek(engine, seconds), seconds);
    setEnabled(engine, tornado, true);
    const scene::Scene seekWith = sceneBySeek(engine, seconds);
    REQUIRE(seekWith.atmospherics.mediumCount == 1);
    const gpu::Image8 seekWithImg = render(renderer, seekWith, seconds);
    // And the renderer's residual, with the one whole-frame reader of a medium outside the march
    // -- the first medium's surface glow, `frame.vortexGlow`, which is fed from lane 12.x -- zeroed.
    scene::Scene noGlow = none;
    noGlow.atmospherics.mediumCount = with.atmospherics.mediumCount;
    for (std::size_t k = 0; k < world::kMaxMedia; ++k) {
        noGlow.atmospherics.media[k] = with.atmospherics.media[k];
    }
    noGlow.atmospherics.media[0].lane[12].x = 0.0f;
    const gpu::Image8 noGlowImg = render(renderer, noGlow, seconds);

    // The renderer's own history: the `none` scene again, now that other scenes have been drawn.
    const gpu::Image8 noneLastImg = render(renderer, none, seconds);

    const scene::Scene copy = none;
    const gpu::Image8 copyImg = render(renderer, copy, seconds);
    scene::Scene zeroSlot = noGlow;
    zeroSlot.atmospherics.media[0] = world::MediumSlot{};
    const gpu::Image8 zeroSlotImg = render(renderer, zeroSlot, seconds);
    scene::Scene countOnly = none;
    countOnly.atmospherics.mediumCount = 1;
    const gpu::Image8 countOnlyImg = render(renderer, countOnly, seconds);

    Bisection b;
    b.plainCopy = diff(noneA, copyImg);
    b.graftedZeroSlot = diff(noneA, zeroSlotImg);
    b.graftedCountOnly = diff(noneA, countOnlyImg);
    b.noneLast = diff(noneA, noneLastImg);
    b.seekNoneRepeat = diff(seekNoneImg, seekNoneAgain);
    b.seekBehind = diff(seekNoneImg, seekWithImg);
    b.seekGraftedNoGlow = diff(noneA, noGlowImg);
    b.noneUpdatedAgain = diff(noneA, noneAgainImg);
    b.noneThirdUpdate = diff(noneAgainImg, noneThirdImg);
    b.repeat = diff(noneA, noneB);
    b.behind = diff(noneA, behind);
    b.behindMediaStripped = diff(noneA, strippedImg);
    b.noneWithMediaGrafted = diff(noneA, graftedImg);
    return b;
}

} // namespace

TEST_CASE("PROBE the march's whole-frame perturbation, bisected", "[.probe][volume][effects]") {
    for (const auto& [seconds, metres] : {std::pair{20.0, 150.0f}, std::pair{62.0, 150.0f}, std::pair{20.0, 2000.0f}}) {
        const Bisection b = bisect(seconds, metres);
        std::fprintf(stderr, "t = %.0f s, tornado %.0f m behind the camera\n", seconds, static_cast<double>(metres));
        print("none vs none (determinism)", b.repeat);
        print("none vs tornado behind the camera", b.behind);
        print("none vs (behind, media stripped)", b.behindMediaStripped);
        print("none vs (none, media grafted)", b.noneWithMediaGrafted);
        print("none vs none after two more updates", b.noneUpdatedAgain);
        print("none (3rd update) vs none (4th update)", b.noneThirdUpdate);
        print("SEEK: none vs none", b.seekNoneRepeat);
        print("SEEK: none vs tornado behind the camera", b.seekBehind);
        print("none vs (none, media grafted, glow lane 0)", b.seekGraftedNoGlow);
        print("none (first) vs none (drawn last)", b.noneLast);
        print("none vs a plain copy of none", b.plainCopy);
        print("none vs (count 1, slot all zero)", b.graftedZeroSlot);
        print("none vs (count 1 set on a copy of none)", b.graftedCountOnly);
    }
}

// The guard: a medium no ray reaches does not move a single byte. Through a seek, so the two arms
// are one scene; on the film ADR-702 measured, whose environment fog is OFF, so switching the
// medium on is also what switches the volume pass on -- the configuration the composite residual
// needed. With the pre-ADR-709 composite restored this fails on 0.23% of the frame.
TEST_CASE("a medium no camera ray reaches leaves the frame byte-identical", "[gpu][volume][effects]") {
    constexpr double kSecond = 20.0;
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json";
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(project).has_value());
    const scene::Camera camera = sceneBySeek(engine, kSecond).camera;
    std::string tornado;
    REQUIRE(engine
                .editEffects([&](std::vector<world::EffectInstance>& list) -> Result<void> {
                    auto added = world::insertEffect(list, tornadoBehind(camera, 2000.0f));
                    if (!added) {
                        return std::unexpected(added.error());
                    }
                    tornado = *added;
                    return {};
                })
                .has_value());

    setEnabled(engine, tornado, false);
    const scene::Scene none = sceneBySeek(engine, kSecond);
    REQUIRE(none.atmospherics.mediumCount == 0);
    const gpu::Image8 without = render(renderer, none, kSecond);
    // The premise: the film's environment fog is off, so without a medium there is no volume pass.
    REQUIRE(none.environment.volumeDensity == 0.0f);
    CHECK(renderer.stats().volume.steps == 0);

    setEnabled(engine, tornado, true);
    const scene::Scene with = sceneBySeek(engine, kSecond);
    REQUIRE(with.atmospherics.mediumCount == 1);
    const gpu::Image8 behind = render(renderer, with, kSecond);
    // The control: the march really ran with the medium in it, or byte-identity proves nothing.
    REQUIRE(renderer.stats().volume.steps > 0);
    REQUIRE(renderer.stats().volume.media == 1);

    const Diff d = diff(without, behind);
    INFO(d.fraction * 100.0 << "% of pixels differ, max " << d.maxChannel << " levels");
    CHECK(gpu::hashImage(without) == gpu::hashImage(behind));
}
