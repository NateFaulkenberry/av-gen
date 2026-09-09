#include "core/color.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using namespace avgen;
using namespace avgen::color;
using Catch::Matchers::WithinAbs;

namespace {

double d(float v) {
    return static_cast<double>(v);
}

void checkVec(const glm::vec3& v, const glm::vec3& expected, double tol = 1e-5) {
    CHECK_THAT(d(v.x), WithinAbs(d(expected.x), tol));
    CHECK_THAT(d(v.y), WithinAbs(d(expected.y), tol));
    CHECK_THAT(d(v.z), WithinAbs(d(expected.z), tol));
}

// A grid of colours covering greys, primaries, secondaries and mid tones (linear RGB in [0, 1]).
std::vector<glm::vec3> colourGrid() {
    std::vector<glm::vec3> grid;
    for (int r = 0; r <= 4; ++r) {
        for (int g = 0; g <= 4; ++g) {
            for (int b = 0; b <= 4; ++b) {
                grid.emplace_back(static_cast<float>(r) / 4.0f, static_cast<float>(g) / 4.0f,
                                  static_cast<float>(b) / 4.0f);
            }
        }
    }
    grid.emplace_back(0.7f, 0.2f, 0.35f);
    grid.emplace_back(0.13f, 0.61f, 0.28f);
    grid.emplace_back(0.01f, 0.02f, 0.9f);
    return grid;
}

Ramp threeStopRamp(bool perceptual, bool cyclic) {
    Ramp ramp;
    ramp.stopCount = 3;
    ramp.stops[0] = {0.0f, {1.0f, 0.0f, 0.0f}};
    ramp.stops[1] = {0.5f, {0.0f, 1.0f, 0.0f}};
    ramp.stops[2] = {1.0f, {0.0f, 0.0f, 1.0f}};
    ramp.perceptual = perceptual;
    ramp.cyclic = cyclic;
    return ramp;
}

} // namespace

// ---- conversions ------------------------------------------------------------------------------

TEST_CASE("HSV: known values and round trips", "[color]") {
    checkVec(rgbToHsv({1.0f, 0.0f, 0.0f}), {0.0f, 1.0f, 1.0f});
    checkVec(rgbToHsv({0.0f, 1.0f, 0.0f}), {1.0f / 3.0f, 1.0f, 1.0f});
    checkVec(rgbToHsv({0.0f, 0.0f, 1.0f}), {2.0f / 3.0f, 1.0f, 1.0f});
    checkVec(rgbToHsv({1.0f, 1.0f, 0.0f}), {1.0f / 6.0f, 1.0f, 1.0f});
    checkVec(rgbToHsv({1.0f, 0.0f, 1.0f}), {5.0f / 6.0f, 1.0f, 1.0f});
    checkVec(rgbToHsv({0.5f, 0.5f, 0.5f}), {0.0f, 0.0f, 0.5f});
    checkVec(rgbToHsv({0.0f, 0.0f, 0.0f}), {0.0f, 0.0f, 0.0f});
    checkVec(hsvToRgb({0.0f, 1.0f, 1.0f}), {1.0f, 0.0f, 0.0f});
    checkVec(hsvToRgb({0.5f, 1.0f, 1.0f}), {0.0f, 1.0f, 1.0f});
    checkVec(hsvToRgb({1.0f, 1.0f, 1.0f}), {1.0f, 0.0f, 0.0f}); // hue wraps
    checkVec(hsvToRgb({-0.25f, 1.0f, 1.0f}), hsvToRgb({0.75f, 1.0f, 1.0f}));
    for (const glm::vec3& c : colourGrid()) {
        checkVec(hsvToRgb(rgbToHsv(c)), c, 1e-4);
    }
    // hue in [0, 1)
    for (const glm::vec3& c : colourGrid()) {
        const glm::vec3 hsv = rgbToHsv(c);
        CHECK(hsv.x >= 0.0f);
        CHECK(hsv.x < 1.0f);
    }
}

