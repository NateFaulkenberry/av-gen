#include "params/processor.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <tuple>
#include <vector>

using namespace avgen::params;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {
double d(float v) {
    return static_cast<double>(v);
}

// Feeds `steps` samples of `x` through the chain at a fixed dt and returns the last output.
float run(const ProcessorChain& chain, ProcessorChain::State& state, float x, double dt, int steps,
          bool event = false) {
    float y = 0.0f;
    for (int i = 0; i < steps; ++i) {
        y = chain.process(x, event, dt, state);
    }
    return y;
}
} // namespace

TEST_CASE("Default chain is an identity", "[processor]") {
    ProcessorChain chain;
    ProcessorChain::State state;
    CHECK(chain.process(0.37f, false, 1.0 / 60.0, state) == 0.37f);
    CHECK(chain.process(-2.0f, false, 1.0 / 60.0, state) == -2.0f);
}

TEST_CASE("Gain and offset stages", "[processor]") {
    ProcessorChain chain;
    chain.gain = 2.0f;
    chain.offset = 0.25f;
    ProcessorChain::State state;
    CHECK_THAT(d(chain.process(0.5f, false, 0.016, state)), WithinAbs(1.25, 1e-6));
}

TEST_CASE("Every curve maps 0 to 0 and 1 to 1 and is monotonic on [0, 1]", "[processor][curve]") {
    const auto curve =
        GENERATE(CurveType::Linear, CurveType::Power, CurveType::Log, CurveType::Exp, CurveType::SCurve);
    const auto amount = GENERATE(0.5f, 1.0f, 2.0f, 4.0f);
    INFO("curve " << static_cast<int>(curve) << " amount " << amount);
    CHECK_THAT(d(applyCurve(0.0f, curve, amount)), WithinAbs(0.0, 1e-6));
    CHECK_THAT(d(applyCurve(1.0f, curve, amount)), WithinAbs(1.0, 1e-5));
    float previous = applyCurve(0.0f, curve, amount);
    for (int i = 1; i <= 100; ++i) {
        const float x = static_cast<float>(i) / 100.0f;
        const float y = applyCurve(x, curve, amount);
        CHECK(y >= previous);
        CHECK(y >= -1e-6f);
        CHECK(y <= 1.0f + 1e-5f);
        previous = y;
    }
}

TEST_CASE("Curve shapes are the documented functions", "[processor][curve]") {
    CHECK_THAT(d(applyCurve(0.25f, CurveType::Power, 2.0f)), WithinAbs(0.0625, 1e-6));
    CHECK_THAT(d(applyCurve(0.25f, CurveType::Power, 0.5f)), WithinAbs(0.5, 1e-6));
    CHECK_THAT(d(applyCurve(-0.25f, CurveType::Power, 2.0f)), WithinAbs(-0.0625, 1e-6)); // sign preserved
    CHECK_THAT(d(applyCurve(0.5f, CurveType::Log, 9.0f)), WithinAbs(std::log1p(4.5) / std::log1p(9.0), 1e-6));
    CHECK_THAT(d(applyCurve(0.5f, CurveType::Exp, 2.0f)),
               WithinAbs((std::exp(1.0) - 1.0) / (std::exp(2.0) - 1.0), 1e-6));
    CHECK_THAT(d(applyCurve(0.5f, CurveType::SCurve, 6.0f)), WithinAbs(0.5, 1e-6)); // centred
    CHECK(applyCurve(0.25f, CurveType::SCurve, 8.0f) < 0.25f); // steep: pushes ends apart
    CHECK(applyCurve(0.75f, CurveType::SCurve, 8.0f) > 0.75f);
    // Non-positive amounts pass through instead of dividing by zero.
    CHECK(applyCurve(0.3f, CurveType::Log, 0.0f) == 0.3f);
    CHECK(applyCurve(0.3f, CurveType::Exp, -1.0f) == 0.3f);
    CHECK(applyCurve(0.3f, CurveType::SCurve, 0.0f) == 0.3f);
    CHECK(applyCurve(0.3f, CurveType::Power, 0.0f) == 0.3f);
}

TEST_CASE("Clamp stage", "[processor]") {
    ProcessorChain chain;
    chain.clampEnabled = true;
    chain.clampMin = 0.2f;
    chain.clampMax = 0.8f;
    ProcessorChain::State state;
    CHECK(chain.process(0.1f, false, 0.01, state) == 0.2f);
    CHECK(chain.process(0.5f, false, 0.01, state) == 0.5f);
    CHECK(chain.process(2.0f, false, 0.01, state) == 0.8f);
    chain.clampEnabled = false;
    CHECK(chain.process(2.0f, false, 0.01, state) == 2.0f);
}

