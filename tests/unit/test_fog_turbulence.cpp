// The fog's §16 flow controls: swirl, swell and turbulence (ADR-713).
//
// **What each case is evidence for, and how it fails.**
//   1. Every new control is the IDENTITY at its default, EXACTLY -- the promise to every bank
//      already authored. Make `fogStructureFrame` rotate by `-omega * t` without the `omega == 0`
//      branch and case 1 fails on the `p` comparison (`c + (p - c)` is not `p` in float).
//   2. SWIRL is a rigid rotation of the structure: detail(c + R(w dt)(p - c), t + dt) ==
//      detail(p, t). Negate the angle in `fogStructureFrame` and it fails; its control is that the
//      detail at a fixed point genuinely changes with t (a field that ignores the swirl passes the
//      identity at dt = 0 only).
//   3. SWELL scales the primitive's horizontal extent by exactly `1 + amount * sin(rate * t)`:
//      field(c + s * r, t) == field(c + r, t0) where sin(rate * t0) = 0. Divide the vertical too
//      for a Bank and the vertical half fails; drop the division and the horizontal half fails.
//   4. TURBULENCE moves the OUTLINE: samples outside the undisplaced primitive become dense and
//      samples inside it become empty. Its displacement is bounded by `amount` semi-axes (the
//      bound's proof), carried by the drift, and a pure function of t.
//   5. The flow the turbulence uses is DIVERGENCE-FREE (the CPU twin of `flowCurl`), which is what
//      makes it move density rather than create or destroy it -- §17's "flow affects movement, not
//      basic existence".
//   6. Every row reaches its lane and the lane reaches the field (ADR-421): a row that packs a
//      number nothing reads is the defect this ADR found eight of.

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

world::EffectInstance bankEffect(int shape = 0, float detail = 0.0f) {
    world::EffectInstance e = fogSchema().factory("flow");
    e.vortex.field.center = glm::vec3(30.0f, 50.0f, -20.0f);
    e.vortex.field.radius = 200.0f;
    e.vortex.field.thickness = 60.0f;
    e.vortex.field.cloudNoise = detail;
    setRow(e, "shape", static_cast<float>(shape));
    setRow(e, "bankLength", 1.8f);
    setRow(e, "bankRotation", 23.0f);
    setRow(e, "heightInfluence", 0.5f);
    setRow(e, "detailScale", 7.0f);
    setRow(e, "driftSpeed", 0.0f);
    return e;
}

world::MediumSlot pack(const world::EffectInstance& e) {
    world::MediumSlot slot{};
    world::packMediumSlot(e, 1.0f, slot);
    return slot;
}

glm::vec3 centreOf(const world::MediumSlot& m) { return glm::vec3(m.lane[0]); }

std::vector<glm::vec3> gridAround(const world::MediumSlot& m, float span, int n) {
    std::vector<glm::vec3> pts;
    const glm::vec3 c = centreOf(m);
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            for (int k = 0; k < n; ++k) {
                const auto u = [&](int a) { return -span + 2.0f * span * (float(a) + 0.37f) / float(n); };
                pts.push_back(c + glm::vec3(u(i), u(j) * 0.4f, u(k)));
            }
        }
    }
    return pts;
}

} // namespace

TEST_CASE("the flow controls are exactly the identity at their defaults", "[fog][flow][effects]") {
    // Every preset, packed as the panel would: swirl, turbulence and (for the five presets that do
    // not author one) swell all pack as zero, and each function then returns its input on the same
    // bits. A preset that authors a swell is the one exception and says so in its own line.
    const world::EffectSchema& s = fogSchema();
    for (const world::EffectStyle& style : s.styles) {
        INFO("preset " << style.name);
        world::EffectInstance e = s.factory("defaults");
        style.apply(e);
        // An off-origin centre with an awkward mantissa, so `c + (p - c)` is NOT `p` in float and a
        // swirl that dropped its identity branch cannot hide behind a centre at zero (it did: the
        // first version of this case used the presets' centre, the origin, and passed that break).
        e.vortex.field.center = glm::vec3(1000.1f, -33.3f, 0.7f);
        const world::MediumSlot m = pack(e);
        CHECK(m.lane[1].z == 0.0f); // swirl
        CHECK(m.lane[7].y == 0.0f); // turbulence
        CHECK(m.lane[5].w == 0.0f); // height colour amount (ADR-714)
        CHECK(m.lane[8].w == 0.0f); // distance colour amount (ADR-714)
        for (const glm::vec3 p : {glm::vec3(13.25f, -7.5f, 401.0f), glm::vec3(-1234.5f, 88.0f, 3.0f),
                                  glm::vec3(0.3f, 0.1f, -0.7f), glm::vec3(977.123f, -40.01f, 12.9f)}) {
            for (const float t : {0.0f, 1.0f / 60.0f, 61.3f, 3600.0f}) {
                const glm::vec3 q = world::fogStructureFrame(m, p, t);
                CHECK(q.x == p.x);
                CHECK(q.y == p.y);
                CHECK(q.z == p.z);
                const glm::vec3 d = world::fogTurbulence(m, p, t);
                CHECK(d == glm::vec3(0.0f));
                if (m.lane[3].x == 0.0f) {
                    CHECK(world::fogSwell(m, t) == 1.0f);
                }
            }
        }
    }
}

