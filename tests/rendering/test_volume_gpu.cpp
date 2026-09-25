// Volumetric atmosphere (ADR-032): the fog pass is skipped when volumeDensity is 0 (so scenes
// without fog render exactly as before), it attenuates distant surfaces far more than near ones,
// a density field concentrates it, the height falloff makes a vertical gradient, and two fresh
// renderers produce identical frames.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/render_quality.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"
#include "spatial/field.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <memory>

using namespace avgen;

namespace {

constexpr std::uint32_t kSize = 128;

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

gpu::ShaderLibrary makeShaders(gpu::Context& ctx) {
    return gpu::ShaderLibrary(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
}

std::unique_ptr<rendering::SceneRenderer> makeRenderer(gpu::Context& ctx, gpu::ShaderLibrary& shaders) {
    auto renderer = std::make_unique<rendering::SceneRenderer>(ctx, shaders);
    REQUIRE(renderer->init().has_value());
    return renderer;
}

scene::MeshData boxMesh(float h) {
    scene::MeshData m;
    const glm::vec3 n[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (const glm::vec3 normal : n) {
        const glm::vec3 u = std::abs(normal.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::cross(glm::vec3(0, 1, 0), normal);
        const glm::vec3 v = glm::cross(normal, u);
        const auto base = static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({normal * h - u * h - v * h, normal, {0, 0}});
        m.vertices.push_back({normal * h + u * h - v * h, normal, {1, 0}});
        m.vertices.push_back({normal * h + u * h + v * h, normal, {1, 1}});
        m.vertices.push_back({normal * h - u * h + v * h, normal, {0, 1}});
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return m;
}

// Two unlit boxes of the same apparent size: one 4 units away on the left, one 40 units away on
// the right, so their pixels are identical without fog and only the distance differs.
scene::Scene twoBoxScene() {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.fogColor = glm::vec3(0.02f, 0.03f, 0.05f);
    s.environment.showSkybox = false;
    s.camera.position = {0.0f, 0.0f, 0.0f};
    s.camera.target = {0.0f, 0.0f, -1.0f};
    s.camera.fovYRadians = 0.87266f; // 50 degrees
    s.camera.nearPlane = 0.1f;
    s.camera.farPlane = 400.0f;
    const auto near = s.addMesh(boxMesh(0.5f));
    const auto far = s.addMesh(boxMesh(5.0f));
    {
        auto& e = s.addEntity("near", near);
        e.transform.position = {-1.2f, 0.0f, -4.0f};
        e.material.unlit = true;
        e.material.baseColor = glm::vec3(0.6f);
    }
    {
        auto& e = s.addEntity("far", far);
        e.transform.position = {12.0f, 0.0f, -40.0f};
        e.material.unlit = true;
        e.material.baseColor = glm::vec3(0.6f);
    }
    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(0.0f, -0.2f, -1.0f));
    key.intensity = 2.0f;
    s.addLight(key);
    return s;
}

// Screen positions of the two boxes (both at |ndc.x| = 0.64, ndc.y = 0).
constexpr std::uint32_t kNearX = 23;
constexpr std::uint32_t kFarX = 104;
constexpr std::uint32_t kMidY = kSize / 2;

float luminanceAt(const gpu::ImageF& image, std::uint32_t x, std::uint32_t y) {
    const float* p = image.pixel(x, y);
    return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
}

gpu::ImageF renderFloat(rendering::SceneRenderer& renderer, const scene::Scene& s,
                        std::uint64_t frameIndex = 0, double renderTime = 1.0) {
    FrameTime time{};
    time.renderTime = renderTime;
    time.deltaTime = 1.0 / 60.0;
    time.frameIndex = frameIndex;
    auto image = renderer.renderToImageFloat(s, time, kSize, kSize);
    REQUIRE(image.has_value());
    return std::move(*image);
}

void enableFog(scene::Scene& s, float density) {
    s.environment.volumeDensity = density;
    s.environment.volumeScattering = 0.6f;
    s.environment.volumeAbsorption = 1.0f;
    s.environment.volumeAnisotropy = 0.2f;
    s.environment.volumeSteps = 48;
    s.environment.volumeMaxDistance = 120.0f;
}

} // namespace

TEST_CASE("Fog off encodes no volume pass and renders identically to a fresh renderer", "[volume][gpu]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    const scene::Scene s = twoBoxScene();
    REQUIRE(s.environment.volumeDensity == 0.0f); // off by default

    auto a = makeRenderer(*ctx, shaders);
    const gpu::ImageF imageA = renderFloat(*a, s);
    CHECK(a->stats().volume.steps == 0);
    CHECK(a->stats().volume.volumeMs < 0.0);

    auto b = makeRenderer(*ctx, shaders);
    const gpu::ImageF imageB = renderFloat(*b, s);
    CHECK(gpu::hashImage(imageA) == gpu::hashImage(imageB));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Volumetric fog darkens a distant surface and leaves a near one almost untouched",
          "[volume][gpu]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);

    scene::Scene s = twoBoxScene();
    const gpu::ImageF clear = renderFloat(*renderer, s);
    const float nearClear = luminanceAt(clear, kNearX, kMidY);
    const float farClear = luminanceAt(clear, kFarX, kMidY);
    INFO("near " << nearClear << " far " << farClear);
    REQUIRE(nearClear > 0.05f); // the sample pixels really are on the boxes
    REQUIRE(farClear > 0.05f);
    REQUIRE(std::abs(nearClear - farClear) < 0.02f); // and identical without fog

    enableFog(s, 0.04f);
    const gpu::ImageF foggy = renderFloat(*renderer, s);
    CHECK(renderer->stats().volume.steps == 48);
    CHECK(renderer->stats().volume.halfResolution);
    const float nearFog = luminanceAt(foggy, kNearX, kMidY);
    const float farFog = luminanceAt(foggy, kFarX, kMidY);
    INFO("near fog " << nearFog << " far fog " << farFog);
    // exp(-0.04 * 40) = 0.20 at the far box, exp(-0.04 * 4) = 0.85 at the near one.
    CHECK(farFog < farClear * 0.45f);
    CHECK(nearFog > nearClear * 0.7f);
    CHECK(nearFog < nearClear * 1.3f);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("A density field concentrates the fog where the field is strong", "[volume][gpu]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);

    scene::Scene s = twoBoxScene();
    enableFog(s, 0.06f);
    const gpu::ImageF uniform = renderFloat(*renderer, s);

    // A sphere of fog around the far box only: the near box's ray never enters it.
    spatial::FieldSpec blob;
    blob.name = "blob";
    blob.kind = spatial::FieldKind::Sphere;
    blob.position = {12.0f, 0.0f, -40.0f};
    blob.radius = 14.0f;
    blob.softness = 6.0f;
    s.fields.fields.push_back(blob);
    s.environment.volumeDensityField = "blob";
    const gpu::ImageF shaped = renderFloat(*renderer, s);

    const float nearUniform = luminanceAt(uniform, kNearX, kMidY);
    const float nearShaped = luminanceAt(shaped, kNearX, kMidY);
    const float farUniform = luminanceAt(uniform, kFarX, kMidY);
    const float farShaped = luminanceAt(shaped, kFarX, kMidY);
    INFO("near " << nearUniform << " -> " << nearShaped << ", far " << farUniform << " -> " << farShaped);
    // The near ray leaves the field's sphere alone, so it recovers almost all of its brightness.
    CHECK(nearShaped > nearUniform * 1.05f);
    // The far ray still crosses the blob, so it stays attenuated.
    CHECK(farShaped < nearShaped * 0.8f);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Height falloff makes the fog a vertical gradient", "[volume][gpu]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);

    // An empty scene: the only thing the fog can do is scatter light towards the eye.
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.camera.position = {0.0f, 0.0f, 0.0f};
    s.camera.target = {0.0f, 0.0f, -1.0f};
    s.camera.farPlane = 400.0f;
    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(0.0f, -1.0f, -0.2f));
    key.intensity = 4.0f;
    s.addLight(key);
    enableFog(s, 0.05f);
    s.environment.fogHeight = -2.0f;
    s.environment.fogHeightFalloff = 0.6f; // dense low, thin high

    const gpu::ImageF image = renderFloat(*renderer, s);
    const float low = luminanceAt(image, kSize / 2, kSize - 8);
    const float high = luminanceAt(image, kSize / 2, 8);
    INFO("low " << low << " high " << high);
    CHECK(low > 1e-4f);
    CHECK(low > high * 1.5f);
    // Monotone from the bottom of the frame to the top.
    const float middle = luminanceAt(image, kSize / 2, kSize / 2);
    CHECK(low >= middle);
    CHECK(middle >= high);
    CHECK(ctx->errorCount() == 0);
}

// ADR-032/ADR-033. `PunctualLight::volumetricStrength` (and a light rig's per-light `volumetric`)
// used to be packed for the GPU and read by nothing: the fog was always lit by the first enabled
// light at full strength, whatever the rig asked for. It now weights each light's in-scatter, and
// extinction is independent of it, so a light can darken the air without lighting it.
TEST_CASE("Per-light volumetric strength weights the in-scatter and not the extinction",
          "[volume][gpu]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);

    // A corner pixel sees no geometry, so its whole value is in-scattered fog.
    constexpr std::uint32_t kSkyX = 6;
    constexpr std::uint32_t kSkyY = 6;

    scene::Scene s = twoBoxScene();
    enableFog(s, 0.04f);
    REQUIRE(s.lights.size() == 1);
    REQUIRE(s.lights[0].volumetricStrength == 1.0f); // a light lights the air unless told otherwise
    const gpu::ImageF lit = renderFloat(*renderer, s);
    const float skyLit = luminanceAt(lit, kSkyX, kSkyY);
    const float farLit = luminanceAt(lit, kFarX, kMidY);
    REQUIRE(skyLit > 0.01f);

    s.lights[0].volumetricStrength = 0.0f;
    const gpu::ImageF dark = renderFloat(*renderer, s);
    const float skyDark = luminanceAt(dark, kSkyX, kSkyY);
    const float farDark = luminanceAt(dark, kFarX, kMidY);
    INFO("sky " << skyLit << " -> " << skyDark << ", far box " << farLit << " -> " << farDark);
    CHECK(skyDark < skyLit * 0.02f); // nothing lights the air any more

    s.lights[0].volumetricStrength = 0.5f;
    const float skyHalf = luminanceAt(renderFloat(*renderer, s), kSkyX, kSkyY);
    INFO("half strength " << skyHalf);
    CHECK(skyHalf > skyLit * 0.3f);
    CHECK(skyHalf < skyLit * 0.7f);

    // The far box is still fogged: turning the light out of the air does not turn the air off.
    const gpu::ImageF clear = [&] {
        scene::Scene noFog = twoBoxScene();
        return renderFloat(*renderer, noFog);
    }();
    CHECK(farDark < luminanceAt(clear, kFarX, kMidY) * 0.45f);
    CHECK(ctx->errorCount() == 0);
}

// Two lights, only the second of which is in the air: the fog takes the second one's colour.
TEST_CASE("The fog is lit by every light that declares a volumetric strength", "[volume][gpu]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);

    scene::Scene s = twoBoxScene();
    enableFog(s, 0.04f);
    s.lights[0].color = glm::vec3(1.0f, 0.1f, 0.1f); // red key, out of the air
    s.lights[0].volumetricStrength = 0.0f;
    scene::PunctualLight practical;
    practical.direction = glm::normalize(glm::vec3(0.0f, -0.2f, -1.0f));
    practical.intensity = 2.0f;
    practical.color = glm::vec3(0.1f, 0.1f, 1.0f); // blue practical, in the air
    practical.volumetricStrength = 1.0f;
    s.addLight(practical);

    constexpr std::uint32_t kSkyX = 6;
    constexpr std::uint32_t kSkyY = 6;
    const gpu::ImageF image = renderFloat(*renderer, s);
    const float* sky = image.pixel(kSkyX, kSkyY);
    INFO("sky rgb " << sky[0] << ", " << sky[1] << ", " << sky[2]);
    CHECK(sky[2] > 0.002f);
    CHECK(sky[2] > sky[0] * 4.0f); // the red light contributes nothing to the haze
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Two fresh renderers produce identical volumetric frames", "[volume][gpu]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    scene::Scene s = twoBoxScene();
    enableFog(s, 0.05f);
    s.environment.volumeNoiseAmount = 0.7f;
    s.environment.volumeNoiseScale = 0.15f;
    s.environment.volumeNoiseSpeed = 0.3f;
    s.environment.volumeEmission = 0.4f;

    auto a = makeRenderer(*ctx, shaders);
    auto b = makeRenderer(*ctx, shaders);
    for (std::uint64_t frame = 0; frame < 3; ++frame) {
        const gpu::ImageF ia = renderFloat(*a, s, frame);
        const gpu::ImageF ib = renderFloat(*b, s, frame);
        INFO("frame " << frame);
        CHECK(gpu::hashImage(ia) == gpu::hashImage(ib));
    }
    // The jitter varies from frame to frame, or it would not be decorrelating anything -- and what
    // makes one frame different from the next is the time on the timeline.
    const gpu::ImageF first = renderFloat(*a, s, 0, 1.0);
    const gpu::ImageF second = renderFloat(*a, s, 1, 1.0 + 1.0 / 60.0);
    CHECK(gpu::hashImage(first) != gpu::hashImage(second));

    // ...and the same moment is the same image however many frames this renderer has drawn to reach
    // it. Keying the jitter to the render's own frame counter instead made a given second look one
    // way in a full render and another in a render of the last few seconds: measured on
    // night-shift, three renders covering t=104s agreed on nothing (see FrameTime::frameNonce).
    const gpu::ImageF early = renderFloat(*a, s, 4, 2.0);
    const gpu::ImageF late = renderFloat(*a, s, 104, 2.0);
    CHECK(gpu::hashImage(early) == gpu::hashImage(late));
    CHECK(ctx->errorCount() == 0);
}

// Hidden performance probe: `avgen_render_tests "[.perf][volume]"` (Release). Reports the frame
// GPU time and the volume pass time at 1920x1080 for 32 and 64 steps, with and without the
// 3-octave noise and a density field. See docs/performance/procedural-geometry.md.
TEST_CASE("Volumetric fog throughput", "[.perf][volume]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    auto measure = [&](const scene::Scene& s, const char* label) {
        FixedStepClock clock(60.0);
        double frameSum = 0.0;
        double volumeSum = 0.0;
        int frames = 0;
        int volumeFrames = 0;
        for (int i = 0; i < 90; ++i) {
            auto img = renderer.renderToImage(s, clock.tick(), 1920, 1080);
            REQUIRE(img.has_value());
            if (i < 30) {
                continue;
            }
            if (renderer.stats().gpuFrameMs >= 0.0) {
                frameSum += renderer.stats().gpuFrameMs;
                ++frames;
            }
            if (renderer.stats().volume.volumeMs >= 0.0) {
                volumeSum += renderer.stats().volume.volumeMs;
                ++volumeFrames;
            }
        }
        CHECK(ctx->errorCount() == 0);
        WARN(label << ": scene+post GPU " << (frames ? frameSum / frames : -1.0) << " ms, volume pass "
                   << (volumeFrames ? volumeSum / volumeFrames : -1.0) << " ms, steps "
                   << renderer.stats().volume.steps);
    };

    {
        scene::Scene s = twoBoxScene();
        measure(s, "fog off (baseline)");
    }
    for (const int steps : {32, 64}) {
        {
            scene::Scene s = twoBoxScene();
            enableFog(s, 0.04f);
            s.environment.volumeSteps = steps;
            measure(s, steps == 32 ? "32 steps, no noise" : "64 steps, no noise");
        }
        {
            scene::Scene s = twoBoxScene();
            enableFog(s, 0.04f);
            s.environment.volumeSteps = steps;
            s.environment.volumeNoiseAmount = 0.6f;
            s.environment.volumeNoiseScale = 0.12f;
            s.environment.volumeNoiseSpeed = 0.2f;
            measure(s, steps == 32 ? "32 steps + fbm3 noise" : "64 steps + fbm3 noise");
        }
        {
            scene::Scene s = twoBoxScene();
            enableFog(s, 0.04f);
            s.environment.volumeSteps = steps;
            s.environment.volumeNoiseAmount = 0.6f;
            s.environment.volumeNoiseScale = 0.12f;
            s.environment.volumeEmission = 0.4f;
            spatial::FieldSpec blob;
            blob.name = "blob";
            blob.kind = spatial::FieldKind::Sphere;
            blob.position = {0.0f, 0.0f, -20.0f};
            blob.radius = 20.0f;
            blob.softness = 8.0f;
            s.fields.fields.push_back(blob);
            s.environment.volumeDensityField = "blob";
            measure(s, steps == 32 ? "32 steps + noise + density field + emission"
                                   : "64 steps + noise + density field + emission");
        }
    }
}

// ---- ADR-058: the surface fog's share of the mist layer -----------------------------------------

TEST_CASE("A view ray entirely inside the mist layer is fogged identically whether or not the layer "
          "is integrated",
          "[volume][gpu][fog]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);

    // The layer's top is far above the camera and both boxes, so the height term is 1 the whole
    // way along every ray. G's difference quotient is then exactly 1 and the integrated branch
    // must reduce to the uniform one -- not approximately, bit for bit.
    scene::Scene s = twoBoxScene();
    // ADR-705: the surface pass's density is the air's one density now. `volumeMaxDistance` 0 keeps
    // the march off, so these three cases still see the surface pass alone -- which is what they
    // are about. 0.0333 at the default absorption 0.5 is the Beer--Lambert extinction that matches
    // the exp-squared 0.02 they used to set at the distance where half the light gets through.
    s.environment.volumeMaxDistance = 0.0f;
    s.environment.volumeDensity = 0.0333f;
    s.environment.fogHeight = 500.0f;
    s.environment.fogHeightFalloff = 0.05f;

    s.environment.fogHeightAmount = 0.0f;
    const auto uniform = renderFloat(*renderer, s);
    s.environment.fogHeightAmount = 1.0f;
    const auto integrated = renderFloat(*renderer, s);

    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 0; x < kSize; ++x) {
            REQUIRE(luminanceAt(integrated, x, y) == luminanceAt(uniform, x, y));
        }
    }
}

