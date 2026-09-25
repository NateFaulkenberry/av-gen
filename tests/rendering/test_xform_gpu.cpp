// XFORM on pixels (Effect Library Wave 2). The CPU half is tests/unit/test_xform.cpp; what only a GPU
// can answer:
//   * the velocity target (from `prevModel`, which the renderer captures from the flattened entities)
//     carries the offset's motion -- so motion blur and temporal reprojection see a bob as motion;
//   * with no live XFORM instance the frame is byte-identical, and a frame after one was removed is
//     byte-identical to a frame that never had it.
// With AVGEN_EFFECT_DUMP=<dir> the arms are written as PNGs for a person to look at.

#include "app/engine.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"
#include "scene/scene.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 256;
constexpr std::uint32_t kHeight = 160;

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

void dump(const gpu::Image8& image, const std::string& name) {
    const char* dir = std::getenv("AVGEN_EFFECT_DUMP");
    if (dir == nullptr || dir[0] == '\0') {
        return;
    }
    static_cast<void>(assets::writePng(fs::path(dir) / ("xform-" + name + ".png"), image.width, image.height, image.rgba));
}

// A craft with a lamp hanging under it, and the engine's own flatten producing the scene.
struct Rig {
    app::Engine engine{app::EngineMode::Offline};
    explicit Rig(bool floating, bool enabled = true) {
        REQUIRE(engine.setCompositionJson(nlohmann::json::parse(R"({ "format": "avgen-scene", "version": 1,
            "name": "xform-gpu", "nodes": [
              { "kind": "orb", "name": "craft", "position": [0, 5, 0] },
              { "kind": "orb", "name": "lamp", "parent": "craft", "position": [1.6, -1.2, 0], "scale": [0.4, 0.4, 0.4] } ] })"))
                    .has_value());
        if (floating) {
            world::EffectInstance e = world::makeEffect(world::EffectKind::Float, "Float");
            e.id.clear();
            e.owner = world::EffectOwner::entity("craft");
            e.enabled = enabled;
            e.values.setFloat("float/height", 1.2f);
            e.values.setFloat("float/period", 2.0f);
            e.values.setFloat("float/tilt", 8.0f);
            std::vector<world::EffectInstance> list;
            REQUIRE(world::insertEffect(list, std::move(e)).has_value());
            REQUIRE(engine.setEffects(list).has_value());
        }
        engine.setViewport(kWidth, kHeight);
    }
    // The scene at `seconds`, stepped at 60 Hz from zero, seen by a fixed camera.
    scene::Scene at(double seconds) {
        const auto frames = static_cast<long long>(seconds * 60.0 + 0.5);
        for (long long f = next; f <= frames; ++f) {
            engine.update(FrameTime{static_cast<double>(f) / 60.0, f == 0 ? 0.0 : 1.0 / 60.0,
                                    static_cast<std::uint64_t>(f)});
        }
        next = frames + 1;
        scene::Scene s = engine.scene();
        s.camera.position = glm::vec3(0.0f, 5.0f, 11.0f);
        s.camera.target = glm::vec3(0.0f, 4.5f, 0.0f);
        s.camera.fovYRadians = glm::radians(50.0f);
        s.camera.lens.useExplicitFov = true;
        s.camera.nearPlane = 0.1f;
        s.camera.farPlane = 200.0f;
        return s;
    }
    long long next = 0;
};

// The velocity view draws `velocity * scale` around a neutral grey (0.5): a pixel that moved is one
// whose red or green has left 128.
std::size_t movingPixels(const gpu::Image8& img, int threshold) {
    std::size_t n = 0;
    for (std::size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
        const int off = std::abs(img.rgba[i] - 128) + std::abs(img.rgba[i + 1] - 128);
        n += off > threshold ? 1u : 0u;
    }
    return n;
}

bool sameImage(const gpu::Image8& a, const gpu::Image8& b) { return a.rgba == b.rgba; }

} // namespace

TEST_CASE("XFORM: the velocity target carries the offset's motion (prevModel includes it)",
          "[gpu][effects][xform]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    // Two consecutive frames through the velocity view: the second frame's prevModel is the first's
    // model, so what the target shows is how far each object moved between the two. ONE Scene object
    // for both: the renderer treats a different Scene address as a scene swap and drops its history.
    const auto velocity = [&](Rig& rig) {
        scene::Scene s = rig.at(1.0);
        renderer.setAuxDebugView(rendering::AuxDebugView::None);
        renderer.resetTemporalHistory();
        FrameTime t{};
        t.renderTime = 1.0;
        REQUIRE(renderer.renderToImage(s, t, kWidth, kHeight).has_value());
        s = rig.at(1.0 + 1.0 / 60.0);
        renderer.setAuxDebugView(rendering::AuxDebugView::Velocity);
        // Amplified: one frame of a gentle bob is well under a pixel, which the default scale (40)
        // draws within two grey levels of neutral.
        renderer.setAuxDebugScale(400.0f);
        t.renderTime = 1.0 + 1.0 / 60.0;
        auto img = renderer.renderToImage(s, t, kWidth, kHeight);
        REQUIRE(img.has_value());
        renderer.setAuxDebugView(rendering::AuxDebugView::None);
        renderer.setAuxDebugScale(0.0f);
        return std::move(*img);
    };
    Rig still(false);
    Rig floating(true);
    const gpu::Image8 v0 = velocity(still);
    const gpu::Image8 v1 = velocity(floating);
    dump(v0, "velocity-off");
    dump(v1, "velocity-on");
    const std::size_t moving0 = movingPixels(v0, 16);
    const std::size_t moving1 = movingPixels(v1, 16);
    INFO("pixels with visible velocity: still " << moving0 << ", floating " << moving1);
    CHECK(moving0 < 20);    // the control: nothing moves, the camera is fixed
    CHECK(moving1 > 400);   // the craft and its lamp carry the bob into the velocity target
}

TEST_CASE("XFORM: with no live instance the frame is byte-identical, and nothing lingers after one",
          "[gpu][effects][xform][gate]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    const auto render = [&](const scene::Scene& s, double seconds) {
        FrameTime t{};
        t.renderTime = seconds;
        renderer.resetTemporalHistory();
        REQUIRE(renderer.renderToImage(s, t, kWidth, kHeight).has_value());
        auto img = renderer.renderToImage(s, t, kWidth, kHeight);
        REQUIRE(img.has_value());
        return std::move(*img);
    };
    constexpr double kAt = 1.5;
    Rig none(false);
    Rig disabled(true, false);
    Rig floating(true);
    const gpu::Image8 off = render(none.at(kAt), kAt);
    const gpu::Image8 dis = render(disabled.at(kAt), kAt);
    const gpu::Image8 on = render(floating.at(kAt), kAt);
    dump(off, "gate-none");
    dump(dis, "gate-disabled");
    dump(on, "gate-float");
    CHECK(sameImage(off, dis));
    CHECK_FALSE(sameImage(off, on)); // the control: the Float is visible

    // Remove the instance from the floating rig: the next frame is the never-floated frame.
    REQUIRE(floating.engine.setEffects({}).has_value());
    const gpu::Image8 after = render(floating.at(kAt + 1.0), kAt + 1.0);
    const gpu::Image8 reference = render(none.at(kAt + 1.0), kAt + 1.0);
    CHECK(sameImage(after, reference));
}
