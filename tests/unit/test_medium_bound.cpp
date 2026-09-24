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

#include "core/tornado.hpp"
#include "world/atmospherics.hpp"
#include "world/effects/effect_registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

using namespace avgen;

namespace {

const world::EffectSchema& fogSchema() {
    const world::EffectSchema* s = world::effectSchema(world::EffectKind::VolumetricFog);
    REQUIRE(s != nullptr);
    return *s;
}

void setRow(world::EffectInstance& e, std::string_view leaf, float v) {
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
world::MediumSlot slotOf(const world::EffectInstance& e) {
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

world::EffectInstance bankFor(const Case& c) {
    const world::EffectSchema& s = fogSchema();
    REQUIRE(s.factory != nullptr);
    world::EffectInstance e = s.factory("bank");
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
        const world::EffectInstance e = bankFor(c);
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

// ---- the tornado arm (ADR-580, ADR-706) ----------------------------------------------------------
//
// ADR-566's transliteration carried the tornado's arm, and nothing sampled it: this file's cases are
// all fog banks. ADR-706 then moved the tornado's support BELOW its ground contact -- the funnel now
// ends in a rounded tip that reaches `-footSoft`, and the debris cloud has a rounded underside that
// reaches `-kDebrisUnder * skirtHeight` -- so the bound's floor moved with it, and this is the
// check that it moved far enough in both twins' shared expression.
//
// **How it fails.** Put `b.yBot` back to `centre.y - height * 0.02f` in `medium_bound.cpp` and the
// deep-underside and long-tip cases report dense samples below the claimed floor. Set `below` to
// something enormous and the vacuity half fails instead.

namespace {

tornado::TornadoUniforms fieldOf(const world::MediumSlot& slot) {
    tornado::TornadoUniforms u;
    glm::vec4* lanes[] = {&u.t0, &u.t1, &u.t2, &u.t3, &u.t4, &u.t5, &u.t6,
                          &u.t7, &u.t8, &u.t9, &u.t10, &u.t11, &u.t12};
    for (int i = 0; i < 13; ++i) {
        *lanes[i] = slot.lane[i];
    }
    return u;
}

struct TornadoCase {
    const char* name;
    float skirtDensity;
    float skirtHeight;
    float footSoft;
    float touchdown;
    glm::vec2 lean;
    float wobble;
};

constexpr TornadoCase kTornadoCases[] = {
    {"classic cone", 0.8f, 0.10f, 0.04f, 1.0f, {0.0f, 0.0f}, 18.0f},
    {"deep debris underside", 2.5f, 0.6f, 0.04f, 1.0f, {0.0f, 0.0f}, 0.0f},
    {"long soft tip, no debris", 0.0f, 0.10f, 0.35f, 1.0f, {0.0f, 0.0f}, 0.0f},
    {"hanging funnel over its debris", 1.2f, 0.25f, 0.1f, 0.55f, {0.0f, 0.0f}, 0.0f},
    {"leaning and wobbling", 0.8f, 0.10f, 0.04f, 1.0f, {120.0f, -60.0f}, 70.0f},
};

} // namespace

TEST_CASE("the march's bound contains the tornado it clips, tip and debris underside included",
          "[tornado][bound]") {
    const world::EffectSchema* schema = world::effectSchema(world::EffectKind::Tornado);
    REQUIRE(schema != nullptr);
    for (const TornadoCase& c : kTornadoCases) {
        INFO("case: " << c.name);
        world::EffectInstance e = schema->factory("storm");
        tornado::TornadoField& f = e.tornado.field;
        f.base = glm::vec3(300.0f, 40.0f, -200.0f);
        f.skirtDensity = c.skirtDensity;
        f.skirtHeight = c.skirtHeight;
        f.footSoft = c.footSoft;
        f.touchdown = c.touchdown;
        f.lean = c.lean;
        f.wobbleAmount = c.wobble;
        f.cloudAmount = 0.0f; // the shape; detail is a mean-1 multiply on it and cannot widen it
        const world::MediumSlot slot = slotOf(e);
        const world::MediumBound bound = world::mediumBound(slot);
        const tornado::TornadoUniforms u = fieldOf(slot);

        // The window is sized from the FIELD, not from the bound, so a bound that is too small
        // cannot hide its own defect by shrinking the search.
        const float height = f.height;
        const float spanXZ = f.radiusTop * f.cloudWidth + std::hypot(c.lean.x, c.lean.y) + c.wobble + 200.0f;
        const float yLo = f.base.y - height * 0.5f;
        const float yHi = f.base.y + height * 1.2f;
        constexpr int kNXZ = 60;
        constexpr int kNY = 160;
        float worstOutside = 0.0f;
        glm::vec3 worstAt(0.0f);
        float lowestInside = yHi;
        for (int iy = 0; iy <= kNY; ++iy) {
            const float y = yLo + (yHi - yLo) * float(iy) / float(kNY);
            for (int ix = 0; ix <= kNXZ; ++ix) {
                for (int iz = 0; iz <= kNXZ; ++iz) {
                    const glm::vec3 p(f.base.x - spanXZ + 2.0f * spanXZ * float(ix) / float(kNXZ), y,
                                      f.base.z - spanXZ + 2.0f * spanXZ * float(iz) / float(kNXZ));
                    const float d = tornado::tornadoDensity(u, p, 6.0f);
                    if (d <= 0.0f) {
                        continue;
                    }
                    const float r = std::hypot(p.x - f.base.x, p.z - f.base.z);
                    const bool inside = r <= bound.radiusXZ && p.y >= bound.yBot && p.y <= bound.yTop;
                    if (inside) {
                        lowestInside = std::min(lowestInside, p.y);
                    } else if (d > worstOutside) {
                        worstOutside = d;
                        worstAt = p;
                    }
                }
            }
        }
        INFO("densest sample outside: " << worstOutside << " at (" << worstAt.x << ", " << worstAt.y
             << ", " << worstAt.z << "); bound r=" << bound.radiusXZ << " y=[" << bound.yBot << ", "
             << bound.yTop << "]");
        // Compactly supported: exactly zero outside, no tolerance owed.
        CHECK(worstOutside == 0.0f);
        // Not vacuous at the floor: the field reaches to within a few grid steps of the claimed
        // bottom, so the floor is where the storm ends and not somewhere safely below it.
        const float stepY = (yHi - yLo) / float(kNY);
        INFO("the field's lowest sample is " << lowestInside << ", the bound's floor " << bound.yBot);
        CHECK(lowestInside - bound.yBot < 3.0f * stepY + 0.02f * height);
    }
}
