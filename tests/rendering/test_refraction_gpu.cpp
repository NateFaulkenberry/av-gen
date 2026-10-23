// ADR-1603: the refraction phase for procedural materials, and the ladder's levers on it and on the cinematic water.
//
//   - a refractive procedural surface samples the refraction copy: in front of a red backdrop a blue crystal with no
//     opacity shows red, and its absorption darkens what it shows by channel;
//   - with the ladder's refraction off it draws as its opaque material (the fallback keeps its identity: blue);
//   - a scene with no refractive material pays nothing: the same render passes and the same pixels whether the
//     ladder allows refraction or not, and no refractive draw.
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "rendering/water_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <filesystem>
#include <vector>
#include <memory>

using namespace avgen;

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

scene::ProceduralGeometry cylinder(const char* name, float radius, float height, glm::vec3 at, glm::vec3 colour,
                                   std::uint64_t hash) {
    scene::ProceduralGeometry g;
    g.name = name;
    g.source.kind = scene::PrimitiveKind::Cylinder;
    g.source.radius = radius;
    g.source.height = height;
    g.source.radialSegments = 24;
    g.source.heightSegments = 2;
    scene::InstanceRecord r{};
    r.position = {at.x, at.y, at.z, 1.0f};
    r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    r.scale = {1.0f, 1.0f, 1.0f, 0.0f};
    r.random = {0.5f, 0.5f, 0.5f, 0.5f};
    r.color = {1.0f, 1.0f, 1.0f, 0.0f};
    r.emissive = {1.0f, 1.0f, 1.0f, 0.0f};
    g.instances = {r};
    g.structureVersion = 1;
    g.meshHash = hash;
    g.material.baseColor = colour;
    g.material.unlit = true;
    return g;
}

// A wide red backdrop, and a blue crystal in front of its middle.
scene::Scene crystalScene(bool refractive) {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    s.camera.position = {0.0f, 1.0f, 8.0f};
    s.camera.target = {0.0f, 1.0f, 0.0f};
    s.procedurals.push_back(cylinder("backdrop", 6.0f, 6.0f, {0.0f, -2.0f, -4.0f}, {0.9f, 0.1f, 0.05f}, 11));
    auto crystal = cylinder("crystal", 0.8f, 3.0f, {0.0f, -0.5f, 2.0f}, {0.05f, 0.1f, 0.9f}, 12);
    if (refractive) {
        crystal.material.refraction.strength = 0.02f;
        crystal.material.refraction.opacity = 0.0f;
        crystal.material.refraction.thickness = 4.0f;
    }
    s.procedurals.push_back(crystal);
    return s;
}

struct Rgb {
    int r = 0, g = 0, b = 0;
};
Rgb centre(const gpu::Image8& img) {
    const std::size_t i = (static_cast<std::size_t>(img.height / 2) * img.width + img.width / 2) * 4;
    return {img.rgba[i], img.rgba[i + 1], img.rgba[i + 2]};
}

struct Rig {
    std::unique_ptr<gpu::Context> ctx = makeContext();
    gpu::ShaderLibrary shaders{*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)}};
    rendering::SceneRenderer renderer{*ctx, shaders};
    Rig() { REQUIRE(renderer.init().has_value()); }
    gpu::Image8 render(const scene::Scene& s, bool refraction) {
        rendering::QualitySettings q = renderer.qualitySettings();
        q.refraction = refraction;
        renderer.setQualitySettings(q);
        auto img = renderer.renderToImage(s, FrameTime{}, 96, 96);
        REQUIRE(img.has_value());
        return *img;
    }
};

} // namespace

TEST_CASE("A refractive procedural surface shows the refraction copy, absorbed by channel (ADR-1603)",
          "[gpu][procedural][refraction][adr1603]") {
    Rig rig;
    scene::Scene s = crystalScene(true);

    // Clear, no opacity: the crystal shows the red backdrop behind it.
    const Rgb clear = centre(rig.render(s, true));
    INFO("clear crystal centre " << clear.r << " " << clear.g << " " << clear.b);
    CHECK(clear.r > 100);
    CHECK(clear.r > clear.b + 60);
    CHECK(rig.renderer.stats().procedural.refractiveObjects == 1);
    CHECK(rig.renderer.stats().procedural.refractiveDraws >= 1);

    // Red absorbed over the path behind it: what it shows darkens in red.
    s.procedurals[1].material.refraction.absorption = {1.5f, 0.0f, 0.0f};
    const Rgb absorbing = centre(rig.render(s, true));
    INFO("absorbing crystal centre " << absorbing.r << " " << absorbing.g << " " << absorbing.b);
    CHECK(absorbing.r < clear.r - 30);

    // Full opacity: its own shaded colour again, through the refraction pipeline.
    s.procedurals[1].material.refraction.absorption = {0.0f, 0.0f, 0.0f};
    s.procedurals[1].material.refraction.opacity = 1.0f;
    const Rgb opaque = centre(rig.render(s, true));
    CHECK(opaque.b > opaque.r + 60);

    // The ladder's refraction off: the opaque material, its identity kept, and no refractive draw.
    s.procedurals[1].material.refraction.opacity = 0.0f;
    const Rgb fallback = centre(rig.render(s, false));
    INFO("fallback centre " << fallback.r << " " << fallback.g << " " << fallback.b);
    CHECK(fallback.b > fallback.r + 60);
    CHECK(rig.renderer.stats().procedural.refractiveDraws == 0);
    CHECK(rig.ctx->errorCount() == 0);
}

