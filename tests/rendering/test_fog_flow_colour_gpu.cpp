// The fog's §16 turbulence and §25 colours, in the PICTURE (ADR-713, ADR-714).
//
// `test_fog_parity_gpu.cpp` proves `shaders/fog.wgsl` computes what `world/fog_field.cpp` computes.
// It cannot prove the march CALLS those functions, or that `volume.wgsl`'s bound lets the result
// through -- and both are places a feature is built and unreachable (docs/testing.md; ADR-575 found
// its own emission term missing from a harness this way). So these render.
//
// **The cases and how each fails.**
//   1. *Turbulence reaches beyond the calm primitive's own bound.* Fog appears in pixels whose rays
//      miss the untroubled sphere's bounding cylinder entirely. That needs the field displaced AND
//      the bound in `mediumBoundOf` grown to let the march sample there: remove
//      `turbulence * max(semi.x, semi.z)` from the WGSL bound and case 1 finds no such pixel, even
//      though the CPU twin (and its containment test) is right. Its control: the calm sphere puts
//      no light in those pixels either.
//   2. *A height colour moves hue and not luminance, in the frame.* Luminance is linear, and the
//      tint is luminance-preserving per sample, so the integrated pixel's luminance must not move
//      while its chromaticity does. Drop the tint from `mediumEmissionAt` and the chroma half fails;
//      mix toward the raw tint and the luminance half fails.
//   3. *A distance colour does the same, more to the far side of a bank than the near.*

#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"
#include "world/atmospherics.hpp"
#include "world/effects/effect_registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>

using namespace avgen;

namespace {

constexpr std::uint32_t kSize = 256;
constexpr float kFovY = 0.87266f; // 50 degrees
const glm::vec3 kCamera(0.0f, 0.0f, 600.0f);

std::unique_ptr<gpu::Context> makeContext() {
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

// A self-luminous fog volume hanging in black, no lights: what reaches the frame is the medium's own
// emission, so the picture is the field and its colour and nothing else.
scene::Scene glowScene(const std::function<void(world::EffectInstance&)>& author) {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.volumeDensity = 0.0f;
    s.environment.volumeScattering = 1.0f;
    s.environment.volumeAbsorption = 1.0f;
    s.environment.volumeSteps = 64;
    s.environment.volumeMaxDistance = 1500.0f;
    s.environment.volumeJitter = 0.0f; // no per-pixel offset: two arms differ only by the control
    s.camera.position = kCamera;
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.camera.fovYRadians = kFovY;
    s.camera.lens.useExplicitFov = true; // or the lens model picks the FOV and the ray geometry below is wrong
    s.post.bloomEnabled = false;         // bloom would light every pixel a little
    s.camera.nearPlane = 1.0f;
    s.camera.farPlane = 3000.0f;

    world::EffectInstance e = world::makeEffect(world::EffectKind::VolumetricFog, "bank");
    world::Vortex& v = e.vortex;
    v.field.center = {0.0f, 0.0f, 0.0f};
    v.field.radius = 100.0f;
    v.field.thickness = 100.0f;
    v.field.cloudNoise = 0.0f;
    v.density = 1.2f;
    v.emission = 0.05f;
    v.scattering = 0.0f;
    v.spill = 0.0f;
    v.colorDeep = {0.10f, 0.25f, 0.20f};
    v.colorMid = {0.20f, 0.45f, 0.40f};
    v.colorAccent = {0.30f, 0.60f, 0.55f};
    v.filaments = 0.0f;
    e.values.setFloat("fog/shape", 1.0f); // a sphere
    e.values.setFloat("fog/edgeSoftness", 0.05f);
    e.values.setFloat("fog/heightInfluence", 0.0f);
    e.values.setFloat("fog/densityThreshold", 0.0f);
    author(e);
    s.atmospherics.mediumCount = 1;
    world::packMediumSlot(e, 1.0f, s.atmospherics.media[0]);
    return s;
}

gpu::ImageF shot(rendering::SceneRenderer& renderer, const scene::Scene& s, double t = 2.0) {
    FrameTime time{};
    time.renderTime = t;
    time.deltaTime = 1.0 / 60.0;
    auto image = renderer.renderToImageFloat(s, time, kSize, kSize);
    REQUIRE(image.has_value());
    return std::move(*image);
}

float luminance(const float* p) { return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]; }

// How far the camera ray through pixel (x, y) passes from the world's vertical axis, in XZ --
// the quantity a vertical bounding cylinder about the origin is a threshold on.
float rayAxisDistance(std::uint32_t x, std::uint32_t y) {
    const float t = std::tan(kFovY * 0.5f);
    const float nx = ((float(x) + 0.5f) / float(kSize) * 2.0f - 1.0f) * t;
    const float ny = (1.0f - (float(y) + 0.5f) / float(kSize) * 2.0f) * t;
    const glm::vec3 d = glm::normalize(glm::vec3(nx, ny, -1.0f));
    const glm::vec2 dh = glm::normalize(glm::vec2(d.x, d.z));
    // Distance from the origin to the line through (0, 600) along dh, in the XZ plane.
    return std::abs(kCamera.z * dh.x);
}

// Chromaticity: the colour with its luminance divided out.
glm::vec3 chroma(const float* p) {
    const float l = std::max(luminance(p), 1e-6f);
    return glm::vec3(p[0], p[1], p[2]) / l;
}

} // namespace

