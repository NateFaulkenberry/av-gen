// Phase G, G5: offline parity -- an offline render and an interactive one must agree, and an
// offline render must not depend on how the camera arrived.
//
//   tools/gpu-lock.sh ./build/release/tests/avgen_render_tests "[certification]"
//
// **What is already covered elsewhere, and is not repeated here.**
// `test_composition_gpu.cpp` proves that the offline job's frame and the interactive path's frame
// are byte-identical for the same second ("a frame rendered offline matches the same frame rendered
// live"), that the same second reached by different routes replays identically, and that Glowmere
// replays after a seek away and back. Those are the determinism contract §41 depends on and they
// pass. Re-asserting them would add nothing.
//
// **What this file is about** is the property the *upgrade* puts at risk, which none of those
// tests can see: representation. ADR-125 states the contract in its title -- "hysteresis is opt-in,
// and offline never gets it" -- and §5.9 of the spec is the reason: an offline render is a
// deliverable and must not silently inherit a realtime compromise. Hysteresis is the one mechanism
// in the renderer that reads the *previous frame*, so with it on, what you see depends on how the
// camera arrived and not only on where it is.
//
// There are two hysteresis mechanisms in this renderer and they are not the same one:
//
//   1. `RepresentationPolicy::hysteresis` -- the CPU representation selector (ADR-122/123/125).
//      `forTier(Offline)` sets `forceTopRepresentation` and pins hysteresis and spread to zero.
//   2. `LodSettings::lodHysteresis` -- the GPU instance ladder's dead zone (ADR-082), authored per
//      procedural object and passed to `cull.wgsl` through `procedural_renderer.cpp:1704`.
//
// The tests below check the contract on **both**, by rendering the same camera position twice --
// once approached from far away, once from close up -- and comparing the LOD histogram, which is a
// deterministic counter and therefore carries no noise floor to argue about.
//
// The second test is written to *fail* if the ladder's dead zone survives into an offline render.
// If it does fail, that is the finding and not a bug in the test: the assertion is a transcription
// of ADR-125's title, and weakening it to make it pass would be deleting the contract rather than
// meeting it.

#include "app/engine.hpp"
#include "app/render_job.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/representation.hpp"
#include "rendering/scene_renderer.hpp"
#include "rendering/render_quality.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;

namespace {

constexpr std::uint32_t kWidth = 640;
constexpr std::uint32_t kHeight = 400;

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available");
    }
    return std::move(*ctx);
}

FrameTime frameAt(std::uint64_t index) {
    FrameTime t;
    t.frameIndex = index;
    t.renderTime = static_cast<double>(index) / 60.0;
    t.deltaTime = 1.0 / 60.0;
    return t;
}

scene::InstanceRecord instanceAt(glm::vec3 position) {
    scene::InstanceRecord r{};
    r.position = glm::vec4(position, 1.0f);
    r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    r.scale = {1.0f, 1.0f, 1.0f, 0.0f};
    r.random = {0.25f, 0.5f, 0.75f, 0.125f};
    r.color = {1.0f, 1.0f, 1.0f, 0.0f};
    r.emissive = {1.0f, 1.0f, 1.0f, 0.0f};
    return r;
}

