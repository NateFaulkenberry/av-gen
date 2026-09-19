// The glTF metallic-roughness BSDF and texture sampling (ADR-348 Phase 2, spec sections 18, 19).
//
// These are the checks that catch a BRDF which is wrong by a constant -- the kind of error that
// makes a render "look a bit dark" and never gets found, because nothing about the image says
// "0.87x". Each one pins an identity the model must satisfy rather than a number someone eyeballed.

#include "pathtrace/bsdf.hpp"
#include "pathtrace/sampler.hpp"
#include "pathtrace/texture.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {
constexpr float kPi = 3.14159265358979323846f;

pathtrace::SurfaceMaterial mat(float roughness, float metallic, glm::vec3 base = glm::vec3(0.8f)) {
    pathtrace::SurfaceMaterial m;
    m.baseColor = base;
    m.roughness = roughness;
    m.metallic = metallic;
    return m;
}
} // namespace

// ---- GGX -----------------------------------------------------------------------------------

TEST_CASE("the GGX distribution integrates to 1 over the projected hemisphere",
          "[unit][pathtrace][bsdf]") {
    // int D(h) (n.h) dh = 1 is the normalisation that makes the specular lobe conserve energy.
    // A D that is off by a factor passes every "looks shiny" check and fails this one.
    for (float roughness : {0.15f, 0.35f, 0.6f, 1.0f}) {
        const float alpha = roughness * roughness;
        const int nTheta = 800;
        const int nPhi = 4;
        double integral = 0.0;
        for (int i = 0; i < nTheta; ++i) {
            const double theta = (i + 0.5) * (kPi * 0.5) / nTheta;
            const double dTheta = (kPi * 0.5) / nTheta;
            const double cosT = std::cos(theta);
            const double sinT = std::sin(theta);
            integral += pathtrace::distributionGGX(static_cast<float>(cosT), alpha) * cosT * sinT * dTheta;
        }
        integral *= 2.0 * kPi; // azimuthally symmetric
        (void)nPhi;
        INFO("roughness " << roughness);
        REQUIRE(integral == Approx(1.0).margin(0.01));
    }
}

TEST_CASE("CONTROL: a mis-scaled GGX fails the normalisation check", "[unit][pathtrace][bsdf]") {
    // Without this arm, the integral test above would also pass for a D that was always ~0 in the
    // places the quadrature happened to sample. Scale D by 1.5 and the integral must move to 1.5.
    const float alpha = 0.35f * 0.35f;
    const int nTheta = 800;
    double integral = 0.0;
    for (int i = 0; i < nTheta; ++i) {
        const double theta = (i + 0.5) * (kPi * 0.5) / nTheta;
        const double dTheta = (kPi * 0.5) / nTheta;
        integral += 1.5 * pathtrace::distributionGGX(static_cast<float>(std::cos(theta)), alpha) *
                    std::cos(theta) * std::sin(theta) * dTheta;
    }
    integral *= 2.0 * kPi;
    REQUIRE(integral == Approx(1.5).margin(0.02));
}

TEST_CASE("GGX peaks on the normal and widens with roughness", "[unit][pathtrace][bsdf]") {
    const float smooth = 0.1f * 0.1f;
    const float rough = 0.8f * 0.8f;
    // On-axis, a smooth surface concentrates far more energy than a rough one.
    REQUIRE(pathtrace::distributionGGX(1.0f, smooth) > pathtrace::distributionGGX(1.0f, rough) * 10.0f);
    // Off-axis, the ordering reverses -- that is what "widens" means.
    const float off = std::cos(0.6f);
    REQUIRE(pathtrace::distributionGGX(off, rough) > pathtrace::distributionGGX(off, smooth));
    // Below the horizon there is no lobe at all.
    REQUIRE(pathtrace::distributionGGX(-0.5f, rough) == 0.0f);
}

// ---- Fresnel -------------------------------------------------------------------------------