TEST_CASE("HSL: known values and round trips", "[color]") {
    checkVec(rgbToHsl({1.0f, 0.0f, 0.0f}), {0.0f, 1.0f, 0.5f});
    checkVec(rgbToHsl({0.0f, 1.0f, 0.0f}), {1.0f / 3.0f, 1.0f, 0.5f});
    checkVec(rgbToHsl({1.0f, 1.0f, 1.0f}), {0.0f, 0.0f, 1.0f});
    checkVec(rgbToHsl({0.25f, 0.25f, 0.25f}), {0.0f, 0.0f, 0.25f});
    checkVec(rgbToHsl({0.75f, 0.25f, 0.25f}), {0.0f, 0.5f, 0.5f});
    checkVec(hslToRgb({0.0f, 1.0f, 0.5f}), {1.0f, 0.0f, 0.0f});
    checkVec(hslToRgb({2.0f / 3.0f, 1.0f, 0.75f}), {0.5f, 0.5f, 1.0f});
    for (const glm::vec3& c : colourGrid()) {
        checkVec(hslToRgb(rgbToHsl(c)), c, 1e-4);
    }
}

TEST_CASE("OKLab: white and black, primaries, round trips", "[color]") {
    const glm::vec3 white = rgbToOklab({1.0f, 1.0f, 1.0f});
    CHECK_THAT(d(white.x), WithinAbs(1.0, 1e-4));
    CHECK_THAT(d(white.y), WithinAbs(0.0, 1e-4));
    CHECK_THAT(d(white.z), WithinAbs(0.0, 1e-4));
    checkVec(rgbToOklab({0.0f, 0.0f, 0.0f}), {0.0f, 0.0f, 0.0f});
    // Ottosson's published reference values for the sRGB primaries.
    checkVec(rgbToOklab({1.0f, 0.0f, 0.0f}), {0.627955f, 0.224863f, 0.125846f}, 1e-4);
    checkVec(rgbToOklab({0.0f, 1.0f, 0.0f}), {0.866440f, -0.233888f, 0.179498f}, 1e-4);
    checkVec(rgbToOklab({0.0f, 0.0f, 1.0f}), {0.452014f, -0.032457f, -0.311528f}, 1e-4);
    // Greys are neutral (a = b = 0) and L grows with value.
    const glm::vec3 grey = rgbToOklab({0.3f, 0.3f, 0.3f});
    CHECK_THAT(d(grey.y), WithinAbs(0.0, 1e-5));
    CHECK_THAT(d(grey.z), WithinAbs(0.0, 1e-5));
    CHECK(grey.x < white.x);
    CHECK(grey.x > 0.0f);
    for (const glm::vec3& c : colourGrid()) {
        checkVec(oklabToRgb(rgbToOklab(c)), c, 1e-4);
    }
}

TEST_CASE("OKLCH: hue in turns, chroma, round trips", "[color]") {
    const glm::vec3 red = oklabToOklch(rgbToOklab({1.0f, 0.0f, 0.0f}));
    CHECK_THAT(d(red.y), WithinAbs(std::sqrt(0.224863 * 0.224863 + 0.125846 * 0.125846), 1e-4));
    CHECK_THAT(d(red.z), WithinAbs(std::atan2(0.125846, 0.224863) / (2.0 * 3.14159265358979), 1e-4));
    const glm::vec3 neutral = oklabToOklch({0.5f, 0.0f, 0.0f});
    checkVec(neutral, {0.5f, 0.0f, 0.0f});
    // Negative angles wrap into [0, 1).
    const glm::vec3 blue = oklabToOklch(rgbToOklab({0.0f, 0.0f, 1.0f}));
    CHECK(blue.z > 0.5f);
    CHECK(blue.z < 1.0f);
    checkVec(oklchToOklab({0.7f, 0.1f, 0.25f}), {0.7f, 0.0f, 0.1f}, 1e-6);
    checkVec(oklchToOklab({0.7f, 0.1f, 0.5f}), {0.7f, -0.1f, 0.0f}, 1e-6);
    for (const glm::vec3& c : colourGrid()) {
        checkVec(oklabToRgb(oklchToOklab(oklabToOklch(rgbToOklab(c)))), c, 1e-4);
    }
}

