// ADR-1602: a conditioned signal (`signal.<name>`) -- a route's chain whose output is a bus signal, the same frame.
//
// What has to hold for a scene to move a macro-routed idea onto one:
//   - its value IS the route's: the same chain over the same input, bit for bit (a one-input signal publishes what that
//     route adds to a target at 0);
//   - it is on the bus on its input's frame, so a route reading it lands where a route on the input itself does;
//   - a seek lands it where a play does (ADR-1573's pattern: it holds chain state, so the control replay runs);
//   - firstActive follows a stem while it is active and falls back to the mix when it is not;
//   - its settings round-trip.

#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "params/serialization.hpp"
#include "signals/conditioned_signal.hpp"
#include "signals/source.hpp"
#include "support/synth.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

using namespace avgen;
using nlohmann::json;
using signals::ConditionedSignalSource;

namespace {

// Every stateful stage at once: delay, normalise, trend, smoothing with event ramps, a latching envelope, a spring and
// an integral -- so "bit for bit" covers state, not just arithmetic.
params::ProcessorChain busyChain() {
    params::ProcessorChain c;
    c.delayMs = 50.0f;
    c.gain = 1.7f;
    c.offset = -0.2f;
    c.curve = params::CurveType::Power;
    c.curveAmount = 1.3f;
    c.clampEnabled = true;
    c.clampMin = 0.0f;
    c.clampMax = 1.0f;
    c.attackMs = 12.0f;
    c.decayMs = 180.0f;
    c.envelope = params::EnvelopeMode::LinearFall;
    c.envelopeFallPerSecond = 2.4f;
    c.envelopeRefractoryMs = 130.0f;
    c.envelopeRearm = 0.05f;
    c.springHz = 2.0f;
    c.springDamping = 0.8f;
    return c;
}

params::ProcessorChain trendChain() {
    params::ProcessorChain c;
    c.normalizeSeconds = 4.0f;
    c.trendSeconds = 8.0f;
    c.trendFastMs = 450.0f;
    c.threshold = params::ThresholdMode::Binary;
    c.thresholdLevel = 1.1f;
    c.decayMs = 1500.0f;
    c.envelope = params::EnvelopeMode::PeakHold;
    c.envelopeHoldMs = 650.0f;
    c.envelopeFallPerSecond = 0.33f;
    c.integrate = true;
    c.integrateMin = 0.0f;
    c.integrateMax = 3.0f;
    return c;
}

float wobble(int k) {
    const auto t = static_cast<float>(k) / 30.0f;
    return 0.5f + 0.3f * std::sin(t * 2.1f) + 0.2f * std::sin(t * 7.3f + 1.0f) * std::sin(t * 0.37f);
}

} // namespace

TEST_CASE("signal: a one-input signal is its route's chain, bit for bit", "[signals][conditioned][adr1602]") {
    for (const auto& [chain, depth] : {std::pair{busyChain(), true}, std::pair{trendChain(), false}}) {
        signals::SignalBus bus;
        params::ParameterSet params;
        const signals::SignalId in = bus.declare("stem.bass.onset", 0.0f, 1.0f, true);
        const signals::SignalId level = bus.declare("stem.bass.level");
        auto& target = params.add(params::ParamDesc<float>{.path = "probe/target", .defaultValue = 0.0f,
                                                           .hardMin = -1.0e6f, .hardMax = 1.0e6f});
        params::Modulator modulator;
        params::ModRoute r;
        r.source = "stem.bass.onset";
        r.target = "probe/target";
        r.amount = 1.37f;
        r.chain = chain;
        if (depth) {
            r.depthSource = "stem.bass.level";
            r.depthMin = -0.3f;
            r.depthMax = 1.9f;
        }
        modulator.addRoute(r);
        REQUIRE(modulator.bind(bus, params).has_value());

        ConditionedSignalSource sig("swell");
        ConditionedSignalSource::Input input;
        input.source = "stem.bass.onset";
        input.amount = 1.37f;
        input.chain = chain;
        if (depth) {
            input.depthSource = "stem.bass.level";
            input.depthMin = -0.3f;
            input.depthMax = 1.9f;
        }
        sig.inputs().push_back(input);
        sig.attach(bus, params);
        const signals::SignalId out = *bus.find("signal.swell");

        int moved = 0;
        for (int k = 0; k < 900; ++k) {
            const bool hit = k % 17 == 3 || k % 29 == 0;
            bus.setEvent(in, hit, hit ? wobble(k) : 0.0f);
            bus.set(level, wobble(k + 100));
            signals::SourceContext ctx;
            ctx.time = FrameTime{static_cast<double>(k) / 30.0, k == 0 ? 0.0 : 1.0 / 30.0, static_cast<std::uint64_t>(k)};
            sig.update(bus, ctx);
            params.resetFinals();
            modulator.applyRoutes(bus, params, ctx.time.deltaTime);
            INFO("frame " << k);
            REQUIRE(bus.value(out) == target.finalComponent(0));
            moved += bus.value(out) != 0.0f ? 1 : 0;
            bus.clearEvents();
        }
        CHECK(moved > 100); // the chain did something, or the equality proves nothing
    }
}

