// The 2D composition over the finished 3D frame (ADR-083), on the device: that text actually
// appears, that it appears in the right place at any resolution, that compositing order is the
// stack order, that a layer outside its time range is absent, and -- the one that matters most --
// that the frame an offline render produces is the frame live playback produces.

#include "comp/layer_stack.hpp"
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/composition_renderer.hpp"
#include "app/engine.hpp"
#include "app/render_job.hpp"
#include "assets/asset_registry.hpp"
#include "assets/image.hpp"
#include "rendering/reference_renderer.hpp"
#include "rendering/renderer_snapshot.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "scene/composition.hpp"
#include "support/temp_dir.hpp"

#include <fmt/format.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <array>
#include <algorithm>
#include <bit>
#include <functional>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <numeric>
#include <set>
#include <optional>
#include <ranges>
#include <memory>
#include <string>

using namespace avgen;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

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

// A deliberately plain, entirely black 3D frame, so every non-black pixel in the output came from
// the composition and nothing else.
scene::Scene blackScene() {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.camera.position = {0.0f, 0.0f, 8.0f};
    s.post.bloomEnabled = false;
    s.post.tonemap = scene::TonemapOperator::AgX;
    return s;
}

int luma(const std::uint8_t* px) { return px[0] + px[1] + px[2]; }

std::uint64_t litPixels(const gpu::Image8& img, int threshold = 24) {
    std::uint64_t count = 0;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            if (luma(img.pixel(x, y)) > threshold) {
                ++count;
            }
        }
    }
    return count;
}

// Centre of mass of the lit pixels, normalised to the frame. The one measurement that says "the
// words are where the author put them" without depending on which letters they are.
glm::vec2 centroid(const gpu::Image8& img, int threshold = 24) {
    double sx = 0.0;
    double sy = 0.0;
    double total = 0.0;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const double w = std::max(0, luma(img.pixel(x, y)) - threshold);
            sx += w * x;
            sy += w * y;
            total += w;
        }
    }
    if (total <= 0.0) {
        return {-1.0f, -1.0f};
    }
    // Back to composition coordinates: origin bottom left, y up.
    return {static_cast<float>(sx / total / img.width), 1.0f - static_cast<float>(sy / total / img.height)};
}

struct Harness {
    std::unique_ptr<gpu::Context> ctx;
    std::unique_ptr<gpu::ShaderLibrary> shaders;
    std::unique_ptr<rendering::SceneRenderer> renderer;
    std::unique_ptr<rendering::CompositionRenderer> compositor;