TEST_CASE("A scene with no refractive surface pays nothing for the refraction phase (ADR-1603)",
          "[gpu][procedural][refraction][adr1603]") {
    Rig rig;
    const scene::Scene s = crystalScene(false);
    const auto on = rig.render(s, true);
    const auto passesOn = rig.renderer.stats().state.renderPasses;
    CHECK(rig.renderer.stats().procedural.refractiveObjects == 0);
    CHECK(rig.renderer.stats().procedural.refractiveDraws == 0);
    const auto off = rig.render(s, false);
    CHECK(rig.renderer.stats().state.renderPasses == passesOn);
    CHECK(gpu::hashImage(on) == gpu::hashImage(off));

    // ...and the refractive scene's phase is one more render pass (the split after the copy), not more.
    const scene::Scene r = crystalScene(true);
    static_cast<void>(rig.render(r, true));
    CHECK(rig.renderer.stats().state.renderPasses == passesOn + 1);
    CHECK(rig.ctx->errorCount() == 0);
}

TEST_CASE("The water SSR lever scales the cinematic steps and rays, and turns the SSR off at 0 (ADR-1603)",
          "[gpu][water][refraction][adr1603]") {
    rendering::WaterUniforms u{};
    u.cineA.x = 1.0f;  // ssr
    u.cineB.z = 48.0f; // steps
    u.cineC.y = 4.0f;  // rays
    rendering::WaterUniforms same = u;
    rendering::applyWaterSsrScale(same, 1.0f);
    CHECK(same.cineB.z == 48.0f);
    CHECK(same.cineC.y == 4.0f);
    rendering::WaterUniforms half = u;
    rendering::applyWaterSsrScale(half, 0.5f);
    CHECK(half.cineB.z == 24.0f);
    CHECK(half.cineC.y == 2.0f);
    CHECK(half.cineA.x == 1.0f);
    rendering::WaterUniforms tiny = u;
    rendering::applyWaterSsrScale(tiny, 0.05f);
    CHECK(tiny.cineB.z == 8.0f); // the shader's own floor
    CHECK(tiny.cineC.y == 1.0f);
    rendering::WaterUniforms off = u;
    rendering::applyWaterSsrScale(off, 0.0f);
    CHECK(off.cineA.x == 0.0f);
}

// Hidden ([.]): the small-fixture cost of the refraction phase at 1080p with unquantised timestamps (ADR-1600). Run it
// by name; it measures and WARNs, asserting only that it measured.
TEST_CASE("Refraction phase cost on a small fixture (measure)", "[.][gpu][refraction][measure][adr1603]") {
    Rig rig;
    if (!rig.renderer.timeline().available()) {
        SKIP("timestamp queries unavailable");
    }
    const auto measure = [&](const scene::Scene& s, bool refraction) {
        std::vector<double> gpu, sceneMs;
        rendering::QualitySettings q = rig.renderer.qualitySettings();
        q.refraction = refraction;
        rig.renderer.setQualitySettings(q);
        for (int i = 0; i < 140; ++i) {
            FrameTime t{};
            t.renderTime = i / 30.0;
            REQUIRE(rig.renderer.renderFrame(s, t, 1920, 1080).has_value());
            if (i >= 20) {
                gpu.push_back(rig.renderer.stats().gpuFrameMs);
                sceneMs.push_back(rig.renderer.timeline().msFor("scene"));
            }
        }
        std::sort(gpu.begin(), gpu.end());
        std::sort(sceneMs.begin(), sceneMs.end());
        return std::pair{gpu[gpu.size() / 2], sceneMs[sceneMs.size() / 2]};
    };
    // the crystal enlarged to cover about a third of the frame
    scene::Scene s = crystalScene(true);
    s.procedurals[1].source.radius = 2.5f;
    s.procedurals[1].meshHash = 13;
    s.procedurals[1].material.refraction.absorption = {0.4f, 0.8f, 0.2f};
    s.procedurals[1].material.refraction.opacity = 0.25f;
    static_cast<void>(measure(s, true)); // the clocks up and the pipelines built
    std::vector<double> gpuOff, gpuOn, sceneOff, sceneOn;
    for (int rep = 0; rep < 4; ++rep) { // interleaved, so a clock drift lands on both arms
        const auto a = measure(s, false);
        const auto b = measure(s, true);
        gpuOff.push_back(a.first);
        sceneOff.push_back(a.second);
        gpuOn.push_back(b.first);
        sceneOn.push_back(b.second);
    }
    const auto list = [](const std::vector<double>& v) {
        std::string out;
        for (const double x : v) {
            out += fmt::format(" {:.2f}", x);
        }
        return out;
    };
    WARN("1080p, a crystal over about a third of the frame, 4 interleaved reps of 120 frames (medians, ms): GPU off"
         << list(gpuOff) << " | on" << list(gpuOn) << "; the scene label off" << list(sceneOff) << " | on"
         << list(sceneOn));
    const auto on = std::pair{gpuOn[0], sceneOn[0]};
    CHECK(on.first > 0.0);
}