// A grid of boxes on a three-rung screen-size ladder, with the dead zone authored on.
//
// `lodHysteresis` is deliberately large (0.3, the clamp's ceiling is 0.5) and `lodSpread` is zero.
// Spread would decorrelate the population across the threshold, which is the *other* ADR-082 fix and
// would blur the very thing being measured: the question here is whether the dead zone's memory
// survives into an offline render, and a population that crosses at thirty different thresholds
// makes that harder to see rather than easier.
scene::Scene ladderScene(float hysteresis) {
    scene::Scene s;
    s.environment.showSkybox = false;
    s.environment.backgroundColor = {0.01f, 0.015f, 0.025f};
    s.camera.lens.useExplicitFov = true;
    s.camera.fovYRadians = 0.87f;
    s.camera.nearPlane = 0.1f;
    s.camera.farPlane = 2000.0f;
    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.3f, -0.6f, -0.7f));
    key.intensity = 3.0f;
    s.addLight(key);

    scene::ProceduralGeometry g;
    g.name = "ladder";
    g.source.kind = scene::PrimitiveKind::Box;
    g.source.size = {2.0f, 2.0f, 2.0f};
    g.source.subdivisions = 3;
    g.meshHash = 0x1ADDE40001ull;
    g.structureVersion = 1;
    g.material.baseColor = {0.55f, 0.5f, 0.45f};
    g.material.roughness = 0.6f;
    g.lod.cull = true;
    g.lod.lodCount = 3;
    g.lod.lodByScreenSize = true;
    // Projected radii in pixels: LOD0->1 and 1->2. Chosen so the population straddles both on the
    // approach, which is what gives the dead zone something to remember.
    g.lod.lodDistances[0] = 40.0f;
    g.lod.lodDistances[1] = 12.0f;
    g.lod.lodSpread = 0.0f;
    g.lod.lodHysteresis = hysteresis;
    for (int z = 0; z < 24; ++z) {
        for (int x = -12; x <= 12; ++x) {
            g.instances.push_back(instanceAt({static_cast<float>(x) * 6.0f, 0.0f,
                                              -20.0f - static_cast<float>(z) * 14.0f}));
        }
    }
    s.procedurals.push_back(std::move(g));
    return s;
}

using Histogram = std::array<std::uint64_t, 4>;

std::string describe(const Histogram& h) {
    return "LOD0=" + std::to_string(h[0]) + " LOD1=" + std::to_string(h[1]) + " LOD2=" +
           std::to_string(h[2]) + " LOD3=" + std::to_string(h[3]);
}

// Walk the camera along `path` one frame per entry, then settle at the last position for
// `settleFrames` more, and report the LOD histogram of the final frame.
//
// The settle matters and is the trap this kind of probe usually falls into: a dead zone releases
// over frames, so a single frame at the destination measures the approach rather than the
// destination, and reading it would attribute the arrival path's effect to something else. The
// `SYM-TERRAIN-1` investigation produced four wrong attributions from exactly this shape of probe.
// Settling for a generous number of frames is what makes a *remaining* difference a memory that
// does not release rather than one that had not released yet.
Histogram walkAndSettle(rendering::SceneRenderer& renderer, scene::Scene& s,
                        const std::vector<float>& path, int settleFrames) {
    renderer.procedurals().setViewport(kWidth, kHeight);
    std::uint64_t frame = 0;
    const auto step = [&](float z) {
        s.camera.position = {0.0f, 6.0f, z};
        s.camera.target = {0.0f, 0.0f, z - 40.0f};
        REQUIRE(renderer.renderFrame(s, frameAt(frame++), kWidth, kHeight).has_value());
    };
    for (const float z : path) {
        step(z);
    }
    for (int i = 0; i < settleFrames; ++i) {
        step(path.back());
    }
    const rendering::RenderStats& st = renderer.stats();
    return {st.lodCounts[0], st.lodCounts[1], st.lodCounts[2], st.lodCounts[3]};
}

// The two arrival paths, both ending at the same place. Far-to-near and near-to-far, so every
// instance that can be on a threshold has approached it from both sides.
const std::vector<float> kFromFar = {600.0f, 480.0f, 380.0f, 300.0f, 240.0f, 190.0f, 150.0f,
                                     120.0f, 95.0f,  75.0f,  60.0f,  50.0f,  44.0f,  40.0f};
const std::vector<float> kFromNear = {6.0f,  9.0f,  12.0f, 16.0f, 20.0f, 24.0f, 28.0f,
                                      31.0f, 34.0f, 36.0f, 38.0f, 39.0f, 39.5f, 40.0f};

} // namespace

// ---- the tier's own contract, with no device in it -----------------------------------------------

