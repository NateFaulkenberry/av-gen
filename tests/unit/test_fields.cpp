#include "spatial/field.hpp"

#include "core/noise.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <glm/gtc/constants.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

using namespace avgen;
using namespace avgen::spatial;
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

void checkVec4(const glm::vec4& v, const glm::vec4& expected, double tol = 1e-5) {
    checkVec(glm::vec3(v), glm::vec3(expected), tol);
    CHECK_THAT(d(v.w), WithinAbs(d(expected.w), tol));
}

// A field of `kind` with no falloff and unit strength.
FieldSpec make(FieldKind kind, const char* name = "f") {
    FieldSpec f;
    f.name = name;
    f.kind = kind;
    f.falloff.kind = FalloffKind::None;
    return f;
}

constexpr float kPi = glm::pi<float>();

} // namespace

TEST_CASE("Field enum names round trip", "[spatial][fields]") {
    for (int k = 0; k <= static_cast<int>(FieldKind::Compound); ++k) {
        const auto kind = static_cast<FieldKind>(k);
        CHECK(fieldKindFromName(fieldKindName(kind)) == kind);
    }
    CHECK(fieldKindName(FieldKind::LinearGradient) == std::string("linearGradient"));
    CHECK(fieldKindName(FieldKind::SdfDistance) == std::string("sdfDistance"));
    CHECK(fieldKindName(FieldKind::CurlNoise) == std::string("curlNoise"));
    CHECK(fieldKindName(FieldKind::WaveVector) == std::string("waveVector"));
    CHECK_FALSE(fieldKindFromName("Radial").has_value());
    for (int k = 0; k <= static_cast<int>(FalloffKind::NoiseModulated); ++k) {
        const auto kind = static_cast<FalloffKind>(k);
        CHECK(falloffKindFromName(falloffKindName(kind)) == kind);
    }
    CHECK(falloffKindName(FalloffKind::EaseInOut) == std::string("easeInOut"));
    CHECK(fieldSpaceFromName("local") == FieldSpace::Local);
    CHECK(fieldSpaceFromName("world") == FieldSpace::World);
    CHECK_FALSE(fieldSpaceFromName("object").has_value());
    for (const auto g : {WaveGeometry::Planar, WaveGeometry::Radial, WaveGeometry::Spherical, WaveGeometry::Cylindrical}) {
        CHECK(waveGeometryFromName(waveGeometryName(g)) == g);
    }
    for (const auto s : {WaveShape::Sine, WaveShape::Pulse, WaveShape::Triangle}) {
        CHECK(waveShapeFromName(waveShapeName(s)) == s);
    }
    for (const auto c : {FieldCombine::Add, FieldCombine::Multiply, FieldCombine::Max, FieldCombine::Min, FieldCombine::Mix,
                         FieldCombine::Average}) {
        CHECK(fieldCombineFromName(fieldCombineName(c)) == c);
    }
    CHECK(fieldTypeOf(FieldKind::Radial) == FieldType::Scalar);
    CHECK(fieldTypeOf(FieldKind::Wave) == FieldType::Scalar);
    CHECK(fieldTypeOf(FieldKind::Vortex) == FieldType::Vector);
    CHECK(fieldTypeOf(FieldKind::WaveVector) == FieldType::Vector);
    CHECK(fieldTypeOf(FieldKind::PositionColor) == FieldType::Color);
    CHECK(fieldTypeOf(FieldKind::Compound) == FieldType::Scalar);
    CHECK(fieldTypeName(FieldType::Color) == std::string("color"));
}

TEST_CASE("Falloff curves: endpoints and monotonicity", "[spatial][fields]") {
    const glm::vec4 curve(0.8f, 0.6f, 0.4f, 0.2f);
    for (const auto kind : {FalloffKind::Linear, FalloffKind::Smoothstep, FalloffKind::Smooth, FalloffKind::EaseIn,
                            FalloffKind::EaseOut, FalloffKind::EaseInOut, FalloffKind::Exponential, FalloffKind::NoiseModulated}) {
        INFO(falloffKindName(kind));
        CHECK_THAT(d(falloffCurve(kind, 0.0f, 2.0f, curve)), WithinAbs(1.0, 1e-6));
        CHECK_THAT(d(falloffCurve(kind, 1.0f, 2.0f, curve)), WithinAbs(0.0, 1e-6));
        float previous = 1.0f;
        for (int i = 1; i <= 20; ++i) {
            const float v = falloffCurve(kind, static_cast<float>(i) / 20.0f, 2.0f, curve);
            CHECK(v <= previous + 1e-6f);
            previous = v;
        }
    }
    CHECK(falloffCurve(FalloffKind::None, 0.5f, 2.0f, curve) == 1.0f);
    CHECK(falloffCurve(FalloffKind::Linear, 0.25f, 2.0f, curve) == 0.75f);
    CHECK_THAT(d(falloffCurve(FalloffKind::Smoothstep, 0.5f, 2.0f, curve)), WithinAbs(0.5, 1e-6));
    CHECK_THAT(d(falloffCurve(FalloffKind::Smooth, 0.5f, 2.0f, curve)), WithinAbs(0.5, 1e-6));
    CHECK_THAT(d(falloffCurve(FalloffKind::EaseIn, 0.5f, 2.0f, curve)), WithinAbs(0.875, 1e-6));
    CHECK_THAT(d(falloffCurve(FalloffKind::EaseOut, 0.5f, 2.0f, curve)), WithinAbs(0.125, 1e-6));
    CHECK_THAT(d(falloffCurve(FalloffKind::EaseInOut, 0.5f, 2.0f, curve)), WithinAbs(0.5, 1e-6));
    CHECK_THAT(d(falloffCurve(FalloffKind::EaseInOut, 0.25f, 2.0f, curve)), WithinAbs(1.0 - 4.0 * 0.015625, 1e-6));
    CHECK_THAT(d(falloffCurve(FalloffKind::Exponential, 0.5f, 3.0f, curve)), WithinAbs(0.125, 1e-6));
    // CustomCurve: Bezier through 1, curve.x, curve.y, curve.z (curve.w unused).
    CHECK_THAT(d(falloffCurve(FalloffKind::CustomCurve, 0.0f, 2.0f, curve)), WithinAbs(1.0, 1e-6));
    CHECK_THAT(d(falloffCurve(FalloffKind::CustomCurve, 1.0f, 2.0f, curve)), WithinAbs(0.4, 1e-6));
    const double mid = 0.125 * 1.0 + 3.0 * 0.125 * 0.8 + 3.0 * 0.125 * 0.6 + 0.125 * 0.4;
    CHECK_THAT(d(falloffCurve(FalloffKind::CustomCurve, 0.5f, 2.0f, curve)), WithinAbs(mid, 1e-6));
    // Inputs outside [0, 1] are clamped.
    CHECK(falloffCurve(FalloffKind::Linear, -1.0f, 2.0f, curve) == 1.0f);
    CHECK(falloffCurve(FalloffKind::Linear, 2.0f, 2.0f, curve) == 0.0f);
}

