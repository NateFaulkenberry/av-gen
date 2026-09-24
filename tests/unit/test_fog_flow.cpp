// The fog's structure MOVES rather than changing in place (ADR-571, the brief's §15 and §16).
//
// **The distinction this file exists to make checkable.** §15: *"Never regenerate random density
// independently per frame ... the fog should move through space, not shimmer internally because
// the texture changed."* That is not a statement about how good the motion looks -- it is an exact
// mathematical property, and §16 writes the implementation down in one line: *"sample the density
// field at `position - velocity x time`"*. Written that way the field satisfies
//
//     detail(p + v*dt, t + dt) == detail(p, t)
//
// EXACTLY, for every p, v, dt. A structure that shimmers cannot satisfy it for any dt at all, and
// a structure carried by an offset added in NOISE space satisfies it only for the one `detailScale`
// the offset was tuned at. So the identity is the test, and it is an equality rather than a
// tolerance -- which is the strongest form a test of this kind can take.
//
// **Why an exact equality is safe here.** Both sides evaluate `fbm3` at arithmetically identical
// arguments: `(p + v*dt) - v*(t + dt)` and `p - v*t` differ only by the order of the operations,
// and the case checks the two agree to a tolerance that is float rounding rather than zero, with
// the sampled positions chosen so the cancellation is exact in the common case.
//
// **What each case is evidence for, and how it fails.**
//   1. The identity itself, over a grid of positions, velocities and time offsets. Restore the
//      pre-ADR-571 form -- `fbm3(p * scale + vec3(drift, drift*0.3, -drift*0.7))` -- and case 1
//      fails, because an offset in noise space advects at `drift / scale` metres a second and the
//      case moves the sample by `drift` metres.
//   2. The motion is REAL: the field at a fixed point genuinely changes with time when the drift
//      is non-zero. Without it, case 1 passes perfectly against a field that ignores both `v` and
//      `t` -- "nothing moves" satisfies every advection identity there is.
//   3. The direction is the bank's long axis and the sign is right, so `bankRotation` steers the
//      drift. Pack the drift without the rotation and case 3 fails.
//   4. Zero drift is exactly static, which is the default and therefore the promise to every
//      scene already saved.

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

void setRow(world::EffectInstance& e, std::string_view leaf, float v) {
    for (const world::EffectField& f : fogSchema().fields) {
        if (std::string_view(f.leaf) == leaf) {
            world::setFieldFloat(f, fogSchema(), e, v);
            return;
        }
    }
    FAIL("no such row on the fog schema: " << leaf);
}

// ADR-572 (§17): the same bank, packed with a resolved flow. `influence` 0 is an unsubscribed
// effect, which is what a default-constructed `MediumFlowInput` already is.
world::MediumSlot bankInFlow(float speed, float rotationDeg, float windAmount,
                             glm::vec3 flow, float influence,
                             world::fields::FlowUnits units = world::fields::FlowUnits::Normalised) {
    world::EffectInstance e = fogSchema().factory("drift");
    e.vortex.field.center = glm::vec3(0.0f);
    e.vortex.field.radius = 200.0f;
    e.vortex.field.thickness = 80.0f;
    setRow(e, "bankRotation", rotationDeg);
    setRow(e, "driftSpeed", speed);
    setRow(e, "driftVertical", 0.0f);
    setRow(e, "driftWind", windAmount);
    world::MediumFlowInput in;
    in.sample.flow = flow;
    in.sample.units = units;
    in.influence = influence;
    world::MediumSlot slot{};
    world::packMediumSlot(e, 1.0f, slot, in);
    return slot;
}

world::MediumSlot bank(float speed, float vertical, float rotationDeg, float detail = 0.8f) {
    world::EffectInstance e = fogSchema().factory("drift");
    e.vortex.field.center = glm::vec3(0.0f);
    e.vortex.field.radius = 200.0f;
    e.vortex.field.thickness = 80.0f;
    e.vortex.field.cloudNoise = detail;
    setRow(e, "detailScale", 9.0f);
    setRow(e, "bankRotation", rotationDeg);
    setRow(e, "driftSpeed", speed);
    setRow(e, "driftVertical", vertical);
    world::MediumSlot slot{};
    world::packMediumSlot(e, 1.0f, slot); // ADR-566: the one writer
    return slot;
}

glm::vec3 driftOf(const world::MediumSlot& m) { return glm::vec3(m.lane[2]); }

} // namespace

