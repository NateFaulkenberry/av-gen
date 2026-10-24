// ADR-1641: the aurora's ribbons -- folded sheets overhead, marched as horizontal planes -- against the curtains.
//
//   - off (the default), the ribbon settings do nothing: any values give the same sky, pixel for pixel;
//   - on, the sky is a different one, with structure: bright sheets and dark sky between them;
//   - a still is deterministic (the plane jitter is a hash of the view direction, not a random draw), and a
//     thirtieth of a second later only a sliver of the sky has changed (no flicker);
//   - hidden: the cost at 1080p against the curtains and against no aurora.
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "world/atmospherics.hpp"
#include "world/effects/effect_instance.hpp"

#include <glm/trigonometric.hpp>

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 480;
constexpr std::uint32_t kHeight = 300;

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

// A dark sky over a low ridge, the camera tilted up so most of the frame is sky.
scene::Scene skyScene() {
    scene::Scene scene;
    const scene::MeshId ridge = scene.addMesh(scene::makeCube(1.0f));
    scene::Entity& hill = scene.addEntity("ridge", ridge);
    hill.transform.position = glm::vec3(0.0f, -18.0f, -90.0f);
    hill.transform.scale = glm::vec3(400.0f, 30.0f, 60.0f);
    hill.material.baseColor = glm::vec4(0.05f, 0.06f, 0.08f, 1.0f);
    scene.camera.position = glm::vec3(0.0f, 6.0f, 40.0f);
    scene.camera.target = glm::vec3(0.0f, 60.0f, -120.0f);
    scene.camera.fovYRadians = glm::radians(60.0f);
    scene.environment.backgroundColor = glm::vec3(0.004f, 0.006f, 0.02f);
    scene.environment.showSkybox = false;
    return scene;
}

world::EffectInstance aurora(bool ribbons) {
    world::EffectInstance e = world::glowmereAurora("sky");
    e.aurora.shape.anchor = world::SkyAnchor::World;
    e.aurora.shape.anchorPosition = glm::vec3(0.0f);
    e.aurora.shape.radius = 900.0f;
    e.aurora.shape.baseHeight = -60.0f;
    e.aurora.shape.curtainHeight = 700.0f;
    e.aurora.shape.ribbons = ribbons;
    e.aurora.shape.ribbonAltitude = 300.0f;
    e.aurora.shape.ribbonScale = 900.0f;
    e.aurora.shape.ribbonDistance = 8000.0f;
    e.aurora.appearance.intensity = 4.0f;
    e.aurora.audio.sensitivity = 0.0f;
    e.timing.fadeIn = 0.0;
    e.ground.mode = world::GroundGlow::Off;
    return e;
}

world::AtmosphericFrame frameOf(const world::EffectInstance& e, double seconds) {
    const std::array<world::EffectInstance, 1> set{e};
    world::EffectContext ctx;
    ctx.seconds = seconds;
    world::AtmosphericFrame frame;
    world::buildAtmosphericFrame(set, ctx, frame);
    return frame;
}

struct Harness {
    std::unique_ptr<gpu::Context> ctx = makeContext();
    gpu::ShaderLibrary shaders{*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)}};
    rendering::SceneRenderer renderer{*ctx, shaders};
    scene::Scene scene = skyScene();
    Harness() { REQUIRE(renderer.init().has_value()); }
    gpu::Image8 render(const world::EffectInstance& e, double seconds = 3.0) {
        scene.atmospherics = frameOf(e, seconds);
        const FrameTime time{.renderTime = seconds, .deltaTime = 1.0 / 60.0, .frameIndex = 180};
        auto image = renderer.renderToImage(scene, time, kWidth, kHeight);
        REQUIRE(image.has_value());
        return std::move(*image);
    }
};

int luma(const std::uint8_t* p) { return (p[0] * 54 + p[1] * 183 + p[2] * 19) >> 8; }

