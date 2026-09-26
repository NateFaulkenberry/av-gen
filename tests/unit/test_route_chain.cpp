// ADR-900: the route chain -- a delay stage, events held through their attack, depth from a signal,
// and timeline sources that score events. Every case that proves a stage reaches its output carries
// a control that shows the same measurement without the stage (or under the old rule) failing it.

#include "params/modulation.hpp"
#include "params/processor.hpp"
#include "params/serialization.hpp"
#include "signals/signal_bus.hpp"
#include "signals/source.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <vector>

using namespace avgen;
using namespace avgen::params;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace {

double d(float v) {
    return static_cast<double>(v);
}

// Feeds a one-frame event at frame `eventFrame` (strength 1) through `chain` at `fps` and returns the
// output of every frame; frame 0 has dt = 0, as a play's first frame does.
std::vector<float> eventTrace(const ProcessorChain& chain, double fps, int frames, int eventFrame) {
    ProcessorChain::State state;
    std::vector<float> out;
    for (int f = 0; f < frames; ++f) {
        const bool event = f == eventFrame;
        out.push_back(chain.process(event ? 1.0f : 0.0f, event, f == 0 ? 0.0 : 1.0 / fps, state));
    }
    return out;
}

int firstFrameAbove(const std::vector<float>& trace, float level) {
    for (std::size_t i = 0; i < trace.size(); ++i) {
        if (trace[i] > level) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

float peak(const std::vector<float>& trace) {
    float p = 0.0f;
    for (const float v : trace) {
        p = std::max(p, v);
    }
    return p;
}

// The pre-ADR-900 smoothing stage, verbatim, for the controls: a one-pole on every sample, events
// included.
float oldSmoothingPeak(float attackMs, float decayMs, double fps) {
    float smoothed = 0.0f;
    float best = 0.0f;
    for (int f = 1; f < 600; ++f) {
        const float y = f == 30 ? 1.0f : 0.0f;
        const float c = y > smoothed ? smoothingCoefficient(attackMs, 1.0 / fps) : smoothingCoefficient(decayMs, 1.0 / fps);
        smoothed = c >= 1.0f ? y : smoothed + (y - smoothed) * c;
        best = std::max(best, smoothed);
    }
    return best;
}

} // namespace

// ---- the delay stage ---------------------------------------------------------------------------

TEST_CASE("A 100 ms delay lands an event on frame 6 at 60 fps and frame 3 at 30 fps", "[processor][delay][adr900]") {
    ProcessorChain chain;
    chain.delayMs = 100.0f;
    // The event at frame 30 (0.5 s) of 60 fps lands at frame 36; at 30 fps, frame 15 lands at 18.
    const auto at60 = eventTrace(chain, 60.0, 90, 30);
    const auto at30 = eventTrace(chain, 30.0, 45, 15);
    CHECK(firstFrameAbove(at60, 0.5f) == 36);
    CHECK(firstFrameAbove(at30, 0.5f) == 18);
    // Still a one-frame event: exactly one frame carries it.
    CHECK(std::count_if(at60.begin(), at60.end(), [](float v) { return v > 0.5f; }) == 1);
    CHECK(std::count_if(at30.begin(), at30.end(), [](float v) { return v > 0.5f; }) == 1);

    // Control: without the stage the same event lands on its own frame.
    ProcessorChain none;
    CHECK(firstFrameAbove(eventTrace(none, 60.0, 90, 30), 0.5f) == 30);
    CHECK(firstFrameAbove(eventTrace(none, 30.0, 45, 15), 0.5f) == 15);
}

TEST_CASE("The delay stage carries the event flag and strength to the stages after it", "[processor][delay][adr900]") {
    ProcessorChain chain;
    chain.delayMs = 50.0f;
    ProcessorChain::State s;
    const double dt = 1.0 / 60.0;
    std::ignore = chain.delay(0.0f, false, 0.0, s);
    std::ignore = chain.delay(0.8f, true, dt, s); // t = 1/60
    CHECK_FALSE(chain.delay(0.0f, false, dt, s).event);
    CHECK_FALSE(chain.delay(0.0f, false, dt, s).event); // t = 3/60: the delayed instant is 0
    const ProcessorChain::Delayed delayed = chain.delay(0.0f, false, dt, s); // 4/60 -> 1/60
    CHECK(delayed.event);
    CHECK(delayed.value == 0.8f);
    // After it lands, the held value of an event signal is 0 again, as the bus's is.
    const ProcessorChain::Delayed after = chain.delay(0.0f, false, dt, s);
    CHECK_FALSE(after.event);
    CHECK(after.value == 0.0f);
}

TEST_CASE("A delayed continuous signal is the signal as it was delayMs earlier, at any frame rate",
          "[processor][delay][adr900]") {
    ProcessorChain chain;
    chain.delayMs = 250.0f;
    for (const double fps : {30.0, 60.0, 120.0}) {
        INFO("fps " << fps);
        ProcessorChain::State state;
        const int frames = static_cast<int>(2.0 * fps);
        for (int f = 0; f <= frames; ++f) {
            const double t = static_cast<double>(f) / fps;
            const float x = static_cast<float>(t); // a ramp: the value is the time it was sampled at
            const float y = chain.process(x, false, f == 0 ? 0.0 : 1.0 / fps, state);
            const double expected = std::max(0.0, t - 0.25); // before the first sample: the first holds
            CHECK_THAT(d(y), WithinAbs(expected, 1e-4));
        }
    }
}

TEST_CASE("A repeated instant re-emits what it emitted, so a landing frame keeps its event",
          "[processor][delay][adr900]") {
    ProcessorChain chain;
    chain.delayMs = 50.0f;
    ProcessorChain::State state;
    const double dt = 1.0 / 60.0;
    std::ignore = chain.delay(0.0f, false, 0.0, state);
    std::ignore = chain.delay(1.0f, true, dt, state); // the event, at 1/60
    for (int f = 2; f <= 3; ++f) {
        std::ignore = chain.delay(0.0f, false, dt, state);
    }
    const auto lands = chain.delay(0.0f, false, dt, state); // 4/60: its delayed instant is 1/60
    REQUIRE(lands.event);
    // The same instant again (dt = 0), as a seek's landing frame after its replay's step is.
    const auto again = chain.delay(0.0f, false, 0.0, state);
    CHECK(again.event);
    CHECK(again.value == lands.value);
    // The next instant moves on: the event is not emitted a third time.
    CHECK_FALSE(chain.delay(0.0f, false, dt, state).event);
}

TEST_CASE("The delay stage is clamped to its ceiling and keeps a bounded history", "[processor][delay][adr900]") {
    ProcessorChain chain;
    chain.delayMs = 60000.0f;
    CHECK(chain.delaySeconds() == Catch::Approx(static_cast<double>(ProcessorChain::kMaxDelayMs) / 1000.0));
    ProcessorChain::State state;
    for (int f = 0; f < 60 * 60; ++f) { // a minute at 60 fps
        std::ignore = chain.process(1.0f, false, f == 0 ? 0.0 : 1.0 / 60.0, state);
    }
    // At most the ceiling's worth of samples is live, and the buffer is compacted as it goes.
    CHECK(state.history.size() - state.historyHead <= static_cast<std::size_t>(4.0 * 60.0) + 2);
    CHECK(state.history.size() <= 2 * (static_cast<std::size_t>(4.0 * 60.0) + 2) + 64);
}

// ---- events held through their attack ----------------------------------------------------------

TEST_CASE("A one-frame event reaches its full amount through attack and decay", "[processor][smoothing][adr900]") {
    struct Case {
        float attack;
        float decay;
    };
    // The GV2 multicam's own settings: beat pulses, the elder's downbeat, the build, the section.
    for (const Case c : {Case{10.0f, 260.0f}, Case{60.0f, 900.0f}, Case{250.0f, 1800.0f}, Case{2400.0f, 2800.0f},
                         Case{2200.0f, 6000.0f}}) {
        for (const double fps : {30.0, 60.0, 120.0}) {
            INFO("attack " << c.attack << " ms, decay " << c.decay << " ms, " << fps << " fps");
            ProcessorChain chain;
            chain.attackMs = c.attack;
            chain.decayMs = c.decay;
            const auto trace = eventTrace(chain, fps, static_cast<int>(fps * 10.0), 3);
            // In full: the frame the rise completes in shows the whole level, at every frame rate.
            CHECK(peak(trace) > 1.0f - 1e-5f);
            CHECK(peak(trace) <= 1.0f + 1e-6f);
            // Control: the old stage smoothed the event like any sample and let a fraction through.
            if (c.attack >= 60.0f) {
                CHECK(oldSmoothingPeak(c.attack, c.decay, fps) < 0.5f);
            }
        }
    }
}

TEST_CASE("An event rises in a straight line over attackMs from the start of its frame",
          "[processor][smoothing][adr900]") {
    ProcessorChain chain;
    chain.attackMs = 100.0f; // six frames at 60 fps
    chain.decayMs = 1000.0f;
    const auto trace = eventTrace(chain, 60.0, 30, 10);
    for (int k = 0; k < 6; ++k) {
        INFO("frame " << 10 + k);
        CHECK_THAT(d(trace[static_cast<std::size_t>(10 + k)]), WithinAbs((k + 1) / 6.0, 1e-5));
    }
    // Then the decay: one frame of it after the peak at frame 15.
    CHECK_THAT(d(trace[16]), WithinAbs(std::exp(-1.0 / 60.0), 1e-5));
    // At 120 fps the same rise takes twelve frames, from the start of the event's own frame. (Which
    // frame an event arrives on is the frame grid's -- that sub-frame quantisation is the bus's, and
    // the rise is credited from the start of the interval the event arrived in.)
    const auto fine = eventTrace(chain, 120.0, 60, 20);
    for (int j = 0; j < 12; ++j) {
        CHECK_THAT(d(fine[static_cast<std::size_t>(20 + j)]), WithinAbs((j + 1) / 12.0, 1e-5));
    }
}

TEST_CASE("A stronger event re-aims a rise and a weaker one leaves it alone", "[processor][smoothing][adr900]") {
    ProcessorChain chain;
    chain.attackMs = 100.0f;
    chain.decayMs = 1000.0f;
    ProcessorChain::State state;
    const double dt = 1.0 / 60.0;
    std::ignore = chain.process(0.0f, false, 0.0, state);
    std::ignore = chain.process(0.5f, true, dt, state); // rising to 0.5
    std::ignore = chain.process(0.2f, true, dt, state); // weaker: ignored
    CHECK(state.eventTarget == 0.5f);
    std::ignore = chain.process(1.0f, true, dt, state); // stronger: rises to 1 from where it is
    CHECK(state.eventTarget == 1.0f);
    float y = 0.0f;
    for (int f = 0; f < 6; ++f) {
        y = chain.process(0.0f, false, dt, state);
    }
    CHECK(y > 0.97f);
}

TEST_CASE("A continuous signal, and an event on an edge with no time, are smoothed exactly as before",
          "[processor][smoothing][adr900]") {
    // The old stage, sample for sample.
    const auto old = [](float attack, float decay, const std::vector<float>& xs, double dt) {
        std::vector<float> out;
        float s = xs.front();
        out.push_back(s);
        for (std::size_t i = 1; i < xs.size(); ++i) {
            const float c = xs[i] > s ? smoothingCoefficient(attack, dt) : smoothingCoefficient(decay, dt);
            s = c >= 1.0f ? xs[i] : s + (xs[i] - s) * c;
            out.push_back(s);
        }
        return out;
    };
    std::vector<float> xs;
    for (int i = 0; i < 240; ++i) {
        xs.push_back(0.5f + 0.5f * std::sin(0.13f * static_cast<float>(i)) * ((i % 17) == 0 ? 2.0f : 1.0f));
    }
    const double dt = 1.0 / 60.0;
    SECTION("no event flags: every attack and decay as before") {
        for (const float attack : {0.0f, 15.0f, 120.0f}) {
            ProcessorChain chain;
            chain.attackMs = attack;
            chain.decayMs = 300.0f;
            ProcessorChain::State state;
            const auto expected = old(attack, 300.0f, xs, dt);
            for (std::size_t i = 0; i < xs.size(); ++i) {
                CHECK(chain.process(xs[i], false, i == 0 ? 0.0 : dt, state) == expected[i]);
            }
        }
    }
    SECTION("events with no attack and no decay: at once, as before") {
        ProcessorChain chain;
        ProcessorChain::State state;
        const auto expected = old(0.0f, 0.0f, xs, dt);
        for (std::size_t i = 0; i < xs.size(); ++i) {
            CHECK(chain.process(xs[i], (i % 17) == 0, i == 0 ? 0.0 : dt, state) == expected[i]);
        }
    }
    SECTION("a rising event with no attack lands at once, whatever the decay") {
        ProcessorChain chain;
        chain.decayMs = 300.0f;
        ProcessorChain::State state;
        std::ignore = chain.process(0.1f, false, 0.0, state);
        CHECK(chain.process(0.9f, true, dt, state) == 0.9f);
    }
}

TEST_CASE("An event below the smoothed value is held through the decay (an inverting chain)",
          "[processor][smoothing][adr900]") {
    // gain -0.55, offset 1: rests at 1 and dips to 0.45 on every hit, before the smoothing -- so the
    // event falls, and its edge is the decay.
    ProcessorChain chain;
    chain.gain = -0.55f;
    chain.offset = 1.0f;
    chain.attackMs = 200.0f;
    chain.decayMs = 60.0f;
    const auto trace = eventTrace(chain, 60.0, 90, 20);
    float dip = 1.0f;
    for (const float v : trace) {
        dip = std::min(dip, v);
    }
    CHECK(dip < 0.45f + 1e-5f); // the whole dip, held through the 60 ms decay
    // Control: the old stage followed the dip for one frame with the decay's one-pole and came back up.
    float old = 1.0f;
    float oldDip = 1.0f;
    for (int f = 1; f < 90; ++f) {
        const float y = f == 20 ? 0.45f : 1.0f;
        const float c = y > old ? smoothingCoefficient(200.0f, 1.0 / 60.0) : smoothingCoefficient(60.0f, 1.0 / 60.0);
        old = old + (y - old) * c;
        oldDip = std::min(oldDip, old);
    }
    CHECK(oldDip > 0.85f);
}

// ---- depth from a signal ------------------------------------------------------------------------

namespace {
struct DepthFixture {
    signals::SignalBus bus;
    ParameterSet params;
    Modulator modulator;
    signals::SignalId src = bus.declare("audio.bass");
    signals::SignalId depth = bus.declare("section.energy");
    Parameter<float>& scale = params.add(ParamDesc<float>{.path = "orb/scale", .defaultValue = 1.0f, .hardMin = 0.0f,
                                                          .hardMax = 10.0f});
};
} // namespace

TEST_CASE("depthSource scales a route's deviation from its op's neutral value", "[modulation][depth][adr900]") {
    DepthFixture f;
    f.bus.set(f.src, 0.6f);
    ModRoute r;
    r.source = "audio.bass";
    r.target = "orb/scale";
    r.depthSource = "section.energy";

    SECTION("Add: the offset scales") {
        r.op = ModOp::Add;
        f.modulator.addRoute(r);
        REQUIRE(f.modulator.bind(f.bus, f.params).has_value());
        for (const float e : {0.0f, 0.25f, 1.0f}) {
            f.bus.set(f.depth, e);
            f.modulator.evaluate(f.bus, f.params, 0.01);
            CHECK_THAT(d(f.scale.value()), WithinAbs(1.0 + 0.6 * d(e), 1e-6));
        }
    }
    SECTION("Multiply: the deviation from 1 scales, the target never dims at depth 0") {
        r.op = ModOp::Multiply;
        r.chain.remapEnabled = true;
        r.chain.remapOutMin = 1.0f;
        r.chain.remapOutMax = 2.0f; // 0.6 -> x1.6
        f.scale.setBase(2.0f);
        f.modulator.addRoute(r);
        REQUIRE(f.modulator.bind(f.bus, f.params).has_value());
        f.bus.set(f.depth, 0.5f);
        f.modulator.evaluate(f.bus, f.params, 0.01);
        CHECK_THAT(d(f.scale.value()), WithinAbs(2.0 * 1.3, 1e-5));
        f.bus.set(f.depth, 0.0f);
        f.modulator.evaluate(f.bus, f.params, 0.01);
        CHECK_THAT(d(f.scale.value()), WithinAbs(2.0, 1e-6));
    }
    SECTION("Replace: a crossfade from the value it found") {
        r.op = ModOp::Replace;
        f.scale.setBase(3.0f);
        f.modulator.addRoute(r);
        REQUIRE(f.modulator.bind(f.bus, f.params).has_value());
        f.bus.set(f.depth, 0.25f);
        f.modulator.evaluate(f.bus, f.params, 0.01);
        CHECK_THAT(d(f.scale.value()), WithinAbs(3.0 + (0.6 - 3.0) * 0.25, 1e-5));
    }
    SECTION("depthMin and depthMax remap the signal") {
        r.op = ModOp::Add;
        r.depthMin = 0.3f;
        r.depthMax = 1.0f;
        f.modulator.addRoute(r);
        REQUIRE(f.modulator.bind(f.bus, f.params).has_value());
        f.bus.set(f.depth, 0.0f);
        f.modulator.evaluate(f.bus, f.params, 0.01);
        CHECK_THAT(d(f.scale.value()), WithinAbs(1.0 + 0.6 * 0.3, 1e-6));
    }
}

TEST_CASE("A route without a depth source writes exactly what it always wrote", "[modulation][depth][adr900]") {
    DepthFixture f;
    f.scale.setBase(0.7f);
    f.bus.set(f.src, 0.37f);
    for (const ModOp op : {ModOp::Add, ModOp::Multiply, ModOp::Replace, ModOp::Min, ModOp::Max}) {
        Modulator m;
        ModRoute r;
        r.source = "audio.bass";
        r.target = "orb/scale";
        r.op = op;
        r.amount = 1.7f;
        m.addRoute(r);
        REQUIRE(m.bind(f.bus, f.params).has_value());
        m.evaluate(f.bus, f.params, 0.01);
        CHECK(f.scale.value() == applyModOp(op, 0.7f, 0.37f * 1.7f));
    }
}

TEST_CASE("An unknown depth source leaves the route unbound and says so", "[modulation][depth][adr900]") {
    DepthFixture f;
    ModRoute r;
    r.source = "audio.bass";
    r.target = "orb/scale";
    r.depthSource = "section.nope";
    f.modulator.addRoute(r);
    const auto bound = f.modulator.bind(f.bus, f.params);
    REQUIRE_FALSE(bound.has_value());
    CHECK_THAT(bound.error().message, ContainsSubstring("section.nope"));
    f.bus.set(f.src, 1.0f);
    f.modulator.evaluate(f.bus, f.params, 0.01);
    CHECK(f.scale.value() == 1.0f); // the route does not run half-bound
}

// ---- serialisation ------------------------------------------------------------------------------

TEST_CASE("delayMs, depthSource and the depth range round-trip through a project", "[serialization][adr900]") {
    ModRoute r;
    r.source = "audio.bass";
    r.target = "orb/scale";
    r.chain.delayMs = 125.0f;
    r.chain.attackMs = 40.0f;
    r.depthSource = "section.energy";
    r.depthMin = 0.35f;
    r.depthMax = 0.9f;
    const nlohmann::json j = routeToJson(r);
    CHECK(j["chain"]["delayMs"] == 125.0);
    CHECK(j["depthSource"] == "section.energy");
    auto back = routeFromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->chain.delayMs == 125.0f);
    CHECK(back->depthSource == "section.energy");
    CHECK(back->depthMin == 0.35f);
    CHECK(back->depthMax == 0.9f);
    // A route with no depth source writes no depth keys, and one written before this reads as no delay.
    ModRoute plain;
    plain.source = "audio.bass";
    plain.target = "orb/scale";
    CHECK_FALSE(routeToJson(plain).contains("depthSource"));
    nlohmann::json legacy = routeToJson(plain);
    legacy["chain"].erase("delayMs");
    auto old = routeFromJson(legacy);
    REQUIRE(old.has_value());
    CHECK(old->chain.delayMs == 0.0f);
    // A negative delay is refused rather than read as none.
    legacy["chain"]["delayMs"] = -5.0;
    CHECK_FALSE(routeFromJson(legacy).has_value());
}

// ---- timeline sources that score events ---------------------------------------------------------

namespace {
struct TimelineFixture {
    signals::SignalBus bus;
    ParameterSet params;
    signals::SourceRack rack;
    signals::TimelineSource* timeline = nullptr;

    explicit TimelineFixture(signals::TimelineMode mode, std::vector<signals::Keyframe> keys) {
        auto source = std::make_unique<signals::TimelineSource>("kick");
        source->setMode(mode);
        for (const auto& k : keys) {
            source->addKey(k);
        }
        timeline = static_cast<signals::TimelineSource*>(&rack.add(std::move(source)));
        rack.attach(bus, params);
    }
    // Plays [0, seconds) at `fps` and counts the pulses the frames saw: events in event mode, rising
    // edges of the value in value mode.
    int play(double fps, double seconds) {
        rack.reset();
        int seen = 0;
        bool wasUp = false;
        const auto id = *bus.find("timeline.kick");
        const int frames = static_cast<int>(seconds * fps);
        for (int f = 0; f < frames; ++f) {
            signals::SourceContext ctx;
            ctx.time = FrameTime{static_cast<double>(f) / fps, f == 0 ? 0.0 : 1.0 / fps, static_cast<std::uint64_t>(f)};
            rack.update(bus, ctx);
            if (timeline->mode() == signals::TimelineMode::Event) {
                seen += bus.event(id) ? 1 : 0;
            } else {
                const bool up = bus.value(id) > 0.0f;
                seen += up && !wasUp ? 1 : 0;
                wasUp = up;
            }
            bus.clearEvents();
        }
        return seen;
    }
};

// GV3's shape: 25 ms step pulses, 0 -> 1 -> 0, on a 130 BPM grid starting at 0.46 s.
std::vector<signals::Keyframe> pulses(int count, double width) {
    std::vector<signals::Keyframe> keys{{0.0, 0.0f, signals::KeyInterpolation::Step}};
    for (int i = 0; i < count; ++i) {
        const double t = 0.46 + i * (60.0 / 130.0);
        keys.push_back({t, 1.0f, signals::KeyInterpolation::Step});
        keys.push_back({t + width, 0.0f, signals::KeyInterpolation::Step});
    }
    return keys;
}
} // namespace

TEST_CASE("An event-mode timeline fires every hit once at any frame rate", "[sources][timeline][adr900]") {
    TimelineFixture events(signals::TimelineMode::Event, pulses(40, 0.025));
    for (const double fps : {24.0, 30.0, 60.0, 144.0}) {
        INFO(fps << " fps");
        CHECK(events.play(fps, 20.0) == 40);
    }
    // Control: the same score in value mode loses pulses between frames at 24 and 30 fps (they are
    // 25 ms wide), which is what event mode exists to stop.
    TimelineFixture values(signals::TimelineMode::Value, pulses(40, 0.025));
    CHECK(values.play(60.0, 20.0) == 40);
    CHECK(values.play(30.0, 20.0) < 40);
    CHECK(values.play(24.0, 20.0) < 40);
}

TEST_CASE("An event-mode timeline carries the key's value as the event's strength, and the bus calls it an event",
          "[sources][timeline][adr900]") {
    TimelineFixture f(signals::TimelineMode::Event, {{0.0, 0.0f, signals::KeyInterpolation::Step},
                                                     {0.5, 0.7f, signals::KeyInterpolation::Step},
                                                     {0.6, 0.0f, signals::KeyInterpolation::Step}});
    const auto id = *f.bus.find("timeline.kick");
    CHECK(f.bus.info(id).isEvent);
    signals::SourceContext ctx;
    ctx.time = FrameTime{0.5, 1.0 / 60.0, 30};
    f.rack.update(f.bus, ctx);
    CHECK(f.bus.event(id));
    CHECK(f.bus.value(id) == 0.7f);
    CHECK(f.timeline->hitBetween(0.45, 0.5, false) == std::optional<float>(0.7f));
    CHECK_FALSE(f.timeline->hitBetween(0.5, 0.55, false).has_value()); // (from, to]: fired once
    CHECK_FALSE(f.timeline->hitBetween(0.55, 0.65, false).has_value()); // the 0 key is not a hit
}

TEST_CASE("A fresh event-mode timeline fires a hit exactly at its first instant, and only then",
          "[sources][timeline][adr900]") {
    TimelineFixture f(signals::TimelineMode::Event, {{1.0, 1.0f, signals::KeyInterpolation::Step}});
    const auto id = *f.bus.find("timeline.kick");
    signals::SourceContext landing;
    landing.time = FrameTime{1.0, 0.0, 0}; // a seek's landing frame on the hit
    f.rack.reset();
    f.rack.update(f.bus, landing);
    CHECK(f.bus.event(id));
    f.bus.clearEvents();
    f.rack.update(f.bus, landing); // the same instant redrawn: no second hit
    CHECK_FALSE(f.bus.event(id));
}

TEST_CASE("An event-mode timeline loops", "[sources][timeline][adr900]") {
    TimelineFixture f(signals::TimelineMode::Event, {{0.25, 1.0f, signals::KeyInterpolation::Step}});
    f.timeline->setLoopLength(1.0);
    CHECK(f.play(60.0, 4.0) == 4);
    CHECK(f.timeline->hitBetween(0.9, 1.3, false).has_value()); // across the wrap
}

TEST_CASE("An envelope triggered by an event-mode timeline fires whatever order the rack lists them in",
          "[sources][timeline][envelope][adr900]") {
    signals::SignalBus bus;
    ParameterSet params;
    signals::SourceRack rack;
    rack.add(std::make_unique<signals::EnvelopeSource>("hit", "timeline.kick")); // listed first
    auto timeline = std::make_unique<signals::TimelineSource>("kick");
    timeline->setMode(signals::TimelineMode::Event);
    timeline->addKey({0.5, 1.0f, signals::KeyInterpolation::Step});
    rack.add(std::move(timeline));
    rack.attach(bus, params);
    params.findAs<float>("sources/hit/attackMs")->setBase(0.0f);
    const auto env = *bus.find("env.hit");
    float level = 0.0f;
    for (int f = 0; f <= 31; ++f) {
        signals::SourceContext ctx;
        ctx.time = FrameTime{f / 60.0, f == 0 ? 0.0 : 1.0 / 60.0, static_cast<std::uint64_t>(f)};
        rack.update(bus, ctx);
        if (f == 31) {
            level = bus.value(env); // the frame after the hit's, where an envelope's attack begins
        }
        bus.clearEvents();
    }
    // The envelope saw the hit. Updated in list order it would have read the flag before the timeline
    // set it, and the bus clears events at the end of every frame: it would never have fired.
    CHECK(level > 0.5f);
}

TEST_CASE("A timeline source's mode round-trips, and value mode reports its pulses", "[sources][timeline][adr900]") {
    signals::TimelineSource source("kick");
    source.setMode(signals::TimelineMode::Event);
    const auto j = source.settingsToJson();
    CHECK(j["mode"] == "event");
    signals::TimelineSource back("kick");
    REQUIRE(back.settingsFromJson(j).has_value());
    CHECK(back.mode() == signals::TimelineMode::Event);
    nlohmann::json bad = j;
    bad["mode"] = "sometimes";
    CHECK_FALSE(back.settingsFromJson(bad).has_value());

    signals::TimelineSource value("gap");
    for (const auto& k : pulses(3, 0.02)) {
        value.addKey(k);
    }
    const auto spans = value.positiveSpans(0.0, 10.0);
    REQUIRE(spans.size() == 3);
    CHECK_THAT(spans[0].first, WithinAbs(0.46, 1e-9));
    CHECK_THAT(spans[0].second - spans[0].first, WithinAbs(0.02, 1e-9));
}
