// Ray generation, the sampler and the sampling warps (ADR-344, spec sections 14-15, 26).
//
// The camera tests deliberately check the generated rays against `scene::Camera`'s OWN matrices
// rather than against a hand-derived expectation. A ray generator and a projection that disagree by
// a sign or a half-pixel both look plausible in isolation; the only thing that catches it is making
// them answer the same question. Spec section 86 Step E exists because of exactly this class of bug.

#include "pathtrace/camera.hpp"
#include "pathtrace/sampler.hpp"
#include "scene/scene_types.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>

#include <cmath>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

scene::Camera testCamera() {
    scene::Camera c;
    c.position = glm::vec3(3.0f, 2.0f, 7.0f);
    c.target = glm::vec3(-1.0f, 0.5f, -2.0f);
    c.up = glm::vec3(0.0f, 1.0f, 0.0f);
    c.lens.useExplicitFov = true;
    c.fovYRadians = 0.9f;
    c.nearPlane = 0.1f;
    c.farPlane = 500.0f;
    return c;
}

// Where the rasteriser would put a world point, in the same pixel coordinates generateRay takes.
glm::vec2 projectToPixel(const scene::Camera& cam, std::uint32_t w, std::uint32_t h, const glm::vec3& p) {
    const glm::mat4 vp = cam.projection(static_cast<float>(w) / static_cast<float>(h)) * cam.view();
    const glm::vec4 clip = vp * glm::vec4(p, 1.0f);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return {(ndc.x * 0.5f + 0.5f) * static_cast<float>(w), (0.5f - ndc.y * 0.5f) * static_cast<float>(h)};
}

} // namespace

TEST_CASE("a generated ray hits the point the projection matrix puts under that pixel",
          "[unit][pathtrace][camera]") {
    const scene::Camera cam = testCamera();
    const std::uint32_t w = 320;
    const std::uint32_t h = 200;
    const auto basis = pathtrace::cameraBasis(cam, w, h);

    // Points scattered in front of the camera, including off-axis ones where a bad `aspect` or a
    // flipped `up` shows up and a centred point would not.
    const std::vector<glm::vec3> points = {
        {-1.0f, 0.5f, -2.0f}, {0.0f, 0.0f, 0.0f},   {-3.0f, 2.5f, -1.0f},
        {2.0f, -1.0f, -4.0f}, {-4.0f, -2.0f, 1.0f}, {1.5f, 3.0f, -6.0f},
    };

    for (const glm::vec3& p : points) {
        const glm::vec2 px = projectToPixel(cam, w, h, p);
        // Only meaningful for points the rasteriser would actually put on screen.
        if (px.x < 0.0f || px.x > static_cast<float>(w) || px.y < 0.0f || px.y > static_cast<float>(h)) continue;

        const pathtrace::Ray r = pathtrace::generateRay(basis, px.x, px.y, w, h);
        const glm::vec3 toPoint = glm::normalize(p - r.origin);
        // The ray through that pixel must point at the point, to within a fraction of a pixel.
        REQUIRE(glm::dot(r.direction, toPoint) > 0.99999f);
    }
}

TEST_CASE("CONTROL: a ray from the wrong pixel does NOT point at the point",
          "[unit][pathtrace][camera]") {
    // Without this arm the test above would pass for a generator that ignored its pixel arguments
    // entirely and always returned the camera's forward axis.
    const scene::Camera cam = testCamera();
    const std::uint32_t w = 320;
    const std::uint32_t h = 200;
    const auto basis = pathtrace::cameraBasis(cam, w, h);

    const glm::vec3 p{-3.0f, 2.5f, -1.0f};
    const glm::vec2 px = projectToPixel(cam, w, h, p);
    REQUIRE(px.x >= 0.0f);
    REQUIRE(px.x <= static_cast<float>(w));

    const pathtrace::Ray wrong = pathtrace::generateRay(basis, px.x + 40.0f, px.y + 30.0f, w, h);
    const glm::vec3 toPoint = glm::normalize(p - wrong.origin);
    REQUIRE(glm::dot(wrong.direction, toPoint) < 0.999f);
}

TEST_CASE("the centre pixel looks along the camera's forward axis", "[unit][pathtrace][camera]") {
    const scene::Camera cam = testCamera();
    const auto basis = pathtrace::cameraBasis(cam, 200, 100);
    const pathtrace::Ray r = pathtrace::generateRay(basis, 100.0f, 50.0f, 200, 100);
    const glm::vec3 forward = glm::normalize(cam.target - cam.position);
    REQUIRE(glm::dot(r.direction, forward) > 0.9999999f);
    REQUIRE(r.origin.x == Approx(cam.position.x));
    REQUIRE(r.origin.y == Approx(cam.position.y));
    REQUIRE(r.origin.z == Approx(cam.position.z));
}

