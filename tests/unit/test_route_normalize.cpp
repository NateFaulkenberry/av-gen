// ADR-1182: a route can normalise its signal to its own running peak.
#include "params/processor.hpp"
#include "params/serialization.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace avgen;
using namespace avgen::params;
using Catch::Matchers::WithinAbs;

namespace {
float run(const ProcessorChain& c, ProcessorChain::State& s, float x, double seconds, double dt = 1.0 / 60.0) {
    float y = 0.0f;
    for (double t = 0.0; t < seconds - 1e-9; t += dt) {
        y = c.process(x, false, dt, s);
    }
    return y;
}
} // namespace

TEST_CASE("normalise: a quieter section after a loud one is heard against the peak it left", "[routes][adr1182]") {
    ProcessorChain c;
    c.normalizeSeconds = 10.0f;
    ProcessorChain::State s;
    CHECK_THAT(run(c, s, 1.0f, 5.0), WithinAbs(1.0, 1e-5));      // the loud drop sets the peak
    const float justAfter = run(c, s, 0.6f, 2.0);                 // past the level's 250 ms smoothing
    CHECK_THAT(justAfter, WithinAbs(0.6 / std::exp(-2.0 / 10.0), 0.03)); // a quieter one, heard against the peak...
    const float later = run(c, s, 0.6f, 6.0);
    CHECK(later > 0.95f);                                          // ...and fully once the peak has fallen to it
    CHECK(later <= 1.0f + 1e-5f);

    // The control: off, the chain passes the signal through.
    ProcessorChain off;
    ProcessorChain::State s2;
    run(off, s2, 1.0f, 5.0);
    CHECK_THAT(run(off, s2, 0.6f, 8.0), WithinAbs(0.6, 1e-6));
}

TEST_CASE("normalise: silence is not amplified past the floor", "[routes][adr1182]") {
    ProcessorChain c;
    c.normalizeSeconds = 1.0f;
    c.normalizeFloor = 0.2f;
    ProcessorChain::State s;
    CHECK_THAT(run(c, s, 0.02f, 30.0), WithinAbs(0.1, 1e-4));    // 0.02 / 0.2, not 1
}

TEST_CASE("normalise reads the level, so a train of kicks is one steady level", "[routes][adr1182]") {
    ProcessorChain c;
    c.normalizeSeconds = 20.0f;
    c.normalizeSmoothMs = 500.0f;
    ProcessorChain::State s;
    // A kick every 0.5 s: 1 for one frame, 0.1 between. Raw, the drop is mostly 0.1 of its peak.
    float y = 0.0f;
    float lo = 1.0f;
    for (int f = 0; f < 60 * 20; ++f) {
        y = c.process(f % 30 == 0 ? 1.0f : 0.1f, false, 1.0 / 60.0, s);
        if (f > 60 * 10) {
            lo = std::min(lo, y);
        }
    }
    CHECK(lo > 0.6f); // the level of a steady train sits near its own peak
}

TEST_CASE("normalise round-trips, is absent when off, and is refused when negative", "[routes][adr1182]") {
    ProcessorChain c;
    CHECK_FALSE(chainToJson(c).contains("normalizeSeconds"));
    c.normalizeSeconds = 12.0f;
    c.normalizeFloor = 0.1f;
    auto back = chainFromJson(chainToJson(c));
    REQUIRE(back.has_value());
    CHECK(back->normalizeSeconds == 12.0f);
    CHECK(back->normalizeFloor == 0.1f);
    auto j = chainToJson(c);
    j["normalizeSeconds"] = -1.0;
    CHECK_FALSE(chainFromJson(j).has_value());
}
