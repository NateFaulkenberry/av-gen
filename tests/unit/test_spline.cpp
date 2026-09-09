#include "spatial/spline.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

using namespace avgen;
using namespace avgen::spatial;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

double d(float v) {
    return static_cast<double>(v);
}

constexpr float kPi = glm::pi<float>();

void checkVec(const glm::vec3& v, const glm::vec3& expected, double tol = 1e-5) {
    CHECK_THAT(d(v.x), WithinAbs(d(expected.x), tol));
    CHECK_THAT(d(v.y), WithinAbs(d(expected.y), tol));
    CHECK_THAT(d(v.z), WithinAbs(d(expected.z), tol));
}

void checkFrame(const SplineSample& s, double tol = 1e-4) {
    CHECK_THAT(d(glm::length(s.tangent)), WithinAbs(1.0, tol));
    CHECK_THAT(d(glm::length(s.normal)), WithinAbs(1.0, tol));
    CHECK_THAT(d(glm::length(s.binormal)), WithinAbs(1.0, tol));
    CHECK_THAT(d(glm::dot(s.tangent, s.normal)), WithinAbs(0.0, tol));
    CHECK_THAT(d(glm::dot(s.tangent, s.binormal)), WithinAbs(0.0, tol));
    CHECK_THAT(d(glm::dot(s.normal, s.binormal)), WithinAbs(0.0, tol));
    // Right-handed: binormal x normal = tangent.
    checkVec(glm::cross(s.binormal, s.normal), s.tangent, tol);
}

SplinePoint pt(const glm::vec3& p, const glm::vec3& tangent = glm::vec3(0.0f), float roll = 0.0f, float scale = 1.0f) {
    SplinePoint sp;
    sp.position = p;
    sp.tangent = tangent;
    sp.roll = roll;
    sp.scale = scale;
    return sp;
}

Spline zigzag(SplineKind kind, bool closed = false) {
    Spline s;
    s.name = "zigzag";
    s.kind = kind;
    s.closed = closed;
    s.points = {pt({0.0f, 0.0f, 0.0f}), pt({2.0f, 1.0f, 0.0f}), pt({4.0f, -1.0f, 1.0f}), pt({6.0f, 0.5f, 0.0f}),
                pt({8.0f, 0.0f, -1.0f})};
    return s;
}

Spline helix(float radius = 3.0f, float height = 12.0f, float turns = 2.5f, int count = 48) {
    Spline s;
    s.name = "helix";
    s.generator = SplineGenerator::Helix;
    s.kind = SplineKind::CatmullRom;
    s.radius = radius;
    s.height = height;
    s.turns = turns;
    s.count = count;
    s.samplesPerSegment = 16;
    return s;
}

bool sameBytes(const std::vector<SplineSampleGpu>& a, const std::vector<SplineSampleGpu>& b) {
    return a.size() == b.size() &&
           (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(SplineSampleGpu)) == 0);
}

} // namespace

// ---- names --------------------------------------------------------------------------------------

TEST_CASE("Spline enum names round trip", "[spatial][spline]") {
    for (const SplineKind k : {SplineKind::Polyline, SplineKind::CatmullRom, SplineKind::Bezier, SplineKind::Hermite}) {
        REQUIRE(splineKindFromName(splineKindName(k)).has_value());
        CHECK(*splineKindFromName(splineKindName(k)) == k);
    }
    CHECK(std::string(splineKindName(SplineKind::CatmullRom)) == "catmullRom");
    CHECK(std::string(splineKindName(SplineKind::Polyline)) == "polyline");
    CHECK(!splineKindFromName("cubic").has_value());
    for (const SplineGenerator g : {SplineGenerator::Points, SplineGenerator::Line, SplineGenerator::Circle,
                                    SplineGenerator::Spiral, SplineGenerator::Helix, SplineGenerator::Bezier,
                                    SplineGenerator::Noise}) {
        REQUIRE(splineGeneratorFromName(splineGeneratorName(g)).has_value());
        CHECK(*splineGeneratorFromName(splineGeneratorName(g)) == g);
    }
    CHECK(std::string(splineGeneratorName(SplineGenerator::Noise)) == "noise");
    CHECK(!splineGeneratorFromName("square").has_value());
}

// ---- generators ---------------------------------------------------------------------------------

TEST_CASE("Line generator: endpoints, count, length", "[spatial][spline]") {
    Spline s;
    s.generator = SplineGenerator::Line;
    s.kind = SplineKind::Polyline;
    s.count = 5;
    s.start = {1.0f, 2.0f, 3.0f};
    s.end = {1.0f, 2.0f, 13.0f};
    REQUIRE(s.validate());
    const auto controls = s.controlPoints();
    REQUIRE(controls.size() == 5);
    checkVec(controls.front().position, s.start);
    checkVec(controls.back().position, s.end);
    checkVec(controls[2].position, {1.0f, 2.0f, 8.0f});
    CHECK(s.segmentCount() == 4);
    CHECK_THAT(d(s.length()), WithinAbs(10.0, 1e-4));
    checkVec(s.position(0.0f), s.start);
    checkVec(s.position(1.0f), s.end);
    checkVec(s.position(0.5f), {1.0f, 2.0f, 8.0f}, 1e-4);
    checkVec(s.tangent(0.3f), {0.0f, 0.0f, 1.0f}, 1e-5);
    // The first normal is `up` (perpendicular to the +Z tangent already); binormal = n x t = +X.
    const SplineSample first = s.sample(0.0f);
    checkVec(first.normal, {0.0f, 1.0f, 0.0f}, 1e-5);
    checkVec(first.binormal, {1.0f, 0.0f, 0.0f}, 1e-5);
}

TEST_CASE("Line generator: Catmull-Rom on a straight line stays straight", "[spatial][spline]") {
    Spline s;
    s.generator = SplineGenerator::Line;
    s.kind = SplineKind::CatmullRom;
    s.count = 8;
    s.start = {0.0f, 0.0f, 0.0f};
    s.end = {7.0f, 0.0f, 0.0f};
    CHECK_THAT(d(s.length()), WithinAbs(7.0, 1e-4));
    for (int i = 0; i <= 20; ++i) {
        const glm::vec3 p = s.position(static_cast<float>(i) / 20.0f);
        CHECK_THAT(d(p.y), WithinAbs(0.0, 1e-5));
        CHECK_THAT(d(p.z), WithinAbs(0.0, 1e-5));
    }
}

