// The five local volume primitives (ADR-566, the brief's §9).
//
// **What the brief asks for and why a shape is not a preset.** §9: "Support analytic local volume
// primitives: sphere, ellipsoid, box, capsule, cylinder. ... The artist should be able to place
// several fog banks around the world." The bank that existed before this was ONE shape -- an
// ellipse in plan with a height profile -- and every other formation had to be approximated by
// turning its controls to values that do not mean what they say. A cylinder is not a bank with the
// falloff cranked; it has a flat lid and vertical walls, and no setting of `heightFalloff` produces
// one.
//
// **What each case is evidence for, and how it fails.**
//   1. The three lists agree -- `world::FogShape`, the WGSL constants and the artist-facing names.
//      Reorder any one of them and case 1 fails. This is ADR-562 §9's rule applied to a list: an
//      index that means one thing in C++ and another in a shader is the same defect as a lane that
//      does, and the only difference is that a list can be checked by name.
//   2. Each primitive is the SHAPE IT IS NAMED AFTER, tested at the places where the shapes
//      differ from one another rather than at their centres, where they all agree. The corner of
//      a box is inside it; the corner of its bounding ellipsoid is not. Make `fogPrimitiveDistance`
//      return the bank's elliptical radius for every kind -- the pre-ADR-566 behaviour -- and case
//      2 fails on the box's corner, the cylinder's lid and the sphere's ignoring of `bankLength`.
//   3. A shape ENDS. Every primitive is exactly zero outside its own surface, which is what the
//      march's early-out and ADR-566's bound both depend on; a primitive that leaks is a primitive
//      the bound cannot contain.
//   4. `heightInfluence` at 0 leaves a closed primitive uniform through its height, and at 1 makes
//      it pool. Delete the `mix` in `fogShapeAt` and case 4 fails; it is the check that §9's
//      "height influence" is a control and not a label.
//   5. The Bank is unchanged by all of this. ADR-563's bank is shape 0 and its field must be
//      byte-for-byte what it was, or every existing scene moved. Make the bank take the
//      `heightInfluence` branch and case 5 fails.

#include "world/atmospherics.hpp"
#include "world/world_effects/effect_registry.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <string>
#include <string_view>

using Catch::Approx;
using namespace avgen;

namespace {

const world::EffectSchema& fogSchema() {
    const world::EffectSchema* s = world::effectSchema(world::AtmosphereKind::VolumetricFog);
    REQUIRE(s != nullptr);
    return *s;
}

const world::EffectField& row(std::string_view leaf) {
    for (const world::EffectField& f : fogSchema().fields) {
        if (std::string_view(f.leaf) == leaf) {
            return f;
        }
    }
    FAIL("no such row on the fog schema: " << leaf);
    return fogSchema().fields.front();
}

void setRow(world::AtmosphericEffect& e, std::string_view leaf, float v) {
    world::setFieldFloat(row(leaf), fogSchema(), e, v);
}

// One bank, placed at the origin with round numbers, so the geometry below is arithmetic an
// unaided reader can check: radius 100 across, 300 along at `bankLength` 3, 40 tall.
world::AtmosphericEffect primitive(world::FogShape shape, float bankLength = 3.0f) {
    world::AtmosphericEffect e = fogSchema().factory("p");
    e.vortex.field.center = glm::vec3(0.0f);
    e.vortex.field.radius = 100.0f;
    e.vortex.field.thickness = 40.0f;
    e.vortex.field.cloudNoise = 0.0f;
    setRow(e, "shape", static_cast<float>(static_cast<int>(shape)));
    setRow(e, "bankLength", bankLength);
    setRow(e, "bankRotation", 0.0f);
    setRow(e, "edgeSoftness", 0.02f); // the tightest rim, so a surface is where the maths says
    setRow(e, "groundHug", 0.5f);     // densest layer at the centre, so the shapes are symmetric
    setRow(e, "heightInfluence", 0.0f);
    return e;
}

world::MediumSlot slotOf(const world::AtmosphericEffect& e) {
    world::MediumSlot slot{};
    world::packMediumSlot(e, 1.0f, slot);
    return slot;
}

float distanceAt(const world::AtmosphericEffect& e, glm::vec3 rel) {
    return world::fogPrimitiveDistance(slotOf(e), rel);
}

} // namespace