TEST_CASE("A surface standing clear of the mist layer is fogged less than the same surface inside it",
          "[volume][gpu][fog]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);

    scene::Scene s = twoBoxScene();
    // Fog colour brighter than the boxes, so more fog means a brighter pixel and the sign of the
    // comparison cannot be confused with the boxes' own shading.
    s.environment.fogColor = glm::vec3(0.9f);
    s.environment.volumeMaxDistance = 0.0f; // ADR-705: surface pass alone, as above
    s.environment.volumeDensity = 0.0333f;
    s.environment.fogHeightFalloff = 0.25f;
    s.environment.fogHeightAmount = 1.0f;

    // Buried: the layer's top is above everything, so the far box is seen through full-density air.
    s.environment.fogHeight = 100.0f;
    const float buried = luminanceAt(renderFloat(*renderer, s), kFarX, kMidY);
    // Clear: the layer's top is at the ground, so the ray to the far box climbs out of the mist.
    s.environment.fogHeight = -1.0f;
    const float clear = luminanceAt(renderFloat(*renderer, s), kFarX, kMidY);

    CHECK(clear < buried);
    // The near box barely moves either way: it is close enough that little fog of any kind
    // accumulates in front of it, which is what makes this a height effect and not an exposure one.
    s.environment.fogHeight = 100.0f;
    const float nearBuried = luminanceAt(renderFloat(*renderer, s), kNearX, kMidY);
    s.environment.fogHeight = -1.0f;
    const float nearClear = luminanceAt(renderFloat(*renderer, s), kNearX, kMidY);
    CHECK(std::abs(nearClear - nearBuried) < 0.5f * std::abs(clear - buried));
}