TEST_CASE("Circle generator: constant radius, closed length ~ 2 pi r", "[spatial][spline]") {
    Spline s;
    s.generator = SplineGenerator::Circle;
    s.kind = SplineKind::CatmullRom;
    s.closed = true;
    s.count = 32;
    s.radius = 4.0f;
    s.center = {1.0f, 2.0f, 3.0f};
    s.axis = {0.0f, 1.0f, 0.0f};
    REQUIRE(s.validate());
    CHECK(s.segmentCount() == 32);
    const auto controls = s.controlPoints();
    REQUIRE(controls.size() == 32);
    for (const auto& c : controls) {
        CHECK_THAT(d(glm::length(c.position - s.center)), WithinAbs(4.0, 1e-4));
        CHECK_THAT(d(c.position.y), WithinAbs(2.0, 1e-5)); // in the plane perpendicular to +Y
    }
    // No duplicate seam point for a closed circle.
    CHECK(glm::length(controls.front().position - controls.back().position) > 0.1f);
    for (int i = 0; i < 200; ++i) {
        const glm::vec3 p = s.position(static_cast<float>(i) / 200.0f);
        CHECK_THAT(d(glm::length(p - s.center)), WithinRel(4.0, 0.01));
    }
    CHECK_THAT(d(s.length()), WithinRel(d(2.0f * kPi * 4.0f), 0.01));

    SECTION("open circle runs from startAngle to startAngle + 2 pi inclusive") {
        s.closed = false;
        s.kind = SplineKind::Polyline;
        s.count = 65;
        const auto open = s.controlPoints();
        REQUIRE(open.size() == 65);
        checkVec(open.front().position, open.back().position, 1e-4);
        CHECK_THAT(d(s.length()), WithinRel(d(2.0f * kPi * 4.0f), 0.01));
    }

    SECTION("startAngle rotates the first point; arbitrary axis stays perpendicular") {
        s.axis = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f));
        s.startAngle = 0.7f;
        const auto rotated = s.controlPoints();
        for (const auto& c : rotated) {
            CHECK_THAT(d(glm::dot(c.position - s.center, s.axis)), WithinAbs(0.0, 1e-4));
            CHECK_THAT(d(glm::length(c.position - s.center)), WithinAbs(4.0, 1e-4));
        }
        s.startAngle = 0.0f;
        const auto base = s.controlPoints();
        const float angle = std::acos(glm::clamp(glm::dot(glm::normalize(rotated[0].position - s.center),
                                                          glm::normalize(base[0].position - s.center)),
                                                 -1.0f, 1.0f));
        CHECK_THAT(d(angle), WithinAbs(0.7, 1e-4));
    }
}