    static Harness make() {
        Harness h;
        h.ctx = makeContext();
        h.shaders = std::make_unique<gpu::ShaderLibrary>(*h.ctx,
                                                         std::vector{std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
        h.renderer = std::make_unique<rendering::SceneRenderer>(*h.ctx, *h.shaders);
        REQUIRE(h.renderer->init().has_value());
        h.compositor = std::make_unique<rendering::CompositionRenderer>(*h.ctx, *h.shaders);
        REQUIRE(h.compositor->init().has_value());
        h.compositor->setTimeline(&h.renderer->timeline());
        h.renderer->setOverlay(h.compositor.get());
        return h;
    }

    gpu::Image8 shot(const scene::Scene& scene, comp::LayerStack& stack, double seconds, std::uint32_t w,
                     std::uint32_t h) {
        compositor->setInput(&stack, seconds);
        FrameTime time{};
        time.renderTime = seconds;
        auto image = renderer->renderToImage(scene, time, w, h);
        REQUIRE(image.has_value());
        return std::move(*image);
    }
};

} // namespace

TEST_CASE("authored animated character culling changes pixels and recovers", "[gpu][composition][skinning]") {
    const fs::path sceneFile = fs::path(AVGEN_SOURCE_DIR) / "examples" / "characters" / "alien.scene.json";
    if (!fs::is_regular_file(sceneFile)) {
        SKIP("the alien composition is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    assets::AssetRegistry registry(sceneFile.parent_path());
    auto loaded = scene::Composition::loadFile(sceneFile, registry);
    REQUIRE(loaded.has_value());
    auto& composition = **loaded;
    params::ParameterSet parameters;
    params::Modulator modulator;
    composition.attach(parameters, modulator);
    composition.setViewport(640, 360);

    FrameTime time{};
    time.renderTime = 0.4;
    composition.update(time);
    const auto original = renderer.renderToImage(composition.scene(), time, 640, 360);
    REQUIRE(original.has_value());

    auto* idlePosition = parameters.findAs<glm::vec3>("nodes/idle/position");
    REQUIRE(idlePosition != nullptr);
    idlePosition->setBase({100.0f, 0.0f, 0.0f});
    parameters.resetFinals();
    composition.update(time);
    bool idleCulled = false;
    for (const scene::Entity& entity : composition.scene().entities) {
        if (entity.rig == 0) {
            idleCulled = idleCulled || entity.cameraCulled;
        }
    }
    CHECK(idleCulled);
    const auto hidden = renderer.renderToImage(composition.scene(), time, 640, 360);
    REQUIRE(hidden.has_value());
    CHECK(gpu::hashImage(*hidden) != gpu::hashImage(*original));

    idlePosition->setBase({-1.25f, 0.0f, 0.0f});
    parameters.resetFinals();
    composition.update(time);
    for (const scene::Entity& entity : composition.scene().entities) {
        if (entity.rig == 0) {
            CHECK_FALSE(entity.cameraCulled);
        }
    }
    const auto restored = renderer.renderToImage(composition.scene(), time, 640, 360);
    REQUIRE(restored.has_value());
    CHECK(gpu::hashImage(*restored) == gpu::hashImage(*original));
    CHECK(ctx->errorCount() == 0);
}

// ---- Phase 9.2: the frame-100 -> frame-500 -> frame-100 replay ---------------------------------
//
// The plan names this experiment exactly. Its point is not that a renderer is deterministic from a
// cold start -- that is already covered -- but that it is deterministic *after having been somewhere
// else*. Every temporal store in the frame is a chance for frame 100 reached forwards and frame 100
// reached backwards to differ, and every one of those failures looks like "the image flickers when I
// scrub".
//
// **It is run through `Engine`, and that is the finding.** Driving `Composition::update` straight
// from t=3.3 s to t=16.7 s produces a *different scene* at 16.7 s than loading fresh and going
// there: measured, with a fresh renderer over each, 18196457925992915825 against
// 9262657865428539071. That is not a defect. `Composition::update` is playback, not a seek -- a
// jump integrates stateful simulation across the gap -- and `Engine::seekSeconds` is the operation
// that makes a time jump reproducible (it reseeds modulation, sources, the music classifier, the
// cue state, the entity world and the event scheduler). A forensic test that skips it is testing an
// API contract nobody uses and calling the result a renderer bug.
TEST_CASE("frame 100 replays identically after a seek to 500 and back",
          "[gpu][composition][forensics][determinism]") {
    const fs::path sceneFile = fs::path(AVGEN_SOURCE_DIR) / "examples" / "characters" / "alien.scene.json";
    if (!fs::is_regular_file(sceneFile)) {
        SKIP("the alien composition is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});

    constexpr std::uint32_t kW = 256;
    constexpr std::uint32_t kH = 160;
    constexpr double kFps = 30.0;
    constexpr double kNear = 100.0 / kFps;
    constexpr double kFar = 500.0 / kFps;

    // One engine, one renderer, seeked between the two times exactly as the transport does.
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(sceneFile).has_value());
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const auto renderAt = [&](double seconds) {
        engine.seekSeconds(seconds);
        FixedStepClock clock(kFps);
        clock.restartAt(seconds);
        const FrameTime time = engine.tick(clock);
        engine.setViewport(kW, kH);
        engine.update(time);
        renderer.resetTemporalHistory(); // a seek is a discontinuity, not motion
        auto image = renderer.renderToImage(engine.scene(), time, kW, kH);
        REQUIRE(image.has_value());
        return gpu::hashImage(*image);
    };

    // The references: an engine and a renderer that have been nowhere else.
    const auto renderFresh = [&](double seconds) {
        app::Engine fresh(app::EngineMode::Offline);
        REQUIRE(fresh.loadComposition(sceneFile).has_value());
        rendering::SceneRenderer freshRenderer(*ctx, shaders);
        REQUIRE(freshRenderer.init().has_value());
        fresh.seekSeconds(seconds);
        FixedStepClock clock(kFps);
        clock.restartAt(seconds);
        const FrameTime time = fresh.tick(clock);
        fresh.setViewport(kW, kH);
        fresh.update(time);
        auto image = freshRenderer.renderToImage(fresh.scene(), time, kW, kH);
        REQUIRE(image.has_value());
        return gpu::hashImage(*image);
    };

    const std::uint64_t freshNear = renderFresh(kNear);
    const std::uint64_t freshFar = renderFresh(kFar);
    CHECK(freshNear != freshFar); // the scene really does animate between the two

    // Where a divergence would be, asserted directly: an image hash says "something differs" and
    // this says which subsystem. The first time this ran it reported 98 differing joint matrices
    // with every entity transform identical, which is what identified the animation phase origin as
    // the cause rather than transforms, culling or the renderer.
    {
        app::Engine direct(app::EngineMode::Offline);
        REQUIRE(direct.loadComposition(sceneFile).has_value());
        direct.seekSeconds(kFar);
        FixedStepClock dc(kFps);
        dc.restartAt(kFar);
        FrameTime dt = direct.tick(dc);
        direct.setViewport(kW, kH);
        direct.update(dt);

        app::Engine viaNear(app::EngineMode::Offline);
        REQUIRE(viaNear.loadComposition(sceneFile).has_value());
        viaNear.seekSeconds(kNear);
        FixedStepClock nc(kFps);
        nc.restartAt(kNear);
        FrameTime nt = viaNear.tick(nc);
        viaNear.setViewport(kW, kH);
        viaNear.update(nt);
        viaNear.seekSeconds(kFar);
        FixedStepClock fc(kFps);
        fc.restartAt(kFar);
        nt = viaNear.tick(fc);
        viaNear.setViewport(kW, kH);
        viaNear.update(nt);

        const scene::Scene& a = direct.scene();
        const scene::Scene& b = viaNear.scene();
        REQUIRE(a.rigs.size() == b.rigs.size());
        REQUIRE(a.entities.size() == b.entities.size());
        std::size_t jointsDiffering = 0;
        for (std::size_t r = 0; r < a.rigs.size(); ++r) {
            REQUIRE(a.rigs[r].palette.size() == b.rigs[r].palette.size());
            for (std::size_t j = 0; j < a.rigs[r].palette.size(); ++j) {
                jointsDiffering += a.rigs[r].palette[j] == b.rigs[r].palette[j] ? 0 : 1;
            }
        }
        std::size_t transformsDiffering = 0;
        for (std::size_t e = 0; e < a.entities.size(); ++e) {
            transformsDiffering +=
                a.entities[e].transform.matrix() == b.entities[e].transform.matrix() ? 0 : 1;
        }
        INFO(jointsDiffering << " joint matrices and " << transformsDiffering
                             << " entity transforms differ at the same second");
        CHECK(jointsDiffering == 0);
        CHECK(transformsDiffering == 0);
    }
    const std::uint64_t walkedNear = renderAt(kNear);
    const std::uint64_t walkedFar = renderAt(kFar);
    const std::uint64_t returnedNear = renderAt(kNear);

    INFO("fresh 100 = " << freshNear << ", walked 100 = " << walkedNear << ", far " << walkedFar
                        << " vs fresh far " << freshFar << ", returned 100 = " << returnedNear);
    CHECK(walkedNear == freshNear);
    CHECK(walkedFar == freshFar);
    // The experiment: coming back is the same as arriving.
    CHECK(returnedNear == freshNear);

    // Repeated, because a single round trip can hide a store that needs two to diverge.
    for (int lap = 0; lap < 3; ++lap) {
        INFO("lap " << lap);
        REQUIRE(renderAt(kFar) == freshFar);
        REQUIRE(renderAt(kNear) == freshNear);
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("RendererQA camera cuts match fresh renderers", "[gpu][composition][forensics]") {
    const fs::path sceneFile = fs::path(AVGEN_SOURCE_DIR) / "examples" / "qa" / "renderer-qa.scene.json";
    if (!fs::is_regular_file(sceneFile) ||
        !fs::is_regular_file(fs::path(AVGEN_SOURCE_DIR) / "assets" / "imported" / "alien.gltf")) {
        SKIP("RendererQA or its alien asset is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    assets::AssetRegistry registry(sceneFile.parent_path());
    auto loaded = scene::Composition::loadFile(sceneFile, registry);
    REQUIRE(loaded.has_value());
    auto& composition = **loaded;
    params::ParameterSet parameters;
    params::Modulator modulator;
    composition.attach(parameters, modulator);
    composition.setViewport(640, 360);
    FrameTime time{};
    time.frameIndex = 0;
    composition.update(time);

    struct CameraState {
        glm::vec3 position;
        glm::vec3 target;
    };
    const std::array<CameraState, 4> states = {
        CameraState{{0.0f, 5.0f, 16.0f}, {0.0f, 1.0f, -8.0f}},
        CameraState{{-7.0f, 3.0f, 8.0f}, {0.0f, 1.0f, -5.0f}},
        CameraState{{0.0f, 1.5f, 2.0f}, {0.0f, 1.0f, -5.0f}},
        CameraState{{0.0f, 5.0f, 30.0f}, {0.0f, 1.0f, -90.0f}},
    };
    std::array<std::uint64_t, states.size()> hashes{};
    for (std::size_t i = 0; i < states.size(); ++i) {
        composition.scene().camera.position = states[i].position;
        composition.scene().camera.target = states[i].target;
        renderer.resetTemporalHistory();
        const auto reused = renderer.renderToImage(composition.scene(), time, 640, 360);
        REQUIRE(reused.has_value());

        rendering::SceneRenderer fresh(*ctx, shaders);
        REQUIRE(fresh.init().has_value());
        const auto expected = fresh.renderToImage(composition.scene(), time, 640, 360);
        REQUIRE(expected.has_value());
        CHECK(gpu::hashImage(*reused) == gpu::hashImage(*expected));
        hashes[i] = gpu::hashImage(*reused);
    }
    CHECK(hashes[0] != hashes[1]);
    CHECK(hashes[1] != hashes[2]);
    CHECK(hashes[2] != hashes[3]);

    rendering::SceneRenderer sequenceA(*ctx, shaders);
    rendering::SceneRenderer sequenceB(*ctx, shaders);
    REQUIRE(sequenceA.init().has_value());
    REQUIRE(sequenceB.init().has_value());
    for (std::uint64_t frame = 0; frame < 8; ++frame) {
        const float angle = static_cast<float>(frame) * 0.35f;
        composition.scene().camera.position = {std::sin(angle) * 8.0f, 2.5f + 0.2f * static_cast<float>(frame),
                                               10.0f + std::cos(angle) * 4.0f};
        composition.scene().camera.target = {0.0f, 1.0f, -8.0f};
        time.frameIndex = frame;
        time.renderTime = static_cast<double>(frame) / 30.0;
        time.deltaTime = 1.0 / 30.0;
        const std::uint32_t width = frame % 2 == 0 ? 640u : 576u;
        const std::uint32_t height = frame % 2 == 0 ? 360u : 324u;
        const auto a = sequenceA.renderToImage(composition.scene(), time, width, height);
        const auto b = sequenceB.renderToImage(composition.scene(), time, width, height);
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        CHECK(gpu::hashImage(*a) == gpu::hashImage(*b));
    }

    rendering::SceneRenderer seekA(*ctx, shaders);
    rendering::SceneRenderer seekB(*ctx, shaders);
    REQUIRE(seekA.init().has_value());
    REQUIRE(seekB.init().has_value());
    for (const double seconds : {0.0, 1.0, 0.25}) {
        time.renderTime = seconds;
        time.frameIndex += 1;
        composition.update(time);
        seekA.resetTemporalHistory();
        seekB.resetTemporalHistory();
        const auto a = seekA.renderToImage(composition.scene(), time, 640, 360);
        const auto b = seekB.renderToImage(composition.scene(), time, 640, 360);
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        CHECK(gpu::hashImage(*a) == gpu::hashImage(*b));
    }

    auto reloaded = scene::Composition::loadFile(sceneFile, registry);
    REQUIRE(reloaded.has_value());
    auto& reloadedComposition = **reloaded;
    params::ParameterSet reloadedParameters;
    params::Modulator reloadedModulator;
    reloadedComposition.attach(reloadedParameters, reloadedModulator);
    reloadedComposition.setViewport(640, 360);
    reloadedComposition.update(time);
    seekA.resetTemporalHistory();
    const auto reloadImage = seekA.renderToImage(reloadedComposition.scene(), time, 640, 360);
    REQUIRE(reloadImage.has_value());
    rendering::SceneRenderer freshReload(*ctx, shaders);
    REQUIRE(freshReload.init().has_value());
    const auto freshReloadImage = freshReload.renderToImage(reloadedComposition.scene(), time, 640, 360);
    REQUIRE(freshReloadImage.has_value());
    CHECK(gpu::hashImage(*reloadImage) == gpu::hashImage(*freshReloadImage));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("text appears over the 3D frame", "[gpu][composition]") {
    Harness h = Harness::make();
    const scene::Scene scene = blackScene();

    comp::LayerStack empty;
    const gpu::Image8 without = h.shot(scene, empty, 0.0, 320, 180);
    CHECK(litPixels(without) == 0);

    comp::LayerStack stack;
    auto& text = stack.addText("HELLO");
    text.size = 0.3f;
    text.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    const gpu::Image8 with = h.shot(scene, stack, 0.0, 320, 180);
    CHECK(litPixels(with) > 200);

    // White text is actually white: this is what compositing after the tone map buys.
    int brightest = 0;
    for (std::uint32_t y = 0; y < with.height; ++y) {
        for (std::uint32_t x = 0; x < with.width; ++x) {
            brightest = std::max(brightest, luma(with.pixel(x, y)));
        }
    }
    CHECK(brightest >= 3 * 250);
}

TEST_CASE("text lands where it was placed, at every resolution", "[gpu][composition]") {
    Harness h = Harness::make();
    const scene::Scene scene = blackScene();
    comp::LayerStack stack;
    auto& text = stack.addText("OO");
    text.size = 0.2f;
    text.position = glm::vec2(0.25f, 0.75f);

    for (const auto [w, hh] : {std::pair<std::uint32_t, std::uint32_t>{320, 180},
                               std::pair<std::uint32_t, std::uint32_t>{640, 360},
                               std::pair<std::uint32_t, std::uint32_t>{512, 512}}) {
        const gpu::Image8 image = h.shot(scene, stack, 0.0, w, hh);
        const glm::vec2 c = centroid(image);
        INFO(w << "x" << hh);
        CHECK(c.x == Approx(0.25f).margin(0.04f));
        CHECK(c.y == Approx(0.75f).margin(0.04f));
    }
}

TEST_CASE("a layer outside its time range does not draw", "[gpu][composition]") {
    Harness h = Harness::make();
    const scene::Scene scene = blackScene();
    comp::LayerStack stack;
    auto& text = stack.addText("LATER");
    text.size = 0.3f;
    text.startTime = 5.0;
    text.endTime = 7.0;

    CHECK(litPixels(h.shot(scene, stack, 1.0, 256, 144)) == 0);
    CHECK(litPixels(h.shot(scene, stack, 6.0, 256, 144)) > 100);
    CHECK(litPixels(h.shot(scene, stack, 9.0, 256, 144)) == 0);
}

TEST_CASE("compositing order is stack order", "[gpu][composition]") {
    Harness h = Harness::make();
    const scene::Scene scene = blackScene();
    comp::LayerStack stack;
    auto& red = stack.addShape(comp::ShapeKind::Rectangle);
    red.size = glm::vec2(0.6f, 0.6f);
    red.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    auto& green = stack.addShape(comp::ShapeKind::Rectangle);
    green.size = glm::vec2(0.3f, 0.3f);
    green.color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);

    const gpu::Image8 image = h.shot(scene, stack, 0.0, 128, 128);
    const std::uint8_t* centre = image.pixel(64, 64);
    CHECK(centre[1] > 200); // green on top in the middle
    CHECK(centre[0] < 60);
    // The big square spans 26..102 px at this size and the small one 45..83, so this row is
    // inside the first and outside the second.
    const std::uint8_t* ring = image.pixel(64, 32);
    CHECK(ring[0] > 200);
    CHECK(ring[1] < 60);

    // Move the red square to the top of the stack and it covers the green one.
    REQUIRE(stack.moveTo(red.id, 1));
    const gpu::Image8 swapped = h.shot(scene, stack, 0.0, 128, 128);
    const std::uint8_t* centre2 = swapped.pixel(64, 64);
    CHECK(centre2[0] > 200);
    CHECK(centre2[1] < 60);
}

TEST_CASE("opacity composites against what is underneath", "[gpu][composition]") {
    Harness h = Harness::make();
    const scene::Scene scene = blackScene();
    comp::LayerStack stack;
    auto& under = stack.addShape(comp::ShapeKind::Rectangle);
    under.size = glm::vec2(0.8f, 0.8f);
    under.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    auto& over = stack.addShape(comp::ShapeKind::Rectangle);
    over.size = glm::vec2(0.4f, 0.4f);
    over.color = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);

    over.opacity = 1.0f;
    const std::uint8_t* opaque = nullptr;
    gpu::Image8 a = h.shot(scene, stack, 0.0, 128, 128);
    opaque = a.pixel(64, 64);
    const int opaqueBlue = opaque[2];
    const int opaqueRed = opaque[0];

    over.opacity = 0.5f;
    gpu::Image8 b = h.shot(scene, stack, 0.0, 128, 128);
    const std::uint8_t* half = b.pixel(64, 64);
    CHECK(half[2] < opaqueBlue - 40);
    CHECK(half[0] > opaqueRed + 40); // the red underneath now shows through

    over.opacity = 0.0f;
    gpu::Image8 c = h.shot(scene, stack, 0.0, 128, 128);
    const std::uint8_t* gone = c.pixel(64, 64);
    CHECK(gone[2] < 20);
    CHECK(gone[0] > 200);
}

TEST_CASE("a shape can be a border: stroke only, no fill", "[gpu][composition]") {
    Harness h = Harness::make();
    const scene::Scene scene = blackScene();
    comp::LayerStack stack;
    auto& border = stack.addShape(comp::ShapeKind::Rectangle);
    border.size = glm::vec2(0.7f, 0.7f);
    border.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f); // no fill
    border.strokeWidth = 0.04f;
    border.strokeColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

    const gpu::Image8 image = h.shot(scene, stack, 0.0, 128, 128);
    CHECK(luma(image.pixel(64, 64)) < 24);            // hollow in the middle
    CHECK(luma(image.pixel(64, 19)) > 300);           // the top edge of the box is drawn
    CHECK(luma(image.pixel(4, 4)) < 24);              // and nothing outside it
}

TEST_CASE("the same second produces the same frame however it was reached",
          "[gpu][composition][determinism]") {
    Harness h = Harness::make();
    const scene::Scene scene = blackScene();
    comp::LayerStack stack;
    auto& text = stack.addText("DRIFT");
    text.size = 0.25f;
    text.startTime = 1.0;

    // "Live": stepped forward one frame at a time at 60 fps to 2.0 s.
    gpu::Image8 stepped;
    for (int i = 0; i <= 120; ++i) {
        stepped = h.shot(scene, stack, static_cast<double>(i) / 60.0, 192, 108);
    }
    // "Offline": the same second, arrived at by seeking.
    const gpu::Image8 sought = h.shot(scene, stack, 2.0, 192, 108);

    REQUIRE(stepped.rgba.size() == sought.rgba.size());
    std::size_t differing = 0;
    for (std::size_t i = 0; i < stepped.rgba.size(); ++i) {
        if (stepped.rgba[i] != sought.rgba[i]) {
            ++differing;
        }
    }
    CHECK(differing == 0);
}

// `[.perf]`, not the plain `[performance]` this used to carry: `CHECK(hundred.cpuBuildMs < 1.0)`
// below is a wall-clock threshold, so -- like every other timing test in this repo -- it is hidden
// from a default `ctest` run rather than left for `RESOURCE_LOCK gpu` to protect, since that lock
// only keeps this project's own GPU tests from overlapping each other and has no way to see a
// second process (another agent's render, a concurrent ctest invocation) on the same machine.
TEST_CASE("a hundred text layers stay one pass and a handful of draws",
          "[gpu][composition][.perf]") {
    Harness h = Harness::make();
    const scene::Scene scene = blackScene();
    // 1080p, because a cost per pixel is the only cost this pass really has, and quoting it at
    // 320x180 would be quoting nothing.
    constexpr std::uint32_t kWidth = 1920;
    constexpr std::uint32_t kHeight = 1080;

    struct Sample {
        std::size_t layers = 0;
        double gpuMs = -1.0;
        double cpuBuildMs = 0.0;
        double rebuildMs = 0.0;
        std::uint32_t draws = 0;
        std::uint32_t items = 0;
        std::uint32_t vertices = 0;
        std::uint32_t glyphs = 0;
    };

    const auto measure = [&](std::size_t count) {
        comp::LayerStack stack;
        for (std::size_t i = 0; i < count; ++i) {
            auto& text = stack.addText("a line of lyric");
            text.size = 0.035f;
            text.position = glm::vec2(0.5f, 0.03f + 0.94f * static_cast<float>(i) / static_cast<float>(count + 1));
        }
        // The authoring-time cost: shaping and rasterising everything from cold.
        const auto rebuildStart = std::chrono::steady_clock::now();
        stack.build(comp::Frame{kWidth, kHeight}, 0.0);
        const double rebuildMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - rebuildStart).count();

        // Warm the pipelines and let the timestamp ring fill.
        for (int f = 0; f < 6; ++f) {
            h.shot(scene, stack, 0.0, kWidth, kHeight);
        }
        Sample best;
        best.layers = count;
        best.rebuildMs = rebuildMs;
        constexpr int kFrames = 24;
        double total = 0.0;
        int counted = 0;
        for (int f = 0; f < kFrames; ++f) {
            // A different second every frame, so nothing is cached that would not be in a real
            // animation: every item is rebuilt and re-uploaded.
            h.shot(scene, stack, 0.01 * f, kWidth, kHeight);
            h.compositor->collectTimings();
            const auto& stats = h.compositor->stats();
            if (stats.gpuMs >= 0.0) {
                total += stats.gpuMs;
                ++counted;
            }
            best.cpuBuildMs = std::max(best.cpuBuildMs, stats.cpuBuildMs);
            best.draws = stats.draws;
            best.items = stats.items;
            best.vertices = stats.vertices;
            best.glyphs = stats.glyphs;
        }
        best.gpuMs = counted > 0 ? total / counted : -1.0;
        return best;
    };

    std::string table = "composition cost at 1920x1080 (GPU pass / CPU per-frame build / cold rebuild):\n";
    Sample hundred;
    for (std::size_t count : {std::size_t{0}, std::size_t{1}, std::size_t{10}, std::size_t{100},
                              std::size_t{200}}) {
        const Sample s = measure(count);
        table += fmt::format("  {:>3} layers: gpu {:7.4f} ms | build {:6.4f} ms | cold {:7.3f} ms | "
                             "{} draw(s), {} items, {} verts, {} glyphs\n",
                             s.layers, s.gpuMs, s.cpuBuildMs, s.rebuildMs, s.draws, s.items, s.vertices,
                             s.glyphs);
        if (count == 100) {
            hundred = s;
        }
    }
    WARN(table);

    CHECK(hundred.layers == 100);
    // A hundred plain text layers share one blend mode and contiguous geometry, so they are one
    // draw call. This is the property that stops the system collapsing at scale.
    CHECK(hundred.draws == 1);
    // The same letters in a hundred layers rasterise once.
    CHECK(hundred.glyphs <= 16);
    // And the per-frame CPU work is uploading items, not rebuilding anything.
    CHECK(hundred.cpuBuildMs < 1.0);
}

// ---- the offline path ---------------------------------------------------------------------
// The one that has caught this engine out before: a feature that is correct in memory and absent
// from the render, because the offline renderer reloads the project from a file. These drive the
// real RenderJob over a real project document.

TEST_CASE("an offline render contains the composition, reproducibly",
          "[gpu][composition][determinism]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const fs::path dir = testsupport::processTempDir() / "composition_offline";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path previous = fs::current_path();
    fs::current_path(dir);

    // Two projects that differ only in whether they carry a composition.
    const auto writeProject = [&](const fs::path& file, bool withText) {
        app::Engine engine(app::EngineMode::Offline);
        if (withText) {
            auto& text = engine.addTextLayer("OFFLINE", 0.0, 0.0);
            text.size = 0.3f;
            text.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            text.pushAuthored();
        }
        engine.renderSettings().width = 160;
        engine.renderSettings().height = 90;
        engine.renderSettings().fps = 30.0;
        engine.renderSettings().startSeconds = 0.0;
        engine.renderSettings().endSeconds = 0.2;
        engine.renderSettings().output = app::RenderOutput::PngSequence;
        engine.renderSettings().outputPath = file.stem().string() + "_frames";
        REQUIRE(engine.saveProject(file).has_value());
    };
    writeProject("plain.json", false);
    writeProject("titled.json", true);

    const auto run = [&](const fs::path& project) {
        auto engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
        REQUIRE(engine->loadProject(project).has_value());
        app::RenderSettings settings = engine->renderSettings();
        app::RenderJob job(*ctx, shaders, std::move(engine), settings, fs::path("."));
        REQUIRE(job.start().has_value());
        REQUIRE(job.run().has_value());
        return job.progress().sequenceHash;
    };

    const std::uint64_t plain = run("plain.json");
    const std::uint64_t titled = run("titled.json");
    const std::uint64_t titledAgain = run("titled.json");

    // The composition survived the save, the load and the render...
    CHECK(plain != titled);
    // ...and the render is reproducible, which is what the sequence hash is for.
    CHECK(titled == titledAgain);

    // And it is really text on the picture, not just different bytes.
    auto image = assets::loadImage("titled_frames/frame_000000.png", false);
    REQUIRE(image.has_value());
    std::uint64_t lit = 0;
    for (std::size_t i = 0; i + 3 < image->data.size(); i += 4) {
        if (image->data[i] > 128) {
            ++lit;
        }
    }
    CHECK(lit > 50);

    fs::current_path(previous);
    fs::remove_all(dir);
}

TEST_CASE("a frame rendered offline matches the same frame rendered live",
          "[gpu][composition][determinism]") {
    // The live window path encodes SceneRenderer::render into a target and presents it; the
    // offline path encodes the same call into a texture it reads back. Same engine, same second,
    // so the pixels have to agree -- and would not if the composition were driven by a frame
    // counter, a wall clock or anything else the two paths do not share.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const fs::path dir = testsupport::processTempDir() / "composition_equivalence";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path previous = fs::current_path();
    fs::current_path(dir);

    constexpr std::uint32_t kWidth = 160;
    constexpr std::uint32_t kHeight = 90;
    {
        app::Engine engine(app::EngineMode::Offline);
        auto& text = engine.addTextLayer("MATCH", 0.5, 0.0);
        text.size = 0.28f;
        text.pushAuthored();
        params::Track track;
        track.target = text.parameterPath("opacity");
        track.addKey(params::Key{0.5, {0.0f, 0, 0, 0}, params::KeyInterp::EaseInOut, {}, {}});
        track.addKey(params::Key{2.5, {1.0f, 0, 0, 0}, params::KeyInterp::EaseInOut, {}, {}});
        engine.timeline().addTrack(track);
        engine.renderSettings().width = kWidth;
        engine.renderSettings().height = kHeight;
        engine.renderSettings().fps = 30.0;
        engine.renderSettings().startSeconds = 1.0;
        engine.renderSettings().endSeconds = 1.05;
        engine.renderSettings().output = app::RenderOutput::PngSequence;
        engine.renderSettings().outputPath = "frames";
        REQUIRE(engine.saveProject("show.json").has_value());
    }
    {
        auto engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
        REQUIRE(engine->loadProject("show.json").has_value());
        app::RenderSettings settings = engine->renderSettings();
        app::RenderJob job(*ctx, shaders, std::move(engine), settings, fs::path("."));
        REQUIRE(job.start().has_value());
        REQUIRE(job.run().has_value());
    }
    auto offline = assets::loadImage("frames/frame_000000.png", false);
    REQUIRE(offline.has_value());

    // The same project, the same second, through the interactive path's encode.
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject("show.json").has_value());
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    rendering::CompositionRenderer compositor(*ctx, shaders);
    REQUIRE(compositor.init().has_value());
    renderer.setOverlay(&compositor);
    FixedStepClock clock(30.0);
    clock.restartAt(1.0);
    const FrameTime time = engine.tick(clock);
    engine.setViewport(kWidth, kHeight);
    engine.update(time);
    compositor.setInput(&engine.layers(), engine.timelineClock().seconds);
    auto live = renderer.renderToImage(engine.scene(), time, kWidth, kHeight);
    REQUIRE(live.has_value());

    REQUIRE(offline->data.size() == live->rgba.size());
    std::size_t differing = 0;
    int worst = 0;
    for (std::size_t i = 0; i < live->rgba.size(); ++i) {
        const int delta = std::abs(static_cast<int>(offline->data[i]) - static_cast<int>(live->rgba[i]));
        if (delta != 0) {
            ++differing;
            worst = std::max(worst, delta);
        }
    }
    INFO("differing bytes " << differing << ", worst delta " << worst);
    CHECK(differing == 0);

    // The keyframe is doing something at this second: a quarter of the way into a two-second ease.
    const comp::Layer* layer = engine.layers().at(0);
    REQUIRE(layer != nullptr);
    CHECK(layer->resolvedOpacity() > 0.0f);
    CHECK(layer->resolvedOpacity() < 1.0f);

    fs::current_path(previous);
    fs::remove_all(dir);
}

namespace {

// A composition's camera is **not** `scene().camera`. That field is re-derived from `camera/mode`,
// `camera/position` and `camera/target` (or the orbit block) on every `applyParameters`, so a test
// that writes it directly is overwritten before the frame is drawn -- and then asserts that nothing
// moved while the camera in fact stood still, which is a test that cannot fail. Both composition
// tests below were written that way first.
//
// This is also the answer to a note in the 11 September QA record ("camera overrides via the
// project's parameters did not move the camera"): free mode has to be selected, or position and
// target are computed by the orbit and the writes are ignored.
void aimCompositionCamera(params::ParameterSet& parameters, glm::vec3 position, glm::vec3 target) {
    auto* mode = parameters.findAs<int>("camera/mode");
    auto* pos = parameters.findAs<glm::vec3>("camera/position");
    auto* aim = parameters.findAs<glm::vec3>("camera/target");
    REQUIRE(mode != nullptr);
    REQUIRE(pos != nullptr);
    REQUIRE(aim != nullptr);
    mode->setBase(1); // free
    pos->setBase(position);
    aim->setBase(target);
    parameters.resetFinals();
}

} // namespace

// ---- Phase 10.1 / SYM-STATIC-1: the composition-side half ---------------------------------------
//
// `tests/rendering/test_gpu.cpp` proves `SceneRenderer` does not move a static object. That covers
// the renderer and nothing else: the reported symptom is on the *composition* path, where a node's
// authored transform is flattened through a parent chain into an entity every frame, terrain
// grounding may write a Y, and the sequencer may write anything.
//
// So this drives the same five camera motions through `Engine` over the RendererQA scene and asserts
// the two places the transform lives -- the authored `CompositionNode::transform` and the flattened
// `Entity::transform` it produces -- are bit-identical on every frame. The flattening is re-run each
// update, so a transform that drifts would drift here and nowhere the renderer test could see it.
TEST_CASE("a composition's static nodes hold their transforms under camera motion",
          "[gpu][composition][forensics][static]") {
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "qa" / "renderer-qa.scene.json";
    if (!fs::is_regular_file(project)) {
        SKIP("the RendererQA scene is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(project).has_value());
    scene::Composition* composition = engine.composition();
    REQUIRE(composition != nullptr);

    // Every node the scene authors as static geometry, and the entity range each produced.
    const std::vector<std::string> staticNodes{"floor", "near-cube", "far-cube", "behind-camera",
                                               "transparent-orb"};
    struct Authored {
        std::string node;
        glm::mat4 nodeMatrix{1.0f};
    };
    std::vector<Authored> authored;

    constexpr std::uint32_t kW = 192;
    constexpr std::uint32_t kH = 120;
    const auto step = [&](std::uint64_t index, double seconds) {
        FixedStepClock clock(60.0);
        clock.restartAt(seconds);
        const FrameTime time = engine.tick(clock);
        engine.setViewport(kW, kH);
        engine.update(time);
        auto image = renderer.renderToImage(engine.scene(), time, kW, kH);
        REQUIRE(image.has_value());
        static_cast<void>(index);
    };

    step(0, 0.0);
    for (const std::string& name : staticNodes) {
        const scene::CompositionNode* node = composition->findNode(name);
        if (node == nullptr) {
            continue; // the scene may be edited; assert on what it actually has
        }
        authored.push_back({name, composition->nodeWorldTransform(*node).matrix()});
    }
    REQUIRE(authored.size() >= 4);

    // The entity transforms the flatten produced, keyed by entity name so a re-ordered scene does
    // not silently compare different objects.
    std::vector<std::pair<std::string, glm::mat4>> entityTransforms;
    for (const scene::Entity& e : engine.scene().entities) {
        entityTransforms.emplace_back(e.name, e.transform.matrix());
    }
    REQUIRE_FALSE(entityTransforms.empty());

    struct Pose {
        glm::vec3 position;
        glm::vec3 target;
    };
    struct Motion {
        const char* name;
        int frames;
        Pose (*place)(float);
    };
    const Motion motions[] = {
        {"translation", 60,
         [](float t) {
             const glm::vec3 p{-14.0f + 28.0f * t, 4.0f, 8.0f};
             return Pose{p, p + glm::vec3(0.0f, -0.15f, -1.0f)};
         }},
        {"rotation", 60,
         [](float t) {
             const float a = t * 2.0f * 3.14159265f;
             const glm::vec3 p{0.0f, 3.0f, 2.0f};
             return Pose{p, p + glm::vec3(std::sin(a), -0.1f, -std::cos(a))};
         }},
        {"dolly", 60,
         [](float t) {
             return Pose{{-5.0f, 2.0f, 20.0f - 24.0f * t}, {-5.0f, 1.0f, -4.0f}};
         }},
        {"through", 60,
         [](float t) {
             const glm::vec3 p{-5.0f, 1.0f, 6.0f - 20.0f * t};
             return Pose{p, p + glm::vec3(0.0f, 0.0f, -1.0f)};
         }},
        {"orbit", 80,
         [](float t) {
             const float a = t * 2.0f * 3.14159265f;
             const glm::vec3 c{-5.0f, 1.0f, -4.0f};
             return Pose{c + glm::vec3(16.0f * std::sin(a), 6.0f, 16.0f * std::cos(a)), c};
         }},
    };

    std::uint64_t frame = 1;
    std::size_t checks = 0;
    for (const Motion& motion : motions) {
        INFO("motion: " << motion.name);
        for (int i = 0; i < motion.frames; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(motion.frames - 1);
            const Pose pose = motion.place(t);
            aimCompositionCamera(engine.params(), pose.position, pose.target);
            step(frame, static_cast<double>(frame) / 60.0);
            ++frame;
            REQUIRE(composition->scene().camera.position == pose.position);

            for (const Authored& a : authored) {
                const scene::CompositionNode* node = composition->findNode(a.node);
                REQUIRE(node != nullptr);
                INFO("node " << a.node);
                REQUIRE(composition->nodeWorldTransform(*node).matrix() == a.nodeMatrix);
                ++checks;
            }
            // The flattened entities too: the node transform surviving while the entity it produces
            // drifts would be exactly the reported symptom, and only this half can see it.
            std::size_t compared = 0;
            for (const scene::Entity& e : engine.scene().entities) {
                for (const auto& [name, matrix] : entityTransforms) {
                    if (e.name != name) {
                        continue;
                    }
                    // Skinned characters and particles legitimately move; static geometry does not.
                    if (e.rig != scene::kInvalidRig) {
                        continue;
                    }
                    INFO("entity " << e.name);
                    REQUIRE(e.transform.matrix() == matrix);
                    ++compared;
                }
            }
            REQUIRE(compared > 0);
            checks += compared;
        }
    }

    INFO(checks << " transform comparisons over " << frame << " frames");
    CHECK(checks > 2000);
    CHECK(ctx->errorCount() == 0);
}

// ---- Phase 9.2 on the hard scene ----------------------------------------------------------------
//
// The alien replay found one history-dependent store (the animation phase origin) and there was no
// reason to believe it was the only one. Glowmere is the scene that would hold the others: terrain
// with view-distance culling, water that follows the chunks, scattered vegetation, a wind field,
// simulated plants and an imported character.
//
// Same experiment, harder scene, and the comparison is by subsystem rather than by image hash --
// because "the pixels differ" is where the last one started and "98 joint matrices, no transforms"
// is where it became a fix.
//
// It found one divergence, and the investigation ended at a contract rather than a defect: the
// `wanderer` -- an ambient `EntityWorld` character -- lands about 25 m apart depending on where the
// playhead came from. ADR-091 puts exactly that in the **live tier**: "stateful, reset on seek, and
// **explicitly not frame-accurate under scrub**". Its `seek` makes the result plausible, not
// reproducible, and that is the decision rather than a bug.
//
// So this asserts the *boundary*: everything outside the live tier must replay exactly, and the only
// entities permitted to differ are the ones `EntityWorld` drives. A baked actor, a terrain chunk, a
// water surface or a scattered plant drifting would fail here, which is the regression worth having.
TEST_CASE("Glowmere replays identically outside the live tier after a seek away and back",
          "[gpu][composition][forensics][determinism][glowmere]") {
    const fs::path sceneFile = fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-stylized.scene.json";
    if (!fs::is_regular_file(sceneFile)) {
        SKIP("the Glowmere scene is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});

    constexpr std::uint32_t kW = 192;
    constexpr std::uint32_t kH = 120;
    constexpr double kFps = 30.0;
    constexpr double kNear = 100.0 / kFps;
    constexpr double kFar = 500.0 / kFps;

    // What a frame of this scene *is*, beyond its pixels. Compared field by field so a divergence
    // names its own subsystem.
    struct Snapshot {
        std::vector<glm::mat4> transforms;
        std::vector<glm::mat4> joints;
        std::vector<std::uint8_t> visible;
        std::vector<std::uint8_t> culled;
        std::vector<std::string> names;
        std::vector<std::uint8_t> live; // driven by EntityWorld: ADR-091's live tier
        std::uint64_t image = 0;
    };

    const auto capture = [&](app::Engine& engine, rendering::SceneRenderer& renderer, double seconds) {
        engine.seekSeconds(seconds);
        FixedStepClock clock(kFps);
        clock.restartAt(seconds);
        const FrameTime time = engine.tick(clock);
        engine.setViewport(kW, kH);
        engine.update(time);
        renderer.resetTemporalHistory();
        auto image = renderer.renderToImage(engine.scene(), time, kW, kH);
        REQUIRE(image.has_value());

        Snapshot snap;
        const scene::Scene& s = engine.scene();
        // The nodes EntityWorld drives. An entity belongs to the live tier when its name is that
        // node's or is prefixed by it, which is how the flatten names an asset's parts.
        std::vector<std::string> liveNodes;
        if (const scene::Composition* comp = engine.composition()) {
            for (const auto& e : comp->entityWorld().entities()) {
                liveNodes.push_back(e->desc().driven());
            }
        }
        snap.transforms.reserve(s.entities.size());
        snap.visible.reserve(s.entities.size());
        snap.culled.reserve(s.entities.size());
        for (const scene::Entity& e : s.entities) {
            snap.transforms.push_back(e.transform.matrix());
            snap.visible.push_back(e.visible ? 1u : 0u);
            snap.culled.push_back(e.cameraCulled ? 1u : 0u);
            snap.names.push_back(e.name);
            const bool live = std::ranges::any_of(liveNodes, [&](const std::string& node) {
                return e.name == node || e.name.rfind(node + "/", 0) == 0;
            });
            snap.live.push_back(live ? 1u : 0u);
        }
        for (const scene::SkinnedRig& rig : s.rigs) {
            snap.joints.insert(snap.joints.end(), rig.palette.begin(), rig.palette.end());
        }
        snap.image = gpu::hashImage(*image);
        return snap;
    };

    const auto compare = [](const Snapshot& a, const Snapshot& b, const char* what) {
        INFO(what);
        REQUIRE(a.transforms.size() == b.transforms.size());
        REQUIRE(a.joints.size() == b.joints.size());
        REQUIRE(a.visible.size() == b.visible.size());
        std::size_t transforms = 0;
        std::size_t joints = 0;
        std::size_t visibility = 0;
        std::size_t culling = 0;
        std::size_t liveTierDiffering = 0;
        for (std::size_t i = 0; i < a.transforms.size(); ++i) {
            if (a.transforms[i] != b.transforms[i]) {
                // Permitted only for the live tier, and named when it is not.
                if (a.live[i] != 0u) {
                    ++liveTierDiffering;
                } else {
                    INFO("entity '" << a.names[i] << "' is not live and moved");
                    ++transforms;
                }
            }
            visibility += a.visible[i] == b.visible[i] ? 0 : 1;
            culling += a.culled[i] == b.culled[i] ? 0 : 1;
        }
        for (std::size_t i = 0; i < a.joints.size(); ++i) {
            joints += a.joints[i] == b.joints[i] ? 0 : 1;
        }
        INFO(transforms << " non-live transforms, " << liveTierDiffering << " live-tier, " << joints
                        << " joints, " << visibility << " visibility, " << culling
                        << " culling flags differ of " << a.transforms.size() << " entities and "
                        << a.joints.size() << " joint matrices");
        CHECK(transforms == 0);
        CHECK(joints == 0);
        CHECK(visibility == 0);
        CHECK(culling == 0);
        // The image is *not* compared: a live-tier character 25 m from where it would otherwise be
        // changes pixels, and demanding equality here would be demanding what ADR-091 declines to
        // promise. The state comparison above is the assertion; the hash is reported for the record.
        INFO("image hashes " << a.image << " vs " << b.image);
    };

    // Two engines that have been nowhere else: the references.
    Snapshot freshNear;
    Snapshot freshFar;
    {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadComposition(sceneFile).has_value());
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        freshNear = capture(engine, renderer, kNear);
    }
    {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadComposition(sceneFile).has_value());
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        freshFar = capture(engine, renderer, kFar);
    }
    CHECK(freshNear.image != freshFar.image); // the scene really does change between the two

    // One engine, walked across the gap and back.
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(sceneFile).has_value());
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    // The live tier really is present in this scene: without it the boundary assertion would be
    // vacuous and this would be an ordinary determinism test wearing a forensic hat.
    {
        app::Engine probe(app::EngineMode::Offline);
        REQUIRE(probe.loadComposition(sceneFile).has_value());
        REQUIRE(probe.composition() != nullptr);
        CHECK_FALSE(probe.composition()->entityWorld().entities().empty());
    }

    compare(capture(engine, renderer, kNear), freshNear, "arriving at 100");
    compare(capture(engine, renderer, kFar), freshFar, "seeking forward to 500");
    compare(capture(engine, renderer, kNear), freshNear, "seeking back to 100");
    compare(capture(engine, renderer, kFar), freshFar, "forward again");

    CHECK(ctx->errorCount() == 0);
}

// ---- SYM-STATIC-1 on the scene it was reported against -----------------------------------------
//
// The transform path is proven generically -- 680 renderer frames and 4,488 composition comparisons
// on RendererQA -- but the symptom was reported on Glowmere, against the `visitor`: a static
// procedural sitting 190 m from the origin that appeared to drift as the camera moved. A generic
// proof does not cover an asset-specific one, and distance is exactly where float precision in a
// transform chain would show.
//
// So: the same five camera motions over Glowmere, asserting every static thing holds still. Three
// places, because a Glowmere node lands in three different containers:
//
//   * the authored node's world transform (`nodeWorldTransform`),
//   * the flattened `Entity::transform` for mesh nodes,
//   * `ProceduralGeometry::sourceTransform` and `distributionTransform` for procedural nodes --
//     which is where the `visitor` actually lives, and which the RendererQA test never touched.
//
// The `wanderer` is excluded by name: ADR-091's live tier is allowed to move, and it is the only
// thing here that is.
//
// **Time is held still.** The symptom is that a static object moves *as the camera moves*, so the
// camera has to be the only thing that varies. Advancing the clock as well finds the `visitor`
// rotating -- it is an animated procedural -- which is the scene working, not the renderer failing,
// and an experiment that cannot tell those apart answers nothing.
TEST_CASE("Glowmere's static geometry holds still under every camera motion",
          "[gpu][composition][forensics][static][glowmere]") {
    const fs::path sceneFile = fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-stylized.scene.json";
    if (!fs::is_regular_file(sceneFile)) {
        SKIP("the Glowmere scene is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(sceneFile).has_value());
    scene::Composition* composition = engine.composition();
    REQUIRE(composition != nullptr);

    // The live tier, excluded. Everything else in this scene is static or baked.
    std::vector<std::string> liveNodes;
    for (const auto& e : composition->entityWorld().entities()) {
        liveNodes.push_back(e->desc().driven());
    }
    const auto isLive = [&](const std::string& name) {
        return std::ranges::any_of(liveNodes, [&](const std::string& node) {
            return name == node || name.rfind(node + "/", 0) == 0;
        });
    };

    constexpr std::uint32_t kW = 192;
    constexpr std::uint32_t kH = 120;
    std::uint64_t frame = 0;
    constexpr double kFixedSecond = 4.0; // one second of the piece, held for every camera pose
    const auto step = [&]() {
        FixedStepClock clock(60.0);
        clock.restartAt(kFixedSecond);
        const FrameTime time = engine.tick(clock);
        engine.setViewport(kW, kH);
        engine.update(time);
        auto image = renderer.renderToImage(engine.scene(), time, kW, kH);
        REQUIRE(image.has_value());
    };

    step();

    // The reference: every static node, entity and procedural as the first frame left them.
    std::vector<std::pair<std::string, glm::mat4>> nodeWorld;
    for (const auto& nodePtr : composition->nodes()) {
        if (!nodePtr || isLive(nodePtr->name)) {
            continue;
        }
        nodeWorld.emplace_back(nodePtr->name, composition->nodeWorldTransform(*nodePtr).matrix());
    }
    std::vector<std::pair<std::string, glm::mat4>> entityWorld;
    for (const scene::Entity& e : engine.scene().entities) {
        if (isLive(e.name)) {
            continue;
        }
        entityWorld.emplace_back(e.name, e.transform.matrix());
    }
    std::vector<std::pair<std::string, std::pair<glm::mat4, glm::mat4>>> proceduralWorld;
    for (const scene::ProceduralGeometry& p : engine.scene().procedurals) {
        if (isLive(p.name)) {
            continue;
        }
        proceduralWorld.emplace_back(
            p.name, std::pair{p.sourceTransform.matrix(), p.distributionTransform.matrix()});
    }
    REQUIRE(nodeWorld.size() >= 10);
    REQUIRE(entityWorld.size() >= 50);
    REQUIRE_FALSE(proceduralWorld.empty());
    // The one the symptom was about, and it is 190 m out. A multi-material asset arrives as one
    // procedural per material -- `visitor_m1`, `_m2`, `_m3` -- so it is a prefix, and asserting it
    // was found is what stops this test quietly covering everything except the object it is for.
    const std::size_t visitorParts =
        static_cast<std::size_t>(std::ranges::count_if(proceduralWorld, [](const auto& entry) {
            return entry.first.rfind("visitor", 0) == 0;
        }));
    INFO("visitor procedural parts: " << visitorParts);
    REQUIRE(visitorParts >= 1);

    struct Pose {
        glm::vec3 position;
        glm::vec3 target;
    };
    struct Motion {
        const char* name;
        int frames;
        Pose (*place)(float);
    };
    // Aimed at the visitor at (-45, 31.7, 185), which is the object the report was about.
    const glm::vec3 subject{-45.0f, 31.7f, 185.0f};
    const Motion motions[] = {
        {"translation", 40,
         [](float t) {
             return Pose{{-120.0f + 150.0f * t, 40.0f, 120.0f}, {-45.0f, 31.7f, 185.0f}};
         }},
        {"rotation", 40,
         [](float t) {
             const float a = t * 2.0f * 3.14159265f;
             const glm::vec3 p{-45.0f, 35.0f, 120.0f};
             return Pose{p, p + glm::vec3(std::sin(a), -0.05f, std::cos(a))};
         }},
        {"dolly", 40,
         [](float t) {
             return Pose{{-45.0f, 33.0f, 120.0f - 55.0f * t}, {-45.0f, 31.7f, 185.0f}};
         }},
        {"through", 40,
         [](float t) {
             const glm::vec3 p{-45.0f, 31.7f, 145.0f + 80.0f * t};
             return Pose{p, p + glm::vec3(0.0f, 0.0f, 1.0f)};
         }},
        {"orbit", 48,
         [](float t) {
             const float a = t * 2.0f * 3.14159265f;
             const glm::vec3 c{-45.0f, 31.7f, 185.0f};
             return Pose{c + glm::vec3(70.0f * std::sin(a), 22.0f, 70.0f * std::cos(a)), c};
         }},
    };
    static_cast<void>(subject);

    std::size_t comparisons = 0;
    for (const Motion& motion : motions) {
        INFO("motion: " << motion.name);
        for (int i = 0; i < motion.frames; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(motion.frames - 1);
            const Pose pose = motion.place(t);
            aimCompositionCamera(engine.params(), pose.position, pose.target);
            ++frame;
            step();
            // The camera really is where it was asked to be. Without this the whole test could pass
            // by never moving the camera at all, which is how it was written the first time.
            REQUIRE(composition->scene().camera.position == pose.position);

            for (const auto& [name, matrix] : nodeWorld) {
                const scene::CompositionNode* node = composition->findNode(name);
                REQUIRE(node != nullptr);
                INFO("node '" << name << "'");
                REQUIRE(composition->nodeWorldTransform(*node).matrix() == matrix);
                ++comparisons;
            }
            for (const scene::Entity& e : engine.scene().entities) {
                if (isLive(e.name)) {
                    continue;
                }
                for (const auto& [name, matrix] : entityWorld) {
                    if (e.name != name) {
                        continue;
                    }
                    INFO("entity '" << name << "'");
                    REQUIRE(e.transform.matrix() == matrix);
                    ++comparisons;
                }
            }
            for (const scene::ProceduralGeometry& p : engine.scene().procedurals) {
                if (isLive(p.name)) {
                    continue;
                }
                for (const auto& [name, matrices] : proceduralWorld) {
                    if (p.name != name) {
                        continue;
                    }
                    INFO("procedural '" << name << "'");
                    REQUIRE(p.sourceTransform.matrix() == matrices.first);
                    REQUIRE(p.distributionTransform.matrix() == matrices.second);
                    comparisons += 2;
                }
            }
        }
    }

    INFO(comparisons << " transform comparisons over " << frame << " frames");
    CHECK(comparisons > 5000);

    // ---- the other three axes, on Glowmere itself -------------------------------------------
    //
    // The camera axis is above. This is the same claim with the camera *parked* and the other three
    // variables moving in turn -- the transport, the resolution and a reload -- which is the matrix
    // RendererQA already has and which the plan asks for on the real world with the reported object
    // in it. Glowmere is the harder case on purpose: 278 entities, a live entity tier that has to be
    // excluded, and the visitor procedural the original report was about.
    //
    // The claim is not "nothing ever moves" -- this scene animates -- but the sharper one: **a
    // static node is at the same place at second four however the playhead reached it, whatever
    // size the frame is, and after the scene has been rebuilt from disk.**
    {
        const glm::vec3 parked{-45.0f, 33.0f, 120.0f};
        const glm::vec3 at{-45.0f, 31.7f, 185.0f};
        std::uint32_t width = kW;
        std::uint32_t height = kH;
        const auto renderAt = [&](double seconds, bool seek) {
            if (seek) {
                engine.seekSeconds(seconds);
            }
            FixedStepClock clock(60.0);
            clock.restartAt(seconds);
            const FrameTime time = engine.tick(clock);
            aimCompositionCamera(engine.params(), parked, at);
            engine.setViewport(width, height);
            engine.update(time);
            if (seek) {
                renderer.resetTemporalHistory();
            }
            auto image = renderer.renderToImage(engine.scene(), time, width, height);
            REQUIRE(image.has_value());
        };

        std::size_t axisComparisons = 0;
        const auto compareAll = [&](const char* what) {
            INFO(what);
            for (const auto& [name, matrix] : nodeWorld) {
                const scene::CompositionNode* node = composition->findNode(name);
                REQUIRE(node != nullptr);
                INFO("node '" << name << "'");
                REQUIRE(composition->nodeWorldTransform(*node).matrix() == matrix);
                ++axisComparisons;
            }
            for (const scene::Entity& e : engine.scene().entities) {
                if (isLive(e.name)) {
                    continue;
                }
                for (const auto& [name, matrix] : entityWorld) {
                    if (e.name == name) {
                        INFO("entity '" << name << "'");
                        REQUIRE(e.transform.matrix() == matrix);
                        ++axisComparisons;
                    }
                }
            }
        };

        // Axis one: the transport. Away and back, in both directions, past the end and to zero --
        // each excursion returning to the second the baseline was taken at.
        const double excursions[] = {9.0, 0.0, 2.5, 120.0, 4.5, 1.0};
        for (const double away : excursions) {
            renderAt(away, true);
            renderAt(kFixedSecond, true);
            compareAll("after a seek away and back");
        }

        // A scrub: many small steps, alternating direction, then back. A seek that leaked state
        // would accumulate over this where a single jump might not show it.
        for (int i = 0; i < 24; ++i) {
            const double target = kFixedSecond + ((i % 2 == 0) ? 0.05 : -0.05) * static_cast<double>(i);
            renderAt(std::max(0.0, target), true);
        }
        renderAt(kFixedSecond, true);
        compareAll("after a 24-step scrub");

        // Axis two: the resolution. Sizes chosen to be awkward rather than round.
        const std::pair<std::uint32_t, std::uint32_t> sizes[] = {
            {97, 61}, {320, 180}, {64, 64}, {193, 121}, {kW, kH}};
        for (const auto& [w, h] : sizes) {
            width = w;
            height = h;
            renderAt(kFixedSecond, false);
            compareAll("after a resize");
        }
        REQUIRE(width == kW);

        // Axis three: a reload. The composition is rebuilt from disk, so every node, entity and
        // procedural is a different object in memory that has to land in the same place.
        for (int i = 0; i < 2; ++i) {
            REQUIRE(engine.loadComposition(sceneFile).has_value());
            composition = engine.composition();
            REQUIRE(composition != nullptr);
            renderAt(kFixedSecond, true);
            compareAll("after a reload");
        }

        INFO(axisComparisons << " transform comparisons across the transport, resize and reload axes");
        CHECK(axisComparisons > 2000);
    }

    // And the other half of the answer: with the *camera* held still and time advancing, the visitor
    // does change -- it is an animated procedural. Its translation never moves; its orientation does.
    //
    // This is worth asserting rather than assuming, because it is the likely explanation for the
    // original report. A large, distant, slowly turning object with no obvious animation cue reads
    // as drift, and the first version of this very test mistook it for exactly that.
    {
        const glm::vec3 parked{-45.0f, 33.0f, 120.0f};
        aimCompositionCamera(engine.params(), parked, {-45.0f, 31.7f, 185.0f});

        const auto visitorDistribution = [&]() -> std::optional<glm::mat4> {
            for (const scene::ProceduralGeometry& p : engine.scene().procedurals) {
                if (p.name.rfind("visitor", 0) == 0) {
                    return p.distributionTransform.matrix();
                }
            }
            return std::nullopt;
        };

        FixedStepClock clock(60.0);
        clock.restartAt(2.0);
        FrameTime time = engine.tick(clock);
        engine.setViewport(kW, kH);
        engine.update(time);
        const auto before = visitorDistribution();
        REQUIRE(before.has_value());

        for (int i = 0; i < 90; ++i) {
            time = engine.tick(clock);
            engine.setViewport(kW, kH);
            engine.update(time);
        }
        const auto after = visitorDistribution();
        REQUIRE(after.has_value());

        CHECK(composition->scene().camera.position == parked); // nothing but time changed

        // It moves: the visitor is an animated procedural that turns and hovers.
        CHECK(*after != *before);
        // But it hovers rather than travels -- a couple of centimetres over a second and a half,
        // measured, not assumed. That is the shape of the thing the original report saw: a large
        // distant object, slowly turning, with a small periodic drift and no animation cue.
        const glm::vec3 moved{(*after)[3][0] - (*before)[3][0], (*after)[3][1] - (*before)[3][1],
                              (*after)[3][2] - (*before)[3][2]};
        INFO("hover over 90 frames: " << moved.x << ", " << moved.y << ", " << moved.z);
        CHECK(glm::length(moved) > 0.0f);
        CHECK(glm::length(moved) < 0.5f);
    }

    CHECK(ctx->errorCount() == 0);
}

// ---- Phase 10.1: the axes the camera matrix does not cross ---------------------------------------
//
// The static-object matrix above moves the camera and holds everything else still, which is the
// symptom as reported ("it moves when the camera moves") and only one axis of four. A transform can
// also be lost by a *time* jump, by a reload, or by a change of resolution -- and each of those is
// covered somewhere in the suite for its own sake, with nothing crossing any of them with a static
// object. A one-frame flicker in a scrub and a permanent shift after a reload look identical to a
// person and come from completely different code.
//
// Same instrument, four different variables: authored node world transform and flattened entity
// transform, compared with `==` against what the scene said at t=0.
TEST_CASE("a composition's static nodes hold their transforms across seeks, reloads and resizes",
          "[gpu][composition][forensics][static][transport]") {
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "qa" / "renderer-qa.scene.json";
    if (!fs::is_regular_file(project)) {
        SKIP("the RendererQA scene is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(project).has_value());
    scene::Composition* composition = engine.composition();
    REQUIRE(composition != nullptr);

    // The camera is parked for the whole test. The camera axis has its own matrix; mixing the two
    // would leave a failure unable to say which variable moved the object.
    const glm::vec3 eye{-5.0f, 3.0f, 12.0f};
    const glm::vec3 aim{-5.0f, 1.0f, -4.0f};

    std::uint32_t width = 192;
    std::uint32_t height = 120;
    const auto renderAt = [&](double seconds, bool seek) {
        if (seek) {
            engine.seekSeconds(seconds);
        }
        FixedStepClock clock(60.0);
        clock.restartAt(seconds);
        const FrameTime time = engine.tick(clock);
        aimCompositionCamera(engine.params(), eye, aim);
        engine.setViewport(width, height);
        engine.update(time);
        if (seek) {
            renderer.resetTemporalHistory();
        }
        auto image = renderer.renderToImage(engine.scene(), time, width, height);
        REQUIRE(image.has_value());
        REQUIRE(composition->scene().camera.position == eye);
    };

    renderAt(0.0, true);

    // What the scene says a static object is, taken once.
    const std::vector<std::string> staticNodes{"floor", "near-cube", "far-cube", "behind-camera",
                                               "transparent-orb"};
    std::vector<std::pair<std::string, glm::mat4>> authored;
    for (const std::string& name : staticNodes) {
        if (const scene::CompositionNode* node = composition->findNode(name)) {
            authored.emplace_back(name, composition->nodeWorldTransform(*node).matrix());
        }
    }
    REQUIRE(authored.size() >= 4);
    std::vector<std::pair<std::string, glm::mat4>> entities;
    for (const scene::Entity& e : engine.scene().entities) {
        if (e.rig == scene::kInvalidRig) {   // skinned characters legitimately move
            entities.emplace_back(e.name, e.transform.matrix());
        }
    }
    REQUIRE_FALSE(entities.empty());

    std::size_t checks = 0;
    const auto verify = [&](const char* where) {
        INFO(where);
        for (const auto& [name, matrix] : authored) {
            const scene::CompositionNode* node = composition->findNode(name);
            REQUIRE(node != nullptr);
            INFO("node " << name);
            REQUIRE(composition->nodeWorldTransform(*node).matrix() == matrix);
            ++checks;
        }
        std::size_t compared = 0;
        for (const scene::Entity& e : engine.scene().entities) {
            for (const auto& [name, matrix] : entities) {
                if (e.name != name || e.rig != scene::kInvalidRig) {
                    continue;
                }
                INFO("entity " << e.name);
                REQUIRE(e.transform.matrix() == matrix);
                ++compared;
            }
        }
        REQUIRE(compared > 0);
        checks += compared;
    };
    verify("the frame everything is measured against");

    SECTION("seeking about the timeline") {
        // Forwards, backwards, past the end, to zero, and to a time between frames. A seek is the
        // operation that makes a time jump reproducible (`Composition::update` integrates across a
        // gap instead), so it is the one worth crossing with a static object.
        for (const double seconds : {8.0, 2.0, 20.0, 0.0, 3.5, 19.997, 0.5}) {
            renderAt(seconds, true);
            verify("after a seek");
        }
    }

    SECTION("scrubbing back and forth") {
        // What dragging a playhead actually is: many small seeks, alternating direction, some of
        // them backwards over ground already covered.
        double at = 4.0;
        for (int i = 0; i < 40; ++i) {
            at += (i % 3 == 0) ? -0.21 : 0.13;
            renderAt(std::max(0.0, at), true);
            verify("mid-scrub");
        }
    }

    SECTION("playing without seeking") {
        // The control for the two above: the same frames reached by advancing rather than jumping.
        for (int i = 1; i <= 30; ++i) {
            renderAt(static_cast<double>(i) / 60.0, false);
            verify("while playing");
        }
    }

    SECTION("changing resolution under a still camera") {
        // Aspect ratio reaches culling and the projection, not the transform -- which is the claim
        // being tested rather than assumed. The last size repeats the first, so a transform that
        // drifted with each resize rather than tracking the size would be caught too.
        for (const auto [w, h] : {std::pair{192U, 120U}, {320U, 180U}, {96U, 96U}, {64U, 240U},
                                  {256U, 144U}, {192U, 120U}}) {
            width = w;
            height = h;
            renderAt(1.0, false);
            verify("after a resize");
        }
    }

    SECTION("reloading the scene") {
        // A reload replaces every node, entity and parameter. The authored numbers have to come back
        // *identical*, not merely close: this is the axis where a value re-derived through a
        // different path -- a default applied, a unit converted twice -- would show up.
        for (int i = 0; i < 3; ++i) {
            REQUIRE(engine.loadComposition(project).has_value());
            composition = engine.composition();
            REQUIRE(composition != nullptr);
            renderer.resetTemporalHistory();
            renderAt(0.0, true);
            verify("after a reload");
            renderAt(6.0, true);
            verify("after a reload, seeked");
        }
    }

    INFO(checks << " transform comparisons");
    CHECK(checks > 40);
    CHECK(ctx->errorCount() == 0);
}

// ---- Phase 10.2: the alien matrix ---------------------------------------------------------------
//
// `SYM-ANIM-2` ("the alien flickers or disappears near a frustum edge") was not reproduced by a
// 65-position sweep across the edge, and `SYM-ANIM-1` (a pose jumping under a scrub) was reproduced
// and fixed. What neither covered is the matrix the plan asks for: the three clips this scene
// authors, driven through the transport operations an editor actually performs.
//
// The claim under test is the one the phase-origin repair established, extended to every clip and
// every route to a second: **the pose at a given second is a property of the piece, not of how the
// playhead arrived there.** A character whose walk cycle depends on playback history is a character
// that flicks to a different pose when you scrub, which is what the report describes.
//
// The palettes are compared directly rather than through an image hash. "The pixels differ" is where
// the last investigation started; "98 joint matrices, no transforms" is where it became a fix.
TEST_CASE("the alien's pose is the same second whichever way the playhead reached it",
          "[gpu][composition][forensics][alien][animation]") {
    const fs::path sceneFile = fs::path(AVGEN_SOURCE_DIR) / "examples" / "characters" / "alien.scene.json";
    if (!fs::is_regular_file(sceneFile)) {
        SKIP("the alien composition is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    constexpr std::uint32_t kW = 192;
    constexpr std::uint32_t kH = 120;

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(sceneFile).has_value());

    // Every rig's palette, which is the whole of what "the pose" means here.
    const auto poseAt = [&](app::Engine& e, double seconds, bool seek) {
        if (seek) {
            e.seekSeconds(seconds);
        }
        FixedStepClock clock(60.0);
        clock.restartAt(seconds);
        const FrameTime time = e.tick(clock);
        e.setViewport(kW, kH);
        e.update(time);
        std::vector<glm::mat4> palette;
        for (const scene::SkinnedRig& rig : e.scene().rigs) {
            palette.insert(palette.end(), rig.palette.begin(), rig.palette.end());
        }
        return palette;
    };

    // The reference: an engine that has been nowhere, seeked straight to the second in question.
    const auto poseFresh = [&](double seconds) {
        app::Engine fresh(app::EngineMode::Offline);
        REQUIRE(fresh.loadComposition(sceneFile).has_value());
        return poseAt(fresh, seconds, true);
    };

    const auto differing = [](const std::vector<glm::mat4>& a, const std::vector<glm::mat4>& b) {
        if (a.size() != b.size()) {
            return a.size() + b.size();
        }
        std::size_t n = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            n += a[i] == b[i] ? 0 : 1;
        }
        return n;
    };

    // The three clips this scene authors are all playing at once on three nodes, so every second
    // exercises all of them; there is no state to select.
    REQUIRE_FALSE(engine.scene().rigs.empty());
    const std::vector<glm::mat4> atStart = poseAt(engine, 0.5, true);
    REQUIRE(atStart.size() > 20);   // a real skeleton, not an empty palette

    std::size_t comparisons = 0;
    const auto sameAsFresh = [&](double seconds, const char* how) {
        const std::vector<glm::mat4> mine = poseAt(engine, seconds, false);
        const std::vector<glm::mat4> reference = poseFresh(seconds);
        INFO(how << " at " << seconds << " s: " << differing(mine, reference) << " of "
                 << reference.size() << " joints differ");
        CHECK(differing(mine, reference) == 0);
        ++comparisons;
    };

    SECTION("playing to a second, and seeking to it") {
        // Play forwards through a while of the piece, then check the pose against an engine that
        // seeked straight there.
        for (int i = 1; i <= 120; ++i) {
            poseAt(engine, static_cast<double>(i) / 60.0, false);
        }
        sameAsFresh(2.0, "played to");

        engine.seekSeconds(7.5);
        poseAt(engine, 7.5, false);
        sameAsFresh(7.5, "seeked to");
    }

    SECTION("scrubbing back and forth reaches the same poses") {
        double at = 3.0;
        for (int i = 0; i < 30; ++i) {
            at += (i % 4 == 0) ? -0.37 : 0.19;
            at = std::max(0.0, at);
            engine.seekSeconds(at);
            poseAt(engine, at, false);
        }
        for (const double seconds : {1.0, 4.25, 9.0, 0.0}) {
            engine.seekSeconds(seconds);
            sameAsFresh(seconds, "after a scrub, seeked to");
        }
    }

    SECTION("pausing and resuming does not move the pose") {
        engine.seekSeconds(5.0);
        const std::vector<glm::mat4> paused = poseAt(engine, 5.0, false);
        // The same second asked for again, repeatedly: a pose that drifts while the playhead is
        // parked is a pose being integrated rather than evaluated.
        for (int i = 0; i < 8; ++i) {
            const std::vector<glm::mat4> again = poseAt(engine, 5.0, false);
            INFO("held at 5 s, repeat " << i);
            CHECK(differing(paused, again) == 0);
            ++comparisons;
        }
        sameAsFresh(5.0, "held at");
    }

    SECTION("a reload returns the same pose") {
        for (int i = 0; i < 2; ++i) {
            REQUIRE(engine.loadComposition(sceneFile).has_value());
            renderer.resetTemporalHistory();
            engine.seekSeconds(6.25);
            sameAsFresh(6.25, "after a reload");
        }
    }

    SECTION("the character is drawn, near and far, and never culled while on screen") {
        // The other half of the report: not the pose but whether it is there at all. A rig that is
        // culled while its posed limbs are on screen is `SYM-ANIM-2`.
        for (const float distance : {2.5f, 6.0f, 14.0f, 30.0f}) {
            aimCompositionCamera(engine.params(), glm::vec3(0.0f, 1.4f, distance),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
            for (int i = 0; i < 20; ++i) {
                const double at = 1.0 + static_cast<double>(i) / 30.0;
                poseAt(engine, at, false);
                const auto image = renderer.renderToImage(engine.scene(), FrameTime{}, kW, kH);
                REQUIRE(image.has_value());
                std::size_t rigged = 0;
                std::size_t culled = 0;
                for (const scene::Entity& e : engine.scene().entities) {
                    if (e.rig == scene::kInvalidRig || !e.visible) {
                        continue;
                    }
                    ++rigged;
                    culled += e.cameraCulled ? 1 : 0;
                }
                INFO("at " << distance << " m, frame " << i << ": " << rigged << " rigs, " << culled
                           << " culled");
                REQUIRE(rigged > 0);
                CHECK(culled == 0);   // all three stand at the origin and the camera is on them
                ++comparisons;
            }
        }
    }

    INFO(comparisons << " comparisons");
    CHECK(comparisons > 0);
    CHECK(ctx->errorCount() == 0);
}

// ---- Phase 5.4: who writes a character's Y ------------------------------------------------------
//
// The plan asks whether more than one system writes a character's height in the same frame, and for
// a terrain-crossing regression that proves there is no feedback loop and no one-frame
// disappearance. Both are the same question: a character's Y is either derived from the ground it
// stands on, once, or it is a value two systems argue about -- and the way that argument looks on
// screen is a walker sinking, popping or vanishing for a frame as it crosses a slope.
//
// Glowmere is the instrument, because it is the only scene with a walker, a terrain and water in it.
// Its `wanderer` is an `EntityWorld` character on a navigator built from the same `WorldMap` the
// terrain was meshed from, which is the claim in the code: "a walker can never be above or below the
// surface it is standing on, and never needs a second description of it kept in step by hand".
//
// Continuous playback only. ADR-091 declines to make a live-tier entity reproducible under a *seek*,
// which is a different question and already recorded as a contract rather than a defect.
TEST_CASE("a walker's height is the ground's, every frame, with no second writer",
          "[gpu][composition][forensics][character][terrain]") {
    const fs::path sceneFile = fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-stylized.json";
    if (!fs::is_regular_file(sceneFile)) {
        SKIP("the Glowmere project is not present");
    }
    // The *project*, not the scene: the wanderer's first behaviour is `interest` on `music.impact`,
    // so without a track it never picks a subject and never walks anywhere.
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(sceneFile).has_value());
    scene::Composition* composition = engine.composition();
    REQUIRE(composition != nullptr);

    // The walkers: entities the EntityWorld drives, which are the ones whose Y is nobody's authored
    // number. A scene with none would make everything below vacuous.
    const auto walkers = [&]() {
        std::vector<std::string> names;
        for (const auto& e : composition->entityWorld().entities()) {
            names.push_back(e->name());
        }
        return names;
    }();
    INFO(walkers.size() << " live entities");
    REQUIRE_FALSE(walkers.empty());

    struct Track {
        glm::vec3 last{0.0f};
        bool seen = false;
        float worstAboveGround = 0.0f;
        float worstBelowGround = 0.0f;
        float worstJump = 0.0f;
        float worstStepXZ = 0.0f;
        float travelled = 0.0f;      // total ground distance covered over the run
        float heightRange = 0.0f;    // how much its own height moved, high water mark to low
        float lowest = std::numeric_limits<float>::max();
        float highest = std::numeric_limits<float>::lowest();
        std::size_t frames = 0;
    };
    std::map<std::string, Track> tracks;

    // Playing, not merely advancing frames. `music.impact` is derived from the analysis at the
    // *transport* position, and a stopped transport holds it at zero however many frames go past.
    REQUIRE(engine.play().has_value());
    constexpr int kFrames = 1800;   // thirty seconds at 60
    FixedStepClock walkingClock(60.0);
    for (int i = 0; i < kFrames; ++i) {
        // Standing next to the walker, because an entity beyond its `cullDistance` from the view
        // gets no update at all.
        aimCompositionCamera(engine.params(), glm::vec3(150.0f, 30.0f, 20.0f),
                             glm::vec3(165.0f, 23.0f, 20.0f));
        // One clock, ticked. Restarting a clock at each second reports a *delta* of zero, and every
        // behaviour that integrates -- which is every behaviour that moves -- then does nothing.
        const FrameTime time = engine.tick(walkingClock);
        engine.setViewport(192, 120);
        engine.update(time);

        const world::TerrainQuery query = composition->terrainQuery();
        REQUIRE(query.map != nullptr);
        for (const auto& e : composition->entityWorld().entities()) {
            Track& t = tracks[e->name()];
            // Anchor plus travel is where the entity actually is: the anchor is where the scene put
            // it and `travel` is what navigation has written since.
            const glm::vec3 at = e->state().anchor + e->state().travel;
            const float ground = query.heightAt(glm::vec2(at.x, at.z));
            const float above = at.y - ground;
            t.worstAboveGround = std::max(t.worstAboveGround, above);
            t.worstBelowGround = std::min(t.worstBelowGround, above);
            if (t.seen) {
                // A frame's step, split into the two questions that look identical in a plot: how
                // far it travelled across the ground, and how far its height moved. A walker on a
                // slope legitimately changes height, but only in proportion to how far it walked;
                // a Y written by two systems moves without the walker going anywhere.
                const float stepXZ = glm::length(glm::vec2(at.x - t.last.x, at.z - t.last.z));
                t.worstStepXZ = std::max(t.worstStepXZ, stepXZ);
                t.travelled += stepXZ;
                const float unexplained = std::fabs(at.y - t.last.y) - stepXZ * 2.0f;
                t.worstJump = std::max(t.worstJump, unexplained);
            }
            t.lowest = std::min(t.lowest, at.y);
            t.highest = std::max(t.highest, at.y);
            t.heightRange = t.highest - t.lowest;
            t.last = at;
            t.seen = true;
            ++t.frames;
        }
    }

    // The other path to the same second: a seek. `SYM-ENTITY-1` in the register -- the two paths do
    // not agree about what thirty seconds does to an ambient walker, and this pins the size of the
    // disagreement so a repair has something to move.
    float seekedTravel = 0.0f;
    float seekedSpeed = 0.0f;
    {
        app::Engine seeked(app::EngineMode::Offline);
        REQUIRE(seeked.loadProject(sceneFile).has_value());
        seeked.seekSeconds(30.0);
        FixedStepClock clock(60.0);
        clock.restartAt(30.0);
        const FrameTime time = seeked.tick(clock);
        seeked.setViewport(192, 120);
        seeked.update(time);
        for (const auto& e : seeked.composition()->entityWorld().entities()) {
            const glm::vec3 t = e->state().travel;
            seekedTravel = std::max(seekedTravel, glm::length(glm::vec2(t.x, t.z)));
            seekedSpeed = std::max(seekedSpeed, e->state().speed);
        }
    }

    std::size_t checked = 0;
    float mostTravelled = 0.0f;
    float mostHeightChange = 0.0f;
    for (const auto& [name, t] : tracks) {
        mostTravelled = std::max(mostTravelled, t.travelled);
        mostHeightChange = std::max(mostHeightChange, t.heightRange);
        INFO("entity '" << name << "': " << t.frames << " frames, height above ground in ["
                        << t.worstBelowGround << ", " << t.worstAboveGround
                        << "] m, worst unexplained height step " << t.worstJump
                        << " m against a worst ground step of " << t.worstStepXZ << " m");
        REQUIRE(t.frames == static_cast<std::size_t>(kFrames));
        // On the ground it stands on. The band is generous on the upper side because a hovering
        // craft is a legitimate entity in this scene and its height is *meant* to be above the
        // terrain; what no entity may do is sink into it.
        CHECK(t.worstBelowGround > -0.75f);
        // No unexplained vertical movement. A slope contributes height in proportion to distance
        // travelled -- a gradient of 2 is a cliff -- so anything past that is a writer that is not
        // the ground.
        CHECK(t.worstJump < 0.5f);
        ++checked;
    }
    CHECK(checked > 0);
    // Something walked somewhere, over ground that is not flat, or every assertion above was made
    // about a set of stationary objects.
    //
    // This is the line that caught the sixth vacuous setup of this investigation. Restarting a clock
    // at each second -- the idiom the static-object matrices use, correctly, because there time is
    // *meant* to stand still -- reports a delta of zero, and every behaviour that integrates then
    // does nothing. Thirty seconds of it left the walker at travel 0, which looked exactly like an
    // engine that never walks its ambient life, and was written up as one. One ticked clock walks it
    // 149 m.
    INFO("playback: furthest walked " << mostTravelled << " m, largest height change "
                                      << mostHeightChange << " m. A seek to 30 s: " << seekedTravel
                                      << " m at " << seekedSpeed << " m/s.");
    CHECK(mostTravelled > 20.0f);
    CHECK(mostHeightChange > 1.0f);
    // ...and the seek path simulates too, so both routes to a second move the world.
    CHECK(seekedTravel > 5.0f);
    CHECK(seekedSpeed > 1.0f);
}

// ---- Phase 4.2: every isolation control isolates a real path -------------------------------------
//
// The plan's rule for the forensics mode is that the controls must be truthful: "every enabled
// control must isolate or visualize a real path". A checkbox that does nothing is worse than a
// missing one, because it is evidence -- somebody turns it off, the symptom stays, and a subsystem
// is wrongly cleared.
//
// So each toggle is asserted to change the frame, and where the change is countable it is counted
// rather than hashed. The camera freeze is the one that matters most and is checked hardest: two
// genuinely different camera positions must produce *identical* frames while it is on, which no
// amount of accidental correctness produces.
TEST_CASE("each renderer isolation toggle removes the thing it names",
          "[gpu][composition][forensics][isolation]") {
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "qa" / "renderer-qa.scene.json";
    if (!fs::is_regular_file(project)) {
        SKIP("the RendererQA scene is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    constexpr std::uint32_t kW = 160;
    constexpr std::uint32_t kH = 120;

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(project).has_value());
    FixedStepClock clock(60.0);

    struct Frame {
        std::uint64_t hash = 0;
        std::uint32_t draws = 0;
        std::uint32_t particleSystems = 0;
    };
    // One clock, ticked. Restarting it per call reports a frame delta of zero, and everything that
    // integrates -- the particle systems here -- then never runs: the frame with particles off came
    // out byte-identical to the one with them on, because neither had any. Same trap as the
    // character/terrain matrix, one file away.
    const auto renderWith = [&](const rendering::SceneRenderer::PassToggles& toggles,
                                int warmFrames = 0) {
        renderer.setPassToggles(toggles);
        for (int i = 0; i < warmFrames; ++i) {
            const FrameTime warm = engine.tick(clock);
            engine.setViewport(kW, kH);
            engine.update(warm);
        }
        const FrameTime time = engine.tick(clock);
        engine.setViewport(kW, kH);
        engine.update(time);
        renderer.resetTemporalHistory();
        const auto image = renderer.renderToImage(engine.scene(), time, kW, kH);
        REQUIRE(image.has_value());
        return Frame{gpu::hashImage(*image), renderer.stats().drawCalls,
                     renderer.stats().particles.systems};
    };

    const rendering::SceneRenderer::PassToggles all;
    const Frame baseline = renderWith(all, 60);
    INFO("baseline: " << baseline.draws << " draws, " << baseline.particleSystems
                      << " particle systems");
    REQUIRE(baseline.draws > 0);

    SECTION("transparency") {
        // Looking at the transparent orb specifically. The scene's default view does not contain
        // it, and a toggle tested against a frame the thing is not in passes for nothing.
        aimCompositionCamera(engine.params(), glm::vec3(3.0f, 1.6f, 1.0f),
                             glm::vec3(3.0f, 1.2f, -5.0f));
        const Frame withOrb = renderWith(all);
        auto off = all;
        off.transparency = false;
        const Frame f = renderWith(off);
        INFO(f.draws << " draws with transparency off against " << withOrb.draws << " with it");
        CHECK(f.hash != withOrb.hash);
        CHECK(f.draws < withOrb.draws);   // the transparent orb is gone from the frame
    }

    SECTION("particles") {
        // Looking at the emitter, and late enough for it to have thrown something: at one second a
        // system spawning 80 a second at 6 cm across contributes almost nothing to a wide shot, and
        // a toggle tested against that passes for nothing.
        aimCompositionCamera(engine.params(), glm::vec3(-3.0f, 1.2f, -3.0f),
                             glm::vec3(-3.0f, 1.0f, -6.0f));
        const Frame emitting = renderWith(all, 180);   // long enough for the system to have filled
        auto off = all;
        off.particles = false;
        const Frame f = renderWith(off);
        CHECK(emitting.particleSystems > 0);   // ...or the toggle had nothing to remove
        CHECK(f.particleSystems == 0);
        CHECK(f.hash != emitting.hash);
    }

    SECTION("animation") {
        // The alien draws from its rest vertices, which is a different silhouette from any pose the
        // clip reaches. Compared at a second where the clip has actually moved: at t=0 a pose and a
        // bind pose can legitimately agree.
        const Frame posed = renderWith(all, 150);
        auto off = all;
        off.animation = false;
        const Frame bind = renderWith(off);
        CHECK(bind.hash != posed.hash);
    }

    SECTION("culling") {
        // Aim away from everything, so the cull has work to do, and then take it away.
        aimCompositionCamera(engine.params(), glm::vec3(0.0f, 2.0f, 12.0f),
                             glm::vec3(0.0f, 2.0f, 60.0f));
        const Frame culled = renderWith(all);
        auto off = all;
        off.culling = false;
        const Frame uncut = renderWith(off);
        INFO(culled.draws << " draws with culling, " << uncut.draws << " without");
        CHECK(uncut.draws > culled.draws);   // the objects behind the camera are submitted again
    }

    SECTION("the view freeze holds the projection while the camera moves") {
        // What this control freezes is the **view and projection the scene pass draws with**, and
        // the claim is scoped to that on purpose. The sky, the volumetrics and the particle systems
        // read the live camera themselves, so the *frame* is not identical across a camera move --
        // only the geometry's projection is. A control described as "freeze the camera" and tested
        // by image equality would fail for reasons that have nothing to do with what it does, and
        // one described that way and *not* tested would be the untruthful checkbox the plan warns
        // about. Freezing those other subsystems is a separate control and is not built.
        aimCompositionCamera(engine.params(), glm::vec3(0.0f, 2.0f, 10.0f),
                             glm::vec3(0.0f, 1.0f, 0.0f));
        renderWith(all);
        const glm::mat4 liveView = renderer.diagnosticFrame().view;

        auto frozen = all;
        frozen.cameraMotion = false;
        renderWith(frozen);   // the frame that arms it records the view it will hold
        CHECK(renderer.diagnosticFrame().view == liveView);

        // Somewhere else entirely, twenty-five metres away and looking elsewhere.
        renderer.setDiagnosticEntity("near-cube");
        aimCompositionCamera(engine.params(), glm::vec3(-9.0f, 6.0f, -14.0f),
                             glm::vec3(4.0f, 0.0f, 3.0f));
        renderWith(frozen);
        INFO("scene camera now at " << engine.scene().camera.position.x << ", "
                                    << engine.scene().camera.position.z);
        // The scene's camera really did move...
        CHECK(engine.scene().camera.position != glm::vec3(0.0f, 2.0f, 10.0f));
        // ...and the matrices the frame drew with did not.
        CHECK(renderer.diagnosticFrame().view == liveView);
        CHECK(renderer.diagnosticFrame().viewProjection ==
              renderer.diagnosticFrame().projection * liveView);

        // Released, they follow the camera again.
        renderWith(all);
        CHECK(renderer.diagnosticFrame().view != liveView);
    }

    CHECK(ctx->errorCount() == 0);
}

// ---- Phase 8.2: the progressive matrix ----------------------------------------------------------
//
// Subsystems switched on one at a time, and at every level the same script of camera moves and
// transport operations. The plan's question is "record the first level where each instability
// appears", so the value is entirely in the *order*: a failure at level 6 with level 5 clean says
// which subsystem introduced it, which is the whole reason for building the ladder rather than
// running the full renderer and looking at it.
//
// What each level asks of a frame is the same three things, because they are the three that do not
// need a human to look: it renders at all, the GPU reports no errors, and the identical inputs
// render identically. The last one is the one with teeth -- most instabilities in this investigation
// have shown up first as a frame that would not reproduce.
//
// The ladder covers the switchable dimensions and says which of the plan's sixteen levels are not
// independently reachable rather than pretending. Terrain, lighting, LOD and the sequencer have no
// isolation control (see Phase 4.2), so they are present at every level here.
TEST_CASE("the progressive matrix finds the first level that misbehaves",
          "[gpu][composition][forensics][matrix]") {
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "qa" / "renderer-qa.scene.json";
    if (!fs::is_regular_file(project)) {
        SKIP("the RendererQA scene is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    using Toggles = rendering::SceneRenderer::PassToggles;
    const auto bare = [] {
        Toggles t;
        t.shadows = false;
        t.ao = false;
        t.volume = false;
        t.post = false;
        t.shadowMask = false;
        t.water = false;
        t.transparency = false;
        t.particles = false;
        t.animation = false;
        return t;
    };

    // Cumulative, in the plan's order as far as the controls allow.
    struct Level {
        const char* name;
        Toggles toggles;
        // Some passes need an input before they do anything. RendererQA authors
        // `volumeDensity: 0`, which keeps the volumetric march off however the toggle is set -- so
        // that rung supplies the density rather than the shipped scene carrying it. The QA scene is
        // the *control* for the performance baselines, and changing what it draws to make a test
        // meaningful would spend a fixed point to buy a variable one.
        float volumeDensity = 0.0f;
        // Which scene the rung runs against. Water needs a shoreline, and the control scene has
        // none: `renderer-qa-water.scene.json` is the variant that does (Phase 8.1).
        const char* scene = nullptr;
    };
    std::vector<Level> levels;
    Toggles t = bare();
    levels.push_back({"0 opaque geometry", t});
    t.shadows = true;
    levels.push_back({"4 shadows", t});
    t.shadowMask = true;
    levels.push_back({"4b shadow mask", t});
    // Level 5, water, runs against the variant that has a shoreline in it. The control scene has
    // none -- which is what this matrix found -- and a rung whose subsystem the scene does not
    // contain draws the frame below it and localises nothing.
    t.water = true;
    levels.push_back({"5 water", t, 0.0f, "renderer-qa-water.scene.json"});
    t.transparency = true;
    levels.push_back({"7 transparency", t});
    t.animation = true;
    levels.push_back({"9 animation", t});
    t.particles = true;
    levels.push_back({"10 particles", t});
    t.ao = true;
    levels.push_back({"11a ambient occlusion", t});
    t.volume = true;
    levels.push_back({"11b volumetrics", t, 0.06f});
    t.post = true;
    levels.push_back({"11c post", t});

    // The script, run at every level. Levels 1 and 15 of the plan's list are not levels here but
    // this: the instability being looked for is usually a camera move or a time jump.
    struct Step {
        const char* what;
        glm::vec3 eye;
        glm::vec3 aim;
        double seek;        // < 0 to advance rather than jump
        std::uint32_t width;
        std::uint32_t height;
    };
    static constexpr Step kScript[] = {
        {"opening", {0.0f, 2.0f, 10.0f}, {0.0f, 1.0f, 0.0f}, 0.0, 160, 120},
        {"translate", {6.0f, 2.0f, 10.0f}, {0.0f, 1.0f, 0.0f}, -1.0, 160, 120},
        {"rotate", {6.0f, 2.0f, 10.0f}, {6.0f, 1.6f, -2.0f}, -1.0, 160, 120},
        {"dolly", {1.5f, 1.6f, 2.5f}, {0.0f, 1.0f, -3.0f}, -1.0, 160, 120},
        {"orbit", {-8.0f, 4.0f, -8.0f}, {0.0f, 1.0f, -4.0f}, -1.0, 160, 120},
        {"seek forward", {0.0f, 2.0f, 10.0f}, {0.0f, 1.0f, 0.0f}, 9.0, 160, 120},
        {"seek back", {0.0f, 2.0f, 10.0f}, {0.0f, 1.0f, 0.0f}, 2.0, 160, 120},
        {"scrub", {0.0f, 2.0f, 10.0f}, {0.0f, 1.0f, 0.0f}, 2.05, 160, 120},
        {"resize", {0.0f, 2.0f, 10.0f}, {0.0f, 1.0f, 0.0f}, -1.0, 96, 96},
        {"resize back", {0.0f, 2.0f, 10.0f}, {0.0f, 1.0f, 0.0f}, -1.0, 160, 120},
        {"reload", {0.0f, 2.0f, 10.0f}, {0.0f, 1.0f, 0.0f}, 0.0, 160, 120},
    };

    // One whole run of the script, from nothing: its own engine, its own renderer. Two of these
    // compared step by step is the determinism claim that survives a *time-stepping* subsystem.
    //
    // Re-rendering one state twice was tried first, twice, and is wrong for anything that moves. The
    // particle simulation is stepped *inside* the render call, so rendering the same frame again
    // steps the world again -- the matrix reported "first misbehaving level: particles" about the
    // renderer doing exactly what it is built to do. That is worth knowing on its own: a frame is
    // not a pure function of the scene state while particles are in it.
    const auto runScript = [&](const Level& level, std::vector<std::uint64_t>& hashes) {
        const fs::path file = level.scene != nullptr
                                  ? fs::path(AVGEN_SOURCE_DIR) / "examples" / "qa" / level.scene
                                  : project;
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadComposition(file).has_value());
        rendering::SceneRenderer own(*ctx, shaders);
        REQUIRE(own.init().has_value());
        own.setPassToggles(level.toggles);
        if (level.volumeDensity > 0.0f) {
            auto* density = engine.params().findAs<float>("scene/volumeDensity");
            REQUIRE(density != nullptr);
            density->setBase(level.volumeDensity);
            engine.params().resetFinals();
        }
        FixedStepClock clock(60.0);
        for (const Step& step : kScript) {
            if (std::string_view(step.what) == "reload") {
                REQUIRE(engine.loadComposition(file).has_value());
                own.resetTemporalHistory();
            }
            if (step.seek >= 0.0) {
                engine.seekSeconds(step.seek);
                clock.restartAt(step.seek);
            }
            aimCompositionCamera(engine.params(), step.eye, step.aim);
            const FrameTime time = engine.tick(clock);
            engine.setViewport(step.width, step.height);
            engine.update(time);
            const auto image = own.renderToImage(engine.scene(), time, step.width, step.height);
            REQUIRE(image.has_value());
            hashes.push_back(gpu::hashImage(*image));
        }
    };

    std::string firstBad;
    std::size_t steps = 0;
    std::vector<std::uint64_t> previousLevel;
    std::string previousName;
    const char* previousScene = nullptr;
    for (const Level& level : levels) {
        INFO("level: " << level.name);
        const std::uint32_t errorsBefore = ctx->errorCount();
        std::vector<std::uint64_t> first;
        std::vector<std::uint64_t> second;
        runScript(level, first);
        runScript(level, second);
        REQUIRE(first.size() == std::size(kScript));
        REQUIRE(second.size() == first.size());
        for (std::size_t i = 0; i < first.size(); ++i) {
            INFO("step: " << kScript[i].what);
            const bool stable = first[i] == second[i];
            if (!stable && firstBad.empty()) {
                firstBad = std::string(level.name) + " / " + kScript[i].what;
            }
            CHECK(stable);
            ++steps;
        }
        if (ctx->errorCount() != errorsBefore && firstBad.empty()) {
            firstBad = std::string(level.name) + " / GPU errors";
        }
        CHECK(ctx->errorCount() == errorsBefore);

        // Each rung has to change the picture, or the ladder is not a ladder: a level whose
        // subsystem is invisible in this scene cannot localise anything, and "the symptom appeared
        // at level 6" would be meaningless if level 6 drew the same frame as level 5. This is the
        // control on the whole matrix as well as an assertion about the scene.
        if (!previousLevel.empty() && level.scene == nullptr && previousScene == nullptr) {
            bool changedSomething = false;
            for (std::size_t i = 0; i < first.size() && i < previousLevel.size(); ++i) {
                changedSomething = changedSomething || first[i] != previousLevel[i];
            }
            INFO("'" << level.name << "' against '" << previousName << "'");
            CHECK(changedSomething);
        }
        // A rung on another scene is not comparable with the one below it; what makes *that* rung
        // meaningful is the variant test, which asserts the scene contains water at all.
        if (level.scene == nullptr) {
            previousLevel = first;
            previousName = level.name;
        }
        previousScene = level.scene;
    }

    INFO(steps << " rendered steps across " << levels.size()
               << " levels; first misbehaving level: " << (firstBad.empty() ? "none" : firstBad));
    CHECK(firstBad.empty());
    CHECK(steps >= levels.size() * 10);
    CHECK(ctx->errorCount() == 0);
}

// ---- Phase 8.1: the QA variants contain what they are named for ----------------------------------
//
// The progressive matrix found that RendererQA has no water in it and authors zero volumetric
// density, so two of its rungs drew the frame below them and could localise nothing. The answer is
// variants rather than a fatter QA scene: `renderer-qa.scene.json` is the *control* the performance
// baselines are measured against, and changing what it draws to make a test meaningful spends a
// fixed point to buy a variable one.
//
// A variant that loads and contains nothing is the same trap one level down, so each is asserted to
// hold the thing it is named for -- and to hold *only* that, where the point is isolation.
TEST_CASE("the RendererQA variants each contain the subsystem they isolate",
          "[gpu][composition][forensics][qa]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    struct Counts {
        std::size_t entities = 0;
        std::size_t rigged = 0;
        std::size_t blended = 0;
        std::size_t water = 0;
        std::size_t particles = 0;
        std::size_t procedurals = 0;
        [[nodiscard]] std::size_t objects() const { return entities + procedurals; }
    };
    const auto inspect = [&](const char* file, int frames) {
        const fs::path path = fs::path(AVGEN_SOURCE_DIR) / "examples" / "qa" / file;
        REQUIRE(fs::is_regular_file(path));
        app::Engine engine(app::EngineMode::Offline);
        INFO(file);
        REQUIRE(engine.loadComposition(path).has_value());
        FixedStepClock clock(60.0);
        for (int i = 0; i < frames; ++i) {
            const FrameTime time = engine.tick(clock);
            engine.setViewport(160, 120);
            engine.update(time);
        }
        const FrameTime time = engine.tick(clock);
        engine.setViewport(160, 120);
        engine.update(time);
        const auto image = renderer.renderToImage(engine.scene(), time, 160, 120);
        REQUIRE(image.has_value());
        Counts c;
        for (const scene::Entity& e : engine.scene().entities) {
            ++c.entities;
            c.rigged += e.rig != scene::kInvalidRig ? 1 : 0;
            c.blended += e.material.alphaMode == scene::AlphaMode::Blend ? 1 : 0;
            c.water += e.style == scene::MeshStyle::Water ? 1 : 0;
        }
        c.particles = engine.scene().particles.size();
        // Procedural nodes become `Scene::procedurals` rather than entities, so a count of entities
        // alone reports a scene of boxes as empty.
        c.procedurals = engine.scene().procedurals.size();
        // Something has to be on screen, or a variant is a black frame that agrees with everything.
        std::size_t lit = 0;
        for (std::uint32_t y = 0; y < 120; ++y) {
            for (std::uint32_t x = 0; x < 160; ++x) {
                const std::uint8_t* p = image->pixel(x, y);
                lit += (p[0] > 12 || p[1] > 12 || p[2] > 12) ? 1 : 0;
            }
        }
        INFO(c.entities << " entities, " << c.procedurals << " procedurals, " << c.rigged
                        << " rigged, " << c.blended << " blended, " << c.water << " water, "
                        << c.particles << " particle systems, " << lit << " lit pixels");
        CHECK(lit > 200);
        return c;
    };

    SECTION("minimal is opaque geometry and nothing else") {
        const Counts c = inspect("renderer-qa-minimal.scene.json", 2);
        CHECK(c.entities >= 3);   // plain mesh entities, not procedurals: see the scene's note
        CHECK(c.rigged == 0);
        CHECK(c.blended == 0);
        CHECK(c.water == 0);
        CHECK(c.particles == 0);
    }

    SECTION("water has a generated shoreline in it") {
        const Counts c = inspect("renderer-qa-water.scene.json", 2);
        CHECK(c.water > 0);        // real chunk water, not two planes
        CHECK(c.entities > c.water);   // ...and the ground it cuts through
        CHECK(c.rigged == 0);
        CHECK(c.blended == 0);
    }

    SECTION("character is one skinned character") {
        const Counts c = inspect("renderer-qa-character.scene.json", 30);
        CHECK(c.rigged == 1);
        CHECK(c.water == 0);
        CHECK(c.particles == 0);
    }

    SECTION("transparency is two blended layers over an opaque backstop") {
        const Counts c = inspect("renderer-qa-transparency.scene.json", 2);
        CHECK(c.blended == 2);
        CHECK(c.objects() >= 3);
        CHECK(c.rigged == 0);
    }

    CHECK(ctx->errorCount() == 0);
}

// ---- Phase 9.1: capture, replay and a difference in words ----------------------------------------
//
// The evidence standard this investigation set for itself says a captured frame is *not* an image
// hash: "an image hash says something differs; it never says what, and the Phase 9.2 defect was
// localised in one comparison by checking palettes and transforms separately". This is that
// comparison made portable -- a frame's state written to a file, and two of them diffed into
// sentences naming the object and the field.
//
// Tested the only way a diff can honestly be tested: by making each kind of difference on purpose
// and checking it is the one reported. A differ that returns "something changed" for everything
// would pass a test that only ever moved one thing.
TEST_CASE("a captured frame replays, and two of them differ in words",
          "[gpu][composition][forensics][snapshot]") {
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "qa" / "renderer-qa.scene.json";
    if (!fs::is_regular_file(project)) {
        SKIP("the RendererQA scene is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(project).has_value());
    FixedStepClock clock(60.0);
    const auto capture = [&](const char* note) {
        const FrameTime time = engine.tick(clock);
        engine.setViewport(160, 120);
        engine.update(time);
        const auto image = renderer.renderToImage(engine.scene(), time, 160, 120);
        REQUIRE(image.has_value());
        rendering::FrameSnapshot snap;
        snap.frame = renderer.diagnosticFrame();
        snap.toggles = renderer.passToggles();
        snap.scene = "renderer-qa";
        snap.note = note;
        return snap;
    };

    aimCompositionCamera(engine.params(), glm::vec3(0.0f, 3.0f, 12.0f), glm::vec3(0.0f, 1.0f, -4.0f));
    const rendering::FrameSnapshot first = capture("the reference frame");
    // Three: the grid floor, the alien and the orb. The procedural boxes are *not* here -- the
    // diagnostic frame is built from `Scene::entities`, and a procedural node becomes a
    // `Scene::procedurals` entry instead. Worth knowing before trusting a per-object diagnostic on a
    // scene like Glowmere, where most of the geometry is procedural.
    REQUIRE(first.frame.objects.size() >= 3);

    SECTION("a capture survives a round trip through a file") {
        const fs::path file = fs::temp_directory_path() /
                              ("avgen_snapshot_" + std::to_string(::getpid()) + ".json");
        REQUIRE(rendering::writeSnapshot(first, file).has_value());
        const auto reloaded = rendering::readSnapshot(file);
        INFO((reloaded ? std::string() : reloaded.error().message));
        REQUIRE(reloaded.has_value());
        // Reading back a capture and comparing it against the frame it came from must report
        // nothing. Anything it reports here is the serialiser losing state, which would make every
        // later comparison a comparison with the file format.
        const auto same = rendering::compareSnapshots(first, *reloaded);
        for (const std::string& line : same) {
            INFO(line);
        }
        CHECK(same.empty());
        CHECK(reloaded->frame.objects.size() == first.frame.objects.size());
        CHECK(reloaded->scene == "renderer-qa");
        std::error_code ec;
        fs::remove(file, ec);
    }

    SECTION("the same state captured twice reports no difference") {
        const rendering::FrameSnapshot again = capture("the same frame again");
        const auto diff = rendering::compareSnapshots(first, again);
        for (const std::string& line : diff) {
            INFO(line);
        }
        CHECK(diff.empty());

        // ...and the monotonic bookkeeping is there when it is asked for, labelled as what it is.
        // The rig palette version increments on every upload, so it differs between two arrivals at
        // the same second of the same piece. Phase 9.2 tried to use the state hash as a replay
        // identity for exactly this reason and could not.
        const auto withCounters = rendering::compareSnapshots(first, again, 1e-5f, true);
        CHECK(withCounters.size() > diff.size());
        CHECK(std::any_of(withCounters.begin(), withCounters.end(), [](const std::string& l) {
            return l.find("a counter, not a pose") != std::string::npos;
        }));
    }

    SECTION("each kind of difference is reported as itself") {
        const auto reports = [](const std::vector<std::string>& lines, std::string_view needle) {
            return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) {
                return l.find(needle) != std::string::npos;
            });
        };

        // A moved object.
        {
            rendering::FrameSnapshot moved = first;
            REQUIRE_FALSE(moved.frame.objects.empty());
            moved.frame.objects.front().worldPosition.y += 0.5f;
            moved.frame.objects.front().worldMatrix[3][1] += 0.5f;
            const auto diff = rendering::compareSnapshots(first, moved);
            REQUIRE_FALSE(diff.empty());
            INFO(diff.front());
            CHECK(reports(diff, "moved"));
            CHECK(reports(diff, first.frame.objects.front().name));
        }
        // An object that stopped being submitted, which is what a culling change looks like.
        {
            rendering::FrameSnapshot culled = first;
            culled.frame.objects.front().cameraCulled = !culled.frame.objects.front().cameraCulled;
            culled.frame.objects.front().submitted = !culled.frame.objects.front().submitted;
            const auto diff = rendering::compareSnapshots(first, culled);
            CHECK(reports(diff, "culling"));
            CHECK(reports(diff, "submission"));
        }
        // A swapped mesh or material: a wrong picture with every matrix identical, which is what
        // Phase 2.1 asks a snapshot to carry beyond the geometry's placement.
        {
            rendering::FrameSnapshot other = first;
            other.frame.objects.front().mesh += 1;
            CHECK(reports(rendering::compareSnapshots(first, other), "instead of"));
        }
        {
            rendering::FrameSnapshot other = first;
            other.frame.objects.front().materialHash ^= 0x9E3779B97F4A7C15ull;
            CHECK(reports(rendering::compareSnapshots(first, other), "different material"));
        }
        // A swapped GPU slot: the mechanism Phase 3.3 is about.
        {
            rendering::FrameSnapshot swapped = first;
            swapped.frame.objects.front().objectSlot += 1;
            CHECK(reports(rendering::compareSnapshots(first, swapped), "GPU slot"));
        }
        // An object that is gone, and one that is new.
        {
            rendering::FrameSnapshot fewer = first;
            const std::string dropped = fewer.frame.objects.back().name;
            fewer.frame.objects.pop_back();
            CHECK(reports(rendering::compareSnapshots(first, fewer), "is gone"));
            CHECK(reports(rendering::compareSnapshots(fewer, first), "is new"));
            CHECK(reports(rendering::compareSnapshots(first, fewer), dropped));
        }
        // An arm that was set differently, which invalidates the rest of the comparison.
        {
            rendering::FrameSnapshot other = first;
            other.toggles.shadows = false;
            CHECK(reports(rendering::compareSnapshots(first, other), "isolation differs"));
        }
        // A projection that changed, named by the input that changed it. "The projection differs"
        // is true of a resize and of a lens move alike, and they are completely different questions.
        {
            rendering::FrameSnapshot resized = first;
            resized.frame.viewportWidth *= 2;
            resized.frame.aspect *= 2.0f;
            resized.frame.projection[0][0] *= 0.5f;
            const auto diff = rendering::compareSnapshots(first, resized);
            CHECK(reports(diff, "the projection differs"));
            CHECK(reports(diff, "aspect"));
            CHECK(reports(diff, "viewport"));
        }
        {
            rendering::FrameSnapshot zoomed = first;
            zoomed.frame.fovY *= 0.5f;
            zoomed.frame.projection[1][1] *= 2.0f;
            const auto diff = rendering::compareSnapshots(first, zoomed);
            CHECK(reports(diff, "field of view"));
            CHECK_FALSE(reports(diff, "viewport"));
        }
        // The camera, which moves everything and is therefore worth saying once rather than
        // per object.
        {
            rendering::FrameSnapshot elsewhere = first;
            elsewhere.frame.cameraPosition.x += 3.0f;
            elsewhere.frame.view[3][0] -= 3.0f;
            const auto diff = rendering::compareSnapshots(first, elsewhere);
            CHECK(reports(diff, "camera moved"));
            CHECK(reports(diff, "view matrix"));
        }
    }

    CHECK(ctx->errorCount() == 0);
}

// ---- Phase 2.2: the reference renderer, and comparing the two ------------------------------------
//
// A second renderer earns its keep only if the comparison against it is one the production path
// could fail. Colour is not that comparison: the reference path shades flat and will never look like
// the picture. **Coverage** is -- which pixels contain geometry -- because that is decided entirely
// by the transform chain and the camera, and by nothing the reference path lacks.
//
// The production side runs with its isolation arms off (Phase 4.2), so the two are asked the same
// question: no shadows, no AO, no volumetrics, no post, no particles, no transparency, no animation.
// What remains on both sides is opaque geometry projected through `Camera::view()` and
// `Camera::projection()`.
TEST_CASE("the reference renderer and the production renderer cover the same pixels",
          "[gpu][composition][forensics][reference]") {
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "qa" / "renderer-qa-minimal.scene.json";
    if (!fs::is_regular_file(project)) {
        SKIP("the minimal QA variant is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer production(*ctx, shaders);
    REQUIRE(production.init().has_value());
    rendering::ReferenceRenderer reference(*ctx, shaders);
    REQUIRE(reference.init().has_value());

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(project).has_value());
    constexpr std::uint32_t kW = 192;
    constexpr std::uint32_t kH = 144;

    rendering::SceneRenderer::PassToggles bare;
    bare.shadows = false;
    bare.ao = false;
    bare.volume = false;
    bare.post = false;
    bare.shadowMask = false;
    bare.particles = false;
    bare.transparency = false;
    bare.animation = false;
    production.setPassToggles(bare);

    FixedStepClock clock(60.0);
    // Coverage against each image's *own* background, not an absolute threshold.
    //
    // An absolute one does not survive the comparison: production tone-maps and auto-exposes, so on
    // a dark scene it lifts the empty background well clear of any fixed cut-off and the mask comes
    // out as "the whole frame". The corner pixel is background in every view here, and a pixel
    // counts as geometry when it is meaningfully brighter than it.
    const auto coverage = [](const gpu::Image8& image) {
        const std::uint8_t* corner = image.pixel(0, 0);
        const glm::ivec3 background(corner[0], corner[1], corner[2]);
        std::vector<bool> mask(static_cast<std::size_t>(kW) * kH, false);
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                const std::uint8_t* p = image.pixel(x, y);
                const int delta = std::max({std::abs(p[0] - background.r), std::abs(p[1] - background.g),
                                            std::abs(p[2] - background.b)});
                mask[static_cast<std::size_t>(y) * kW + x] = delta > 14;
            }
        }
        return mask;
    };

    struct View {
        const char* name;
        glm::vec3 eye;
        glm::vec3 aim;
    };
    const View views[] = {
        {"opening", {0.0f, 2.5f, 9.0f}, {0.0f, 1.0f, 0.0f}},
        {"from the side", {9.0f, 3.0f, 2.0f}, {0.0f, 1.0f, -2.0f}},
        {"close", {-1.0f, 1.4f, 2.4f}, {-1.5f, 1.0f, 0.0f}},
        {"looking at the static cube", {10.0f, 4.0f, -8.0f}, {10.0f, 2.0f, -20.0f}},
        {"high and back", {-6.0f, 12.0f, 14.0f}, {0.0f, 0.0f, -6.0f}},
    };

    std::size_t comparedViews = 0;
    for (const View& view : views) {
        INFO("view: " << view.name);
        aimCompositionCamera(engine.params(), view.eye, view.aim);
        const FrameTime time = engine.tick(clock);
        engine.setViewport(kW, kH);
        engine.update(time);
        production.resetTemporalHistory();

        const auto shipped = production.renderToImage(engine.scene(), time, kW, kH);
        REQUIRE(shipped.has_value());
        const auto minimal = reference.renderToImage(engine.scene(), kW, kH);
        REQUIRE(minimal.has_value());

        // The reference path has to have drawn the scene, and to say what it declined.
        const auto& counts = reference.counts();
        INFO(counts.drawn << " drawn, " << counts.skippedSkinned << " skinned, "
                          << counts.skippedBlended << " blended, " << counts.skippedWater
                          << " water, " << counts.skippedNoMesh << " without a mesh");
        REQUIRE(counts.drawn > 0);

        const std::vector<bool> a = coverage(*shipped);
        const std::vector<bool> b = coverage(*minimal);
        std::size_t onlyProduction = 0;
        std::size_t onlyReference = 0;
        std::size_t both = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            both += a[i] && b[i] ? 1 : 0;
            onlyProduction += a[i] && !b[i] ? 1 : 0;
            onlyReference += !a[i] && b[i] ? 1 : 0;
        }
        const auto disagreement = static_cast<double>(onlyProduction + onlyReference) /
                                  static_cast<double>(std::max<std::size_t>(both, 1));
        INFO(both << " pixels covered by both, " << onlyProduction << " only by production, "
                  << onlyReference << " only by the reference, disagreement "
                  << disagreement * 100.0 << "%");
        // The geometry is on screen in both.
        REQUIRE(both > 500);
        // ...and the silhouettes agree. Not exactly: the two paths shade differently, so a pixel on
        // the very edge of a triangle can fall either side of the threshold, and production applies
        // a fog the reference path does not. A transform or camera error is not a percent -- it puts
        // the object somewhere else entirely.
        CHECK(disagreement < 0.08);
        ++comparedViews;
    }
    CHECK(comparedViews == std::size(views));

    // Per *object*, not per frame. Whole-frame coverage agreeing is a weaker claim than it looks:
    // two objects could swap places, or one could be drawn twice and another not at all, and the
    // union of the silhouettes would be unchanged. So each entity's own footprint is measured the
    // only way that does not require the two renderers to agree about shading -- hide it and
    // difference the two frames -- and the footprints are compared to each other.
    //
    // This is what makes the comparison a statement about *transforms*: if production placed an
    // object somewhere the scene does not say, its footprint moves and the reference's does not.
    {
        aimCompositionCamera(engine.params(), glm::vec3(0.0f, 2.5f, 9.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const FrameTime time = engine.tick(clock);
        engine.setViewport(kW, kH);
        engine.update(time);

        struct Footprint {
            std::size_t area = 0;
            glm::vec2 centroid{0.0f};
        };
        const auto footprintOf = [&](const gpu::Image8& with, const gpu::Image8& without) {
            Footprint f;
            glm::vec2 sum(0.0f);
            for (std::uint32_t y = 0; y < kH; ++y) {
                for (std::uint32_t x = 0; x < kW; ++x) {
                    const std::uint8_t* a = with.pixel(x, y);
                    const std::uint8_t* b = without.pixel(x, y);
                    const int delta = std::max({std::abs(a[0] - b[0]), std::abs(a[1] - b[1]),
                                                std::abs(a[2] - b[2])});
                    if (delta > 10) {
                        ++f.area;
                        sum += glm::vec2(static_cast<float>(x), static_cast<float>(y));
                    }
                }
            }
            if (f.area > 0) {
                f.centroid = sum / static_cast<float>(f.area);
            }
            return f;
        };

        scene::Scene& mutableScene = engine.composition()->scene();
        std::size_t compared = 0;
        std::vector<glm::vec2> centroids;
        for (std::size_t i = 0; i < mutableScene.entities.size(); ++i) {
            scene::Entity& entity = mutableScene.entities[i];
            if (!entity.visible || entity.mesh >= mutableScene.meshes.size() ||
                entity.rig != scene::kInvalidRig ||
                entity.material.alphaMode == scene::AlphaMode::Blend ||
                entity.style == scene::MeshStyle::Water) {
                continue; // the reference path declines these by design; it says so in its counts
            }
            INFO("entity '" << entity.name << "'");

            production.resetTemporalHistory();
            const auto shownProduction = production.renderToImage(mutableScene, time, kW, kH);
            const auto shownReference = reference.renderToImage(mutableScene, kW, kH);
            REQUIRE(shownProduction.has_value());
            REQUIRE(shownReference.has_value());
            entity.visible = false;
            production.resetTemporalHistory();
            const auto hiddenProduction = production.renderToImage(mutableScene, time, kW, kH);
            const auto hiddenReference = reference.renderToImage(mutableScene, kW, kH);
            REQUIRE(hiddenProduction.has_value());
            REQUIRE(hiddenReference.has_value());
            entity.visible = true;

            const Footprint fp = footprintOf(*shownProduction, *hiddenProduction);
            const Footprint fr = footprintOf(*shownReference, *hiddenReference);
            INFO("production " << fp.area << " px at (" << fp.centroid.x << ", " << fp.centroid.y
                               << "); reference " << fr.area << " px at (" << fr.centroid.x << ", "
                               << fr.centroid.y << ")");
            if (fp.area < 60 || fr.area < 60) {
                continue; // off screen or too small to say anything about; not a failure
            }
            ++compared;
            // Same place. A transform error is not a few pixels -- it puts the object somewhere
            // else -- so the tolerance is generous and still decisive.
            CHECK(glm::length(fp.centroid - fr.centroid) < 4.0f);
            // Same size, within the difference two shading models make at an object's edge.
            const double ratio = static_cast<double>(fp.area) / static_cast<double>(fr.area);
            CHECK(ratio > 0.7);
            CHECK(ratio < 1.4);
            centroids.push_back(fp.centroid);
        }
        INFO(compared << " entities compared object by object");
        CHECK(compared >= 2);

        // The instrument can tell two objects apart. Without this, a footprint measure that
        // returned the whole frame for everything would satisfy every check above -- each object
        // would "agree" with its reference because both were the same meaningless region.
        float widestSeparation = 0.0f;
        for (std::size_t a = 0; a < centroids.size(); ++a) {
            for (std::size_t b = a + 1; b < centroids.size(); ++b) {
                widestSeparation = std::max(widestSeparation, glm::length(centroids[a] - centroids[b]));
            }
        }
        INFO("widest separation between two object centroids: " << widestSeparation << " px");
        CHECK(widestSeparation > 10.0f);
    }

    CHECK(ctx->errorCount() == 0);
}

// ---- Phase 7: the pass contract, asserted rather than described ----------------------------------
//
// The plan asks for every pass's inputs, outputs, clears and ownership to be enumerated, and for
// each pass to "establish the state it requires rather than relying on a previous pass". The
// enumeration is in the report; what can be *asserted* from outside is the join between the two
// halves this investigation built: **each isolation arm removes exactly its own pass, and nothing
// else.**
//
// That is a stronger claim than it looks. An arm that removed two passes would mean one subsystem
// owning another's state, which is precisely the "relies on a previous pass" the phase is about; an
// arm that removed none would be the untruthful control the plan forbids. And a pass that appears
// when its arm is off would be a pass running for nobody.
TEST_CASE("each isolation arm removes exactly its own pass", "[gpu][composition][forensics][passes]") {
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "qa" / "renderer-qa.scene.json";
    if (!fs::is_regular_file(project)) {
        SKIP("the RendererQA scene is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(project).has_value());
    // The volumetric march is off at zero density however its arm is set, and RendererQA authors
    // zero -- so without this the volume arm would be tested against a pass that never ran and
    // would pass by removing nothing.
    {
        auto* density = engine.params().findAs<float>("scene/volumeDensity");
        REQUIRE(density != nullptr);
        density->setBase(0.06f);
        engine.params().resetFinals();
    }
    FixedStepClock clock(60.0);

    // The set of pass labels a frame encoded, from the GPU timeline the renderer already keeps.
    // Several frames, because the timeline resolves asynchronously and an early frame reports
    // nothing at all.
    const auto passesWith = [&](const rendering::SceneRenderer::PassToggles& toggles) {
        renderer.setPassToggles(toggles);
        std::set<std::string> labels;
        for (int i = 0; i < 8; ++i) {
            const FrameTime time = engine.tick(clock);
            engine.setViewport(160, 120);
            engine.update(time);
            const auto image = renderer.renderToImage(engine.scene(), time, 160, 120);
            REQUIRE(image.has_value());
            for (const auto& entry : renderer.timeline().passes()) {
                labels.insert(std::string(entry.label));
            }
        }
        return labels;
    };

    const rendering::SceneRenderer::PassToggles all;
    const std::set<std::string> full = passesWith(all);
    {
        std::string names;
        for (const std::string& label : full) {
            names += (names.empty() ? "" : " ") + label;
        }
        INFO("the full frame encodes " << full.size() << " distinct passes: " << names);
        // Each arm below is tested against a pass that actually runs in this scene; an arm whose
        // pass is absent would "pass" by removing nothing.
        // `volume` is a family since ADR-139 split the march from the composite, so this asks
        // whether a pass of that name *or* one prefixed by it ran. The check is unchanged in
        // strength: an arm whose pass is absent still "passes" by removing nothing, which is what
        // this guards against.
        for (const char* needed : {"shadow", "ao", "volume", "shadowmask"}) {
            INFO("needed: " << needed);
            const std::string prefix(needed);
            const bool present =
                std::any_of(full.begin(), full.end(), [&](const std::string& label) {
                    return label == prefix || label.compare(0, prefix.size() + 1, prefix + ".") == 0;
                });
            REQUIRE(present);
        }
    }
    // The timeline has to be reporting at all, or every comparison below is between two empty sets.
    REQUIRE(full.size() > 4);

    struct Arm {
        const char* name;
        bool rendering::SceneRenderer::PassToggles::*field;
        const char* pass;   // the label that must disappear, or nullptr when the arm removes work
                            // from inside a pass rather than removing the pass itself
    };
    const Arm arms[] = {
        {"shadows", &rendering::SceneRenderer::PassToggles::shadows, "shadow"},
        {"ao", &rendering::SceneRenderer::PassToggles::ao, "ao"},
        {"volume", &rendering::SceneRenderer::PassToggles::volume, "volume"},
        {"shadow mask", &rendering::SceneRenderer::PassToggles::shadowMask, "shadowmask"},
        // These three draw inside the scene pass rather than owning one, so what they remove is
        // draws and not a pass. Named here so the distinction is recorded rather than discovered.
        {"water", &rendering::SceneRenderer::PassToggles::water, nullptr},
        {"transparency", &rendering::SceneRenderer::PassToggles::transparency, nullptr},
        {"culling", &rendering::SceneRenderer::PassToggles::culling, nullptr},
    };

    for (const Arm& arm : arms) {
        INFO("arm: " << arm.name);
        auto off = all;
        off.*arm.field = false;
        const std::set<std::string> reduced = passesWith(off);

        std::vector<std::string> missing;
        std::set_difference(full.begin(), full.end(), reduced.begin(), reduced.end(),
                            std::back_inserter(missing));
        std::vector<std::string> extra;
        std::set_difference(reduced.begin(), reduced.end(), full.begin(), full.end(),
                            std::back_inserter(extra));
        std::string report;
        for (const std::string& m : missing) {
            report += (report.empty() ? "" : ", ") + m;
        }
        INFO("gone: [" << report << "]; unexpectedly added: " << extra.size());
        // Nothing new may appear because a subsystem was switched off.
        CHECK(extra.empty());
        if (arm.pass != nullptr) {
            // Exactly its own pass, and only its own. `post` is excluded from this table because the
            // post chain is several labelled passes rather than one, which is a fact about the chain
            // and not a violation of the contract.
            INFO("expected '" << arm.pass << "' to be the only pass removed");
            CHECK(missing.size() == 1);
            CHECK(std::find(missing.begin(), missing.end(), arm.pass) != missing.end());
        } else {
            // A draw-level arm must not take a pass away with it: the pass still runs, with less in
            // it. A water arm that removed the scene pass would be water owning the pass everything
            // else draws into.
            CHECK(missing.empty());
        }
    }

    // Post is the exception the table names: it owns several passes, and switching it off must
    // remove all of them and nothing else.
    {
        auto off = all;
        off.post = false;
        const std::set<std::string> reduced = passesWith(off);
        std::vector<std::string> missing;
        std::set_difference(full.begin(), full.end(), reduced.begin(), reduced.end(),
                            std::back_inserter(missing));
        INFO(missing.size() << " passes gone with post off");
        CHECK(missing.size() >= 1);
        for (const std::string& gone : missing) {
            INFO("gone: " << gone);
            CHECK(gone.rfind("post", 0) == 0);
        }
    }
    CHECK(ctx->errorCount() == 0);
}

// ---- Phase 4.4: the diagnostic views, checked before any more are built --------------------------
//
// Seven auxiliary views already exist -- normal, roughness, velocity, emission, ids, occlusion,
// depth -- and the plan's rule applies to them exactly as it applies to the isolation arms: a view
// that shows the same thing as another, or nothing at all, is worse than a missing one, because
// somebody looks at it and concludes something.
//
// Two claims, and the second is the one with teeth. Every view must differ from the shaded frame and
// from every other view; and the depth view must actually vary with depth, which is checked by
// moving the camera and requiring the image to follow.
TEST_CASE("every auxiliary debug view shows something of its own",
          "[gpu][composition][forensics][views]") {
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "qa" / "renderer-qa.scene.json";
    if (!fs::is_regular_file(project)) {
        SKIP("the RendererQA scene is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(project).has_value());
    FixedStepClock clock(60.0);
    constexpr std::uint32_t kW = 160;
    constexpr std::uint32_t kH = 120;

    const auto renderView = [&](rendering::AuxDebugView view) {
        renderer.setAuxDebugView(view);
        const FrameTime time = engine.tick(clock);
        engine.setViewport(kW, kH);
        engine.update(time);
        renderer.resetTemporalHistory();
        auto image = renderer.renderToImage(engine.scene(), time, kW, kH);
        REQUIRE(image.has_value());
        return std::move(*image);
    };

    aimCompositionCamera(engine.params(), glm::vec3(0.0f, 3.0f, 10.0f), glm::vec3(0.0f, 1.0f, -4.0f));

    // Overdraw and FragmentDensity (ADR-115) are deliberately not in this list: their counting pass
    // only sees plain, non-skinned opaque *entities*, and every visible thing in RendererQA is
    // either procedural (the floor grid, the two boxes, the sphere), the orb (a blended/SDF surface)
    // or the alien (a skinned gltf) -- none of which reach it, so the view would legitimately come
    // back blank here and the blank-view assertion below would fail for a reason that has nothing to
    // do with the views being broken. They get their own test, built on plain mesh entities that are
    // inside the diagnostic's documented scope: "overdraw and fragment density count submitted
    // fragments, not visible ones" below.
    const rendering::AuxDebugView views[] = {
        rendering::AuxDebugView::None,      rendering::AuxDebugView::Normal,
        rendering::AuxDebugView::Roughness, rendering::AuxDebugView::Velocity,
        rendering::AuxDebugView::Emission,  rendering::AuxDebugView::Ids,
        rendering::AuxDebugView::Occlusion, rendering::AuxDebugView::Depth,
        rendering::AuxDebugView::LinearDepth, rendering::AuxDebugView::DepthEdges,
    };
    std::map<std::uint64_t, std::string> seen;
    for (const rendering::AuxDebugView view : views) {
        const gpu::Image8 image = renderView(view);
        const std::uint64_t hash = gpu::hashImage(image);
        const std::string name = rendering::auxDebugViewName(view);
        INFO("view: " << name);
        const auto clash = seen.find(hash);
        if (clash != seen.end()) {
            INFO("identical to '" << clash->second << "'");
        }
        // Each view is its own picture. Two views that hash alike are either the same buffer shown
        // twice or two empty frames, and both of those are a diagnostic that lies.
        CHECK(clash == seen.end());
        seen.emplace(hash, name);
    }
    CHECK(seen.size() == std::size(views));

    SECTION("the depth view varies with depth") {
        // A view called depth that does not move when the camera does is showing something else.
        // The mean of the frame is the instrument: pulling back puts more distant surface in shot,
        // and the whole image shifts rather than one pixel.
        const auto meanOf = [](const gpu::Image8& image) {
            double total = 0.0;
            for (std::uint32_t y = 0; y < kH; ++y) {
                for (std::uint32_t x = 0; x < kW; ++x) {
                    const std::uint8_t* p = image.pixel(x, y);
                    total += static_cast<double>(p[0] + p[1] + p[2]) / 3.0;
                }
            }
            return total / static_cast<double>(kW * kH);
        };
        aimCompositionCamera(engine.params(), glm::vec3(0.0f, 2.0f, 6.0f), glm::vec3(0.0f, 1.0f, -4.0f));
        const double near = meanOf(renderView(rendering::AuxDebugView::Depth));
        aimCompositionCamera(engine.params(), glm::vec3(0.0f, 2.0f, 26.0f), glm::vec3(0.0f, 1.0f, -4.0f));
        const double far = meanOf(renderView(rendering::AuxDebugView::Depth));
        INFO("depth view mean: " << near << " from 6 m, " << far << " from 26 m");
        CHECK(std::fabs(far - near) > 1.0);
    }

    SECTION("the id view separates objects") {
        // Identifiers are a *palette*, not a gradient: the value of a pixel is which object it is.
        // So the check is that the view contains several distinct values, where the shaded frame's
        // colours would be a continuum.
        aimCompositionCamera(engine.params(), glm::vec3(0.0f, 3.0f, 10.0f), glm::vec3(0.0f, 1.0f, -4.0f));
        const gpu::Image8 ids = renderView(rendering::AuxDebugView::Ids);
        std::set<std::uint32_t> distinct;
        for (std::uint32_t y = 0; y < kH; y += 2) {
            for (std::uint32_t x = 0; x < kW; x += 2) {
                const std::uint8_t* p = ids.pixel(x, y);
                distinct.insert((static_cast<std::uint32_t>(p[0]) << 16) |
                                (static_cast<std::uint32_t>(p[1]) << 8) | p[2]);
            }
        }
        INFO(distinct.size() << " distinct identifier colours");
        // Background plus at least two objects. RendererQA has a floor, an alien and an orb in this
        // view, so anything less than three means the view is not separating them.
        CHECK(distinct.size() >= 3);
    }

    SECTION("linear depth is the buffer it claims to show, and the exponential view is not") {
        // The two depth views are deliberately different pictures of the same buffer, and the
        // difference is the whole reason the second exists: `depth` compresses so a three-kilometre
        // view is legible at all, `linear depth` does not, so a distance can be read off it.
        //
        // The instrument took three tries, and the two that failed are worth keeping. The frame's
        // mean measured the wrong thing entirely -- pulling the camera back put less sky in shot, so
        // the average *fell* while every surface got further away. Moving the camera along its view
        // axis so the centre ray stayed on one surface was better arithmetic and still wrong: this
        // scene has an object right in front of the camera, so the move passed *through* the
        // surface being measured and the distance jumped the other way.
        //
        // What finally works is not a proxy at all. The view claims to show the linear-depth target;
        // that target can be read back; so every pixel of the picture is checked against the number
        // it is supposed to be a picture of. No camera arithmetic, no assumption about the scene,
        // and it fails for any wrong constant rather than only for a wrong trend.
        const float range = 40.0f;
        renderer.setAuxDebugScale(renderer.diagnosticFrame().farPlane / range);
        aimCompositionCamera(engine.params(), glm::vec3(0.0f, 3.0f, 10.0f), glm::vec3(0.0f, 1.0f, -4.0f));
        const gpu::Image8 shown = renderView(rendering::AuxDebugView::LinearDepth);
        auto bits = gpu::readTextureR32Uint(*ctx, renderer.linearDepthTexture(), kW, kH);
        REQUIRE(bits.has_value());
        REQUIRE(bits->size() == static_cast<std::size_t>(kW) * kH);

        std::size_t surfaces = 0;
        std::size_t sky = 0;
        double worst = 0.0;
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                const float depth = std::bit_cast<float>((*bits)[static_cast<std::size_t>(y) * kW + x]);
                const int byte = shown.pixel(x, y)[0];
                if (depth > 1.0e6f) {
                    // The linear target's own "nothing was drawn" sentinel. Full white, and its own
                    // value: an empty sky and a surface at the far plane are not the same picture.
                    ++sky;
                    CHECK(byte == 255);
                    continue;
                }
                ++surfaces;
                const double expected = std::clamp(static_cast<double>(depth) / static_cast<double>(range), 0.0, 1.0) * 255.0;
                worst = std::max(worst, std::fabs(expected - static_cast<double>(byte)));
            }
        }
        INFO(surfaces << " surface pixels and " << sky << " sky, worst error " << worst << " of 255");
        // Both populations are present, or one of the two claims above is being asserted over an
        // empty set.
        REQUIRE(surfaces > 100);
        REQUIRE(sky > 100);
        // One step of 8-bit quantisation, and nothing else.
        CHECK(worst <= 1.5);

        // The control, and the reason two views exist. The same comparison against the same buffer,
        // with the exponential view in the frame: it is a *different* function of the same numbers,
        // so it must fail this check by a wide margin. Without this, a linear view that had quietly
        // become the exponential one would still pass everything above.
        renderer.setAuxDebugScale(0.0f);
        const gpu::Image8 compressed = renderView(rendering::AuxDebugView::Depth);
        double worstExponential = 0.0;
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                const float depth = std::bit_cast<float>((*bits)[static_cast<std::size_t>(y) * kW + x]);
                if (depth > 1.0e6f) {
                    continue;
                }
                const double expected = std::clamp(static_cast<double>(depth) / static_cast<double>(range), 0.0, 1.0) * 255.0;
                worstExponential = std::max(worstExponential, std::fabs(expected - compressed.pixel(x, y)[0]));
            }
        }
        INFO("the exponential view differs from the linear reading by up to " << worstExponential);
        CHECK(worstExponential > 16.0);

        // And the regression guard for what made all of the above possible. This pass used to draw
        // into the HDR target, so every diagnostic went through auto-exposure and a filmic curve on
        // its way to the screen -- a linear 1.0 arrived as 202, an identifier's palette moved with
        // how bright the scene happened to be, and none of the comparisons above could have been
        // written. Four stops of exposure compensation must change nothing here.
        auto* compensation = engine.params().findAs<float>("camera/exposure/compensation");
        REQUIRE(compensation != nullptr);
        renderer.setAuxDebugScale(renderer.diagnosticFrame().farPlane / range);
        const std::uint64_t before = gpu::hashImage(renderView(rendering::AuxDebugView::LinearDepth));
        compensation->setBase(4.0f);
        engine.params().resetFinals();
        const std::uint64_t after = gpu::hashImage(renderView(rendering::AuxDebugView::LinearDepth));
        CHECK(before == after);
        // The control: the same four stops are plainly visible in the shaded frame, so the equality
        // above is the view being independent of exposure rather than exposure doing nothing.
        const std::uint64_t litBright = gpu::hashImage(renderView(rendering::AuxDebugView::None));
        compensation->setBase(0.0f);
        engine.params().resetFinals();
        const std::uint64_t litNormal = gpu::hashImage(renderView(rendering::AuxDebugView::None));
        CHECK(litBright != litNormal);
        renderer.setAuxDebugScale(0.0f);
    }

    SECTION("depth edges are edges, not surfaces") {
        // A relative threshold is what makes this a silhouette finder rather than a brightness map.
        // The assertion is the shape of the histogram: most of the frame is flat (near zero) and a
        // small minority is lit. A view that marked every surface would have no dark majority, and
        // one that marked nothing would have no lit minority at all -- both are checked.
        aimCompositionCamera(engine.params(), glm::vec3(0.0f, 3.0f, 10.0f), glm::vec3(0.0f, 1.0f, -4.0f));
        const gpu::Image8 edges = renderView(rendering::AuxDebugView::DepthEdges);
        std::size_t dark = 0;
        std::size_t lit = 0;
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                const std::uint8_t* p = edges.pixel(x, y);
                if (p[0] < 24) {
                    ++dark;
                } else if (p[0] > 96) {
                    ++lit;
                }
            }
        }
        const double total = static_cast<double>(kW * kH);
        INFO(dark << " flat pixels and " << lit << " edge pixels of " << total);
        CHECK(static_cast<double>(dark) / total > 0.5);
        CHECK(lit > 0);
    }

    SECTION("object depth shows the named object and nothing else") {
        // The view that is easiest to get silently wrong: with nothing selected it must be empty
        // rather than object zero, and with something selected the lit region must be that object's
        // and must move when the selection changes. All three are checked, because a view that
        // showed *an* object whatever you asked for would look right in a screenshot.
        aimCompositionCamera(engine.params(), glm::vec3(0.0f, 3.0f, 10.0f), glm::vec3(0.0f, 1.0f, -4.0f));
        const auto litPixels = [](const gpu::Image8& image) {
            std::size_t count = 0;
            for (std::uint32_t y = 0; y < kH; ++y) {
                for (std::uint32_t x = 0; x < kW; ++x) {
                    const std::uint8_t* p = image.pixel(x, y);
                    if (p[2] > 128) { // the object is drawn blue-dominant; the rest is near black
                        ++count;
                    }
                }
            }
            return count;
        };

        renderer.setDiagnosticEntity("");
        CHECK(litPixels(renderView(rendering::AuxDebugView::ObjectDepth)) == 0);

        // Two entities the QA scene is known to contain, selected in turn.
        std::vector<std::string> names;
        for (const scene::Entity& entity : engine.scene().entities) {
            if (entity.visible) {
                names.push_back(entity.name);
            }
        }
        REQUIRE(names.size() >= 2);
        std::size_t bestCount = 0;
        std::string bestName;
        std::size_t secondCount = 0;
        std::string secondName;
        for (const std::string& name : names) {
            renderer.setDiagnosticEntity(name);
            const std::size_t count = litPixels(renderView(rendering::AuxDebugView::ObjectDepth));
            if (count > bestCount) {
                secondCount = bestCount;
                secondName = bestName;
                bestCount = count;
                bestName = name;
            } else if (count > secondCount) {
                secondCount = count;
                secondName = name;
            }
        }
        INFO("'" << bestName << "' covers " << bestCount << " px, '" << secondName << "' covers " << secondCount);
        // Something is on screen, it is not the whole frame, and a different selection is a
        // different picture rather than the same one relabelled.
        CHECK(bestCount > 0);
        CHECK(bestCount < static_cast<std::size_t>(kW * kH));
        CHECK(secondCount != bestCount);

        // And the sky belongs to nobody. This is the assertion with teeth, and it exists because
        // the section first passed against a shader that matched only the low sixteen bits of the
        // identifier word -- where entity zero's pick id and an empty pixel are *both* 0 -- so
        // selecting the first entity painted the entire sky as that object. Every bound above was
        // satisfied by the handful of pixels belonging to other objects, and the test agreed with a
        // view that was almost entirely wrong. A count cannot catch that; asking whether a
        // *particular region* is claimed can. The top rows of this framing are empty in every
        // selection, so no selection may own them. (A large floor legitimately covers two thirds of
        // this frame, which is why the bound cannot simply be "a minority".)
        for (const std::string& name : names) {
            renderer.setDiagnosticEntity(name);
            const gpu::Image8 image = renderView(rendering::AuxDebugView::ObjectDepth);
            std::size_t skyClaimed = 0;
            for (std::uint32_t y = 0; y < 8; ++y) {
                for (std::uint32_t x = 0; x < kW; ++x) {
                    skyClaimed += image.pixel(x, y)[2] > 128 ? 1 : 0;
                }
            }
            INFO("'" << name << "' claims " << skyClaimed << " sky pixels");
            CHECK(skyClaimed == 0);
        }
        renderer.setDiagnosticEntity("");
    }

    renderer.setAuxDebugView(rendering::AuxDebugView::None);
    CHECK(ctx->errorCount() == 0);
}

// ---- ADR-115: overdraw and fragment density -------------------------------------------------------
//
// The one property that matters for these two views is the one the task brief calls out by name: a
// view that always shows the same thing is worse than none. So rather than judging the rendered
// picture alone, this reads the counter buffer the views are built on (gpu::readBuffer, the same
// technique test_shadows_gpu.cpp uses on the cluster buffer) and checks the actual per-pixel counts
// against two scenes built to differ in exactly one way: five cubes stacked so every one of them
// covers the same screen pixels, against the same five cubes spread out so none of them do.
TEST_CASE("overdraw and fragment density count submitted fragments, not visible ones",
          "[gpu][composition][forensics][views]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    constexpr std::uint32_t kW = 128;
    constexpr std::uint32_t kH = 96;
    const FrameTime time{};

    const auto buildScene = [&](bool overlapping) {
        scene::Scene scene;
        const auto mesh = scene.addMesh(scene::makeCube(1.0f));
        for (int i = 0; i < 5; ++i) {
            scene::Entity& cube = scene.addEntity("cube" + std::to_string(i), mesh);
            // Overlapping: every cube sits on the camera's centre ray, at a different depth, so all
            // five cover the same pixels and a pixel there is shaded five times. Spread: the same
            // five cubes offset sideways by more than their own width, so none of them share a pixel
            // with another and every covered pixel is shaded once.
            cube.transform.position = overlapping ? glm::vec3(0.0f, 0.0f, -static_cast<float>(i) * 0.2f)
                                                  : glm::vec3(static_cast<float>(i) * 3.0f - 6.0f, 0.0f, 0.0f);
            cube.material.baseColor = {0.6f, 0.6f, 0.6f};
        }
        scene.camera.position = {0.0f, 0.0f, 12.0f};
        scene.camera.target = {0.0f, 0.0f, 0.0f};
        scene::PunctualLight& key = scene.lights.emplace_back();
        key.type = scene::PunctualLight::Type::Directional;
        key.direction = glm::normalize(glm::vec3(-0.4f, -0.8f, -0.4f));
        key.intensity = 3.0f;
        return scene;
    };

    const auto readCounts = [&](rendering::AuxDebugView view, const scene::Scene& scene) {
        renderer.setAuxDebugView(view);
        auto image = renderer.renderToImage(scene, time, kW, kH);
        REQUIRE(image.has_value());
        auto raw = gpu::readBuffer(*ctx, renderer.overdrawBuffer(), 0,
                                   static_cast<std::uint64_t>(kW) * kH * sizeof(std::uint32_t));
        REQUIRE(raw.has_value());
        std::vector<std::uint32_t> counts(static_cast<std::size_t>(kW) * kH);
        std::memcpy(counts.data(), raw->data(), raw->size());
        return counts;
    };
    // The average count *over pixels the geometry actually touched*, not over the whole frame. A
    // plain sum conflates two different things: how many times a covered pixel was shaded, and how
    // much of the screen is covered at all -- and the spread scene covers roughly five times the
    // screen area the stacked scene does, so its sum would win even though every one of its pixels
    // is shaded exactly once. This is the number overdraw is actually defined by.
    const auto meanOverCovered = [](const std::vector<std::uint32_t>& counts) {
        std::uint64_t sum = 0;
        std::uint64_t covered = 0;
        for (const std::uint32_t c : counts) {
            sum += c;
            covered += c > 0 ? 1 : 0;
        }
        return covered > 0 ? static_cast<double>(sum) / static_cast<double>(covered) : 0.0;
    };

    const scene::Scene overlapping = buildScene(true);
    const scene::Scene spread = buildScene(false);

    SECTION("the counting pass does not run for an ordinary view") {
        // The whole point of making this an opt-in pass is that it must not touch the buffer, let
        // alone the frame, unless one of the two views that read it is selected.
        renderer.setAuxDebugView(rendering::AuxDebugView::None);
        auto image = renderer.renderToImage(overlapping, time, kW, kH);
        REQUIRE(image.has_value());
        auto raw = gpu::readBuffer(*ctx, renderer.overdrawBuffer(), 0,
                                   static_cast<std::uint64_t>(kW) * kH * sizeof(std::uint32_t));
        REQUIRE(raw.has_value());
        std::vector<std::uint32_t> counts(static_cast<std::size_t>(kW) * kH);
        std::memcpy(counts.data(), raw->data(), raw->size());
        const std::uint64_t total = std::accumulate(counts.begin(), counts.end(), std::uint64_t{0});
        INFO("total count with an ordinary view selected: " << total);
        CHECK(total == 0);
    }

    SECTION("overdraw counts more fragments per covered pixel for the overlapping scene") {
        const auto overlapCounts = readCounts(rendering::AuxDebugView::Overdraw, overlapping);
        const auto spreadCounts = readCounts(rendering::AuxDebugView::Overdraw, spread);
        const double overlapMean = meanOverCovered(overlapCounts);
        const double spreadMean = meanOverCovered(spreadCounts);
        const std::uint32_t overlapMax = *std::max_element(overlapCounts.begin(), overlapCounts.end());
        const std::uint32_t spreadMax = *std::max_element(spreadCounts.begin(), spreadCounts.end());
        INFO("overlapping: mean " << overlapMean << ", max " << overlapMax << "; spread: mean " << spreadMean
                                   << ", max " << spreadMax);
        // Both scenes draw the same five cubes covering (as it happens) close to the same total
        // screen area, so the pixel-count sum alone would not separate them; the average shading
        // count *per covered pixel* is the number that actually says "these overlap and those don't".
        CHECK(overlapMean > spreadMean);
        // The strongest version of the same claim: some pixel was shaded five times (once per cube,
        // back faces culled so a lone cube contributes exactly one) in the overlapping scene, where
        // the spread scene -- five cubes that share no pixel with each other -- never shades a pixel
        // more than once.
        CHECK(overlapMax >= 5);
        CHECK(spreadMax == 1);
    }

    SECTION("fragment density is a different picture of the same data, and it too tracks overlap") {
        // Mode 12 is a spatial average of the same counter, not a copy of mode 11 -- so the two
        // rendered images must differ from each other, and the density view must still separate the
        // two scenes the way the raw counter does.
        renderer.setAuxDebugScale(1.0f);
        const gpu::Image8 overdrawImage = [&] {
            renderer.setAuxDebugView(rendering::AuxDebugView::Overdraw);
            auto image = renderer.renderToImage(overlapping, time, kW, kH);
            REQUIRE(image.has_value());
            return std::move(*image);
        }();
        const gpu::Image8 densityImage = [&] {
            renderer.setAuxDebugView(rendering::AuxDebugView::FragmentDensity);
            auto image = renderer.renderToImage(overlapping, time, kW, kH);
            REQUIRE(image.has_value());
            return std::move(*image);
        }();
        CHECK(gpu::hashImage(overdrawImage) != gpu::hashImage(densityImage));

        const auto overlapCounts = readCounts(rendering::AuxDebugView::FragmentDensity, overlapping);
        const auto spreadCounts = readCounts(rendering::AuxDebugView::FragmentDensity, spread);
        const double overlapMean = meanOverCovered(overlapCounts);
        const double spreadMean = meanOverCovered(spreadCounts);
        INFO("overlapping mean " << overlapMean << ", spread mean " << spreadMean);
        CHECK(overlapMean > spreadMean);
    }

    renderer.setAuxDebugView(rendering::AuxDebugView::None);
    CHECK(ctx->errorCount() == 0);
}

// ---- Phase 9.3: the guards that need a device ---------------------------------------------------
//
// The unit binary shut four doors NaN was walking through (`test_renderer_layout_guards.cpp`). The
// material-to-`ObjectUniforms` packing was the one it could not reach, because it lives inside
// `SceneRenderer::render` behind a device -- and it is the door with the widest blast radius. A NaN
// in a base colour does not stay in its object: bloom's downsample averages it across a tile, the
// tone map carries the tile to the frame, and what arrives is a bright or black region nowhere near
// anything that could be blamed for it.
//
// So the test is in two halves, and the first is the one that gives the second its meaning: prove
// the spread is real by measuring it with the guard's substitution *accepted*, then prove the frame
// is otherwise unchanged.
TEST_CASE("a non-finite material is replaced rather than shaded", "[gpu][composition][forensics][guards]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    constexpr std::uint32_t kW = 96;
    constexpr std::uint32_t kH = 96;
    scene::Scene scene;
    const auto mesh = scene.addMesh(scene::makeCube(1.0f));
    scene::Entity& cube = scene.addEntity("cube", mesh);
    cube.transform.position = {0.0f, 0.0f, 0.0f};
    cube.material.baseColor = {0.1f, 0.7f, 0.3f};
    cube.material.roughness = 0.6f;
    scene.camera.position = {0.0f, 0.0f, 4.0f};
    scene.camera.target = {0.0f, 0.0f, 0.0f};
    scene::PunctualLight& key = scene.lights.emplace_back();
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.4f, -0.8f, -0.4f));
    key.intensity = 3.0f;

    const FrameTime time{};
    const auto shoot = [&] {
        auto image = renderer.renderToImage(scene, time, kW, kH);
        REQUIRE(image.has_value());
        return std::move(*image);
    };
    // Against the frame's *own* background, taken from a corner, rather than an absolute
    // threshold. The environment lights the whole frame, so "brighter than 24" counts the sky and
    // reports every pixel as the object -- which is what the first version of this did, and it made
    // the upper bound below unsatisfiable.
    const auto litPixels = [&](const gpu::Image8& image) {
        const std::uint8_t* corner = image.pixel(0, 0);
        const glm::ivec3 background(corner[0], corner[1], corner[2]);
        std::size_t count = 0;
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                const std::uint8_t* p = image.pixel(x, y);
                const int delta = std::max({std::abs(p[0] - background.r), std::abs(p[1] - background.g),
                                            std::abs(p[2] - background.b)});
                if (delta > 14) {
                    ++count;
                }
            }
        }
        return count;
    };

    const gpu::Image8 healthy = shoot();
    const std::size_t healthyLit = litPixels(healthy);
    INFO(healthyLit << " lit pixels with a finite material");
    REQUIRE(healthyLit > 100);
    REQUIRE(healthyLit < static_cast<std::size_t>(kW * kH));

    SECTION("each non-finite field is caught, and the object stays where it is") {
        // Every field the packer reads, one at a time, because a guard that checks the first three
        // and not the ninth is a guard that will be found by the ninth.
        struct Poison {
            const char* what;
            std::function<void(scene::Material&)> apply;
        };
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        const std::vector<Poison> poisons{
            {"baseColor", [&](scene::Material& m) { m.baseColor.g = nan; }},
            {"opacity", [&](scene::Material& m) { m.opacity = nan; }},
            {"emissiveColor", [&](scene::Material& m) { m.emissiveColor.b = inf; }},
            {"emissiveIntensity", [&](scene::Material& m) { m.emissiveIntensity = nan; }},
            {"roughness", [&](scene::Material& m) { m.roughness = nan; }},
            {"metallic", [&](scene::Material& m) { m.metallic = inf; }},
            {"normalScale", [&](scene::Material& m) { m.normalScale = nan; }},
            {"occlusionStrength", [&](scene::Material& m) { m.occlusionStrength = nan; }},
            {"alphaCutoff", [&](scene::Material& m) { m.alphaCutoff = nan; }},
        };
        const scene::Material pristine = cube.material;
        for (const Poison& poison : poisons) {
            cube.material = pristine;
            poison.apply(cube.material);
            const gpu::Image8 image = shoot();
            const std::size_t lit = litPixels(image);
            INFO("poisoned " << poison.what << ": " << lit << " lit pixels");
            // The object is still drawn, in roughly the same place, and the frame is not a NaN
            // wash. Both bounds matter: dropping the draw would make the object vanish, which is
            // the single hardest report to act on, and a NaN reaching the shader would take the
            // whole frame with it through bloom.
            CHECK(lit > healthyLit / 3);
            CHECK(lit < static_cast<std::size_t>(kW * kH));
            // Every pixel is a number. This is the assertion the guard exists for -- a NaN that
            // reached the tone map would show up here as a channel that is neither dark nor bright
            // but arbitrary, and in practice as a saturated frame.
            std::size_t saturated = 0;
            for (std::uint32_t y = 0; y < kH; ++y) {
                for (std::uint32_t x = 0; x < kW; ++x) {
                    const std::uint8_t* p = image.pixel(x, y);
                    if (p[0] == 255 && p[1] == 255 && p[2] == 255) {
                        ++saturated;
                    }
                }
            }
            INFO(saturated << " fully saturated pixels");
            CHECK(saturated < static_cast<std::size_t>(kW * kH) / 4);
        }
        cube.material = pristine;
    }

    SECTION("the substitution is visible, and a healthy frame is untouched") {
        // The control for the whole test: the guard must not be firing on the healthy material.
        // Without this, a `checkMaterial` that returned "bad" for everything would pass every
        // assertion above -- the object would be drawn, in place, unsaturated, in magenta.
        const gpu::Image8 again = shoot();
        CHECK(gpu::hashImage(again) == gpu::hashImage(healthy));

        cube.material.baseColor.r = std::numeric_limits<float>::quiet_NaN();
        const gpu::Image8 substituted = shoot();
        CHECK(gpu::hashImage(substituted) != gpu::hashImage(healthy));
        // Magenta: red and blue present, green suppressed, over the object's own pixels.
        std::size_t magenta = 0;
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = 0; x < kW; ++x) {
                const std::uint8_t* p = substituted.pixel(x, y);
                if (p[0] > 40 && p[2] > 40 && p[1] * 2 < p[0] && p[1] * 2 < p[2]) {
                    ++magenta;
                }
            }
        }
        INFO(magenta << " magenta pixels");
        CHECK(magenta > healthyLit / 4);
    }

    CHECK(ctx->errorCount() == 0);
}

// ---- Phase 7: target load/store, resize and the depth views in the pass matrix -------------------
//
// The pass table in the report says what each pass does to its targets. A table is a description,
// and a description of a clear is exactly the kind of claim that is true when it is written and
// quietly stops being true later. These are the three parts of it that can be checked from outside
// the renderer without transcribing descriptors back into assertions.
TEST_CASE("the passes establish their targets rather than inheriting them",
          "[gpu][composition][forensics][passes][targets]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    constexpr std::uint32_t kW = 128;
    constexpr std::uint32_t kH = 96;
    scene::Scene scene;
    const auto mesh = scene.addMesh(scene::makeCube(1.0f));
    scene::Entity& cube = scene.addEntity("cube", mesh);
    cube.material.baseColor = {0.8f, 0.2f, 0.2f};
    cube.material.emissiveColor = {1.0f, 0.4f, 0.1f};
    cube.material.emissiveIntensity = 4.0f;
    scene.camera.position = {0.0f, 0.0f, 4.0f};
    scene.camera.target = {0.0f, 0.0f, 0.0f};
    scene::PunctualLight& key = scene.lights.emplace_back();
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.4f, -0.8f, -0.4f));
    key.intensity = 3.0f;