TEST_CASE("sRGB transfer functions: known values and round trip", "[color]") {
    checkVec(srgbToLinear({0.0f, 0.5f, 1.0f}), {0.0f, 0.214041f, 1.0f}, 1e-5);
    checkVec(linearToSrgb({0.0f, 0.214041f, 1.0f}), {0.0f, 0.5f, 1.0f}, 1e-5);
    CHECK_THAT(d(srgbToLinear(glm::vec3(0.04045f)).x), WithinAbs(0.04045 / 12.92, 1e-6));
    CHECK_THAT(d(linearToSrgb(glm::vec3(0.0031308f)).x), WithinAbs(0.0031308 * 12.92, 1e-6));
    for (const glm::vec3& c : colourGrid()) {
        checkVec(srgbToLinear(linearToSrgb(c)), c, 1e-5);
        checkVec(linearToSrgb(srgbToLinear(c)), c, 1e-5);
    }
}

TEST_CASE("luminance uses Rec. 709 weights", "[color]") {
    CHECK_THAT(d(luminance({1.0f, 1.0f, 1.0f})), WithinAbs(1.0, 1e-6));
    CHECK_THAT(d(luminance({1.0f, 0.0f, 0.0f})), WithinAbs(0.2126, 1e-6));
    CHECK_THAT(d(luminance({0.0f, 1.0f, 0.0f})), WithinAbs(0.7152, 1e-6));
    CHECK_THAT(d(luminance({0.0f, 0.0f, 1.0f})), WithinAbs(0.0722, 1e-6));
}

// ---- manipulation -----------------------------------------------------------------------------

TEST_CASE("hueShift: identity at 1 turn, flips at half a turn, clamps to >= 0", "[color]") {
    const glm::vec3 c{0.7f, 0.3f, 0.2f};
    checkVec(hueShift(c, 0.0f), c, 1e-4);
    checkVec(hueShift(c, 1.0f), c, 1e-4);
    checkVec(hueShift(c, -1.0f), c, 1e-4);
    const glm::vec3 lchBefore = oklabToOklch(rgbToOklab(c));
    const glm::vec3 shifted = hueShift(c, 0.5f);
    const glm::vec3 lchAfter = oklabToOklch(rgbToOklab(shifted));
    // Lightness and chroma preserved (the flipped colour stays in gamut), hue off by 0.5.
    CHECK_THAT(d(lchAfter.x), WithinAbs(d(lchBefore.x), 2e-3));
    float dh = std::fabs(lchAfter.z - lchBefore.z);
    CHECK_THAT(d(dh), WithinAbs(0.5, 2e-3));
    // A quarter turn composes.
    checkVec(hueShift(hueShift(c, 0.25f), 0.25f), hueShift(c, 0.5f), 1e-3);
    // Greys are unaffected.
    checkVec(hueShift({0.4f, 0.4f, 0.4f}, 0.3f), {0.4f, 0.4f, 0.4f}, 1e-4);
    // Saturated primaries rotated out of gamut are clamped, never negative.
    const glm::vec3 clamped = hueShift({1.0f, 0.0f, 0.0f}, 0.5f);
    CHECK(clamped.x >= 0.0f);
    CHECK(clamped.y >= 0.0f);
    CHECK(clamped.z >= 0.0f);
}

TEST_CASE("hueShiftHsv rotates the HSV hue", "[color]") {
    checkVec(hueShiftHsv({1.0f, 0.0f, 0.0f}, 1.0f / 3.0f), {0.0f, 1.0f, 0.0f}, 1e-5);
    checkVec(hueShiftHsv({1.0f, 0.0f, 0.0f}, 2.0f / 3.0f), {0.0f, 0.0f, 1.0f}, 1e-5);
    checkVec(hueShiftHsv({1.0f, 0.0f, 0.0f}, 1.0f), {1.0f, 0.0f, 0.0f}, 1e-5);
    checkVec(hueShiftHsv({0.2f, 0.5f, 0.7f}, 0.0f), {0.2f, 0.5f, 0.7f}, 1e-5);
}