TEST_CASE("Circle basis matches the procedural radial distribution planes", "[spatial][spline]") {
    // procedural.cpp planeFrame: XZ = (a +X, b -Z, n +Y), XY = (+X, +Y, +Z), YZ = (+Y, +Z, +X).
    Spline s;
    s.generator = SplineGenerator::Circle;
    s.kind = SplineKind::Polyline;
    s.count = 5;
    s.radius = 1.0f;
    const auto check = [&](const glm::vec3& axis, const glm::vec3& a, const glm::vec3& b) {
        s.axis = axis;
        const auto c = s.controlPoints();
        REQUIRE(c.size() == 5);
        checkVec(c[0].position, a, 1e-5);         // theta = 0
        checkVec(c[1].position, b, 1e-5);         // theta = pi / 2
        checkVec(c[2].position, -a, 1e-5);        // theta = pi
        checkVec(glm::cross(a, b), axis, 1e-5);   // right-handed about the axis
    };
    check({0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f});
    check({0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    check({1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
}

TEST_CASE("Helix generator: rise along the axis, constant radius", "[spatial][spline]") {
    Spline s = helix(3.0f, 12.0f, 2.5f, 64);
    REQUIRE(s.validate());
    const auto controls = s.controlPoints();
    REQUIRE(controls.size() == 64);
    checkVec(controls.front().position, {3.0f, 0.0f, 0.0f}, 1e-4); // starts on +X for the +Y axis
    CHECK_THAT(d(controls.front().position.y), WithinAbs(0.0, 1e-5));
    CHECK_THAT(d(controls.back().position.y), WithinAbs(12.0, 1e-4));
    for (std::size_t i = 0; i < controls.size(); ++i) {
        const glm::vec3 p = controls[i].position;
        CHECK_THAT(d(std::hypot(p.x, p.z)), WithinAbs(3.0, 1e-4));
        // Monotonic rise.
        if (i > 0) {
            CHECK(p.y > controls[i - 1].position.y);
        }
    }
    // Sampled: the rise is linear in distance for a helix.
    const SplineSample mid = s.sampleByDistance(s.length() * 0.5f);
    CHECK_THAT(d(mid.position.y), WithinAbs(6.0, 0.05));
    // 2.5 turns of radius 3 rising 12: length ~ sqrt((2 pi r turns)^2 + h^2).
    const double expected = std::sqrt(std::pow(2.0 * d(kPi) * 3.0 * 2.5, 2.0) + 144.0);
    CHECK_THAT(d(s.length()), WithinRel(expected, 0.01));
}

TEST_CASE("Spiral generator: radius grows linearly by radiusGrowth", "[spatial][spline]") {
    Spline s;
    s.generator = SplineGenerator::Spiral;
    s.kind = SplineKind::Polyline;
    s.count = 41;
    s.radius = 1.0f;
    s.radiusGrowth = 4.0f;
    s.turns = 2.0f;
    REQUIRE(s.validate());
    const auto controls = s.controlPoints();
    REQUIRE(controls.size() == 41);
    for (std::size_t i = 0; i < controls.size(); ++i) {
        const float f = static_cast<float>(i) / 40.0f;
        const glm::vec3 p = controls[i].position;
        CHECK_THAT(d(std::hypot(p.x, p.z)), WithinAbs(d(1.0f + 4.0f * f), 1e-4));
        CHECK_THAT(d(p.y), WithinAbs(0.0, 1e-5));
    }
    // Two full turns: the last point is on the same ray as the first.
    const glm::vec3 a = glm::normalize(controls.front().position);
    const glm::vec3 b = glm::normalize(controls.back().position);
    CHECK_THAT(d(glm::dot(a, b)), WithinAbs(1.0, 1e-4));
}

TEST_CASE("Bezier generator: samples the cubic p0..p3", "[spatial][spline]") {
    Spline s;
    s.generator = SplineGenerator::Bezier;
    s.kind = SplineKind::Polyline;
    s.count = 3;
    s.p0 = {0.0f, 0.0f, 0.0f};
    s.p1 = {0.0f, 1.0f, 0.0f};
    s.p2 = {3.0f, 1.0f, 0.0f};
    s.p3 = {3.0f, 0.0f, 0.0f};
    const auto controls = s.controlPoints();
    REQUIRE(controls.size() == 3);
    checkVec(controls[0].position, s.p0);
    checkVec(controls[1].position, {1.5f, 0.75f, 0.0f});
    checkVec(controls[2].position, s.p3);
}

TEST_CASE("Noise generator: deterministic jitter of a line, seed-dependent", "[spatial][spline]") {
    Spline line;
    line.generator = SplineGenerator::Line;
    line.kind = SplineKind::Polyline;
    line.count = 12;
    line.start = {0.0f, 0.0f, 0.0f};
    line.end = {0.0f, 0.0f, 20.0f};

    Spline noisy = line;
    noisy.generator = SplineGenerator::Noise;
    noisy.noiseAmount = 0.5f;
    noisy.noiseScale = 0.3f;
    noisy.seed = 7;
    REQUIRE(noisy.validate());

    const auto a = noisy.controlPoints();
    const auto b = noisy.controlPoints();
    REQUIRE(a.size() == 12);
    bool moved = false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].position == b[i].position); // bit-identical
        const glm::vec3 base = line.controlPoints()[i].position;
        CHECK(glm::length(a[i].position - base) <= 0.5f * std::sqrt(3.0f) + 1e-5f);
        moved = moved || glm::length(a[i].position - base) > 1e-4f;
    }
    CHECK(moved);

    Spline other = noisy;
    other.seed = 8;
    const auto c = other.controlPoints();
    bool differs = false;
    for (std::size_t i = 0; i < c.size(); ++i) {
        differs = differs || c[i].position != a[i].position;
    }
    CHECK(differs);

    SECTION("noiseAmount jitters any generator; zero amount leaves it untouched") {
        Spline circle;
        circle.generator = SplineGenerator::Circle;
        circle.count = 16;
        circle.radius = 3.0f;
        const auto clean = circle.controlPoints();
        circle.noiseAmount = 0.2f;
        const auto jittered = circle.controlPoints();
        bool any = false;
        for (std::size_t i = 0; i < clean.size(); ++i) {
            any = any || glm::length(clean[i].position - jittered[i].position) > 1e-4f;
        }
        CHECK(any);
        circle.noiseAmount = 0.0f;
        const auto again = circle.controlPoints();
        for (std::size_t i = 0; i < clean.size(); ++i) {
            CHECK(clean[i].position == again[i].position);
        }
    }
}

TEST_CASE("Generated control points carry finite-difference tangents", "[spatial][spline]") {
    Spline s;
    s.generator = SplineGenerator::Line;
    s.kind = SplineKind::Hermite;
    s.count = 4;
    s.start = {0.0f, 0.0f, 0.0f};
    s.end = {3.0f, 0.0f, 0.0f};
    const auto controls = s.controlPoints();
    REQUIRE(controls.size() == 4);
    checkVec(controls[0].tangent, {1.0f, 0.0f, 0.0f});
    checkVec(controls[1].tangent, {1.0f, 0.0f, 0.0f});
    checkVec(controls[3].tangent, {1.0f, 0.0f, 0.0f});
    // A Hermite line with those tangents is parametrised uniformly (constant speed).
    checkVec(s.position(0.5f), {1.5f, 0.0f, 0.0f}, 1e-4);
    // Bezier handles are a third of the derivative.
    s.kind = SplineKind::Bezier;
    checkVec(s.controlPoints()[1].tangent, {1.0f / 3.0f, 0.0f, 0.0f});
    checkVec(s.position(0.5f), {1.5f, 0.0f, 0.0f}, 1e-4);
}

// ---- interpolation kinds --------------------------------------------------------------------------

TEST_CASE("Polyline: exact linear interpolation between control points", "[spatial][spline]") {
    Spline s = zigzag(SplineKind::Polyline);
    REQUIRE(s.validate());
    CHECK(s.segmentCount() == 4);
    checkVec(s.position(0.0f), {0.0f, 0.0f, 0.0f});
    checkVec(s.position(0.25f), {2.0f, 1.0f, 0.0f}, 1e-5);
    checkVec(s.position(0.125f), {1.0f, 0.5f, 0.0f}, 1e-5);
    checkVec(s.position(1.0f), {8.0f, 0.0f, -1.0f});
    // Tangent inside the first segment.
    checkVec(s.tangent(0.1f), glm::normalize(glm::vec3(2.0f, 1.0f, 0.0f)), 1e-5);
    // Length is the sum of chords.
    double len = 0.0;
    for (std::size_t i = 1; i < s.points.size(); ++i) {
        len += d(glm::length(s.points[i].position - s.points[i - 1].position));
    }
    CHECK_THAT(d(s.length()), WithinAbs(len, 1e-4));
    // Clamping outside [0, 1] for open splines.
    checkVec(s.position(-1.0f), s.points.front().position);
    checkVec(s.position(2.0f), s.points.back().position);
}

TEST_CASE("Catmull-Rom passes through control points and is C1 inside", "[spatial][spline]") {
    Spline s = zigzag(SplineKind::CatmullRom);
    s.samplesPerSegment = 256;
    REQUIRE(s.validate());
    const int segments = s.segmentCount();
    REQUIRE(segments == 4);
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        checkVec(s.position(t), s.points[static_cast<std::size_t>(i)].position, 1e-4);
    }
    // One-sided finite differences on both sides of each interior boundary. `h` is one table
    // cell, so each difference is the chord slope of the adjacent cell; for a C1 curve they differ
    // by about |p''| h (< 0.2 here), while a corner (see the polyline control) jumps by ~ 16.
    const float h = 1.0f / static_cast<float>(segments * 256);
    auto jumpAt = [&](const Spline& spline, int boundary) {
        const float t = static_cast<float>(boundary) / static_cast<float>(segments);
        const glm::vec3 before = (spline.position(t) - spline.position(t - h)) / h;
        const glm::vec3 after = (spline.position(t + h) - spline.position(t)) / h;
        return glm::length(after - before);
    };
    for (int i = 1; i < segments; ++i) {
        CHECK(jumpAt(s, i) < 0.5f);
        // The analytic tangent matches the central difference direction.
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const glm::vec3 central = s.position(t + h) - s.position(t - h);
        checkVec(s.tangent(t), glm::normalize(central), 1e-3);
    }
    Spline corner = zigzag(SplineKind::Polyline);
    corner.samplesPerSegment = 256;
    for (int i = 1; i < segments; ++i) {
        CHECK(jumpAt(corner, i) > 5.0f);
    }
    // Analytic tangent at a boundary: normalize(tension * (p[i+1] - p[i-1])).
    checkVec(s.tangent(0.5f), glm::normalize(s.points[3].position - s.points[1].position), 1e-5);

    // tension 0.5 is the standard uniform Catmull-Rom: midpoint of the segment (1 -> 2) with
    // neighbours 0 and 3: 0.5 (p1 + p2) + 0.125 (m0 - m1), m = 0.5 (p[i+1] - p[i-1]).
    const glm::vec3 p0 = s.points[0].position, p1 = s.points[1].position, p2 = s.points[2].position,
                    p3 = s.points[3].position;
    const glm::vec3 m0 = (p2 - p0) * 0.5f;
    const glm::vec3 m1 = (p3 - p1) * 0.5f;
    const glm::vec3 expected = (p1 + p2) * 0.5f + (m0 - m1) * 0.125f;
    checkVec(s.position(1.5f / 4.0f), expected, 1e-4);

    SECTION("tension 0 degenerates to a smoothstep between neighbours") {
        s.tension = 0.0f;
        checkVec(s.position(1.5f / 4.0f), (p1 + p2) * 0.5f, 1e-4);
    }

    SECTION("closed: neighbours wrap around the seam") {
        s.closed = true;
        REQUIRE(s.segmentCount() == 5);
        // Tangent at the first point uses p[n-1] and p[1].
        const glm::vec3 wrapped = glm::normalize(s.points[1].position - s.points[4].position);
        checkVec(s.tangent(0.0f), wrapped, 1e-5);
    }
}