TEST_CASE("the offline tier's representation policy reads no previous frame", "[certification][offline]") {
    using rendering::QualityTier;
    using rendering::RepresentationPolicy;

    // ADR-125's decision, stated as a check rather than as prose. The default matters as much as
    // the offline case: the ADR rejected "on by default in realtime, off offline" explicitly,
    // because two subsystems with opposite defaults for one property is worse than either default.
    for (const QualityTier tier :
         {QualityTier::Preview, QualityTier::Realtime, QualityTier::High, QualityTier::Offline}) {
        const RepresentationPolicy policy = RepresentationPolicy::forTier(tier);
        INFO("tier " << rendering::qualityTierName(tier));
        CHECK(policy.hysteresis == 0.0f);
    }

    const RepresentationPolicy offline = RepresentationPolicy::forTier(QualityTier::Offline);
    CHECK(offline.forceTopRepresentation);
    CHECK(offline.spread == 0.0f);

    // And the selector honours it: with `forceTopRepresentation`, the decision is the top rung
    // whatever the metric says, so there is nothing left for a dead zone to be unstable about.
    rendering::ImportanceRecord tiny;
    tiny.pixelsPerTriangle = 0.01f;
    tiny.projectedRadius = 0.1f;
    tiny.projectedArea = 0.03f;
    tiny.triangles = 3000;
    const rendering::LodRung rungs[] = {{100.0f, 3000}, {40.0f, 750}, {10.0f, 180}};
    rendering::RepresentationChoice previous;
    previous.kind = rendering::Representation::MeshLod;
    previous.lodLevel = 2;
    const rendering::RepresentationChoice choice =
        rendering::RepresentationSelector::decide(tiny, rungs, offline, previous);
    CHECK(choice.kind == rendering::Representation::FullMesh);
    CHECK(choice.lodLevel == 0);
    CHECK_FALSE(choice.held);
}

// ---- the GPU ladder, which is the other hysteresis ------------------------------------------------

TEST_CASE("with no dead zone the LOD histogram is a function of where the camera is",
          "[gpu][certification][offline]") {
    // The control. With `lodHysteresis` at zero there is no memory anywhere in the ladder, so the
    // two arrival paths must agree exactly. If this fails, the experiment below cannot mean
    // anything -- the difference would be something other than the dead zone.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});

    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    renderer.setQuality(rendering::QualityTier::Offline);

    scene::Scene a = ladderScene(0.0f);
    scene::Scene b = ladderScene(0.0f);
    const Histogram far = walkAndSettle(renderer, a, kFromFar, 12);
    const Histogram near = walkAndSettle(renderer, b, kFromNear, 12);
    INFO("from far:  " << describe(far));
    INFO("from near: " << describe(near));
    // The fixture has to put instances on more than one rung, or "the histograms agree" is a
    // statement about an empty ladder.
    CHECK(far[0] + far[1] + far[2] + far[3] > 0);
    CHECK((far[1] > 0 || far[2] > 0));
    CHECK(far == near);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("an offline render does not depend on how the camera arrived",
          "[gpu][certification][offline]") {
    // ADR-125, transcribed: "hysteresis is opt-in, and offline never gets it". §5.9: an offline
    // render must not silently inherit a realtime compromise.
    //
    // The scene opts into the GPU ladder's dead zone, which is a legal thing for a scene to do, and
    // is then rendered at `QualityTier::Offline`. Whatever the tier does about the *representation
    // selector*, the image the offline path produces must be a function of where the camera is.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});

    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    renderer.setQuality(rendering::QualityTier::Offline);

    scene::Scene a = ladderScene(0.3f);
    scene::Scene b = ladderScene(0.3f);
    const Histogram far = walkAndSettle(renderer, a, kFromFar, 12);
    const Histogram near = walkAndSettle(renderer, b, kFromNear, 12);
    INFO("from far:  " << describe(far));
    INFO("from near: " << describe(near));
    INFO("both settled 12 frames at the same camera position, tier = offline, lodHysteresis = 0.3");
    CHECK(far == near);
    CHECK(ctx->errorCount() == 0);
}


// ---- which tier does an offline render actually run at? --------------------------------------------

namespace {
namespace fs = std::filesystem;

// How many bytes of two RGBA images differ, and by how much at worst.
struct Difference {
    std::size_t bytes = 0;
    int worst = 0;
    [[nodiscard]] bool identical() const { return bytes == 0; }
};

Difference compare(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    Difference d;
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        const int delta = std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
        if (delta != 0) {
            ++d.bytes;
            d.worst = std::max(d.worst, delta);
        }
    }
    return d;
}

} // namespace

