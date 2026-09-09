// Lighting performance probes (ADR-033/034), hidden behind `[.perf]` so they never run in CI.
//
//   avgen_render_tests "[.perf][lighting]"
//
// The numbers they print go into docs/performance/lighting.md. Every case renders the same world
// at 1920x1080 and varies one thing, so the delta is attributable: the shadow passes come from
// their own GPU timer, ambient occlusion from its own, and the whole-frame figure is the frame
// timer (every pass from the first shadow map through tone mapping).

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;

namespace {

constexpr std::uint32_t kWidth = 1920;
constexpr std::uint32_t kHeight = 1080;
constexpr int kWarmup = 20;
constexpr int kMeasured = 60;

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

scene::MeshData boxMesh(glm::vec3 half) {
    scene::MeshData m;
    const glm::vec3 normals[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (const glm::vec3 normal : normals) {
        const glm::vec3 u = std::abs(normal.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::cross(glm::vec3(0, 1, 0), normal);
        const glm::vec3 v = glm::cross(normal, u);
        const glm::vec3 c = normal * half;
        const glm::vec3 du = u * half;
        const glm::vec3 dv = v * half;
        const auto base = static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({c - du - dv, normal, {0, 0}});
        m.vertices.push_back({c + du - dv, normal, {1, 0}});
        m.vertices.push_back({c + du + dv, normal, {1, 1}});
        m.vertices.push_back({c - du + dv, normal, {0, 1}});
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return m;
}

// A courtyard of pillars on a floor: enough occluders for the shadow passes to do real work and
// enough creases for the occlusion pass to find, at a scale a world actually uses.
scene::Scene courtyard(std::uint32_t localLights) {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.01f);
    s.environment.showSkybox = false;
    s.environment.environmentIntensity = 0.0f;
    s.camera.position = {0.0f, 14.0f, 42.0f};
    s.camera.target = {0.0f, 4.0f, 0.0f};
    s.camera.fovYRadians = 0.85f;
    s.camera.nearPlane = 0.4f;
    s.camera.farPlane = 400.0f;

    const auto floor = s.addMesh(boxMesh({60.0f, 0.5f, 60.0f}));
    const auto pillar = s.addMesh(boxMesh({1.2f, 7.0f, 1.2f}));
    {
        auto& e = s.addEntity("floor", floor);
        e.transform.position = {0.0f, -0.5f, 0.0f};
        e.material.baseColor = glm::vec3(0.6f);
        e.material.roughness = 0.85f;
    }
    for (int i = 0; i < 48; ++i) {
        const float a = static_cast<float>(i) * 6.2831853f / 48.0f;
        const float r = 10.0f + 9.0f * static_cast<float>(i % 3);
        auto& e = s.addEntity("pillar" + std::to_string(i), pillar);
        e.transform.position = {std::cos(a) * r, 7.0f, std::sin(a) * r};
        e.material.baseColor = glm::vec3(0.65f, 0.62f, 0.58f);
        e.material.roughness = 0.6f;
    }
    scene::PunctualLight key;
    key.name = "key";
    key.direction = glm::normalize(glm::vec3(-0.45f, -1.0f, -0.35f));
    key.intensity = 3.0f;
    key.castsShadow = true;
    key.contactShadow = true;
    s.addLight(key);
    for (std::uint32_t i = 0; i < localLights; ++i) {
        const float a = static_cast<float>(i) * 2.39996f;
        const float r = 6.0f + 22.0f * std::fmod(static_cast<float>(i) * 0.618f, 1.0f);
        scene::PunctualLight l;
        l.name = "local" + std::to_string(i);
        l.type = scene::PunctualLight::Type::Point;
        l.position = {std::cos(a) * r, 2.0f + 6.0f * std::fmod(static_cast<float>(i) * 0.31f, 1.0f),
                      std::sin(a) * r};
        l.intensity = 6.0f;
        l.range = 14.0f;
        l.contactShadow = false; // only the key marches; the rest are cheap
        s.addLight(l);
    }
    return s;
}

FrameTime frameAt(std::uint64_t index) {
    FrameTime t;
    t.frameIndex = index;
    t.renderTime = static_cast<double>(index) / 60.0;
    t.deltaTime = 1.0 / 60.0;
    return t;
}

double median(std::vector<double> values) {
    if (values.empty()) {
        return -1.0;
    }
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

struct Timings {
    double frameMs = -1.0;
    double shadowMs = -1.0;
    double aoMs = -1.0;
};

// Renders `kWarmup + kMeasured` advancing frames and reports the median of the measured window.
Timings measure(gpu::Context& ctx, gpu::ShaderLibrary& shaders, const scene::Scene& s,
                const rendering::QualitySettings& quality) {
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    renderer.setQualitySettings(quality);
    std::vector<double> frames;
    std::vector<double> shadows;
    std::vector<double> ao;
    for (int i = 0; i < kWarmup + kMeasured; ++i) {
        auto image = renderer.renderToImage(s, frameAt(static_cast<std::uint64_t>(i)), kWidth, kHeight);
        REQUIRE(image.has_value());
        if (i < kWarmup) {
            continue;
        }
        const rendering::RenderStats& stats = renderer.stats();
        if (stats.gpuFrameMs >= 0.0) {
            frames.push_back(stats.gpuFrameMs);
        }
        if (stats.shadows.shadowMs >= 0.0) {
            shadows.push_back(stats.shadows.shadowMs);
        }
        if (stats.ao.aoMs >= 0.0) {
            ao.push_back(stats.ao.aoMs);
        }
    }
    return {median(frames), median(shadows), median(ao)};
}

void report(const char* label, const Timings& t) {
    std::printf("  %-34s frame %7.3f ms   shadows %7.3f ms   ao %7.3f ms\n", label, t.frameMs, t.shadowMs,
                t.aoMs);
    std::fflush(stdout);
}

} // namespace

TEST_CASE("shadow, occlusion and cluster cost at 1080p", "[.perf][lighting]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const scene::Scene s = courtyard(0);

    const auto base = rendering::QualitySettings::forTier(rendering::QualityTier::Realtime);
    std::printf("\n1920x1080, 49 opaque draws, one shadow-casting directional light\n");

    rendering::QualitySettings off = base;
    off.cascadeCount = 0;
    off.ambientOcclusion = false;
    off.contactShadows = false;
    // A scene whose light casts nothing at all: the floor the rest is measured against.
    scene::Scene plain = s;
    plain.lights[0].castsShadow = false;
    plain.lights[0].contactShadow = false;
    const Timings none = measure(*ctx, shaders, plain, off);
    report("no shadows, no AO", none);

    rendering::QualitySettings shadowsOnly = base;
    shadowsOnly.ambientOcclusion = false;
    shadowsOnly.contactShadows = false;
    const Timings cascades = measure(*ctx, shaders, s, shadowsOnly);
    report("3 cascades @ 2048, PCSS", cascades);

    rendering::QualitySettings contact = base;
    contact.ambientOcclusion = false;
    const Timings withContact = measure(*ctx, shaders, s, contact);
    report("+ contact shadows (12 steps)", withContact);

    const Timings full = measure(*ctx, shaders, s, base);
    report("+ GTAO (half res, 3x6)", full);

    const Timings high = measure(*ctx, shaders, s, rendering::QualitySettings::forTier(rendering::QualityTier::High));
    report("high tier (4 cascades, 4x8 AO)", high);

    const Timings preview =
        measure(*ctx, shaders, s, rendering::QualitySettings::forTier(rendering::QualityTier::Preview));
    report("preview tier (2 cascades @ 1024)", preview);

    std::printf("\nclustered lighting, 3 cascades + contact + GTAO\n");
    for (const std::uint32_t lights : {0u, 16u, 64u, 200u}) {
        const scene::Scene many = courtyard(lights);
        const Timings t = measure(*ctx, shaders, many, base);
        report((std::to_string(lights) + " local point lights").c_str(), t);
    }

    rendering::QualitySettings fallback = base;
    fallback.clusteredLighting = false;
    const Timings uniform = measure(*ctx, shaders, courtyard(7), fallback);
    report("8-light uniform fallback path", uniform);
    const Timings clustered = measure(*ctx, shaders, courtyard(7), base);
    report("same 8 lights, clustered", clustered);

    // The budget from ADR-034: shadows plus occlusion under 4 ms combined at the realtime tier.
    if (full.shadowMs >= 0.0 && full.aoMs >= 0.0) {
        std::printf("\n  shadows + AO = %.3f ms (budget 4.0 ms)\n", full.shadowMs + full.aoMs);
        CHECK(full.shadowMs + full.aoMs < 4.0);
    }
    SUCCEED();
}
