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

// ADR-565: the field the march evaluates FOR THIS KIND, which since ADR-563 is `fogShapeAt` and
// not the vortex's. A probe that samples the pre-dispatch path goes on passing while asserting
// about a field nothing calls for a fog bank -- it does not break, which is what makes it
// dangerous. See the audit note at the top of this file.
float fogSampleAt(const world::AtmosphericEffect& e, glm::vec3 offset) {
    world::MediumSlot slot{};
    const world::EffectSchema& s = fogSchema();
    REQUIRE(s.resolve.pack != nullptr);
    world::AtmosphericEffect copy = e;
    s.resolve.pack(copy, 1.0f, slot);
    return world::fogShapeAt(slot, e.vortex.field.center + offset);
}

} // namespace

TEST_CASE("a fog bank is filled to its own axis", "[fog]") {
    // ADR-561 fixed a 22%-of-radius hole inherited from the vortex's eye. ADR-563 moved the fog
    // bank onto its own field, where there is no eye term at all -- so this now asserts the
    // property of the field the march ACTUALLY evaluates, rather than of the one it used to.
    for (const world::EffectStyle& style : fogSchema().styles) {
        INFO("style: " << style.name);
        world::AtmosphericEffect e = fogBank(style.name);
        // Detail OFF: the claim is about the ANALYTIC shape having no hole in it. With the macro
        // detail live the field varies by design, which is a different property and is asserted
        // by its own case below -- testing both at once would mean neither could fail cleanly.
        e.vortex.field.cloudNoise = 0.0f;
        const float radius = e.vortex.field.radius;
        REQUIRE(radius > 0.0f);

        const float axis = fogSampleAt(e, glm::vec3(0.0f));
        CHECK(axis > 0.0f);
        // ...and nothing dips on the way out. 0.22 is where the vortex's crest sat, so the samples
        // deliberately still straddle it.
        for (const float rr : {0.05f, 0.10f, 0.22f, 0.35f, 0.50f}) {
            INFO("rr = " << rr);
            CHECK(fogSampleAt(e, glm::vec3(radius * rr, 0.0f, 0.0f)) >= axis * 0.98f);
        }
        // The rim still falls away, or the above would pass on a field that is 1 everywhere and
        // this case would prove nothing (ADR-182).
        CHECK(fogSampleAt(e, glm::vec3(radius * 1.6f, 0.0f, 0.0f)) == Approx(0.0f).margin(1e-6f));
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

    // And it must reach the picture **through the field the march actually evaluates for this
    // kind**. The first version of this sampled `vortex::sampleVortex`, which was correct until
    // ADR-563 gave the fog bank its own density function -- after which the probe went on passing
    // while the control did nothing to a fog bank, because it was sampling a field the march no
    // longer calls for this kind. `docs/testing.md` family C: the probe looked where the effect
    // could not reach, and the tell was that nothing downstream disagreed.
    const world::EffectSchema& fs = fogSchema();
    REQUIRE(fs.resolve.pack != nullptr);
    world::AtmosphericEffect probe = fogBank("Valley Mist");
    const float radius = probe.vortex.field.radius;
    world::MediumSlot slotOff{};
    world::MediumSlot slotOn{};
    probe.vortex.field.cloudNoise = 0.0f;
    fs.resolve.pack(probe, 1.0f, slotOff);
    probe.vortex.field.cloudNoise = 1.0f;
    fs.resolve.pack(probe, 1.0f, slotOn);
    bool moved = false;
    for (int i = 1; i < 24 && !moved; ++i) {
        const float rr = 0.04f * static_cast<float>(i);
        const glm::vec3 p = probe.vortex.field.center +
                            glm::vec3(radius * rr, 0.0f, radius * rr * 0.31f);
        moved = world::fogShapeAt(slotOff, p) != world::fogShapeAt(slotOn, p);
    }
    CHECK(moved);
}

TEST_CASE("a fog bank with its detail at zero still has a field with shape in it", "[fog]") {
    world::AtmosphericEffect e = fogBank("Valley Mist");
    e.vortex.field.cloudNoise = 0.0f;
    const float radius = e.vortex.field.radius;
    const float thickness = e.vortex.field.thickness;

    // ADR-565: sampled through `fogShapeAt`, the field the march evaluates for this kind. This
    // case sampled the VORTEX's field until the audit -- passing the whole time, about a field
    // nothing calls for a fog bank.
    std::vector<float> radial;
    for (int i = 0; i <= 16; ++i) {
        const float rr = 0.1f * static_cast<float>(i);
        radial.push_back(fogSampleAt(e, glm::vec3(radius * rr, 0.0f, 0.0f)));
    }
    CHECK(*std::max_element(radial.begin(), radial.end()) > 0.0f);
    CHECK(*std::min_element(radial.begin(), radial.end()) == Approx(0.0f).margin(1e-6f));
    // Vertical: the profile falls away above the bank.
    const float mid = fogSampleAt(e, glm::vec3(0.0f));
    const float high = fogSampleAt(e, glm::vec3(0.0f, thickness * 4.0f, 0.0f));
    CHECK(high < mid * 0.2f);

    // The STRONG form, and **it has flipped**. ADR-560 recorded this assertion inverted -- asserting
    // that the field is uniform in angle -- as the "before" that §46 B had to break. ADR-563 broke
    // it: a fog bank's footprint is an ELLIPSE now, evaluated by `shaders/fog.wgsl` rather than by
    // the vortex's field, so at a fixed radius and height the density varies with angle **with the
    // detail at zero**. That is §44's first quality bar becoming reachable, which is the whole
    // point of the phase.
    //
    // Measured through the packed lanes and the same `fogShapeAt` the march runs, not a copy.
    world::AtmosphericEffect shaped = fogBank("Valley Mist");
    shaped.vortex.field.cloudNoise = 0.0f;
    shaped.values.setFloat("fog/bankLength", 2.5f); // a bank with a long axis
    world::MediumSlot slot{};
    const world::EffectSchema& fs = fogSchema();
    REQUIRE(fs.resolve.pack != nullptr);
    fs.resolve.pack(shaped, 1.0f, slot);

    // Sampled at 0.9 of the radius, NOT at 0.45, and the difference is a finding rather than a
    // fitting of the test to the code: with `edgeSoftness` at its default the bank is a flat
    // plateau out to about 65% of its radius, so the ellipse is visible in the SILHOUETTE and not
    // in the interior. An honest probe has to sample where the shape actually varies -- the first
    // version of this assertion sampled the plateau and read a spread of exactly 0, which looks
    // identical to the ellipse not working.
    //
    // That the interior is still flat is §46 B's stated limit: this phase gives a bank an outline,
    // and the interior structure is §46 C's local volumes and §46 D's height and distance terms.
    const float r = shaped.vortex.field.radius * 0.9f;
    float lo = 1e30f;
    float hi = -1e30f;
    for (int i = 0; i < 16; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / 16.0f;
        const glm::vec3 p = shaped.vortex.field.center +
                            glm::vec3(r * std::cos(a), 0.0f, r * std::sin(a));
        const float d = world::fogShapeAt(slot, p);
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    }
    INFO("angular spread at rr=0.9, detail 0, bankLength 2.5: " << (hi - lo));
    CHECK(hi - lo > 0.05f); // ADR-563: it varies with angle. Was asserted == 0 before §46 B.

    // ...and the control, or the assertion above would pass on a field that is simply noisy: a
    // CIRCULAR bank must still be uniform in angle, because that is the shape it is.
    shaped.values.setFloat("fog/bankLength", 1.0f);
    world::MediumSlot round{};
    fs.resolve.pack(shaped, 1.0f, round);
    float rlo = 1e30f;
    float rhi = -1e30f;
    for (int i = 0; i < 16; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / 16.0f;
        const glm::vec3 p = shaped.vortex.field.center +
                            glm::vec3(r * std::cos(a), 0.0f, r * std::sin(a));
        const float d = world::fogShapeAt(round, p);
        rlo = std::min(rlo, d);
        rhi = std::max(rhi, d);
    }
    INFO("angular spread of a CIRCULAR bank: " << (rhi - rlo));
    CHECK(rhi - rlo == Approx(0.0f).margin(1e-5f));
}

// ADR-564 (§24): one authored density means the same thing at any bank size.
//
// Before this, `density` was an extinction PER METRE, so the optical depth through a bank scaled
// with the bank: the shipped `Valley Mist` 0.0016 gives 0.45 at a 100 m radius and 4.03 at 900 m --
// invisible to opaque slab, across a range an artist crosses by dragging the RADIUS slider without
// touching density at all. A preset could not ship a correct density because no number is right at
// two sizes.
//
// The probe is the optical depth through the bank's long axis, which is the quantity the artist is
// really setting: `perMetre * crossing`, where `crossing` is what `packMedium` normalised by.
TEST_CASE("one fog density reads the same at any bank size", "[fog]") {
    const world::EffectSchema& s = fogSchema();
    REQUIRE(s.resolve.pack != nullptr);

    const auto opticalDepth = [&](float radius) {
        world::AtmosphericEffect e = fogBank("Valley Mist");
        e.vortex.field.radius = radius;
        e.values.setFloat("fog/bankLength", 2.0f);
        world::MediumSlot slot{};
        s.resolve.pack(e, 1.0f, slot);
        const float perMetre = slot.lane[1].w;
        const float crossing = 2.0f * radius * 2.0f;
        return perMetre * crossing;
    };

    const float small = opticalDepth(100.0f);
    const float mid = opticalDepth(400.0f);
    const float large = opticalDepth(900.0f);
    INFO("optical depth at radius 100 / 400 / 900: " << small << " / " << mid << " / " << large);
    // Equal to within float rounding, across a NINE-fold change in size.
    CHECK(mid == Approx(small).epsilon(0.01));
    CHECK(large == Approx(small).epsilon(0.01));
    // ...and it is not equal by being zero, which is the way this assertion would pass vacuously.
    CHECK(small > 0.1f);

    // The control: the authored number still CHANGES the depth, or the test above would pass on a
    // packer that ignored it entirely (ADR-182, and the shape ADR-460's reachability probe uses).
    world::AtmosphericEffect a = fogBank("Valley Mist");
    a.vortex.field.radius = 400.0f;
    world::MediumSlot lo{};
    world::MediumSlot hi{};
    a.vortex.density = 1.0f;
    s.resolve.pack(a, 1.0f, lo);
    a.vortex.density = 4.0f;
    s.resolve.pack(a, 1.0f, hi);
    CHECK(hi.lane[1].w > lo.lane[1].w * 3.5f);
}

// ADR-565: the macro detail must not move the medium's mean density, and this is MEASURED rather
// than inherited from a form that was already found wrong once.
//
// ADR-560 measured that the vortex's `mix(flatLevel, shaped, cloudNoise)` is **not**
// mean-preserving in practice: turning its detail OFF raised the frame's mean luminance by 20.7
// levels, because the compensation assumed a noise mean that `smokeBillow` and `turbulence` moved.
//
// It matters more for fog than it did for the vortex, and that is ADR-564's doing: `density` is now
// an **optical depth** calibrated against the analytic field's mean, so a detail term that shifts
// the mean silently re-scales every preset. **The optical-depth fix made mean-preservation
// load-bearing in a way it was not before.**
//
// The form is `1 + amount * (n * 2 - 1)`, whose mean is exactly 1 iff `E[n] == 0.5` -- the same
// shape `vortexSpiralBands` uses for the same reason (ADR-389's family rule).
TEST_CASE("the fog macro detail does not move the medium's mean", "[fog]") {
    const world::EffectSchema& s = fogSchema();
    REQUIRE(s.resolve.pack != nullptr);
    world::AtmosphericEffect e = fogBank("Valley Mist");
    e.vortex.field.cloudNoise = 1.0f;
    e.values.setFloat("fog/detailScale", 7.0f);
    world::MediumSlot slot{};
    s.resolve.pack(e, 1.0f, slot);

    // The claim is about the MULTIPLIER, over many periods -- and the distinction is a finding
    // rather than a convenience. The first version averaged the whole field over the bank's
    // interior and read +7.1%, which looks like a violation and is not: a LOW-FREQUENCY term
    // cannot preserve a mean over a region comparable to its own period, because there are only a
    // few periods in it to average. Mean-preservation is a global property of the form, and
    // sampling it locally measures where the bank happens to sit in the noise.
    //
    // So this samples the term itself across many periods. The envelope's own shape is asserted
    // by the cases above and does not belong in this one.
    const float radius = e.vortex.field.radius;
    double sum = 0.0;
    int n = 0;
    for (int i = -40; i <= 40; ++i) {
        for (int j = -40; j <= 40; ++j) {
            const glm::vec3 p = e.vortex.field.center +
                                glm::vec3(radius * 0.35f * static_cast<float>(i), 0.0f,
                                          radius * 0.35f * static_cast<float>(j));
            sum += world::fogMacroDetail(slot, p, 3.0f);
            ++n;
        }
    }
    const double mean = sum / n;
    INFO("mean of the detail multiplier over " << n << " samples: " << mean);
    // Within 1% of unity. Measured directly: E[fbm3] is 0.50044, so the form's mean is 1.00089 at
    // full amount -- against the vortex's equivalent, which ADR-560 measured moving the frame by
    // 20.7 luminance levels when its detail was switched OFF.
    CHECK(mean == Approx(1.0).epsilon(0.01));

    // The control: the term must not be the constant 1.0, or the assertion above is vacuous.
    double lo = 1e30;
    double hi = -1e30;
    for (int i = 0; i < 64; ++i) {
        const glm::vec3 p = e.vortex.field.center + glm::vec3(radius * 0.11f * static_cast<float>(i), 0.0f, 0.0f);
        const double d = world::fogMacroDetail(slot, p, 3.0f);
        lo = std::min(lo, d);
        hi = std::max(hi, d);
    }
    INFO("detail multiplier range: " << lo << " .. " << hi);
    CHECK(hi - lo > 0.2);
}