    const FrameTime time{};
    const auto shoot = [&] {
        auto image = renderer.renderToImage(scene, time, kW, kH);
        REQUIRE(image.has_value());
        return std::move(*image);
    };

    SECTION("the identifier target is cleared each frame, not carried over") {
        // The one auxiliary target whose contents are unambiguous: a non-zero identifier means an
        // object was written there. Draw the cube, then hide it, and the target must be empty. If
        // the scene pass loaded instead of clearing, the previous frame's identifiers would still
        // be sitting in it -- and the picker reads this target, so a stale id is a click that
        // selects an object that is no longer on screen.
        shoot();
        auto withCube = gpu::readTextureR32Uint(*ctx, renderer.identifierTexture(), kW, kH);
        REQUIRE(withCube.has_value());
        std::size_t written = 0;
        for (const std::uint32_t id : *withCube) {
            written += id != 0 ? 1 : 0;
        }
        INFO(written << " identifier texels written with the cube visible");
        REQUIRE(written > 100);

        cube.visible = false;
        shoot();
        auto withoutCube = gpu::readTextureR32Uint(*ctx, renderer.identifierTexture(), kW, kH);
        REQUIRE(withoutCube.has_value());
        std::size_t stale = 0;
        for (const std::uint32_t id : *withoutCube) {
            stale += id != 0 ? 1 : 0;
        }
        INFO(stale << " identifier texels still set with nothing to draw");
        CHECK(stale == 0);
        cube.visible = true;
    }