TEST_CASE("Bezier kind: hand-computed midpoint with handles from tangents", "[spatial][spline]") {
    Spline s;
    s.kind = SplineKind::Bezier;
    s.samplesPerSegment = 16;
    // A = (0,0,0) out-handle (0,1,0); B = (3,0,0) in-handle = B - (0,-1,0) = (3,1,0).
    s.points = {pt({0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}), pt({3.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f})};
    REQUIRE(s.validate());
    checkVec(s.position(0.5f), {1.5f, 0.75f, 0.0f}, 1e-5);
    // u = 0.25: w = 0.75; B = w^3 A + 3 w^2 u C1 + 3 w u^2 C2 + u^3 B
    const float u = 0.25f, w = 0.75f;
    const glm::vec3 c1(0.0f, 1.0f, 0.0f), c2(3.0f, 1.0f, 0.0f), b(3.0f, 0.0f, 0.0f);
    const glm::vec3 expected = c1 * (3.0f * w * w * u) + c2 * (3.0f * w * u * u) + b * (u * u * u);
    checkVec(s.position(0.25f), expected, 1e-5);
    // Start tangent points along the out-handle, end tangent along the in-handle direction.
    checkVec(s.tangent(0.0f), {0.0f, 1.0f, 0.0f}, 1e-5);
    checkVec(s.tangent(1.0f), {0.0f, -1.0f, 0.0f}, 1e-5);
}

TEST_CASE("Hermite kind: hand-computed midpoint from tangents", "[spatial][spline]") {
    Spline s;
    s.kind = SplineKind::Hermite;
    s.samplesPerSegment = 16;
    s.points = {pt({0.0f, 0.0f, 0.0f}, {0.0f, 4.0f, 0.0f}), pt({2.0f, 0.0f, 0.0f}, {0.0f, -4.0f, 0.0f})};
    REQUIRE(s.validate());
    // h00 = h01 = 0.5, h10 = 0.125, h11 = -0.125 at u = 0.5.
    checkVec(s.position(0.5f), {1.0f, 1.0f, 0.0f}, 1e-5);
    checkVec(s.tangent(0.0f), {0.0f, 1.0f, 0.0f}, 1e-5);
    // Derivative at u = 0.5: 1.5 (p1 - p0) - 0.25 (m0 + m1) = (3, 0, 0).
    checkVec(s.tangent(0.5f), {1.0f, 0.0f, 0.0f}, 1e-5);
}

TEST_CASE("Roll and scale interpolate linearly along a segment", "[spatial][spline]") {
    Spline s;
    s.kind = SplineKind::Polyline;
    s.samplesPerSegment = 16;
    s.points = {pt({0.0f, 0.0f, 0.0f}, {}, 0.0f, 1.0f), pt({0.0f, 0.0f, 4.0f}, {}, 0.0f, 3.0f)};
    CHECK_THAT(d(s.sample(0.0f).scale), WithinAbs(1.0, 1e-6));
    CHECK_THAT(d(s.sample(0.5f).scale), WithinAbs(2.0, 1e-5));
    CHECK_THAT(d(s.sample(1.0f).scale), WithinAbs(3.0, 1e-6));
    CHECK_THAT(d(s.sampleByDistance(1.0f).scale), WithinAbs(1.5, 1e-4));
}

// ---- arc length ---------------------------------------------------------------------------------