TEST_CASE("saturate scales OKLab chroma; saturate(0) is grey", "[color]") {
    const glm::vec3 c{0.8f, 0.3f, 0.1f};
    const glm::vec3 grey = saturate(c, 0.0f);
    const glm::vec3 lab = rgbToOklab(grey);
    CHECK_THAT(d(lab.y), WithinAbs(0.0, 1e-4));
    CHECK_THAT(d(lab.z), WithinAbs(0.0, 1e-4));
    CHECK_THAT(d(lab.x), WithinAbs(d(rgbToOklab(c).x), 1e-4));
    CHECK_THAT(d(grey.x), WithinAbs(d(grey.y), 1e-4));
    CHECK_THAT(d(grey.y), WithinAbs(d(grey.z), 1e-4));
    checkVec(saturate(c, 1.0f), c, 1e-4);
    const glm::vec3 half = rgbToOklab(saturate(c, 0.5f));
    CHECK_THAT(d(half.y), WithinAbs(d(rgbToOklab(c).y) * 0.5, 1e-4));
    CHECK_THAT(d(half.z), WithinAbs(d(rgbToOklab(c).z) * 0.5, 1e-4));
}

TEST_CASE("lighten, contrast and mixOklab", "[color]") {
    const glm::vec3 c{0.3f, 0.4f, 0.5f};
    CHECK_THAT(d(rgbToOklab(lighten(c, 0.1f)).x), WithinAbs(d(rgbToOklab(c).x) + 0.1, 1e-4));
    checkVec(lighten(c, 0.0f), c, 1e-4);
    checkVec(lighten({0.0f, 0.0f, 0.0f}, -0.5f), {0.0f, 0.0f, 0.0f}); // clamped

    checkVec(contrast(c, 1.0f), c);
    checkVec(contrast(c, 2.0f), {0.1f, 0.3f, 0.5f}, 1e-6);
    checkVec(contrast(c, 2.0f, 0.4f), {0.2f, 0.4f, 0.6f}, 1e-6);
    checkVec(contrast({0.1f, 0.1f, 0.1f}, 4.0f), {0.0f, 0.0f, 0.0f}); // clamped
    checkVec(contrast(c, 0.0f), {0.5f, 0.5f, 0.5f});

    const glm::vec3 a{1.0f, 0.0f, 0.0f}, b{0.0f, 0.0f, 1.0f};
    checkVec(mixOklab(a, b, 0.0f), a, 1e-4);
    checkVec(mixOklab(a, b, 1.0f), b, 1e-4);
    const glm::vec3 mid = mixOklab(a, b, 0.5f);
    const glm::vec3 midLab = rgbToOklab(mid);
    const glm::vec3 expectedLab = (rgbToOklab(a) + rgbToOklab(b)) * 0.5f;
    // The midpoint is in gamut for these two, so the OKLab midpoint is reproduced exactly.
    checkVec(midLab, expectedLab, 1e-4);
    // Perceptual and linear midpoints differ.
    CHECK(glm::length(mid - (a + b) * 0.5f) > 0.05f);
}

// ---- palette and ramp -------------------------------------------------------------------------

TEST_CASE("CosinePalette follows a + b cos(2 pi (c t + d))", "[color]") {
    CosinePalette p;
    checkVec(p.sample(0.0f),
             {0.5f + 0.5f * std::cos(0.0f), 0.5f + 0.5f * std::cos(6.2831853f * 0.33f),
              0.5f + 0.5f * std::cos(6.2831853f * 0.67f)},
             1e-5);
    // Period 1 in t.
    checkVec(p.sample(0.3f), p.sample(1.3f), 1e-5);
    CosinePalette q{{0.2f, 0.3f, 0.4f}, {0.1f, 0.2f, 0.3f}, {2.0f, 1.0f, 0.5f}, {0.0f, 0.25f, 0.5f}};
    checkVec(q.sample(0.0f), {0.3f, 0.3f, 0.1f}, 1e-5);
    checkVec(q.sample(0.25f), {0.2f - 0.1f, 0.3f - 0.2f, 0.4f + 0.3f * std::cos(6.2831853f * 0.625f)}, 1e-5);
}

