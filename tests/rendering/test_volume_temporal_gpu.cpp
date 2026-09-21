// Temporal stability of the volumetric march (§29), measured rather than described.
//
// **The brief is unusually blunt about this one.** §29: *"Mandatory. No crawling noise,
// shimmering, temporal popping, density flicker, unstable shadows or animated grain."* Six named
// artefacts, and the march has a mechanism for the last one by construction: `stepJitter` hashes
// the pixel **and the frame nonce**, and `FrameTime::frameNonce` is a function of `renderTime`. So
// the per-pixel march offset changes every frame, and `rendering/volume_renderer.cpp` has no
// history buffer to resolve it against -- ADR-143 rejected reprojection and ADR-460 re-confirmed
// the reopening trigger is shut. **Animated grain is not a risk here; it is the design.**
//
// Whether that is acceptable is a judgement about a picture. **How much of it there is, is a
// number**, and this file is the instrument for it.
//
// **The subject is held still on purpose, and that took two attempts.** The first measurement ran
// on the Tree of Life project over a third of a second and came back with a mean per-pixel
// temporal standard deviation of 8.17 levels with the jitter on and 2.10 with it off -- until the
// control asked whether consecutive frames were identical with the jitter OFF, and the answer was
// that they differ by up to 212 levels. The camera was moving. **The whole measurement was of
// camera motion, and the jitter arm differed from the static arm for a reason that had nothing to
// do with jitter.** So the scene here has a fixed camera, unlit boxes, no wind, no environment
// noise and a medium with no drift and no detail -- which makes the field a constant in time, so
// the ONLY thing `renderTime` can change is the jitter.
//
// How each case fails: remove `frameIndex` from `stepJitter`'s hash and case 2's measured grain
// goes to zero (which is the change §29 would be arguing for -- see ADR-577); make the jitter
// ignore `depthParams.z` and case 1 fails because 0 no longer means off.

#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"
#include "world/atmospherics.hpp"
#include "world/world_effects/effect_registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <memory>
#include <cstdio>
#include <vector>

using namespace avgen;