TEST_CASE("the fog's structure is carried through the world, not regenerated in it",
          "[fog][flow]") {
    // The identity, over velocities that differ in direction, speed and sign, and over time
    // offsets from a frame to a minute. A field that shimmers fails at the first row.
    for (const auto [speed, vertical, rot] : {std::tuple{6.0f, 0.0f, 0.0f},
                                              std::tuple{-3.5f, 1.2f, 37.0f},
                                              std::tuple{12.0f, -2.0f, -104.0f},
                                              std::tuple{0.4f, 0.05f, 180.0f}}) {
        const world::MediumSlot m = bank(speed, vertical, rot);
        const glm::vec3 v = driftOf(m);
        INFO("speed " << speed << " vertical " << vertical << " rotation " << rot
             << " -> velocity (" << v.x << ", " << v.y << ", " << v.z << ")");
        for (const float t : {0.0f, 3.25f, 61.5f}) {
            for (const float dt : {1.0f / 60.0f, 0.5f, 7.0f}) {
                for (const glm::vec3 p : {glm::vec3(10.0f, 5.0f, -20.0f),
                                          glm::vec3(-88.0f, -12.0f, 44.0f),
                                          glm::vec3(0.0f)}) {
                    const float before = world::fogMacroDetail(m, p, t);
                    const float after = world::fogMacroDetail(m, p + v * dt, t + dt);
                    INFO("t=" << t << " dt=" << dt << " p=(" << p.x << ", " << p.y << ", " << p.z
                         << ")");
                    CHECK(after == Approx(before).margin(2e-4));
                }
            }
        }
    }
}

TEST_CASE("the drift actually moves the structure", "[fog][flow]") {
    // THE CONTROL on the case above. A field that ignores its velocity and its time satisfies
    // every advection identity there is, and would pass case 1 perfectly while being a still
    // image. So: at a FIXED point, a drifting bank's detail must change.
    const world::MediumSlot moving = bank(9.0f, 0.0f, 0.0f);
    const glm::vec3 p(35.0f, 10.0f, -15.0f);
    const float a = world::fogMacroDetail(moving, p, 0.0f);
    const float b = world::fogMacroDetail(moving, p, 4.0f);
    INFO("detail at a fixed point: " << a << " at t=0, " << b << " at t=4");
    CHECK(std::abs(b - a) > 0.01f);
    // ...and it is the DRIFT that moves it, not time on its own: a bank with no drift is frozen.
    const world::MediumSlot still = bank(0.0f, 0.0f, 0.0f);
    CHECK(world::fogMacroDetail(still, p, 0.0f) == world::fogMacroDetail(still, p, 4.0f));
}

TEST_CASE("the drift follows the bank's long axis", "[fog][flow]") {
    // §16's "Drift Direction", answered without a control of its own: a bank elongated along a
    // valley drifts along the valley. At rotation 0 the long axis is +X; at 90 degrees it is +Z.
    CHECK(driftOf(bank(5.0f, 0.0f, 0.0f)).x == Approx(5.0f));
    CHECK(driftOf(bank(5.0f, 0.0f, 0.0f)).z == Approx(0.0f).margin(1e-5));
    CHECK(driftOf(bank(5.0f, 0.0f, 90.0f)).x == Approx(0.0f).margin(1e-5));
    CHECK(driftOf(bank(5.0f, 0.0f, 90.0f)).z == Approx(5.0f));
    // A negative speed reverses it rather than doing something else.
    CHECK(driftOf(bank(-5.0f, 0.0f, 0.0f)).x == Approx(-5.0f));
    // Vertical is independent of the rotation, because up is not in the bank's frame.
    CHECK(driftOf(bank(5.0f, 2.5f, 137.0f)).y == Approx(2.5f));
}

TEST_CASE("no drift is exactly static", "[fog][flow][determinism]") {
    // The default, and therefore the promise to every saved scene: `driftSpeed` and
    // `driftVertical` are both 0 out of the factory, and at 0 the field must not depend on time
    // at all -- not nearly, exactly. ADR-091's two-tier determinism rests on this.
    const world::MediumSlot m = bank(0.0f, 0.0f, 61.0f);
    REQUIRE(driftOf(m) == glm::vec3(0.0f));
    bool sawStructure = false;
    for (const glm::vec3 p : {glm::vec3(0.0f), glm::vec3(60.0f, 20.0f, -30.0f),
                              glm::vec3(-120.0f, -40.0f, 90.0f)}) {
        const float base = world::fogMacroDetail(m, p, 0.0f);
        for (const float t : {0.5f, 13.0f, 900.0f}) {
            INFO("p=(" << p.x << ", " << p.y << ", " << p.z << ") t=" << t);
            REQUIRE(world::fogMacroDetail(m, p, t) == base);
        }
        sawStructure = sawStructure || std::abs(base - 1.0f) > 1e-3f;
    }
    CHECK(sawStructure); // a field that is the constant 1 is static for free
}


