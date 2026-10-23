// ADR-1607: the spatial upscaler in the tonemap's fetch.
//   - at renderScale 1 the frame is byte-identical with the upscaler on or off (it runs only on a smaller target);
//   - at 0.5 it is sharper than the bilinear fetch (more edge energy) without changing the picture's mean or adding
//     values outside the scene's range (the anti-ringing clamp).
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
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


double edgeEnergy(const gpu::Image8& img) {
    double sum = 0.0;
    for (std::uint32_t y = 1; y + 1 < img.height; ++y) {
        for (std::uint32_t x = 1; x + 1 < img.width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 4;
            const int gx = img.rgba[i + 4] - img.rgba[i - 4];
            const int gy = img.rgba[i + img.width * 4] - img.rgba[i - img.width * 4];
            sum += std::sqrt(static_cast<double>(gx * gx + gy * gy));
        }
    }
    return sum;
}

double meanOf(const gpu::Image8& img) {
    double sum = 0.0;
    for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
        sum += img.rgba[i] + img.rgba[i + 1] + img.rgba[i + 2];
    }
    return sum / static_cast<double>(img.rgba.size() / 4 * 3);
}

} // namespace

TEST_CASE("The spatial upscaler sharpens a reduced frame and leaves a full-size one alone (ADR-1607)",
          "[gpu][upscale][adr1607]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    const scene::Scene s = crystalScene(false);
    const auto render = [&](float scale, bool upscaler) {
        rendering::QualitySettings q = renderer.qualitySettings();
        q.renderScale = scale;
        q.spatialUpscaler = upscaler;
        renderer.setQualitySettings(q);
        auto img = renderer.renderToImage(s, FrameTime{}, 256, 256);
        REQUIRE(img.has_value());
        return *img;
    };
    const auto fullOn = render(1.0f, true);
    const auto fullOff = render(1.0f, false);
    CHECK(gpu::hashImage(fullOn) == gpu::hashImage(fullOff));

    const auto bilinear = render(0.5f, false);
    const auto upscaled = render(0.5f, true);
    CHECK(gpu::hashImage(bilinear) != gpu::hashImage(upscaled));
    const double eb = edgeEnergy(bilinear);
    const double eu = edgeEnergy(upscaled);
    const double ef = edgeEnergy(fullOn);
    WARN("edge energy at 256x256: full " << ef << ", renderScale 0.5 bilinear " << eb << ", upscaled " << eu);
    CHECK(eu > eb * 1.05);  // sharper than the bilinear fetch
    CHECK(eu < ef * 1.25);  // and not oversharpened past the full-resolution frame
    CHECK(std::abs(meanOf(upscaled) - meanOf(bilinear)) < 2.0); // the same picture, on average
    CHECK(ctx->errorCount() == 0);
}