namespace {

constexpr std::uint32_t kSize = 96;

std::unique_ptr<gpu::Context> makeContext() {
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

// A medium that is a CONSTANT IN TIME: no drift, no detail, no breath. `fogShapeAt` is then a pure
// function of position, so `renderTime` reaches the picture through exactly one path -- the
// march's own per-pixel start offset.
scene::Scene stillScene(float jitter, float density = 1.4f, int steps = 32) {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.volumeDensity = 0.0f;   // no environment fog, and so no environment noise either
    s.environment.volumeScattering = 1.0f;
    s.environment.volumeAbsorption = 1.0f;
    s.environment.volumeAnisotropy = 0.0f;
    s.environment.volumeSteps = steps;    // 32 is the SHIPPED count: §29 is about what ships
    s.environment.volumeMaxDistance = 1200.0f;
    s.environment.volumeJitter = jitter;

    s.camera.position = {0.0f, 0.0f, 600.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.camera.fovYRadians = 0.87266f;
    s.camera.nearPlane = 1.0f;
    s.camera.farPlane = 2000.0f;

    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(-0.3f, -0.4f, -1.0f));
    key.intensity = 5.0f;
    key.volumetricStrength = 1.0f;
    s.addLight(key);

    world::AtmosphericEffect e =
        world::makeAtmosphericEffect(world::AtmosphereKind::VolumetricFog, "still");
    world::Vortex& v = e.vortex;
    v.field.center = {0.0f, 0.0f, 0.0f};
    v.field.radius = 170.0f;
    v.field.thickness = 130.0f;
    v.field.cloudNoise = 0.0f;      // no detail: nothing in the field is a function of time
    v.field.rotationSpeed = 0.0f;   // and nothing turns
    v.field.breathAmount = 0.0f;
    v.density = density;
    v.emission = 0.0f;
    v.scattering = 1.0f;
    e.values.setFloat("fog/shape", 1.0f); // a sphere
    e.values.setFloat("fog/edgeSoftness", 0.45f);
    e.values.setFloat("fog/driftSpeed", 0.0f);
    e.values.setFloat("fog/driftVertical", 0.0f);
    s.atmospherics.mediumCount = 1;
    world::packMediumSlot(e, 1.0f, s.atmospherics.media[0]);
    return s;
}

float luminanceAt(const gpu::ImageF& image, std::uint32_t x, std::uint32_t y) {
    const float* p = image.pixel(x, y);
    return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
}

std::vector<gpu::ImageF> sequence(rendering::SceneRenderer& renderer, const scene::Scene& s,
                                  int frames) {
    std::vector<gpu::ImageF> out;
    for (int i = 0; i < frames; ++i) {
        FrameTime time{};
        // A different SECOND each frame, because `FrameTime::frameNonce` is a function of
        // `renderTime` and not of `frameIndex` -- reading that rather than assuming it is the
        // difference between varying the jitter and varying nothing at all.
        time.renderTime = 1.0 + 0.05 * static_cast<double>(i);
        time.deltaTime = 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(i);
        auto image = renderer.renderToImageFloat(s, time, kSize, kSize);
        REQUIRE(image.has_value());
        out.push_back(std::move(*image));
    }
    return out;
}

// Mean per-pixel temporal standard deviation over the frame, in luminance, plus the share of
// pixels that move at all. The mean is the honest headline: a worst-case pixel on a silhouette
// says more about the edge than about the grain.
struct Temporal {
    double meanSd = 0.0;
    double movedFraction = 0.0;
    double peak = 0.0;
};

Temporal temporalOf(const std::vector<gpu::ImageF>& frames) {
    Temporal t;
    std::size_t counted = 0;
    std::size_t moved = 0;
    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 0; x < kSize; ++x) {
            double sum = 0.0;
            double lo = 1e30;
            double hi = -1e30;
            for (const auto& f : frames) {
                const double v = luminanceAt(f, x, y);
                sum += v;
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            const double mean = sum / static_cast<double>(frames.size());
            double var = 0.0;
            for (const auto& f : frames) {
                const double d = luminanceAt(f, x, y) - mean;
                var += d * d;
            }
            const double sd = std::sqrt(var / static_cast<double>(frames.size()));
            t.meanSd += sd;
            t.peak = std::max(t.peak, sd);
            if (hi - lo > 1e-6) {
                ++moved;
            }
            ++counted;
        }
    }
    t.meanSd /= static_cast<double>(counted);
    t.movedFraction = static_cast<double>(moved) / static_cast<double>(counted);
    return t;
}

// Mean |neighbour difference| over lit pixels -- a crude high-pass, and the SPATIAL half of what
// the jitter does. The temporal measure above answers "does it crawl"; this one answers "is it
// speckled in a single frame", and §23 and §29 between them care about both. Measuring one and
// reporting it as "the grain" is the mistake this function exists to prevent.
double spatialGrain(const gpu::ImageF& f) {
    double sum = 0.0;
    std::size_t n = 0;
    for (std::uint32_t y = 1; y < kSize; ++y) {
        for (std::uint32_t x = 1; x < kSize; ++x) {
            const double v = luminanceAt(f, x, y);
            if (v <= 0.001) {
                continue;
            }
            sum += std::abs(v - luminanceAt(f, x - 1, y)) + std::abs(v - luminanceAt(f, x, y - 1));
            n += 2;
        }
    }
    return n > 0 ? sum / static_cast<double>(n) : 0.0;
}

} // namespace

TEST_CASE("with the march jitter off, a still medium is perfectly still", "[gpu][volume][temporal]") {
    // The control on the instrument AND the property. `volumeJitter` 0 means "start every pixel at
    // the middle of its first step" (ADR-461), which removes the only per-frame term in this
    // scene -- so the eight frames must be IDENTICAL, not nearly. If they are not, something else
    // in the pipeline is a function of time and the measurement below is not about jitter.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const auto frames = sequence(renderer, stillScene(0.0f), 8);
    bool sawMedium = false;
    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 0; x < kSize; ++x) {
            const float first = luminanceAt(frames.front(), x, y);
            sawMedium = sawMedium || first > 0.01f;
            for (std::size_t i = 1; i < frames.size(); ++i) {
                INFO("pixel (" << x << ", " << y << ") frame " << i);
                REQUIRE(luminanceAt(frames[i], x, y) == first);
            }
        }
    }
    CHECK(sawMedium); // eight black frames are identical for free
}

