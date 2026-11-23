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

// msgs 218/222/224: the newest release must not re-fade the fronts already in flight. With the Timing defaults a
// scene file gets (fadeIn 0.35 s) the instance envelope used to restart at every release: the frame after each
// quarter note drew NO front and the whole wave pumped on the beat.
TEST_CASE("World pulse: fronts on the quarter grid keep their strength through each new release", "[adr2124]") {
    const double beat = 0.35097, k0 = 0.0021;
    world::EffectInstance e = wave(beat);
    e.timing.trigger.phase = k0;
    e.timing.fadeIn = 0.35; // what a scene file without a "timing" block gets
    e.timing.fadeOut = 0.9;
    for (const double t : {10.0 * beat + k0 + 0.001, 10.0 * beat + k0 + 0.17, 10.0 * beat + k0 + 0.34}) {
        INFO("t = " << t);
        const world::PulseFrame f = pulses(e, t);
        REQUIRE(f.count >= 7);
        // every front's release time is on the grid: R / 160 m/s = t - (k0 + n beat)
        for (std::uint32_t i = 0; i < f.count; ++i) {
            const double age = f.fronts[i].centreRadius.w / 160.0;
            const double n = (t - age - k0) / beat;
            CHECK(std::abs(n - std::round(n)) < 1e-3);
            if (i > 1) { // (the newest is still in its own fade-in)
                CHECK(f.fronts[i].motion.z < f.fronts[i - 1].motion.z); // weaker the further out
            }
        }
        // a front 128 m out has (25 / r)^0.5 of the push, whatever the newest release's age (only the oldest fades)
        const world::PulseFront& mid = f.fronts[2];
        CHECK(mid.motion.z == Approx(0.9f * std::sqrt(25.0f / mid.centreRadius.w) *
                                     (1.0f - 0.0f)).epsilon(0.12));
    }
}

// ADR-2126 (Neon Dreams msg 253: "the shockwaves still feel like they're jerking the camera"): the lens calm reaches
// the shaders in the documented lanes, and every lane is 0 -- the ADR-2124 picture -- when nothing asks for it.
TEST_CASE("World pulse: the lens calm reaches the shell, the ring and the push; off by default", "[adr2126]") {
    world::EffectInstance e = wave(0.35);
    {
        const world::DistortionFrame d = proxies(e, 2.0);
        REQUIRE(d.count > 0);
        CHECK(d.proxies[0].motion == glm::vec4(0.0f));
        const world::PulseFrame f = pulses(e, 2.0);
        REQUIRE(f.count > 0);
        CHECK(f.fronts[0].flash.w == 0.0f);
        CHECK(f.fronts[0].shape.w == 0.0f);
    }
    setRow(e, "maxShift", 1.5f);
    setRow(e, "lensNear", 8.0f);
    setRow(e, "lensClear", 20.0f);
    const world::DistortionFrame d = proxies(e, 2.0);
    REQUIRE(d.count > 0);
    CHECK(d.proxies[0].motion.x == Approx(8.0f));  // calm near the lens (m)
    CHECK(d.proxies[0].motion.y == Approx(20.0f)); // clear of the lens (m)
    CHECK(d.proxies[0].motion.z == Approx(1.5f));  // the shift cap (px at 1080p)
    const world::PulseFrame f = pulses(e, 2.0);
    REQUIRE(f.count > 0);
    CHECK(f.fronts[0].flash.w == Approx(1.5f)); // the push's on-screen cap
    CHECK(f.fronts[0].shape.w == Approx(8.0f)); // no push near the lens
    setRow(e, "travel", 0.0f);                  // the ring: the calm in motion.xy, the cap in rim.w
    const world::DistortionFrame ring = proxies(e, 2.0);
    REQUIRE(ring.count > 0);
    CHECK(ring.proxies[0].motion.x == Approx(8.0f));
    CHECK(ring.proxies[0].motion.y == Approx(20.0f));
    CHECK(ring.proxies[0].rim.w == Approx(1.5f));
}

TEST_CASE("World pulse: a front grows in over rampIn and the source clearance reaches the block and the shell; off by default",
          "[adr2128]") {
    world::EffectInstance e = wave(10.0); // one release, at 0.5 s
    setRow(e, "refRadius", 1000.0f);      // no weakening with size: the ramp alone
    {
        const world::PulseFrame f = pulses(e, 0.5 + 0.1);
        REQUIRE(f.count == 1);
        CHECK(f.sourceClear == 0.0f);
        CHECK(f.fronts[0].motion.z == Approx(0.9f).margin(1e-3)); // the original: full within 2 % of the life
        const world::DistortionFrame d = proxies(e, 0.6);
        REQUIRE(d.count > 0);
        CHECK(d.proxies[0].motion.w == 0.0f);
    }
    setRow(e, "rampIn", 0.6f);
    setRow(e, "sourceClear", 30.0f);
    // eased in: a third of the way through the ramp it is under a third of the push, then full, never a step
    float last = 0.0f;
    for (int i = 1; i <= 24; ++i) {
        const double age = 0.025 * i;
        const world::PulseFrame f = pulses(e, 0.5 + age);
        REQUIRE(f.count == 1);
        const float push = f.fronts[0].motion.z;
        CHECK(push >= last - 1e-6f);
        CHECK(push - last < 0.12f); // no frame-sized jump (0.025 s steps; full is 0.9)
        last = push;
    }
    CHECK(last == Approx(0.9f).margin(1e-3));
    CHECK(pulses(e, 0.5 + 0.2).fronts[0].motion.z < 0.3f * 0.9f);
    const world::PulseFrame f = pulses(e, 1.0);
    CHECK(f.sourceClear == Approx(30.0f));
    const world::DistortionFrame d = proxies(e, 1.0);
    REQUIRE(d.count > 0);
    CHECK(d.proxies[0].motion.w == Approx(30.0f));
}