namespace {

constexpr std::uint32_t kRate = 44100;

std::filesystem::path clicks() {
    static const std::filesystem::path path = [] {
        const std::size_t frames = 8 * kRate;
        auto tone = testsupport::sine(55.0f, kRate, frames, 0.4f);
        auto click = testsupport::clickTrack(120.0f, kRate, frames);
        std::vector<float> mono(frames);
        for (std::size_t i = 0; i < frames; ++i) {
            mono[i] = tone[i] * (0.5f + 0.5f * std::sin(static_cast<float>(i) / kRate * 1.3f)) + click[i] * 0.6f;
        }
        auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(mono, 2), 2, kRate);
        const auto p = testsupport::processTempDir() / "conditioned_signal_clicks.wav";
        REQUIRE(file.writeWav(p).has_value());
        return p;
    }();
    return path;
}

// The engine: a signal over the onsets (each one a breath falling over a second), one route reading it, and the same
// chain as a route straight from the onsets -- Rainbow World's swell before and after (minus the macro).
struct Rig {
    app::Engine engine{app::EngineMode::Offline};
    Rig() {
        REQUIRE(engine.loadAudio(clicks()).has_value());
        auto sig = std::make_unique<ConditionedSignalSource>("breath");
        ConditionedSignalSource::Input in;
        in.source = "audio.onset";
        in.chain.envelope = params::EnvelopeMode::LinearFall;
        in.chain.envelopeFallPerSecond = 1.0f;
        in.chain.springHz = 3.0f;
        sig->inputs().push_back(in);
        engine.sources().add(std::move(sig));
        engine.sources().attach(engine.signals(), engine.params());
        params::ModRoute out; // a stateless route reading the signal
        out.source = "signal.breath";
        out.target = "post/grade/saturation";
        out.amount = 2.0f;
        engine.modulator().addRoute(out);
        params::ModRoute direct; // the same chain as a route on the input: the same frame, the same value
        direct.source = "audio.onset";
        direct.target = "post/grade/contrast";
        direct.amount = 2.0f;
        direct.chain = in.chain;
        engine.modulator().addRoute(direct);
        engine.rebind();
    }
    void frame(long long k) {
        engine.update(FrameTime{static_cast<double>(k) / 60.0, k == 0 ? 0.0 : 1.0 / 60.0, static_cast<std::uint64_t>(k)});
    }
    void landing(long long k) { engine.update(FrameTime{static_cast<double>(k) / 60.0, 0.0, 0}); }
    [[nodiscard]] float viaSignal() { return engine.params().find("post/grade/saturation")->finalComponent(0); }
    [[nodiscard]] float direct() { return engine.params().find("post/grade/contrast")->finalComponent(0); }
};

float base(app::Engine& e, const char* path) {
    return e.params().find(path)->baseComponent(0);
}

} // namespace

TEST_CASE("signal: a route reading it lands on its input's frame, as the chain routed directly does",
          "[signals][conditioned][adr1602]") {
    Rig rig;
    const float sat0 = base(rig.engine, "post/grade/saturation");
    const float con0 = base(rig.engine, "post/grade/contrast");
    float peak = 0.0f;
    for (long long k = 0; k <= 400; ++k) {
        rig.frame(k);
        INFO("frame " << k);
        REQUIRE(rig.viaSignal() - sat0 == rig.direct() - con0);
        peak = std::max(peak, rig.viaSignal() - sat0);
    }
    CHECK(peak > 0.5f);
}

