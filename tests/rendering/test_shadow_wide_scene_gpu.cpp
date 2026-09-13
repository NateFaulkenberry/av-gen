// A wide scene of small objects has to get cast shadows (ADR-112).
//
// The failure this pins: the shadowed range used to be three *scene* radii, which is the world's
// size and not the camera's need, and a camera frustum widens linearly with distance -- so a 2.4 km
// landscape fitted its last cascade to a bounding sphere most of a kilometre across and got a
// 2.36 m texel. Every prop in it smaller than a garden shed was a fraction of a texel and cast
// nothing, and props are what a landscape is made of.
//
// The measurement is a differential and deliberately blunt: the same scene rendered with the key
// light's `castsShadow` on and off, and how much of the *ground* got darker. A scene whose objects
// cast no shadows renders the same either way. `QualitySettings::shadowTexelTarget` at zero is the
// pre-ADR-112 renderer exactly, so it is the control arm throughout.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/render_quality.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

using namespace avgen;

namespace {

constexpr std::uint32_t kSize = 320;

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

std::unique_ptr<rendering::SceneRenderer> makeRenderer(gpu::Context& ctx, gpu::ShaderLibrary& shaders,
                                                      float texelTarget) {
    auto renderer = std::make_unique<rendering::SceneRenderer>(ctx, shaders);
    REQUIRE(renderer->init().has_value());
    // The map term is the subject; the contact march and the occlusion term are not, and both would
    // darken the ground around an object whether or not it cast a shadow.
    rendering::QualitySettings quality =
        rendering::QualitySettings::forTier(rendering::QualityTier::Realtime);
    quality.contactShadows = false;
    quality.ambientOcclusion = false;
    quality.shadowTexelTarget = texelTarget;
    renderer->setQualitySettings(quality);
    return renderer;
}

FrameTime frameAt(std::uint64_t index) {
    FrameTime t;
    t.frameIndex = index;
    t.renderTime = static_cast<double>(index) / 60.0;
    t.deltaTime = 1.0 / 60.0;
    return t;
}

scene::MeshData boxMesh(glm::vec3 half) {
    scene::MeshData m;
    const glm::vec3 n[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (const glm::vec3 normal : n) {
        const glm::vec3 u =
            std::abs(normal.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::cross(glm::vec3(0, 1, 0), normal);
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

struct WideSceneDesc {
    bool castsShadow = true;
    float worldHalfExtent = 400.0f; // a landscape, not a room
    float propHalfSize = 0.35f;     // a prop the size of a rock, not a building
    // gridSide^2 props. Kept under the renderer's 256-visible-entity cap, with the ground, so the
    // measurement is of shadows and not of entities the renderer quietly declined to draw.
    int gridSide = 15;
    float propSpacing = 2.2f;
    float cameraHeight = 6.0f;
    // How far down the field the rows of props run, as a multiple of their spacing. Above 1 the
    // field reaches past the end of the shadowed range, which is what the range-fade case needs.
    float depthStretch = 1.0f;
};

// A wide ground plane with a field of small props standing on it, and one directional light at a
// workable angle. The camera stands among the props and looks down the field, so the frame holds
// props at every distance from a couple of metres to a couple of hundred -- which is the case that
// forces the cascades to cover a long range and is where they stop resolving anything.
scene::Scene wideScene(const WideSceneDesc& desc) {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.environmentIntensity = 0.0f;
    s.camera.position = {0.0f, desc.cameraHeight, 30.0f};
    s.camera.target = {0.0f, 1.0f, -40.0f};
    s.camera.fovYRadians = 0.9f;
    s.camera.nearPlane = 0.5f;
    s.camera.farPlane = desc.worldHalfExtent * 2.5f;

    const auto ground = s.addMesh(boxMesh({desc.worldHalfExtent, 0.25f, desc.worldHalfExtent}));
    const auto prop = s.addMesh(boxMesh(glm::vec3(desc.propHalfSize)));
    {
        auto& e = s.addEntity("ground", ground);
        e.transform.position = {0.0f, -0.25f, 0.0f};
        e.material.baseColor = glm::vec3(0.85f);
        e.material.roughness = 0.95f;
        e.material.metallic = 0.0f;
    }
    // A jittered grid, so the props do not line up into rows that a cascade could resolve by luck.
    std::uint32_t rng = 0x9E3779B9u;
    auto next = [&] {
        rng = rng * 1664525u + 1013904223u;
        return static_cast<float>((rng >> 8) & 0xFFFFu) / 65535.0f - 0.5f;
    };
    for (int iz = 0; iz < desc.gridSide; ++iz) {
        for (int ix = 0; ix < desc.gridSide; ++ix) {
            auto& e = s.addEntity("prop", prop);
            const float x = (static_cast<float>(ix) - static_cast<float>(desc.gridSide) * 0.5f) *
                            desc.propSpacing;
            const float z = 20.0f - static_cast<float>(iz) * desc.propSpacing * 2.0f * desc.depthStretch;
            e.transform.position = {x + next() * desc.propSpacing * 0.6f, desc.propHalfSize,
                                    z + next() * desc.propSpacing * 0.6f * desc.depthStretch};
            e.material.baseColor = glm::vec3(0.85f);
            e.material.roughness = 0.95f;
        }
    }
    scene::PunctualLight key;
    key.name = "key";
    key.type = scene::PunctualLight::Type::Directional;
    // Low enough that each prop throws a shadow several times its own width along the ground, so
    // there is a large signal to find: a prop that casts nothing is unmistakable.
    key.direction = glm::normalize(glm::vec3(0.25f, -0.5f, -0.83f));
    key.color = glm::vec3(1.0f);
    key.intensity = 4.0f;
    key.castsShadow = desc.castsShadow;
    key.contactShadow = false;
    key.softness = 0.25f;
    s.addLight(key);
    return s;
}

float luminanceAt(const gpu::Image8& image, std::uint32_t x, std::uint32_t y) {
    const std::size_t index = (static_cast<std::size_t>(y) * image.width + x) * 4;
    if (index + 3 > image.rgba.size()) {
        return 0.0f;
    }
    return (0.2126f * static_cast<float>(image.rgba[index]) +
            0.7152f * static_cast<float>(image.rgba[index + 1]) +
            0.0722f * static_cast<float>(image.rgba[index + 2])) /
           255.0f;
}

// How much of the frame the cast shadows actually darken: the fraction of pixels that got at least
// `drop` darker once the light started casting. Counting darkened pixels rather than a mean is what
// makes this a measure of *coverage* -- a handful of very dark pixels and a wash of slightly darker
// ones have the same mean and are not the same picture.
struct ShadowCoverage {
    float darkenedFraction = 0.0f;
    float meanDarkening = 0.0f;
    float litFraction = 0.0f; // share of the band that was lit at all, the "is anything there" arm
};

ShadowCoverage shadowCoverage(const gpu::Image8& unshadowed, const gpu::Image8& shadowed,
                              float drop = 0.02f, float y0 = 0.0f, float y1 = 1.0f) {
    const auto top = static_cast<std::uint32_t>(y0 * static_cast<float>(unshadowed.height));
    const auto bottom = static_cast<std::uint32_t>(y1 * static_cast<float>(unshadowed.height));
    std::uint64_t darkened = 0;
    std::uint64_t counted = 0;
    double total = 0.0;
    for (std::uint32_t y = top; y < bottom && y < unshadowed.height; ++y) {
        for (std::uint32_t x = 0; x < unshadowed.width; ++x) {
            const float lit = luminanceAt(unshadowed, x, y);
            if (lit < 0.01f) {
                continue; // sky, or something unlit: there is nothing to darken
            }
            ++counted;
            const float delta = lit - luminanceAt(shadowed, x, y);
            total += static_cast<double>(std::max(delta, 0.0f));
            if (delta > drop) {
                ++darkened;
            }
        }
    }
    ShadowCoverage out;
    const auto inBand = static_cast<std::uint64_t>(bottom - top) * unshadowed.width;
    out.litFraction = inBand > 0 ? static_cast<float>(counted) / static_cast<float>(inBand) : 0.0f;
    if (counted > 0) {
        out.darkenedFraction = static_cast<float>(darkened) / static_cast<float>(counted);
        out.meanDarkening = static_cast<float>(total / static_cast<double>(counted));
    }
    return out;
}

struct Bands {
    float range = 0.0f;
    float coarsestTexel = 0.0f;
    float litFraction = 0.0f;
    ShadowCoverage all;
    ShadowCoverage near;  // the bottom third of the frame: ground a few metres away
    ShadowCoverage far;   // the band just below the horizon: ground tens to hundreds of metres away
};

Bands measure(gpu::Context& ctx, gpu::ShaderLibrary& shaders, const WideSceneDesc& desc,
                       std::uint64_t frame, float texelTarget = 0.08f) {
    WideSceneDesc off = desc;
    off.castsShadow = false;
    const scene::Scene lit = wideScene(off);
    const scene::Scene shadowed = wideScene(desc);
    // Two renderers, because SceneRenderer caches meshes and textures against the address of the
    // scene it uploaded them from and these two scenes are alive at once anyway.
    auto a = makeRenderer(ctx, shaders, texelTarget);
    auto unshadowed = a->renderToImage(lit, frameAt(frame), kSize, kSize);
    REQUIRE(unshadowed.has_value());
    auto b = makeRenderer(ctx, shaders, texelTarget);
    auto dark = b->renderToImage(shadowed, frameAt(frame), kSize, kSize);
    REQUIRE(dark.has_value());
    Bands out;
    out.range = b->shadows().stats().range;
    out.coarsestTexel = b->shadows().stats().coarsestTexel;
    out.all = shadowCoverage(*unshadowed, *dark);
    out.near = shadowCoverage(*unshadowed, *dark, 0.02f, 0.7f, 1.0f);
    out.far = shadowCoverage(*unshadowed, *dark, 0.02f, 0.42f, 0.55f);
    out.litFraction = out.all.litFraction;
    return out;
}

} // namespace

TEST_CASE("a wide scene of small objects casts shadows", "[gpu][shadows][cascades][wide]") {
    // The defect, and the fix, in one measurement. The same 1200 m field of 70 cm props, rendered
    // with the range rule off (which is exactly the pre-ADR-112 renderer) and on.
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);

    WideSceneDesc desc;
    desc.worldHalfExtent = 1200.0f;
    const Bands before = measure(*ctx, shaders, desc, 31, 0.0f);
    const Bands after = measure(*ctx, shaders, desc, 31, 0.08f);

    INFO("range " << before.range << " -> " << after.range << " m, coarsest texel "
                  << before.coarsestTexel * 100.0f << " -> " << after.coarsestTexel * 100.0f
                  << " cm; ground darkened " << before.near.darkenedFraction * 100.0f << " -> "
                  << after.near.darkenedFraction * 100.0f << "% near, "
                  << before.far.darkenedFraction * 100.0f << " -> "
                  << after.far.darkenedFraction * 100.0f << "% far");

    // The cause: the coarsest cascade's texel was two and a third metres across, so a 70 cm prop
    // was a third of a texel and cast nothing.
    REQUIRE(before.coarsestTexel > 1.0f);
    CHECK(after.coarsestTexel < 0.08f);

    // The effect. Measured: 1.81% of the near ground darkened before, 5.30% after; 0.21% of the far
    // ground before, 0.58% after.
    CHECK(after.near.darkenedFraction > before.near.darkenedFraction * 2.0f);
    CHECK(after.far.darkenedFraction > before.far.darkenedFraction * 1.8f);

    // The control: the props are there and lit in both arms, so the numbers above are a difference
    // in shadows and not a difference in what got drawn.
    CHECK(before.litFraction > 0.2f);
    CHECK(std::abs(after.litFraction - before.litFraction) < 0.02f);
}

TEST_CASE("the shadow fit no longer depends on how wide the world is",
          "[gpu][shadows][cascades][wide]") {
    // The sharper statement of the same fix. The camera, the props and the light are identical in
    // all three arms; only the ground plane's extent differs, which is the only thing that used to
    // reach the cascade fit -- through `sceneRadius * 3`. A 120 m world and a 1200 m world should
    // shadow the ground in front of the camera identically, and before ADR-112 they differed by a
    // factor of three.
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);

    std::vector<float> before;
    std::vector<float> after;
    for (const float extent : {120.0f, 400.0f, 1200.0f}) {
        WideSceneDesc d;
        d.worldHalfExtent = extent;
        before.push_back(measure(*ctx, shaders, d, 31, 0.0f).near.darkenedFraction);
        after.push_back(measure(*ctx, shaders, d, 31, 0.08f).near.darkenedFraction);
    }
    auto spread = [](const std::vector<float>& v) {
        const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
        return *hi / std::max(*lo, 1e-6f);
    };
    INFO("near-ground coverage over 120/400/1200 m worlds: before "
         << before[0] * 100.0f << "/" << before[1] * 100.0f << "/" << before[2] * 100.0f
         << "%, after " << after[0] * 100.0f << "/" << after[1] * 100.0f << "/" << after[2] * 100.0f
         << "%");
    CHECK(spread(before) > 2.0f);  // it did depend on the world's width: 5.27 / 4.53 / 1.81
    CHECK(spread(after) < 1.05f);  // and now it does not: 5.30 / 5.30 / 5.30
}

TEST_CASE("a small scene is left exactly as it was", "[gpu][shadows][cascades][wide]") {
    // The rule is a ceiling, never a reason to reach further, so a scene that already asked for less
    // than it allows must render identically -- not similarly, identically. Everything that worked
    // before ADR-112 has to keep working, and this is the arm that says so.
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);

    WideSceneDesc d;
    d.worldHalfExtent = 25.0f;
    const Bands before = measure(*ctx, shaders, d, 31, 0.0f);
    const Bands after = measure(*ctx, shaders, d, 31, 0.08f);
    INFO("range " << before.range << " -> " << after.range << " m");
    CHECK(before.range == after.range);
    CHECK(before.coarsestTexel == after.coarsestTexel);
    CHECK(before.near.darkenedFraction == after.near.darkenedFraction);
    CHECK(before.all.darkenedFraction == after.all.darkenedFraction);

    // Not vacuous: the same comparison on a wide world moves.
    WideSceneDesc wide;
    wide.worldHalfExtent = 1200.0f;
    CHECK(measure(*ctx, shaders, wide, 31, 0.0f).range !=
          measure(*ctx, shaders, wide, 31, 0.08f).range);
}

TEST_CASE("the shadowed range is the same at every quality tier", "[gpu][shadows][cascades][wide]") {
    // Where the shadows stop is composition. A preview whose shadows ended at 40 m and a final whose
    // shadows ended at 150 m would not be the same shot, so the range is sized against a fixed
    // reference resolution and only the sharpness follows the tier.
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    const scene::Scene s = wideScene(WideSceneDesc{});

    float reference = -1.0f;
    float previewTexel = 0.0f;
    float offlineTexel = 0.0f;
    for (const auto tier : {rendering::QualityTier::Preview, rendering::QualityTier::Realtime,
                            rendering::QualityTier::High, rendering::QualityTier::Offline}) {
        auto renderer = std::make_unique<rendering::SceneRenderer>(*ctx, shaders);
        REQUIRE(renderer->init().has_value());
        rendering::QualitySettings q = rendering::QualitySettings::forTier(tier);
        q.contactShadows = false;
        q.ambientOcclusion = false;
        renderer->setQualitySettings(q);
        auto image = renderer->renderToImage(s, frameAt(32), kSize, kSize);
        REQUIRE(image.has_value());
        const auto& stats = renderer->shadows().stats();
        INFO("tier map " << stats.resolution << ": range " << stats.range << " m, coarsest texel "
                         << stats.coarsestTexel * 100.0f << " cm");
        if (reference < 0.0f) {
            reference = stats.range;
            previewTexel = stats.coarsestTexel;
        }
        offlineTexel = stats.coarsestTexel;
        CHECK(stats.range == reference);
    }
    // And the control: the tiers really do differ in resolution, so the equality above is a
    // property of the range rule and not of four identical renders.
    INFO("coarsest texel, preview " << previewTexel * 100.0f << " cm -> offline "
                                    << offlineTexel * 100.0f << " cm");
    CHECK(offlineTexel < previewTexel * 0.5f);
}

// A single long wall running away from the camera, throwing one continuous stripe of shadow across
// the ground beside it. The point is that the darkening profile *down the frame* is smooth by
// construction -- there is exactly one caster and it is the same at every depth -- so any step in
// that profile is the shadow system's and not the scene's. A field of separate props cannot do this
// job: where its first row begins is itself a step, and a larger one than the effect being looked
// for (measured there: 45x the median row-to-row change, and identical with the fade on and off).
scene::Scene wallScene(bool castsShadow) {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.environmentIntensity = 0.0f;
    s.camera.position = {6.0f, 3.0f, 40.0f};
    s.camera.target = {0.0f, 0.0f, -200.0f};
    s.camera.fovYRadians = 0.9f;
    s.camera.nearPlane = 0.5f;
    s.camera.farPlane = 1200.0f;

    const auto ground = s.addMesh(boxMesh({1200.0f, 0.25f, 1200.0f}));
    const auto wall = s.addMesh(boxMesh({0.4f, 2.5f, 400.0f}));
    {
        auto& e = s.addEntity("ground", ground);
        e.transform.position = {0.0f, -0.25f, 0.0f};
        e.material.baseColor = glm::vec3(0.85f);
        e.material.roughness = 0.95f;
    }
    {
        auto& e = s.addEntity("wall", wall);
        e.transform.position = {-3.0f, 2.5f, -340.0f}; // z = +60 to z = -740
        e.material.baseColor = glm::vec3(0.85f);
        e.material.roughness = 0.95f;
    }
    scene::PunctualLight key;
    key.name = "key";
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(0.6f, -0.5f, 0.0f)); // across the wall, towards the camera side
    key.color = glm::vec3(1.0f);
    key.intensity = 4.0f;
    key.castsShadow = castsShadow;
    key.contactShadow = false;
    key.softness = 0.25f;
    s.addLight(key);
    return s;
}