TEST_CASE("swirl turns the bank's structure rigidly about its axis", "[fog][flow]") {
    world::EffectInstance e = bankEffect(0, 0.8f);
    setRow(e, "swirl", 0.05f);
    const world::MediumSlot m = pack(e);
    REQUIRE(m.lane[1].z == Approx(0.05f));
    const glm::vec3 c = centreOf(m);
    int changed = 0;
    for (const glm::vec3 p : gridAround(m, 180.0f, 5)) {
        for (const float t : {0.0f, 4.0f, 37.5f}) {
            for (const float dt : {0.5f, 6.0f}) {
                // Carry the point round by the angle the structure turned in dt.
                const float a = 0.05f * dt;
                const glm::vec3 r = p - c;
                const glm::vec3 carried =
                    c + glm::vec3(r.x * std::cos(a) - r.z * std::sin(a), r.y, r.x * std::sin(a) + r.z * std::cos(a));
                INFO("t " << t << " dt " << dt << " p (" << p.x << ", " << p.y << ", " << p.z << ")");
                CHECK(world::fogMacroDetail(m, carried, t + dt) ==
                      Approx(world::fogMacroDetail(m, p, t)).margin(2e-5));
                if (std::abs(world::fogMacroDetail(m, p, t + dt) - world::fogMacroDetail(m, p, t)) > 1e-3f) {
                    ++changed;
                }
            }
        }
    }
    // The control: the structure at a fixed point really does change, or the identity above is
    // satisfied by a field that never moves.
    CHECK(changed > 100);
}

TEST_CASE("swell scales the primitive's extent by exactly its factor", "[fog][flow]") {
    for (const int shape : {0, 1, 2, 4}) {
        INFO("shape " << shape);
        world::EffectInstance e = bankEffect(shape);
        setRow(e, "swell", 0.3f);
        setRow(e, "swellSpeed", 0.25f);
        const world::MediumSlot m = pack(e);
        const glm::vec3 c = centreOf(m);
        const float tPeak = 3.14159265f * 0.5f / 0.25f; // sin = 1
        const float tRest = 0.0f;                        // sin = 0
        const float s = world::fogSwell(m, tPeak);
        REQUIRE(s == Approx(1.3f).margin(1e-5));
        REQUIRE(world::fogSwell(m, tRest) == 1.0f);
        int dense = 0;
        for (const glm::vec3 p : gridAround(m, 260.0f, 7)) {
            glm::vec3 r = p - c;
            glm::vec3 swelled = r;
            swelled.x *= s;
            swelled.z *= s;
            if (shape == 1) {
                swelled.y *= s; // the sphere swells in every direction
            }
            const float rest = world::fogShapeAt(m, c + r, tRest);
            CHECK(world::fogShapeAt(m, c + swelled, tPeak) == Approx(rest).margin(1e-4));
            dense += rest > 0.05f ? 1 : 0;
        }
        CHECK(dense > 20);
    }
}

