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
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"
#include "scene/composition.hpp"
#include "support/temp_dir.hpp"

#include <fmt/format.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <array>
#include <cmath>
#include <filesystem>
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

TEST_CASE("a hundred text layers stay one pass and a handful of draws",
          "[gpu][composition][performance]") {
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
    for (int i = 0; i < kFrames; ++i) {
        // Standing next to the walker, because an entity beyond its `cullDistance` from the view
        // gets no update at all.
        aimCompositionCamera(engine.params(), glm::vec3(150.0f, 30.0f, 20.0f),
                             glm::vec3(165.0f, 23.0f, 20.0f));
        FixedStepClock clock(60.0);
        clock.restartAt(static_cast<double>(i) / 60.0);
        const FrameTime time = engine.tick(clock);
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
    // `SYM-ENTITY-1`, pinned rather than asserted away.
    //
    // Nothing walked. Thirty seconds of playback -- with the project's audio loaded, the transport
    // playing and the camera fifteen metres from the walker -- leaves Glowmere's `wanderer` at
    // travel 0 and speed 0, activity Idle, while a *seek* to the same second puts it tens of metres
    // away at exactly the `explore` behaviour's authored 5 m/s.
    //
    // Three hypotheses eliminated on the way: it is not the `cullDistance` band (the camera is well
    // inside `fullDetailDistance`), not a missing track (the project loads one, and the first
    // behaviour keys on `music.impact`), and not a stopped transport (the signals it reads come from
    // the analysis at the transport position, and it is playing).
    //
    // The test asserts the disagreement it measured rather than the behaviour it wants, because a
    // test that demanded walking would fail for a reason nobody has established yet. When the cause
    // is found, this is the case to invert.
    INFO("playback: furthest walked " << mostTravelled << " m, largest height change "
                                      << mostHeightChange << " m. A seek to 30 s: " << seekedTravel
                                      << " m at " << seekedSpeed << " m/s.");
    CHECK(seekedTravel > 5.0f);           // the seek path does simulate
    CHECK(seekedSpeed > 1.0f);
    CHECK(mostTravelled < 0.01f);         // ...and the per-frame path does not. SYM-ENTITY-1.
    CHECK(mostHeightChange < 0.01f);
}
