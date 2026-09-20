// Tier 0 vegetation motion on the GPU (ADR-055). shaders/wind.wgsl claims to be a transliteration
// of core/wind.cpp; these tests make it earn the claim by rendering a single upright stalk in a
// wind whose every stochastic term has been switched off, so the deformation reduces to one number
// the CPU can predict, and then measuring where the stalk's tip actually landed on screen.
//
// A wind with no regional variation, no gusts and no turbulence is not a wind anybody would ship.
// It is, however, the only version of one whose answer can be written down, which is exactly what
// a parity test needs.

#include "core/log.hpp"
#include "core/wind.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"
#include "support/image_diff.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

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

constexpr float kStalkHeight = 2.0f;
constexpr float kStalkRadius = 0.06f;

// The stalk is a thin cylinder, subdivided along its axis: a shape with no interior vertices
// would show a height-dependent bend only at its ends and would read as a shear. `makeCylinder`
// centres it on the origin, so its root is at -height/2 and its tip at +height/2.
scene::ProceduralGeometry stalk(const wind::VegetationMotion& motion) {
    scene::ProceduralGeometry g;
    g.name = "stalk";
    g.source.kind = scene::PrimitiveKind::Cylinder;
    g.source.radius = kStalkRadius;
    g.source.height = kStalkHeight;
    g.source.radialSegments = 10;
    g.source.heightSegments = 24;
    g.structureVersion = 1;
    g.meshHash = 0x571A1Bull;
    scene::InstanceRecord r{};
    r.position = {0.0f, 0.0f, 0.0f, 1.0f};
    r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    r.scale = {1.0f, 1.0f, 1.0f, 0.0f};
    r.random = {0.0f, 0.0f, 0.5f, 0.0f}; // random.z = 0.5 => the per-instance amplitude is exactly 1
    r.color = {1.0f, 1.0f, 1.0f, 0.0f};
    r.emissive = {1.0f, 1.0f, 1.0f, 0.0f};
    g.instances.push_back(r);
    // Flat white and unlit, with no emission at all: an emissive stalk gets a bloom halo, and the
    // outer edge of a halo is a convolution of the whole bar rather than the position of its tip --
    // which is how the first version of this test came to measure 44% of the right answer.
    g.material.baseColor = glm::vec3(1.0f);
    g.material.emissiveIntensity = 0.0f;
    g.material.unlit = true;
    g.motion = motion;
    return g;
}

// A wind with every stochastic term silenced: strength is exactly `speed` everywhere and for all
// time, there are no gusts, the direction never turns, and nothing flutters.
wind::WindParams steadyWind(float speed, float direction) {
    wind::WindParams w;
    w.enabled = true;
    w.direction = direction;
    w.speed = speed;
    w.regionAmount = 0.0f;
    w.regionDrift = 0.0f;
    w.turbulence = 0.0f; // also zeroes the flutter gain: nothing excites the plant's own mode
    w.gustAmount = 0.0f;
    return w;
}

wind::VegetationMotion softPlant() {
    wind::VegetationMotion m;
    m.stiffness = 1.0f;
    m.mass = 0.01f;
    m.damping = 0.3f;
    m.windSensitivity = 1.0f;
    m.tipAmplitude = 0.25f;
    m.bendLimit = 1.0f; // no ceiling: this test is about the amplitude, not about the clamp
    m.gustResponse = 1.0f;
    m.bendCurve = 2.0f;
    m.amplitudeVariance = 0.0f;
    return m;
}