TEST_CASE("turbulence deforms the outline and stays within the bound it claims", "[fog][flow][bound]") {
    for (const int shape : {0, 1, 3, 4}) {
        INFO("shape " << shape);
        // A crisp edge, so the outline is a thin band a displacement can visibly cross. At the
        // default softness the rim is a third of the radius wide and the same displacement shows as
        // a change of density inside the band instead -- the property is the same, the probe blunter.
        world::EffectInstance calmEffect = bankEffect(shape);
        setRow(calmEffect, "edgeSoftness", 0.08f);
        const world::MediumSlot calm = pack(calmEffect);
        world::EffectInstance e = calmEffect;
        setRow(e, "turbulence", 0.35f);
        setRow(e, "turbulenceScale", 2.0f);
        setRow(e, "turbulenceSpeed", 0.3f);
        const world::MediumSlot m = pack(e);
        REQUIRE(m.lane[7].y == Approx(0.35f));
        const glm::vec3 semi = world::fogSemiAxes(m);
        int grew = 0;
        int eroded = 0;
        double sumD = 0.0;
        double sumChange = 0.0;
        int nD = 0;
        for (const glm::vec3 p : gridAround(m, 340.0f, 16)) {
            for (const float t : {0.0f, 9.0f}) {
                const glm::vec3 d = world::fogTurbulence(m, p, t);
                // The displacement's proof, in the bank's frame: at most `amount` of each semi-axis.
                // Checked on the length in world XZ against the longer horizontal semi-axis, and on Y.
                CHECK(std::hypot(d.x, d.z) <= 0.35f * std::max(semi.x, semi.z) * 1.0001f);
                CHECK(std::abs(d.y) <= 0.35f * semi.y * 1.0001f);
                const float before = world::fogShapeAt(calm, p, t);
                const float after = world::fogShapeAt(m, p, t);
                sumD += glm::length(d / semi);
                sumChange += std::abs(after - before);
                ++nD;
                // Across the outline in each direction. The field is soft-edged (a smoothstep rim
                // a third of the radius wide), so "the outline" is a band and the test is a sample
                // crossing it: thin to thick, or thick to thin.
                grew += (before < 0.15f && after > 0.45f) ? 1 : 0;
                eroded += (before > 0.45f && after < 0.15f) ? 1 : 0;
            }
        }
        // The outline moved both ways: dense fog where the primitive was thin, and thin where it
        // was dense. A turbulence that only modulated density inside the boundary (the detail term's
        // job) would grow nothing.
        INFO("grew " << grew << ", eroded " << eroded << "; mean displacement " << sumD / nD
             << " semi-axes, mean |change| " << sumChange / nD);
        // Measured 8..40 per direction per shape on this grid; zero with the turbulence off.
        CHECK(grew > 5);
        CHECK(eroded > 5);
    }
}

TEST_CASE("turbulence is carried by the drift and is a pure function of time", "[fog][flow]") {
    world::EffectInstance e = bankEffect(0);
    setRow(e, "turbulence", 0.3f);
    setRow(e, "turbulenceSpeed", 0.0f); // frozen shape: then the drift carries it exactly
    setRow(e, "driftSpeed", 5.0f);
    setRow(e, "driftVertical", 0.4f);
    const world::MediumSlot m = pack(e);
    const glm::vec3 v(m.lane[2]);
    REQUIRE(glm::length(v) > 4.0f);
    for (const glm::vec3 p : gridAround(m, 200.0f, 4)) {
        for (const float dt : {0.25f, 8.0f}) {
            const glm::vec3 a = world::fogTurbulence(m, p, 3.0f);
            const glm::vec3 b = world::fogTurbulence(m, p + v * dt, 3.0f + dt);
            CHECK(b.x == Approx(a.x).margin(1e-3));
            CHECK(b.y == Approx(a.y).margin(1e-3));
            CHECK(b.z == Approx(a.z).margin(1e-3));
        }
    }
    // ADR-091: evaluation order does not matter -- no history is read.
    setRow(e, "turbulenceSpeed", 0.7f);
    const world::MediumSlot live = pack(e);
    const glm::vec3 p(80.0f, 40.0f, 10.0f);
    const float at12 = world::fogShapeAt(live, p, 12.0f);
    for (const float t : {99.0f, 0.0f, 12.0f, 5.5f}) {
        const float s = world::fogShapeAt(live, p, t);
        if (t == 12.0f) {
            CHECK(s == at12);
        }
    }
    // ...and at a non-zero rate the shape genuinely evolves in place.
    int evolved = 0;
    for (const glm::vec3 q : gridAround(live, 200.0f, 4)) {
        const glm::vec3 d0 = world::fogTurbulence(live, q, 0.0f);
        const glm::vec3 d1 = world::fogTurbulence(live, q + v * 4.0f, 4.0f);
        evolved += glm::length(d1 - d0) > 1.0f ? 1 : 0;
    }
    CHECK(evolved > 30);
}