TEST_CASE("turbulence carries fog past the calm volume's own bound", "[gpu][fog][flow]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const scene::Scene calmScene = glowScene([](world::EffectInstance&) {});
    const scene::Scene wildScene = glowScene([](world::EffectInstance& e) {
        e.values.setFloat("fog/turbulence", 1.0f);      // the row's hard maximum: the longest reach
        e.values.setFloat("fog/turbulenceScale", 0.5f); // big lobes: the whole side of the ball moves
    });
    const float calmBound = world::mediumBound(calmScene.atmospherics.media[0]).radiusXZ;
    REQUIRE(calmBound > 100.0f);
    int outsideCalm = 0;   // pixels outside the calm bound the calm sphere lit (must be 0)
    int outsideWild = 0;   // ...and the turbulent one lit
    int calmInside = 0;    // the control: the calm sphere is in the frame at all
    // Three instants, because the lobes that reach past the calm bound come and go as the flow
    // evolves; at any one instant a few pixels' worth of the ball's rim is out there.
    for (const double t : {2.0, 6.5, 13.0}) {
        const gpu::ImageF calm = shot(renderer, calmScene, t);
        const gpu::ImageF wild = shot(renderer, wildScene, t);
        for (std::uint32_t y = 0; y < kSize; ++y) {
            for (std::uint32_t x = 0; x < kSize; ++x) {
                // 4% clear of the bound, which is clear of the half-resolution march's upsample.
                const bool outside = rayAxisDistance(x, y) > calmBound * 1.04f;
                const float lc = luminance(calm.pixel(x, y));
                const float lw = luminance(wild.pixel(x, y));
                if (outside) {
                    outsideCalm += lc > 1e-4f ? 1 : 0;
                    outsideWild += lw > 1e-4f ? 1 : 0;
                } else {
                    calmInside += lc > 1e-3f ? 1 : 0;
                }
            }
        }
    }
    INFO("calm bound r " << calmBound << "; lit pixels outside it: calm " << outsideCalm << ", turbulent "
         << outsideWild << "; calm lit inside " << calmInside);
    REQUIRE(calmInside > 500);
    CHECK(outsideCalm == 0);
    // Measured 9 at t = 2 alone and zero with the WGSL bound's growth removed; the calm control is
    // exactly 0. A presence test, and the right one: the broken bound makes the count exactly zero.
    CHECK(outsideWild > 5);
}

TEST_CASE("a height colour moves the frame's hue and not its luminance", "[gpu][fog][colour]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const glm::vec3 warm(0.9f, 0.35f, 0.1f);
    const auto withTint = [&](const char* colourKey, const char* amountKey, float amount) {
        return glowScene([=](world::EffectInstance& e) {
            e.values.setColor(colourKey, warm);
            e.values.setFloat(amountKey, amount);
            e.values.setFloat("fog/distanceColorRange", 500.0f);
        });
    };
    struct Arm {
        const char* name;
        const char* colour;
        const char* amount;
    };
    for (const Arm& arm : {Arm{"height", "fog/heightColor", "fog/heightColorAmount"},
                           Arm{"distance", "fog/distanceColor", "fog/distanceColorAmount"}}) {
        INFO("arm " << arm.name);
        const gpu::ImageF plain = shot(renderer, withTint(arm.colour, arm.amount, 0.0f));
        const gpu::ImageF tinted = shot(renderer, withTint(arm.colour, arm.amount, 1.0f));
        int lit = 0;
        int warmer = 0;
        double worstLum = 0.0;
        for (std::uint32_t y = 0; y < kSize; ++y) {
            for (std::uint32_t x = 0; x < kSize; ++x) {
                const float* a = plain.pixel(x, y);
                const float* b = tinted.pixel(x, y);
                const float la = luminance(a);
                if (la < 1e-3f) {
                    continue;
                }
                ++lit;
                worstLum = std::max(worstLum, double(std::abs(luminance(b) - la) / la));
                // Warmer: the red share of the chromaticity rose.
                if (chroma(b).r > chroma(a).r + 0.05f) {
                    ++warmer;
                }
            }
        }
        INFO("lit " << lit << ", warmer " << warmer << ", worst relative luminance change " << worstLum);
        REQUIRE(lit > 500);
        CHECK(warmer > lit / 4);
        // Half-float storage rounds each channel to ~1e-3 relative; three of them into a luminance.
        CHECK(worstLum < 5e-3);
    }
}