TEST_CASE("Schlick's Fresnel is F0 at normal incidence and white at grazing",
          "[unit][pathtrace][bsdf]") {
    const glm::vec3 dielectric{0.04f};
    const glm::vec3 gold{1.0f, 0.77f, 0.34f};

    const glm::vec3 atNormal = pathtrace::fresnelSchlick(1.0f, dielectric);
    REQUIRE(atNormal.x == Approx(0.04f).margin(1e-6));

    const glm::vec3 atGrazing = pathtrace::fresnelSchlick(0.0f, dielectric);
    REQUIRE(atGrazing.x == Approx(1.0f).margin(1e-6));
    REQUIRE(atGrazing.y == Approx(1.0f).margin(1e-6));

    // A metal keeps its colour head-on and loses it at the edge -- the reason gold rims go white.
    const glm::vec3 goldNormal = pathtrace::fresnelSchlick(1.0f, gold);
    REQUIRE(goldNormal.z == Approx(0.34f).margin(1e-6));
    const glm::vec3 goldGrazing = pathtrace::fresnelSchlick(0.0f, gold);
    REQUIRE(goldGrazing.z == Approx(1.0f).margin(1e-6));

    // Monotonic in between, and never below F0.
    float prev = -1.0f;
    for (int i = 10; i >= 0; --i) {
        const float f = pathtrace::fresnelSchlick(i / 10.0f, dielectric).x;
        REQUIRE(f >= 0.04f - 1e-6f);
        REQUIRE(f > prev);
        prev = f;
    }
}

// ---- the combined BSDF -----------------------------------------------------------------------

TEST_CASE("a metal has no diffuse lobe and a dielectric keeps its colour",
          "[unit][pathtrace][bsdf]") {
    const glm::vec3 red{0.9f, 0.1f, 0.1f};
    const pathtrace::SurfaceMaterial metal = mat(0.3f, 1.0f, red);
    const pathtrace::SurfaceMaterial plastic = mat(0.3f, 0.0f, red);

    REQUIRE(metal.diffuseAlbedo() == glm::vec3(0.0f));
    REQUIRE(metal.f0().x == Approx(0.9f));      // the metal's F0 IS its base colour
    REQUIRE(plastic.f0().x == Approx(0.04f));   // the dielectric's is 4%
    REQUIRE(plastic.diffuseAlbedo().x == Approx(0.9f));
}

TEST_CASE("the BSDF is reciprocal", "[unit][pathtrace][bsdf]") {
    // f(v, l) == f(l, v) is a physical requirement, and a broken Smith term breaks it.
    const glm::vec3 n{0.0f, 0.0f, 1.0f};
    for (float roughness : {0.15f, 0.5f, 0.9f}) {
        for (float metallic : {0.0f, 1.0f}) {
            const auto m = mat(roughness, metallic);
            for (int i = 0; i < 40; ++i) {
                pathtrace::Sampler s(11, static_cast<std::uint32_t>(i), 0);
                const glm::vec3 v = pathtrace::sampleCosineHemisphere(s.next2D());
                const glm::vec3 l = pathtrace::sampleCosineHemisphere(s.next2D());
                const glm::vec3 a = pathtrace::evaluateBsdf(m, n, v, l);
                const glm::vec3 b = pathtrace::evaluateBsdf(m, n, l, v);
                REQUIRE(a.x == Approx(b.x).margin(1e-5));
                REQUIRE(a.y == Approx(b.y).margin(1e-5));
            }
        }
    }
}

TEST_CASE("the BSDF is never negative and never returns light from below the surface",
          "[unit][pathtrace][bsdf]") {
    const glm::vec3 n{0.0f, 0.0f, 1.0f};
    const glm::vec3 v = glm::normalize(glm::vec3(0.3f, 0.1f, 0.9f));
    for (float roughness : {0.05f, 0.3f, 1.0f}) {
        const auto m = mat(roughness, 0.5f);
        // Below the horizon: no transmission in this model, so exactly zero.
        REQUIRE(pathtrace::evaluateBsdf(m, n, v, glm::vec3(0.0f, 0.0f, -1.0f)) == glm::vec3(0.0f));
        for (int i = 0; i < 200; ++i) {
            pathtrace::Sampler s(5, static_cast<std::uint32_t>(i), 0);
            const glm::vec3 l = pathtrace::sampleCosineHemisphere(s.next2D());
            const glm::vec3 f = pathtrace::evaluateBsdf(m, n, v, l);
            REQUIRE(f.x >= 0.0f);
            REQUIRE(std::isfinite(f.x));
            REQUIRE(std::isfinite(f.y));
            REQUIRE(std::isfinite(f.z));
        }
    }
}