// The sky's upper two thirds: how many pixels are lit (an aurora) and how many are still the bare sky (within a
// few levels of `background`, the empty sky's own luma).
struct SkyCover {
    int lit = 0;
    int dark = 0;
    int total = 0;
};
SkyCover cover(const gpu::Image8& img, int background) {
    SkyCover c;
    for (std::uint32_t y = 0; y < img.height * 2 / 3; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const int l = luma(img.pixel(x, y));
            c.lit += l > background + 60 ? 1 : 0;
            c.dark += l <= background + 6 ? 1 : 0;
            ++c.total;
        }
    }
    return c;
}

std::size_t changed(const gpu::Image8& a, const gpu::Image8& b, int threshold) {
    std::size_t n = 0;
    for (std::size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        for (std::size_t c = 0; c < 3; ++c) {
            if (std::abs(static_cast<int>(a.rgba[i + c]) - static_cast<int>(b.rgba[i + c])) > threshold) {
                ++n;
                break;
            }
        }
    }
    return n;
}

} // namespace

TEST_CASE("the aurora's ribbon settings do nothing while ribbons are off (ADR-1641)",
          "[gpu][atmospherics][aurora][adr1641]") {
    Harness h;
    const gpu::Image8 curtains = h.render(aurora(false));
    world::EffectInstance tweaked = aurora(false);
    tweaked.aurora.shape.ribbonAltitude = 5000.0f;
    tweaked.aurora.shape.ribbonScale = 333.0f;
    tweaked.aurora.shape.ribbonWidth = 0.4f;
    tweaked.aurora.shape.ribbonSwirl = 5.0f;
    tweaked.aurora.shape.ribbonEdge = 2.0f;
    tweaked.aurora.shape.ribbonSteps = 17.0f;
    tweaked.aurora.shape.ribbonDistance = 1234.0f;
    CHECK(gpu::hashImage(h.render(tweaked)) == gpu::hashImage(curtains));
    const int background = luma(curtains.pixel(kWidth / 2, 2)); // the top of the frame: sky above the curtains
    const SkyCover c = cover(curtains, background);
    CHECK(c.lit > 0); // the control: there is an aurora in this sky to change
    CHECK(h.ctx->errorCount() == 0);
}

TEST_CASE("the aurora's ribbons draw sheets with dark sky between them, steadily (ADR-1641)",
          "[gpu][atmospherics][aurora][adr1641]") {
    Harness h;
    const gpu::Image8 curtains = h.render(aurora(false));
    const gpu::Image8 ribbons = h.render(aurora(true));
    if (const char* dir = std::getenv("AVGEN_EFFECT_DUMP"); dir != nullptr && *dir != '\0') {
        fs::create_directories(dir);
        REQUIRE(gpu::writePpm(curtains, fs::path(dir) / "ribbons-off.ppm").has_value());
        REQUIRE(gpu::writePpm(ribbons, fs::path(dir) / "ribbons-on.ppm").has_value());
    }
    const int background = luma(curtains.pixel(kWidth / 2, 2)); // the top of the frame: sky above the curtains
    const SkyCover c = cover(ribbons, background);
    INFO("ribbons: " << c.lit << " lit, " << c.dark << " dark of " << c.total << " sky pixels");
    CHECK(changed(curtains, ribbons, 8) > kWidth * kHeight / 10); // a different sky
    CHECK(c.lit > c.total / 50);                                  // sheets...
    CHECK(c.dark > c.total / 10);                                 // ...and the sky between them

    // A still is a function of its time: the jitter is a hash of the view direction.
    CHECK(gpu::hashImage(h.render(aurora(true))) == gpu::hashImage(ribbons));
    // A thirtieth of a second on, the drift has moved the sheets a little and nothing has flickered.
    const gpu::Image8 next = h.render(aurora(true), 3.0 + 1.0 / 30.0);
    const std::size_t moved = changed(ribbons, next, 24);
    INFO("pixels changed by more than 24/255 in 1/30 s: " << moved);
    CHECK(moved < kWidth * kHeight / 50);
    CHECK(h.ctx->errorCount() == 0);
}

