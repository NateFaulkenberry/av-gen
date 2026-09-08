#include "analysis/analyzer.hpp"
#include "signals/audio_signals.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace avgen;
using namespace avgen::signals;

TEST_CASE("SignalBus declares, finds and updates signals", "[signals]") {
    SignalBus bus;
    CHECK(bus.size() == 0);
    CHECK_FALSE(bus.find("audio.bass").has_value());

    const SignalId bass = bus.declare("audio.bass");
    const SignalId onset = bus.declare("audio.onset", 0.0f, 1.0f, true);
    CHECK(bus.size() == 2);
    CHECK(bass != onset);
    REQUIRE(bus.find("audio.bass").has_value());
    CHECK(*bus.find("audio.bass") == bass);
    CHECK(bus.info(bass).name == "audio.bass");
    CHECK_FALSE(bus.info(bass).isEvent);
    CHECK(bus.info(onset).isEvent);
    CHECK(bus.value(bass) == 0.0f);
    CHECK_FALSE(bus.event(bass));

    bus.set(bass, 0.7f);
    CHECK(bus.value(bass) == 0.7f);
    CHECK_FALSE(bus.event(bass));
    CHECK(bus.infos().size() == 2);
}

TEST_CASE("SignalBus declare is idempotent and keeps the first range", "[signals]") {
    SignalBus bus;
    const SignalId a = bus.declare("x", 0.0f, 4.0f);
    const SignalId b = bus.declare("x", -1.0f, 1.0f, true);
    CHECK(a == b);
    CHECK(bus.size() == 1);
    CHECK(bus.info(a).maxValue == 4.0f);
    CHECK_FALSE(bus.info(a).isEvent);
}

TEST_CASE("SignalBus events carry strength for one frame and clear", "[signals]") {
    SignalBus bus;
    const SignalId level = bus.declare("level");
    const SignalId onset = bus.declare("onset", 0.0f, 1.0f, true);
    bus.set(level, 0.4f);
    bus.setEvent(onset, true, 0.8f);
    CHECK(bus.event(onset));
    CHECK(bus.value(onset) == 0.8f);

    bus.clearEvents();
    CHECK_FALSE(bus.event(onset));
    CHECK(bus.value(onset) == 0.0f);
    CHECK(bus.value(level) == 0.4f); // non-event values survive

    bus.setEvent(onset, false, 0.9f);
    CHECK_FALSE(bus.event(onset));
    CHECK(bus.value(onset) == 0.0f);
}

TEST_CASE("AudioSignals declares the fixed vocabulary and publishes frames", "[signals][audio]") {
    SignalBus bus;
    const AudioSignals audio = AudioSignals::declare(bus);
    CHECK(bus.size() == 16);
    for (const char* name : {"audio.rms", "audio.peak", "audio.bass", "audio.lowMid", "audio.mid",
                             "audio.highMid", "audio.treble", "audio.spectralCentroid", "audio.spectralFlux",
                             "audio.onsetStrength", "audio.onset", "audio.tempo", "audio.tempoConfidence",
                             "audio.beat", "audio.beatPhase", "audio.beatCount"}) {
        INFO(name);
        CHECK(bus.find(name).has_value());
    }
    CHECK(bus.info(audio.onset).isEvent);
    CHECK(bus.info(audio.onsetStrength).maxValue == 4.0f);
    CHECK(bus.info(audio.beat).isEvent);
    CHECK(bus.info(audio.tempo).maxValue == 300.0f);
    CHECK(bus.info(audio.beatCount).maxValue == 100000.0f);
    CHECK_FALSE(bus.info(audio.beatPhase).isEvent);

    analysis::AnalysisFrame frame;
    frame.rms = 0.3f;
    frame.peak = 0.6f;
    frame.bandCount = 5;
    frame.bands = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.0f, 0.0f, 0.0f};
    frame.centroidNorm = 0.45f;
    frame.flux = 0.15f;
    frame.onsetStrength = 3.0f;
    frame.onset = true;
    frame.tempoBpm = 124.5f;
    frame.tempoConfidence = 0.8f;
    frame.beat = true;
    frame.beatPhase = 0.25f;
    frame.beatCount = 37;
    audio.publish(bus, frame);

    CHECK(bus.value(audio.rms) == 0.3f);
    CHECK(bus.value(audio.peak) == 0.6f);
    CHECK(bus.value(audio.bass) == 0.1f);
    CHECK(bus.value(audio.lowMid) == 0.2f);
    CHECK(bus.value(audio.mid) == 0.3f);
    CHECK(bus.value(audio.highMid) == 0.4f);
    CHECK(bus.value(audio.treble) == 0.5f);
    CHECK(bus.value(audio.centroid) == 0.45f);
    CHECK(bus.value(audio.flux) == 0.15f);
    CHECK(bus.value(audio.onsetStrength) == 3.0f);
    CHECK(bus.event(audio.onset));
    CHECK(bus.value(audio.onset) == 1.0f); // min(1, 3/2)
    CHECK(bus.value(audio.tempo) == 124.5f);
    CHECK(bus.value(audio.tempoConfidence) == 0.8f);
    CHECK(bus.event(audio.beat));
    CHECK(bus.value(audio.beat) == 1.0f);
    CHECK(bus.value(audio.beatPhase) == 0.25f);
    CHECK(bus.value(audio.beatCount) == 37.0f);

    // Fewer bands than signals: missing bands publish zero.
    frame.bandCount = 2;
    frame.onset = false;
    frame.onsetStrength = 1.0f;
    frame.beat = false;
    audio.publish(bus, frame);
    CHECK_FALSE(bus.event(audio.beat));
    CHECK(bus.value(audio.beat) == 0.0f);
    CHECK(bus.value(audio.beatCount) == 37.0f); // non-event values persist
    CHECK(bus.value(audio.bass) == 0.1f);
    CHECK(bus.value(audio.lowMid) == 0.2f);
    CHECK(bus.value(audio.mid) == 0.0f);
    CHECK(bus.value(audio.treble) == 0.0f);
    CHECK_FALSE(bus.event(audio.onset));
    CHECK(bus.value(audio.onset) == 0.0f);

    // Onset strength below 2 gives a proportional event value.
    frame.onset = true;
    audio.publish(bus, frame);
    CHECK_THAT(static_cast<double>(bus.value(audio.onset)), Catch::Matchers::WithinAbs(0.5, 1e-6));

    audio.publishSilence(bus);
    for (const SignalInfo& info : bus.infos()) {
        INFO(info.name);
        CHECK(bus.value(*bus.find(info.name)) == 0.0f);
    }
    CHECK_FALSE(bus.event(audio.onset));
    CHECK_FALSE(bus.event(audio.beat));
}
