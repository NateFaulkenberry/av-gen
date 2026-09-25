// The fog's §25 height and distance colours (ADR-714).
//
// **The property that is the design, held as an equality.** §25's brief: *"avoid making colour
// responsible for structure."* The eye reads this medium's structure from luminance -- the three
// depth colours run dark where the bank is thin and bright where it is dense -- so a tint that
// changed luminance would be drawing density that is not there. Both tints are therefore
// luminance-preserving by construction, and case 1 asserts it for arbitrary colours and weights.
//
// **How each case fails.**
//   1. Luminance: mix toward `tint` itself instead of toward `tint` rescaled to `base`'s
//      luminance (`fogHueMix`), and case 1 fails at the first saturated pair.
//   2. Identity at amount 0, bitwise: remove the `w <= 0` early return and case 2 fails on the
//      reconstructed colour's rounding.
//   3. The weights are functions of WHERE, never of density: the height weight rises from the
//      bank's densest layer to two thicknesses above it and the distance weight is
//      `1 - exp(-d / range)`. Swap the smoothstep's ends and the monotone half fails.
//   4. Every row reaches its lane, and the lane reaches the colour (ADR-421).
//   5. The hue really moves toward the tint -- the control for case 1, which a function that
//      returned `base` unchanged would pass perfectly.

#include "world/atmospherics.hpp"
#include "world/effects/effect_registry.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string_view>

using Catch::Approx;
using namespace avgen;

namespace {

const world::EffectSchema& fogSchema() {
    const world::EffectSchema* s = world::effectSchema(world::EffectKind::VolumetricFog);
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
    return fogSchema().fields[0];
}

void setRow(world::EffectInstance& e, std::string_view leaf, float v) {
    world::setFieldFloat(row(leaf), fogSchema(), e, v);
}

world::MediumSlot pack(const world::EffectInstance& e) {
    world::MediumSlot slot{};
    world::packMediumSlot(e, 1.0f, slot);
    return slot;
}

world::EffectInstance bank() {
    world::EffectInstance e = fogSchema().factory("colour");
    e.vortex.field.center = glm::vec3(0.0f, 40.0f, 0.0f);
    e.vortex.field.radius = 300.0f;
    e.vortex.field.thickness = 50.0f;
    return e;
}

// Small deterministic sequence in [0, 1).
float frac(float x) { return x - std::floor(x); }

} // namespace

TEST_CASE("a height or distance colour moves hue and never luminance", "[fog][colour]") {
    int moved = 0;
    for (int i = 0; i < 400; ++i) {
        const float a = float(i);
        const glm::vec3 base(frac(a * 0.6180f) * 0.4f, frac(a * 0.4142f) * 0.4f, frac(a * 0.7320f) * 0.4f);
        const glm::vec3 tint(frac(a * 0.2360f + 0.1f), frac(a * 0.3170f + 0.5f), frac(a * 0.8660f + 0.3f));
        const float w = frac(a * 0.1414f);
        const glm::vec3 c = world::fogHueMix(base, tint, w);
        INFO("i " << i << " base (" << base.r << ", " << base.g << ", " << base.b << ") tint (" << tint.r
             << ", " << tint.g << ", " << tint.b << ") w " << w);
        CHECK(world::fogLuminance(c) == Approx(world::fogLuminance(base)).margin(1e-6).epsilon(1e-5));
        // The control: the colour itself moved toward the tint's chromaticity.
        if (world::fogLuminance(tint) > 1e-3f && w > 0.2f && glm::length(c - base) > 1e-3f) {
            ++moved;
        }
    }
    CHECK(moved > 200);
}

TEST_CASE("the tints are exactly the identity at amount 0", "[fog][colour]") {
    const world::MediumSlot m = pack(bank()); // every row at its default
    REQUIRE(m.lane[5].w == 0.0f);
    REQUIRE(m.lane[8].w == 0.0f);
    for (const glm::vec3 base : {glm::vec3(0.02f, 0.3f, 0.11f), glm::vec3(0.7f, 0.1f, 0.0f)}) {
        for (const float relY : {-80.0f, 0.0f, 400.0f}) {
            for (const float d : {0.0f, 150.0f, 1e5f}) {
                const glm::vec3 c = world::fogTintedColour(m, base, relY, d);
                CHECK(c.r == base.r);
                CHECK(c.g == base.g);
                CHECK(c.b == base.b);
            }
        }
    }
}