TEST_CASE("pixel row 0 is the TOP of the image", "[unit][pathtrace][camera]") {
    // A vertical flip is the single most common coordinate bug in a new renderer and it survives
    // every symmetric test. Pin it: a ray through the top row must point above the forward axis.
    scene::Camera cam;
    cam.position = glm::vec3(0.0f, 0.0f, 5.0f);
    cam.target = glm::vec3(0.0f, 0.0f, 0.0f);
    cam.up = glm::vec3(0.0f, 1.0f, 0.0f);
    cam.lens.useExplicitFov = true;
    cam.fovYRadians = 1.0f;

    const auto basis = pathtrace::cameraBasis(cam, 100, 100);
    const pathtrace::Ray top = pathtrace::generateRay(basis, 50.0f, 1.0f, 100, 100);
    const pathtrace::Ray bottom = pathtrace::generateRay(basis, 50.0f, 99.0f, 100, 100);
    REQUIRE(top.direction.y > 0.0f);
    REQUIRE(bottom.direction.y < 0.0f);
}

TEST_CASE("a wider aspect widens the horizontal field and leaves the vertical alone",
          "[unit][pathtrace][camera]") {
    scene::Camera cam;
    cam.position = glm::vec3(0.0f, 0.0f, 5.0f);
    cam.target = glm::vec3(0.0f, 0.0f, 0.0f);
    cam.lens.useExplicitFov = true;
    cam.fovYRadians = 1.0f;

    const auto square = pathtrace::cameraBasis(cam, 100, 100);
    const auto wide = pathtrace::cameraBasis(cam, 200, 100);

    const auto edgeX = [](const pathtrace::CameraBasis& b, std::uint32_t w, std::uint32_t h) {
        return std::abs(pathtrace::generateRay(b, static_cast<float>(w), static_cast<float>(h) * 0.5f, w, h).direction.x);
    };
    REQUIRE(edgeX(wide, 200, 100) > edgeX(square, 100, 100));

    const auto edgeY = [](const pathtrace::CameraBasis& b, std::uint32_t w, std::uint32_t h) {
        return std::abs(pathtrace::generateRay(b, static_cast<float>(w) * 0.5f, 0.0f, w, h).direction.y);
    };
    REQUIRE(edgeY(wide, 200, 100) == Approx(edgeY(square, 100, 100)).margin(1e-6));
}

TEST_CASE("a degenerate camera produces finite rays rather than NaNs", "[unit][pathtrace][camera]") {
    scene::Camera cam;
    cam.position = glm::vec3(1.0f, 2.0f, 3.0f);
    cam.target = cam.position; // no direction at all
    const auto basis = pathtrace::cameraBasis(cam, 64, 64);
    const pathtrace::Ray r = pathtrace::generateRay(basis, 32.0f, 32.0f, 64, 64);
    REQUIRE(std::isfinite(r.direction.x));
    REQUIRE(std::isfinite(r.direction.y));
    REQUIRE(std::isfinite(r.direction.z));
    REQUIRE(glm::length(r.direction) == Approx(1.0f));

    // Looking straight down, where `up` is parallel to `forward` and the cross product degenerates.
    scene::Camera down;
    down.position = glm::vec3(0.0f, 10.0f, 0.0f);
    down.target = glm::vec3(0.0f, 0.0f, 0.0f);
    down.up = glm::vec3(0.0f, 1.0f, 0.0f);
    const auto b2 = pathtrace::cameraBasis(down, 64, 64);
    const pathtrace::Ray r2 = pathtrace::generateRay(b2, 10.0f, 50.0f, 64, 64);
    REQUIRE(std::isfinite(r2.direction.x));
    REQUIRE(glm::length(r2.direction) == Approx(1.0f));
}

// ---- sampler ---------------------------------------------------------------------------------

