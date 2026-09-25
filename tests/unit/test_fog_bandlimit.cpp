// The fog turbulence's band-limit (ADR-718): the flow keeps the octaves the march's step can
// carry and drops the ones it cannot.
//
// **What each case is evidence for, and how it fails.**
//   1. The octave weights are 1 up to half a cycle per step and 0 from one cycle per step, measured
//      ALONG the step. The same step length straight down through a flat bank crosses more of the
//      flow than it does along the bank, so it keeps less. Measure the cycles against the smallest
//      semi-axis instead of along the step, and the anisotropy half fails.
//   2. A step that resolves both octaves leaves the field EXACTLY as the point sample: the moderate
//      settings are not touched where the march can carry them. Its control: a step past the band
//      changes the field.
//   3. A step that can carry neither octave leaves NO flow: the field is the calm primitive's, bit
//      for bit. Drop the band from `fogTurbulence` and this fails on every displaced sample.
//   4. Turbulence 0 ignores the step entirely, and the band-limited flow is still divergence-free,
//      so it still moves density rather than making it (ADR-713's §17 argument).

#include "core/noise.hpp"
#include "world/atmospherics.hpp"
#include "world/effects/effect_registry.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

using Catch::Approx;
using namespace avgen;

namespace {

const world::EffectSchema& fogSchema() {
    const world::EffectSchema* s = world::effectSchema(world::EffectKind::VolumetricFog);
    REQUIRE(s != nullptr);
    return *s;
}

void setRow(world::EffectInstance& e, std::string_view leaf, float v) {
    for (const world::EffectField& f : fogSchema().fields) {
        if (std::string_view(f.leaf) == leaf) {
            world::setFieldFloat(f, fogSchema(), e, v);
            return;
        }
    }
    FAIL("no such row on the fog schema: " << leaf);
}

// An ellipsoid shaped like ADR-713's review volume: long, flat and yawed.
world::EffectInstance volume(float turbulence, float scale) {
    world::EffectInstance e = fogSchema().factory("band");
    e.vortex.field.center = glm::vec3(-30.9f, -30.0f, -141.1f);
    e.vortex.field.radius = 52.0f;
    e.vortex.field.thickness = 28.0f;
    e.vortex.field.cloudNoise = 0.45f;
    setRow(e, "shape", 2.0f); // ellipsoid
    setRow(e, "bankLength", 2.0f);
    setRow(e, "bankRotation", 62.0f);
    setRow(e, "edgeSoftness", 0.18f);
    setRow(e, "turbulence", turbulence);
    setRow(e, "turbulenceScale", scale);
    setRow(e, "turbulenceSpeed", 0.4f);
    return e;
}

world::MediumSlot pack(const world::EffectInstance& e) {
    world::MediumSlot slot{};
    world::packMediumSlot(e, 1.0f, slot);
    return slot;
}

std::vector<glm::vec3> gridAround(const world::MediumSlot& m, int n) {
    std::vector<glm::vec3> pts;
    const glm::vec3 c(m.lane[0]);
    const glm::vec3 span(150.0f, 45.0f, 150.0f);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            for (int k = 0; k < n; ++k) {
                const auto u = [&](int a) { return -1.0f + 2.0f * (float(a) + 0.37f) / float(n); };
                pts.push_back(c + span * glm::vec3(u(i), u(j), u(k)));
            }
        }
    }
    return pts;
}

// The unit step along the primitive's long axis, which `fogSemiAxes().x` measures.
glm::vec3 longAxis(const world::MediumSlot& m) { return glm::vec3(m.lane[13].z, 0.0f, m.lane[13].w); }

} // namespace