TEST_CASE("the height and distance weights depend on where, not on how dense", "[fog][colour]") {
    world::EffectInstance e = bank();
    setRow(e, "heightColorAmount", 0.8f);
    setRow(e, "distanceColorAmount", 0.6f);
    setRow(e, "distanceColorRange", 500.0f);
    const world::MediumSlot m = pack(e);
    // groundHug 0 puts the densest layer one thickness below the centre.
    const float floor = -50.0f;
    CHECK(world::fogHeightColourWeight(m, floor) == 0.0f);
    CHECK(world::fogHeightColourWeight(m, floor - 30.0f) == 0.0f);
    CHECK(world::fogHeightColourWeight(m, floor + 100.0f) == Approx(0.8f));
    CHECK(world::fogHeightColourWeight(m, floor + 400.0f) == Approx(0.8f));
    float last = -1.0f;
    for (int i = 0; i <= 20; ++i) {
        const float w = world::fogHeightColourWeight(m, floor + 5.0f * float(i));
        CHECK(w >= last);
        last = w;
    }
    CHECK(world::fogDistanceColourWeight(m, 0.0f) == 0.0f);
    CHECK(world::fogDistanceColourWeight(m, 500.0f) == Approx(0.6f * (1.0f - std::exp(-1.0f))));
    CHECK(world::fogDistanceColourWeight(m, 1e6f) == Approx(0.6f));

    // Density does not enter: a bank ten times as dense, with a different contrast and threshold,
    // tints the same point the same.
    world::EffectInstance thick = e;
    thick.vortex.density *= 10.0f;
    thick.vortex.field.contrast = 3.0f;
    setRow(thick, "densityThreshold", 0.3f);
    const world::MediumSlot n = pack(thick);
    for (const float relY : {-20.0f, 10.0f, 60.0f}) {
        const glm::vec3 base(0.1f, 0.2f, 0.15f);
        const glm::vec3 a = world::fogTintedColour(m, base, relY, 700.0f);
        const glm::vec3 b = world::fogTintedColour(n, base, relY, 700.0f);
        CHECK(a == b);
    }
}

TEST_CASE("every colour row reaches its lane and the lane reaches the colour", "[fog][colour][effects]") {
    const world::EffectSchema& s = fogSchema();
    const glm::vec3 base(0.05f, 0.18f, 0.12f);
    const auto tinted = [&](const world::MediumSlot& m) {
        return world::fogTintedColour(m, base, 120.0f, 900.0f);
    };

    world::EffectInstance on = bank();
    setRow(on, "heightColorAmount", 0.9f);
    setRow(on, "distanceColorAmount", 0.7f);
    const world::MediumSlot reference = pack(on);

    struct Move {
        const char* leaf;
        int lane;
        int component;
    };
    for (const Move& mv : {Move{"heightColorAmount", 5, 3}, Move{"distanceColorAmount", 8, 3},
                           Move{"distanceColorRange", 2, 3}}) {
        INFO("row " << mv.leaf);
        world::EffectInstance e = on;
        setRow(e, mv.leaf, std::string_view(mv.leaf) == "distanceColorRange" ? 3000.0f : 0.3f);
        const world::MediumSlot m = pack(e);
        CHECK(m.lane[mv.lane][mv.component] != reference.lane[mv.lane][mv.component]);
        CHECK(glm::length(tinted(m) - tinted(reference)) > 1e-3f);
    }
    // The two colours, through the store the panel writes.
    for (const char* leaf : {"heightColor", "distanceColor"}) {
        INFO("row " << leaf);
        const world::EffectField& f = row(leaf);
        REQUIRE(f.type == world::FieldType::Color);
        world::EffectInstance e = on;
        e.values.setColor(world::storeKey(s, f), glm::vec3(0.9f, 0.05f, 0.4f));
        const world::MediumSlot m = pack(e);
        CHECK(glm::length(tinted(m) - tinted(reference)) > 1e-3f);
    }
    // And the height colour lands in the three depth colours' fourth components.
    world::EffectInstance e = on;
    e.values.setColor(world::storeKey(s, row("heightColor")), glm::vec3(0.25f, 0.5f, 0.75f));
    const world::MediumSlot m = pack(e);
    CHECK(m.lane[9].w == 0.25f);
    CHECK(m.lane[10].w == 0.5f);
    CHECK(m.lane[11].w == 0.75f);
    CHECK(glm::vec3(m.lane[9]) == e.vortex.colorDeep); // the depth colours themselves untouched
}