TEST_CASE("the aurora's ribbon coverage fills the gaps between the sheets (ADR-1660)",
          "[gpu][atmospherics][aurora][adr1660]") {
    Harness h;
    const gpu::Image8 curtains = h.render(aurora(false));
    const int background = luma(curtains.pixel(kWidth / 2, 2));
    const gpu::Image8 gaps = h.render(aurora(true));
    world::EffectInstance zero = aurora(true);
    zero.aurora.shape.ribbonCoverage = 0.0f;
    // 0 is ADR-1641's sky exactly
    CHECK(gpu::hashImage(h.render(zero)) == gpu::hashImage(gaps));
    world::EffectInstance full = aurora(true);
    full.aurora.shape.ribbonCoverage = 1.0f;
    const gpu::Image8 covered = h.render(full);
    const SkyCover a = cover(gaps, background);
    const SkyCover b = cover(covered, background);
    INFO("coverage 0: " << a.lit << " lit, " << a.dark << " bare; coverage 1: " << b.lit << " lit, " << b.dark
                        << " bare, of " << a.total);
    CHECK(b.dark < a.dark - a.dark / 4); // the bare sky between the ribbons shrinks
    CHECK(b.lit >= a.lit);               // and nothing that was lit goes out
    CHECK(h.ctx->errorCount() == 0);
}

// Hidden ([.]): the ribbons' cost at 1080p (honest timestamps, ADR-1600): no aurora, the curtains (three shells),
// ribbons at 24 and at 48 planes, interleaved over 4 reps of 100 frames; the GPU frame's median.
TEST_CASE("aurora ribbons: cost (measure)", "[.][gpu][atmospherics][aurora][measure][adr1641]") {
    Harness h;
    if (!h.renderer.timeline().available()) {
        SKIP("timestamp queries unavailable");
    }
    const auto measure = [&](const world::AtmosphericFrame& sky) {
        h.scene.atmospherics = sky;
        std::vector<double> gpu;
        for (int i = 0; i < 120; ++i) {
            FrameTime t{};
            t.renderTime = 3.0 + i / 60.0;
            REQUIRE(h.renderer.renderFrame(h.scene, t, 1920, 1080).has_value());
            if (i >= 20) {
                gpu.push_back(h.renderer.stats().gpuFrameMs);
            }
        }
        std::sort(gpu.begin(), gpu.end());
        return gpu[gpu.size() / 2];
    };
    world::EffectInstance r24 = aurora(true);
    r24.aurora.shape.ribbonSteps = 24.0f;
    world::EffectInstance r48 = aurora(true);
    r48.aurora.shape.ribbonSteps = 48.0f;
    const std::array<world::AtmosphericFrame, 4> arms{world::AtmosphericFrame{}, frameOf(aurora(false), 3.0),
                                                      frameOf(r24, 3.0), frameOf(r48, 3.0)};
    for (const auto& a : arms) {
        static_cast<void>(measure(a)); // the clocks up and the pipelines built
    }
    std::array<std::vector<double>, 4> ms;
    for (int rep = 0; rep < 4; ++rep) {
        for (std::size_t k = 0; k < arms.size(); ++k) {
            ms[k].push_back(measure(arms[k]));
        }
    }
    const auto list = [](const std::vector<double>& v) {
        std::string out;
        for (const double x : v) {
            out += fmt::format(" {:.3f}", x);
        }
        return out;
    };
    WARN("1080p, a mostly-sky frame, GPU frame median (ms), 4 interleaved reps: no aurora"
         << list(ms[0]) << " | curtains (3 shells)" << list(ms[1]) << " | ribbons, 24 planes" << list(ms[2])
         << " | ribbons, 48 planes" << list(ms[3]));
    CHECK(ms[3][0] > 0.0);
}