TEST_CASE("Threshold modes", "[processor][threshold]") {
    ProcessorChain chain;
    chain.thresholdLevel = 0.5f;
    ProcessorChain::State state;

    SECTION("gate passes values at or above the level") {
        chain.threshold = ThresholdMode::Gate;
        CHECK(chain.process(0.49f, false, 0.01, state) == 0.0f);
        CHECK(chain.process(0.5f, false, 0.01, state) == 0.5f);
        CHECK(chain.process(0.9f, false, 0.01, state) == 0.9f);
    }
    SECTION("binary emits 0 or 1") {
        chain.threshold = ThresholdMode::Binary;
        CHECK(chain.process(0.49f, false, 0.01, state) == 0.0f);
        CHECK(chain.process(0.5f, false, 0.01, state) == 1.0f);
        CHECK(chain.process(3.0f, false, 0.01, state) == 1.0f);
    }
    SECTION("subtract rescales the remainder to 0..1") {
        chain.threshold = ThresholdMode::Subtract;
        CHECK(chain.process(0.3f, false, 0.01, state) == 0.0f);
        CHECK_THAT(d(chain.process(0.75f, false, 0.01, state)), WithinAbs(0.5, 1e-6));
        CHECK_THAT(d(chain.process(1.0f, false, 0.01, state)), WithinAbs(1.0, 1e-6));
    }
    SECTION("subtract guards a level of 1 or more") {
        chain.threshold = ThresholdMode::Subtract;
        chain.thresholdLevel = 1.0f;
        CHECK(std::isfinite(chain.process(0.5f, false, 0.01, state)));
        CHECK(std::isfinite(chain.process(1.5f, false, 0.01, state)));
    }
}

