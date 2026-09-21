// The shared self-shadow march (ADR-570, the brief's §20 and §22).
//
// **What is being asserted, and why it needed a rendered frame.** The march now sends a short
// secondary ray from each sample toward each light that lights the air, through the placed media's
// own density, and attenuates that light's in-scatter by the transmittance. Nothing about that is
// visible in a unit test of a field: the field is unchanged. What changes is the PICTURE, and
// specifically one property of it -- a bank becomes brighter on the side facing the light than on
// the side away from it. Before this there was no setting of any control that produced that
// asymmetry, which is why §20's "backlit fog" and "dark moody fog" were unreachable and why
// ADR-358 refused to build a volumetric beam without shadow sampling.
//
// **The three cases and how each fails.**
//
//   1. *The feature reaches the picture at all.* Turning the steps up must change the frame. This
//      is the "built but unreachable" guard this repository has been bitten by four times in one
//      session (see the memory note of that name, and ADR-562's kind tag, which was packed,
//      compared and never uploaded for a day). Delete the `shadow` factor from `inScatterAt` and
//      case 1 fails.
//   2. *The asymmetry is the right way round and it is what the shadow march causes.* With the
//      light coming from +X, the +X side of the bank must be brighter than the -X side, and the
//      difference must be LARGER with the shadow march on than off. The second half is what makes
//      this a test of the shadow rather than of the phase function -- Henyey-Greenstein already
//      produces some asymmetry from the view direction alone, and a case that only checked "one
//      side is brighter" would pass with the whole feature deleted.
//   3. *Zero steps is exactly off.* At `volumeShadowSteps` 0 the strength must do nothing at all,
//      bit for bit, because the default is 0 and every scene in the repository renders through
//      this code. Make the shader read the strength before the step-count branch and case 3 fails.

#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"
#include "world/atmospherics.hpp"
#include "world/world_effects/effect_registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>

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

// A fog bank hanging in empty space with ONE light, pointing along -X so it arrives from +X. The
// camera looks down -Z, so the bank's lit and unlit sides are the left and right of the frame and
// the asymmetry is a horizontal one that a column average can read.
scene::Scene bankScene(int shadowSteps, float shadowStrength) {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.volumeDensity = 0.0f; // no environment fog: the placed medium is the only air
    s.environment.volumeScattering = 1.0f;
    s.environment.volumeAbsorption = 1.0f;
    s.environment.volumeAnisotropy = 0.0f; // isotropic, so the phase function contributes no
                                           // left-right asymmetry of its own and case 2 is about
                                           // the shadow rather than about Henyey-Greenstein
    s.environment.volumeSteps = 64;
    s.environment.volumeMaxDistance = 1200.0f;
    s.environment.volumeShadowSteps = shadowSteps;
    s.environment.volumeShadowStrength = shadowStrength;

    s.camera.position = {0.0f, 0.0f, 600.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.camera.fovYRadians = 0.87266f;
    s.camera.nearPlane = 1.0f;
    s.camera.farPlane = 2000.0f;

    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(-1.0f, 0.0f, 0.0f)); // travelling -X, so from +X
    key.intensity = 6.0f;
    key.volumetricStrength = 1.0f;
    key.color = glm::vec3(1.0f);
    s.addLight(key);

    world::AtmosphericEffect e =
        world::makeAtmosphericEffect(world::AtmosphereKind::VolumetricFog, "bank");
    world::Vortex& v = e.vortex;
    v.field.center = {0.0f, 0.0f, 0.0f};
    v.field.radius = 160.0f;
    v.field.thickness = 120.0f;
    v.field.cloudNoise = 0.0f; // the analytic shape alone, so the picture is the geometry
    v.density = 3.0f;          // ADR-564: an optical depth through the bank, thick enough that the
                               // far side is genuinely in shadow rather than slightly dimmer
    v.emission = 0.0f;         // NOT self-luminous: this case is about light arriving, and an
                               // emissive bank would swamp the term under test
    v.scattering = 1.0f;       // ADR-388's per-slot weight: this medium takes the scene's lights
    e.values.setFloat("fog/shape", 1.0f);       // a sphere, so the two sides are geometrically equal
    e.values.setFloat("fog/edgeSoftness", 0.4f);
    e.values.setFloat("fog/heightInfluence", 0.0f);

    // ADR-566: the one writer of the bytes the march reads.
    s.atmospherics.mediumCount = 1;
    world::packMediumSlot(e, 1.0f, s.atmospherics.media[0]);
    return s;
}

