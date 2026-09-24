// The deliverable invariant, in pixels: an effect the session is drawing is in the frame the
// render writes.
//
// The reported defect was "world effects, skybox effects in particular are not rendered as part of
// the output video - despite being visible in the app during playback". Not a renderer fault: an
// offline render builds a **second** `Engine` and loads a project *document*
// (`Application::makeRenderJob`), and `startRenderFromUi` saves that document for you first. So the
// question is never "does the atmosphere pass run offline" -- it is "did the session's sky survive
// the save the render loads through". It did not, because ADR-230's effects belong to the
// `Composition` and a project saves its scene by reference.
//
// Three arms over one scene at one second, because the fix is only proved by the pair *and* by the
// control (ADR-182): a comparison that cannot see an aurora that is there says nothing about one
// that is not.
//
//   none       the composition with no atmospheric effect at all
//   session    the same composition with an aurora added, rendered from the session's own scene
//   delivered  that session saved to a project and reloaded by a second Engine, as a render does
//
// `session` must differ from `none` -- the control. `delivered` must equal `session` -- the
// invariant.
//
// Measured, on this machine, against `74f9c0e` (the commit before the fix) and again after it:
//
//   arm        hash, before the fix    hash, after
//   none       3287881903340913539     3287881903340913539
//   session    6381471283130077767     6381471283130077767
//   delivered  3287881903340913539     6381471283130077767
//
// Before: the exported frame was the *empty sky*, and not approximately -- the same 64-bit image
// hash, bit for bit. The control held in both runs, which is what makes the third row evidence
// rather than a coincidence of two blank frames.

#include "app/engine.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "world/atmospherics.hpp"
#include "world/effects/effect_instance.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <unistd.h>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 192;
constexpr std::uint32_t kHeight = 108;
constexpr double kSecond = 3.0;

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

// An aurora with nothing subtle about it: `Always` so no cut has to be installed for it to fire,
// and a curtain wall bright enough that a 192x108 frame cannot miss it. The point of the test is
// where the effect goes, not how it looks.
world::EffectInstance loudAurora() {
    world::EffectInstance e;
    e.id = "valley-aurora";
    e.name = "Valley Aurora";
    e.kind = world::EffectKind::Aurora;
    e.activation = world::Activation::Always;
    e.aurora.appearance.intensity = 6.0f;
    e.aurora.appearance.opacity = 1.0f;
    e.aurora.appearance.horizonGlow = 1.5f;
    e.aurora.shape.curtainCount = 1.0f;
    e.aurora.shape.waveAmplitude = 0.0f;
    return e;
}

// The scene the engine is holding at `kSecond`, after `update` has re-derived it. Reading before
// updating measures the frame the engine was already on (ADR-091).
const scene::Scene& sceneAt(app::Engine& engine, double seconds) {
    engine.setViewport(kWidth, kHeight);
    engine.update(FrameTime{seconds, 1.0 / 60.0, static_cast<std::uint64_t>(seconds * 60.0)});
    return engine.scene();
}

// One arm's frame. The temporal history is dropped first and the frame is drawn twice, because a
// renderer reused across arms carries the *previous* arm's history into this one -- which made the
// first version of this test report three different hashes for two identical worlds. A frame whose
// value depends on what was rendered before it is not a measurement of the world (ADR-182).
std::uint64_t renderHash(rendering::SceneRenderer& renderer, const scene::Scene& s, double seconds) {
    FrameTime t{};
    t.renderTime = seconds;
    renderer.resetTemporalHistory();
    auto first = renderer.renderToImage(s, t, kWidth, kHeight);
    REQUIRE(first.has_value());
    auto img = renderer.renderToImage(s, t, kWidth, kHeight);
    REQUIRE(img.has_value());
    return gpu::hashImage(*img);
}

// A fresh engine over the same scene file, advanced to `seconds` exactly once. Three engines rather
// than one mutated three times: an engine that has already been updated at this second has a history
// the others do not, and "the same world" has to mean the same number of updates too.
std::unique_ptr<app::Engine> engineAt(const fs::path& scene, double seconds,
                                      const std::vector<world::EffectInstance>& effects) {
    auto engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
    REQUIRE(engine->loadComposition(scene).has_value());
    if (!effects.empty()) {
        REQUIRE(engine->setEffects(effects).has_value());
    }
    static_cast<void>(sceneAt(*engine, seconds));
    return engine;
}

} // namespace

TEST_CASE("An atmospheric effect the session draws is in the frame a render writes",
          "[gpu][atmospherics][project]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const fs::path dir = fs::temp_directory_path() /
                         ("avgen_effects_deliverable_" + std::to_string(getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);

    {
        app::Engine author(app::EngineMode::Offline);
        author.newComposition();
        REQUIRE(author.saveComposition(dir / "scene.json").has_value());
    }

    const auto empty = engineAt(dir / "scene.json", kSecond, {});
    REQUIRE(empty->scene().atmospherics.auroraCount == 0);
    const std::uint64_t none = renderHash(renderer, empty->scene(), kSecond);

    // `setEffects` is the Effects panel's own call, and the scene file is not written again -- as
    // it is not when somebody presses Render.
    const auto session = engineAt(dir / "scene.json", kSecond, {loudAurora()});
    REQUIRE(session->scene().atmospherics.auroraCount == 1);
    const std::uint64_t live = renderHash(renderer, session->scene(), kSecond);

    // The control. Two frames that should differ and hash the same are void, not equal: if the
    // aurora does not move a pixel here, nothing below is evidence of anything.
    INFO("none=" << none << " live=" << live);
    REQUIRE(live != none);

    // The deliverable's path: save the session, build a second engine, load the document.
    REQUIRE(session->saveProject(dir / "session.json").has_value());
    app::Engine render(app::EngineMode::Offline);
    REQUIRE(render.loadProject(dir / "session.json").has_value());
    const scene::Scene& delivered = sceneAt(render, kSecond);
    CHECK(delivered.atmospherics.auroraCount == 1);
    const std::uint64_t exported = renderHash(renderer, delivered, kSecond);

    INFO("exported=" << exported << " live=" << live << " none=" << none);
    // The failure this test exists for: before the fix the exported frame was the *empty sky*, bit
    // for bit -- the aurora was in the window and in no document the render reads.
    CHECK(exported != none);
    CHECK(exported == live);
}