TEST_CASE("smoothingCoefficient", "[processor][smoothing]") {
    CHECK(smoothingCoefficient(0.0f, 0.016) == 1.0f);
    CHECK(smoothingCoefficient(-5.0f, 0.016) == 1.0f);
    CHECK_THAT(d(smoothingCoefficient(100.0f, 0.1)), WithinAbs(1.0 - std::exp(-1.0), 1e-6));
    CHECK_THAT(d(smoothingCoefficient(1000.0f, 0.0)), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Smoothing reaches 63.2% of a step after one time constant", "[processor][smoothing]") {
    ProcessorChain chain;
    chain.attackMs = 100.0f;
    chain.decayMs = 100.0f;
    ProcessorChain::State state;
    const double dt = 1.0 / 60.0;
    std::ignore = chain.process(0.0f, false, dt, state); // initialise at rest
    // 100 ms = 6 frames at 60 fps.
    const float y = run(chain, state, 1.0f, 0.1 / 6.0, 6);
    CHECK_THAT(d(y), WithinRel(1.0 - std::exp(-1.0), 0.02));
}

TEST_CASE("Smoothing is frame-rate independent", "[processor][smoothing]") {
    ProcessorChain chain;
    chain.attackMs = 120.0f;
    chain.decayMs = 300.0f;
    const double tau = 0.12;

    auto at3tau = [&](double fps) {
        ProcessorChain::State state;
        std::ignore = chain.process(0.0f, false, 1.0 / fps, state);
        const int steps = static_cast<int>(std::lround(3.0 * tau * fps));
        return run(chain, state, 1.0f, 1.0 / fps, steps);
    };
    const float y60 = at3tau(60.0);
    const float y240 = at3tau(240.0);
    CHECK_THAT(d(y60), WithinRel(1.0 - std::exp(-3.0), 0.03));
    CHECK_THAT(d(y240), WithinRel(d(y60), 0.03));
}

TEST_CASE("Smoothing is asymmetric: attack faster than decay", "[processor][smoothing]") {
    ProcessorChain chain;
    chain.attackMs = 10.0f;
    chain.decayMs = 500.0f;
    ProcessorChain::State state;
    const double dt = 1.0 / 60.0;
    std::ignore = chain.process(0.0f, false, dt, state);
    const float afterRise = run(chain, state, 1.0f, dt, 6); // 100 ms
    CHECK(afterRise > 0.99f);
    const float afterFall = run(chain, state, 0.0f, dt, 6); // 100 ms
    CHECK(afterFall > 0.7f);                                // slow decay: still mostly up
    CHECK(afterFall < afterRise);
}

TEST_CASE("First sample initialises the smoother without a ramp", "[processor][smoothing]") {
    ProcessorChain chain;
    chain.attackMs = 1000.0f;
    ProcessorChain::State state;
    CHECK(chain.process(0.8f, false, 0.016, state) == 0.8f);
    CHECK(state.initialised);
}

TEST_CASE("PeakHold envelope holds then falls linearly", "[processor][envelope]") {
    ProcessorChain chain;
    chain.envelope = EnvelopeMode::PeakHold;
    chain.envelopeHoldMs = 100.0f;
    chain.envelopeFallPerSecond = 4.0f;
    ProcessorChain::State state;
    const double dt = 0.01;

    CHECK(chain.process(1.0f, true, dt, state) == 1.0f);
    // Held for ~100 ms (9 frames well inside the hold window).
    for (int i = 0; i < 9; ++i) {
        CHECK(chain.process(0.0f, false, dt, state) == 1.0f);
    }
    // Let the hold expire (a frame or two of slack for float accumulation), then fall at 4/s.
    float y = 1.0f;
    for (int i = 0; i < 3 && y >= 1.0f; ++i) {
        y = chain.process(0.0f, false, dt, state);
    }
    REQUIRE(y < 1.0f);
    const float step = y;
    const float next = chain.process(0.0f, false, dt, state);
    CHECK_THAT(d(step - next), WithinAbs(0.04, 1e-5));
    // Hits the floor and stays there.
    const float floor = run(chain, state, 0.0f, dt, 100);
    CHECK(floor == 0.0f);

    // A weaker event while the envelope is high retriggers the hold without lowering it.
    std::ignore = chain.process(1.0f, true, dt, state);
    run(chain, state, 0.0f, dt, 20);
    const float before = chain.process(0.0f, false, dt, state);
    std::ignore = chain.process(0.2f, true, dt, state);
    CHECK(chain.process(0.0f, false, dt, state) >= before);
}

TEST_CASE("LinearFall envelope falls immediately", "[processor][envelope]") {
    ProcessorChain chain;
    chain.envelope = EnvelopeMode::LinearFall;
    chain.envelopeFallPerSecond = 2.0f;
    ProcessorChain::State state;
    const double dt = 0.05;
    CHECK(chain.process(1.0f, true, dt, state) == 1.0f);
    CHECK_THAT(d(chain.process(0.0f, false, dt, state)), WithinAbs(0.9, 1e-6));
    CHECK_THAT(d(chain.process(0.0f, false, dt, state)), WithinAbs(0.8, 1e-6));
    // A larger continuous value re-peaks even without an event.
    CHECK_THAT(d(chain.process(0.95f, false, dt, state)), WithinAbs(0.95, 1e-6));
    CHECK(run(chain, state, 0.0f, dt, 50) == 0.0f);
}

TEST_CASE("Envelope None passes the smoothed signal through", "[processor][envelope]") {
    ProcessorChain chain;
    ProcessorChain::State state;
    CHECK(chain.process(0.3f, true, 0.01, state) == 0.3f);
    CHECK(chain.process(0.0f, false, 0.01, state) == 0.0f);
}

TEST_CASE("Remap maps linearly without clamping", "[processor][remap]") {
    ProcessorChain chain;
    chain.remapEnabled = true;
    chain.remapInMin = 0.0f;
    chain.remapInMax = 2.0f;
    chain.remapOutMin = 10.0f;
    chain.remapOutMax = 20.0f;
    ProcessorChain::State state;
    CHECK_THAT(d(chain.process(1.0f, false, 0.01, state)), WithinAbs(15.0, 1e-5));
    CHECK_THAT(d(chain.process(3.0f, false, 0.01, state)), WithinAbs(25.0, 1e-5)); // no clamp
    CHECK_THAT(d(chain.process(-1.0f, false, 0.01, state)), WithinAbs(5.0, 1e-5));
    // Inverted output range.
    chain.remapOutMin = 1.0f;
    chain.remapOutMax = 0.0f;
    CHECK_THAT(d(chain.process(0.5f, false, 0.01, state)), WithinAbs(0.75, 1e-5));
    // Degenerate input range does not divide by zero.
    chain.remapInMax = chain.remapInMin;
    CHECK(std::isfinite(chain.process(0.5f, false, 0.01, state)));
}

TEST_CASE("Stages run in the fixed order", "[processor]") {
    // gain(2) -> offset(-0.5) -> clamp[0,1] -> binary threshold at 0.5 -> remap to 0..10
    ProcessorChain chain;
    chain.gain = 2.0f;
    chain.offset = -0.5f;
    chain.clampEnabled = true;
    chain.threshold = ThresholdMode::Binary;
    chain.thresholdLevel = 0.5f;
    chain.remapEnabled = true;
    chain.remapOutMax = 10.0f;
    ProcessorChain::State state;
    CHECK(chain.process(0.4f, false, 0.01, state) == 0.0f);  // 0.3 < 0.5
    CHECK(chain.process(0.5f, false, 0.01, state) == 10.0f); // 0.5 -> 1 -> 10
    CHECK(chain.process(5.0f, false, 0.01, state) == 10.0f); // clamped before threshold
}
