// Milestone 0.8: the timeline through the engine (ADR-018): keyed values at exact times, routes
// stacking on automation, beat-based loops, cues recalling presets, seeks, projects and scene swaps.

#include "app/engine.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "params/timeline.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

std::filesystem::path writeSilence(const char* name, double seconds) {
    constexpr std::uint32_t rate = 48000;
    auto mono = testsupport::silence(static_cast<std::size_t>(rate * seconds));
    auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(mono, 2), 2, rate);
    const auto wav = std::filesystem::temp_directory_path() / name;
    REQUIRE(file.writeWav(wav).has_value());
    return wav;
}

// Runs the offline engine until the render time reaches `seconds` (inclusive of that frame).
void runUntil(app::Engine& engine, FixedStepClock& clock, double seconds) {
    for (;;) {
        const auto time = engine.tick(clock);
        engine.update(time);
        if (time.renderTime >= seconds - 1e-9) {
            return;
        }
    }
}

} // namespace

TEST_CASE("Timeline tracks drive finals at exact times and routes stack on top", "[integration][timeline]") {
    const auto wav = writeSilence("avgen_timeline_silence.wav", 8.0);
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(wav).has_value());
    auto* scale = engine.params().find("orb/scale");
    REQUIRE(scale != nullptr);
    const float base = scale->baseComponent(0);

    params::Track track;
    track.target = "orb/scale";
    track.addKey({.time = 1.0, .value = {1.0f}, .interp = params::KeyInterp::Linear});
    track.addKey({.time = 3.0, .value = {3.0f}, .interp = params::KeyInterp::Step});
    engine.timeline().addTrack(track);
    engine.rebind();
    CHECK(engine.timeline().isAutomated("orb/scale"));

    FixedStepClock clock(50.0); // 20 ms steps hit whole seconds exactly
    runUntil(engine, clock, 0.5);
    CHECK_THAT(scale->finalComponent(0), WithinAbs(1.0, 1e-5)); // before the first key: its value
    runUntil(engine, clock, 2.0);
    CHECK_THAT(scale->finalComponent(0), WithinAbs(2.0, 1e-5)); // linear midpoint
    runUntil(engine, clock, 4.0);
    CHECK_THAT(scale->finalComponent(0), WithinAbs(3.0, 1e-5)); // holds after the last key
    CHECK(scale->baseComponent(0) == base);                       // base untouched

    // A second track in add mode stacks on the replaced value, like a route would.
    params::Track add;
    add.target = "orb/scale";
    add.mode = params::TrackMode::Add;
    add.addKey({.time = 0.0, .value = {0.5f}});
    engine.timeline().addTrack(add);
    engine.rebind();
    runUntil(engine, clock, 6.0);
    CHECK_THAT(scale->finalComponent(0), WithinAbs(3.5, 1e-5));

    engine.timeline().enabled = false;
    runUntil(engine, clock, 7.0);
    CHECK_THAT(scale->finalComponent(0), WithinAbs(static_cast<double>(base), 1e-5));
    std::filesystem::remove(wav);
}

TEST_CASE("Beat-based tracks loop with the click track", "[integration][timeline][beat]") {
    constexpr std::uint32_t rate = 48000;
    auto mono = testsupport::clickTrack(120.0f, rate, rate * 12);
    auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(mono, 2), 2, rate);
    const auto wav = std::filesystem::temp_directory_path() / "avgen_timeline_click.wav";
    REQUIRE(file.writeWav(wav).has_value());
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(wav).has_value());
    auto* emissive = engine.params().find("orb/emissive");
    REQUIRE(emissive != nullptr);

    params::Track track;
    track.target = "orb/emissive";
    track.timeBase = params::TimeBase::Beats;
    track.loopLength = 1.0;
    track.addKey({.time = 0.0, .value = {2.0f}, .interp = params::KeyInterp::Linear});
    track.addKey({.time = 1.0, .value = {0.0f}, .interp = params::KeyInterp::Linear});
    engine.timeline().addTrack(track);
    engine.rebind();

    FixedStepClock clock(60.0);
    // Once the tempo locks (a few seconds in), the value saws down once per beat.
    int falls = 0;
    float last = -1.0f;
    float maxSeen = 0.0f;
    for (int i = 0; i < 60 * 12; ++i) {
        engine.update(engine.tick(clock));
        const float v = emissive->finalComponent(0);
        if (i > 60 * 4) {
            if (v > last + 0.5f) {
                ++falls; // wrapped back up to the loop start
            }
            maxSeen = std::max(maxSeen, v);
        }
        last = v;
    }
    INFO("wraps=" << falls << " max=" << maxSeen);
    CHECK(falls >= 12); // 8 seconds at 120 BPM = 16 beats; allow for lock-in
    CHECK(maxSeen > 1.5f);
    std::filesystem::remove(wav);
}

