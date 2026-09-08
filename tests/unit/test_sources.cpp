#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "signals/signal_bus.hpp"
#include "signals/source.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

using namespace avgen;
using namespace avgen::signals;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;
using nlohmann::json;

namespace {
double d(float v) {
    return static_cast<double>(v);
}

SourceContext at(double renderTime, double deltaTime = 0.0) {
    SourceContext ctx;
    ctx.time.renderTime = renderTime;
    ctx.time.deltaTime = deltaTime;
    return ctx;
}

SourceContext fromClock(FrameClock& clock) {
    SourceContext ctx;
    ctx.time = clock.current();
    return ctx;
}

struct Fixture {
    SignalBus bus;
    params::ParameterSet params;

    float signal(const char* name) const {
        const auto id = bus.find(name);
        REQUIRE(id.has_value());
        return bus.value(*id);
    }
    params::Parameter<float>& floatParam(const std::string& path) {
        auto* p = params.findAs<float>(path);
        REQUIRE(p != nullptr);
        return *p;
    }
};

// One engine frame: sources publish, then the modulator writes finals for the next frame.
void frame(SourceRack& rack, params::Modulator& modulator, Fixture& f, const SourceContext& ctx) {
    rack.update(f.bus, ctx);
    modulator.evaluate(f.bus, f.params, ctx.time.deltaTime);
    f.bus.clearEvents();
}
} // namespace

// ---- LFO -------------------------------------------------------------------------------------

TEST_CASE("LFO shapes evaluate to known values", "[sources][lfo]") {
    CHECK_THAT(d(LfoSource::evaluate(LfoShape::Sine, 0.0f, 0.5f, 0)), WithinAbs(0.0, 1e-6));
    CHECK_THAT(d(LfoSource::evaluate(LfoShape::Sine, 0.25f, 0.5f, 0)), WithinAbs(0.5, 1e-6));
    CHECK_THAT(d(LfoSource::evaluate(LfoShape::Sine, 0.5f, 0.5f, 0)), WithinAbs(1.0, 1e-6));
    CHECK_THAT(d(LfoSource::evaluate(LfoShape::Sine, 0.75f, 0.5f, 0)), WithinAbs(0.5, 1e-6));

    CHECK_THAT(d(LfoSource::evaluate(LfoShape::Triangle, 0.0f, 0.5f, 0)), WithinAbs(0.0, 1e-6));
    CHECK_THAT(d(LfoSource::evaluate(LfoShape::Triangle, 0.25f, 0.5f, 0)), WithinAbs(0.5, 1e-6));
    CHECK_THAT(d(LfoSource::evaluate(LfoShape::Triangle, 0.5f, 0.5f, 0)), WithinAbs(1.0, 1e-6));
    CHECK_THAT(d(LfoSource::evaluate(LfoShape::Triangle, 0.75f, 0.5f, 0)), WithinAbs(0.5, 1e-6));

    CHECK_THAT(d(LfoSource::evaluate(LfoShape::Saw, 0.3f, 0.5f, 0)), WithinAbs(0.3, 1e-6));
    CHECK_THAT(d(LfoSource::evaluate(LfoShape::Saw, 0.9f, 0.5f, 0)), WithinAbs(0.9, 1e-6));

    CHECK(LfoSource::evaluate(LfoShape::Square, 0.0f, 0.5f, 0) == 1.0f);
    CHECK(LfoSource::evaluate(LfoShape::Square, 0.49f, 0.5f, 0) == 1.0f);
    CHECK(LfoSource::evaluate(LfoShape::Square, 0.5f, 0.5f, 0) == 0.0f);
    CHECK(LfoSource::evaluate(LfoShape::Square, 0.1f, 0.05f, 0) == 0.0f); // pulse width
    CHECK(LfoSource::evaluate(LfoShape::Square, 0.7f, 0.8f, 0) == 1.0f);

    // Sample & hold ignores the phase and depends only on the cycle seed.
    const float a = LfoSource::evaluate(LfoShape::SampleHold, 0.1f, 0.5f, 7);
    CHECK(LfoSource::evaluate(LfoShape::SampleHold, 0.9f, 0.5f, 7) == a);
    CHECK(LfoSource::evaluate(LfoShape::SampleHold, 0.1f, 0.5f, 8) != a);
    CHECK(a >= 0.0f);
    CHECK(a < 1.0f);
}