// The directional albedo of the BSDF: integrate f * cos over the hemisphere using the BSDF's own
// sampler, which is the low-variance estimator. Two independent estimators (this one and cosine
// sampling) agree to four decimals, so the numbers below are the model's behaviour and not noise.
namespace {
double directionalAlbedo(const pathtrace::SurfaceMaterial& m, float vz, int samples = 120000) {
    const glm::vec3 n{0.0f, 0.0f, 1.0f};
    const glm::vec3 v = glm::normalize(glm::vec3(std::sqrt(std::max(0.0f, 1.0f - vz * vz)), 0.0f, vz));
    double sum = 0.0;
    for (int i = 0; i < samples; ++i) {
        pathtrace::Sampler s(303, static_cast<std::uint32_t>(i), 0);
        const auto bs = pathtrace::sampleBsdf(m, n, v, s.next2D(), s.next1D());
        if (bs.valid) sum += bs.weight.x;
    }
    return sum / samples;
}
} // namespace

TEST_CASE("a metal conserves energy at every roughness and every angle",
          "[unit][pathtrace][bsdf][energy]") {
    // The specular lobe on its own. This is the arm that says GGX, Smith and Fresnel are right:
    // with metallic 1 and a white base, F is 1 and the integral is pure D*Vis, which must not
    // exceed 1 and (being single-scattering only) should fall below it as roughness rises.
    for (float roughness : {0.1f, 0.3f, 0.5f, 0.8f}) {
        for (float vz : {0.95f, 0.5f, 0.2f, 0.05f}) {
            const double a = directionalAlbedo(mat(roughness, 1.0f, glm::vec3(1.0f)), vz);
            INFO("roughness " << roughness << " v.z " << vz << " albedo " << a);
            REQUIRE(a <= 1.0 + 1e-3);
            REQUIRE(a > 0.5);  // live arm: it is not measuring zero
        }
    }
    // Single-scattering GGX loses energy as roughness rises. If this stops being true, the
    // visibility term has changed.
    REQUIRE(directionalAlbedo(mat(0.8f, 1.0f, glm::vec3(1.0f)), 0.95f) <
            directionalAlbedo(mat(0.1f, 1.0f, glm::vec3(1.0f)), 0.95f));
}

TEST_CASE("a dielectric conserves energy except where the glTF model is known not to",
          "[unit][pathtrace][bsdf][energy]") {
    // Head-on, and at ordinary roughness, a dielectric conserves.
    REQUIRE(directionalAlbedo(mat(0.5f, 0.0f, glm::vec3(0.8f)), 0.95f) <= 1.0);
    REQUIRE(directionalAlbedo(mat(0.8f, 0.0f, glm::vec3(0.8f)), 0.7f) <= 1.0);
    REQUIRE(directionalAlbedo(mat(0.5f, 0.0f, glm::vec3(0.8f)), 0.95f) > 0.5);  // live arm

    // MEASURED, DELIBERATE, AND NOT A BUG IN THIS CODE: the glTF 2.0 metallic-roughness model
    // suppresses its diffuse lobe by (1 - F(v.h)). For a diffuse direction v.h is usually near 1,
    // so F is near f0 = 0.04 and the diffuse is barely suppressed -- while the specular lobe's own
    // directional albedo at grazing is large. The two therefore sum to more than 1.
    //
    // The spec (section 19) asks for glTF metallic-roughness as the BRDF baseline, so this is kept
    // faithful rather than quietly "fixed". It is pinned here as a BAND so that a change which made
    // it worse, or a change which silently altered the model, both show up. See ADR-349.
    const double worst = directionalAlbedo(mat(0.1f, 0.0f, glm::vec3(1.0f)), 0.05f);
    INFO("white smooth dielectric at grazing: " << worst);
    REQUIRE(worst > 1.5);   // it really does gain; if this fails the model changed
    REQUIRE(worst < 1.8);   // and it must not get worse

    // The gain is worst for bright, smooth surfaces and vanishes for ordinary ones.
    REQUIRE(directionalAlbedo(mat(0.5f, 0.0f, glm::vec3(0.5f)), 0.05f) < 1.0);
    REQUIRE(directionalAlbedo(mat(0.1f, 0.0f, glm::vec3(1.0f)), 0.95f) < 1.01);
}