// ---- ADR-705: one law, one density ---------------------------------------------------------------
//
// ADR-569 measured that the surface pass and the march were two laws with two densities that agreed
// at exactly one distance. ADR-705 unified them: the surface pass is Beer--Lambert with the march's
// own extinction, and it takes the air over where the march stops. These cases are the invariant.
//
// The instrument is the far box of `twoBoxScene`: unlit, so its pixel is its colour times whatever
// the air lets through; fog colour black and no scattering or emission, so nothing is ADDED and the
// ratio to the clear frame is the transmittance itself, from either pass.
namespace {

struct Transmittances {
    float marched;   // the march carries the whole ray (its reach is past the box)
    float surface;   // no march at all: the surface pass integrates the whole ray
    float handover;  // the march carries the first 12 m and the surface pass the rest
};

Transmittances farBoxTransmittance(rendering::SceneRenderer& renderer, float extinction, float horizon = 0.0f) {
    scene::Scene s = twoBoxScene();
    s.environment.fogColor = glm::vec3(0.0f);
    const float clear = luminanceAt(renderFloat(renderer, s), kFarX, kMidY);
    REQUIRE(clear > 0.05f);
    enableFog(s, extinction); // absorption 1, so density IS extinction
    s.environment.volumeScattering = 0.0f;
    s.environment.volumeSteps = 256; // the march's own quadrature error well under the tolerance
    s.environment.volumeJitter = 0.0f;
    s.environment.horizonDensity = horizon;
    Transmittances t{};
    s.environment.volumeMaxDistance = 120.0f;
    t.marched = luminanceAt(renderFloat(renderer, s), kFarX, kMidY) / clear;
    s.environment.volumeMaxDistance = 0.0f;
    t.surface = luminanceAt(renderFloat(renderer, s), kFarX, kMidY) / clear;
    s.environment.volumeMaxDistance = 12.0f;
    t.handover = luminanceAt(renderFloat(renderer, s), kFarX, kMidY) / clear;
    return t;
}

} // namespace