TEST_CASE("Falloff weight: inner/outer, None, degenerate ranges, noise modulation", "[spatial][fields]") {
    Falloff f;
    f.kind = FalloffKind::Linear;
    f.inner = 1.0f;
    f.outer = 3.0f;
    CHECK(f.weight(0.0f) == 1.0f);
    CHECK(f.weight(1.0f) == 1.0f);
    CHECK_THAT(d(f.weight(2.0f)), WithinAbs(0.5, 1e-6));
    CHECK(f.weight(3.0f) == 0.0f);
    CHECK(f.weight(5.0f) == 0.0f);
    f.kind = FalloffKind::None;
    CHECK(f.weight(100.0f) == 1.0f);
    f.kind = FalloffKind::Smoothstep;
    f.outer = 1.0f; // outer <= inner: a step at inner
    CHECK(f.weight(0.5f) == 1.0f);
    CHECK(f.weight(1.0f) == 1.0f);
    CHECK(f.weight(1.001f) == 0.0f);
    f.kind = FalloffKind::NoiseModulated;
    f.inner = 0.0f;
    f.outer = 4.0f;
    f.noiseAmount = 2.0f;
    f.noiseScale = 2.0f;
    const glm::vec3 p(0.3f, 1.2f, -0.7f);
    const float expected =
        falloffCurve(FalloffKind::Smoothstep, 0.5f, 2.0f, f.curve) *
        std::clamp(1.0f + 2.0f * (noise::fbm3(p * 2.0f, 9) * 2.0f - 1.0f), 0.0f, 1.0f);
    CHECK_THAT(d(f.weight(2.0f, p, 9)), WithinAbs(d(expected), 1e-6));
    bool seedMatters = false;
    for (std::uint32_t seed = 0; seed < 16 && !seedMatters; ++seed) {
        seedMatters = f.weight(2.0f, p, seed) != f.weight(2.0f, p, 9);
    }
    CHECK(seedMatters);
    CHECK(f.weight(2.0f, p, 9) <= 0.5f);
    CHECK(f.weight(0.0f, p, 9) == 1.0f);
}

TEST_CASE("Scalar field kinds sample the documented shapes", "[spatial][fields]") {
    CHECK(sampleScalar(make(FieldKind::Constant), {3.0f, 4.0f, 5.0f}, 0.0) == 1.0f);

    FieldSpec lg = make(FieldKind::LinearGradient);
    lg.axis = {0.0f, 1.0f, 0.0f};
    lg.length = 4.0f;
    CHECK(sampleScalar(lg, {0.0f, -2.0f, 0.0f}, 0.0) == 0.0f);
    CHECK(sampleScalar(lg, {7.0f, 0.0f, 0.0f}, 0.0) == 0.5f);
    CHECK(sampleScalar(lg, {0.0f, 2.0f, 0.0f}, 0.0) == 1.0f);
    CHECK(sampleScalar(lg, {0.0f, 9.0f, 0.0f}, 0.0) == 1.0f);
    CHECK(sampleScalar(lg, {0.0f, 1.0f, 0.0f}, 0.0) == 0.75f);

    FieldSpec radial = make(FieldKind::Radial);
    radial.radius = 2.0f;
    CHECK(sampleScalar(radial, {0.0f, 0.0f, 0.0f}, 0.0) == 1.0f);
    CHECK(sampleScalar(radial, {1.0f, 0.0f, 0.0f}, 0.0) == 0.5f);
    CHECK(sampleScalar(radial, {0.0f, 0.0f, 3.0f}, 0.0) == 0.0f);
    radial.point = {1.0f, 0.0f, 0.0f};
    CHECK(sampleScalar(radial, {1.0f, 0.0f, 0.0f}, 0.0) == 1.0f);

    FieldSpec box = make(FieldKind::Box);
    box.size = {1.0f, 1.0f, 1.0f};
    box.softness = 0.5f;
    CHECK(sampleScalar(box, {0.5f, -0.5f, 0.9f}, 0.0) == 1.0f);
    CHECK_THAT(d(sampleScalar(box, {1.25f, 0.0f, 0.0f}, 0.0)), WithinAbs(0.5, 1e-6));
    CHECK(sampleScalar(box, {0.0f, 2.0f, 0.0f}, 0.0) == 0.0f);

    FieldSpec sphere = make(FieldKind::Sphere);
    sphere.radius = 1.0f;
    sphere.softness = 1.0f;
    CHECK(sampleScalar(sphere, {0.5f, 0.0f, 0.0f}, 0.0) == 1.0f);
    CHECK_THAT(d(sampleScalar(sphere, {0.0f, 1.5f, 0.0f}, 0.0)), WithinAbs(0.5, 1e-6));
    CHECK(sampleScalar(sphere, {0.0f, 0.0f, 3.0f}, 0.0) == 0.0f);

    FieldSpec plane = make(FieldKind::Plane);
    plane.softness = 2.0f;
    CHECK(sampleScalar(plane, {0.0f, -1.0f, 0.0f}, 0.0) == 0.0f);
    CHECK(sampleScalar(plane, {0.0f, 1.0f, 0.0f}, 0.0) == 0.5f);
    CHECK(sampleScalar(plane, {0.0f, 5.0f, 0.0f}, 0.0) == 1.0f);

    FieldSpec noiseField = make(FieldKind::Noise);
    noiseField.frequency = 0.5f;
    noiseField.seed = 3;
    noiseField.speed = 2.0f;
    noiseField.phase = 0.25f;
    const glm::vec3 p(1.3f, -0.4f, 2.2f);
    const float tau = 2.0f * 1.5f + 0.25f;
    CHECK(sampleScalar(noiseField, p, 1.5) == noise::fbm3(p * 0.5f + tau * glm::vec3(1.0f, 0.7f, 1.3f), 3));
    CHECK(sampleScalar(noiseField, p, 0.0) != sampleScalar(noiseField, p, 1.5));
    for (int i = 0; i < 20; ++i) {
        const float v = sampleScalar(noiseField, p * static_cast<float>(i), 0.0);
        CHECK(v >= 0.0f);
        CHECK(v <= 1.0f);
    }

    FieldSpec voronoi = make(FieldKind::Voronoi);
    voronoi.frequency = 1.0f;
    voronoi.seed = 5;
    CHECK(sampleScalar(voronoi, p, 0.0) == std::clamp(noise::voronoiF1(p, 5), 0.0f, 1.0f));

    FieldSpec dist = make(FieldKind::Distance);
    dist.radius = 4.0f;
    dist.point = {0.0f, 0.0f, 1.0f};
    CHECK(sampleScalar(dist, {2.0f, 0.0f, 1.0f}, 0.0) == 0.5f);
    CHECK(sampleScalar(dist, {9.0f, 0.0f, 1.0f}, 0.0) == 1.0f);

    FieldSpec sdf = make(FieldKind::SdfDistance);
    sdf.reference = "blob";
    CHECK(sampleScalar(sdf, p, 0.0) == 0.0f);
}