TEST_CASE("the shape list is one list in three places", "[fog][primitive]") {
    // The C++ enum, the artist-facing names and the WGSL constants. Three copies of one order, and
    // a scene file carries the NAME -- so a disagreement between them is not a compile error, it
    // is every saved bank in the project quietly becoming a different shape.
    const world::EffectField& shapeRow = row("shape");
    REQUIRE(shapeRow.type == world::FieldType::Choice);
    REQUIRE(shapeRow.choiceCount == world::kFogShapeCount);

    constexpr std::array<const char*, 6> kExpected{"Bank",    "Sphere",  "Ellipsoid",
                                                   "Box",     "Capsule", "Cylinder"};
    for (int i = 0; i < world::kFogShapeCount; ++i) {
        INFO("index " << i);
        CHECK(std::string_view(shapeRow.choices[i]) == std::string_view(kExpected[static_cast<std::size_t>(i)]));
    }
    // ...and the same order in the shader, read from the file rather than restated here. A
    // constant copied into a test is a constant the test cannot disagree with.
    std::ifstream in("shaders/fog.wgsl");
    if (!in) {
        WARN("shaders/fog.wgsl not readable from the test's working directory; the WGSL half of "
             "this case did not run");
    } else {
        const std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        constexpr std::array<const char*, 6> kConst{
            "kFogShapeBank: u32 = 0u",   "kFogShapeSphere: u32 = 1u",  "kFogShapeEllipsoid: u32 = 2u",
            "kFogShapeBox: u32 = 3u",    "kFogShapeCapsule: u32 = 4u", "kFogShapeCylinder: u32 = 5u"};
        for (const char* c : kConst) {
            INFO(c);
            CHECK(src.find(c) != std::string::npos);
        }
    }
}