TEST_CASE("The surface fog and the march agree on the transmittance of the same air",
          "[volume][gpu][fog]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);

    // The far box's front face is 35 m down the axis; the sample pixel's ray reaches it at about
    // 36.5 m. Three extinctions, from light haze to most of the light gone.
    for (const float extinction : {0.01f, 0.033f, 0.06f}) {
        const Transmittances t = farBoxTransmittance(*renderer, extinction);
        const float expected = std::exp(-extinction * 36.5f);
        INFO("extinction " << extinction << ": marched " << t.marched << ", surface " << t.surface
                           << ", handover " << t.handover << ", Beer-Lambert " << expected);
        // The air is really there, and really thinner than nothing: without these the equalities
        // below would pass against a surface pass that did nothing or a march that stopped at once.
        REQUIRE(t.marched < 0.95f);
        REQUIRE(t.marched > 0.05f);
        // ONE LAW. The surface pass on its own attenuates this box exactly as the march does. The
        // exp-squared law this replaced gives exp(-(36.5 * sigma)^2): at 0.06 that is 0.008
        // against the march's 0.112, and at 0.01 it is 0.875 against 0.694 -- it fails at both
        // ends, which is ADR-569's 79 m crossover seen from either side.
        CHECK(t.surface == Catch::Approx(t.marched).epsilon(0.03));
        CHECK(t.surface == Catch::Approx(expected).epsilon(0.03));
        // ONE DENSITY, COUNTED ONCE. The march carrying 12 m and the surface pass the remaining
        // 24.5 multiply to the whole ray. Counting the near segment in both passes would give
        // exp(-sigma * 48.5) here instead -- 0.055 against 0.112 at 0.06.
        CHECK(t.handover == Catch::Approx(t.marched).epsilon(0.03));
    }
    CHECK(ctx->errorCount() == 0);
}