TEST_CASE("the distance colour tints the far side of a volume more than the near", "[gpu][fog][colour]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    // An elongated box running AWAY from the camera, so its near end fills the centre of the frame
    // and its far end is 300 m further on, seen above it past the near end's top.
    const auto scene = [](float amount) {
        scene::Scene s = glowScene([=](world::EffectInstance& e) {
            e.values.setFloat("fog/shape", 3.0f); // a box
            e.values.setFloat("fog/bankLength", 4.0f);
            e.values.setFloat("fog/bankRotation", 90.0f); // long axis along Z
            e.vortex.field.thickness = 30.0f;
            e.values.setColor("fog/distanceColor", glm::vec3(0.9f, 0.35f, 0.1f));
            e.values.setFloat("fog/distanceColorAmount", amount);
            e.values.setFloat("fog/distanceColorRange", 400.0f);
        });
        s.camera.position = {0.0f, 160.0f, 600.0f};
        return s;
    };
    const gpu::ImageF plain = shot(renderer, scene(0.0f));
    const gpu::ImageF tinted = shot(renderer, scene(1.0f));
    // Rows run from the far end (top of the volume on screen) to the near end (bottom).
    double farShift = 0.0;
    double nearShift = 0.0;
    int farN = 0;
    int nearN = 0;
    int firstLit = -1;
    int lastLit = -1;
    for (std::uint32_t y = 0; y < kSize; ++y) {
        const float* p = plain.pixel(kSize / 2, y);
        if (luminance(p) > 1e-3f) {
            firstLit = firstLit < 0 ? int(y) : firstLit;
            lastLit = int(y);
        }
    }
    REQUIRE(firstLit >= 0);
    REQUIRE(lastLit - firstLit > 10);
    const int span = lastLit - firstLit;
    for (int y = firstLit; y <= lastLit; ++y) {
        const float* a = plain.pixel(kSize / 2, std::uint32_t(y));
        const float* b = tinted.pixel(kSize / 2, std::uint32_t(y));
        if (luminance(a) < 1e-3f) {
            continue;
        }
        const double shift = chroma(b).r - chroma(a).r;
        if (y < firstLit + span / 3) {
            farShift += shift;
            ++farN;
        } else if (y > lastLit - span / 3) {
            nearShift += shift;
            ++nearN;
        }
    }
    REQUIRE(farN > 0);
    REQUIRE(nearN > 0);
    INFO("mean red-chroma shift: far " << farShift / farN << ", near " << nearShift / nearN);
    CHECK(farShift / farN > nearShift / nearN + 0.02);
    CHECK(nearShift / nearN > 0.0);
}

TEST_CASE("a flow finer than the march's step does not reach the frame", "[gpu][fog][flow][bandlimit]") {
    // ADR-718, in the PICTURE. The parity case proves the shader band-limits against the step it is
    // handed; only a render proves the march HANDS it the step -- a `mediumShape` that passed a zero
    // step would point-sample the flow and every CPU and parity test would still pass.
    //
    // Scale 20 on a 100 m sphere is a first octave of 5 m. At 32 steps the march spaces its samples
    // about 10 m apart through this volume -- two cycles a step -- so the flow is dropped and the
    // frame is the calm sphere's. At 256 steps the same flow is a quarter cycle a step, and it is
    // back: the band follows the step, not the scale. Break `mediumShape`'s step (a zero vector) and
    // the 32-step frame is the full turbulent one.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const auto scene = [](float turbulence, int steps) {
        scene::Scene s = glowScene([=](world::EffectInstance& e) {
            e.values.setFloat("fog/turbulence", turbulence);
            e.values.setFloat("fog/turbulenceScale", 20.0f);
        });
        s.environment.volumeSteps = steps;
        return s;
    };
    // The mean absolute luminance difference over the pixels the calm sphere lights, relative to
    // their mean luminance.
    const auto differs = [&](int steps) {
        const gpu::ImageF calm = shot(renderer, scene(0.0f, steps));
        const std::uint32_t marched = renderer.stats().volume.steps;
        const gpu::ImageF wild = shot(renderer, scene(0.7f, steps));
        double diff = 0.0;
        double lum = 0.0;
        int lit = 0;
        for (std::uint32_t y = 0; y < kSize; ++y) {
            for (std::uint32_t x = 0; x < kSize; ++x) {
                const float lc = luminance(calm.pixel(x, y));
                if (lc < 1e-3f) {
                    continue;
                }
                diff += std::abs(luminance(wild.pixel(x, y)) - lc);
                lum += lc;
                ++lit;
            }
        }
        INFO("steps " << steps << " (marched " << marched << "): " << lit << " lit pixels");
        REQUIRE(lit > 2000);
        return diff / lum;
    };
    const double coarse = differs(32);
    const double fine = differs(256);
    INFO("relative luminance change from the flow: 32 steps " << coarse << ", 256 steps " << fine);
    // Measured 0.0095 at 32 steps (grazing rays through the bound's rim have short steps and keep
    // some flow) and 0.091 at 256. With the march passing a zero step, 32 steps measures 0.11.
    CHECK(coarse < 0.03);
    CHECK(fine > 0.05);
}