// Camera down -Z at the stalk, so world +X is screen +x and world +Y is screen -y.
scene::Scene stalkScene(const wind::VegetationMotion& motion, const wind::WindParams& w) {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.environmentIntensity = 0.0f;
    s.environment.wind = w;
    s.camera.position = {0.0f, 0.0f, 7.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.procedurals.push_back(stalk(motion));
    return s;
}

gpu::Image8 render(gpu::Context& ctx, const scene::Scene& s, double time, std::uint32_t size = 512) {
    gpu::ShaderLibrary shaders(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FrameTime t{};
    t.renderTime = time;
    auto img = renderer.renderToImage(s, t, size, size);
    REQUIRE(img.has_value());
    return *img;
}

struct Band {
    bool any = false;
    float centroidX = 0.0f;
};

// The horizontal centroid of the lit pixels in rows [y0, y1).
Band band(const gpu::Image8& img, std::uint32_t y0, std::uint32_t y1) {
    double sum = 0.0;
    double weight = 0.0;
    for (std::uint32_t y = y0; y < std::min(y1, img.height); ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const auto* p = img.pixel(x, y);
            const double lum = static_cast<double>(p[0]) + p[1] + p[2];
            if (lum > 30.0) {
                sum += lum * x;
                weight += lum;
            }
        }
    }
    Band b;
    b.any = weight > 0.0;
    b.centroidX = b.any ? static_cast<float>(sum / weight) : 0.0f;
    return b;
}

// The rightmost column holding a lit pixel (-1 when nothing is lit).
int rightmostLit(const gpu::Image8& img) {
    for (int x = static_cast<int>(img.width) - 1; x >= 0; --x) {
        for (std::uint32_t y = 0; y < img.height; ++y) {
            const auto* p = img.pixel(static_cast<std::uint32_t>(x), y);
            if (p[0] + p[1] + p[2] > 30) {
                return x;
            }
        }
    }
    return -1;
}

// ADR-378. The rightmost lit column within `rows` rows of THIS image's own topmost lit row.
//
// `rightmostLit` scans the whole frame, and that is wrong for comparing a bent stalk with an
// upright one: the upright stalk's rightmost pixel is its full radius, found at mid-height, while
// the bent stalk's is the TIP, and a capped stalk's apex is narrower than its barrel. Differencing
// the two subtracts the tip's half-width from the barrel's radius as well as measuring the travel,
// which under-reports by that difference -- about 2 px here, systematically and always downward.
//
// Taking both from each image's own top rows measures the same part of the silhouette in both, so
// the radius genuinely cancels, which is what the test always claimed it did.
int rightmostLitAtTop(const gpu::Image8& img, int rows) {
    int top = -1;
    for (std::uint32_t y = 0; y < img.height && top < 0; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const auto* p = img.pixel(x, y);
            if (p[0] + p[1] + p[2] > 30) {
                top = static_cast<int>(y);
                break;
            }
        }
    }
    if (top < 0) {
        return -1;
    }
    const auto last = static_cast<std::uint32_t>(std::min<int>(top + rows, static_cast<int>(img.height)));
    for (int x = static_cast<int>(img.width) - 1; x >= 0; --x) {
        for (auto y = static_cast<std::uint32_t>(top); y < last; ++y) {
            const auto* p = img.pixel(static_cast<std::uint32_t>(x), y);
            if (p[0] + p[1] + p[2] > 30) {
                return x;
            }
        }
    }
    return -1;
}

// The leftmost column holding a lit pixel (image width when nothing is lit).
int leftmostLit(const gpu::Image8& img) {
    for (std::uint32_t x = 0; x < img.width; ++x) {
        for (std::uint32_t y = 0; y < img.height; ++y) {
            const auto* p = img.pixel(x, y);
            if (p[0] + p[1] + p[2] > 30) {
                return static_cast<int>(x);
            }
        }
    }
    return static_cast<int>(img.width);
}

// Rows of the image occupied by lit pixels.
std::pair<int, int> litRows(const gpu::Image8& img) {
    int lo = 1 << 30;
    int hi = -1;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const auto* p = img.pixel(x, y);
            if (p[0] + p[1] + p[2] > 30) {
                lo = std::min(lo, static_cast<int>(y));
                hi = std::max(hi, static_cast<int>(y));
                break;
            }
        }
    }
    return {lo, hi};
}

} // namespace