    SECTION("the linear-depth target is cleared to its sentinel, not to zero") {
        // The clear value is 1e7, the "nothing was drawn" sentinel, and it has to be that rather
        // than zero: a cleared-to-zero linear depth reads as a surface *at the camera*, which is
        // the worst possible default for AO, water and the fog the particle pass does against it.
        cube.visible = false;
        shoot();
        auto bits = gpu::readTextureR32Uint(*ctx, renderer.linearDepthTexture(), kW, kH);
        REQUIRE(bits.has_value());
        std::size_t sentinel = 0;
        std::size_t atTheCamera = 0;
        for (const std::uint32_t word : *bits) {
            const float d = std::bit_cast<float>(word);
            sentinel += d > 1.0e6f ? 1 : 0;
            atTheCamera += d < 1.0f ? 1 : 0;
        }
        INFO(sentinel << " texels at the sentinel and " << atTheCamera << " within a metre of the camera");
        CHECK(sentinel == static_cast<std::size_t>(kW) * kH);
        CHECK(atTheCamera == 0);
        cube.visible = true;
    }

    SECTION("a resized renderer draws what a renderer born at that size draws") {
        // Target recreation. A renderer that has been through other sizes must arrive back at a
        // size with the same picture as one that has only ever known it -- otherwise something
        // survived the recreation, and the thing that survives a resize is exactly the class of
        // state this investigation keeps finding at boundaries.
        renderer.resetTemporalHistory();
        const std::uint64_t born = gpu::hashImage(shoot());

        REQUIRE(renderer.renderToImage(scene, time, 320, 240).has_value());
        REQUIRE(renderer.renderToImage(scene, time, 64, 64).has_value());
        REQUIRE(renderer.renderToImage(scene, time, 257, 129).has_value()); // deliberately not a round size
        renderer.resetTemporalHistory();
        const std::uint64_t returned = gpu::hashImage(shoot());
        CHECK(born == returned);

        // And a genuinely fresh renderer agrees with both, which is what makes the equality above
        // about the targets rather than about one renderer being self-consistently wrong.
        rendering::SceneRenderer fresh(*ctx, shaders);
        REQUIRE(fresh.init().has_value());
        auto first = fresh.renderToImage(scene, time, kW, kH);
        REQUIRE(first.has_value());
        CHECK(gpu::hashImage(*first) == born);
    }