TEST_CASE("the march's jitter is animated grain, and this is how much", "[gpu][volume][temporal]") {
    // §29 lists "animated grain" among six artefacts it calls mandatory to avoid, and the march
    // produces it by construction: `stepJitter` hashes the pixel AND the frame nonce, and nothing
    // accumulates across frames. This case does not judge it -- it measures it, so ADR-577's
    // recommendation rests on a number rather than on an adjective.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const Temporal still = temporalOf(sequence(renderer, stillScene(0.0f), 8));
    const Temporal jittered = temporalOf(sequence(renderer, stillScene(1.0f), 8));

    INFO("jitter 0: mean per-pixel temporal SD " << still.meanSd << ", moved "
         << (100.0 * still.movedFraction) << "%, peak " << still.peak);
    INFO("jitter 1: mean per-pixel temporal SD " << jittered.meanSd << ", moved "
         << (100.0 * jittered.movedFraction) << "%, peak " << jittered.peak);

    // With the jitter off nothing moves at all -- the same claim as case 1, stated as the zero the
    // other arm is measured against.
    CHECK(still.meanSd == 0.0);
    CHECK(still.movedFraction == 0.0);
    // With it on, the picture is not still -- and the SIZE of that is the surprise, which is why
    // measuring beat asserting. At an optical depth of 1.4 and the shipped 32 steps the mean
    // per-pixel temporal SD is **1.4e-5 in scene-linear luminance**, against a medium sitting at
    // about 0.17. That is four orders of magnitude down: real, and nowhere near visible.
    //
    // The threshold is 1e-6 rather than the measured value, because this case's job is "the grain
    // is real and it is here", not "it is exactly 1.43e-5" -- a test pinned to a measurement
    // becomes a tripwire on every unrelated change to the march. The magnitude belongs in ADR-577,
    // where it can be read with the conditions it was taken under.
    //
    // **These units are scene-linear float, not 0-255 levels**, and the first version of this
    // assertion used a 0.05 threshold carried over from an 8-bit habit. It failed against working
    // code, which is the cheapest possible way to be reminded what `renderToImageFloat` returns.
    CHECK(jittered.meanSd > 1e-6);
    CHECK(jittered.movedFraction > 0.10);
}


TEST_CASE("how the march's grain scales with depth and steps", "[.][gpu][volume][temporal][sweep]") {
    // A HIDDEN instrument, not a check -- `[.]` keeps it out of the default run. It exists because
    // the headline number above (1.4e-5 at optical depth 1.4 and 32 steps) is a single point on a
    // surface, and a single point is exactly the shape of evidence this project has been burned by.
    // An artist needs to know WHEN the grain matters, and "it depends on how thick the fog is and
    // how many steps you spend" is only useful with the numbers attached.
    //
    //     tools/gpu-lock.sh ./build/release/tests/avgen_render_tests \
    //         "how the march's grain scales with depth and steps" -s
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    for (const int steps : {16, 32, 64, 128}) {
        for (const float depth : {0.5f, 1.4f, 4.0f, 12.0f}) {
            const Temporal j = temporalOf(sequence(renderer, stillScene(1.0f, depth, steps), 6));
            // The medium's own brightness, so the grain can be read as a FRACTION of the thing it
            // is grain on -- an absolute SD says nothing without it.
            const auto ref = sequence(renderer, stillScene(0.0f, depth, steps), 1);
            double sum = 0.0;
            std::size_t lit = 0;
            for (std::uint32_t y = 0; y < kSize; ++y) {
                for (std::uint32_t x = 0; x < kSize; ++x) {
                    const float v = luminanceAt(ref.front(), x, y);
                    if (v > 0.001f) {
                        sum += v;
                        ++lit;
                    }
                }
            }
            const double mean = lit > 0 ? sum / static_cast<double>(lit) : 0.0;
            const auto lit1 = sequence(renderer, stillScene(1.0f, depth, steps), 1);
            const double spatialOn = spatialGrain(lit1.front());
            const double spatialOff = spatialGrain(ref.front());
            // `fprintf` rather than `WARN`, because Catch2 wraps its message text at the console
            // width and a wrapped number is a number a script cannot parse -- which cost one
            // re-run here. The lighting lab's `AVGEN_LAB_DUMP` uses the same escape for the same
            // reason.
            std::fprintf(stderr, "GRAIN %4d %6.1f %12.3e %10.4f %12.3e %12.3e %12.3e %8.3f\n",
                         steps, static_cast<double>(depth), j.meanSd, mean,
                         mean > 0.0 ? j.meanSd / mean : 0.0, spatialOn, spatialOff,
                         spatialOff > 0.0 ? spatialOn / spatialOff : 0.0);
        }
    }
}
