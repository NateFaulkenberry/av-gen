// ADR-1652 and ADR-1651: the aurora ribbons' travelling pulse and the live ladder's lever on their planes, the GPU half.
//
//   - pulse 0 (with every other pulse setting off its default) is byte-identical to a renderer whose shader has the
//     pulse blocks removed: the ribbons exactly as before;
//   - on, the ribbons brighten where the front passes, and the front climbs: as `pulse` falls (front = 1 - pulse) the
//     light it adds moves up the frame;
//   - `auroraStepScale` thins the ribbons' planes (a different picture at 0.5), leaves a curtain aurora byte-identical,
//     and the default (offline) settings are the authored planes;
//   - hidden: the pulse's cost and the step scale's saving at 1080p.
#include "support/temp_dir.hpp"
#include "support/water_shader_variant.hpp"

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
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 480;
constexpr std::uint32_t kHeight = 300;
constexpr const char* kPulseBegin = "---- ribbon pulse (ADR-1652) begin ----";
constexpr const char* kPulseEnd = "---- ribbon pulse (ADR-1652) end ----";

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

// A dark sky over a low ridge, the camera tilted up so most of the frame is sky (test_aurora_ribbons_gpu's).
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

std::string readShader(const char* name) {
    std::ifstream in(fs::path(AVGEN_SHADER_SOURCE_DIR) / name);
    REQUIRE(in.good());
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

fs::path writeShaderVariant(const std::string& dirName, const char* file, const std::string& source) {
    const fs::path dir = testsupport::processTempDir() / dirName;
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::ofstream out(dir / file, std::ios::trunc);
    out << source;
    out.close();
    REQUIRE(out.good());
    return dir;
}

struct Harness {
    std::unique_ptr<gpu::Context> ctx = makeContext();
    std::unique_ptr<gpu::ShaderLibrary> shaders;
    std::unique_ptr<rendering::SceneRenderer> renderer;
    scene::Scene scene = skyScene();
    explicit Harness(std::vector<fs::path> dirs = {}) {
        dirs.emplace_back(AVGEN_SHADER_SOURCE_DIR);
        shaders = std::make_unique<gpu::ShaderLibrary>(*ctx, dirs);
        renderer = std::make_unique<rendering::SceneRenderer>(*ctx, *shaders);
        REQUIRE(renderer->init().has_value());
    }
    gpu::ImageF render(const world::EffectInstance& e, double seconds = 3.0) {
        scene.atmospherics = frameOf(e, seconds);
        const FrameTime time{.renderTime = seconds, .deltaTime = 1.0 / 60.0, .frameIndex = 180};
        renderer->resetTemporalHistory();
        auto image = renderer->renderToImageFloat(scene, time, kWidth, kHeight);
        REQUIRE(image.has_value());
        return std::move(*image);
    }
    void stepScale(float s) {
        rendering::QualitySettings q = renderer->qualitySettings();
        q.auroraStepScale = s;
        renderer->setQualitySettings(q);
    }
};

double lumaAt(const gpu::ImageF& img, std::size_t i) {
    return 0.2126 * img.rgba[i * 4] + 0.7152 * img.rgba[i * 4 + 1] + 0.0722 * img.rgba[i * 4 + 2];
}

// The light `b` adds over `a`: its total, and the row (0 = top) it is centred on.
struct Added {
    double total = 0.0;
    double row = 0.0;
};
Added added(const gpu::ImageF& a, const gpu::ImageF& b) {
    Added out;
    double weighted = 0.0;
    for (std::uint32_t y = 0; y < a.height; ++y) {
        for (std::uint32_t x = 0; x < a.width; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * a.width + x;
            const double d = std::max(lumaAt(b, i) - lumaAt(a, i), 0.0);
            out.total += d;
            weighted += d * y;
        }
    }
    out.row = out.total > 0.0 ? weighted / out.total : 0.0;
    return out;
}

world::EffectInstance pulsed(float pulse) {
    world::EffectInstance e = aurora(true);
    e.aurora.shape.pulse = pulse;
    e.aurora.shape.pulseGain = 3.0f;
    e.aurora.shape.pulseThicken = 1.5f;
    e.aurora.shape.pulseWidth = 0.2f;
    return e;
}

} // namespace