    SECTION("every auxiliary view survives a resize and a selection change") {
        // Auxiliary debug target selection through all passes, at sizes that are not multiples of
        // anything convenient -- a readback or a bind group sized from a stale extent shows up here
        // as a device error rather than as a picture somebody has to notice.
        const rendering::AuxDebugView views[] = {
            rendering::AuxDebugView::Normal,     rendering::AuxDebugView::Roughness,
            rendering::AuxDebugView::Velocity,   rendering::AuxDebugView::Emission,
            rendering::AuxDebugView::Ids,        rendering::AuxDebugView::Occlusion,
            rendering::AuxDebugView::Depth,      rendering::AuxDebugView::LinearDepth,
            rendering::AuxDebugView::DepthEdges, rendering::AuxDebugView::ObjectDepth,
        };
        const std::pair<std::uint32_t, std::uint32_t> sizes[] = {{97, 61}, {320, 180}, {64, 64}};
        renderer.setDiagnosticEntity("cube");
        std::size_t rendered = 0;
        for (const rendering::AuxDebugView view : views) {
            renderer.setAuxDebugView(view);
            for (const auto& [w, h] : sizes) {
                INFO("view " << rendering::auxDebugViewName(view) << " at " << w << "x" << h);
                auto image = renderer.renderToImage(scene, time, w, h);
                REQUIRE(image.has_value());
                CHECK(image->width == w);
                CHECK(image->height == h);
                ++rendered;
            }
        }
        CHECK(rendered == std::size(views) * std::size(sizes));
        renderer.setAuxDebugView(rendering::AuxDebugView::None);
        renderer.setDiagnosticEntity("");
    }