TEST_CASE("sampleByDistance is monotonic and consistent with length", "[spatial][spline]") {
    Spline s = helix();
    REQUIRE(s.validate());
    const float len = s.length();
    REQUIRE(len > 0.0f);
    const int steps = 2000;
    double travelled = 0.0;
    glm::vec3 prev = s.sampleByDistance(0.0f).position;
    float prevDistance = -1.0f;
    float prevT = -1.0f;
    for (int i = 1; i <= steps; ++i) {
        const float dist = len * static_cast<float>(i) / static_cast<float>(steps);
        const SplineSample smp = s.sampleByDistance(dist);
        CHECK(smp.distance > prevDistance);
        CHECK(smp.t > prevT);
        CHECK_THAT(d(smp.distance), WithinAbs(d(dist), 1e-4));
        travelled += d(glm::length(smp.position - prev));
        prev = smp.position;
        prevDistance = smp.distance;
        prevT = smp.t;
    }
    CHECK_THAT(travelled, WithinRel(d(len), 0.01));
    // Reaching the parameter through distance and back through t agrees.
    const SplineSample byDistance = s.sampleByDistance(len * 0.37f);
    const SplineSample byT = s.sample(byDistance.t);
    checkVec(byDistance.position, byT.position, 1e-3);
    // Clamping for open splines.
    checkVec(s.sampleByDistance(-5.0f).position, s.sample(0.0f).position, 1e-5);
    checkVec(s.sampleByDistance(len + 5.0f).position, s.sample(1.0f).position, 1e-5);
    CHECK_THAT(d(s.sampleByDistance(len).distance), WithinAbs(d(len), 1e-5));
}

TEST_CASE("sample(t): distance grows with t and the table interpolates smoothly", "[spatial][spline]") {
    Spline s = zigzag(SplineKind::CatmullRom);
    float prevDistance = -1.0f;
    for (int i = 0; i <= 100; ++i) {
        const float t = static_cast<float>(i) / 100.0f;
        const SplineSample smp = s.sample(t);
        CHECK(smp.distance > prevDistance);
        CHECK_THAT(d(smp.t), WithinAbs(d(t), 1e-6));
        checkFrame(smp);
        prevDistance = smp.distance;
    }
    CHECK_THAT(d(s.sample(1.0f).distance), WithinAbs(d(s.length()), 1e-5));
}

TEST_CASE("samples(n): spacing by distance for open and closed splines", "[spatial][spline]") {
    Spline open = helix();
    const float len = open.length();
    const auto o = open.samples(11);
    REQUIRE(o.size() == 11);
    for (std::size_t i = 0; i < o.size(); ++i) {
        CHECK_THAT(d(o[i].distance), WithinAbs(d(len * static_cast<float>(i) / 10.0f), 1e-4));
    }
    CHECK_THAT(d(o.back().distance), WithinAbs(d(len), 1e-5));

    Spline closed;
    closed.generator = SplineGenerator::Circle;
    closed.closed = true;
    closed.count = 24;
    closed.radius = 2.0f;
    const float clen = closed.length();
    const auto c = closed.samples(8);
    REQUIRE(c.size() == 8);
    for (std::size_t i = 0; i < c.size(); ++i) {
        CHECK_THAT(d(c[i].distance), WithinAbs(d(clen * static_cast<float>(i) / 8.0f), 1e-4));
    }
    // The duplicate end is omitted: the last sample is a step short of the start.
    CHECK(glm::length(c.back().position - c.front().position) > 0.5f);

    const auto one = closed.samples(1);
    REQUIRE(one.size() == 1);
    CHECK_THAT(d(one[0].distance), WithinAbs(0.0, 1e-6));
    CHECK(closed.samples(0).empty());
}

// ---- frames -------------------------------------------------------------------------------------

TEST_CASE("Frames are orthonormal everywhere and normals do not flip on a helix", "[spatial][spline]") {
    Spline s = helix();
    const auto smp = s.samples(256);
    REQUIRE(smp.size() == 256);
    for (const auto& x : smp) {
        checkFrame(x);
    }
    for (std::size_t i = 1; i < smp.size(); ++i) {
        CHECK(glm::dot(smp[i - 1].normal, smp[i].normal) > 0.9f);
        CHECK(glm::dot(smp[i - 1].tangent, smp[i].tangent) > 0.9f);
    }
    // Rotation-minimising: the frame twist about the tangent per step is tiny, so the normal
    // of the transported frame never depends on the world up in the interior.
    // Also check sample(t) frames.
    for (int i = 0; i <= 50; ++i) {
        checkFrame(s.sample(static_cast<float>(i) / 50.0f));
    }
}

TEST_CASE("Frames: first normal is `up` projected perpendicular to the first tangent", "[spatial][spline]") {
    Spline s;
    s.kind = SplineKind::Polyline;
    s.points = {pt({0.0f, 0.0f, 0.0f}), pt({1.0f, 1.0f, 0.0f}), pt({2.0f, 2.0f, 0.0f})};
    s.up = {0.0f, 1.0f, 0.0f};
    const SplineSample first = s.sample(0.0f);
    checkVec(first.tangent, glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f)), 1e-5);
    checkVec(first.normal, glm::normalize(glm::vec3(-1.0f, 1.0f, 0.0f)), 1e-5);
    checkFrame(first);

    SECTION("up parallel to the tangent falls back to a stable perpendicular") {
        s.up = {1.0f, 1.0f, 0.0f};
        const SplineSample f = s.sample(0.0f);
        checkFrame(f);
        CHECK_THAT(d(glm::dot(f.normal, f.tangent)), WithinAbs(0.0, 1e-5));
    }

    SECTION("a different up changes the whole frame consistently") {
        s.up = {0.0f, 0.0f, 1.0f};
        const SplineSample f = s.sample(0.5f);
        checkVec(f.normal, {0.0f, 0.0f, 1.0f}, 1e-5);
        checkFrame(f);
    }
}