TEST_CASE("Strength, falloff distance measures and invert", "[spatial][fields]") {
    FieldSpec radial = make(FieldKind::Radial);
    radial.radius = 2.0f;
    radial.strength = 2.0f;
    radial.falloff.kind = FalloffKind::Linear;
    radial.falloff.inner = 0.0f;
    radial.falloff.outer = 2.0f;
    // shape 0.5, weight 2 * 0.5.
    CHECK_THAT(d(sampleScalar(radial, {1.0f, 0.0f, 0.0f}, 0.0)), WithinAbs(0.5, 1e-6));
    CHECK_THAT(d(sampleWeight(radial, {1.0f, 0.0f, 0.0f}, 0.0)), WithinAbs(1.0, 1e-6));
    CHECK(sampleWeight(radial, {0.0f, 0.0f, 0.0f}, 0.0) == 2.0f);
    CHECK(sampleWeight(radial, {5.0f, 0.0f, 0.0f}, 0.0) == 0.0f);

    // Gradient-like kinds measure |dot(q, axis)|: far along x is still inside.
    FieldSpec plane = make(FieldKind::Plane);
    plane.falloff.kind = FalloffKind::Linear;
    plane.falloff.outer = 1.0f;
    plane.softness = 0.5f;
    CHECK(sampleScalar(plane, {100.0f, 0.25f, 0.0f}, 0.0) == 0.5f * 0.75f);
    CHECK(sampleScalar(plane, {0.0f, 2.0f, 0.0f}, 0.0) == 0.0f);

    // Box measures the Chebyshev distance outside the box.
    FieldSpec box = make(FieldKind::Box);
    box.size = {1.0f, 1.0f, 1.0f};
    box.softness = 10.0f;
    box.falloff.kind = FalloffKind::Linear;
    box.falloff.outer = 2.0f;
    CHECK(sampleWeight(box, {0.9f, 0.9f, 0.9f}, 0.0) == 1.0f);
    CHECK_THAT(d(sampleWeight(box, {2.0f, 0.0f, 0.0f}, 0.0)), WithinAbs(0.5, 1e-6));
    CHECK(sampleWeight(box, {3.0f, 0.0f, 0.0f}, 0.0) == 0.0f);

    // Invert: scalar 1 - v, vector -v, colour swaps a/b.
    FieldSpec constant = make(FieldKind::Constant);
    constant.invert = true;
    CHECK(sampleScalar(constant, {0.0f, 0.0f, 0.0f}, 0.0) == 0.0f);
    radial.invert = true;
    radial.strength = 1.0f;
    radial.falloff.kind = FalloffKind::None;
    CHECK(sampleScalar(radial, {1.0f, 0.0f, 0.0f}, 0.0) == 0.5f);
    CHECK(sampleScalar(radial, {0.0f, 0.0f, 0.0f}, 0.0) == 0.0f);
    FieldSpec dir = make(FieldKind::Direction);
    dir.invert = true;
    checkVec(sampleVector(dir, {0.0f, 0.0f, 0.0f}, 0.0), {0.0f, -1.0f, 0.0f});
    FieldSpec cc = make(FieldKind::ConstantColor);
    cc.colorA = {1.0f, 0.0f, 0.0f, 1.0f};
    cc.colorB = {0.0f, 0.0f, 1.0f, 1.0f};
    cc.invert = true;
    checkVec4(sampleColor(cc, {0.0f, 0.0f, 0.0f}, 0.0), {0.0f, 0.0f, 1.0f, 1.0f});
    // A disabled field samples as zero.
    constant.enabled = false;
    constant.invert = false;
    CHECK(sampleScalar(constant, {0.0f, 0.0f, 0.0f}, 0.0) == 0.0f);
}

TEST_CASE("Field local frame: position, rotation, scale", "[spatial][fields]") {
    FieldSpec radial = make(FieldKind::Radial);
    radial.radius = 2.0f;
    radial.position = {5.0f, 0.0f, 0.0f};
    radial.scale = {2.0f, 2.0f, 2.0f};
    // q = (6 - 5) / 2 = 0.5 -> 1 - 0.25.
    CHECK_THAT(d(sampleScalar(radial, {6.0f, 0.0f, 0.0f}, 0.0)), WithinAbs(0.75, 1e-6));
    CHECK_THAT(d(sampleScalar(radial, {5.0f, 0.0f, 0.0f}, 0.0)), WithinAbs(1.0, 1e-6));
    // The transform matrices are inverses.
    const glm::mat4 id = radial.localToWorld() * radial.worldToLocal();
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            CHECK_THAT(d(id[c][r]), WithinAbs(c == r ? 1.0 : 0.0, 1e-6));
        }
    }
    const glm::vec3 local = glm::vec3(radial.worldToLocal() * glm::vec4(7.0f, 2.0f, -4.0f, 1.0f));
    checkVec(local, {1.0f, 1.0f, -2.0f});

    // Rotation: a Direction field's axis (local +Y) rotated 90 degrees about Z points to world -X;
    // the scale does not affect the direction.
    FieldSpec dir = make(FieldKind::Direction);
    dir.rotationDegrees = {0.0f, 0.0f, 90.0f};
    dir.scale = {3.0f, 0.5f, 1.0f};
    checkVec(sampleVector(dir, {0.0f, 0.0f, 0.0f}, 0.0), {-1.0f, 0.0f, 0.0f});
    // Euler XYZ convention: glm::quat(radians(euler)).
    FieldSpec dir2 = make(FieldKind::Direction);
    dir2.axis = {1.0f, 0.0f, 0.0f};
    dir2.rotationDegrees = {30.0f, 45.0f, 60.0f};
    const glm::vec3 expected = glm::quat(glm::radians(dir2.rotationDegrees)) * glm::vec3(1.0f, 0.0f, 0.0f);
    checkVec(sampleVector(dir2, {0.0f, 0.0f, 0.0f}, 0.0), expected, 1e-5);
    // A vortex around a moved centre.
    FieldSpec vortex = make(FieldKind::Vortex);
    vortex.position = {0.0f, 0.0f, 10.0f};
    checkVec(sampleVector(vortex, {1.0f, 0.0f, 10.0f}, 0.0), {0.0f, 0.0f, -1.0f});
}