    SECTION("the object-depth view follows the culling arm") {
        // The depth diagnostics joined to the pass matrix, which is the last of Phase 7's asks. The
        // selected object's depth is a picture of what the frame *submitted*, so an object the cull
        // dropped must be absent from it -- and must come back when the cull is disarmed. Both
        // directions, because a view that always showed the object would pass the first alone.
        const auto litPixels = [&](const gpu::Image8& image, std::uint32_t w, std::uint32_t h) {
            std::size_t count = 0;
            for (std::uint32_t y = 0; y < h; ++y) {
                for (std::uint32_t x = 0; x < w; ++x) {
                    count += image.pixel(x, y)[2] > 128 ? 1 : 0;
                }
            }
            return count;
        };
        renderer.setDiagnosticEntity("cube");
        renderer.setAuxDebugView(rendering::AuxDebugView::ObjectDepth);
        const std::size_t drawn = litPixels(shoot(), kW, kH);
        INFO(drawn << " pixels of the cube's own depth");
        REQUIRE(drawn > 100);

        cube.cameraCulled = true;
        CHECK(litPixels(shoot(), kW, kH) == 0);

        rendering::SceneRenderer::PassToggles noCulling;
        noCulling.culling = false;
        renderer.setPassToggles(noCulling);
        const std::size_t uncull = litPixels(shoot(), kW, kH);
        INFO(uncull << " pixels once the cull is disarmed");
        CHECK(uncull > 100);

        renderer.setPassToggles(rendering::SceneRenderer::PassToggles{});
        cube.cameraCulled = false;
        renderer.setAuxDebugView(rendering::AuxDebugView::None);
        renderer.setDiagnosticEntity("");
    }