TEST_CASE("with the ribbon pulse at 0 the sky is byte-identical to the shader with no pulse code (ADR-1652)",
          "[gpu][atmospherics][aurora][adr1652][identity]") {
    int blocks = 0;
    const std::string stripped =
        testsupport::stripMarkedBlocks(readShader("atmosphere_fx.wgsl"), kPulseBegin, kPulseEnd, &blocks);
    INFO(blocks << " marked pulse blocks removed");
    REQUIRE(blocks == 3); // the pulsed march, and the two halves of the branch round the call
    REQUIRE(stripped.find("ribbon2") == std::string::npos);
    const fs::path dir = writeShaderVariant("atmos-variant-no-pulse", "atmosphere_fx.wgsl", stripped);
    Harness live;
    Harness none({dir});
    // every pulse setting but the pulse itself off its default
    world::EffectInstance e = pulsed(0.0f);
    const gpu::ImageF a = live.render(e);
    CHECK(gpu::hashImage(a) == gpu::hashImage(none.render(e)));
    // and on the curtains, which never read it
    world::EffectInstance curtains = e;
    curtains.aurora.shape.ribbons = false;
    curtains.aurora.shape.pulse = 0.8f;
    CHECK(gpu::hashImage(live.render(curtains)) == gpu::hashImage(none.render(curtains)));
    // THE CONTROL: on the ribbons, it reaches the picture
    CHECK(gpu::hashImage(live.render(pulsed(0.8f))) != gpu::hashImage(a));
    CHECK(live.ctx->errorCount() == 0);
}

TEST_CASE("the ribbon pulse brightens the ribbons as it passes and climbs as it decays (ADR-1652)",
          "[gpu][atmospherics][aurora][adr1652]") {
    Harness h;
    const gpu::ImageF still = h.render(pulsed(0.0f));
    const Added base = added(gpu::ImageF{still.width, still.height, std::vector<float>(still.rgba.size(), 0.0f)}, still);
    REQUIRE(base.total > 0.0); // the control: there are ribbons to brighten
    const gpu::ImageF strike = h.render(pulsed(1.0f));  // the kick: the front on the lower rims
    const gpu::ImageF mid = h.render(pulsed(0.55f));    // decaying: the front half way up
    const gpu::ImageF late = h.render(pulsed(0.25f));   // most of the way up, and fading
    const Added a1 = added(still, strike);
    const Added a2 = added(still, mid);
    const Added a3 = added(still, late);
    INFO("ribbon light " << base.total << "; the pulse adds " << a1.total << " centred on row " << a1.row << " at 1, "
                         << a2.total << " on row " << a2.row << " at 0.55, " << a3.total << " on row " << a3.row
                         << " at 0.25");
    CHECK(a1.total > 0.15 * base.total); // a visible accent
    CHECK(a3.total < a1.total);          // fading as it climbs
    CHECK(a3.total > 0.0);
    // climbing: the light it adds moves up the frame (smaller rows) as the pulse falls
    CHECK(a2.row < a1.row - 1.0);
    CHECK(a3.row < a2.row - 1.0);
    CHECK(h.ctx->errorCount() == 0);
}