TEST_CASE("A stalk in a steady wind bends by exactly what the CPU model predicts", "[gpu][wind]") {
    auto ctx = makeContext();
    const wind::VegetationMotion plant = softPlant();
    const wind::WindParams w = steadyWind(0.8f, 0.0f); // straight along +X, i.e. screen right

    const scene::Scene calm = stalkScene(plant, wind::WindParams{});
    const scene::Scene blown = stalkScene(plant, w);
    const gpu::Image8 still = render(*ctx, calm, 0.0);
    const gpu::Image8 bent = render(*ctx, blown, 0.0);

    const auto [rowTop, rowBottom] = litRows(still);
    REQUIRE(rowBottom > rowTop + 40); // the stalk really is on screen and upright
    const float pixelsPerMetre = static_cast<float>(rowBottom - rowTop) / kStalkHeight;

    // The root band: the bottom eighth of the stalk. It must not move. This is the anchoring claim,
    // and it is the one that separates a bend from "rotate the whole mesh", which is what fake wind
    // always turns out to be when you look at where the stem meets the soil.
    const auto rootY0 = static_cast<std::uint32_t>(rowBottom - (rowBottom - rowTop) / 8);
    const Band rootStill = band(still, rootY0, static_cast<std::uint32_t>(rowBottom) + 1);
    const Band rootBent = band(bent, rootY0, static_cast<std::uint32_t>(rowBottom) + 1);
    REQUIRE(rootStill.any);
    REQUIRE(rootBent.any);
    REQUIRE(std::abs(rootBent.centroidX - rootStill.centroidX) < 1.5f);

    // The tip: the rightmost lit column in the whole image. The offset is monotonic in height and
    // the stalk's radius is constant, so the furthest-right pixel *is* the tip, and the difference
    // between the two images is the tip's travel with the radius cancelled out. A band centroid
    // would instead measure the average of the bend curve over whatever slice of the stalk the band
    // happened to catch, which is a different quantity and one that moves when the tip drops.
    const int tipStill = rightmostLitAtTop(still, 6);
    const int tipBent = rightmostLitAtTop(bent, 6);
    REQUIRE(tipStill > 0);
    REQUIRE(tipBent > tipStill);

    const wind::WindUniforms u = wind::packWind(w);
    const wind::MotionResponse r = wind::motionResponse(w, plant);
    const wind::WindSample s = wind::sampleWind(u, glm::vec3(0.0f), 0.0f);
    const glm::vec3 predicted =
        wind::vegetationDisplacement(kStalkHeight * 0.5f, -kStalkHeight * 0.5f, kStalkHeight, 1.0f, s, r,
                                     glm::vec4(0.0f, 0.0f, 0.5f, 0.0f), 0.0f);
    REQUIRE(predicted.x > 0.05f); // the CPU model says it leans, or this test proves nothing

    const float measured = static_cast<float>(tipBent - tipStill) / pixelsPerMetre;
    // ADR-378. The tolerance is in PIXELS, not per cent, because the error is in pixels.
    //
    // This used to be +-6% of the prediction, and it failed at 8.3% -- measured 24 px against a
    // predicted 26.2. Two hypotheses were wrong before the right one: not ADR-372's AgX curve (the
    // old tonemap swapped in through AVGEN_SHADER_DIR gives the same value byte for byte), and not
    // the per-instance random (forcing the shader to use the CPU model's `(0,0,0.5,0)` also gives
    // the same value byte for byte -- `amplitudeVariance` is 0 here, so it cannot matter).
    //
    // What it is: `tipBent - tipStill` is a difference of two THRESHOLDED silhouette edges, so its
    // error is roughly a pixel each way however large the displacement is. Run with the ceiling
    // made inert the same scene measures 30 px against 31.4 -- 1.4 px short and comfortably inside
    // 6%. Run with the ceiling active the displacement is only 24 px, the same ~2 px of edge
    // quantisation is now 8.3% of it, and a relative tolerance fails a shader that is doing exactly
    // the right arithmetic. The bug was in the tolerance's FORM: a percentage of a quantity that
    // shrinks, guarding an error that does not.
    //
    // With both edges taken from the apex the residual is -1.17 px, which is edge quantisation and
    // nothing else. 2.5 px is what that can resolve, and it is a SYMMETRIC guard: perturbing the
    // shader'"'"'s amplitude by +15% gives +2.83 px and by -15% gives -3.17 px, and both fail. The old
    // relative form passed +15%, because the 2 px bias cancelled the error -- a guard that is blind
    // in one direction is worse than a loose one, and that is the part worth keeping in mind.
    const float measuredPx = static_cast<float>(tipBent - tipStill);
    const float predictedPx = predicted.x * pixelsPerMetre;
    INFO("measured " << measuredPx << " px, predicted " << predictedPx << " px");
    REQUIRE(std::abs(measuredPx - predictedPx) < 2.5f);

    // ...and the claim this test exists to make is that the SHADER runs `core/wind.cpp`'s
    // arithmetic, so it has to fail when it does not. A 15% error in the shader's amplitude moves
    // the tip by about 4 px here, which this catches and which the old +-6% also would have. The
    // point of stating it is that 2.5 px is not a licence: anything that changes the transfer
    // function by more than a few per cent still trips it.
    REQUIRE(predictedPx > 20.0f); // or 2.5 px would be a loose relative bound after all
}