TEST_CASE("Vector field kinds", "[spatial][fields]") {
    checkVec(sampleVector(make(FieldKind::Direction), {5.0f, 5.0f, 5.0f}, 0.0), {0.0f, 1.0f, 0.0f});
    FieldSpec dir = make(FieldKind::Direction);
    dir.axis = {0.0f, 3.0f, 4.0f};
    dir.strength = 2.0f;
    checkVec(sampleVector(dir, {0.0f, 0.0f, 0.0f}, 0.0), {0.0f, 1.2f, 1.6f});
    dir.axis = glm::vec3(0.0f); // zero axis falls back to +Y
    checkVec(sampleVector(dir, {0.0f, 0.0f, 0.0f}, 0.0), {0.0f, 2.0f, 0.0f});

    checkVec(sampleVector(make(FieldKind::RadialVector), {2.0f, 0.0f, 0.0f}, 0.0), {1.0f, 0.0f, 0.0f});
    checkVec(sampleVector(make(FieldKind::RadialVector), {0.0f, 0.0f, 0.0f}, 0.0), {0.0f, 0.0f, 0.0f});
    checkVec(sampleVector(make(FieldKind::Attractor), {2.0f, 0.0f, 0.0f}, 0.0), {-1.0f, 0.0f, 0.0f});
    checkVec(sampleVector(make(FieldKind::Repulsor), {0.0f, 0.0f, -3.0f}, 0.0), {0.0f, 0.0f, -1.0f});
    FieldSpec attractor = make(FieldKind::Attractor);
    attractor.point = {1.0f, 0.0f, 0.0f};
    checkVec(sampleVector(attractor, {3.0f, 0.0f, 0.0f}, 0.0), {-1.0f, 0.0f, 0.0f});
    checkVec(sampleVector(make(FieldKind::Vortex), {1.0f, 0.0f, 0.0f}, 0.0), {0.0f, 0.0f, -1.0f});
    checkVec(sampleVector(make(FieldKind::Vortex), {0.0f, 0.0f, 1.0f}, 0.0), {1.0f, 0.0f, 0.0f});
    checkVec(sampleVector(make(FieldKind::Vortex), {0.0f, 5.0f, 0.0f}, 0.0), {0.0f, 0.0f, 0.0f});

    FieldSpec curl = make(FieldKind::CurlNoise);
    curl.frequency = 0.3f;
    curl.seed = 8;
    const glm::vec3 p(0.7f, 1.1f, -2.0f);
    CHECK(sampleVector(curl, p, 0.0) == noise::curlNoise(p * 0.3f, 8));
    CHECK(sampleVector(curl, p, 0.0) == sampleVector(curl, p, 0.0));
    CHECK(sampleVector(curl, p, 0.0) != sampleVector(curl, p * 2.0f, 0.0));

    FieldSpec spiral = make(FieldKind::Spiral);
    spiral.spiralBias = 1.0f;
    const float s = std::sqrt(0.5f);
    checkVec(sampleVector(spiral, {1.0f, 0.0f, 0.0f}, 0.0), {s, 0.0f, -s});
    spiral.spiralBias = 0.0f;
    checkVec(sampleVector(spiral, {1.0f, 0.0f, 0.0f}, 0.0), {0.0f, 0.0f, -1.0f});

    FieldSpec wv = make(FieldKind::WaveVector);
    wv.waveGeometry = WaveGeometry::Spherical;
    wv.wavelength = 4.0f;
    wv.waveSpeed = 0.0f;
    wv.waveWidth = 0.0f;
    checkVec(sampleVector(wv, {1.0f, 0.0f, 0.0f}, 0.0), {1.0f, 0.0f, 0.0f}); // sin(pi/2) outward
    checkVec(sampleVector(wv, {0.0f, 3.0f, 0.0f}, 0.0), {0.0f, -1.0f, 0.0f}); // sin(3pi/2)
    wv.waveGeometry = WaveGeometry::Planar;
    checkVec(sampleVector(wv, {9.0f, 1.0f, 0.0f}, 0.0), {0.0f, 1.0f, 0.0f});
    wv.waveGeometry = WaveGeometry::Radial;
    checkVec(sampleVector(wv, {0.0f, 9.0f, 1.0f}, 0.0), {0.0f, 0.0f, 1.0f});
    wv.waveGeometry = WaveGeometry::Cylindrical;
    checkVec(sampleVector(wv, {0.0f, 9.0f, 1.0f}, 0.0), {0.0f, 0.0f, 1.0f});
}