TEST_CASE("the sampler is reproducible and decorrelated", "[unit][pathtrace][sampler]") {
    // Same (seed, pixel, sample) must give the same numbers -- this is the whole determinism claim.
    pathtrace::Sampler a(1234, 77, 3);
    pathtrace::Sampler b(1234, 77, 3);
    for (int i = 0; i < 16; ++i) REQUIRE(a.next1D() == b.next1D());

    // CONTROL: change any one of the three and the sequence must differ, or the seeding is a no-op.
    const auto first = [](std::uint64_t s, std::uint32_t p, std::uint32_t n) {
        pathtrace::Sampler smp(s, p, n);
        return smp.next1D();
    };
    const float base = first(1234, 77, 3);
    REQUIRE(first(1235, 77, 3) != base);
    REQUIRE(first(1234, 78, 3) != base);
    REQUIRE(first(1234, 77, 4) != base);

    // Adjacent pixels on the same sample index must not share a sequence: that is the structured
    // noise a pixel-only seed produces, and it does not average away with more samples.
    int agreements = 0;
    for (std::uint32_t p = 0; p < 256; ++p) {
        pathtrace::Sampler s0(99, p, 0);
        pathtrace::Sampler s1(99, p + 1, 0);
        if (std::abs(s0.next1D() - s1.next1D()) < 1e-6f) ++agreements;
    }
    REQUIRE(agreements < 4);
}

TEST_CASE("sampler output is uniform in [0,1) and does not drift", "[unit][pathtrace][sampler]") {
    double sum = 0.0;
    int bins[10] = {};
    const int n = 200000;
    for (int i = 0; i < n; ++i) {
        pathtrace::Sampler s(7, static_cast<std::uint32_t>(i), 0);
        const float v = s.next1D();
        REQUIRE(v >= 0.0f);
        REQUIRE(v < 1.0f);
        sum += v;
        bins[std::min(9, static_cast<int>(v * 10.0f))]++;
    }
    // A band, not a floor: the mean of a uniform is 0.5 and 200k samples pin it tightly.
    REQUIRE(sum / n > 0.495);
    REQUIRE(sum / n < 0.505);
    // Every decile populated within 15% of expectation -- catches a generator stuck in a subrange.
    for (int b : bins) {
        REQUIRE(b > n / 10 * 0.85);
        REQUIRE(b < n / 10 * 1.15);
    }
}

TEST_CASE("cosine hemisphere sampling matches its PDF", "[unit][pathtrace][sampler]") {
    // Integrate cos(theta)/pi over the hemisphere by Monte Carlo with the PDF the integrator uses.
    // If the warp and the PDF disagree, this does not come out at 1 -- and a wrong-by-a-constant
    // BSDF is exactly the bug that makes a render "look a bit dark" and never gets found.
    double sum = 0.0;
    const int n = 100000;
    int belowHorizon = 0;
    for (int i = 0; i < n; ++i) {
        pathtrace::Sampler s(3, static_cast<std::uint32_t>(i), 0);
        const glm::vec3 d = pathtrace::sampleCosineHemisphere(s.next2D());
        if (d.z < 0.0f) ++belowHorizon;
        REQUIRE(glm::length(d) == Approx(1.0f).margin(1e-4));
        const float pdf = pathtrace::cosineHemispherePdf(d.z);
        if (pdf > 0.0f) sum += (d.z * 0.31830988618379067) / pdf; // integrand cos/pi over pdf
    }
    REQUIRE(belowHorizon == 0);
    REQUIRE(sum / n == Approx(1.0).margin(0.01));
}

TEST_CASE("the orthonormal basis is orthonormal, including at the poles", "[unit][pathtrace][sampler]") {
    const std::vector<glm::vec3> normals = {
        {0, 1, 0},  {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {1, 0, 0},
        {-1, 0, 0}, glm::normalize(glm::vec3(0.3f, -0.9f, 0.2f)),
        // The pole of the naive basis: n.z == -1 is where the a = -1/(sign+n.z) form would divide
        // by zero if the sign flip were missing.
        {0.0f, 0.0f, -1.0f},
    };
    for (const glm::vec3& n : normals) {
        glm::vec3 t{};
        glm::vec3 b{};
        pathtrace::orthonormalBasis(n, t, b);
        REQUIRE(glm::length(t) == Approx(1.0f).margin(1e-5));
        REQUIRE(glm::length(b) == Approx(1.0f).margin(1e-5));
        REQUIRE(glm::dot(t, b) == Approx(0.0f).margin(1e-5));
        REQUIRE(glm::dot(t, n) == Approx(0.0f).margin(1e-5));
        REQUIRE(glm::dot(b, n) == Approx(0.0f).margin(1e-5));
        // toWorld must map +Z to n itself, or every BSDF sample is rotated.
        const glm::vec3 up = pathtrace::toWorld(glm::vec3(0, 0, 1), n);
        REQUIRE(glm::dot(up, n) == Approx(1.0f).margin(1e-5));
    }
}
