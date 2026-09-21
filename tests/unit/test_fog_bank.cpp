// The fog bank's field, as an artist reaches it (ADR-560, ADR-561).
//
// Every question here goes through the SHIPPED path and not through a copy of it: the registry's
// own factory and styles produce the `AtmosphericEffect`, `vortex::packVortex` packs its `field` -- which is exactly the sequence `rendering::VolumeRenderer::update`
// performs, and since ADR-561 it is literally the same three calls. That matters more here than
// usual, because ADR-401 recorded the failure mode: a test can be green about a path nobody
// renders. Before ADR-561 the conversion step existed only inside the renderer, so a test of this
// shape would have had to write it out a second time and would then have been asserting about its
// own copy. ADR-562 deleted the conversion outright: `world::Vortex` composes `vortex::VortexField`,
// so `e.vortex.field` IS what the renderer packs.
//
// What each case is evidence for, and how it fails:
//
//   1. A fog bank is filled to its axis. Before ADR-561 the envelope was EXACTLY ZERO on the
//      centre line -- a 200 m hole inside a 900 m bank -- because `eyeWallWidth` defaults to the
//      0.22 the vortex's eye wants and `applyStyle` never reset it. Delete `v.field.eyeWallWidth = 0`
//      from `applyStyle` and case 1 fails at the first assertion.
//   2. The detail weight is reachable from the fog kind's own rows. Before ADR-561 `cloudNoise` was
//      declared by the vortex and not by the fog bank, so the one control the brief's §44 bar 1 and
//      its Definition of Done both turn on could not be reached from the fog panel at all. Delete
//      the `detailAmount` row and case 2 fails.
//   3. Detail at 0 leaves a field with shape in it. This is the bar ADR-560 says is currently
//      unreachable BY CONSTRUCTION, so it is deliberately written as the weak form that passes
//      today -- the envelope must vary across the bank -- and the strong form it will become when
//      §46 B lands is named in the case itself.

#include "core/vortex.hpp"
#include "world/atmospherics.hpp"
#include "world/world_effects/effect_registry.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

using Catch::Approx;
using namespace avgen;

namespace {

const world::EffectSchema& fogSchema() {
    const world::EffectSchema* s = world::effectSchema(world::AtmosphereKind::VolumetricFog);
    REQUIRE(s != nullptr);
    return *s;
}

// A fog bank as the Add button makes one, then as a named style leaves it.
world::AtmosphericEffect fogBank(std::string_view style) {
    const world::EffectSchema& s = fogSchema();
    REQUIRE(s.factory != nullptr);
    world::AtmosphericEffect e = s.factory("bank");
    const auto it = std::find_if(s.styles.begin(), s.styles.end(),
                                 [style](const world::EffectStyle& st) { return st.name == style; });
    REQUIRE(it != s.styles.end());
    REQUIRE(it->apply != nullptr);
    it->apply(e);
    return e;
}

vortex::VortexSample sampleAt(const world::AtmosphericEffect& e, glm::vec3 offset, float t = 0.0f) {
    return vortex::sampleVortex(vortex::packVortex(e.vortex.field),
                                e.vortex.field.center + offset, t);
}

} // namespace

TEST_CASE("a fog bank is filled to its own axis", "[fog]") {
    for (const world::EffectStyle& style : fogSchema().styles) {
        INFO("style: " << style.name);
        const world::AtmosphericEffect e = fogBank(style.name);
        const float radius = e.vortex.field.radius;
        REQUIRE(radius > 0.0f);

        // The centre line, at the height the bank's Gaussian is centred on. There is nothing here
        // for the eye's smoothstep to climb out of, so the envelope must already be at its plateau.
        const vortex::VortexSample axis = sampleAt(e, glm::vec3(0.0f));
        REQUIRE(axis.radialT == Approx(0.0f).margin(1e-6f));
        // Before ADR-561 this was 0.0000 exactly, for every style.
        CHECK(axis.envelope > 0.99f);

        // ...and it does not dip on the way out to the rim, which is the shape a residual eye wall
        // would leave behind if `eyeWallWidth` were merely reduced rather than removed. 0.22 is
        // where the old crest sat, so the samples deliberately straddle it.
        for (const float rr : {0.05f, 0.10f, 0.22f, 0.35f, 0.50f, 0.65f}) {
            INFO("rr = " << rr);
            CHECK(sampleAt(e, glm::vec3(radius * rr, 0.0f, 0.0f)).envelope > 0.99f);
        }

        // The rim still falls away, or the test above would pass on a field that is simply 1
        // everywhere and this case would prove nothing (ADR-182).
        CHECK(sampleAt(e, glm::vec3(radius * 1.4f, 0.0f, 0.0f)).envelope == Approx(0.0f).margin(1e-6f));
    }
}