TEST_CASE("sampleBsdf agrees with evaluateBsdf and bsdfPdf", "[unit][pathtrace][bsdf]") {
    // The sampler's returned weight must equal f * cos / pdf recomputed independently. If the two
    // disagree the renderer is biased in a way no image inspection will reveal.
    const glm::vec3 n{0.0f, 0.0f, 1.0f};
    const glm::vec3 v = glm::normalize(glm::vec3(0.2f, -0.3f, 0.93f));
    int valid = 0;
    for (float roughness : {0.15f, 0.45f, 0.9f}) {
        for (float metallic : {0.0f, 0.5f, 1.0f}) {
            const auto m = mat(roughness, metallic);
            for (int i = 0; i < 400; ++i) {
                pathtrace::Sampler s(23, static_cast<std::uint32_t>(i), 0);
                const glm::vec2 u = s.next2D();
                const float pick = s.next1D();
                const auto bs = pathtrace::sampleBsdf(m, n, v, u, pick);
                if (!bs.valid) continue;
                ++valid;
                const float pdf = pathtrace::bsdfPdf(m, n, v, bs.direction);
                REQUIRE(pdf == Approx(bs.pdf).epsilon(1e-4));
                const glm::vec3 expected =
                    pathtrace::evaluateBsdf(m, n, v, bs.direction) * glm::dot(n, bs.direction) / pdf;
                REQUIRE(bs.weight.x == Approx(expected.x).epsilon(1e-4));
                REQUIRE(bs.weight.y == Approx(expected.y).epsilon(1e-4));
            }
        }
    }
    REQUIRE(valid > 2000); // the arm is live
}

TEST_CASE("the BSDF PDF integrates to 1", "[unit][pathtrace][bsdf]") {
    // Sample with the BSDF's own sampler and estimate int pdf domega by 1/pdf; a PDF that does not
    // integrate to 1 makes every MIS weight in Phase 3 wrong.
    const glm::vec3 n{0.0f, 0.0f, 1.0f};
    const glm::vec3 v = glm::normalize(glm::vec3(0.1f, 0.1f, 0.99f));
    for (float roughness : {0.3f, 0.7f}) {
        const auto m = mat(roughness, 0.0f);
        double sum = 0.0;
        const int n_ = 80000;
        for (int i = 0; i < n_; ++i) {
            pathtrace::Sampler s(29, static_cast<std::uint32_t>(i), 0);
            // Uniform hemisphere reference measure: pdf_u = 1/(2pi).
            const glm::vec2 u = s.next2D();
            const float z = u.x;
            const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
            const float phi = 2.0f * kPi * u.y;
            const glm::vec3 l{r * std::cos(phi), r * std::sin(phi), z};
            sum += pathtrace::bsdfPdf(m, n, v, l) * (2.0 * kPi);
        }
        INFO("roughness " << roughness);
        REQUIRE(sum / n_ == Approx(1.0).margin(0.05));
    }
}

// ---- textures and colour space (spec section 18) ------------------------------------------------

namespace {
scene::TextureData solid(scene::TextureFormat fmt, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    scene::TextureData t;
    t.name = "solid";
    t.width = 2;
    t.height = 2;
    t.format = fmt;
    t.data.assign(2 * 2 * 4, 0);
    for (int i = 0; i < 4; ++i) {
        t.data[i * 4 + 0] = r;
        t.data[i * 4 + 1] = g;
        t.data[i * 4 + 2] = b;
        t.data[i * 4 + 3] = 255;
    }
    return t;
}
} // namespace