TEST_CASE("signal: a seek lands it where a play does", "[signals][conditioned][seek][adr1602][adr1573]") {
    for (const long long t : {155LL, 211LL, 300LL}) {
        INFO("frame " << t);
        Rig played;
        std::vector<float> want;
        for (long long k = 0; k <= t + 2; ++k) {
            played.frame(k);
            if (k >= t) {
                want.push_back(played.viaSignal());
            }
        }
        Rig seeked;
        CHECK(seeked.engine.controlReplayNeeded());
        seeked.engine.seekSeconds(static_cast<double>(t) / 60.0);
        seeked.landing(t);
        CHECK(seeked.viaSignal() == want[0]);
        seeked.frame(t + 1);
        CHECK(seeked.viaSignal() == want[1]);
        seeked.frame(t + 2);
        CHECK(seeked.viaSignal() == want[2]);
    }
}

TEST_CASE("signal: firstActive follows the stem while it is active and falls back to the mix",
          "[signals][conditioned][adr1602]") {
    signals::SignalBus bus;
    params::ParameterSet params;
    const signals::SignalId stem = bus.declare("stem.drums.highMid");
    const signals::SignalId active = bus.declare("stem.drums.active");
    const signals::SignalId mix = bus.declare("response.onsetEnv");
    ConditionedSignalSource sig("clap");
    ConditionedSignalSource::Input a;
    a.source = "stem.drums.highMid"; // gate: stem.drums.active, found by name
    a.amount = 2.0f;
    ConditionedSignalSource::Input b;
    b.source = "response.onsetEnv"; // no response.active: always active, the fallback
    sig.inputs() = {a, b};
    sig.attach(bus, params);
    const signals::SignalId out = *bus.find("signal.clap");
    const auto step = [&](float s, bool on, float m) {
        bus.set(stem, s);
        bus.set(active, on ? 1.0f : 0.0f);
        bus.set(mix, m);
        signals::SourceContext ctx;
        ctx.time.deltaTime = 1.0 / 30.0;
        sig.update(bus, ctx);
        return bus.value(out);
    };
    CHECK(step(0.25f, true, 0.9f) == 0.5f);
    CHECK(sig.selected() == 0);
    CHECK(step(0.25f, false, 0.9f) == 0.9f); // the stem went quiet past its span: the mix
    CHECK(sig.selected() == 1);
    CHECK(step(0.4f, true, 0.1f) == 0.8f);
    CHECK(sig.selected() == 0);

    // A stem the project does not have: its input never resolves and the mix plays throughout.
    signals::SignalBus noStem;
    const signals::SignalId mixOnly = noStem.declare("response.onsetEnv");
    ConditionedSignalSource fallback("clap");
    fallback.inputs() = {a, b};
    fallback.attach(noStem, params);
    noStem.set(mixOnly, 0.7f);
    signals::SourceContext ctx;
    ctx.time.deltaTime = 1.0 / 30.0;
    fallback.update(noStem, ctx);
    CHECK(noStem.value(*noStem.find("signal.clap")) == 0.7f);
    CHECK(fallback.selected() == 1);
    // ...and a resolved input gated on a stem that is not there (a signal built from the stem's signal) stands aside.
    ConditionedSignalSource gatedOnAbsent("clap2");
    ConditionedSignalSource::Input viaSignal{.source = "signal.clap", .gate = "stem.drums.active"};
    gatedOnAbsent.inputs() = {viaSignal, b};
    gatedOnAbsent.attach(noStem, params);
    noStem.set(mixOnly, 0.3f);
    fallback.update(noStem, ctx);
    gatedOnAbsent.update(noStem, ctx);
    CHECK(gatedOnAbsent.selected() == 1);
    CHECK(noStem.value(*noStem.find("signal.clap2")) == 0.3f);

    // An explicit gate, and the other combines over the active inputs.
    ConditionedSignalSource sum("both");
    ConditionedSignalSource::Input gated = b;
    gated.gate = "stem.drums.active";
    sum.inputs() = {gated, b, ConditionedSignalSource::Input{.constant = 0.5f}};
    sum.setCombine(ConditionedSignalSource::Combine::Sum);
    sum.attach(bus, params);
    bus.set(mix, 0.2f);
    bus.set(active, 0.0f);
    sum.update(bus, ctx);
    CHECK(bus.value(*bus.find("signal.both")) == 0.2f + 0.5f); // the gated twin stands aside
    bus.set(active, 1.0f);
    sum.update(bus, ctx);
    CHECK(bus.value(*bus.find("signal.both")) == 0.2f + 0.2f + 0.5f);
    sum.setCombine(ConditionedSignalSource::Combine::Max);
    sum.update(bus, ctx);
    CHECK(bus.value(*bus.find("signal.both")) == 0.5f);
    sum.setCombine(ConditionedSignalSource::Combine::Mean);
    sum.update(bus, ctx);
    CHECK(bus.value(*bus.find("signal.both")) == (0.2f + 0.2f + 0.5f) / 3.0f);
}