TEST_CASE("Cues recall presets, morph over time and re-sync after seeks", "[integration][timeline]") {
    const auto wav = writeSilence("avgen_timeline_cues.wav", 10.0);
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(wav).has_value());
    auto* scale = engine.params().find("orb/scale");
    REQUIRE(scale != nullptr);
    scale->setBaseComponent(0, 1.0f);
    engine.storePreset("small");
    scale->setBaseComponent(0, 3.0f);
    engine.storePreset("big");
    scale->setBaseComponent(0, 1.0f);

    engine.timeline().addCue({.time = 2.0, .name = "drop", .preset = "big", .morphSeconds = 1.0});
    engine.timeline().addCue({.time = 6.0, .name = "calm", .preset = "small"});
    engine.timeline().addCue({.time = 4.0, .name = "marker"}); // sorted insert, no preset

    FixedStepClock clock(50.0);
    runUntil(engine, clock, 1.0);
    CHECK_THAT(scale->baseComponent(0), WithinAbs(1.0, 1e-5));
    CHECK(engine.cueState().index == -1);
    runUntil(engine, clock, 2.5);
    CHECK(engine.cueState().index == 0);
    CHECK_THAT(scale->baseComponent(0), WithinAbs(2.0, 1e-4)); // halfway through the morph
    runUntil(engine, clock, 3.5);
    CHECK_THAT(scale->baseComponent(0), WithinAbs(3.0, 1e-5));
    // A user edit after the cue has fully applied is kept (the cue does not keep writing).
    scale->setBaseComponent(0, 2.5f);
    runUntil(engine, clock, 4.5);
    CHECK(engine.cueState().index == 1); // the marker
    CHECK_THAT(scale->baseComponent(0), WithinAbs(2.5, 1e-5));
    runUntil(engine, clock, 6.5);
    CHECK(engine.cueState().index == 2);
    CHECK_THAT(scale->baseComponent(0), WithinAbs(1.0, 1e-5));

    // Seek back before the second cue: the first cue re-applies (instantly, morph elapsed).
    engine.seekSeconds(3.0);
    clock.seek(3.0);
    runUntil(engine, clock, 3.5);
    CHECK(engine.cueState().index == 0);
    CHECK_THAT(scale->baseComponent(0), WithinAbs(3.0, 1e-5));
    std::filesystem::remove(wav);
}

TEST_CASE("Projects round-trip the timeline and scene swaps rebind tracks", "[integration][timeline][json]") {
    const auto wav = writeSilence("avgen_timeline_project.wav", 4.0);
    const auto project = std::filesystem::temp_directory_path() / "avgen_timeline_project.json";
    {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadAudio(wav).has_value());
        params::Track track;
        track.target = "orb/scale";
        track.addKey({.time = 0.0, .value = {1.0f}, .interp = params::KeyInterp::EaseInOut});
        track.addKey({.time = 2.0, .value = {2.0f}, .interp = params::KeyInterp::Bezier, .tangentIn = {-1.0f}});
        engine.timeline().addTrack(track);
        engine.storePreset("p");
        engine.timeline().addCue({.time = 1.0, .name = "c", .preset = "p", .morphSeconds = 0.25});
        REQUIRE(engine.saveProject(project).has_value());
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(wav).has_value());
    REQUIRE(engine.loadProject(project).has_value());
    REQUIRE(engine.timeline().tracks().size() == 1);
    const auto& t = engine.timeline().tracks()[0];
    CHECK(t.target == "orb/scale");
    CHECK(t.param != nullptr); // bound on load
    REQUIRE(t.keys.size() == 2);
    CHECK(t.keys[1].interp == params::KeyInterp::Bezier);
    CHECK(t.keys[1].tangentIn[0] == -1.0f);
    REQUIRE(engine.timeline().cues().size() == 1);
    CHECK(engine.timeline().cues()[0].preset == "p");

    // Swapping to a scene without orb/scale leaves the track unbound; back to the orb rebinds.
    engine.newComposition();
    CHECK(engine.timeline().tracks()[0].param == nullptr);
    CHECK_FALSE(engine.timeline().isAutomated("orb/scale"));
    FixedStepClock clock(60.0);
    engine.update(engine.tick(clock)); // must not touch a dangling parameter
    engine.loadOrbScene();
    CHECK(engine.timeline().tracks()[0].param != nullptr);
    CHECK(engine.timeline().isAutomated("orb/scale"));

    // recordKey through the engine keys the base value at the current time.
    engine.params().find("orb/scale")->setBaseComponent(0, 1.75f);
    auto* recorded = engine.recordKey("orb/scale");
    REQUIRE(recorded != nullptr);
    CHECK(recorded == &engine.timeline().tracks()[0]);
    CHECK(engine.recordKey("nope/missing") == nullptr);
    std::filesystem::remove(wav);
    std::filesystem::remove(project);
}