TEST_CASE("Wave fields: geometries, shapes, envelope and propagation in time", "[spatial][fields]") {
    FieldSpec wave = make(FieldKind::Wave);
    wave.waveGeometry = WaveGeometry::Planar;
    wave.waveShape = WaveShape::Sine;
    wave.wavelength = 4.0f;
    wave.waveSpeed = 0.0f;
    wave.waveWidth = 0.0f;
    wave.amplitude = 1.0f;
    CHECK_THAT(d(sampleScalar(wave, {0.0f, 1.0f, 0.0f}, 0.0)), WithinAbs(1.0, 1e-6));
    CHECK_THAT(d(sampleScalar(wave, {0.0f, 2.0f, 0.0f}, 0.0)), WithinAbs(0.0, 1e-6));
    CHECK_THAT(d(sampleScalar(wave, {0.0f, 3.0f, 0.0f}, 0.0)), WithinAbs(-1.0, 1e-6));
    wave.amplitude = 2.5f;
    CHECK_THAT(d(sampleScalar(wave, {0.0f, 1.0f, 0.0f}, 0.0)), WithinAbs(2.5, 1e-6));
    wave.amplitude = 1.0f;
    // Propagation: s = dist - origin - waveSpeed * t.
    wave.waveSpeed = 2.0f;
    CHECK_THAT(d(sampleScalar(wave, {0.0f, 3.0f, 0.0f}, 1.0)), WithinAbs(1.0, 1e-6));
    wave.waveOrigin = 1.0f;
    CHECK_THAT(d(sampleScalar(wave, {0.0f, 4.0f, 0.0f}, 1.0)), WithinAbs(1.0, 1e-6));
    wave.waveOrigin = 0.0f;
    wave.waveSpeed = 0.0f;

    wave.waveShape = WaveShape::Pulse;
    CHECK_THAT(d(sampleScalar(wave, {0.0f, 0.0f, 0.0f}, 0.0)), WithinAbs(1.0, 1e-6));
    CHECK(sampleScalar(wave, {0.0f, 2.0f, 0.0f}, 0.0) < 1e-3f);
    CHECK_THAT(d(sampleScalar(wave, {0.0f, 1.0f, 0.0f}, 0.0)), WithinAbs(std::exp(-(d(kPi) / 2.0) * (d(kPi) / 2.0)), 1e-5));
    wave.waveShape = WaveShape::Triangle;
    CHECK_THAT(d(sampleScalar(wave, {0.0f, 0.0f, 0.0f}, 0.0)), WithinAbs(0.0, 1e-6));
    CHECK_THAT(d(sampleScalar(wave, {0.0f, 1.0f, 0.0f}, 0.0)), WithinAbs(1.0, 1e-6));
    CHECK_THAT(d(sampleScalar(wave, {0.0f, 0.5f, 0.0f}, 0.0)), WithinAbs(0.5, 1e-6));
    CHECK_THAT(d(sampleScalar(wave, {0.0f, 3.0f, 0.0f}, 0.0)), WithinAbs(-1.0, 1e-6));

    // Envelope: smoothstep over waveWidth around the front.
    wave.waveShape = WaveShape::Sine;
    wave.waveWidth = 2.0f;
    CHECK_THAT(d(sampleScalar(wave, {0.0f, 1.0f, 0.0f}, 0.0)), WithinAbs(0.5, 1e-6)); // envelope at t = 0.5
    CHECK_THAT(d(sampleScalar(wave, {0.0f, 3.0f, 0.0f}, 0.0)), WithinAbs(0.0, 1e-6)); // beyond the width
    wave.waveWidth = 0.0f;

    // Geometries: radial ignores the axis component, spherical does not, cylindrical == radial.
    wave.waveGeometry = WaveGeometry::Radial;
    CHECK_THAT(d(sampleScalar(wave, {1.0f, 100.0f, 0.0f}, 0.0)), WithinAbs(1.0, 1e-5));
    wave.waveGeometry = WaveGeometry::Cylindrical;
    CHECK_THAT(d(sampleScalar(wave, {1.0f, 100.0f, 0.0f}, 0.0)), WithinAbs(1.0, 1e-5));
    wave.waveGeometry = WaveGeometry::Spherical;
    CHECK_THAT(d(sampleScalar(wave, {0.0f, 0.0f, 1.0f}, 0.0)), WithinAbs(1.0, 1e-6));
    wave.point = {0.0f, 0.0f, 1.0f};
    CHECK_THAT(d(sampleScalar(wave, {0.0f, 1.0f, 1.0f}, 0.0)), WithinAbs(1.0, 1e-6));
    // A pulse travelling outward: peak at r = waveSpeed * t.
    wave.point = glm::vec3(0.0f);
    wave.waveShape = WaveShape::Pulse;
    wave.waveSpeed = 2.0f;
    CHECK(sampleScalar(wave, {2.0f, 0.0f, 0.0f}, 0.0) < 1e-3f);
    CHECK_THAT(d(sampleScalar(wave, {2.0f, 0.0f, 0.0f}, 1.0)), WithinAbs(1.0, 1e-6));
    CHECK_THAT(d(sampleScalar(wave, {0.0f, 4.0f, 0.0f}, 2.0)), WithinAbs(1.0, 1e-6));
    CHECK(sampleScalar(wave, {0.0f, 0.0f, 0.0f}, 2.0) < 1e-3f);
}

TEST_CASE("Colour field kinds and cross-type reads", "[spatial][fields]") {
    const glm::vec4 red(1.0f, 0.0f, 0.0f, 1.0f);
    const glm::vec4 blue(0.0f, 0.0f, 1.0f, 1.0f);
    FieldSpec cc = make(FieldKind::ConstantColor);
    cc.colorA = red;
    cc.strength = 0.5f;
    checkVec4(sampleColor(cc, {1.0f, 2.0f, 3.0f}, 0.0), {1.0f, 0.0f, 0.0f, 0.5f});

    FieldSpec grad = make(FieldKind::Gradient);
    grad.colorA = red;
    grad.colorB = blue;
    grad.length = 2.0f;
    checkVec4(sampleColor(grad, {0.0f, -1.0f, 0.0f}, 0.0), red);
    checkVec4(sampleColor(grad, {0.0f, 1.0f, 0.0f}, 0.0), blue);
    checkVec4(sampleColor(grad, {3.0f, 0.0f, 0.0f}, 0.0), {0.5f, 0.0f, 0.5f, 1.0f});

    FieldSpec rg = make(FieldKind::RadialGradient);
    rg.colorA = red;
    rg.colorB = blue;
    rg.radius = 2.0f;
    checkVec4(sampleColor(rg, {0.0f, 0.0f, 0.0f}, 0.0), red);
    checkVec4(sampleColor(rg, {1.0f, 0.0f, 0.0f}, 0.0), {0.5f, 0.0f, 0.5f, 1.0f});
    checkVec4(sampleColor(rg, {0.0f, 9.0f, 0.0f}, 0.0), blue);

    FieldSpec nc = make(FieldKind::NoiseColor);
    nc.colorA = red;
    nc.colorB = blue;
    nc.frequency = 1.0f;
    nc.seed = 2;
    const glm::vec3 p(0.4f, 0.9f, 1.7f);
    const float n = noise::fbm3(p, 2);
    checkVec4(sampleColor(nc, p, 0.0), {1.0f - n, 0.0f, n, 1.0f}, 1e-6);

    FieldSpec pc = make(FieldKind::PositionColor);
    pc.frequency = 1.0f;
    checkVec4(sampleColor(pc, {1.25f, 2.5f, -0.25f}, 0.0), {0.25f, 0.5f, 0.75f, 1.0f});

    // Cross-type reads.
    FieldSpec constant = make(FieldKind::Constant);
    constant.axis = {0.0f, 0.0f, 2.0f};
    constant.strength = 3.0f;
    checkVec(sampleVector(constant, {0.0f, 0.0f, 0.0f}, 0.0), {0.0f, 0.0f, 3.0f}); // s * unit axis
    constant.colorA = red;
    constant.colorB = blue;
    checkVec4(sampleColor(constant, {0.0f, 0.0f, 0.0f}, 0.0), {0.0f, 0.0f, 1.0f, 3.0f}); // mix by saturate(s)
    FieldSpec dir = make(FieldKind::Direction);
    dir.strength = 2.0f;
    CHECK(sampleScalar(dir, {0.0f, 0.0f, 0.0f}, 0.0) == 2.0f); // length
    checkVec4(sampleColor(dir, {0.0f, 0.0f, 0.0f}, 0.0), {0.5f, 1.5f, 0.5f, 2.0f}); // v * 0.5 + 0.5
    FieldSpec white = make(FieldKind::ConstantColor);
    white.strength = 0.5f;
    CHECK_THAT(d(sampleScalar(white, {0.0f, 0.0f, 0.0f}, 0.0)), WithinAbs(0.5, 1e-6)); // luminance * alpha
    cc.strength = 1.0f;
    // colour as vector = luminance * alpha along the (unit) axis, like the GPU
    checkVec(sampleVector(cc, {0.0f, 0.0f, 0.0f}, 0.0), {0.0f, 0.2126f, 0.0f});
}