// ADR-705, §7's Horizon Density, on the unified law. Three properties: off is off, to the bit; more
// of it thickens the far air and barely the near; and it is still ONE law -- the march and the
// surface pass read the same number and agree on what it does.
TEST_CASE("Horizon Density at 0 renders exactly as the environment's default", "[volume][gpu][fog]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);
    // Both passes live: the march carries the first 12 m and the surface pass the rest, so the
    // zero has to be a no-op in both shaders, not only in the one that happens to see the box.
    scene::Scene s = twoBoxScene();
    enableFog(s, 0.03f);
    s.environment.volumeMaxDistance = 12.0f;
    REQUIRE(s.environment.horizonDensity == 0.0f); // the default is off
    const auto silent = renderFloat(*renderer, s);
    s.environment.horizonDensity = 0.0f;
    const auto spoken = renderFloat(*renderer, s);
    // ...and a control that moved nothing when non-zero would pass the equality vacuously.
    s.environment.horizonDensity = 1.0f;
    const auto on = renderFloat(*renderer, s);
    CHECK(gpu::hashImage(spoken) == gpu::hashImage(silent));
    CHECK(gpu::hashImage(on) != gpu::hashImage(silent));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Horizon Density thickens the far air monotonically and leaves the near air alone",
          "[volume][gpu][fog]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);

    scene::Scene s = twoBoxScene();
    s.environment.fogColor = glm::vec3(0.0f);
    enableFog(s, 0.02f);
    s.environment.volumeScattering = 0.0f;
    s.environment.volumeMaxDistance = 12.0f; // near box marched, far box handed over
    float lastFar = 2.0f;
    float nearAtZero = 0.0f;
    for (const float horizon : {0.0f, 2.0f, 4.0f, 8.0f}) {
        s.environment.horizonDensity = horizon;
        const auto image = renderFloat(*renderer, s);
        const float farL = luminanceAt(image, kFarX, kMidY);
        const float nearL = luminanceAt(image, kNearX, kMidY);
        if (horizon == 0.0f) {
            nearAtZero = nearL;
        }
        INFO("horizon " << horizon << ": near " << nearL << ", far " << farL);
        CHECK(farL < lastFar); // strictly darker every step
        // The near box is 4 m out, where the factor is 1 + 8 * 0.004 at most: a few percent.
        CHECK(nearL == Catch::Approx(nearAtZero).epsilon(0.05));
        lastFar = farL;
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Horizon Density is one law: the march and the surface fog agree on it", "[volume][gpu][fog]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);
    // At 36.5 m a horizon of 8 makes the air up to 1.29x as dense: optical depth x(1 + 8 * 36.5 / 2000).
    const float extinction = 0.03f;
    const float horizon = 8.0f;
    const Transmittances t = farBoxTransmittance(*renderer, extinction, horizon);
    const Transmittances off = farBoxTransmittance(*renderer, extinction, 0.0f);
    const float expected = std::exp(-extinction * 36.5f * (1.0f + horizon * 36.5f / 2000.0f));
    INFO("marched " << t.marched << ", surface " << t.surface << ", handover " << t.handover
                    << ", expected " << expected << ", without horizon " << off.marched);
    // The control does something in each pass, or the agreement below is two zeros agreeing.
    REQUIRE(t.marched < off.marched * 0.93f);
    REQUIRE(t.surface < off.surface * 0.93f);
    CHECK(t.surface == Catch::Approx(t.marched).epsilon(0.03));
    CHECK(t.surface == Catch::Approx(expected).epsilon(0.03));
    CHECK(t.handover == Catch::Approx(t.marched).epsilon(0.03));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("The styled ambient defaults are the constants the shader used to carry",
          "[volume][gpu][fog][stylized]") {
    // ADR-058 made three shader constants authorable on the promise that a scene naming none of
    // them renders as it did. Half of that promise is the defaults themselves: if they drift, every
    // styled scene in the repository quietly changes and no other test would notice.
    const scene::Environment defaults;
    CHECK(defaults.styledSkyAmbient == glm::vec3(0.38f, 0.56f, 0.65f));
    CHECK(defaults.styledGroundAmbient == glm::vec3(0.12f, 0.10f, 0.22f));
    CHECK(defaults.styledAmbientFloor == 0.68f);
    CHECK(defaults.fogHeightAmount == 0.0f);

    // The other half: naming them explicitly at those values changes nothing.
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);
    scene::Scene s = twoBoxScene();
    s.environment.stylized = true;
    s.environment.volumeMaxDistance = 0.0f; // ADR-705: surface pass alone, as above
    s.environment.volumeDensity = 0.0167f;
    const auto silent = renderFloat(*renderer, s);
    s.environment.styledSkyAmbient = defaults.styledSkyAmbient;
    s.environment.styledGroundAmbient = defaults.styledGroundAmbient;
    s.environment.styledAmbientFloor = defaults.styledAmbientFloor;
    const auto spoken = renderFloat(*renderer, s);
    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 0; x < kSize; ++x) {
            REQUIRE(luminanceAt(spoken, x, y) == luminanceAt(silent, x, y));
        }
    }
}