TEST_CASE("Roll rotates the normal about the tangent (right-handed)", "[spatial][spline]") {
    Spline s;
    s.kind = SplineKind::Polyline;
    s.points = {pt({0.0f, 0.0f, 0.0f}, {}, 0.5f * kPi), pt({0.0f, 0.0f, 10.0f}, {}, 0.5f * kPi)};
    const SplineSample a = s.sample(0.5f);
    checkVec(a.tangent, {0.0f, 0.0f, 1.0f}, 1e-5);
    checkVec(a.normal, {-1.0f, 0.0f, 0.0f}, 1e-5); // +Y rotated +90 degrees about +Z
    checkVec(a.binormal, {0.0f, 1.0f, 0.0f}, 1e-5);
    checkFrame(a);

    SECTION("roll interpolates linearly along the segment") {
        s.points[0].roll = 0.0f;
        s.points[1].roll = kPi;
        const SplineSample mid = s.sample(0.5f);
        checkVec(mid.normal, {-1.0f, 0.0f, 0.0f}, 1e-4);
        checkVec(s.sample(0.0f).normal, {0.0f, 1.0f, 0.0f}, 1e-5);
        checkVec(s.sample(1.0f).normal, {0.0f, -1.0f, 0.0f}, 1e-4);
    }
}

TEST_CASE("SplineSample::rotation maps +X/+Y/+Z onto binormal/normal/tangent", "[spatial][spline]") {
    Spline s = helix();
    for (const auto& smp : s.samples(16)) {
        const glm::quat q = smp.rotation();
        CHECK_THAT(d(glm::length(q)), WithinAbs(1.0, 1e-5));
        checkVec(q * glm::vec3(1.0f, 0.0f, 0.0f), smp.binormal, 1e-4);
        checkVec(q * glm::vec3(0.0f, 1.0f, 0.0f), smp.normal, 1e-4);
        checkVec(q * glm::vec3(0.0f, 0.0f, 1.0f), smp.tangent, 1e-4);
    }
}

TEST_CASE("Closed spline: seam continuity of position and frame, t and distance wrap", "[spatial][spline]") {
    Spline s;
    s.generator = SplineGenerator::Circle;
    s.kind = SplineKind::CatmullRom;
    s.closed = true;
    s.count = 32;
    s.radius = 3.0f;
    s.axis = glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f));
    s.samplesPerSegment = 32;
    REQUIRE(s.validate());
    const float len = s.length();
    const SplineSample start = s.sample(0.0f);
    const SplineSample end = s.sample(1.0f - 1e-4f);
    checkVec(end.position, start.position, 5e-3);
    CHECK(glm::dot(end.tangent, start.tangent) > 0.999f);
    CHECK(glm::dot(end.normal, start.normal) > 0.999f);
    CHECK(glm::dot(end.binormal, start.binormal) > 0.999f);
    // Wrapping.
    checkVec(s.sample(1.0f).position, start.position, 1e-5);
    checkVec(s.sample(1.25f).position, s.sample(0.25f).position, 1e-5);
    checkVec(s.sample(-0.25f).position, s.sample(0.75f).position, 1e-5);
    checkVec(s.sampleByDistance(len).position, start.position, 1e-5);
    checkVec(s.sampleByDistance(len + 1.0f).position, s.sampleByDistance(1.0f).position, 1e-4);
    checkVec(s.sampleByDistance(-1.0f).position, s.sampleByDistance(len - 1.0f).position, 1e-4);
    // No flip anywhere around the loop.
    const auto ring = s.samples(128);
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const auto& a = ring[i];
        const auto& b = ring[(i + 1) % ring.size()];
        CHECK(glm::dot(a.normal, b.normal) > 0.99f);
        checkFrame(a);
    }
}

// ---- cache, hashing, determinism ----------------------------------------------------------------

TEST_CASE("structuralHash is sensitive to every member and stable", "[spatial][spline]") {
    const Spline base = zigzag(SplineKind::CatmullRom);
    const std::uint64_t h = base.structuralHash();
    CHECK(h == zigzag(SplineKind::CatmullRom).structuralHash());

    auto changed = [&](auto mutate) {
        Spline s = base;
        mutate(s);
        return s.structuralHash() != h;
    };
    CHECK(changed([](Spline& s) { s.kind = SplineKind::Bezier; }));
    CHECK(changed([](Spline& s) { s.closed = true; }));
    CHECK(changed([](Spline& s) { s.tension = 0.3f; }));
    CHECK(changed([](Spline& s) { s.points[2].position.x += 0.001f; }));
    CHECK(changed([](Spline& s) { s.points[2].tangent.y = 1.0f; }));
    CHECK(changed([](Spline& s) { s.points[1].roll = 0.1f; }));
    CHECK(changed([](Spline& s) { s.points[1].scale = 2.0f; }));
    CHECK(changed([](Spline& s) { s.points.pop_back(); }));
    CHECK(changed([](Spline& s) { s.generator = SplineGenerator::Line; }));
    CHECK(changed([](Spline& s) { s.count = 17; }));
    CHECK(changed([](Spline& s) { s.start.z = 1.0f; }));
    CHECK(changed([](Spline& s) { s.end.z = 1.0f; }));
    CHECK(changed([](Spline& s) { s.radius = 1.0f; }));
    CHECK(changed([](Spline& s) { s.radiusGrowth = 1.0f; }));
    CHECK(changed([](Spline& s) { s.turns = 2.0f; }));
    CHECK(changed([](Spline& s) { s.height = 2.0f; }));
    CHECK(changed([](Spline& s) { s.axis = {1.0f, 0.0f, 0.0f}; }));
    CHECK(changed([](Spline& s) { s.center.x = 1.0f; }));
    CHECK(changed([](Spline& s) { s.startAngle = 1.0f; }));
    CHECK(changed([](Spline& s) { s.p0.x = 1.0f; }));
    CHECK(changed([](Spline& s) { s.p1.x = 1.0f; }));
    CHECK(changed([](Spline& s) { s.p2.x = 1.0f; }));
    CHECK(changed([](Spline& s) { s.p3.x = 1.0f; }));
    CHECK(changed([](Spline& s) { s.noiseAmount = 0.1f; }));
    CHECK(changed([](Spline& s) { s.noiseScale = 0.1f; }));
    CHECK(changed([](Spline& s) { s.seed = 2; }));
    CHECK(changed([](Spline& s) { s.samplesPerSegment = 8; }));
    CHECK(changed([](Spline& s) { s.up = {0.0f, 0.0f, 1.0f}; }));
    CHECK(changed([](Spline& s) { s.name = "other"; }));
    // -0 and +0 hash the same.
    Spline z = base;
    z.tension = -0.0f;
    Spline z2 = base;
    z2.tension = 0.0f;
    CHECK(z.structuralHash() == z2.structuralHash());
}