TEST_CASE("Compound fields combine children and resolve by name", "[spatial][fields]") {
    FieldSet set;
    FieldSpec a = make(FieldKind::Constant, "a");
    a.strength = 2.0f;
    FieldSpec b = make(FieldKind::Constant, "b");
    b.strength = 3.0f;
    FieldSpec c = make(FieldKind::Compound, "c");
    c.children = {"a", "b", "missing"};
    set.fields = {a, b, c};
    CHECK(set.find("b") == &set.fields[1]);
    CHECK(set.find("zzz") == nullptr);
    CHECK(set.indexOf("c") == 2);
    CHECK(set.indexOf("zzz") == -1);

    const glm::vec3 p(0.0f);
    const auto sample = [&](FieldCombine combine, float mix = 0.5f) {
        set.fields[2].combine = combine;
        set.fields[2].mix = mix;
        return sampleScalar(set.fields[2], p, 0.0, &set);
    };
    CHECK(sample(FieldCombine::Add) == 5.0f);
    CHECK(sample(FieldCombine::Multiply) == 6.0f);
    CHECK(sample(FieldCombine::Max) == 3.0f);
    CHECK(sample(FieldCombine::Min) == 2.0f);
    CHECK(sample(FieldCombine::Mix, 0.25f) == 2.25f);
    CHECK(sample(FieldCombine::Average) == 2.5f);
    // Without a set there are no children; own strength/falloff/invert apply.
    CHECK(sampleScalar(set.fields[2], p, 0.0) == 0.0f);
    set.fields[2].combine = FieldCombine::Add;
    set.fields[2].strength = 2.0f;
    CHECK(sampleScalar(set.fields[2], p, 0.0, &set) == 10.0f);
    set.fields[2].strength = 1.0f;
    set.fields[2].invert = true;
    CHECK(sampleScalar(set.fields[2], p, 0.0, &set) == -4.0f);
    set.fields[2].invert = false;
    set.fields[2].falloff.kind = FalloffKind::Linear;
    set.fields[2].falloff.outer = 2.0f;
    CHECK_THAT(d(sampleScalar(set.fields[2], {1.0f, 0.0f, 0.0f}, 0.0, &set)), WithinAbs(2.5, 1e-6));
    set.fields[2].falloff.kind = FalloffKind::None;
    // Disabled children are skipped (Average over the rest).
    set.fields[0].enabled = false;
    set.fields[2].combine = FieldCombine::Average;
    CHECK(sampleScalar(set.fields[2], p, 0.0, &set) == 3.0f);
    set.fields[0].enabled = true;

    // Vector and colour compounds.
    FieldSpec d1 = make(FieldKind::Direction, "d1");
    FieldSpec d2 = make(FieldKind::Direction, "d2");
    d2.axis = {1.0f, 0.0f, 0.0f};
    FieldSpec vc = make(FieldKind::Compound, "vc");
    vc.children = {"d1", "d2"};
    FieldSpec c1 = make(FieldKind::ConstantColor, "c1");
    c1.colorA = {1.0f, 0.0f, 0.0f, 1.0f};
    FieldSpec c2 = make(FieldKind::ConstantColor, "c2");
    c2.colorA = {0.0f, 0.5f, 0.0f, 1.0f};
    c2.strength = 0.5f;
    FieldSpec ccomp = make(FieldKind::Compound, "cc");
    ccomp.children = {"c1", "c2"};
    set.fields = {d1, d2, vc, c1, c2, ccomp};
    checkVec(sampleVector(set.fields[2], p, 0.0, &set), {1.0f, 1.0f, 0.0f});
    checkVec4(sampleColor(set.fields[5], p, 0.0, &set), {1.0f, 0.5f, 0.0f, 1.0f}); // rgb sum, max alpha
    set.fields[5].combine = FieldCombine::Max;
    checkVec4(sampleColor(set.fields[5], p, 0.0, &set), {1.0f, 0.5f, 0.0f, 1.0f});
    set.fields[5].combine = FieldCombine::Multiply;
    checkVec4(sampleColor(set.fields[5], p, 0.0, &set), {0.0f, 0.0f, 0.0f, 1.0f});
    // A scalar compound over vector children takes their lengths.
    set.fields[2].combine = FieldCombine::Add;
    CHECK(sampleScalar(set.fields[2], p, 0.0, &set) == 2.0f);

    // Cycles terminate at depth 4 and yield 0 for the cyclic branch.
    FieldSpec self = make(FieldKind::Compound, "self");
    self.children = {"self", "k"};
    FieldSpec k = make(FieldKind::Constant, "k");
    FieldSet cyc;
    cyc.fields = {self, k};
    // Depths 0..3 each add k once; the self branch at depth > 4 (and k at depth 5) sample as 0.
    CHECK(sampleScalar(cyc.fields[0], p, 0.0, &cyc) == 4.0f);
    // At most kMaxCompoundChildren children are used.
    FieldSpec many = make(FieldKind::Compound, "many");
    many.children = {"k", "k", "k", "k", "k", "k"};
    cyc.fields.push_back(many);
    CHECK(sampleScalar(cyc.fields[2], p, 0.0, &cyc) == 4.0f);
}