TEST_CASE("each primitive is the shape it is named after", "[fog][primitive]") {
    // Every assertion below is at a point where the shapes DISAGREE. At the centre they are all 0
    // and at infinity they are all large, so a test that sampled either would pass against a
    // single shape wearing six names -- which is exactly the pre-ADR-566 state.

    SECTION("a sphere ignores the length and the thickness") {
        const auto e = primitive(world::FogShape::Sphere, 3.0f);
        CHECK(distanceAt(e, glm::vec3(100.0f, 0.0f, 0.0f)) == Approx(1.0f));
        CHECK(distanceAt(e, glm::vec3(0.0f, 0.0f, 100.0f)) == Approx(1.0f));
        // 40 m up is the THICKNESS, and a sphere does not have one: it is well inside.
        CHECK(distanceAt(e, glm::vec3(0.0f, 40.0f, 0.0f)) == Approx(0.4f));
        // 300 m along is where a `bankLength` 3 shape would have its surface. A sphere has not.
        CHECK(distanceAt(e, glm::vec3(300.0f, 0.0f, 0.0f)) == Approx(3.0f));
    }

    SECTION("an ellipsoid has three different semi-axes") {
        const auto e = primitive(world::FogShape::Ellipsoid, 3.0f);
        CHECK(distanceAt(e, glm::vec3(300.0f, 0.0f, 0.0f)) == Approx(1.0f)); // long
        CHECK(distanceAt(e, glm::vec3(0.0f, 0.0f, 100.0f)) == Approx(1.0f)); // across
        CHECK(distanceAt(e, glm::vec3(0.0f, 40.0f, 0.0f)) == Approx(1.0f));  // up
        // The point where a box and an ellipsoid part company: the corner of the box is OUTSIDE
        // the ellipsoid inscribed in it, by exactly sqrt(3).
        CHECK(distanceAt(e, glm::vec3(300.0f, 40.0f, 100.0f)) == Approx(std::sqrt(3.0f)));
    }

    SECTION("a box has corners") {
        const auto e = primitive(world::FogShape::Box, 3.0f);
        CHECK(distanceAt(e, glm::vec3(300.0f, 40.0f, 100.0f)) == Approx(1.0f)); // the corner is ON it
        CHECK(distanceAt(e, glm::vec3(299.0f, 39.0f, 99.0f)) < 1.0f);           // and just inside
        // A face's centre and its corner are both at distance 1: that is what a box IS, and what
        // no setting of the bank's ellipse can produce.
        CHECK(distanceAt(e, glm::vec3(300.0f, 0.0f, 0.0f)) == Approx(1.0f));
    }

    SECTION("a capsule is a segment, not an ellipsoid") {
        const auto e = primitive(world::FogShape::Capsule, 3.0f);
        // Half-length 300 - 100 = 200, cap radius 100. So the end cap's pole is at 300.
        CHECK(distanceAt(e, glm::vec3(300.0f, 0.0f, 0.0f)) == Approx(1.0f));
        // ...and the SIDE is a cylinder of radius 100 the whole way along, which is the difference
        // from an ellipsoid: at the same 200 m along, an ellipsoid has narrowed and this has not.
        CHECK(distanceAt(e, glm::vec3(200.0f, 0.0f, 100.0f)) == Approx(1.0f));
        CHECK(distanceAt(e, glm::vec3(0.0f, 0.0f, 100.0f)) == Approx(1.0f));
        CHECK(distanceAt(e, glm::vec3(0.0f, 40.0f, 0.0f)) == Approx(1.0f)); // thickness is the Y axis
        // At `bankLength` 1 the segment has no length left and it degenerates rather than inverts.
        const auto round = primitive(world::FogShape::Capsule, 1.0f);
        CHECK(distanceAt(round, glm::vec3(100.0f, 0.0f, 0.0f)) == Approx(1.0f));
    }

    SECTION("a cylinder has a flat lid and vertical walls") {
        const auto e = primitive(world::FogShape::Cylinder, 3.0f);
        CHECK(distanceAt(e, glm::vec3(0.0f, 40.0f, 0.0f)) == Approx(1.0f));
        // The wall does not draw in as it rises -- the property that separates it from every other
        // shape here and that `heightFalloff` cannot produce.
        CHECK(distanceAt(e, glm::vec3(0.0f, 0.0f, 100.0f)) == Approx(1.0f));
        CHECK(distanceAt(e, glm::vec3(0.0f, 39.0f, 100.0f)) == Approx(1.0f));
        // The rim of the lid is on the surface, and the corner past it is outside.
        CHECK(distanceAt(e, glm::vec3(0.0f, 41.0f, 100.0f)) > 1.0f);
    }
}

TEST_CASE("a primitive ends", "[fog][primitive]") {
    // The property the march's early-out and ADR-566's bound are both built on. A shape that
    // leaks is a shape no bound can contain, and the leak would present as cost rather than as a
    // picture -- every sample in the frame evaluating a field that is never quite zero.
    for (int i = 1; i < world::kFogShapeCount; ++i) {
        const auto shape = static_cast<world::FogShape>(i);
        INFO("shape index " << i);
        const auto e = primitive(shape, 3.0f);
        const world::MediumSlot slot = slotOf(e);
        CHECK(world::fogShapeKindOf(slot) == shape);
        // Well outside any of them: the long axis reaches 300, the rim closes by 1.35 of that.
        CHECK(world::fogShapeAt(slot, glm::vec3(900.0f, 0.0f, 0.0f)) == 0.0f);
        CHECK(world::fogShapeAt(slot, glm::vec3(0.0f, 400.0f, 0.0f)) == 0.0f);
        CHECK(world::fogShapeAt(slot, glm::vec3(0.0f, -400.0f, 0.0f)) == 0.0f);
        CHECK(world::fogShapeAt(slot, glm::vec3(0.0f, 0.0f, 600.0f)) == 0.0f);
        // ...and it is not empty, which is the control: "zero everywhere" passes the four above.
        CHECK(world::fogShapeAt(slot, glm::vec3(0.0f)) > 0.0f);
    }
}