TEST_CASE("the turbulence flow is divergence-free and its clamp touches only the tail",
          "[fog][flow]") {
    // `noise::flowCurl` is the CPU twin of the WGSL function, and the property it is chosen for is
    // zero divergence: it moves density around instead of making or destroying it. Central
    // differences at a small step, against the magnitude of the gradients themselves.
    double worstRatio = 0.0;
    int clamped = 0;
    int total = 0;
    double sumLen = 0.0;
    std::vector<float> lengths;
    for (int i = 0; i < 20; ++i) {
        for (int j = 0; j < 20; ++j) {
            for (int k = 0; k < 20; ++k) {
                const glm::vec3 p(0.173f * i - 1.3f, 0.211f * j + 0.4f, 0.157f * k - 2.1f);
                const float t = 0.37f * float(i + j);
                const float h = 1e-3f;
                const glm::vec3 fx1 = noise::flowCurl(p + glm::vec3(h, 0, 0), t, 53u);
                const glm::vec3 fx0 = noise::flowCurl(p - glm::vec3(h, 0, 0), t, 53u);
                const glm::vec3 fy1 = noise::flowCurl(p + glm::vec3(0, h, 0), t, 53u);
                const glm::vec3 fy0 = noise::flowCurl(p - glm::vec3(0, h, 0), t, 53u);
                const glm::vec3 fz1 = noise::flowCurl(p + glm::vec3(0, 0, h), t, 53u);
                const glm::vec3 fz0 = noise::flowCurl(p - glm::vec3(0, 0, h), t, 53u);
                const double div = (fx1.x - fx0.x + fy1.y - fy0.y + fz1.z - fz0.z) / (2.0 * h);
                const double scale = (std::abs(fx1.x - fx0.x) + std::abs(fy1.y - fy0.y) +
                                      std::abs(fz1.z - fz0.z)) / (2.0 * h) + 1e-3;
                worstRatio = std::max(worstRatio, std::abs(div) / scale);
                const float len = glm::length(noise::flowCurl(p, t, 53u)) * world::kFogTurbulenceGain;
                lengths.push_back(len);
                sumLen += len;
                clamped += len > 1.0f ? 1 : 0;
                ++total;
            }
        }
    }
    const double clampedFraction = double(clamped) / double(total);
    std::sort(lengths.begin(), lengths.end());
    INFO("worst |div| / |partials| " << worstRatio << "; scaled length mean " << sumLen / total
         << ", median " << lengths[lengths.size() / 2] << ", p98 " << lengths[lengths.size() * 98 / 100]
         << ", max " << lengths.back() << "; clamp engaged on " << clampedFraction * 100.0
         << "% of samples");
    // Divergence is zero up to the finite difference's own error, against partials of order 1.
    CHECK(worstRatio < 0.02);
    // The gain is set so the clamp is a bound on the tail, not a reshaping of the flow.
    CHECK(clampedFraction < 0.02);
    // ...and not so low that the "amount" row can never reach its stated reach.
    CHECK(sumLen / total > 0.2);
}

TEST_CASE("every flow row reaches its lane and the lane reaches the field", "[fog][flow][effects]") {
    // ADR-421, and the reason ADR-713 exists: eight rows on this panel packed numbers nothing read.
    // Each row here is moved alone and must change BOTH its packed lane and what the field returns.
    struct Row {
        const char* leaf;
        float value;
        int lane;
        int component;
    };
    for (const Row& r : {Row{"swirl", 0.08f, 1, 2}, Row{"swell", 0.25f, 3, 0},
                         Row{"swellSpeed", 0.4f, 3, 1}, Row{"turbulence", 0.3f, 7, 1},
                         Row{"turbulenceScale", 3.0f, 7, 3}, Row{"turbulenceSpeed", 1.5f, 6, 3}}) {
        INFO("row " << r.leaf);
        world::EffectInstance base = bankEffect(0, 0.7f);
        // Each needs its partner switched on to be visible: a swell needs a rate and a rate needs a
        // swell, and the turbulence's scale and rate need a turbulence. Swirl needs detail (0.7).
        const std::string_view leaf(r.leaf);
        if (leaf == "swellSpeed") {
            setRow(base, "swell", 0.25f);
        }
        if (leaf == "swell") {
            setRow(base, "swellSpeed", 0.4f);
        }
        if (leaf == "turbulenceScale" || leaf == "turbulenceSpeed") {
            setRow(base, "turbulence", 0.3f);
        }
        world::EffectInstance moved = base;
        setRow(moved, r.leaf, r.value);
        const world::MediumSlot a = pack(base);
        const world::MediumSlot b = pack(moved);
        CHECK(b.lane[r.lane][r.component] != a.lane[r.lane][r.component]);
        int differs = 0;
        for (const glm::vec3 p : gridAround(a, 230.0f, 6)) {
            for (const float t : {3.0f, 17.0f}) {
                differs += std::abs(world::fogShapeAt(a, p, t) - world::fogShapeAt(b, p, t)) > 1e-3f ? 1 : 0;
            }
        }
        CHECK(differs > 10);
    }
}