TEST_CASE("A stalk bends the way the wind is pointing", "[gpu][wind]") {
    auto ctx = makeContext();
    const wind::VegetationMotion plant = softPlant();
    const gpu::Image8 east = render(*ctx, stalkScene(plant, steadyWind(0.8f, 0.0f)), 0.0);
    const gpu::Image8 west = render(*ctx, stalkScene(plant, steadyWind(0.8f, 3.14159265f)), 0.0);

    // Reversing the wind reverses the lean: the tip that reached furthest right now reaches
    // furthest left, by the same distance from the still stalk.
    const gpu::Image8 calm = render(*ctx, stalkScene(plant, wind::WindParams{}), 0.0);
    const int stillRight = rightmostLit(calm);
    const int stillLeft = leftmostLit(calm);
    REQUIRE(rightmostLit(east) - stillRight > 8);
    REQUIRE(stillLeft - leftmostLit(west) > 8);
    REQUIRE(std::abs((rightmostLit(east) - stillRight) - (stillLeft - leftmostLit(west))) <= 2);
}

TEST_CASE("Wind off is byte-identical to wind never having existed", "[gpu][wind][determinism]") {
    auto ctx = makeContext();
    // A layer that says nothing about motion, in a world whose wind is switched off, must render
    // exactly as it did before any of this was written -- and the same two frames apart, because a
    // scene with no wind in it is a scene that does not move.
    const scene::Scene calm = stalkScene(wind::VegetationMotion{}, wind::WindParams{});
    const gpu::Image8 a = render(*ctx, calm, 0.0);
    const gpu::Image8 b = render(*ctx, calm, 4.0);
    // ADR-362: a count and an offset, not `a.rgba == b.rgba`. This assertion has failed in a
    // full-suite run and the equality form took the whole binary down with it (ADR-358), so the
    // difference it found was never printed.
    {
        const auto d = testing::byteDiff(a.rgba, b.rgba);
        INFO("calm at 0 s vs 4 s: " << d.describe());
        REQUIRE(d.identical());
    }

    // And a plant that *would* sway does not, when the air is still.
    const scene::Scene stillAir = stalkScene(softPlant(), wind::WindParams{});
    const gpu::Image8 c = render(*ctx, stillAir, 0.0);
    const gpu::Image8 d = render(*ctx, stillAir, 4.0);
    {
        const auto diff = testing::byteDiff(c.rgba, d.rgba);
        INFO("still air at 0 s vs 4 s: " << diff.describe());
        REQUIRE(diff.identical());
    }
}

TEST_CASE("The GPU wind field is deterministic in time", "[gpu][wind][determinism]") {
    auto ctx = makeContext();
    // A full field, gusts and all: the same render time must give the same frame, twice, from a
    // fresh renderer. Offline rendering of a moving world depends on exactly this.
    wind::WindParams w;
    w.enabled = true;
    w.speed = 0.9f;
    w.direction = 0.6f;
    const scene::Scene s = stalkScene(softPlant(), w);
    const gpu::Image8 a = render(*ctx, s, 3.25);
    const gpu::Image8 b = render(*ctx, s, 3.25);
    {
        const auto d = testing::byteDiff(a.rgba, b.rgba);
        INFO("same time twice: " << d.describe());
        REQUIRE(d.identical());
    }
    // ... and a different time gives a different frame, or nothing is moving at all. This is the
    // control: it must report a non-zero count, and the count is worth seeing when it does not.
    const gpu::Image8 c = render(*ctx, s, 4.75);
    {
        const auto d = testing::byteDiff(a.rgba, c.rgba);
        INFO("3.25 s vs 4.75 s: " << d.describe());
        REQUIRE_FALSE(d.identical());
    }
}