TEST_CASE("packField fills every GPU field", "[spatial][fields]") {
    FieldSpec f;
    f.name = "w";
    f.kind = FieldKind::Wave;
    f.position = {1.0f, 2.0f, 3.0f};
    f.rotationDegrees = {0.0f, 90.0f, 0.0f};
    f.scale = {2.0f, 2.0f, 2.0f};
    f.strength = 1.5f;
    f.invert = true;
    f.falloff.kind = FalloffKind::Exponential;
    f.falloff.inner = 1.0f;
    f.falloff.outer = 6.0f;
    f.falloff.exponent = 3.0f;
    f.falloff.curve = {0.1f, 0.2f, 0.3f, 0.4f};
    f.falloff.noiseAmount = 0.7f;
    f.falloff.noiseScale = 1.3f;
    f.speed = 2.0f;
    f.phase = 0.5f;
    f.axis = {0.0f, 0.0f, 1.0f};
    f.point = {4.0f, 5.0f, 6.0f};
    f.radius = 7.0f;
    f.length = 8.0f;
    f.size = {9.0f, 10.0f, 11.0f};
    f.softness = 0.25f;
    f.frequency = 0.6f;
    f.seed = 42;
    f.spiralBias = 0.8f;
    f.waveGeometry = WaveGeometry::Cylindrical;
    f.waveShape = WaveShape::Triangle;
    f.amplitude = 1.1f;
    f.wavelength = 2.2f;
    f.waveSpeed = 3.3f;
    f.waveWidth = 4.4f;
    f.waveOrigin = 5.5f;
    f.colorA = {0.1f, 0.2f, 0.3f, 0.4f};
    f.colorB = {0.5f, 0.6f, 0.7f, 0.8f};
    f.children = {"b", "zzz", "a"};
    f.combine = FieldCombine::Mix;
    f.mix = 0.35f;
    FieldSet set;
    set.fields = {make(FieldKind::Constant, "a"), make(FieldKind::Constant, "b"), f};

    const FieldGpu g = packField(f, 2.0, &set);
    CHECK(g.kind == static_cast<std::uint32_t>(FieldKind::Wave));
    CHECK(g.type == static_cast<std::uint32_t>(FieldType::Scalar));
    CHECK(g.falloffKind == static_cast<std::uint32_t>(FalloffKind::Exponential));
    CHECK(g.seed == 42);
    CHECK(g.worldToLocal == f.worldToLocal());
    // Rotation-only rows: world = (dot(row0, v), dot(row1, v), dot(row2, v)); +90 about Y maps x -> -z.
    const glm::vec3 v(1.0f, 0.0f, 0.0f);
    const glm::vec3 rotated(glm::dot(glm::vec3(g.localToWorldRow0), v), glm::dot(glm::vec3(g.localToWorldRow1), v),
                            glm::dot(glm::vec3(g.localToWorldRow2), v));
    checkVec(rotated, {0.0f, 0.0f, -1.0f});
    CHECK(g.localToWorldRow0.w == 0.0f);
    CHECK(g.localToWorldRow1.w == 0.0f);
    CHECK(g.localToWorldRow2.w == 0.0f);
    CHECK(g.strengthInnerOuterTau == glm::vec4(1.5f, 1.0f, 6.0f, 2.0f * 2.0f + 0.5f));
    CHECK(g.axisRadius == glm::vec4(0.0f, 0.0f, 1.0f, 7.0f));
    CHECK(g.pointLength == glm::vec4(4.0f, 5.0f, 6.0f, 8.0f));
    CHECK(g.sizeSoftness == glm::vec4(9.0f, 10.0f, 11.0f, 0.25f));
    CHECK(g.freqExpInvertBias == glm::vec4(0.6f, 3.0f, 1.0f, 0.8f));
    CHECK(g.wave0 == glm::vec4(1.1f, 2.2f, 3.3f, 4.4f));
    CHECK(g.wave1 == glm::vec4(5.5f, 3.0f, 2.0f, 2.0f));
    CHECK(g.colorA == f.colorA);
    CHECK(g.colorB == f.colorB);
    CHECK(g.curve == f.falloff.curve);
    CHECK(g.noiseCombineMix == glm::vec4(0.7f, 1.3f, 4.0f, 0.35f));
    CHECK(g.children == glm::ivec4(1, -1, 0, -1));
    const FieldGpu noSet = packField(f, 0.0);
    CHECK(noSet.children == glm::ivec4(-1));
    CHECK(noSet.freqExpInvertBias.z == 1.0f);
    f.invert = false;
    CHECK(packField(f, 0.0).freqExpInvertBias.z == 0.0f);
    // A non-grid kind leaves the Grid members zero, so gridRes.w = 0 ("no grid bound") and the
    // shader never touches the grid table (ADR-032).
    CHECK(g.gridBounds0 == glm::vec4(0.0f));
    CHECK(g.gridBounds1 == glm::vec4(0.0f));
    CHECK(g.gridRes == glm::vec4(0.0f));
    CHECK(sizeof(FieldGpu) == 368);
}

TEST_CASE("packField points a Grid field at its grid's range of the table", "[spatial][fields]") {
    GridField a;
    a.name = "a";
    a.resolution = {4, 4, 4}; // 64 floats
    GridField b;
    b.name = "b";
    b.mode = GridMode::Vector;
    b.wrap = GridWrap::Wrap;
    b.resolution = {8, 4, 2};
    b.boundsMin = {-1.0f, -2.0f, -3.0f};
    b.boundsMax = {5.0f, 6.0f, 7.0f};

    FieldSpec f;
    f.name = "read";
    f.kind = FieldKind::Grid;
    f.reference = "b";
    FieldSet set;
    set.grids = {a, b};
    set.fields = {f};

    const FieldGpu g = packField(f, 0.0, &set);
    CHECK(g.kind == static_cast<std::uint32_t>(FieldKind::Grid));
    CHECK(g.type == static_cast<std::uint32_t>(FieldType::Vector)); // the bound grid's mode wins
    CHECK(g.gridBounds0 == glm::vec4(-1.0f, -2.0f, -3.0f, 64.0f));  // grid `a` comes first
    CHECK(g.gridBounds1 == glm::vec4(5.0f, 6.0f, 7.0f, 4.0f));      // vector cells are 4 floats
    CHECK(g.gridRes == glm::vec4(8.0f, 4.0f, 2.0f, 3.0f));          // 1 bound + 2 wrapping
    // Unbound (no set, or an unknown name) stays zero, which samples as 0.
    CHECK(packField(f, 0.0).gridRes.w == 0.0f);
    f.reference = "missing";
    CHECK(packField(f, 0.0, &set).gridRes.w == 0.0f);
}