TEST_CASE("the fog bank's detail weight is reachable from its own rows", "[fog]") {
    const world::EffectSchema& s = fogSchema();
    world::AtmosphericEffect e = fogBank("Valley Mist");

    // The panel, registration and the route table all walk `s.fields`. A control that is not a row
    // here is a control that does not exist for this kind, whatever the struct carries (ADR-421).
    const auto row = std::find_if(s.fields.begin(), s.fields.end(), [](const world::EffectField& f) {
        return std::string_view(f.leaf) == "detailAmount";
    });
    REQUIRE(row != s.fields.end());

    // Reachability, not existence: the row's setter must move the member the march reads, and the
    // move must reach the packed bytes. This is the probe shape ADR-460's parity work found a live
    // defect with ("changing innerVoid moved 0 of 160 GPU samples").
    world::setFieldFloat(*row, s, e, 0.0f);
    CHECK(e.vortex.field.cloudNoise == Approx(0.0f));
    const vortex::VortexUniforms off = vortex::packVortex(e.vortex.field);
    world::setFieldFloat(*row, s, e, 1.0f);
    CHECK(e.vortex.field.cloudNoise == Approx(1.0f));
    const vortex::VortexUniforms on = vortex::packVortex(e.vortex.field);
    CHECK(off.v7.z != on.v7.z);

    // And it must reach the picture: somewhere inside the bank the two must disagree about the
    // density. A packed byte that no sample depends on is the same defect one layer down.
    const float radius = e.vortex.field.radius;
    bool moved = false;
    for (int i = 1; i < 24 && !moved; ++i) {
        const float rr = 0.04f * static_cast<float>(i);
        const glm::vec3 p = e.vortex.field.center + glm::vec3(radius * rr, 0.0f, radius * rr * 0.31f);
        moved = vortex::sampleVortex(off, p, 3.0f).density !=
                vortex::sampleVortex(on, p, 3.0f).density;
    }
    CHECK(moved);
}

TEST_CASE("a fog bank with its detail at zero still has a field with shape in it", "[fog]") {
    world::AtmosphericEffect e = fogBank("Valley Mist");
    e.vortex.field.cloudNoise = 0.0f;
    const vortex::VortexUniforms u = vortex::packVortex(e.vortex.field);
    const float radius = e.vortex.field.radius;
    const float thickness = e.vortex.field.thickness;

    // The WEAK form, and it is weak on purpose. ADR-560's finding is that with the detail at zero
    // this field is monotone in radius, uniform in ANGLE and smooth in height -- a grey card rather
    // than fog -- so the brief's §44 bar 1 is unreachable by construction and a test asserting it
    // today would be a test that cannot pass. What is true today is that the field varies at all,
    // which is the floor: a bank that is uniform everywhere is not even a bank.
    std::vector<float> radial;
    for (int i = 0; i <= 14; ++i) {
        const float rr = 0.1f * static_cast<float>(i);
        radial.push_back(vortex::sampleVortex(u, e.vortex.field.center + glm::vec3(radius * rr, 0.0f, 0.0f),
                                              0.0f).density);
    }
    CHECK(*std::max_element(radial.begin(), radial.end()) > 0.0f);
    CHECK(*std::min_element(radial.begin(), radial.end()) == Approx(0.0f).margin(1e-6f));
    // Vertical: the Gaussian wall, so a sample two half-heights up is far below the centre's.
    const float mid = vortex::sampleVortex(u, e.vortex.field.center, 0.0f).density;
    const float high = vortex::sampleVortex(u, e.vortex.field.center + glm::vec3(0.0f, thickness * 2.0f, 0.0f),
                                            0.0f).density;
    CHECK(high < mid * 0.05f);

    // The STRONG form, which §46 B has to make pass and which is recorded here rather than in a
    // document so that it is the thing that goes green: with the detail at zero the field must vary
    // with ANGLE at a fixed radius and height. Today it cannot -- every term in the envelope is a
    // function of `length(rel.xz)` and `rel.y` alone, and `bandArms` is 0 for a fog bank -- so this
    // is deliberately asserted the wrong way round, as the "before" that must break when the shape
    // arrives. If this case starts failing, §46 B has landed and the CHECK below inverts.
    float lo = 1e30f;
    float hi = -1e30f;
    for (int i = 0; i < 16; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / 16.0f;
        const glm::vec3 p = e.vortex.field.center +
                            glm::vec3(radius * 0.45f * std::cos(a), 0.0f, radius * 0.45f * std::sin(a));
        const float d = vortex::sampleVortex(u, p, 0.0f).density;
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    }
    INFO("angular spread at rr=0.45, detail 0: " << (hi - lo));
    CHECK(hi - lo == Approx(0.0f).margin(1e-6f)); // ADR-560 §3: it is uniform in angle. Invert here.
}