TEST_CASE("Ramp: stops, clamping, cyclic wrap, offset", "[color]") {
    const Ramp linear = threeStopRamp(false, false);
    checkVec(linear.sample(0.0f), {1.0f, 0.0f, 0.0f});
    checkVec(linear.sample(0.5f), {0.0f, 1.0f, 0.0f});
    checkVec(linear.sample(1.0f), {0.0f, 0.0f, 1.0f});
    checkVec(linear.sample(0.25f), {0.5f, 0.5f, 0.0f}, 1e-6);
    checkVec(linear.sample(0.75f), {0.0f, 0.5f, 0.5f}, 1e-6);
    // Clamped outside [0, 1].
    checkVec(linear.sample(-0.5f), {1.0f, 0.0f, 0.0f});
    checkVec(linear.sample(1.5f), {0.0f, 0.0f, 1.0f});
    // Offset adds before clamping.
    checkVec(linear.sample(0.0f, 0.25f), linear.sample(0.25f));
    checkVec(linear.sample(0.9f, 0.5f), {0.0f, 0.0f, 1.0f});

    const Ramp cyclic = threeStopRamp(false, true);
    checkVec(cyclic.sample(0.25f), cyclic.sample(1.25f), 1e-6);
    checkVec(cyclic.sample(0.25f), cyclic.sample(-0.75f), 1e-6);
    checkVec(cyclic.sample(0.75f, 0.5f), cyclic.sample(0.25f), 1e-6);
    // With stops at 0 and 1 the wrap segment is degenerate: t = 1 wraps to the first stop.
    checkVec(cyclic.sample(1.0f), {1.0f, 0.0f, 0.0f});

    // Interior stops: the wrap segment blends last → first across t = 1.
    Ramp inner;
    inner.stopCount = 2;
    inner.stops[0] = {0.25f, {1.0f, 0.0f, 0.0f}};
    inner.stops[1] = {0.75f, {0.0f, 0.0f, 1.0f}};
    inner.perceptual = false;
    inner.cyclic = true;
    checkVec(inner.sample(0.5f), {0.5f, 0.0f, 0.5f}, 1e-6);
    checkVec(inner.sample(0.0f), {0.5f, 0.0f, 0.5f}, 1e-6); // halfway through the wrap
    checkVec(inner.sample(0.875f), {0.25f, 0.0f, 0.75f}, 1e-6);
    checkVec(inner.sample(0.125f), {0.75f, 0.0f, 0.25f}, 1e-6);
    inner.cyclic = false;
    checkVec(inner.sample(0.0f), {1.0f, 0.0f, 0.0f});
    checkVec(inner.sample(1.0f), {0.0f, 0.0f, 1.0f});

    // Perceptual mixing differs from linear between stops but agrees at the stops.
    const Ramp perceptual = threeStopRamp(true, false);
    checkVec(perceptual.sample(0.0f), {1.0f, 0.0f, 0.0f}, 1e-4);
    checkVec(perceptual.sample(0.5f), {0.0f, 1.0f, 0.0f}, 1e-4);
    checkVec(perceptual.sample(1.0f), {0.0f, 0.0f, 1.0f}, 1e-4);
    CHECK(glm::length(perceptual.sample(0.25f) - linear.sample(0.25f)) > 0.05f);
    checkVec(perceptual.sample(0.25f), mixOklab({1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.5f), 1e-5);

    // Degenerate ramps.
    Ramp empty;
    checkVec(empty.sample(0.3f), {0.0f, 0.0f, 0.0f});
    Ramp single;
    single.stopCount = 1;
    single.stops[0] = {0.5f, {0.2f, 0.4f, 0.6f}};
    checkVec(single.sample(0.0f), {0.2f, 0.4f, 0.6f});
    checkVec(single.sample(1.0f), {0.2f, 0.4f, 0.6f});
}