TEST_CASE("the band keeps what the step can carry and drops what it cannot", "[fog][flow][bandlimit]") {
    const world::MediumSlot m = pack(volume(0.7f, 4.0f));
    const glm::vec3 semi = world::fogSemiAxes(m);
    const float scale = m.lane[7].w;
    REQUIRE(scale == Approx(4.0f));
    REQUIRE(semi.y < semi.x);

    // A zero step is a point sample.
    CHECK(world::fogTurbulenceBand(m, 3.0f, glm::vec3(0.0f)) == glm::vec2(1.0f));

    // Along the long axis a step of L crosses `scale * L / semi.x` cycles of the first octave.
    const auto along = [&](float cycles) { return longAxis(m) * (cycles * semi.x / scale); };
    // Up to Nyquist the first octave is whole; the second, 2.03 times finer, is past one cycle.
    const glm::vec2 nyquist = world::fogTurbulenceBand(m, 3.0f, along(0.49f));
    CHECK(nyquist.x == 1.0f);
    CHECK(nyquist.y == Approx(0.0f).margin(1e-3));
    // Past one cycle a step, both are gone.
    CHECK(world::fogTurbulenceBand(m, 3.0f, along(1.01f)) == glm::vec2(0.0f));
    // Both resolved: under half a cycle for the finer octave.
    CHECK(world::fogTurbulenceBand(m, 3.0f, along(0.24f)) == glm::vec2(1.0f));

    // Monotone in the step, and the finer octave never keeps more than the coarser one.
    glm::vec2 last(1.0f);
    int between = 0;
    for (int i = 1; i <= 200; ++i) {
        const glm::vec2 w = world::fogTurbulenceBand(m, 3.0f, along(0.01f * float(i)));
        CHECK(w.x <= last.x);
        CHECK(w.y <= last.y);
        CHECK(w.y <= w.x);
        CHECK(w.x >= 0.0f);
        CHECK(w.x <= 1.0f);
        between += (w.x > 0.0f && w.x < 1.0f) ? 1 : 0;
        last = w;
    }
    CHECK(between > 40); // the fade is a ramp, not a switch

    // ALONG the step. The same length straight down crosses semi.x / semi.y times as many cycles
    // (the flat axis), so it keeps less; the shorter axis is not a floor applied to every ray.
    const float len = 0.3f * semi.x / scale;
    const glm::vec2 flat = world::fogTurbulenceBand(m, 3.0f, longAxis(m) * len);
    const glm::vec2 down = world::fogTurbulenceBand(m, 3.0f, glm::vec3(0.0f, -len, 0.0f));
    INFO("along the bank " << flat.x << ", " << flat.y << "; down through it " << down.x << ", " << down.y);
    CHECK(flat.x == 1.0f);
    CHECK(down.x < flat.x);
}

TEST_CASE("a step that resolves the flow leaves the field exactly as the point sample",
          "[fog][flow][bandlimit]") {
    // ADR-718's promise to the moderate settings: where the march can carry the flow, nothing moved.
    for (const float turbulence : {0.35f, 0.7f}) {
        INFO("turbulence " << turbulence << " at scale 1.5");
        const world::MediumSlot m = pack(volume(turbulence, 1.5f));
        const glm::vec3 semi = world::fogSemiAxes(m);
        // A quarter cycle of the FINER octave per step, along the thinnest axis: resolved everywhere.
        const glm::vec3 fine(0.0f, 0.24f * semi.y / (1.5f * 2.03f), 0.0f);
        REQUIRE(world::fogTurbulenceBand(m, 5.0f, fine) == glm::vec2(1.0f));
        // ...and a step three times the long semi-axis, which can carry neither.
        const glm::vec3 coarse = longAxis(m) * (3.0f * semi.x);
        int inside = 0;
        int changed = 0;
        for (const glm::vec3 p : gridAround(m, 10)) {
            for (const float t : {0.0f, 5.0f}) {
                const float point = world::fogShapeAt(m, p, t);
                CHECK(world::fogShapeAt(m, p, t, fine) == point);
                const glm::vec3 d = world::fogTurbulence(m, p, t, fine);
                CHECK(d == world::fogTurbulence(m, p, t));
                inside += point > 1e-3f ? 1 : 0;
                changed += std::abs(world::fogShapeAt(m, p, t, coarse) - point) > 1e-3f ? 1 : 0;
            }
        }
        INFO("inside " << inside << ", changed by the coarse step " << changed);
        CHECK(inside > 100);
        CHECK(changed > 50); // the control: the step is an input the field reads
    }
}