TEST_CASE("LFO free-running phase is a pure function of renderTime", "[sources][lfo]") {
    Fixture f;
    LfoSource lfo("wobble", LfoShape::Sine);
    lfo.attach(f.bus, f.params);
    REQUIRE(lfo.outputs() == std::vector<std::string>{"lfo.wobble", "lfo.wobble.bipolar"});
    f.floatParam("sources/wobble/rate").setBase(2.0f);
    f.params.resetFinals();

    lfo.update(f.bus, at(0.125)); // phase 0.25
    CHECK_THAT(d(f.signal("lfo.wobble")), WithinAbs(0.5, 1e-5));
    CHECK_THAT(d(f.signal("lfo.wobble.bipolar")), WithinAbs(0.0, 1e-5));
    lfo.update(f.bus, at(0.25)); // phase 0.5
    CHECK_THAT(d(f.signal("lfo.wobble")), WithinAbs(1.0, 1e-5));
    CHECK_THAT(d(f.signal("lfo.wobble.bipolar")), WithinAbs(1.0, 1e-5));

    // Seek: the same time yields the same value regardless of what was evaluated before.
    lfo.update(f.bus, at(1234.5));
    lfo.update(f.bus, at(7.0));
    lfo.update(f.bus, at(0.125));
    CHECK_THAT(d(f.signal("lfo.wobble")), WithinAbs(0.5, 1e-5));

    // Phase offset shifts the waveform.
    f.floatParam("sources/wobble/phase").setBase(0.25f);
    f.params.resetFinals();
    lfo.update(f.bus, at(0.0));
    CHECK_THAT(d(f.signal("lfo.wobble")), WithinAbs(0.5, 1e-5));

    // Negative times still produce a phase in [0,1).
    lfo.update(f.bus, at(-0.125));
    CHECK_THAT(d(f.signal("lfo.wobble")), WithinAbs(0.0, 1e-5));
}

TEST_CASE("LFO beat sync derives its phase from beatCount and beatPhase", "[sources][lfo]") {
    Fixture f;
    LfoSource lfo("beat", LfoShape::Saw);
    lfo.attach(f.bus, f.params);
    auto* sync = f.params.findAs<bool>("sources/beat/beatSync");
    REQUIRE(sync != nullptr);
    sync->setBase(true);
    f.params.resetFinals();

    SourceContext ctx = at(99.0);
    ctx.tempoBpm = 120.0f;
    ctx.beatCount = 3;
    ctx.beatPhase = 0.5f;
    lfo.update(f.bus, ctx); // (3 + 0.5) / 1 -> phase 0.5
    CHECK_THAT(d(f.signal("lfo.beat")), WithinAbs(0.5, 1e-5));

    f.floatParam("sources/beat/beatsPerCycle").setBase(4.0f);
    f.params.resetFinals();
    lfo.update(f.bus, ctx); // 3.5 / 4 -> phase 0.875
    CHECK_THAT(d(f.signal("lfo.beat")), WithinAbs(0.875, 1e-5));

    // Unknown tempo falls back to free-running on renderTime (rate 0.5 Hz, t = 99 -> phase 0.5).
    ctx.tempoBpm = 0.0f;
    lfo.update(f.bus, ctx);
    CHECK_THAT(d(f.signal("lfo.beat")), WithinAbs(0.5, 1e-5));
}

TEST_CASE("LFO sample & hold is constant within a cycle and identical across instances", "[sources][lfo]") {
    Fixture f;
    LfoSource a("a", LfoShape::SampleHold);
    LfoSource b("b", LfoShape::SampleHold);
    a.attach(f.bus, f.params);
    b.attach(f.bus, f.params);
    f.floatParam("sources/a/rate").setBase(1.0f);
    f.floatParam("sources/b/rate").setBase(1.0f);
    f.params.resetFinals();

    a.update(f.bus, at(2.1));
    const float first = f.signal("lfo.a");
    a.update(f.bus, at(2.9));
    CHECK(f.signal("lfo.a") == first);
    a.update(f.bus, at(3.1));
    CHECK(f.signal("lfo.a") != first);
    b.update(f.bus, at(2.5));
    CHECK(f.signal("lfo.b") == first);
}

TEST_CASE("LFO settings serialise the shape", "[sources][lfo][json]") {
    LfoSource lfo("x", LfoShape::Triangle);
    CHECK(lfo.settingsToJson() == json{{"shape", "triangle"}});
    REQUIRE(lfo.settingsFromJson(json{{"shape", "samplehold"}}).has_value());
    CHECK(lfo.shape() == LfoShape::SampleHold);
    REQUIRE(lfo.settingsFromJson(json::object()).has_value()); // missing key keeps the shape
    CHECK(lfo.shape() == LfoShape::SampleHold);
    CHECK_FALSE(lfo.settingsFromJson(json{{"shape", "Sine"}}).has_value());
    CHECK_FALSE(lfo.settingsFromJson(json{{"shape", 3}}).has_value());
    CHECK_FALSE(lfo.settingsFromJson(json::array()).has_value());
    CHECK(lfo.shape() == LfoShape::SampleHold);
}

// ---- Envelope --------------------------------------------------------------------------------

namespace {
struct EnvelopeRig {
    Fixture f;
    SignalId onset;
    EnvelopeSource env{"hit", "audio.onset"};
    FixedStepClock clock{1000.0};

    EnvelopeRig()
        : onset(f.bus.declare("audio.onset", 0.0f, 1.0f, true)) {
        env.attach(f.bus, f.params);
        f.floatParam("sources/hit/attackMs").setBase(10.0f);
        f.floatParam("sources/hit/decayMs").setBase(10.0f);
        f.floatParam("sources/hit/sustain").setBase(0.4f);
        f.floatParam("sources/hit/holdMs").setBase(10.0f);
        f.floatParam("sources/hit/releaseMs").setBase(10.0f);
        f.params.resetFinals();
        clock.tick(); // frame 0, dt = 0
    }

