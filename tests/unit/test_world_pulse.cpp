// ADR-2124: a Shockwave that travels through the world. Its fronts reach the frame block (`PulseFrame`) for the vertex
// stages, the surfaces and the clouds, and its DF proxies become world shells; the ring mode is untouched.

#include "world/effects/distortion_frame.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_trigger.hpp"
#include "world/effects/world_pulse.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

void setRow(world::EffectInstance& e, const char* leaf, float v) { e.values.setFloat(std::string("shockwave/") + leaf, v); }

world::EffectInstance wave(double period) {
    world::EffectInstance e = world::makeEffect(world::EffectKind::Shockwave, "wave");
    e.id = "wave";
    e.activation = world::Activation::Trigger;
    world::Trigger t;
    t.source = world::TriggerSource::Repeat;
    t.period = period;
    t.phase = 0.5;
    e.timing.trigger = t;
    setRow(e, "travel", 1.0f);
    setRow(e, "maxRadius", 400.0f);
    setRow(e, "duration", 2.5f);
    setRow(e, "ease", 3.0f); // linear: 160 m/s
    setRow(e, "thickness", 7.0f);
    setRow(e, "maxConcurrent", 8.0f);
    setRow(e, "push", 0.9f);
    setRow(e, "flash", 2.0f);
    setRow(e, "clouds", 1.0f);
    setRow(e, "refRadius", 25.0f);
    setRow(e, "radiusDecay", 0.5f);
    setRow(e, "offsetX", 10.0f);
    return e;
}

world::EffectContext context(double seconds) {
    static const world::TriggerClock kNone;
    world::EffectContext ctx;
    ctx.seconds = seconds;
    ctx.triggers = &kNone;
    return ctx;
}

world::PulseFrame pulses(const world::EffectInstance& e, double seconds) {
    world::PulseFrame out;
    const std::vector<world::EffectInstance> list{e};
    world::buildPulseFrame(list, context(seconds), out, {});
    return out;
}

world::DistortionFrame proxies(const world::EffectInstance& e, double seconds) {
    world::DistortionFrame out;
    const std::vector<world::EffectInstance> list{e};
    std::vector<world::EffectStatus> status(1);
    std::vector<std::string> reasons(1);
    world::buildDistortionFrame(list, context(seconds), out, {}, status, reasons);
    return out;
}

} // namespace

TEST_CASE("World pulse: fronts travel at maxRadius / duration, several in the air, newest first", "[adr2124]") {
    const world::EffectInstance e = wave(0.35);
    const world::PulseFrame f = pulses(e, 2.0); // released at 0.5, 0.85, 1.2, 1.55, 1.9
    REQUIRE(f.count == 5);
    CHECK(f.fronts[0].centreRadius.w == Approx(0.1f * 160.0f).margin(0.01f));
    CHECK(f.fronts[4].centreRadius.w == Approx(1.5f * 160.0f).margin(0.01f));
    CHECK(f.fronts[0].centreRadius.x == Approx(10.0f));
    for (std::uint32_t i = 0; i < f.count; ++i) {
        const world::PulseFront& p = f.fronts[i];
        CHECK(p.motion.x == Approx(p.centreRadius.w - 160.0f / 30.0f).margin(0.01f)); // a frame earlier
        CHECK(p.motion.y == Approx(7.0f));
        CHECK(p.motion.z > 0.0f);
        if (i > 0) {
            CHECK(p.motion.z <= f.fronts[i - 1].motion.z); // weaker as it spreads
        }
    }
    // the far front: (25 / 240)^0.5 of the push
    CHECK(f.fronts[4].motion.z == Approx(0.9f * std::sqrt(25.0f / 240.0f)).epsilon(0.02));
    // past the duration a front is gone; unused slots are zero
    CHECK(f.fronts[5].centreRadius == glm::vec4(0.0f));
}

TEST_CASE("World pulse: a pure function of the second (seek equals play)", "[adr2124]") {
    const world::EffectInstance e = wave(0.35);
    const world::PulseFrame a = pulses(e, 7.123);
    (void)pulses(e, 3.0);
    const world::PulseFrame b = pulses(e, 7.123);
    REQUIRE(a.count == b.count);
    for (std::uint32_t i = 0; i < a.count; ++i) {
        CHECK(a.fronts[i].centreRadius == b.fronts[i].centreRadius);
        CHECK(a.fronts[i].motion == b.fronts[i].motion);
    }
}

TEST_CASE("World pulse: the world mode draws shells; the ring mode draws no pulse", "[adr2124]") {
    world::EffectInstance e = wave(0.35);
    const world::DistortionFrame d = proxies(e, 2.0);
    REQUIRE(d.count == 5);
    CHECK(static_cast<int>(d.proxies[0].axis0.w + 0.5f) == static_cast<int>(world::DistortionShape::Shell));
    CHECK(static_cast<int>(d.proxies[0].axis1.w + 0.5f) == static_cast<int>(world::DistortionField::Front));
    CHECK(d.proxies[0].shape.x == Approx(16.0f).margin(0.01f));
    CHECK(d.proxies[0].shape.z == 0.0f); // the rect bound's turbulence term
    setRow(e, "travel", 0.0f);
    CHECK(pulses(e, 2.0).count == 0);
    const world::DistortionFrame ring = proxies(e, 2.0);
    REQUIRE(ring.count > 0);
    CHECK(static_cast<int>(ring.proxies[0].axis0.w + 0.5f) == static_cast<int>(world::DistortionShape::Ellipsoid));
    CHECK(ring.count <= 4); // the ring mode keeps its four
}

TEST_CASE("World pulse: a disabled or muted wave leaves the frame block empty", "[adr2124]") {
    world::EffectInstance e = wave(0.35);
    e.enabled = false;
    CHECK(pulses(e, 2.0).count == 0);
    CHECK(pulses(wave(0.35), 0.2).count == 0); // before the first release
}

TEST_CASE("World pulse: the range fade takes a front out past fadeEnd", "[adr2124]") {
    world::EffectInstance e = wave(0.35);
    setRow(e, "fadeStart", 100.0f);
    setRow(e, "fadeEnd", 200.0f);
    const world::PulseFrame f = pulses(e, 2.0); // fronts at 16, 72, 128, 184, 240 m
    REQUIRE(f.count >= 4);
    CHECK(f.fronts[0].motion.z == Approx(0.9f * std::sqrt(25.0f / 25.0f) * 1.0f).epsilon(0.05)); // 16 m: untouched
    CHECK(f.fronts[2].motion.z < 0.9f * std::sqrt(25.0f / 128.0f));                            // 128 m: fading
    for (std::uint32_t i = 0; i < f.count; ++i) {
        if (f.fronts[i].centreRadius.w >= 200.0f) {
            CHECK(f.fronts[i].motion.z == 0.0f);
            CHECK(f.fronts[i].flash == glm::vec4(0.0f));
        }
    }
}
