// Milestone 0.3 integration: sources, time/beat signals and projects through the Engine.
#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "params/serialization.hpp"
#include "signals/source.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace avgen;
using Catch::Matchers::WithinAbs;

TEST_CASE("An LFO source modulates the orb without any audio", "[integration][sources]") {
    app::Engine engine(app::EngineMode::Offline);
    auto& lfo = engine.addSource("lfo", "wobble");
    CHECK(lfo.kind() == "lfo");
    CHECK(engine.params().find("sources/wobble/rate") != nullptr);
    engine.params().findAs<float>("sources/wobble/rate")->setBase(1.0f); // 1 Hz
    params::ModRoute route{.source = "lfo.wobble", .target = "orb/scale", .amount = 1.0f};
    engine.modulator().addRoute(route);
    engine.rebind();

    FixedStepClock clock(100.0);
    float minScale = 10.0f;
    float maxScale = 0.0f;
    for (int i = 0; i < 100; ++i) { // one full cycle at 1 Hz
        engine.update(engine.tick(clock));
        const float s = engine.orbScene()->scale().value();
        minScale = std::min(minScale, s);
        maxScale = std::max(maxScale, s);
    }
    CHECK_THAT(static_cast<double>(minScale), WithinAbs(1.0, 0.02));
    CHECK_THAT(static_cast<double>(maxScale), WithinAbs(2.0, 0.02));

    // Sources are functions of time: seeking back reproduces the same value.
    engine.seekSeconds(0.0);
    clock.seek(0.24); // tick() advances one 10 ms step, landing exactly on 0.25 s
    engine.update(engine.tick(clock));
    const float atQuarter = engine.signals().value(*engine.signals().find("lfo.wobble"));
    CHECK_THAT(static_cast<double>(atQuarter), WithinAbs(0.5, 1e-3));

    engine.removeSource("lfo", "wobble");
    CHECK(engine.params().find("sources/wobble/rate") == nullptr);
}

TEST_CASE("Modulators of modulators: an LFO drives another LFO's rate", "[integration][sources]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.addSource("lfo", "slow");
    engine.addSource("lfo", "fast");
    engine.params().findAs<float>("sources/slow/rate")->setBase(0.25f);
    engine.params().findAs<float>("sources/fast/rate")->setBase(1.0f);
    params::ModRoute route{.source = "lfo.slow", .target = "sources/fast/rate", .amount = 4.0f};
    engine.modulator().addRoute(route);
    engine.rebind();
    FixedStepClock clock(60.0);
    for (int i = 0; i < 120; ++i) {
        engine.update(engine.tick(clock));
    }
    // After 2 s the slow LFO (0.25 Hz sine) is at its peak: fast rate = 1 + 4 * 1 = 5 Hz.
    CHECK(engine.params().find("sources/fast/rate")->finalComponent(0) > 4.5f);
}

TEST_CASE("Project round trip through the engine restores sources, routes, presets and values", "[integration][project]") {
    const auto path = std::filesystem::temp_directory_path() / "avgen_project_roundtrip.json";
    {
        app::Engine engine(app::EngineMode::Offline);
        engine.addSource("lfo", "wobble");
        engine.addSource("envelope", "hit");
        engine.params().findAs<float>("sources/wobble/rate")->setBase(2.5f);
        engine.orbScene()->scale().setBase(1.7f);
        params::ModRoute route{.source = "lfo.wobble", .target = "orb/emissive", .amount = 3.0f, .polarity = params::Polarity::Bipolar};
        engine.modulator().addRoute(route);
        engine.rebind();
        engine.storePreset("bright");
        engine.orbScene()->scale().setBase(0.5f);
        engine.storePreset("small");
        REQUIRE(engine.saveProject(path).has_value());
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(path).has_value());
    CHECK(engine.sources().find("lfo", "wobble") != nullptr);
    CHECK(engine.sources().find("envelope", "hit") != nullptr);
    REQUIRE(engine.params().find("sources/wobble/rate") != nullptr);
    CHECK_THAT(static_cast<double>(engine.params().find("sources/wobble/rate")->baseComponent(0)), WithinAbs(2.5, 1e-6));
    CHECK_THAT(static_cast<double>(engine.orbScene()->scale().base()), WithinAbs(0.5, 1e-6));
    bool found = false;
    for (const auto& r : engine.modulator().routes()) {
        if (r.source == "lfo.wobble" && r.target == "orb/emissive") {
            found = true;
            CHECK(r.polarity == params::Polarity::Bipolar);
            CHECK(r.amount == 3.0f);
        }
    }
    CHECK(found);
    CHECK(engine.presets().find("bright") != nullptr);
    CHECK(engine.recallPreset("bright"));
    CHECK_THAT(static_cast<double>(engine.orbScene()->scale().base()), WithinAbs(1.7, 1e-6));
    engine.morphPresets("bright", "small", 0.5f);
    CHECK_THAT(static_cast<double>(engine.orbScene()->scale().base()), WithinAbs(1.1, 1e-5));
    // The loaded LFO still drives the bus after rebind.
    FixedStepClock clock(60.0);
    for (int i = 0; i < 10; ++i) {
        engine.update(engine.tick(clock));
    }
    CHECK(engine.modulator().bound());
    std::filesystem::remove(path);
}

TEST_CASE("Beat clock signals follow a click track offline", "[integration][beat]") {
    constexpr std::uint32_t rate = 48000;
    auto mono = testsupport::clickTrack(120.0f, rate, rate * 12);
    auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(mono, 2), 2, rate);
    const auto wav = std::filesystem::temp_directory_path() / "avgen_beat_clock.wav";
    REQUIRE(file.writeWav(wav).has_value());
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(wav).has_value());
    const auto& ts = engine.timeSignals();
    FixedStepClock clock(60.0);
    // Events are cleared at the end of update() (they are for routes); observe the beat counter.
    int pulses = 0;
    float lastCount = 0.0f;
    float lastPhase = 0.0f;
    int wraps = 0;
    for (int i = 0; i < 60 * 12; ++i) {
        engine.update(engine.tick(clock));
        const float count = engine.signals().value(ts.beatCount);
        if (count > lastCount) {
            ++pulses;
        }
        lastCount = count;
        const float phase = engine.signals().value(ts.beatPhase);
        if (phase < lastPhase - 0.5f) {
            ++wraps;
        }
        lastPhase = phase;
    }
    const float bpm = engine.signals().value(ts.bpm);
    INFO("bpm=" << bpm << " pulses=" << pulses << " wraps=" << wraps);
    CHECK_THAT(static_cast<double>(bpm), WithinAbs(120.0, 3.0));
    CHECK(pulses >= 18); // 12 s at 120 BPM = 24 beats, minus warm-up
    CHECK(wraps >= 18);
    CHECK(engine.signals().value(ts.seconds) > 11.0f);
    std::filesystem::remove(wav);
}