    // Advances one millisecond, optionally firing the trigger on that frame.
    float step(bool fire = false) {
        clock.tick();
        f.bus.setEvent(onset, fire);
        env.update(f.bus, fromClock(clock));
        f.bus.clearEvents();
        return f.signal("env.hit");
    }
    float run(int ms) {
        float last = 0.0f;
        for (int i = 0; i < ms; ++i) {
            last = step();
        }
        return last;
    }
};
} // namespace

TEST_CASE("Envelope follows attack, decay, hold and release timing", "[sources][envelope]") {
    EnvelopeRig rig;
    CHECK(rig.run(5) == 0.0f);                           // idle
    CHECK_THAT(d(rig.step(true)), WithinAbs(0.0, 1e-4)); // trigger frame: the attack starts now
    CHECK_THAT(d(rig.run(5)), WithinAbs(0.5, 1e-3));
    CHECK_THAT(d(rig.run(5)), WithinAbs(1.0, 1e-3)); // peak at attackMs
    CHECK_THAT(d(rig.run(5)), WithinAbs(0.7, 1e-3)); // half way down to sustain
    CHECK_THAT(d(rig.run(5)), WithinAbs(0.4, 1e-3)); // sustain
    CHECK_THAT(d(rig.run(5)), WithinAbs(0.4, 1e-3)); // holding
    CHECK_THAT(d(rig.run(5)), WithinAbs(0.4, 2e-3)); // end of hold
    CHECK_THAT(d(rig.run(5)), WithinAbs(0.2, 3e-3)); // releasing
    CHECK_THAT(d(rig.run(6)), WithinAbs(0.0, 1e-6)); // released
    CHECK(rig.run(20) == 0.0f);
    CHECK(rig.env.level() == 0.0f);
}

TEST_CASE("Envelope retrigger continues the attack from the current level", "[sources][envelope]") {
    EnvelopeRig rig;
    rig.step(true);
    CHECK_THAT(d(rig.run(5)), WithinAbs(0.5, 1e-3));
    const float before = rig.step(true); // retrigger mid-attack
    CHECK_THAT(d(before), WithinAbs(0.6, 1e-3));
    CHECK_THAT(d(rig.step()), WithinAbs(0.7, 1e-3)); // no drop, keeps rising
    CHECK_THAT(d(rig.run(3)), WithinAbs(1.0, 1e-3));

    // Retrigger during release restarts the attack from where it is.
    rig.run(25); // through decay + hold into release
    const float level = rig.f.signal("env.hit");
    CHECK(level > 0.0f);
    CHECK(level < 0.4f);
    rig.step(true);
    CHECK(rig.step() > level);

    rig.env.reset();
    CHECK(rig.env.level() == 0.0f);
    CHECK(rig.run(3) == 0.0f);
}

TEST_CASE("Envelope resolves a trigger declared after attach and instant stages", "[sources][envelope]") {
    Fixture f;
    EnvelopeSource env("late", "custom.trigger");
    env.attach(f.bus, f.params);
    env.update(f.bus, at(0.0, 0.001));
    CHECK(f.signal("env.late") == 0.0f);

    const SignalId trigger = f.bus.declare("custom.trigger", 0.0f, 1.0f, true);
    f.floatParam("sources/late/attackMs").setBase(0.0f); // instant attack
    f.params.resetFinals();
    f.bus.setEvent(trigger, true);
    env.update(f.bus, at(0.001, 0.001));
    f.bus.clearEvents();
    env.update(f.bus, at(0.002, 0.001));
    CHECK(f.signal("env.late") > 0.9f);

    env.setTrigger("other.trigger");
    CHECK(env.trigger() == "other.trigger");
    CHECK(env.settingsToJson() == json{{"trigger", "other.trigger"}});
    REQUIRE(env.settingsFromJson(json{{"trigger", "audio.onset"}}).has_value());
    CHECK(env.trigger() == "audio.onset");
    CHECK_FALSE(env.settingsFromJson(json{{"trigger", 1}}).has_value());
    CHECK(env.trigger() == "audio.onset");
}

// ---- Noise -----------------------------------------------------------------------------------