TEST_CASE("height influence fills a primitive or pools it", "[fog][primitive]") {
    // §9's "height influence". At 0 a closed volume is uniform through its height; at 1 the bank's
    // profile shapes the density inside it. The measurement is a RATIO between two heights in one
    // volume, so it does not depend on what the absolute density happens to be.
    auto pooled = primitive(world::FogShape::Ellipsoid, 1.0f);
    setRow(pooled, "groundHug", 0.0f);      // densest layer at the floor
    setRow(pooled, "heightFalloff", 3.0f);  // and thinning quickly above it
    auto uniform = pooled;

    setRow(uniform, "heightInfluence", 0.0f);
    setRow(pooled, "heightInfluence", 1.0f);

    const world::MediumSlot su = slotOf(uniform);
    const world::MediumSlot sp = slotOf(pooled);
    // Two points on the same vertical line, both well inside the ellipsoid.
    const glm::vec3 low(0.0f, -20.0f, 0.0f);
    const glm::vec3 high(0.0f, 20.0f, 0.0f);

    const float uLow = world::fogShapeAt(su, low);
    const float uHigh = world::fogShapeAt(su, high);
    const float pLow = world::fogShapeAt(sp, low);
    const float pHigh = world::fogShapeAt(sp, high);
    REQUIRE(uLow > 0.0f);
    REQUIRE(pLow > 0.0f);

    INFO("uniform " << uLow << " -> " << uHigh << ", pooled " << pLow << " -> " << pHigh);
    // At 0 the two heights are the same density: the volume is filled, not shaped.
    CHECK(uHigh == Approx(uLow).epsilon(0.001));
    // At 1 the top is a small fraction of the floor. The profile at these settings is
    // exp(-h * 3) with h measured in thicknesses of 40 m, so 40 m of rise is exp(-3) = 0.05.
    CHECK(pHigh < pLow * 0.25f);
}

TEST_CASE("the bank is what ADR-563 left it", "[fog][primitive]") {
    // Shape 0 must be untouched by ADR-566, because every scene already saved is one. In
    // particular the bank must IGNORE `heightInfluence`: it has no vertical geometry of its own,
    // so blending its profile toward 1 would make it an infinite vertical column -- a field with
    // no support for any bound to contain, which is the defect ADR-566 is about, reintroduced
    // through a control instead of through a constant.
    auto a = primitive(world::FogShape::Bank, 2.0f);
    setRow(a, "heightInfluence", 0.0f);
    auto b = a;
    setRow(b, "heightInfluence", 1.0f);

    const world::MediumSlot sa = slotOf(a);
    const world::MediumSlot sb = slotOf(b);
    bool sawDensity = false;
    for (float y = -200.0f; y <= 400.0f; y += 25.0f) {
        for (float x = -400.0f; x <= 400.0f; x += 50.0f) {
            const glm::vec3 p(x, y, 0.0f);
            const float da = world::fogShapeAt(sa, p);
            INFO("at (" << x << ", " << y << ")");
            CHECK(da == world::fogShapeAt(sb, p));
            sawDensity = sawDensity || da > 0.0f;
        }
    }
    CHECK(sawDensity); // the control: "identical everywhere" is free if both are empty

    // And the bank is still the ellipse ADR-563 gave it -- the horizontal footprint, unchanged by
    // the dispatch that now stands in front of it.
    CHECK(world::fogPrimitiveDistance(sa, glm::vec3(200.0f, 0.0f, 0.0f)) ==
          Approx(world::fogEllipticalRadius(sa, glm::vec3(200.0f, 0.0f, 0.0f))));
    CHECK(world::fogPrimitiveDistance(sa, glm::vec3(0.0f, 999.0f, 70.0f)) ==
          Approx(world::fogEllipticalRadius(sa, glm::vec3(0.0f, 999.0f, 70.0f))));
}