gpu::ImageF shot(rendering::SceneRenderer& renderer, const scene::Scene& s) {
    FrameTime time{};
    time.renderTime = 1.0;
    time.deltaTime = 1.0 / 60.0;
    auto image = renderer.renderToImageFloat(s, time, kSize, kSize);
    REQUIRE(image.has_value());
    return std::move(*image);
}

float luminanceAt(const gpu::ImageF& image, std::uint32_t x, std::uint32_t y) {
    const float* p = image.pixel(x, y);
    return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
}

// Mean luminance of a vertical band, centred on the frame's middle rows so the sample is inside
// the bank rather than on its top and bottom edges.
float band(const gpu::ImageF& image, std::uint32_t x0, std::uint32_t x1) {
    double sum = 0.0;
    int n = 0;
    for (std::uint32_t y = kSize / 3; y < 2 * kSize / 3; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            sum += static_cast<double>(luminanceAt(image, x, y));
            ++n;
        }
    }
    return n > 0 ? static_cast<float>(sum / n) : 0.0f;
}

struct Sides {
    float lit = 0.0f;   // the +X half, which is screen RIGHT for a camera on +Z looking at -Z
    float dark = 0.0f;  // the -X half
};

Sides sidesOf(const gpu::ImageF& image) {
    // Quarter-width bands either side of centre: far enough out to be inside the bank's body on
    // each side, far enough in to avoid its rim, where the density is low on both sides and the
    // difference the case is about does not exist.
    return Sides{band(image, kSize * 5 / 8, kSize * 7 / 8), band(image, kSize / 8, kSize * 3 / 8)};
}

} // namespace

TEST_CASE("the self-shadow march reaches the picture", "[gpu][fog][shadow]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const gpu::ImageF off = shot(renderer, bankScene(0, 1.0f));
    const gpu::ImageF on = shot(renderer, bankScene(6, 1.0f));

    // The control first: the bank is actually in the frame and lit. Without this the comparison
    // below could be two black images agreeing perfectly about nothing.
    const Sides offSides = sidesOf(off);
    INFO("shadow off: lit " << offSides.lit << ", dark " << offSides.dark);
    REQUIRE(offSides.lit > 0.01f);

    int moved = 0;
    float worst = 0.0f;
    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 0; x < kSize; ++x) {
            const float d = std::abs(luminanceAt(on, x, y) - luminanceAt(off, x, y));
            if (d > 1e-4f) {
                ++moved;
            }
            worst = std::max(worst, d);
        }
    }
    INFO("pixels moved: " << moved << " of " << (kSize * kSize) << ", worst " << worst);
    CHECK(moved > 200);
}

TEST_CASE("a shadowed bank is brighter on the side the light comes from", "[gpu][fog][shadow]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const Sides off = sidesOf(shot(renderer, bankScene(0, 1.0f)));
    const Sides on = sidesOf(shot(renderer, bankScene(6, 1.0f)));

    INFO("off: lit " << off.lit << " dark " << off.dark << " (difference " << (off.lit - off.dark)
         << ")");
    INFO("on:  lit " << on.lit << " dark " << on.dark << " (difference " << (on.lit - on.dark)
         << ")");

    // The property. With the phase function isotropic, the only thing that can make the two sides
    // of a SPHERE differ is how much light reached each of them.
    CHECK(on.lit > on.dark);
    // ...and it is the shadow march that causes it, not something already there. This is the half
    // that fails when the feature is deleted; the line above would still pass on rounding.
    CHECK((on.lit - on.dark) > (off.lit - off.dark) + 0.01f);
    // The far side got DARKER rather than the near side getting brighter: a shadow removes light,
    // it does not add any. A term that brightened the lit side instead would pass both checks
    // above and be wrong about what it is.
    CHECK(on.dark < off.dark);
}

TEST_CASE("zero shadow steps is exactly off", "[gpu][fog][shadow][determinism]") {
    // The default, and therefore the promise to every scene in the repository. `volumeShadowSteps`
    // is 0 unless a scene asks otherwise, and at 0 the strength must do nothing at all -- not
    // nearly nothing. The shader returns 1.0 from a branch that tests the step count first, and
    // this is what holds that ordering in place.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const gpu::ImageF a = shot(renderer, bankScene(0, 0.0f));
    const gpu::ImageF b = shot(renderer, bankScene(0, 4.0f));
    bool sawLight = false;
    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 0; x < kSize; ++x) {
            INFO("at (" << x << ", " << y << ")");
            REQUIRE(luminanceAt(a, x, y) == luminanceAt(b, x, y));
            sawLight = sawLight || luminanceAt(a, x, y) > 0.01f;
        }
    }
    CHECK(sawLight); // two black frames are identical for free
}