TEST_CASE("an sRGB texture is decoded and a linear one is NOT", "[unit][pathtrace][texture]") {
    // This is the section 18 non-negotiable, and it is the single most consequential line in the
    // texture path: decoding a roughness map moves every roughness value in the scene.
    const std::uint8_t mid = 128;
    const scene::TextureData srgb = solid(scene::TextureFormat::Rgba8Srgb, mid, mid, mid);
    const scene::TextureData linear = solid(scene::TextureFormat::Rgba8Unorm, mid, mid, mid);

    const glm::vec4 s = pathtrace::texelLinear(srgb, 0, 0);
    const glm::vec4 l = pathtrace::texelLinear(linear, 0, 0);

    // 128/255 = 0.502 encoded; decoded it is ~0.216. The linear one stays at 0.502.
    REQUIRE(l.x == Approx(128.0f / 255.0f).margin(1e-4));
    REQUIRE(s.x == Approx(0.2158f).margin(0.005));
    REQUIRE(s.x < l.x * 0.5f); // and they are emphatically not the same number

    // Alpha is coverage, never decoded, in both.
    REQUIRE(s.w == Approx(1.0f));
    REQUIRE(l.w == Approx(1.0f));

    // The endpoints are fixed points of the transfer function in both directions.
    const scene::TextureData white = solid(scene::TextureFormat::Rgba8Srgb, 255, 255, 255);
    const scene::TextureData black = solid(scene::TextureFormat::Rgba8Srgb, 0, 0, 0);
    REQUIRE(pathtrace::texelLinear(white, 0, 0).x == Approx(1.0f).margin(1e-5));
    REQUIRE(pathtrace::texelLinear(black, 0, 0).x == Approx(0.0f).margin(1e-5));
}

TEST_CASE("wrap modes address the texels they say they do", "[unit][pathtrace][texture]") {
    REQUIRE(pathtrace::wrapTexel(0, 4, scene::WrapMode::Repeat) == 0);
    REQUIRE(pathtrace::wrapTexel(4, 4, scene::WrapMode::Repeat) == 0);
    REQUIRE(pathtrace::wrapTexel(5, 4, scene::WrapMode::Repeat) == 1);
    REQUIRE(pathtrace::wrapTexel(-1, 4, scene::WrapMode::Repeat) == 3); // negative must not fold wrong

    REQUIRE(pathtrace::wrapTexel(-3, 4, scene::WrapMode::Clamp) == 0);
    REQUIRE(pathtrace::wrapTexel(9, 4, scene::WrapMode::Clamp) == 3);

    // Mirror: 0 1 2 3 | 3 2 1 0 | 0 1 2 3 ...
    REQUIRE(pathtrace::wrapTexel(3, 4, scene::WrapMode::Mirror) == 3);
    REQUIRE(pathtrace::wrapTexel(4, 4, scene::WrapMode::Mirror) == 3);
    REQUIRE(pathtrace::wrapTexel(7, 4, scene::WrapMode::Mirror) == 0);
    REQUIRE(pathtrace::wrapTexel(8, 4, scene::WrapMode::Mirror) == 0);
    REQUIRE(pathtrace::wrapTexel(-1, 4, scene::WrapMode::Mirror) == 0);
}

TEST_CASE("bilinear filtering blends, and blends in LINEAR space", "[unit][pathtrace][texture]") {
    // A 2x1 sRGB texture, black and white. Sampled exactly between the two texel centres the answer
    // must be the mean of the DECODED values (0.5), not the decode of the mean byte (~0.216).
    scene::TextureData t;
    t.width = 2;
    t.height = 1;
    t.format = scene::TextureFormat::Rgba8Srgb;
    t.data = {0, 0, 0, 255, 255, 255, 255, 255};

    scene::TextureRef ref;
    ref.texture = 0;
    ref.linearFilter = true;
    ref.wrapU = scene::WrapMode::Clamp;
    ref.wrapV = scene::WrapMode::Clamp;

    const glm::vec4 mid = pathtrace::sampleTexture(t, ref, glm::vec2(0.5f, 0.5f));
    REQUIRE(mid.x == Approx(0.5f).margin(0.01));
    // CONTROL: had the blend happened before the decode, this would be ~0.216 instead.
    REQUIRE(mid.x > 0.4f);

    // At the texel centres themselves the filter must return the texels unblended.
    REQUIRE(pathtrace::sampleTexture(t, ref, glm::vec2(0.25f, 0.5f)).x == Approx(0.0f).margin(1e-4));
    REQUIRE(pathtrace::sampleTexture(t, ref, glm::vec2(0.75f, 0.5f)).x == Approx(1.0f).margin(1e-4));

    // Nearest filtering picks one or the other and never anything between.
    scene::TextureRef nearest = ref;
    nearest.linearFilter = false;
    const float n0 = pathtrace::sampleTexture(t, nearest, glm::vec2(0.3f, 0.5f)).x;
    const float n1 = pathtrace::sampleTexture(t, nearest, glm::vec2(0.7f, 0.5f)).x;
    REQUIRE(n0 == Approx(0.0f).margin(1e-5));
    REQUIRE(n1 == Approx(1.0f).margin(1e-5));
}