TEST_CASE("the density response curve is identity at its defaults and a curve away from them",
          "[fog][density]") {
    // ADR-571, the brief's §24: raw density -> remap -> final density, with Density, Contrast,
    // Threshold and Softness exposed. Two properties, and the first is the one that makes the
    // second safe to ship.
    //
    // **`Contrast` was a dead knob before this.** The row has existed since this kind did and all
    // three styles set it -- Valley Mist 1.5, Glowmere Haze 2.6, Dense Bank 3.4 -- and since
    // ADR-563 gave the fog its own field nothing read the number. Delete the `pow` in
    // `fogDensityRemap` and the third section fails; before ADR-571 every assertion in it would
    // have passed against a field that ignored the control entirely.
    auto plain = primitive(world::FogShape::Sphere, 1.0f);
    setRow(plain, "densityThreshold", 0.0f);
    setRow(plain, "densitySoftness", 0.0f);
    plain.vortex.field.contrast = 1.0f;

    SECTION("at threshold 0, softness 0 and contrast 1 the curve does nothing") {
        // The default a bank is made with, so this is the promise that §24 is opt-in. `1e-4` is
        // float rounding through a divide and a clamp, not a tolerance on the behaviour.
        const world::MediumSlot m = slotOf(plain);
        for (const float s : {0.0f, 0.05f, 0.31f, 0.5f, 0.87f, 1.0f}) {
            INFO("raw density " << s);
            CHECK(world::fogDensityRemap(m, s) == Approx(s).margin(1e-4));
        }
    }

    SECTION("a threshold clears thin density and renormalises what is left") {
        auto e = plain;
        setRow(e, "densityThreshold", 0.4f);
        const world::MediumSlot m = slotOf(e);
        CHECK(world::fogDensityRemap(m, 0.2f) == 0.0f);   // below it: clear air
        CHECK(world::fogDensityRemap(m, 0.4f) == 0.0f);   // exactly at it
        CHECK(world::fogDensityRemap(m, 0.7f) == Approx(0.5f));  // halfway up what remains
        CHECK(world::fogDensityRemap(m, 1.0f) == Approx(1.0f));  // and the core is untouched
    }

    SECTION("contrast is a response curve and it REACHES the field") {
        auto dense = plain;
        dense.vortex.field.contrast = 3.0f;
        auto thin = plain;
        thin.vortex.field.contrast = 0.4f;
        const world::MediumSlot md = slotOf(dense);
        const world::MediumSlot mt = slotOf(thin);
        // Above 1 the mid-range thins and the core stays; below 1 the mid-range fills out. The
        // ends are fixed points of x^k, which is what makes this a CURVE rather than a scale.
        CHECK(world::fogDensityRemap(md, 0.5f) < 0.5f);
        CHECK(world::fogDensityRemap(mt, 0.5f) > 0.5f);
        CHECK(world::fogDensityRemap(md, 1.0f) == Approx(1.0f));
        CHECK(world::fogDensityRemap(md, 0.0f) == Approx(0.0f));

        // ...and the same numbers reach `fogShapeAt`, which is the half that was missing: the
        // control was declared and packed and the field never read it.
        //
        // Sampled ON THE RIM and not in the core, because 0 and 1 are fixed points of `x^k`: the
        // first version of this assertion read the centre of the sphere, got 1.0 from both arms
        // and failed -- correctly. A curve has to be measured where the curve is.
        auto rimDense = dense;
        auto rimThin = thin;
        setRow(rimDense, "edgeSoftness", 0.6f);
        setRow(rimThin, "edgeSoftness", 0.6f);
        const world::MediumSlot rd = slotOf(rimDense);
        const world::MediumSlot rt = slotOf(rimThin);
        const glm::vec3 p(100.0f, 0.0f, 0.0f); // the sphere's surface: rim is mid-range here
        const float withDense = world::fogShapeAt(rd, p);
        const float withThin = world::fogShapeAt(rt, p);
        INFO("fogShapeAt with contrast 3.0 = " << withDense << ", with 0.4 = " << withThin);
        REQUIRE(withThin > 0.0f);
        CHECK(withDense < withThin);
    }
}


