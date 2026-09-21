// The march's per-slot bound must contain the field it claims to contain (ADR-566).
//
// **What this is evidence for.** `shaders/volume.wgsl` clips each medium's ray march to a vertical
// cylinder, and everything outside that cylinder is skipped without evaluating the field. So the
// cylinder is a CLAIM about where the field is non-zero, and the failure mode when the claim is
// wrong is the worst kind there is: the medium is still there, still soft-edged, still the right
// colour, and simply smaller than the artist authored. Nothing errors. Nothing looks broken. The
// artist turns `Bank length` up, sees the bank not get longer, and decides the control is weak.
//
// **Why it could not be tested before this file.** The claim lived only in WGSL. A rendered frame
// cannot tell you a bank is three times too short unless you already know how long it should be,
// and the only thing that knows that is the field. `world::mediumBound` is the transliteration of
// the shader's bound, so the two can be put in front of each other here: sample the field on a
// grid, and assert that everything it says is dense lies inside what the bound says is possible.
//
// **The two halves, and why they are different assertions.**
//   - A CLOSED primitive (sphere, ellipsoid, box, capsule, cylinder) is exactly zero past its
//     surface, so its bound must contain the support exactly: zero outside, no tolerance.
//   - A BANK has no lid. Its upper profile is an exponential, which is never exactly zero, so a
//     finite bound must cut something and the only honest question is HOW MUCH. ADR-566 chose 1%
//     of peak; this case holds it to 1.2% and would fail at the 82% the old constant bound left.
//
// **How each fails.** In `src/world/medium_bound.cpp` and `shaders/volume.wgsl`:
//   - set `reach` to `1.0f` unconditionally (the pre-ADR-566 bound). "the bound contains the
//     field" fails on every elongated shape -- a bank at `bankLength` 4 has full-density samples
//     a factor of ~3 outside the claimed radius.
//   - set `hTop` to `3.0f` unconditionally (the pre-ADR-566 vertical constant). The same case
//     fails on the low-falloff bank, whose density at the old ceiling is 0.82 of peak.
//   - widen the bound to something enormous and "the bound is not vacuous" fails instead, which
//     is the half that stops the first two being satisfied by giving up.

#include "world/atmospherics.hpp"
#include "world/world_effects/effect_registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

using namespace avgen;

namespace {

const world::EffectSchema& fogSchema() {
    const world::EffectSchema* s = world::effectSchema(world::AtmosphereKind::VolumetricFog);
    REQUIRE(s != nullptr);
    return *s;
}

void setRow(world::AtmosphericEffect& e, std::string_view leaf, float v) {
    const world::EffectSchema& s = fogSchema();
    for (const world::EffectField& f : s.fields) {
        if (std::string_view(f.leaf) == leaf) {
            world::setFieldFloat(f, s, e, v);
            return;
        }
    }
    FAIL("no such row on the fog schema: " << leaf);
}

// The bytes the march reads, through the one writer that produces them (ADR-566's
// `packMediumSlot`): the kind's packer, the reserved-lane check and the kind tag, in order. A test
// that packed and then wrote the tag itself would be asserting about its own copy of the sequence.
world::MediumSlot slotOf(const world::AtmosphericEffect& e) {
    world::MediumSlot slot{};
    world::packMediumSlot(e, 1.0f, slot);
    return slot;
}

struct Case {
    const char* name;
    int shape;
    float bankLength;
    float rotationDeg;
    float heightFalloff;
    float edgeSoftness;
    float groundHug;
};

// Deliberately spread across the ENDS of the two controls that broke the bound, not around their
// defaults. At `bankLength` 1 and `heightFalloff` 1.4 the old bound was right, which is why a day
// of rendering never showed it (docs/testing.md 23: the case that agrees is not the control).
constexpr Case kCases[] = {
    {"bank, defaults", 0, 1.0f, 0.0f, 1.4f, 0.35f, 0.0f},
    {"bank, long", 0, 4.0f, 0.0f, 1.4f, 0.35f, 0.0f},
    {"bank, long and rotated", 0, 4.0f, 37.0f, 1.4f, 0.6f, 0.0f},
    {"bank, shallow falloff", 0, 1.0f, 0.0f, 0.12f, 0.35f, 0.0f},
    {"bank, long and shallow and high-biased", 0, 3.0f, -64.0f, 0.3f, 0.9f, 1.0f},
    {"sphere", 1, 4.0f, 0.0f, 1.4f, 0.35f, 0.0f}, // must IGNORE bankLength, bound included
    {"ellipsoid", 2, 3.0f, 21.0f, 1.4f, 0.5f, 0.5f},
    {"box", 3, 2.5f, -80.0f, 1.4f, 0.2f, 0.0f},
    {"capsule", 4, 4.5f, 12.0f, 1.4f, 0.35f, 0.0f},
    {"cylinder", 5, 2.0f, 50.0f, 1.4f, 0.8f, 1.0f},
};

world::AtmosphericEffect bankFor(const Case& c) {
    const world::EffectSchema& s = fogSchema();
    REQUIRE(s.factory != nullptr);
    world::AtmosphericEffect e = s.factory("bank");
    e.vortex.field.center = glm::vec3(0.0f, 120.0f, 0.0f);
    e.vortex.field.radius = 240.0f;
    e.vortex.field.thickness = 70.0f;
    e.vortex.field.cloudNoise = 0.0f; // the shape alone; the detail term is ADR-565's and is mean 1
    setRow(e, "shape", static_cast<float>(c.shape));
    setRow(e, "bankLength", c.bankLength);
    setRow(e, "bankRotation", c.rotationDeg);
    setRow(e, "heightFalloff", c.heightFalloff);
    setRow(e, "edgeSoftness", c.edgeSoftness);
    setRow(e, "groundHug", c.groundHug);
    setRow(e, "heightInfluence", 1.0f); // the widest the profile can make a closed primitive
    return e;
}

} // namespace