TEST_CASE("Noise is deterministic per seed and time", "[sources][noise]") {
    const float a = NoiseSource::evaluate(1.234, 1.0f, 1.0f, 42);
    CHECK(NoiseSource::evaluate(1.234, 1.0f, 1.0f, 42) == a);
    CHECK(NoiseSource::evaluate(1.234, 1.0f, 1.0f, 43) != a);
    CHECK(a >= 0.0f);
    CHECK(a <= 1.0f);

    // Lattice values are spread out, not degenerate.
    float lo = 1.0f;
    float hi = 0.0f;
    for (int i = 0; i < 64; ++i) {
        const float v = NoiseSource::evaluate(static_cast<double>(i), 1.0f, 0.0f, 5);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    CHECK(lo < 0.2f);
    CHECK(hi > 0.8f);
}

TEST_CASE("Smooth noise is continuous and step noise is piecewise constant", "[sources][noise]") {
    float previous = NoiseSource::evaluate(0.0, 2.0f, 1.0f, 9);
    for (int i = 1; i <= 5000; ++i) {
        const float v = NoiseSource::evaluate(static_cast<double>(i) * 0.001, 2.0f, 1.0f, 9);
        CHECK(std::abs(v - previous) < 0.01f);
        previous = v;
    }
    // Step: constant inside a cell, changes at cell boundaries.
    const float cell0 = NoiseSource::evaluate(0.1, 1.0f, 0.0f, 9);
    CHECK(NoiseSource::evaluate(0.9, 1.0f, 0.0f, 9) == cell0);
    CHECK(NoiseSource::evaluate(1.1, 1.0f, 0.0f, 9) != cell0);
    // Smoothness 0 at a lattice point equals the smooth value there.
    CHECK(NoiseSource::evaluate(3.0, 1.0f, 0.0f, 9) == NoiseSource::evaluate(3.0, 1.0f, 1.0f, 9));
}

TEST_CASE("Noise source publishes and serialises its seed", "[sources][noise][json]") {
    Fixture f;
    NoiseSource noise("n", 77);
    noise.attach(f.bus, f.params);
    f.floatParam("sources/n/rate").setBase(3.0f);
    f.floatParam("sources/n/smoothness").setBase(0.0f);
    f.params.resetFinals();
    noise.update(f.bus, at(0.5));
    CHECK(f.signal("noise.n") == NoiseSource::evaluate(0.5, 3.0f, 0.0f, 77));
    CHECK(noise.settingsToJson() == json{{"seed", 77}});
    REQUIRE(noise.settingsFromJson(json{{"seed", 5}}).has_value());
    CHECK(noise.seed() == 5);
    CHECK_FALSE(noise.settingsFromJson(json{{"seed", -1}}).has_value());
    CHECK_FALSE(noise.settingsFromJson(json{{"seed", "x"}}).has_value());
    CHECK(noise.seed() == 5);
}

// ---- Random ----------------------------------------------------------------------------------

TEST_CASE("Random draws a reproducible value per trigger", "[sources][random]") {
    Fixture f;
    const SignalId onset = f.bus.declare("audio.onset", 0.0f, 1.0f, true);
    RandomSource random("r", "audio.onset", 3);
    random.attach(f.bus, f.params);

    auto fire = [&](bool on) {
        f.bus.setEvent(onset, on);
        random.update(f.bus, at(0.0, 0.016));
        f.bus.clearEvents();
        return f.signal("random.r");
    };
    CHECK(fire(false) == 0.0f);
    std::vector<float> first;
    for (int i = 0; i < 4; ++i) {
        first.push_back(fire(true));
        CHECK(fire(false) == first.back()); // holds between triggers
    }
    CHECK(first[0] != first[1]);
    CHECK(first[1] != first[2]);

    random.reset();
    CHECK(fire(false) == 0.0f);
    for (int i = 0; i < 4; ++i) {
        CHECK(fire(true) == first[static_cast<std::size_t>(i)]);
    }

    // Another instance with the same seed produces the same sequence; another seed does not.
    RandomSource twin("t", "audio.onset", 3);
    RandomSource other("o", "audio.onset", 4);
    twin.attach(f.bus, f.params);
    other.attach(f.bus, f.params);
    f.bus.setEvent(onset, true);
    twin.update(f.bus, at(0.0, 0.016));
    other.update(f.bus, at(0.0, 0.016));
    f.bus.clearEvents();
    CHECK(f.signal("random.t") == first[0]);
    CHECK(f.signal("random.o") != first[0]);
}

TEST_CASE("Random slews towards its target", "[sources][random]") {
    Fixture f;
    const SignalId onset = f.bus.declare("audio.onset", 0.0f, 1.0f, true);
    RandomSource random("r", "audio.onset", 11);
    random.attach(f.bus, f.params);
    f.floatParam("sources/r/slewMs").setBase(100.0f);
    f.params.resetFinals();

    f.bus.setEvent(onset, true);
    random.update(f.bus, at(0.0, 0.0));
    f.bus.clearEvents();
    CHECK(f.signal("random.r") == 0.0f); // no time has passed
    // Find the target by evaluating a slew-free twin.
    RandomSource instant("i", "audio.onset", 11);
    instant.attach(f.bus, f.params);
    f.bus.setEvent(onset, true);
    instant.update(f.bus, at(0.0, 0.0));
    f.bus.clearEvents();
    const float target = f.signal("random.i");
    REQUIRE(target > 0.05f);

    float previous = 0.0f;
    for (int i = 1; i <= 10; ++i) {
        random.update(f.bus, at(static_cast<double>(i) * 0.01, 0.01));
        const float v = f.signal("random.r");
        CHECK(v > previous);
        CHECK(v < target);
        previous = v;
    }
    CHECK_THAT(d(previous), WithinAbs(d(target * (1.0f - std::exp(-1.0f))), 1e-3)); // one time constant
    for (int i = 0; i < 200; ++i) {
        random.update(f.bus, at(1.0, 0.01));
    }
    CHECK_THAT(d(f.signal("random.r")), WithinAbs(d(target), 1e-4));

    CHECK(random.settingsToJson() == json{{"trigger", "audio.onset"}, {"seed", 11}});
    REQUIRE(random.settingsFromJson(json{{"trigger", "beat.pulse"}, {"seed", 2}}).has_value());
    CHECK(random.trigger() == "beat.pulse");
    CHECK_FALSE(random.settingsFromJson(json{{"seed", 1.5}}).has_value());
}

// ---- Timeline --------------------------------------------------------------------------------

namespace {
TimelineSource makeTimeline() {
    TimelineSource tl("t");
    tl.addKey(Keyframe{2.0, 0.0f, KeyInterpolation::Smooth});
    tl.addKey(Keyframe{0.0, 0.0f, KeyInterpolation::Linear});
    tl.addKey(Keyframe{3.0, 1.0f, KeyInterpolation::Linear});
    tl.addKey(Keyframe{1.0, 1.0f, KeyInterpolation::Step});
    return tl;
}
} // namespace

TEST_CASE("Timeline keeps keys sorted and interpolates per left key", "[sources][timeline]") {
    TimelineSource tl = makeTimeline();
    REQUIRE(tl.keys().size() == 4);
    CHECK(tl.keys()[0].time == 0.0);
    CHECK(tl.keys()[1].time == 1.0);
    CHECK(tl.keys()[2].time == 2.0);
    CHECK(tl.keys()[3].time == 3.0);

    CHECK_THAT(d(tl.evaluate(0.5)), WithinAbs(0.5, 1e-6)); // linear
    CHECK_THAT(d(tl.evaluate(1.0)), WithinAbs(1.0, 1e-6)); // on a key
    CHECK_THAT(d(tl.evaluate(1.5)), WithinAbs(1.0, 1e-6)); // step holds the left value
    CHECK_THAT(d(tl.evaluate(1.999)), WithinAbs(1.0, 1e-6));
    CHECK_THAT(d(tl.evaluate(2.0)), WithinAbs(0.0, 1e-6));
    CHECK_THAT(d(tl.evaluate(2.25)), WithinAbs(0.15625, 1e-6)); // smoothstep(0.25)
    CHECK_THAT(d(tl.evaluate(2.5)), WithinAbs(0.5, 1e-6));
    CHECK_THAT(d(tl.evaluate(2.75)), WithinAbs(0.84375, 1e-6));
    // Hold outside the key range.
    CHECK(tl.evaluate(-5.0) == 0.0f);
    CHECK(tl.evaluate(3.0) == 1.0f);
    CHECK(tl.evaluate(50.0) == 1.0f);

    // Loop wraps time.
    tl.setLoopLength(3.0);
    CHECK_THAT(d(tl.evaluate(3.5)), WithinAbs(0.5, 1e-6));
    CHECK_THAT(d(tl.evaluate(6.5)), WithinAbs(0.5, 1e-6));
    CHECK_THAT(d(tl.evaluate(-0.5)), WithinAbs(0.5, 1e-6)); // 2.5 after wrapping

    // Manual edits followed by sortKeys().
    tl.keys()[0].time = 10.0;
    tl.sortKeys();
    CHECK(tl.keys().back().time == 10.0);

    TimelineSource empty("e");
    CHECK(empty.evaluate(1.0) == 0.0f);
}

TEST_CASE("Timeline offset and scale parameters shift the evaluation time", "[sources][timeline]") {
    Fixture f;
    TimelineSource tl = makeTimeline();
    tl.attach(f.bus, f.params);
    tl.update(f.bus, at(0.5));
    CHECK_THAT(d(f.signal("timeline.t")), WithinAbs(0.5, 1e-6));

    f.floatParam("sources/t/offset").setBase(2.0f);
    f.params.resetFinals();
    tl.update(f.bus, at(0.5)); // evaluate(2.5) -> smooth mid point
    CHECK_THAT(d(f.signal("timeline.t")), WithinAbs(0.5, 1e-6));

    f.floatParam("sources/t/offset").setBase(0.0f);
    f.floatParam("sources/t/scale").setBase(2.0f);
    f.params.resetFinals();
    tl.update(f.bus, at(0.125)); // evaluate(0.25)
    CHECK_THAT(d(f.signal("timeline.t")), WithinAbs(0.25, 1e-6));
}

TEST_CASE("Timeline settings round-trip and reject malformed keys", "[sources][timeline][json]") {
    TimelineSource tl = makeTimeline();
    tl.setLoopLength(4.0);
    const json j = tl.settingsToJson();
    REQUIRE(j["keys"].is_array());
    CHECK(j["keys"].size() == 4);
    CHECK(j["keys"][1]["interp"] == "step");
    CHECK(j["loopLength"] == 4.0);

    TimelineSource back("t");
    REQUIRE(back.settingsFromJson(j).has_value());
    CHECK(back.settingsToJson() == j);
    CHECK(back.loopLength() == 4.0);
    CHECK(back.keys()[2].interpolation == KeyInterpolation::Smooth);

    // Unsorted input is sorted on load; a missing interp defaults to linear.
    REQUIRE(back.settingsFromJson(json{{"keys", json::array({json{{"time", 5.0}, {"value", 1.0}},
                                                             json{{"time", 1.0}, {"value", 0.0}}})}})
                .has_value());
    REQUIRE(back.keys().size() == 2);
    CHECK(back.keys()[0].time == 1.0);
    CHECK(back.keys()[0].interpolation == KeyInterpolation::Linear);
    CHECK(back.loopLength() == 4.0); // untouched when absent

    const json before = back.settingsToJson();
    CHECK_FALSE(back.settingsFromJson(json{{"keys", 3}}).has_value());
    CHECK_FALSE(back.settingsFromJson(json{{"keys", json::array({json{{"time", "a"}, {"value", 1.0}}})}})
                    .has_value());
    CHECK_FALSE(back.settingsFromJson(json{{"keys", json::array({json{{"time", 1.0}}})}}).has_value());
    CHECK_FALSE(
        back.settingsFromJson(
                json{{"keys", json::array({json{{"time", 1.0}, {"value", 1.0}, {"interp", "cubic"}}})}})
            .has_value());
    CHECK_FALSE(back.settingsFromJson(json{{"loopLength", -1.0}}).has_value());
    CHECK_FALSE(back.settingsFromJson(json{{"loopLength", "x"}}).has_value());
    CHECK(back.settingsToJson() == before);
}

// ---- Macro -----------------------------------------------------------------------------------

TEST_CASE("Macro knobs publish their parameter values and are modulatable", "[sources][macro]") {
    Fixture f;
    const SignalId bass = f.bus.declare("audio.bass");
    SourceRack rack;
    auto macro = std::make_unique<MacroSource>();
    macro->addKnob("x", 0.3f);
    macro->addKnob("y"); // default 0.5
    CHECK(macro->knobs() == std::vector<std::string>{"x", "y"});
    CHECK(macro->outputs() == std::vector<std::string>{"macro.x", "macro.y"});
    rack.add(std::move(macro));
    rack.attach(f.bus, f.params);
    REQUIRE(f.params.find("macros/x") != nullptr);
    REQUIRE(f.params.find("macros/y") != nullptr);
    CHECK(f.params.find("macros/x")->group() == "macros");

    params::Modulator modulator;
    frame(rack, modulator, f, at(0.0));
    CHECK_THAT(d(f.signal("macro.x")), WithinAbs(0.3, 1e-6));
    CHECK_THAT(d(f.signal("macro.y")), WithinAbs(0.5, 1e-6));

    params::ModRoute route;
    route.source = "audio.bass";
    route.target = "macros/x";
    route.op = params::ModOp::Add;
    modulator.addRoute(route);
    REQUIRE(modulator.bind(f.bus, f.params).has_value());
    f.bus.set(bass, 0.5f);
    frame(rack, modulator, f, at(0.0)); // modulator writes final = 0.8 for the next frame
    frame(rack, modulator, f, at(0.016, 0.016));
    CHECK_THAT(d(f.signal("macro.x")), WithinAbs(0.8, 1e-6));
    CHECK_THAT(d(f.params.findAs<float>("macros/x")->base()), WithinAbs(0.3, 1e-6));

    // A knob added after attach is registered by re-attaching (attach is idempotent).
    auto* source = dynamic_cast<MacroSource*>(rack.find("macro", "macros"));
    REQUIRE(source != nullptr);
    source->addKnob("z", 0.9f);
    CHECK(f.params.find("macros/z") == nullptr);
    rack.attach(f.bus, f.params);
    REQUIRE(f.params.find("macros/z") != nullptr);
    frame(rack, modulator, f, at(0.032, 0.016));
    CHECK_THAT(d(f.signal("macro.z")), WithinAbs(0.9, 1e-6));
    CHECK(f.params.findAs<float>("macros/x") != nullptr);

    rack.detachAll();
    CHECK(f.params.find("macros/x") == nullptr);
    CHECK(f.params.find("macros/z") == nullptr);
}

TEST_CASE("Macro settings serialise knobs", "[sources][macro][json]") {
    MacroSource macro;
    macro.addKnob("a", 0.25f);
    macro.addKnob("b", 2.0f); // clamped
    const json j = macro.settingsToJson();
    CHECK(j == json{{"knobs", json::array({json{{"name", "a"}, {"default", 0.25}},
                                           json{{"name", "b"}, {"default", 1.0}}})}});
    MacroSource back;
    REQUIRE(back.settingsFromJson(j).has_value());
    CHECK(back.knobs() == std::vector<std::string>{"a", "b"});
    CHECK(back.settingsToJson() == j);
    CHECK_FALSE(back.settingsFromJson(json{{"knobs", 1}}).has_value());
    CHECK_FALSE(back.settingsFromJson(json{{"knobs", json::array({json{{"default", 0.1}}})}}).has_value());
    CHECK_FALSE(back.settingsFromJson(json{{"knobs", json::array({json{{"name", "a"}, {"default", "x"}}})}})
                    .has_value());
    CHECK_FALSE(
        back.settingsFromJson(json{{"knobs", json::array({json{{"name", "a"}}, json{{"name", "a"}}})}})
            .has_value());
    CHECK(back.settingsToJson() == j);
}

// ---- SourceRack ------------------------------------------------------------------------------

TEST_CASE("SourceRack add, replace, remove and find", "[sources][rack]") {
    SourceRack rack;
    CHECK(rack.find("lfo", "a") == nullptr);
    Source& a = rack.add(std::make_unique<LfoSource>("a", LfoShape::Saw));
    rack.add(std::make_unique<NoiseSource>("a"));
    CHECK(rack.sources().size() == 2);
    CHECK(rack.find("lfo", "a") == &a);
    CHECK(rack.find("noise", "a") != nullptr);
    CHECK(rack.find("noise", "b") == nullptr);

    Source& replaced = rack.add(std::make_unique<LfoSource>("a", LfoShape::Square));
    CHECK(rack.sources().size() == 2);
    CHECK(rack.sources()[0].get() == &replaced); // keeps its slot
    CHECK(dynamic_cast<LfoSource&>(replaced).shape() == LfoShape::Square);

    CHECK(rack.remove("lfo", "a"));
    CHECK_FALSE(rack.remove("lfo", "a"));
    CHECK(rack.sources().size() == 1);
    rack.clear();
    CHECK(rack.sources().empty());
}

TEST_CASE("SourceRack attaches existing and future sources and detaches their parameters",
          "[sources][rack]") {
    Fixture f;
    SourceRack rack;
    rack.add(std::make_unique<LfoSource>("a"));
    CHECK(f.params.find("sources/a/rate") == nullptr);
    rack.attach(f.bus, f.params);
    CHECK(f.params.find("sources/a/rate") != nullptr);
    CHECK(f.params.find("sources/a/beatSync") != nullptr);
    CHECK(f.bus.find("lfo.a").has_value());

    rack.add(std::make_unique<EnvelopeSource>("e"));
    CHECK(f.params.find("sources/e/attackMs") != nullptr);
    CHECK(f.bus.find("env.e").has_value());

    // Replacing a source re-registers its parameters (same paths, fresh objects).
    rack.add(std::make_unique<LfoSource>("a", LfoShape::Triangle));
    CHECK(f.params.find("sources/a/rate") != nullptr);
    CHECK(f.params.find("sources/a/phase") != nullptr);

    CHECK(rack.remove("envelope", "e"));
    CHECK(f.params.find("sources/e/attackMs") == nullptr);

    rack.detachAll();
    CHECK(f.params.find("sources/a/rate") == nullptr);
    CHECK(f.params.size() == 0);
    rack.add(std::make_unique<NoiseSource>("n"));
    CHECK(f.params.find("sources/n/rate") == nullptr); // not attached any more

    rack.attach(f.bus, f.params);
    CHECK(f.params.find("sources/n/rate") != nullptr);
    rack.clear();
    CHECK(f.params.size() == 0);
    rack.add(std::make_unique<NoiseSource>("m")); // clear() keeps the rack attached
    CHECK(f.params.find("sources/m/rate") != nullptr);
}

TEST_CASE("SourceRack update publishes every source and reset clears state", "[sources][rack]") {
    Fixture f;
    const SignalId onset = f.bus.declare("audio.onset", 0.0f, 1.0f, true);
    SourceRack rack;
    rack.add(std::make_unique<LfoSource>("a", LfoShape::Saw));
    rack.add(std::make_unique<RandomSource>("r", "audio.onset", 1));
    rack.attach(f.bus, f.params);
    f.floatParam("sources/a/rate").setBase(1.0f);
    f.params.resetFinals();

    f.bus.setEvent(onset, true);
    rack.update(f.bus, at(0.25, 0.016));
    f.bus.clearEvents();
    CHECK_THAT(d(f.signal("lfo.a")), WithinAbs(0.25, 1e-6));
    CHECK(f.signal("random.r") != 0.0f);
    rack.reset();
    rack.update(f.bus, at(0.25, 0.016));
    CHECK(f.signal("random.r") == 0.0f);
}

TEST_CASE("SourceRack JSON round-trips every kind", "[sources][rack][json]") {
    SourceRack rack;
    rack.add(std::make_unique<LfoSource>("wobble", LfoShape::Square));
    rack.add(std::make_unique<EnvelopeSource>("hit", "audio.onset.kick"));
    rack.add(std::make_unique<NoiseSource>("drift", 123));
    rack.add(std::make_unique<RandomSource>("pick", "beat.pulse", 9));
    auto timeline = std::make_unique<TimelineSource>("intro");
    timeline->addKey(Keyframe{0.0, 0.0f, KeyInterpolation::Smooth});
    timeline->addKey(Keyframe{4.0, 1.0f, KeyInterpolation::Step});
    timeline->setLoopLength(8.0);
    rack.add(std::move(timeline));
    auto macro = std::make_unique<MacroSource>();
    macro->addKnob("energy", 0.7f);
    rack.add(std::move(macro));

    const json j = rack.toJson();
    REQUIRE(j.is_array());
    REQUIRE(j.size() == 6);
    CHECK(j[0]["kind"] == "lfo");
    CHECK(j[0]["name"] == "wobble");
    CHECK(j[0]["settings"]["shape"] == "square");
    CHECK(j[1]["settings"]["trigger"] == "audio.onset.kick");
    CHECK(j[2]["settings"]["seed"] == 123);
    CHECK(j[3]["settings"]["seed"] == 9);
    CHECK(j[4]["settings"]["loopLength"] == 8.0);
    CHECK(j[5]["settings"]["knobs"][0]["name"] == "energy");

    Fixture f;
    SourceRack back;
    back.attach(f.bus, f.params);
    REQUIRE(back.fromJson(j).has_value());
    CHECK(back.toJson() == j);
    CHECK(back.sources().size() == 6);
    // Loaded into an attached rack: parameters and signals exist.
    CHECK(f.params.find("sources/wobble/pulseWidth") != nullptr);
    CHECK(f.params.find("sources/intro/offset") != nullptr);
    CHECK(f.params.find("macros/energy") != nullptr);
    CHECK(f.bus.find("timeline.intro").has_value());
    CHECK(dynamic_cast<LfoSource*>(back.find("lfo", "wobble"))->shape() == LfoShape::Square);
    CHECK(dynamic_cast<EnvelopeSource*>(back.find("envelope", "hit"))->trigger() == "audio.onset.kick");
    CHECK(dynamic_cast<TimelineSource*>(back.find("timeline", "intro"))->keys().size() == 2);

    // fromJson replaces: loading a shorter list drops the rest and their parameters.
    REQUIRE(back.fromJson(json::array({json{{"kind", "lfo"}, {"name", "solo"}}})).has_value());
    CHECK(back.sources().size() == 1);
    CHECK(f.params.find("sources/wobble/rate") == nullptr);
    CHECK(f.params.find("sources/solo/rate") != nullptr);
}

TEST_CASE("SourceRack skips unknown kinds and leaves the rack unchanged on malformed input",
          "[sources][rack][json]") {
    Fixture f;
    SourceRack rack;
    rack.attach(f.bus, f.params);
    rack.add(std::make_unique<LfoSource>("keep"));
    const json before = rack.toJson();

    REQUIRE(
        rack.fromJson(json::array({json{{"kind", "warp"}, {"name", "x"}},
                                   json{{"kind", "noise"}, {"name", "n"}, {"settings", json{{"seed", 2}}}}}))
            .has_value());
    CHECK(rack.sources().size() == 1);
    CHECK(rack.find("noise", "n") != nullptr);
    CHECK(f.params.find("sources/keep/rate") == nullptr);

    const json afterLoad = rack.toJson();
    for (const json& bad : {
             json(3),
             json::array({json(1)}),
             json::array({json{{"name", "x"}}}),
             json::array({json{{"kind", "lfo"}}}),
             json::array({json{{"kind", "lfo"}, {"name", ""}}}),
             json::array({json{{"kind", "lfo"}, {"name", "ok"}, {"settings", json{{"shape", "blob"}}}}}),
             json::array({json{{"kind", "lfo"}, {"name", "ok"}},
                          json{{"kind", "timeline"}, {"name", "t"}, {"settings", json{{"keys", 1}}}}}),
         }) {
        auto result = rack.fromJson(bad);
        INFO(bad.dump());
        CHECK_FALSE(result.has_value());
        CHECK(rack.toJson() == afterLoad);
        CHECK(f.params.find("sources/ok/rate") == nullptr);
    }
    CHECK(f.params.find("sources/n/rate") != nullptr);

    // Factory.
    for (const char* kind : {"lfo", "envelope", "noise", "random", "timeline", "macro"}) {
        auto s = SourceRack::create(kind, "x");
        REQUIRE(s != nullptr);
        CHECK(s->kind() == kind);
        CHECK(s->name() == "x");
    }
    CHECK(SourceRack::create("nope", "x") == nullptr);
}

TEST_CASE("Sources modulate other sources through ordinary routes", "[sources][rack][modulation]") {
    Fixture f;
    SourceRack rack;
    rack.add(std::make_unique<LfoSource>("a", LfoShape::Saw));
    rack.add(std::make_unique<LfoSource>("b", LfoShape::Saw));
    rack.attach(f.bus, f.params);
    f.floatParam("sources/a/rate").setBase(1.0f);
    f.params.resetFinals();

    params::Modulator modulator;
    params::ModRoute route;
    route.source = "lfo.a";
    route.target = "sources/b/rate";
    route.op = params::ModOp::Replace;
    route.amount = 10.0f;
    modulator.addRoute(route);
    REQUIRE(modulator.bind(f.bus, f.params).has_value());

    // Frame 1 at t = 0.5: lfo.a = 0.5, so b's rate becomes 5 Hz for the next frame.
    frame(rack, modulator, f, at(0.5, 0.0));
    CHECK_THAT(d(f.signal("lfo.a")), WithinAbs(0.5, 1e-6));
    CHECK_THAT(d(f.floatParam("sources/b/rate").value()), WithinAbs(5.0, 1e-6));
    // Frame 2 at t = 0.1: with rate 5 the saw phase is 0.5 (it would be 0.05 at the default 0.5 Hz).
    frame(rack, modulator, f, at(0.1, 0.016));
    CHECK_THAT(d(f.signal("lfo.b")), WithinAbs(0.5, 1e-5));
}