TEST_CASE("an offline render is rendered at the offline tier", "[gpu][certification][offline][tier]") {
    // §5.9: an offline render is a deliverable and must not silently inherit a realtime compromise.
    // `QualityTier::Offline` is what that sentence is implemented as -- 4096-pixel shadow maps
    // against realtime's default, four cascades, 24 PCF taps, 24 contact steps, six AO slices, and
    // the ambient-occlusion and shadow-mask terms at full resolution instead of a fraction of it
    // (`QualitySettings::forTier`, render_quality.hpp).
    //
    // The question here is not what that tier does. It is whether the batch render path selects it.
    // Read on its own the code says no -- `SceneRenderer::setQuality` has exactly one caller,
    // `Application::setup`, which calls it on the *interactive* renderer, while `app::RenderJob`
    // constructs a renderer of its own (render_job.cpp:93) and `RenderSettings` has no tier field
    // for it to be told one through. But reading is how ADR-131's withdrawn result was reached, so
    // this asks the pixels instead.
    //
    // The experiment: render one frame through the real batch path, then render the same second
    // through the interactive path at each tier, and see which one the batch frame equals.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    const fs::path source = fs::path(AVGEN_SOURCE_DIR) / "examples" / "qa" / "renderer-qa.scene.json";
    if (!fs::is_regular_file(source)) {
        SKIP("the RendererQA scene is not present");
    }
    const fs::path dir = testsupport::processTempDir() / "phase_g_offline_tier";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path previous = fs::current_path();
    fs::current_path(dir);

    // A resolution big enough that a shadow map four times the size has somewhere to show up. The
    // RendererQA scene is the control the performance baselines are measured against and has a
    // floor, objects standing on it and a directional key -- which is exactly the content the tier
    // differences are about.
    constexpr std::uint32_t kW = 480;
    constexpr std::uint32_t kH = 300;
    constexpr double kSecond = 1.0;
    {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadComposition(source).has_value());
        engine.renderSettings().width = kW;
        engine.renderSettings().height = kH;
        engine.renderSettings().fps = 30.0;
        engine.renderSettings().startSeconds = kSecond;
        engine.renderSettings().endSeconds = kSecond + 0.01;
        engine.renderSettings().output = app::RenderOutput::PngSequence;
        engine.renderSettings().outputPath = "frames";
        REQUIRE(engine.saveProject("qa.json").has_value());
    }
    {
        auto engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
        REQUIRE(engine->loadProject("qa.json").has_value());
        app::RenderSettings settings = engine->renderSettings();
        app::RenderJob job(*ctx, shaders, std::move(engine), settings, fs::path("."));
        REQUIRE(job.start().has_value());
        REQUIRE(job.run().has_value());
    }
    const auto batch = assets::loadImage("frames/frame_000000.png", false);
    REQUIRE(batch.has_value());

    // The same project, the same second, through the interactive path at a named tier.
    const auto interactiveAt = [&](rendering::QualityTier tier) {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadProject("qa.json").has_value());
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        renderer.setQuality(tier);
        FixedStepClock clock(30.0);
        clock.restartAt(kSecond);
        const FrameTime time = engine.tick(clock);
        engine.setViewport(kW, kH);
        engine.update(time);
        auto image = renderer.renderToImage(engine.scene(), time, kW, kH);
        REQUIRE(image.has_value());
        return image->rgba;
    };

    const std::vector<std::uint8_t> realtime = interactiveAt(rendering::QualityTier::Realtime);
    const std::vector<std::uint8_t> offline = interactiveAt(rendering::QualityTier::Offline);

    // The premise: the two tiers must actually produce different pixels on this scene, or the test
    // below is comparing a frame against two identical things and cannot distinguish them. If this
    // fails, the fixture needs content the tier changes, not a weaker assertion.
    const Difference tiers = compare(realtime, offline);
    INFO("realtime vs offline, interactive path: " << tiers.bytes << " bytes differ, worst "
                                                   << tiers.worst);
    REQUIRE_FALSE(tiers.identical());

    const Difference againstRealtime = compare(batch->data, realtime);
    const Difference againstOffline = compare(batch->data, offline);
    INFO("batch vs realtime: " << againstRealtime.bytes << " bytes differ, worst "
                               << againstRealtime.worst);
    INFO("batch vs offline:  " << againstOffline.bytes << " bytes differ, worst "
                               << againstOffline.worst);

    // What §5.9 requires: the frame a batch render delivers is the offline tier's frame.
    CHECK(againstOffline.bytes <= againstRealtime.bytes);

    fs::current_path(previous);
    fs::remove_all(dir);
    CHECK(ctx->errorCount() == 0);
}