TEST_CASE("signal: an event signal fires with its selected input", "[signals][conditioned][adr1602]") {
    signals::SignalBus bus;
    params::ParameterSet params;
    const signals::SignalId kick = bus.declare("stem.drums.onset", 0.0f, 1.0f, true);
    const signals::SignalId active = bus.declare("stem.drums.active");
    const signals::SignalId onset = bus.declare("response.onset", 0.0f, 1.0f, true);
    ConditionedSignalSource sig("hit");
    sig.inputs() = {ConditionedSignalSource::Input{.source = "stem.drums.onset"},
                    ConditionedSignalSource::Input{.source = "response.onset"}};
    sig.setEventMode(true);
    sig.attach(bus, params);
    const signals::SignalId out = *bus.find("signal.hit");
    CHECK(bus.info(out).isEvent);
    signals::SourceContext ctx;
    ctx.time.deltaTime = 1.0 / 30.0;
    bus.set(active, 1.0f);
    bus.setEvent(onset, true, 0.6f); // the mix fires, but the stem is the one selected
    sig.update(bus, ctx);
    CHECK_FALSE(bus.event(out));
    bus.clearEvents();
    bus.setEvent(kick, true, 0.8f);
    sig.update(bus, ctx);
    CHECK(bus.event(out));
    CHECK(bus.value(out) == 0.8f);
    bus.clearEvents();
    bus.set(active, 0.0f);
    bus.setEvent(onset, true, 0.6f);
    sig.update(bus, ctx);
    CHECK(bus.event(out));
    CHECK(bus.value(out) == 0.6f);
}

TEST_CASE("signal: settings round-trip, through the rack", "[signals][conditioned][serialization][adr1602]") {
    const json settings = json::parse(R"({
      "combine": "firstActive",
      "inputs": [
        {"source": "stem.bass.onset", "amount": 1.5,
         "chain": {"envelope": "linearfall", "envelopeFallPerSecond": 2.4, "envelopeRefractoryMs": 130.0,
                   "envelopeRearm": 0.05},
         "depthSource": "stem.bass.level", "depthMin": -0.3, "depthMax": 1.9, "depthDefault": 0.125},
        {"source": "response.bass", "polarity": "bipolar", "gate": "stem.bass.present",
         "chain": {"gain": 2.4, "offset": -1.45, "clampEnabled": true, "attackMs": 10.0, "decayMs": 180.0}},
        {"constant": 1.0}],
      "chain": {"clampEnabled": true, "clampMin": 0.0, "clampMax": 1.0},
      "event": true})");
    ConditionedSignalSource a("swell");
    REQUIRE(a.settingsFromJson(settings).has_value());
    REQUIRE(a.inputs().size() == 3);
    CHECK(a.inputs()[0].depthDefault == 0.125f);
    CHECK(a.inputs()[1].bipolar);
    CHECK(a.inputs()[1].gate == "stem.bass.present");
    CHECK(a.inputs()[2].constant == 1.0f);
    CHECK(a.outputChain().has_value());
    CHECK(a.eventMode());
    const json written = a.settingsToJson();
    ConditionedSignalSource b("swell");
    REQUIRE(b.settingsFromJson(written).has_value());
    CHECK(b.settingsToJson() == written);
    CHECK(params::chainToJson(b.inputs()[0].chain) == params::chainToJson(a.inputs()[0].chain));

    signals::SourceRack rack;
    REQUIRE(rack.fromJson(json::array({{{"kind", "signal"}, {"name", "swell"}, {"settings", settings}}})).has_value());
    REQUIRE(rack.find("signal", "swell") != nullptr);
    CHECK(rack.toJson()[0]["settings"] == written);
    CHECK(rack.find("signal", "swell")->outputs() == std::vector<std::string>{"signal.swell"});

    // Malformed settings fail and say where.
    ConditionedSignalSource bad("x");
    CHECK_FALSE(bad.settingsFromJson(json::object()).has_value());
    CHECK_FALSE(bad.settingsFromJson(json{{"inputs", json::array({json{{"source", "a"}, {"constant", 1}}})}}).has_value());
    CHECK_FALSE(bad.settingsFromJson(json{{"inputs", json::array({json{{"source", "a"}}})}, {"combine", "median"}})
                    .has_value());
    CHECK_FALSE(bad.settingsFromJson(json{{"inputs", json::array({json{{"source", "a"}, {"chain", 3}}})}}).has_value());
}