TEST_CASE("FieldSpec JSON round trip, structural hash and validation", "[spatial][fields]") {
    FieldSpec f;
    f.name = "gust";
    f.kind = FieldKind::Compound;
    f.enabled = false;
    f.space = FieldSpace::Local;
    f.position = {1.0f, 2.0f, 3.0f};
    f.rotationDegrees = {10.0f, 20.0f, 30.0f};
    f.scale = {2.0f, 3.0f, 4.0f};
    f.strength = 1.5f;
    f.invert = true;
    f.falloff.kind = FalloffKind::CustomCurve;
    f.falloff.inner = 1.0f;
    f.falloff.outer = 6.0f;
    f.falloff.exponent = 3.0f;
    f.falloff.curve = {0.1f, 0.2f, 0.3f, 0.4f};
    f.falloff.noiseAmount = 0.7f;
    f.falloff.noiseScale = 1.3f;
    f.speed = 2.0f;
    f.phase = 0.5f;
    f.axis = {0.0f, 0.0f, 1.0f};
    f.point = {4.0f, 5.0f, 6.0f};
    f.radius = 7.0f;
    f.length = 8.0f;
    f.size = {9.0f, 10.0f, 11.0f};
    f.softness = 0.25f;
    f.frequency = 0.6f;
    f.seed = 42;
    f.spiralBias = 0.8f;
    f.waveGeometry = WaveGeometry::Spherical;
    f.waveShape = WaveShape::Pulse;
    f.amplitude = 1.1f;
    f.wavelength = 2.2f;
    f.waveSpeed = 3.3f;
    f.waveWidth = 4.4f;
    f.waveOrigin = 5.5f;
    f.colorA = {0.1f, 0.2f, 0.3f, 0.4f};
    f.colorB = {0.5f, 0.6f, 0.7f, 0.8f};
    f.children = {"a", "b"};
    f.combine = FieldCombine::Average;
    f.mix = 0.35f;
    f.reference = "blob";

    const nlohmann::json j = f.toJson();
    CHECK(j.at("kind") == "compound");
    CHECK(j.at("space") == "local");
    CHECK(j.at("falloff").at("kind") == "customCurve");
    CHECK(j.at("waveGeometry") == "spherical");
    CHECK(j.at("waveShape") == "pulse");
    CHECK(j.at("combine") == "average");
    CHECK(j.at("children").size() == 2);
    auto back = FieldSpec::fromJson(j);
    REQUIRE(back.has_value());
    const FieldSpec& b = *back;
    CHECK(b.name == "gust");
    CHECK(b.kind == FieldKind::Compound);
    CHECK_FALSE(b.enabled);
    CHECK(b.space == FieldSpace::Local);
    CHECK(b.position == f.position);
    CHECK(b.rotationDegrees == f.rotationDegrees);
    CHECK(b.scale == f.scale);
    CHECK(b.strength == 1.5f);
    CHECK(b.invert);
    CHECK(b.falloff.kind == FalloffKind::CustomCurve);
    CHECK(b.falloff.inner == 1.0f);
    CHECK(b.falloff.outer == 6.0f);
    CHECK(b.falloff.exponent == 3.0f);
    CHECK(b.falloff.curve == f.falloff.curve);
    CHECK(b.falloff.noiseAmount == 0.7f);
    CHECK(b.falloff.noiseScale == 1.3f);
    CHECK(b.speed == 2.0f);
    CHECK(b.phase == 0.5f);
    CHECK(b.axis == f.axis);
    CHECK(b.point == f.point);
    CHECK(b.radius == 7.0f);
    CHECK(b.length == 8.0f);
    CHECK(b.size == f.size);
    CHECK(b.softness == 0.25f);
    CHECK(b.frequency == 0.6f);
    CHECK(b.seed == 42);
    CHECK(b.spiralBias == 0.8f);
    CHECK(b.waveGeometry == WaveGeometry::Spherical);
    CHECK(b.waveShape == WaveShape::Pulse);
    CHECK(b.amplitude == 1.1f);
    CHECK(b.wavelength == 2.2f);
    CHECK(b.waveSpeed == 3.3f);
    CHECK(b.waveWidth == 4.4f);
    CHECK(b.waveOrigin == 5.5f);
    CHECK(b.colorA == f.colorA);
    CHECK(b.colorB == f.colorB);
    CHECK(b.children == f.children);
    CHECK(b.combine == FieldCombine::Average);
    CHECK(b.mix == 0.35f);
    CHECK(b.reference == "blob");
    CHECK(b.structuralHash() == f.structuralHash());

    // Defaults survive a minimal file; the hash is sensitive to every field.
    auto minimal = FieldSpec::fromJson(nlohmann::json{{"name", "m"}, {"kind", "vortex"}});
    REQUIRE(minimal.has_value());
    CHECK(minimal->kind == FieldKind::Vortex);
    CHECK(minimal->radius == 10.0f);
    const std::uint64_t h = f.structuralHash();
    FieldSpec m = f;
    m.mix = 0.36f;
    CHECK(m.structuralHash() != h);
    m = f;
    m.children.push_back("c");
    CHECK(m.structuralHash() != h);
    m = f;
    m.falloff.curve.w = 0.0f;
    CHECK(m.structuralHash() != h);
    m = f;
    m.waveShape = WaveShape::Sine;
    CHECK(m.structuralHash() != h);

    // Validation errors.
    CHECK(f.validate().has_value());
    m = f;
    m.name.clear();
    CHECK_FALSE(m.validate().has_value());
    m = f;
    m.scale.y = 0.0f;
    CHECK_FALSE(m.validate().has_value());
    m = f;
    m.falloff.outer = 0.5f; // < inner
    CHECK_FALSE(m.validate().has_value());
    m = f;
    m.falloff.inner = -1.0f;
    CHECK_FALSE(m.validate().has_value());
    m = f;
    m.radius = 0.0f;
    CHECK_FALSE(m.validate().has_value());
    m = f;
    m.wavelength = 0.0f;
    CHECK_FALSE(m.validate().has_value());
    m = f;
    m.children = {"a", "b", "c", "d", "e"};
    CHECK_FALSE(m.validate().has_value());
    m = f;
    m.children = {"gust"};
    CHECK_FALSE(m.validate().has_value());
    m = f;
    m.kind = FieldKind::SdfDistance;
    m.reference.clear();
    CHECK_FALSE(m.validate().has_value());
    CHECK_FALSE(FieldSpec::fromJson(nlohmann::json{{"name", "x"}, {"kind", "nope"}}).has_value());
    CHECK_FALSE(FieldSpec::fromJson(nlohmann::json{{"name", "x"}, {"falloff", 3}}).has_value());
    CHECK_FALSE(FieldSpec::fromJson(nlohmann::json{{"name", "x"}, {"children", "a"}}).has_value());
    CHECK_FALSE(FieldSpec::fromJson(nlohmann::json{{"name", "x"}, {"radius", -1.0f}}).has_value());
}