TEST_CASE("a step that carries neither octave leaves no flow at all", "[fog][flow][bandlimit]") {
    // Turbulence 0.7 at scale 4, against a step of half the long semi-axis: two cycles of the first
    // octave a step. The flow is gone, so the field is the calm primitive's, bit for bit.
    const world::MediumSlot wild = pack(volume(0.7f, 4.0f));
    const world::MediumSlot calm = pack(volume(0.0f, 4.0f));
    const glm::vec3 semi = world::fogSemiAxes(wild);
    const glm::vec3 coarse = longAxis(wild) * (0.5f * semi.x);
    REQUIRE(world::fogTurbulenceBand(wild, 5.0f, coarse) == glm::vec2(0.0f));
    int inside = 0;
    int movedAtPoint = 0;
    for (const glm::vec3 p : gridAround(wild, 10)) {
        for (const float t : {0.0f, 5.0f}) {
            CHECK(world::fogTurbulence(wild, p, t, coarse) == glm::vec3(0.0f));
            const float c = world::fogShapeAt(calm, p, t);
            CHECK(world::fogShapeAt(wild, p, t, coarse) == c);
            inside += c > 1e-3f ? 1 : 0;
            // The control: point-sampled, the same flow moves the field.
            movedAtPoint += std::abs(world::fogShapeAt(wild, p, t) - c) > 1e-2f ? 1 : 0;
        }
    }
    INFO("inside " << inside << ", moved by the point-sampled flow " << movedAtPoint);
    CHECK(inside > 100);
    CHECK(movedAtPoint > 100);
}

TEST_CASE("turbulence 0 ignores the step, and the banded flow is still divergence-free",
          "[fog][flow][bandlimit]") {
    const world::MediumSlot calm = pack(volume(0.0f, 4.0f));
    for (const glm::vec3 p : gridAround(calm, 5)) {
        for (const glm::vec3 step : {glm::vec3(3.0f, -1.0f, 2.0f), glm::vec3(400.0f, 0.0f, 0.0f)}) {
            CHECK(world::fogShapeAt(calm, p, 2.0f, step) == world::fogShapeAt(calm, p, 2.0f));
        }
    }

    // `flowCurlBanded` at (1, 1) IS `flowCurl`; at any other constant weights it is still the cross
    // product of two gradients, so its divergence is zero up to the finite difference's error. That
    // error is not small everywhere: value noise's second derivative jumps at a lattice face, and the
    // divergence is made of second derivatives, so a difference taken across a face reads a few
    // percent. The threshold is therefore set against a CONTROL: the same flow scaled by
    // `1 + 0.5 x`, which has a real divergence, must read far above it.
    const auto worstDivergence = [](const auto& f) {
        double worst = 0.0;
        for (int i = 0; i < 12; ++i) {
            for (int j = 0; j < 12; ++j) {
                for (int k = 0; k < 12; ++k) {
                    const glm::vec3 p(0.173f * i - 1.3f, 0.211f * j + 0.4f, 0.157f * k - 2.1f);
                    const float t = 0.37f * float(i + j);
                    const float h = 1e-3f;
                    const glm::vec3 fx1 = f(p + glm::vec3(h, 0, 0), t);
                    const glm::vec3 fx0 = f(p - glm::vec3(h, 0, 0), t);
                    const glm::vec3 fy1 = f(p + glm::vec3(0, h, 0), t);
                    const glm::vec3 fy0 = f(p - glm::vec3(0, h, 0), t);
                    const glm::vec3 fz1 = f(p + glm::vec3(0, 0, h), t);
                    const glm::vec3 fz0 = f(p - glm::vec3(0, 0, h), t);
                    const double div = (fx1.x - fx0.x + fy1.y - fy0.y + fz1.z - fz0.z) / (2.0 * h);
                    const double partials = (std::abs(fx1.x - fx0.x) + std::abs(fy1.y - fy0.y) +
                                             std::abs(fz1.z - fz0.z)) / (2.0 * h) + 1e-3;
                    worst = std::max(worst, std::abs(div) / partials);
                }
            }
        }
        return worst;
    };
    for (int i = 0; i < 12; ++i) {
        const glm::vec3 p(0.173f * i - 1.3f, 0.3f * i, -0.2f * i);
        CHECK(noise::flowCurlBanded(p, 0.37f * i, 53u, 1.0f, 1.0f) == noise::flowCurl(p, 0.37f * i, 53u));
    }
    const double control = worstDivergence([](const glm::vec3& q, float t) {
        return noise::flowCurlBanded(q, t, 53u, 0.6f, 0.3f) * (1.0f + 0.5f * q.x);
    });
    INFO("the control (a flow with a real divergence) reads " << control);
    CHECK(control > 0.2);
    for (const glm::vec2 w : {glm::vec2(1.0f), glm::vec2(0.6f, 0.3f), glm::vec2(1.0f, 0.0f), glm::vec2(0.2f, 0.0f)}) {
        const double worst = worstDivergence([&](const glm::vec3& q, float t) {
            return noise::flowCurlBanded(q, t, 53u, w.x, w.y);
        });
        INFO("weights " << w.x << ", " << w.y << ": worst |div| / |partials| " << worst);
        CHECK(worst < 0.05);
    }
}