TEST_CASE("the bank's glow can follow its height", "[fog][emission]") {
    // ADR-575, the brief's §26: "Optional emission: intensity, color, height influence, density
    // influence." The march had three of the four. This is the fourth, and it is a SEPARATE number
    // from the density's height influence on purpose -- a bank can be densest at its floor and
    // glow evenly, or be uniform and glow only where it is low.
    auto e = primitive(world::FogShape::Bank, 1.0f);
    setRow(e, "groundHug", 0.0f);     // densest layer at the floor
    setRow(e, "heightFalloff", 3.0f); // thinning quickly above it

    SECTION("at zero it is exactly uniform, which is what it has always been") {
        setRow(e, "emissionHeight", 0.0f);
        const world::MediumSlot m = slotOf(e);
        for (const float y : {-60.0f, -10.0f, 0.0f, 25.0f, 90.0f}) {
            INFO("y = " << y);
            REQUIRE(world::fogEmissionHeight(m, y) == 1.0f);
        }
    }

    SECTION("at one it follows the density's own vertical profile") {
        // Reusing `fogVerticalProfile` rather than introducing a second vertical shape is the
        // decision under test: one vertical model for the medium. A version with its own curve
        // would pass "the glow fades upward" and fail this.
        setRow(e, "emissionHeight", 1.0f);
        const world::MediumSlot m = slotOf(e);
        for (const float y : {-60.0f, -10.0f, 0.0f, 25.0f, 90.0f}) {
            INFO("y = " << y);
            // `.margin` rather than a relative tolerance: the profile is 5.8e-5 at the top of
            // this range, and `1 + (p - 1)` differs from `p` by float rounding that is nothing in
            // absolute terms and enormous relative to 5.8e-5. A relative tolerance on a quantity
            // that legitimately approaches zero is a test that fails where the value stops
            // mattering.
            CHECK(world::fogEmissionHeight(m, y) ==
                  Approx(world::fogVerticalProfile(m, y)).margin(1e-6));
        }
    }

    SECTION("it is independent of the DENSITY's height influence") {
        // The two are different rows and must stay different numbers. Setting one must not move
        // the other, which is the check that they did not end up sharing a lane slot.
        setRow(e, "emissionHeight", 1.0f);
        setRow(e, "heightInfluence", 0.0f);
        const float a = world::fogEmissionHeight(slotOf(e), 30.0f);
        setRow(e, "heightInfluence", 1.0f);
        const float b = world::fogEmissionHeight(slotOf(e), 30.0f);
        INFO("glow at y=30 with density influence 0 and 1: " << a << ", " << b);
        CHECK(a == b);
        CHECK(a < 1.0f); // ...and the control is doing something, or the equality is free
    }
}

TEST_CASE("a preset is a starting point, not a continuation", "[fog][presets]") {
    // ADR-579, the brief's §37: "Presets are starting points, not hard-coded special effects."
    //
    // `applyStyle` opens with `v = Vortex{}` and a comment explaining exactly why: "a fog preset
    // applied to an effect that was a vortex a moment ago must not leave a spiral and a throat
    // behind, and a preset that only set what it wanted would." **That argument is right and it
    // covers half the parameters.** Since ADR-566 a fog bank's shape, its drift, its density curve
    // and its glow height live in `AtmosphericEffect::values`, and nothing resets those -- so a
    // preset applied after an artist set Shape to Box gets a box, and after another preset gets
    // that preset's leftovers.
    //
    // The property, stated so it cannot be satisfied by accident: **applying B must give the same
    // effect whether or not A was applied first.** Delete the stored-row reset from `applyStyle`
    // and this fails on the first pair.
    const world::EffectSchema& s = fogSchema();
    REQUIRE(!s.styles.empty());

    for (const world::EffectStyle& a : s.styles) {
        for (const world::EffectStyle& b : s.styles) {
            world::AtmosphericEffect viaA = s.factory("p");
            a.apply(viaA);
            // ...and an artist's own edits in between, which is the case a preset must survive
            // being applied after.
            setRow(viaA, "shape", static_cast<float>(static_cast<int>(world::FogShape::Box)));
            setRow(viaA, "driftSpeed", 17.0f);
            setRow(viaA, "densityThreshold", 0.7f);
            b.apply(viaA);

            world::AtmosphericEffect fresh = s.factory("p");
            b.apply(fresh);

            INFO("'" << b.name << "' applied after '" << a.name << "' and an edit");
            for (const world::EffectField& f : s.fields) {
                if (!f.stored || f.type != world::FieldType::Float) {
                    continue;
                }
                INFO("row " << f.leaf);
                CHECK(world::fieldFloat(f, s, viaA) == world::fieldFloat(f, s, fresh));
            }
        }
    }
}