    CHECK(ctx->errorCount() == 0);
}

// ---- Phase 8.3: automatic subsystem bisection ---------------------------------------------------
//
// The progressive matrix answers "at which rung does this appear". Bisection answers the sharper
// question: *which subsystems does the symptom actually need?* Given a predicate over a frame, it
// finds a **minimal** set of arms that must stay on for the predicate to hold -- minimal in the
// delta-debugging sense, that turning any one of them off makes the symptom go away.
//
// It is written here rather than in the renderer because it is a search over renders, and the thing
// that owns a render loop is the thing that should own it. The search is over `passArms()`, so an
// arm added to the renderer is automatically in the search; an arm the search could not see would
// silently clear the subsystem behind it, which is the failure mode this whole plan is about.
namespace {

// The arms that must remain on. `reproduces(toggles)` renders and says whether the symptom is there.
std::vector<std::string> bisectArms(const std::function<bool(const rendering::SceneRenderer::PassToggles&)>& reproduces) {
    std::vector<std::string> candidates;
    for (const auto& arm : rendering::SceneRenderer::passArms()) {
        candidates.emplace_back(arm.name);
    }
    const auto togglesWith = [](const std::vector<std::string>& on) {
        rendering::SceneRenderer::PassToggles toggles;
        for (const auto& arm : rendering::SceneRenderer::passArms()) {
            REQUIRE(rendering::SceneRenderer::setPassArm(toggles, arm.name, false));
        }
        for (const std::string& name : on) {
            REQUIRE(rendering::SceneRenderer::setPassArm(toggles, name, true));
        }
        return toggles;
    };

    // With everything on the symptom must be there, or there is nothing to bisect. With everything
    // off it may still be there -- that is the empty answer, and it is a real one: the symptom is in
    // the part of the frame no arm removes.
    if (!reproduces(togglesWith(candidates))) {
        return {};
    }
    if (reproduces(togglesWith({}))) {
        return {};
    }

    // Greedy minimisation: drop one arm at a time and keep the drop when the symptom survives.
    // Linear in the number of arms times a render, which for eleven arms is cheaper than a proper
    // ddmin and gives the same one-minimal answer.
    std::vector<std::string> needed = candidates;
    for (const auto& arm : rendering::SceneRenderer::passArms()) {
        std::vector<std::string> without;
        for (const std::string& name : needed) {
            if (name != arm.name) {
                without.push_back(name);
            }
        }
        if (without.size() != needed.size() && reproduces(togglesWith(without))) {
            needed = without;
        }
    }
    return needed;
}

} // namespace