TEST_CASE("Sampling is deterministic and the cache follows the hash", "[spatial][spline]") {
    Spline a = helix();
    a.noiseAmount = 0.2f;
    a.seed = 99;
    Spline b = a;
    a.prepare();
    // `b` is sampled on the fly (no prepare) and must yield the same bytes.
    CHECK(sameBytes(packSplineTable(a, 128), packSplineTable(b, 128)));
    const SplineSample sa = a.sample(0.371f);
    const SplineSample sb = b.sample(0.371f);
    CHECK(sa.position == sb.position);
    CHECK(sa.normal == sb.normal);

    // Editing invalidates the cache: the sampled result follows the new geometry.
    const float lenBefore = a.length();
    a.radius *= 2.0f;
    CHECK(a.length() > lenBefore * 1.5f);
    a.prepare();
    a.prepare(); // idempotent
    CHECK(a.length() > lenBefore * 1.5f);
    // A copy carries the cache but its hash still matches, so results are identical.
    const Spline copy = a;
    CHECK(sameBytes(packSplineTable(copy, 64), packSplineTable(a, 64)));
}

TEST_CASE("samplesPerSegment controls the table resolution (clamped 4..256)", "[spatial][spline]") {
    Spline coarse = zigzag(SplineKind::CatmullRom);
    coarse.samplesPerSegment = 1; // clamped up to 4
    Spline fine = zigzag(SplineKind::CatmullRom);
    fine.samplesPerSegment = 256;
    REQUIRE(coarse.validate());
    REQUIRE(fine.validate());
    // The coarse table underestimates the length of a curved spline but only slightly.
    CHECK(coarse.length() <= fine.length() + 1e-4f);
    CHECK_THAT(d(coarse.length()), WithinRel(d(fine.length()), 0.02));
}

// ---- GPU table ----------------------------------------------------------------------------------

TEST_CASE("packSplineTable layout: w components carry distance, scale, t and the roll flag", "[spatial][spline]") {
    Spline s = helix();
    s.points.clear();
    const auto table = packSplineTable(s, 64);
    REQUIRE(table.size() == 64);
    const auto smp = s.samples(64);
    const float len = s.length();
    for (std::size_t i = 0; i < table.size(); ++i) {
        const SplineSampleGpu& g = table[i];
        CHECK(glm::vec3(g.position) == smp[i].position);
        CHECK(g.position.w == smp[i].distance);
        CHECK(glm::vec3(g.tangent) == smp[i].tangent);
        CHECK(g.tangent.w == smp[i].scale);
        CHECK(glm::vec3(g.normal) == smp[i].normal);
        CHECK(g.normal.w == smp[i].t);
        CHECK(glm::vec3(g.binormal) == smp[i].binormal);
        CHECK(g.binormal.w == 1.0f);
    }
    CHECK(table.front().position.w == 0.0f);
    CHECK_THAT(d(table.back().position.w), WithinAbs(d(len), 1e-5));
    CHECK_THAT(d(table.back().normal.w), WithinAbs(1.0, 1e-6));
    CHECK(packSplineTable(s).size() == static_cast<std::size_t>(kSplineGpuSamples));
    static_assert(sizeof(SplineSampleGpu) == 64);
    static_assert(alignof(SplineSampleGpu) == 16);
}

// ---- JSON ---------------------------------------------------------------------------------------

TEST_CASE("Spline JSON round trip preserves every member", "[spatial][spline]") {
    Spline s = zigzag(SplineKind::Hermite, true);
    s.name = "rail";
    s.tension = 0.35f;
    s.points[1].tangent = {0.1f, 0.2f, 0.3f};
    s.points[1].roll = 0.4f;
    s.points[2].scale = 1.7f;
    s.generator = SplineGenerator::Helix;
    s.count = 40;
    s.start = {1.0f, 2.0f, 3.0f};
    s.end = {4.0f, 5.0f, 6.0f};
    s.radius = 2.5f;
    s.radiusGrowth = 1.25f;
    s.turns = 3.5f;
    s.height = 7.0f;
    s.axis = {0.0f, 0.0f, 1.0f};
    s.center = {-1.0f, 0.5f, 0.25f};
    s.startAngle = 0.125f;
    s.p0 = {1.0f, 1.0f, 1.0f};
    s.p1 = {2.0f, 2.0f, 2.0f};
    s.p2 = {3.0f, 3.0f, 3.0f};
    s.p3 = {4.0f, 4.0f, 4.0f};
    s.noiseAmount = 0.3f;
    s.noiseScale = 0.7f;
    s.seed = 12345u;
    s.samplesPerSegment = 24;
    s.up = {0.0f, 0.0f, 1.0f};

    const nlohmann::json j = s.toJson();
    CHECK(j.at("kind") == "hermite");
    CHECK(j.at("generator") == "helix");
    CHECK(j.at("points").is_array());
    CHECK(j.at("points").size() == 5);
    CHECK(j.at("points").at(1).at("roll").get<float>() == 0.4f);
    CHECK(j.at("axis") == nlohmann::json::array({0.0f, 0.0f, 1.0f}));

    const auto parsed = Spline::fromJson(j);
    REQUIRE(parsed);
    CHECK(parsed->name == "rail");
    CHECK(parsed->kind == SplineKind::Hermite);
    CHECK(parsed->closed);
    CHECK(parsed->tension == s.tension);
    REQUIRE(parsed->points.size() == s.points.size());
    for (std::size_t i = 0; i < s.points.size(); ++i) {
        CHECK(parsed->points[i].position == s.points[i].position);
        CHECK(parsed->points[i].tangent == s.points[i].tangent);
        CHECK(parsed->points[i].roll == s.points[i].roll);
        CHECK(parsed->points[i].scale == s.points[i].scale);
    }
    CHECK(parsed->generator == SplineGenerator::Helix);
    CHECK(parsed->count == 40);
    CHECK(parsed->start == s.start);
    CHECK(parsed->end == s.end);
    CHECK(parsed->radius == s.radius);
    CHECK(parsed->radiusGrowth == s.radiusGrowth);
    CHECK(parsed->turns == s.turns);
    CHECK(parsed->height == s.height);
    CHECK(parsed->axis == s.axis);
    CHECK(parsed->center == s.center);
    CHECK(parsed->startAngle == s.startAngle);
    CHECK(parsed->p0 == s.p0);
    CHECK(parsed->p1 == s.p1);
    CHECK(parsed->p2 == s.p2);
    CHECK(parsed->p3 == s.p3);
    CHECK(parsed->noiseAmount == s.noiseAmount);
    CHECK(parsed->noiseScale == s.noiseScale);
    CHECK(parsed->seed == s.seed);
    CHECK(parsed->samplesPerSegment == 24);
    CHECK(parsed->up == s.up);
    CHECK(parsed->structuralHash() == s.structuralHash());
    CHECK(parsed->toJson() == j);
}