// ---- ADR-139: the march resolution and the step count are tier parameters -------------------
//
// Three properties, none of them a timing: the scale reaches the target and the target is the size
// the scale asked for; the fog the frame renders is still the same fog at every scale (the gate a
// timer cannot see is checked here as "distant surfaces are still fogged and near ones are not",
// which is what the pass is *for*); and at 1.0 the composite's bilinear footprint collapses, so the
// Offline tier's per-pixel march is not silently blurred back to half resolution.
TEST_CASE("The volumetric march resolution follows the quality tier", "[volume][gpu]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);

    scene::Scene s = twoBoxScene();
    enableFog(s, 0.04f);

    struct Case {
        float scale;
        std::uint32_t expectedWidth;
        std::uint32_t expectedHeight;
        bool belowFull;
    };
    // kSize is 128, so these are exact; `scaledOf` rounds up, which is what keeps 0.5 identical to
    // the historical `(v + 1) / 2` on odd sizes.
    const Case cases[] = {
        {1.0f, kSize, kSize, false},
        {0.5f, kSize / 2, kSize / 2, true},
        {0.25f, kSize / 4, kSize / 4, true},
    };
    for (const Case& c : cases) {
        INFO("volumeResolutionScale " << c.scale);
        rendering::QualitySettings q = renderer->qualitySettings();
        q.volumeResolutionScale = c.scale;
        renderer->setQualitySettings(q);
        const gpu::ImageF foggy = renderFloat(*renderer, s);

        // The scale reached the pass, and the record says what it reached it with.
        CHECK(renderer->stats().volume.marchWidth == c.expectedWidth);
        CHECK(renderer->stats().volume.marchHeight == c.expectedHeight);
        CHECK(renderer->stats().volume.halfResolution == c.belowFull);
        CHECK(renderer->stats().volume.resolutionScale == c.scale);

        // And it is still the same fog: the far box attenuated, the near one nearly untouched.
        // A scale that broke the march or the upsample would show up here and not in any timer.
        const float nearFog = luminanceAt(foggy, kNearX, kMidY);
        const float farFog = luminanceAt(foggy, kFarX, kMidY);
        INFO("near " << nearFog << " far " << farFog);
        CHECK(farFog < nearFog * 0.6f);
        CHECK(nearFog > 0.03f);
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("The quality tier scales the authored march step count", "[volume][gpu]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);

    scene::Scene s = twoBoxScene();
    enableFog(s, 0.04f); // volumeSteps = 48
    rendering::QualitySettings q = renderer->qualitySettings();

    q.volumeStepScale = 1.0f;
    renderer->setQualitySettings(q);
    (void)renderFloat(*renderer, s);
    CHECK(renderer->stats().volume.steps == 48);

    q.volumeStepScale = 0.5f;
    renderer->setQualitySettings(q);
    (void)renderFloat(*renderer, s);
    CHECK(renderer->stats().volume.steps == 24);

    // Clamped, not wrapped or zeroed: a march of no steps renders no fog at all, and a scale that
    // silently reached zero would look exactly like the pass being switched off.
    q.volumeStepScale = 0.001f;
    renderer->setQualitySettings(q);
    (void)renderFloat(*renderer, s);
    CHECK(renderer->stats().volume.steps >= 1);
    CHECK(ctx->errorCount() == 0);
}

// §5.9: an offline render takes no shortcut. The two tiers that stand for "the reference picture"
// march per pixel at the authored step count, and Preview -- the tier that is allowed to be cheap
// -- is the only one that reduces either. Asserted on the tier table rather than on a frame,
// because this is a statement about policy and a frame cannot distinguish 1.0 from 0.99.
TEST_CASE("Offline and High march the volume at full resolution", "[volume]") {
    using rendering::QualitySettings;
    using rendering::QualityTier;
    for (const QualityTier tier : {QualityTier::High, QualityTier::Offline}) {
        const QualitySettings q = QualitySettings::forTier(tier);
        CHECK(q.volumeResolutionScale == 1.0f);
        CHECK(q.volumeStepScale == 1.0f);
    }
    const QualitySettings realtime = QualitySettings::forTier(QualityTier::Realtime);
    CHECK(realtime.volumeResolutionScale == 0.5f); // exactly what shipped, and it must not move
    CHECK(realtime.volumeStepScale == 1.0f);
    const QualitySettings preview = QualitySettings::forTier(QualityTier::Preview);
    CHECK(preview.volumeResolutionScale < realtime.volumeResolutionScale);
    CHECK(preview.volumeStepScale < realtime.volumeStepScale);
}
