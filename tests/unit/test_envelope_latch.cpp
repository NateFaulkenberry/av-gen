// ADR-1574: a latching envelope. Once triggered it ignores its input for `envelopeRefractoryMs` (it plays its whole
// hold and fall), and with a finite `envelopeRearm` it cannot trigger again until the input has fallen to that level.
// Both off by default, where every chain behaves exactly as before.

#include "params/processor.hpp"
#include "params/serialization.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <functional>
#include <vector>

using namespace avgen::params;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kDt = 1.0 / 60.0;

ProcessorChain holdChain() {
    ProcessorChain c;
    c.envelope = EnvelopeMode::PeakHold;
    c.envelopeHoldMs = 1000.0f;
    c.envelopeFallPerSecond = 1.0f;
    return c;
}

// The chain's output every frame for `frames` frames of the input `in(frame)`.
std::vector<float> run(const ProcessorChain& c, int frames, const std::function<float(int)>& in) {
    ProcessorChain::State s;
    std::vector<float> out;
    for (int i = 0; i < frames; ++i) {
        out.push_back(c.process(in(i), false, kDt, s));
    }
    return out;
}

} // namespace

TEST_CASE("ADR-1574: off by default, an envelope re-triggers as it always has", "[modulation][adr1574]") {
    const ProcessorChain c = holdChain();
    CHECK_FALSE(c.envelopeLatches());
    // a second pulse 1.5 s in (in the fall) lifts the envelope back to 1
    const auto out = run(c, 240, [](int i) { return (i == 0 || i == 90) ? 1.0f : 0.0f; });
    CHECK_THAT(out[90], WithinAbs(1.0, 1e-6));
}

TEST_CASE("ADR-1574: a refractory envelope plays its whole hold and fall, whatever the input does", "[modulation][adr1574]") {
    ProcessorChain c = holdChain();
    c.envelopeRefractoryMs = 5000.0f;
    REQUIRE(c.envelopeLatches());
    const auto single = run(c, 240, [](int i) { return i == 0 ? 1.0f : 0.0f; });
    // pulses and dips inside the refractory time change nothing: the envelope is the single pulse's
    const auto busy = run(c, 240, [](int i) { return (i == 0 || i == 90 || (i > 100 && i < 140)) ? 1.0f : 0.0f; });
    for (std::size_t i = 0; i < single.size(); ++i) {
        CHECK_THAT(busy[i], WithinAbs(single[i], 1e-6));
    }
    // after the refractory time it can fire again
    const auto later = run(c, 400, [](int i) { return (i == 0 || i == 320) ? 1.0f : 0.0f; });
    CHECK_THAT(later[320], WithinAbs(1.0, 1e-6));
}

TEST_CASE("ADR-1574: with a rearm level, a held input triggers once, and again only on a new rising edge",
          "[modulation][adr1574]") {
    ProcessorChain c = holdChain();
    c.envelopeRefractoryMs = 100.0f;
    c.envelopeRearm = 0.2f;
    // high for 6 s: one trigger, then the hold (1 s) and the fall (1 s) run out to 0 while it stays high
    const auto held = run(c, 360, [](int) { return 1.0f; });
    CHECK_THAT(held[0], WithinAbs(1.0, 1e-6));
    CHECK_THAT(held[200], WithinAbs(0.0, 1e-6));
    CHECK_THAT(held[359], WithinAbs(0.0, 1e-6));
    // high, low (re-arms), high again: a second trigger
    const auto edges = run(c, 360, [](int i) { return i < 150 ? 1.0f : (i < 170 ? 0.0f : 1.0f); });
    CHECK_THAT(edges[170], WithinAbs(1.0, 1e-6));
}

TEST_CASE("ADR-1574: the fields round-trip, and are written only when used", "[modulation][adr1574]") {
    ProcessorChain c = holdChain();
    CHECK_FALSE(chainToJson(c).contains("envelopeRefractoryMs"));
    CHECK_FALSE(chainToJson(c).contains("envelopeRearm"));
    c.envelopeRefractoryMs = 20000.0f;
    c.envelopeRearm = 0.3f;
    const auto back = chainFromJson(chainToJson(c));
    REQUIRE(back.has_value());
    CHECK(back->envelopeRefractoryMs == 20000.0f);
    CHECK(back->envelopeRearm == 0.3f);
}

// ADR-1575: the trend stage -- the short-term average over the long-term one.
TEST_CASE("ADR-1575: the trend reads 1 on a steady input and above 1 on a swell, at any level", "[modulation][adr1575]") {
    ProcessorChain c;
    c.trendSeconds = 8.0f;
    c.trendFastMs = 1000.0f;
    for (const float level : {0.05f, 0.8f}) {
        ProcessorChain::State s;
        float y = 0.0f;
        for (int i = 0; i < 60 * 30; ++i) { // 30 s steady
            y = c.process(level, false, kDt, s);
        }
        CHECK_THAT(y, WithinAbs(1.0, 1e-3));
        for (int i = 0; i < 60 * 2; ++i) { // a swell to 1.5x for 2 s
            y = c.process(level * 1.5f, false, kDt, s);
        }
        CHECK(y > 1.2f);
    }
    // silence, then a small blip: the long-term average's floor bounds the ratio
    {
        ProcessorChain::State s;
        float y = 0.0f;
        for (int i = 0; i < 60 * 10; ++i) {
            y = c.process(0.0f, false, kDt, s);
        }
        CHECK_THAT(y, WithinAbs(0.0, 1e-6));
        float worst = 0.0f;
        for (int i = 0; i < 30; ++i) {
            worst = std::max(worst, c.process(0.004f, false, kDt, s));
        }
        CHECK(worst <= 0.004f / c.trendFloor + 1e-6f);
    }
    ProcessorChain off;
    CHECK_FALSE(chainToJson(off).contains("trendSeconds"));
    const auto back = chainFromJson(chainToJson(c));
    REQUIRE(back.has_value());
    CHECK(back->trendSeconds == 8.0f);
    CHECK(back->trendFastMs == 1000.0f);
}