TEST_CASE("bisection finds the smallest set of subsystems a symptom needs",
          "[gpu][composition][forensics][bisect]") {
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "qa" / "renderer-qa.scene.json";
    if (!fs::is_regular_file(project)) {
        SKIP("the RendererQA scene is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(project).has_value());
    FixedStepClock clock(60.0);
    constexpr std::uint32_t kW = 160;
    constexpr std::uint32_t kH = 120;
    aimCompositionCamera(engine.params(), glm::vec3(0.0f, 3.0f, 10.0f), glm::vec3(0.0f, 1.0f, -4.0f));
    const FrameTime time = engine.tick(clock);
    engine.setViewport(kW, kH);
    engine.update(time);

    std::size_t renders = 0;
    const auto frameWith = [&](const rendering::SceneRenderer::PassToggles& toggles) {
        renderer.setPassToggles(toggles);
        renderer.resetTemporalHistory();
        auto image = renderer.renderToImage(engine.scene(), time, kW, kH);
        REQUIRE(image.has_value());
        ++renders;
        return std::move(*image);
    };
    const auto stats = [&] { return renderer.stats(); };

    SECTION("a symptom that is one subsystem is attributed to that subsystem alone") {
        // The symptom: the frame simulated particles. Its true cause is known, so the answer is
        // checkable -- which is the only way to test a search whose output is a claim about cause.
        const std::vector<std::string> needed = bisectArms([&](const auto& toggles) {
            frameWith(toggles);
            return stats().particles.dispatches > 0;
        });
        INFO("bisection over " << renders << " renders returned " << needed.size() << " arms");
        for (const std::string& name : needed) {
            INFO("needed: " << name);
        }
        REQUIRE(needed.size() == 1);
        CHECK(needed.front() == "particles");
    }

    SECTION("a symptom no arm can remove is reported as needing none") {
        // The honest empty answer, and the control for the section above. Opaque geometry has no
        // arm, so a symptom that rests on it survives every arm being off -- and the search must
        // say "none of these" rather than picking whichever arm it happened to test last.
        renders = 0;
        const std::vector<std::string> needed = bisectArms([&](const auto& toggles) {
            frameWith(toggles);
            return stats().drawCalls > 0;
        });
        INFO("bisection over " << renders << " renders returned " << needed.size() << " arms");
        CHECK(needed.empty());
    }

    SECTION("a symptom nothing produces is reported as needing none") {
        // The other end: a predicate that is false even with everything on. The search must not
        // return an arbitrary set for a symptom that does not exist.
        renders = 0;
        const std::vector<std::string> needed = bisectArms([&](const auto& toggles) {
            frameWith(toggles);
            return stats().drawCalls > 100000;
        });
        CHECK(needed.empty());
        CHECK(renders == 1); // it gives up on the first render rather than searching
    }

    SECTION("every arm in an answer is load-bearing") {
        // The property that makes an answer a claim about *cause* rather than a list of what
        // happened to be on. Whatever set comes back for "the frame recorded shadow draws", it must
        // contain the shadow arm, and removing any single member must make the symptom go away.
        // The second half is the real assertion: a search that returned a superset would pass the
        // first half and fail this.
        renders = 0;
        const std::vector<std::string> needed = bisectArms([&](const auto& toggles) {
            frameWith(toggles);
            return stats().shadowDraws > 0;
        });
        INFO("bisection over " << renders << " renders returned " << needed.size() << " arms");
        for (const std::string& name : needed) {
            INFO("needed: " << name);
        }
        REQUIRE(!needed.empty());
        CHECK(std::find(needed.begin(), needed.end(), "shadows") != needed.end());
        for (const std::string& name : needed) {
            rendering::SceneRenderer::PassToggles toggles;
            for (const auto& arm : rendering::SceneRenderer::passArms()) {
                REQUIRE(rendering::SceneRenderer::setPassArm(toggles, arm.name, false));
            }
            for (const std::string& on : needed) {
                if (on != name) {
                    REQUIRE(rendering::SceneRenderer::setPassArm(toggles, on, true));
                }
            }
            frameWith(toggles);
            INFO("without '" << name << "' the symptom should be gone");
            CHECK(stats().shadowDraws == 0);
        }
    }

    renderer.setPassToggles(rendering::SceneRenderer::PassToggles{});
    CHECK(ctx->errorCount() == 0);
}

// ---- The second of the two seek defects Phase 3.4 found -----------------------------------------
//
// Both are "a frame ago" meaning something false after a jump, and both were invisible until
// somebody compared a seeked renderer against a fresh one channel by channel. The particle-pool one
// is asserted where it was found, in `test_resource_lifetime_gpu.cpp`, whose harness controls for
// frame index; the whole-frame version of that comparison written here was withdrawn because it did
// not -- a renderer forty frames along and a fresh one are at different frame indices, so the
// jittered passes differ for a reason that has nothing to do with the seek.
TEST_CASE("a seek reseeds the previous skinning palette", "[gpu][composition][forensics][lifetime][seek]") {
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "characters" / "alien.scene.json";
    if (!fs::is_regular_file(project)) {
        SKIP("the alien scene is not present");
    }
    // The defect at the level it lives at, with no renderer in the way: after a jump, a rig's
    // "previous" palette must be the pose it landed on, not the one it left. Measured in model
    // units, because that is what the vertex stage differences to build a motion vector.
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(project).has_value());
    FixedStepClock clock(60.0);
    engine.setViewport(160, 120);
    for (int i = 0; i < 40; ++i) {
        const FrameTime t = engine.tick(clock);
        engine.update(t);
    }
    scene::Composition* composition = engine.composition();
    REQUIRE(composition != nullptr);
    REQUIRE(!composition->scene().rigs.empty());

    // Before the repair this measured 71.5 on a ~100-unit character. The control is below: ordinary
    // playback must still report motion, or the fix has simply switched motion blur off for rigs.
    engine.seekSeconds(1.0);
    {
        const FrameTime t = engine.tick(clock);
        engine.update(t);
    }
    double worstAcrossSeek = 0.0;
    std::size_t joints = 0;
    for (const scene::SkinnedRig& rig : composition->scene().rigs) {
        REQUIRE(rig.palette.size() == rig.previousPalette.size());
        for (std::size_t j = 0; j < rig.palette.size(); ++j) {
            ++joints;
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r) {
                    worstAcrossSeek = std::max(worstAcrossSeek,
                                               std::fabs(static_cast<double>(rig.palette[j][c][r]) -
                                                         rig.previousPalette[j][c][r]));
                }
            }
        }
    }
    INFO(joints << " joints, worst previous-to-current element across the seek " << worstAcrossSeek);
    REQUIRE(joints > 0);
    CHECK(worstAcrossSeek == 0.0);

    // The control. A rig that is simply playing must still report joint motion, or a test that only
    // checked the line above would be satisfied by a renderer that had stopped animating.
    double worstInPlayback = 0.0;
    for (int i = 0; i < 10; ++i) {
        const FrameTime t = engine.tick(clock);
        engine.update(t);
        for (const scene::SkinnedRig& rig : composition->scene().rigs) {
            for (std::size_t j = 0; j < rig.palette.size(); ++j) {
                for (int c = 0; c < 4; ++c) {
                    for (int r = 0; r < 4; ++r) {
                        worstInPlayback = std::max(worstInPlayback,
                                                   std::fabs(static_cast<double>(rig.palette[j][c][r]) -
                                                             rig.previousPalette[j][c][r]));
                    }
                }
            }
        }
    }
    INFO("worst previous-to-current element during ordinary playback " << worstInPlayback);
    CHECK(worstInPlayback > 0.0);
}

// ---- Phase 4.5: freeze animation, which is not the same control as disabling it -----------------
//
// Two arms that a casual reading would merge. `animation` removes skinning from the frame: the
// palettes are not uploaded at all and every skinned mesh draws from its rest vertices, so the
// character snaps to its bind pose. `animationMotion` holds the palettes already on the GPU, so the
// character stops moving *where it is*. "The character's pose is wrong" and "the character's pose is
// not changing" are different questions, and a scene where only one of the arms changes the picture
// is what says which one you are looking at.
//
// The test is therefore a three-way comparison, not a two-way: bind pose, held pose and live pose
// must all be different pictures. Checking the freeze alone would pass for an arm that had quietly
// become the bind-pose one.
TEST_CASE("freezing animation holds the pose; disabling it takes the pose away",
          "[gpu][composition][forensics][isolation][animation]") {
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "characters" / "alien.scene.json";
    if (!fs::is_regular_file(project)) {
        SKIP("the alien scene is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(project).has_value());
    FixedStepClock clock(60.0);
    constexpr std::uint32_t kW = 160;
    constexpr std::uint32_t kH = 120;
    REQUIRE(engine.play().has_value());

    // The camera is parked, and it has to be. This scene's camera orbits, so with it live *every*
    // frame differs whatever the rig is doing -- the first version of this test passed its
    // "animation is moving" precondition on camera motion alone and then failed the freeze, which is
    // the instrument reporting the wrong subsystem rather than the arm being wrong.
    const auto park = [&] {
        aimCompositionCamera(engine.params(), glm::vec3(0.0f, 1.2f, 3.2f), glm::vec3(0.0f, 0.9f, 0.0f));
    };
    const auto step = [&](const rendering::SceneRenderer::PassToggles& arms) {
        renderer.setPassToggles(arms);
        const FrameTime time = engine.tick(clock);
        engine.setViewport(kW, kH);
        park();
        engine.update(time);
        auto image = renderer.renderToImage(engine.scene(), time, kW, kH);
        REQUIRE(image.has_value());
        return gpu::hashImage(*image);
    };

    // Post and ambient occlusion off, for the reason Phase 6.2 established: the AO buffer is
    // temporally jittered and auto-exposure re-meters on content, so consecutive frames of a
    // *completely static* scene do not hash alike. With them on, "the frozen frame stopped changing"
    // is unmeasurable through a frame hash -- which is how the first version of this test failed,
    // reporting the freeze for a difference the freeze does not own.
    rendering::SceneRenderer::PassToggles live;
    live.ao = false;
    live.post = false;
    rendering::SceneRenderer::PassToggles held = live;
    held.animationMotion = false;
    rendering::SceneRenderer::PassToggles bind = live;
    bind.animation = false;

    // Walk a few frames so the rig is genuinely mid-clip rather than at its first pose.
    for (int i = 0; i < 12; ++i) {
        step(live);
    }
    // Live: consecutive frames differ, or there is no motion here to freeze and the rest is empty.
    const std::uint64_t liveA = step(live);
    const std::uint64_t liveB = step(live);
    REQUIRE(liveA != liveB);

    // Held: the picture stops changing, over several frames rather than one, because a single pair
    // could agree by the clip happening to repeat.
    const std::uint64_t heldFirst = step(held);
    for (int i = 0; i < 6; ++i) {
        INFO("held frame " << i);
        CHECK(step(held) == heldFirst);
    }

    // ...and the rig underneath is still being posed, so the equality above is the *arm* holding the
    // frame and not the animation having stopped in the scene.
    scene::Composition* composition = engine.composition();
    REQUIRE(composition != nullptr);
    REQUIRE(!composition->scene().rigs.empty());
    const std::uint64_t versionWhileHeld = composition->scene().rigs.front().paletteVersion;
    step(held);
    CHECK(composition->scene().rigs.front().paletteVersion > versionWhileHeld);

    // Bind pose: a different picture again. This is what separates the two arms -- if `held` had
    // merely stopped uploading in the way `animation` does, these two would agree.
    const std::uint64_t bindFrame = step(bind);
    CHECK(bindFrame != heldFirst);
    CHECK(bindFrame != liveA);
    const std::uint64_t bindAgain = step(bind);
    CHECK(bindAgain == bindFrame); // the bind pose does not move either

    // Releasing the freeze puts the character back in motion rather than leaving it stuck.
    step(live);
    const std::uint64_t resumedA = step(live);
    const std::uint64_t resumedB = step(live);
    CHECK(resumedA != resumedB);

    renderer.setPassToggles(rendering::SceneRenderer::PassToggles{});
    CHECK(ctx->errorCount() == 0);
}

// ---- Phase 3.3: identifiers that stay the same object ------------------------------------------
//
// The identifier target is what a click resolves against and what the `Ids` view colours by, so the
// property that matters is not that ids *exist* but that a given id keeps naming the same thing
// while the camera moves, the timeline runs and objects come and go from the frame. An id that
// silently re-pointed would give a stable-looking picture and a wrong selection -- the failure this
// numbering already had once, when a click on a scattered tree resolved as an entity index.
//
// The word is checked whole, and both halves are cross-examined against each other: the low sixteen
// bits are the pick id and the high sixteen are the material id, which is one-based. Those two are
// derived from the same entity index by different arithmetic, so requiring them to agree is a real
// check on the packing rather than a restatement of it.
TEST_CASE("an object identifier keeps naming the same object", "[gpu][composition][forensics][ids]") {
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "qa" / "renderer-qa.scene.json";
    if (!fs::is_regular_file(project)) {
        SKIP("the RendererQA scene is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(project).has_value());
    FixedStepClock clock(60.0);
    constexpr std::uint32_t kW = 160;
    constexpr std::uint32_t kH = 120;
    REQUIRE(engine.play().has_value());

    // id -> the entity name it named, the first time it was seen.
    std::map<std::uint32_t, std::string> named;
    std::set<std::uint32_t> everSeen;
    std::size_t framesWithGeometry = 0;

    for (int frame = 0; frame < 24; ++frame) {
        // The camera orbits, so objects enter and leave the frame and the submission order changes
        // with them -- which is exactly the condition under which a slot-derived id would drift.
        const float angle = static_cast<float>(frame) * 0.26f;
        aimCompositionCamera(engine.params(),
                             glm::vec3(std::sin(angle) * 11.0f, 3.0f + std::sin(angle * 0.7f) * 2.0f,
                                       std::cos(angle) * 11.0f),
                             glm::vec3(0.0f, 1.0f, -4.0f));
        const FrameTime time = engine.tick(clock);
        engine.setViewport(kW, kH);
        engine.update(time);
        REQUIRE(renderer.renderToImage(engine.scene(), time, kW, kH).has_value());

        auto words = gpu::readTextureR32Uint(*ctx, renderer.identifierTexture(), kW, kH);
        REQUIRE(words.has_value());
        const scene::Scene& scene = engine.scene();

        std::set<std::uint32_t> thisFrame;
        for (const std::uint32_t word : *words) {
            if (word == 0) {
                continue; // nothing drawn here; entity zero's own id is 0, which is why the whole
                          // word is the emptiness test and not the low half
            }
            thisFrame.insert(word);
        }
        if (!thisFrame.empty()) {
            ++framesWithGeometry;
        }
        for (const std::uint32_t word : thisFrame) {
            everSeen.insert(word);
            const std::uint32_t pick = word & 0xFFFFu;
            const std::uint32_t material = (word >> 16) & 0xFFFFu;
            INFO("frame " << frame << " word " << word);
            // The two halves are derived from the same index by different arithmetic -- the low one
            // through `packPickId`, the high one as `index + 1` -- so requiring them to agree is a
            // check on the packing rather than a restatement of it. Both the entity and the
            // procedural writers use that convention, which is itself worth pinning: three
            // renderers write into this one target and each numbers from zero, which is why the
            // space tag exists at all.
            REQUIRE(material > 0); // one-based, which is what keeps index zero's word non-zero
            const std::uint32_t index = scene::pickIndexOf(pick);
            CHECK(index == material - 1);

            // Resolved through the space tag. Without it these are the same small numbers, and a
            // click on a scattered tree used to select whichever entity shared its index.
            std::string name;
            switch (scene::pickSpaceOf(pick)) {
            case scene::PickSpace::Entity:
                REQUIRE(index < scene.entities.size());
                name = "entity:" + scene.entities[index].name;
                break;
            case scene::PickSpace::Procedural:
                REQUIRE(index < scene.procedurals.size());
                name = "procedural:" + scene.procedurals[index].name;
                break;
            case scene::PickSpace::Sdf:
                REQUIRE(index < scene.sdfs.size());
                name = "sdf:" + std::to_string(index);
                break;
            }
            const auto [it, inserted] = named.emplace(word, name);
            if (!inserted) {
                INFO("id " << word << " named '" << it->second << "' before and '" << name << "' now");
                CHECK(it->second == name);
            }
        }
    }

    INFO(everSeen.size() << " distinct identifiers over " << framesWithGeometry << " frames");
    // Several objects were actually seen, or the loop above asserted nothing. Three is the floor the
    // views test uses for this scene: a floor, an alien and an orb.
    CHECK(framesWithGeometry == 24);
    CHECK(everSeen.size() >= 3);

    // Water is the documented exception and is asserted as one rather than left to be rediscovered:
    // its pipeline masks every scene target but colour and emission, so it writes no identifier at
    // all. Any water entity in this scene is therefore absent from everything above.
    std::size_t waterEntities = 0;
    for (std::size_t i = 0; i < engine.scene().entities.size(); ++i) {
        if (engine.scene().entities[i].style == scene::MeshStyle::Water) {
            ++waterEntities;
            const std::uint32_t pick = scene::packPickId(scene::PickSpace::Entity, i);
            for (const std::uint32_t word : everSeen) {
                CHECK((word & 0xFFFFu) != pick);
            }
        }
    }
    INFO(waterEntities << " water entities, none of which wrote an identifier");

    CHECK(ctx->errorCount() == 0);
}

// ---- Phase 9.2: object ordering is a property of the scene, not of the frame --------------------
//
// The slot an object gets is its submission order, and submission order is what a renderer is free
// to change for its own reasons -- a sort, a cull, a batch. This asserts that it does not: the same
// scene state produces the same assignment, and the assignment is the scene's entity order filtered
// by what is drawable, not an order the renderer invented.
//
// It matters because two other things in this investigation rest on it. The object uniform buffer is
// addressed by slot, so an order that varied between two renders of one state would be a different
// buffer layout for the same frame; and a capture compares objects by *name* precisely because slot
// is not an identity, which is only a safe design if slot is at least stable for a given state.
TEST_CASE("object submission order is a function of the scene", "[gpu][composition][forensics][ordering]") {
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "qa" / "renderer-qa-minimal.scene.json";
    if (!fs::is_regular_file(project)) {
        SKIP("the minimal QA variant is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(project).has_value());
    FixedStepClock clock(60.0);
    constexpr std::uint32_t kW = 128;
    constexpr std::uint32_t kH = 96;
    aimCompositionCamera(engine.params(), glm::vec3(0.0f, 2.5f, 9.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const FrameTime time = engine.tick(clock);
    engine.setViewport(kW, kH);
    engine.update(time);

    // name -> slot, from the renderer's own diagnostic.
    const auto slotsNow = [&] {
        std::map<std::string, std::uint32_t> slots;
        for (const auto& object : renderer.diagnosticFrame().objects) {
            if (object.submitted) {
                slots.emplace(object.name, object.objectSlot);
            }
        }
        return slots;
    };
    const auto draw = [&] {
        REQUIRE(renderer.renderToImage(engine.scene(), time, kW, kH).has_value());
        return slotsNow();
    };

    const std::map<std::string, std::uint32_t> first = draw();
    REQUIRE(first.size() >= 2);

    // The same state, drawn again: identical assignment.
    CHECK(draw() == first);

    // No two objects share a slot, and the slots are a dense range from zero -- the property that
    // makes "slot times stride" a valid address and not merely a number.
    std::set<std::uint32_t> used;
    for (const auto& [name, slot] : first) {
        INFO("'" << name << "' in slot " << slot);
        CHECK(used.insert(slot).second);
    }
    CHECK(*used.begin() == 0);
    CHECK(*used.rbegin() == used.size() - 1);

    // The order follows the *scene*, not the renderer: slots ascend with entity index among the
    // objects that were submitted. Without this the checks above would pass for any fixed
    // permutation the renderer happened to invent and then repeat.
    std::vector<std::pair<std::size_t, std::uint32_t>> byEntity;
    for (const auto& object : renderer.diagnosticFrame().objects) {
        if (object.submitted) {
            byEntity.emplace_back(object.entityIndex, object.objectSlot);
        }
    }
    std::sort(byEntity.begin(), byEntity.end());
    for (std::size_t i = 1; i < byEntity.size(); ++i) {
        INFO("entity " << byEntity[i - 1].first << " -> slot " << byEntity[i - 1].second << ", entity "
                       << byEntity[i].first << " -> slot " << byEntity[i].second);
        CHECK(byEntity[i - 1].second < byEntity[i].second);
    }

    // A reload rebuilds every object; the assignment has to come back the same, or a capture taken
    // before a reload could not be compared with one taken after.
    REQUIRE(engine.loadComposition(project).has_value());
    aimCompositionCamera(engine.params(), glm::vec3(0.0f, 2.5f, 9.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const FrameTime reloaded = engine.tick(clock);
    engine.setViewport(kW, kH);
    engine.update(reloaded);
    REQUIRE(renderer.renderToImage(engine.scene(), reloaded, kW, kH).has_value());
    CHECK(slotsNow() == first);

    // ...and hiding an object closes the gap rather than leaving a hole, which is what says the
    // assignment is a filter over the scene rather than an index into it.
    scene::Scene& mutableScene = engine.composition()->scene();
    const std::string hidden = first.begin()->first;
    for (scene::Entity& entity : mutableScene.entities) {
        if (entity.name == hidden) {
            entity.visible = false;
        }
    }
    REQUIRE(renderer.renderToImage(mutableScene, reloaded, kW, kH).has_value());
    const std::map<std::string, std::uint32_t> without = slotsNow();
    INFO("hid '" << hidden << "'");
    CHECK(without.size() == first.size() - 1);
    std::set<std::uint32_t> stillUsed;
    for (const auto& [name, slot] : without) {
        stillUsed.insert(slot);
    }
    CHECK(*stillUsed.begin() == 0);
    CHECK(*stillUsed.rbegin() == stillUsed.size() - 1);

    CHECK(ctx->errorCount() == 0);
}

// ---- The pre-upgrade baseline ------------------------------------------------------------------
//
// Committed captures of what the renderer *derives* from each canonical scene: every object's world
// transform and matrix, its bounds, its mesh and material fingerprint, its frustum margins, its GPU
// slot and buffer offset, the camera, and the numbers the projection was built from.
//
// **This exists because it cannot be made later.** A renderer upgrade changes pixels by design, so
// an image baseline would be thrown away on the first day and tell you nothing. What must *not*
// change is the state the renderer reads out of a scene: an object is in the same place, the same
// size, made of the same things, and either drawn or not for the same reason. That is the contract a
// new renderer has to meet, and the only moment it can be recorded is before the old one is gone.
//
// `compareSnapshots` reports differences as sentences naming the object and the field, so a failure
// here is a work item rather than a hash mismatch somebody has to bisect.
//
// To adopt a deliberate change: run with `AVGEN_UPDATE_BASELINES=1`, then read the diff in `git`.
// The diff is the point -- it is the change, stated in JSON, in a review.
TEST_CASE("the canonical scenes derive the state the baselines recorded",
          "[gpu][composition][forensics][baseline]") {
    struct Subject {
        const char* scene;
        glm::vec3 eye;
        glm::vec3 aim;
    };
    // Terrain is deliberately absent: `SYM-TERRAIN-1` means that scene does not render the same
    // frame twice, and a baseline whose subject is unstable is a baseline that teaches people to
    // ignore failures.
    const Subject subjects[] = {
        {"renderer-qa-minimal.scene.json", {0.0f, 2.5f, 9.0f}, {0.0f, 1.0f, 0.0f}},
        {"renderer-qa.scene.json", {0.0f, 3.0f, 12.0f}, {0.0f, 1.0f, -4.0f}},
        {"renderer-qa-transparency.scene.json", {0.0f, 2.0f, 8.0f}, {0.0f, 1.0f, 0.0f}},
    };
    const bool updating = std::getenv("AVGEN_UPDATE_BASELINES") != nullptr;
    std::size_t written = 0;
    const fs::path dir = fs::path(AVGEN_SOURCE_DIR) / "examples" / "qa" / "baselines";

    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    std::size_t compared = 0;

    for (const Subject& subject : subjects) {
        const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "qa" / subject.scene;
        if (!fs::is_regular_file(project)) {
            continue;
        }
        INFO("scene: " << subject.scene);
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadComposition(project).has_value());

        // Fixed everything: second, size, camera, arms. A baseline whose inputs drift is a baseline
        // that reports the drift instead of the renderer.
        constexpr std::uint32_t kW = 192;
        constexpr std::uint32_t kH = 120;
        constexpr double kSecond = 1.0;
        engine.seekSeconds(kSecond);
        FixedStepClock clock(60.0);
        clock.restartAt(kSecond);
        const FrameTime time = engine.tick(clock);
        aimCompositionCamera(engine.params(), subject.eye, subject.aim);
        engine.setViewport(kW, kH);
        engine.update(time);
        REQUIRE(renderer.renderToImage(engine.scene(), time, kW, kH).has_value());

        rendering::FrameSnapshot now;
        now.frame = renderer.diagnosticFrame();
        now.toggles = renderer.passToggles();
        now.scene = subject.scene;
        now.note = "pre-upgrade baseline: the state the renderer derives, not the pixels it draws";
        REQUIRE(now.frame.objects.size() >= 2);

        std::string stem = subject.scene;
        stem = stem.substr(0, stem.find(".scene.json"));
        const fs::path file = dir / (stem + ".snapshot.json");

        if (updating || !fs::is_regular_file(file)) {
            fs::create_directories(dir);
            REQUIRE(rendering::writeSnapshot(now, file).has_value());
            ++written;
            WARN("wrote baseline " << file.filename().string()
                                   << " -- review the diff in git before committing it");
            continue;
        }

        const auto baseline = rendering::readSnapshot(file);
        INFO((baseline ? std::string() : baseline.error().message));
        REQUIRE(baseline.has_value());
        // Counters excluded: the rig palette version is monotonic, so two arrivals at the same
        // second legitimately disagree about it. That is the trap Phase 9.2 fell into, and a
        // baseline is exactly where it would be fallen into again.
        const std::vector<std::string> differences = rendering::compareSnapshots(*baseline, now);
        for (const std::string& line : differences) {
            INFO(line);
        }
        CHECK(differences.empty());
        ++compared;
    }

    // A run that had to create the baselines has nothing to compare, and that is not a failure --
    // it is the first run on a fresh checkout, or a deliberate update. Only a run that found
    // baselines is required to have used them.
    INFO(compared << " scenes compared against a committed baseline, " << written << " written");
    if (!updating && written == 0) {
        CHECK(compared >= 2);
    }
    CHECK(ctx->errorCount() == 0);
}