TEST_CASE("Spline JSON: missing members default, bad values error", "[spatial][spline]") {
    const auto empty = Spline::fromJson(nlohmann::json::object());
    REQUIRE(empty);
    const Spline def;
    CHECK(empty->name == def.name);
    CHECK(empty->kind == def.kind);
    CHECK(empty->generator == def.generator);
    CHECK(empty->count == def.count);
    CHECK(empty->samplesPerSegment == def.samplesPerSegment);
    CHECK(empty->structuralHash() == def.structuralHash());

    const auto partial = Spline::fromJson(nlohmann::json::parse(
        R"({"kind":"polyline","points":[{"position":[0,0,0]},{"position":[1,0,0],"roll":0.5}]})"));
    REQUIRE(partial);
    CHECK(partial->kind == SplineKind::Polyline);
    REQUIRE(partial->points.size() == 2);
    CHECK(partial->points[0].tangent == glm::vec3(0.0f));
    CHECK(partial->points[0].scale == 1.0f);
    CHECK(partial->points[1].roll == 0.5f);

    CHECK(!Spline::fromJson(nlohmann::json::parse(R"({"kind":"nurbs"})")));
    CHECK(!Spline::fromJson(nlohmann::json::parse(R"({"generator":"square"})")));
    CHECK(!Spline::fromJson(nlohmann::json::parse(R"({"radius":"big"})")));
    CHECK(!Spline::fromJson(nlohmann::json::parse(R"({"axis":[1,0]})")));
    CHECK(!Spline::fromJson(nlohmann::json::parse(R"({"points":{}})")));
    CHECK(!Spline::fromJson(nlohmann::json::parse(R"({"points":[{"position":[0,0]}]})")));
    CHECK(!Spline::fromJson(nlohmann::json::parse(R"({"seed":-1})")));
    CHECK(!Spline::fromJson(nlohmann::json::parse(R"({"closed":"yes"})")));
    CHECK(!Spline::fromJson(nlohmann::json::array()));
}

// ---- validation ---------------------------------------------------------------------------------

TEST_CASE("Spline validate: control point count, generator count, ranges, finiteness", "[spatial][spline]") {
    Spline s;
    CHECK(!s.validate()); // no points
    s.points = {pt({0.0f, 0.0f, 0.0f})};
    CHECK(!s.validate()); // one point
    s.points.push_back(pt({1.0f, 0.0f, 0.0f}));
    CHECK(s.validate());

    Spline g;
    g.generator = SplineGenerator::Circle;
    g.count = 1;
    CHECK(!g.validate());
    g.count = 2;
    CHECK(g.validate());
    g.radius = -1.0f;
    CHECK(!g.validate());
    g.radius = 0.0f;
    CHECK(g.validate());

    g.samplesPerSegment = 0;
    CHECK(!g.validate());
    g.samplesPerSegment = 257;
    CHECK(!g.validate());
    g.samplesPerSegment = 1;
    CHECK(g.validate());
    g.samplesPerSegment = 256;
    CHECK(g.validate());

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    Spline bad = g;
    bad.tension = nan;
    CHECK(!bad.validate());
    bad = g;
    bad.center.y = inf;
    CHECK(!bad.validate());
    bad = g;
    bad.up.x = nan;
    CHECK(!bad.validate());
    bad = s;
    bad.points[1].tangent.z = nan;
    CHECK(!bad.validate());
    bad = s;
    bad.points[0].roll = inf;
    CHECK(!bad.validate());
    // Error messages name the spline.
    s.name = "rail";
    s.points.clear();
    const auto r = s.validate();
    REQUIRE(!r);
    CHECK(r.error().message.find("rail") != std::string::npos);
}

TEST_CASE("Degenerate splines sample without crashing", "[spatial][spline]") {
    Spline s;
    s.points = {pt({1.0f, 2.0f, 3.0f})};
    CHECK(s.segmentCount() == 0);
    CHECK(s.length() == 0.0f);
    checkVec(s.position(0.5f), {1.0f, 2.0f, 3.0f});
    checkFrame(s.sample(0.3f));
    CHECK(s.samples(3).size() == 3);
    CHECK(packSplineTable(s, 4).size() == 4);

    // Coincident points: zero length, still a valid frame.
    s.points = {pt({1.0f, 2.0f, 3.0f}), pt({1.0f, 2.0f, 3.0f})};
    CHECK(s.length() == 0.0f);
    checkFrame(s.sampleByDistance(0.5f));
    checkVec(s.position(1.0f), {1.0f, 2.0f, 3.0f});
}

// ---- sets ---------------------------------------------------------------------------------------

TEST_CASE("SplineSet find and indexOf", "[spatial][spline]") {
    SplineSet set;
    Spline a;
    a.name = "rail";
    Spline b;
    b.name = "camera";
    set.splines = {a, b};
    CHECK(set.indexOf("rail") == 0);
    CHECK(set.indexOf("camera") == 1);
    CHECK(set.indexOf("missing") == -1);
    REQUIRE(set.find("camera") != nullptr);
    CHECK(set.find("camera")->name == "camera");
    CHECK(set.find("missing") == nullptr);
    CHECK(SplineSet{}.find("rail") == nullptr);
}