TEST_CASE("the aurora step scale thins the ribbons' planes and nothing else (ADR-1651)",
          "[gpu][atmospherics][aurora][adr1651]") {
    Harness h;
    REQUIRE(h.renderer->qualitySettings().auroraStepScale == 1.0f); // the default is every authored plane
    rendering::QualitySettings arm; // and the A/B arms that set it (`--quality-arm auroraplanes75`)
    REQUIRE(rendering::SceneRenderer::setQualityArm(arm, "auroraplanes75"));
    CHECK(arm.auroraStepScale == 0.75f);
    REQUIRE(rendering::SceneRenderer::setQualityArm(arm, "auroraplaneshalf"));
    CHECK(arm.auroraStepScale == 0.5f);
    const gpu::ImageF full = h.render(aurora(true));
    const gpu::ImageF curtains = h.render(aurora(false));
    h.stepScale(1.0f);
    CHECK(gpu::hashImage(h.render(aurora(true))) == gpu::hashImage(full));
    h.stepScale(0.5f);
    const gpu::ImageF half = h.render(aurora(true));
    CHECK(gpu::hashImage(half) != gpu::hashImage(full)); // fewer planes: a different sampling of the same ribbons
    CHECK(gpu::hashImage(h.render(aurora(false))) == gpu::hashImage(curtains)); // the curtains: untouched
    // the same ribbons, only sampled more coarsely: the total light within a few percent
    const auto total = [](const gpu::ImageF& img) {
        double t = 0.0;
        for (std::size_t i = 0; i < static_cast<std::size_t>(img.width) * img.height; ++i) {
            t += lumaAt(img, i);
        }
        return t;
    };
    INFO("ribbon light at 48 planes " << total(full) << ", at 24 " << total(half));
    CHECK(std::abs(total(half) - total(full)) < 0.1 * total(full));
    // exactly as if the scene had authored 24 planes
    h.stepScale(1.0f);
    world::EffectInstance authored24 = aurora(true);
    authored24.aurora.shape.ribbonSteps = 24.0f;
    CHECK(gpu::hashImage(h.render(authored24)) == gpu::hashImage(half));
    CHECK(h.ctx->errorCount() == 0);
}

// Hidden ([.]): the pulse's cost and the step scale's saving at 1080p (honest timestamps, ADR-1600), mostly-sky frame,
// ribbons at 48 planes: pulse 0, pulse 0.7, then step scale 0.75 and 0.5 (pulse 0); interleaved over 8 reps of 100
// frames; the scene label's median (the sky is drawn in the scene pass).
TEST_CASE("aurora ribbon pulse and step scale: cost (measure)", "[.][gpu][atmospherics][aurora][measure][adr1652]") {
    Harness h;
    if (!h.renderer->timeline().available()) {
        SKIP("timestamp queries unavailable");
    }
    world::EffectInstance r48 = aurora(true);
    r48.aurora.shape.ribbonSteps = 48.0f;
    world::EffectInstance r48p = pulsed(0.7f);
    r48p.aurora.shape.ribbonSteps = 48.0f;
    struct Arm {
        world::AtmosphericFrame sky;
        float scale;
    };
    const std::array<Arm, 4> arms{Arm{frameOf(r48, 3.0), 1.0f}, Arm{frameOf(r48p, 3.0), 1.0f},
                                  Arm{frameOf(r48, 3.0), 0.75f}, Arm{frameOf(r48, 3.0), 0.5f}};
    const auto measure = [&](const Arm& arm) {
        h.stepScale(arm.scale);
        h.scene.atmospherics = arm.sky;
        std::vector<double> gpu;
        for (int i = 0; i < 120; ++i) {
            FrameTime t{};
            t.renderTime = 3.0 + i / 60.0;
            REQUIRE(h.renderer->renderFrame(h.scene, t, 1920, 1080).has_value());
            if (i >= 20) {
                gpu.push_back(h.renderer->timeline().msFor("scene"));
            }
        }
        std::sort(gpu.begin(), gpu.end());
        return gpu[gpu.size() / 2];
    };
    for (const auto& a : arms) {
        static_cast<void>(measure(a));
    }
    std::array<std::vector<double>, 4> ms;
    for (int rep = 0; rep < 8; ++rep) {
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
    WARN("1080p, a mostly-sky frame, ribbons at 48 planes, the scene label's median (ms), 8 interleaved reps: pulse 0"
         << list(ms[0]) << " | pulse 0.7" << list(ms[1]) << " | step scale 0.75 (36 planes)" << list(ms[2])
         << " | step scale 0.5 (24 planes)" << list(ms[3]));
    CHECK(ms[0][0] > 0.0);
}