TEST_CASE("the march's bound contains the field it clips", "[fog][bound]") {
    for (const Case& c : kCases) {
        INFO("case: " << c.name);
        const world::AtmosphericEffect e = bankFor(c);
        const world::MediumSlot slot = slotOf(e);
        const world::MediumBound bound = world::mediumBound(slot);
        REQUIRE(bound.radiusXZ > 0.0f);
        REQUIRE(bound.yTop > bound.yBot);

        const glm::vec3 centre = e.vortex.field.center;
        // The search window is derived from the FIELD's parameters and not from the bound, which
        // matters: a window sized by the bound shrinks with it, and a too-small bound would then
        // hide its own defect by moving the search inside itself. These multipliers are generous
        // (1.6x the design extent plus a margin) so that a bound broken in either direction still
        // has the dense samples it wrongly excludes inside the window being searched.
        const float radius = e.vortex.field.radius;
        const float thickness = e.vortex.field.thickness;
        const float spanXZ = radius * std::max(c.bankLength, 1.0f) * 2.0f + 400.0f;
        const float vertical =
            (c.shape == 0) ? thickness * std::clamp(4.6f / c.heightFalloff, 3.0f, 40.0f) * 1.6f
                           : std::max(radius, thickness) * 2.2f;
        const float yLo = centre.y - thickness * 4.0f - 400.0f;
        const float yHi = centre.y + vertical + 400.0f;

        constexpr int kN = 34;
        float worstOutside = 0.0f;   // the densest sample the bound claims cannot exist
        glm::vec3 worstAt(0.0f);
        float insideMaxR = 0.0f;     // how far the field actually reaches, for the vacuity check
        float insideMaxY = centre.y;
        for (int ix = 0; ix <= kN; ++ix) {
            for (int iy = 0; iy <= kN; ++iy) {
                for (int iz = 0; iz <= kN; ++iz) {
                    const glm::vec3 p(centre.x - spanXZ + 2.0f * spanXZ * float(ix) / float(kN),
                                      yLo + (yHi - yLo) * float(iy) / float(kN),
                                      centre.z - spanXZ + 2.0f * spanXZ * float(iz) / float(kN));
                    const float d = world::fogShapeAt(slot, p, 0.0f);
                    if (d <= 0.0f) {
                        continue;
                    }
                    const float r = std::hypot(p.x - centre.x, p.z - centre.z);
                    const bool inside = r <= bound.radiusXZ && p.y >= bound.yBot && p.y <= bound.yTop;
                    if (inside) {
                        insideMaxR = std::max(insideMaxR, r);
                        insideMaxY = std::max(insideMaxY, p.y);
                    } else if (d > worstOutside) {
                        worstOutside = d;
                        worstAt = p;
                    }
                }
            }
        }

        INFO("densest sample outside the bound: " << worstOutside << " at (" << worstAt.x << ", "
             << worstAt.y << ", " << worstAt.z << "); bound r=" << bound.radiusXZ
             << " y=[" << bound.yBot << ", " << bound.yTop << "]");
        if (c.shape == 0) {
            // A bank's exponential top never reaches zero, so a finite bound must cut something.
            // ADR-566 sized it to leave 1% of peak; the slack here is the grid's, not the design's.
            CHECK(worstOutside <= 0.012f);
        } else {
            // A closed primitive is exactly zero past its surface. No tolerance is owed.
            CHECK(worstOutside == 0.0f);
        }

        // The half that stops the other half being satisfied by returning infinity. A bound is
        // allowed to be generous -- it costs only field evaluations -- but one that is orders of
        // magnitude past the field is a bound that has stopped meaning anything, and it would let
        // the containment check above pass while the march skipped nothing.
        // ...and the grid's own resolution is added back before the comparison, because a sampled
        // support can only be reported to within one step and the box's top landed between two.
        // docs/testing.md 21: count what fits in the window before trusting a number taken over
        // it -- here the window is fine and the STEP was the thing to count.
        const float stepXZ = 2.0f * spanXZ / float(kN);
        const float stepY = (yHi - yLo) / float(kN);
        INFO("field reaches r=" << insideMaxR << " of a claimed " << bound.radiusXZ
             << ", and y=" << insideMaxY << " of a claimed " << bound.yTop
             << " (grid step " << stepXZ << " x " << stepY << ")");
        CHECK(insideMaxR + 2.0f * stepXZ > bound.radiusXZ * 0.55f);
        CHECK(insideMaxY - centre.y + 2.0f * stepY > (bound.yTop - centre.y) * 0.5f);
    }
}