TEST_CASE("an unset or out-of-range texture slot returns the fallback, not black",
          "[unit][pathtrace][texture]") {
    // Section 54: a missing texture must not silently become black, which would look like a
    // shading bug rather than a missing asset.
    const std::vector<scene::TextureData> none;
    scene::TextureRef unset;
    REQUIRE(pathtrace::sampleSlot(none, unset, glm::vec2(0.5f), glm::vec4(1.0f)) == glm::vec4(1.0f));

    scene::TextureRef outOfRange;
    outOfRange.texture = 7;
    REQUIRE(pathtrace::sampleSlot(none, outOfRange, glm::vec2(0.5f), glm::vec4(1.0f)) == glm::vec4(1.0f));

    // CONTROL: a slot that IS in range returns the texture, so the fallback is not unconditional.
    std::vector<scene::TextureData> one{solid(scene::TextureFormat::Rgba8Unorm, 0, 0, 0)};
    scene::TextureRef valid;
    valid.texture = 0;
    REQUIRE(pathtrace::sampleSlot(one, valid, glm::vec2(0.5f), glm::vec4(1.0f)).x == Approx(0.0f));
}

TEST_CASE("diag2: albedo vs metallic and base colour", "[.pathtrace-energy2]") {
    const glm::vec3 n{0.0f, 0.0f, 1.0f};
    for (float metallic : {0.0f, 1.0f}) {
        for (float base : {1.0f, 0.8f, 0.5f}) {
            for (float roughness : {0.1f, 0.5f}) {
                float worst = 0.0f;
                float worstVz = 0.0f;
                for (float vz : {0.95f, 0.7f, 0.5f, 0.3f, 0.2f, 0.1f, 0.05f}) {
                    const auto m = mat(roughness, metallic, glm::vec3(base));
                    const glm::vec3 v = glm::normalize(glm::vec3(std::sqrt(1.0f - vz * vz), 0.0f, vz));
                    double est = 0.0;
                    const int N = 120000;
                    for (int i = 0; i < N; ++i) {
                        pathtrace::Sampler s2(303, static_cast<std::uint32_t>(i), 0);
                        const auto bs = pathtrace::sampleBsdf(m, n, v, s2.next2D(), s2.next1D());
                        if (bs.valid) est += bs.weight.x;
                    }
                    if (est / N > worst) { worst = static_cast<float>(est / N); worstVz = vz; }
                }
                WARN("metallic " << metallic << " base " << base << " rough " << roughness
                                 << "  worst albedo " << worst << " at v.z " << worstVz);
            }
        }
    }
}

TEST_CASE("diag: albedo by cosine sampling vs by BSDF sampling", "[.pathtrace-energy]") {
    const glm::vec3 n{0.0f, 0.0f, 1.0f};
    for (float roughness : {0.1f, 0.4f, 0.8f}) {
        for (float vz : {0.95f, 0.5f, 0.2f}) {
            const auto m = mat(roughness, 0.0f, glm::vec3(1.0f));
            const glm::vec3 v = glm::normalize(glm::vec3(std::sqrt(1.0f - vz * vz), 0.0f, vz));
            const int N = 400000;
            double cosEst = 0.0;
            double bsdfEst = 0.0;
            int bsdfN = 0;
            for (int i = 0; i < N; ++i) {
                pathtrace::Sampler s(101, static_cast<std::uint32_t>(i), 0);
                const glm::vec3 l = pathtrace::sampleCosineHemisphere(s.next2D());
                const float pdf = pathtrace::cosineHemispherePdf(l.z);
                if (pdf > 0.0f) cosEst += pathtrace::evaluateBsdf(m, n, v, l).x * l.z / pdf;

                pathtrace::Sampler s2(202, static_cast<std::uint32_t>(i), 0);
                const auto bs = pathtrace::sampleBsdf(m, n, v, s2.next2D(), s2.next1D());
                ++bsdfN;
                if (bs.valid) bsdfEst += bs.weight.x;
            }
            WARN("rough " << roughness << " v.z " << vz << "  cosine-est " << (cosEst / N)
                          << "  bsdf-est " << (bsdfEst / bsdfN));
        }
    }
}