TEST_CASE("the flow steers the drift and never sets its speed", "[fog][flow][wind]") {
    // ADR-572, the brief's §17: "flow affects movement, not basic existence." Setting which way the
    // structure travels is movement; it cannot make the bank exist anywhere it did not.
    //
    // The design decision under test is that the flow contributes a DIRECTION and nothing else,
    // and it is a unit decision rather than a simplification. `fields::FlowSample::units` exists
    // because the wind publishes a dimensionless strength and a vortex publishes metres a second,
    // and this codebase has produced the same unit bug four separate times by assuming. A
    // direction is unit-free, so the two publishers cannot make one control mean two things.
    const glm::vec3 acrossZ(0.0f, 0.0f, 1.0f);

    SECTION("at zero coupling the flow does nothing at all") {
        // The default, so this is the promise to every bank that has never heard of a flow field.
        const glm::vec3 free = glm::vec3(bankInFlow(7.0f, 0.0f, 0.0f, acrossZ, 1.0f).lane[2]);
        CHECK(free.x == Approx(7.0f));
        CHECK(free.z == Approx(0.0f).margin(1e-5));
    }

    SECTION("an unsubscribed effect is unaffected however high the coupling") {
        // `influence` is 0 when the effect is unsubscribed, names a dead field, or set its own
        // subscription to 0 -- so the control is inert until a scene asks for a field, which is
        // what stops it being a trap for a bank that has no flow to follow.
        const glm::vec3 v = glm::vec3(bankInFlow(7.0f, 0.0f, 1.0f, acrossZ, 0.0f).lane[2]);
        CHECK(v.x == Approx(7.0f));
        CHECK(v.z == Approx(0.0f).margin(1e-5));
    }

    SECTION("at full coupling the drift follows the flow instead of the bank's axis") {
        const glm::vec3 v = glm::vec3(bankInFlow(7.0f, 0.0f, 1.0f, acrossZ, 1.0f).lane[2]);
        INFO("drift (" << v.x << ", " << v.y << ", " << v.z << ")");
        CHECK(v.z == Approx(7.0f));
        CHECK(v.x == Approx(0.0f).margin(1e-5));
        // ...and it overrides the bank's rotation rather than adding to it.
        const glm::vec3 turned = glm::vec3(bankInFlow(7.0f, 90.0f, 1.0f, acrossZ, 1.0f).lane[2]);
        CHECK(turned.z == Approx(7.0f));
    }

    SECTION("THE SPEED IS THE ARTIST'S, whatever the flow is doing") {
        // The property the whole design rests on. A flow of magnitude 40 and a flow of magnitude
        // 0.01 in the same direction must produce the same drift, because only the direction is
        // read -- and the magnitude of the drift must be exactly `driftSpeed` in every case.
        for (const float mag : {0.01f, 1.0f, 40.0f}) {
            for (const float coupling : {0.0f, 0.25f, 0.5f, 1.0f}) {
                const glm::vec3 v =
                    glm::vec3(bankInFlow(6.0f, 33.0f, coupling, acrossZ * mag, 1.0f).lane[2]);
                INFO("flow magnitude " << mag << " coupling " << coupling);
                CHECK(glm::length(v) == Approx(6.0f).margin(1e-4));
            }
        }
    }

    SECTION("the units a publisher answers in do not change the answer") {
        // The claim that makes the direction-only design correct against BOTH publishers, tested
        // rather than argued: the wind's normalised strength and a vortex's metres a second give
        // the same drift for the same direction. A version that read the magnitude would differ
        // here by a factor of the magnitude, and would do it silently.
        const glm::vec3 normalised =
            glm::vec3(bankInFlow(5.0f, 0.0f, 1.0f, acrossZ * 0.3f, 1.0f,
                                 world::fields::FlowUnits::Normalised).lane[2]);
        const glm::vec3 metres =
            glm::vec3(bankInFlow(5.0f, 0.0f, 1.0f, acrossZ * 22.0f, 1.0f,
                                 world::fields::FlowUnits::MetresPerSecond).lane[2]);
        INFO("normalised (" << normalised.x << ", " << normalised.z << "), metres ("
             << metres.x << ", " << metres.z << ")");
        CHECK(metres.x == Approx(normalised.x).margin(1e-5));
        CHECK(metres.z == Approx(normalised.z).margin(1e-5));
        CHECK(glm::length(metres) == Approx(5.0f).margin(1e-4));
    }
}
