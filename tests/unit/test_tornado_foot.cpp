// Where the tornado meets the ground (ADR-706): the funnel ends in a tip, not a plane, and the
// debris cloud is a mound at the foot that exists only when it is asked for.
//
// These are properties of the analytic field -- structure, evaluated with Detail at 0 -- so they
// are asked of `core/tornado.cpp` directly. `test_tornado_parity_gpu.cpp` holds the shader to the
// same answers, including at the new positions below the contact.
//
// **How each fails**, measured on the field this replaced:
//   - the tip: the old funnel was 57 m wide 16 m below the contact (full width) and its envelope
//     just above the old `h = -0.02` plane was 0.158 -- a density that stops at a horizontal cut.
//     Both assertions below fail on it.
//   - the mound: the old skirt put 5.5% of its mass in the upper half of its height (a pool on the
//     floor); this one puts 18%.

#include "core/tornado.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace avgen;

namespace {

// The Classic Cone, with the striations and the detail off: the silhouette alone.
tornado::TornadoField cone(float debris) {
    tornado::TornadoField f;
    f.height = 800.0f;
    f.radiusBottom = 40.0f;
    f.radiusMid = 52.0f;
    f.radiusTop = 70.0f;
    f.taper = 1.4f;
    f.touchdown = 1.0f;
    f.footSoft = 0.04f;
    f.skirtDensity = debris;
    f.skirtWidth = 2.2f;
    f.skirtHeight = 0.10f;
    f.skirtFlare = 0.6f;
    f.stripeCount = 0.0f;
    f.cloudAmount = 0.0f;
    return f;
}

float envelopeAt(const tornado::TornadoUniforms& u, glm::vec3 p) {
    return tornado::sampleTornado(u, p, 0.0f).envelope;
}

// How far from the axis the field reaches at height `y`, over sixteen directions.
float extentAt(const tornado::TornadoUniforms& u, float y) {
    float e = 0.0f;
    for (float r = 0.0f; r < 400.0f; r += 0.5f) {
        for (int a = 0; a < 16; ++a) {
            const float th = static_cast<float>(a) * 0.3927f;
            if (envelopeAt(u, {r * std::cos(th), y, r * std::sin(th)}) > 1e-3f) {
                e = std::max(e, r);
            }
        }
    }
    return e;
}

float maxEnvelopeAt(const tornado::TornadoUniforms& u, float y) {
    float m = 0.0f;
    for (float r = 0.0f; r < 400.0f; r += 0.5f) {
        m = std::max(m, envelopeAt(u, {r, y, 0.0f}));
    }
    return m;
}

// The field's mass between two heights, as a solid of revolution along +x.
double massBetween(const tornado::TornadoUniforms& u, float y0, float y1) {
    double m = 0.0;
    for (float y = y0; y < y1; y += 1.0f) {
        for (float r = 0.0f; r < 400.0f; r += 1.0f) {
            m += static_cast<double>(envelopeAt(u, {r, y, 0.0f})) * r;
        }
    }
    return m;
}

} // namespace

TEST_CASE("the funnel ends in a tip, not a horizontal cut", "[tornado][structure]") {
    const tornado::TornadoUniforms u = tornado::packTornado(cone(0.0f));
    const float full = extentAt(u, 80.0f); // h = 0.1, clear of the foot
    const float below = extentAt(u, -16.0f); // h = -0.02, inside the foot ramp
    INFO("the funnel is " << full << " m wide at h = 0.1 and " << below << " m at h = -0.02");
    REQUIRE(full > 40.0f); // the control: the column is there to be measured
    CHECK(below < 0.6f * full);

    // And there is no plane for the density to stop at: the lowest slice of the field's support
    // is already (nearly) empty, where the old field stopped at 0.158 of its shell.
    const float floorY = -tornado::supportBelow(u) * 800.0f;
    const float atFloor = maxEnvelopeAt(u, floorY + 0.1f);
    INFO("the densest sample 0.1 m above the support floor at " << floorY << " m is " << atFloor);
    CHECK(atFloor < 0.01f);
}

TEST_CASE("the debris cloud is at the foot, and absent with Debris at 0", "[tornado][structure]") {
    const tornado::TornadoUniforms off = tornado::packTornado(cone(0.0f));
    const tornado::TornadoUniforms on = tornado::packTornado(cone(0.8f));

    // Outside the funnel's own reach, around the foot: all debris.
    int present = 0;
    int absent = 0;
    for (int a = 0; a < 12; ++a) {
        const float th = static_cast<float>(a) * 0.5236f;
        for (const float y : {-12.0f, 4.0f, 20.0f, 40.0f}) {
            const glm::vec3 p(80.0f * std::cos(th), y, 80.0f * std::sin(th));
            present += envelopeAt(on, p) > 0.05f ? 1 : 0;
            absent += envelopeAt(off, p) == 0.0f ? 1 : 0;
        }
    }
    INFO(present << " of 48 foot samples carry debris with it on; " << absent << " of 48 are empty with it off");
    CHECK(present >= 44);
    CHECK(absent == 48);

    // ...and nowhere else: above its height the two fields are the same field.
    for (const float y : {120.0f, 400.0f, 760.0f}) {
        for (const float r : {0.0f, 45.0f, 90.0f, 200.0f}) {
            INFO("y = " << y << ", r = " << r);
            CHECK(envelopeAt(on, {r, y, 0.0f}) == envelopeAt(off, {r, y, 0.0f}));
        }
    }
}

TEST_CASE("the debris cloud is a mound, not a pool on the floor", "[tornado][structure]") {
    const tornado::TornadoUniforms off = tornado::packTornado(cone(0.0f));
    const tornado::TornadoUniforms on = tornado::packTornado(cone(0.8f));
    // The debris alone: the field with it minus the field without, over its 80 m height.
    const double lower = massBetween(on, 0.0f, 40.0f) - massBetween(off, 0.0f, 40.0f);
    const double upper = massBetween(on, 40.0f, 80.0f) - massBetween(off, 40.0f, 80.0f);
    REQUIRE(lower + upper > 0.0);
    const double fraction = upper / (lower + upper);
    INFO("the upper half of the debris height holds " << fraction * 100.0 << "% of its mass");
    CHECK(fraction > 0.12);
    // Still denser low than high: dust is lifted from the ground, not dropped onto it.
    CHECK(fraction < 0.5);
}