TEST_CASE("shadows fade out at the end of the range rather than stopping",
          "[gpu][shadows][cascades][wide]") {
    // Shortening the range moved its far edge into the picture. Past the last cascade a fragment
    // reads as fully lit, so without a fade the ground would go from shadowed to clear over one row
    // of pixels -- a line across the frame that sweeps as the camera moves.
    //
    // The measurement walks down the frame row by row over ground that recedes from the camera and
    // asks how much the wall's shadow darkens each row. A step is one row-to-row jump much larger
    // than its neighbours, so the test compares the largest jump against the typical one rather
    // than against an absolute number, which would only be a statement about this scene's
    // brightness.
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);

    const scene::Scene unshadowedScene = wallScene(false);
    const scene::Scene shadowedScene = wallScene(true);
    auto a = makeRenderer(*ctx, shaders, 0.08f);
    auto unshadowed = a->renderToImage(unshadowedScene, frameAt(33), kSize, kSize);
    REQUIRE(unshadowed.has_value());
    auto b = makeRenderer(*ctx, shaders, 0.08f);
    auto shadowed = b->renderToImage(shadowedScene, frameAt(33), kSize, kSize);
    REQUIRE(shadowed.has_value());
    INFO("shadowed range " << b->shadows().stats().range << " m");

    std::vector<float> profile;
    for (std::uint32_t y = kSize / 2; y < kSize; ++y) {
        double total = 0.0;
        std::uint32_t counted = 0;
        for (std::uint32_t x = 0; x < kSize; ++x) {
            const float base = luminanceAt(*unshadowed, x, y);
            if (base < 0.01f) {
                continue;
            }
            total += static_cast<double>(std::max(base - luminanceAt(*shadowed, x, y), 0.0f));
            ++counted;
        }
        if (counted > kSize / 4) {
            profile.push_back(static_cast<float>(total / counted));
        }
    }
    REQUIRE(profile.size() > 40);

    // Deliberately *not* smoothed first, which was tried and is wrong: a three-row box filter
    // spreads a one-row step over three rows and divides its height by three, so it suppresses
    // exactly the thing being looked for. With it in place the fade-off arm passed.
    std::vector<float> jumps;
    for (std::size_t i = 1; i < profile.size(); ++i) {
        jumps.push_back(std::abs(profile[i] - profile[i - 1]));
    }
    std::vector<float> sorted = jumps;
    std::sort(sorted.begin(), sorted.end());
    const float median = sorted[sorted.size() / 2];
    const float worst = sorted.back();
    INFO("row-to-row change in darkening: median " << median << ", worst " << worst << " ("
                                                   << worst / std::max(median, 1e-6f) << "x)");
    CHECK(*std::max_element(profile.begin(), profile.end()) > 0.005f); // there is a shadow at all
    // Measured on this scene: 17.3x the median row-to-row change with the fade switched off, 12.3x
    // with it at 0.18, 9.7x at 0.35. The floor is not zero and never will be -- the per-row dither
    // of the PCF rotation puts a few times the median into the profile on its own -- so the
    // threshold sits between the two arms rather than near either. Both numbers are exact: these
    // renders are deterministic, and the same build produces the same figure every run.
    CHECK(worst < median * 15.0f);
}

