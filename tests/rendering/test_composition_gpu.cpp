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

    struct Motion {
        const char* name;
        int frames;
        void (*place)(scene::Camera&, float);
    };
    const Motion motions[] = {
        {"translation", 60,
         [](scene::Camera& c, float t) {
             c.position = {-14.0f + 28.0f * t, 4.0f, 8.0f};
             c.target = c.position + glm::vec3(0.0f, -0.15f, -1.0f);
         }},
        {"rotation", 60,
         [](scene::Camera& c, float t) {
             const float a = t * 2.0f * 3.14159265f;
             c.position = {0.0f, 3.0f, 2.0f};
             c.target = c.position + glm::vec3(std::sin(a), -0.1f, -std::cos(a));
         }},
        {"dolly", 60,
         [](scene::Camera& c, float t) {
             c.position = {-5.0f, 2.0f, 20.0f - 24.0f * t};
             c.target = {-5.0f, 1.0f, -4.0f};
         }},
        {"through", 60,
         [](scene::Camera& c, float t) {
             c.position = {-5.0f, 1.0f, 6.0f - 20.0f * t};
             c.target = c.position + glm::vec3(0.0f, 0.0f, -1.0f);
         }},
        {"orbit", 80,
         [](scene::Camera& c, float t) {
             const float a = t * 2.0f * 3.14159265f;
             c.position = glm::vec3(-5.0f, 1.0f, -4.0f) +
                          glm::vec3(16.0f * std::sin(a), 6.0f, 16.0f * std::cos(a));
             c.target = {-5.0f, 1.0f, -4.0f};
         }},
    };

    std::uint64_t frame = 1;
    std::size_t checks = 0;
    for (const Motion& motion : motions) {
        INFO("motion: " << motion.name);
        for (int i = 0; i < motion.frames; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(motion.frames - 1);
            motion.place(composition->scene().camera, t);
            step(frame, static_cast<double>(frame) / 60.0);
            ++frame;

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
