// The transport through the engine (ADR-102): what Play, Pause, Stop, seek, loop and frame stepping
// actually do to the timeline, the parameters and the scene.
//
// `tests/unit/test_transport.cpp` checks the state machine on its own. This file checks the thing
// the state machine exists for -- that the rest of the engine moves with it -- and in particular the
// defect it was built to fix: before this, "playing" meant "the audio device is running", so a
// project with no audio could not be played, paused, seeked or stopped at all.

#include "app/engine.hpp"
#include "app/transport.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "params/timeline.hpp"
#include "seq/sequence.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>

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

// A live engine with no audio at all, driven by a fixed-step clock so the wall-clock deltas the
// transport integrates are deterministic. `EngineMode::Live` with no source opens no device, which
// is what makes this runnable on a machine with no sound card.
struct Silent {
    app::Engine engine{app::EngineMode::Live};
    FixedStepClock clock{60.0};

    FrameTime step() {
        const FrameTime time = engine.tick(clock);
        engine.update(time);
        return time;
    }
    void steps(int n) {
        for (int i = 0; i < n; ++i) {
            static_cast<void>(step());
        }
    }
    [[nodiscard]] double timelineSeconds() const { return engine.timelineClock().seconds; }
};

// A track that ramps a parameter linearly over the whole piece, so a test can ask "what time does
// the *scene* think it is" rather than "what time does the transport say it is". The two agreeing is
// the entire point of the transport, and a test that only asked the transport would pass with the
// timeline disconnected from it.
//
// `orb/scale` is clamped to 0.05..8, so the ramp is scaled into that range and read back out rather
// than writing seconds directly -- a track that writes 9.97 onto it evaluates perfectly and stores
// 8, which is a test that passes for the wrong reason at one end of the piece and fails at the other.
constexpr const char* kClockTarget = "orb/scale";
constexpr float kClockBase = 0.05f;
double clockGain(double duration) { return 7.9 / std::max(1.0, duration); }

void addClockTrack(app::Engine& engine, double duration) {
    const double gain = clockGain(duration);
    params::Track track;
    track.target = kClockTarget;
    track.addKey({.time = 0.0, .value = {kClockBase}, .interp = params::KeyInterp::Linear});
    track.addKey({.time = duration,
                  .value = {static_cast<float>(kClockBase + duration * gain)},
                  .interp = params::KeyInterp::Linear});
    engine.timeline().addTrack(track);
    engine.rebind();
}

// The second the scene is showing, read back out of that ramp.
double sceneSeconds(app::Engine& engine, double duration) {
    const params::IParameter* p = engine.params().find(kClockTarget);
    REQUIRE(p != nullptr);
    return (static_cast<double>(p->finalComponent(0)) - kClockBase) / clockGain(duration);
}

} // namespace

// ---- the headline ------------------------------------------------------------------------------

TEST_CASE("A project with no audio plays, pauses, seeks and stops", "[integration][transport]") {
    Silent s;
    REQUIRE_FALSE(s.engine.hasAudio());
    // A length from something other than a wav: this is what makes the piece a piece.
    s.engine.timeline().enabled = true;
    addClockTrack(s.engine, 20.0);
    s.engine.refreshTransport();
    CHECK(s.engine.durationSeconds() >= 20.0);

    // Play. Before the transport this returned "no audio loaded" and nothing moved.
    REQUIRE(s.engine.play().has_value());
    CHECK(s.engine.isPlaying());
    s.steps(60); // one second at sixty
    CHECK_THAT(s.timelineSeconds(), WithinAbs(1.0, 0.03));

    // Pause: the timeline stops. The render clock does not, and that is deliberate -- wind, water
    // and every other continuously animated thing keep moving while the piece is parked.
    s.engine.pause();
    CHECK_FALSE(s.engine.isPlaying());
    const double parked = s.timelineSeconds();
    const FrameTime a = s.step();
    s.steps(30);
    const FrameTime b = s.step();
    CHECK_THAT(s.timelineSeconds(), WithinAbs(parked, 1e-9));
    CHECK(b.renderTime > a.renderTime); // the render clock is still running

    // Seek while paused, and it *stays* seeked. This is the exact shape of the old defect: the
    // position was written and then overwritten by the free-running render clock on the next frame.
    s.engine.seekSeconds(5.0);
    CHECK_THAT(s.timelineSeconds(), WithinAbs(5.0, 1e-9));
    s.steps(30);
    CHECK_THAT(s.timelineSeconds(), WithinAbs(5.0, 1e-9));

    // And the scene is at that second, not merely the transport.
    s.steps(1);
    CHECK_THAT(sceneSeconds(s.engine, 20.0), WithinAbs(5.0, 1e-2));

    // Resume from where it was parked.
    REQUIRE(s.engine.play().has_value());
    s.steps(60);
    CHECK_THAT(s.timelineSeconds(), WithinAbs(6.0, 0.05));

    // Stop returns to the start and stays there.
    s.engine.stop();
    CHECK_FALSE(s.engine.isPlaying());
    CHECK_THAT(s.timelineSeconds(), WithinAbs(0.0, 1e-9));
    s.steps(30);
    CHECK_THAT(s.timelineSeconds(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("The project's length is the longest thing in it", "[integration][transport]") {
    const auto wav = writeSilence("avgen_transport_len.wav", 4.0);
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(wav).has_value());
    CHECK_THAT(engine.audioDurationSeconds(), WithinAbs(4.0, 0.01));
    CHECK_THAT(engine.durationSeconds(), WithinAbs(4.0, 0.01));

    // A timeline that runs past the audio makes the project longer. Before this, the project was
    // exactly as long as its wav and a sequence over a short piece of audio stopped early.
    params::Track track;
    track.target = "orb/scale";
    track.addKey({.time = 0.0, .value = {1.0f}});
    track.addKey({.time = 30.0, .value = {2.0f}});
    engine.timeline().addTrack(track);
    engine.rebind();
    engine.refreshTransport();
    CHECK_THAT(engine.durationSeconds(), WithinAbs(30.0, 0.01));
    CHECK_THAT(engine.audioDurationSeconds(), WithinAbs(4.0, 0.01)); // the file is still the file
}

// ---- seeking -------------------------------------------------------------------------------------

TEST_CASE("Seeking is clamped and the whole engine lands on the clamped second",
          "[integration][transport]") {
    Silent s;
    s.engine.timeline().enabled = true;
    addClockTrack(s.engine, 10.0);
    s.engine.refreshTransport();

    s.engine.seekSeconds(-100.0);
    CHECK_THAT(s.engine.positionSeconds(), WithinAbs(0.0, 1e-9));
    s.engine.seekSeconds(1e6);
    CHECK_THAT(s.engine.positionSeconds(), WithinAbs(s.engine.durationSeconds(), 1e-6));
    // The timeline follows the position that was *taken*, not the one that was asked for.
    s.steps(1);
    CHECK_THAT(s.timelineSeconds(), WithinAbs(s.engine.durationSeconds(), 1e-6));
}

TEST_CASE("Transport discontinuity revision changes only for moved seeks", "[integration][transport]") {
    Silent s;
    const std::uint64_t initial = s.engine.transport().discontinuityRevision();
    s.engine.seekSeconds(0.0);
    CHECK(s.engine.transport().discontinuityRevision() == initial);

    s.engine.seekSeconds(1.0);
    const std::uint64_t afterSeek = s.engine.transport().discontinuityRevision();
    CHECK(afterSeek > initial);
    s.engine.seekSeconds(1.0);
    CHECK(s.engine.transport().discontinuityRevision() == afterSeek);

    REQUIRE(s.engine.play().has_value());
    s.steps(2);
    CHECK(s.engine.transport().discontinuityRevision() == afterSeek);
}

TEST_CASE("Frame stepping moves the timeline by exactly one frame", "[integration][transport]") {
    Silent s;
    s.engine.timeline().enabled = true;
    addClockTrack(s.engine, 100.0);
    // Through the render settings, because that is where the project's frame rate lives: the frames
    // a person steps through are the frames the project exports, and there is deliberately not a
    // second number for it.
    s.engine.renderSettings().fps = 29.97;
    s.engine.refreshTransport();
    CHECK(s.engine.transport().frameRate() == app::FrameRate{30000, 1001});
    s.engine.seekSeconds(10.0);

    const std::int64_t start = s.engine.transport().frame();
    s.engine.stepFrames(1);
    CHECK(s.engine.transport().frame() == start + 1);
    s.steps(1);
    CHECK_THAT(s.timelineSeconds(), WithinAbs(s.engine.transport().secondsOfFrame(start + 1), 1e-9));

    s.engine.stepFrames(-1);
    CHECK(s.engine.transport().frame() == start);

    // Stepping is a seek, so everything a seek resynchronises moves with it -- including the scene.
    s.steps(1);
    CHECK_THAT(sceneSeconds(s.engine, 100.0),
               WithinAbs(s.engine.transport().secondsOfFrame(start), 1e-2));
}

TEST_CASE("Beat stepping lands on beats", "[integration][transport]") {
    Silent s;
    s.engine.refreshTransport();
    // No analyzed beat grid here, so the tempo is the grid. 120 bpm is a beat every half second.
    s.engine.transport().setTempo(120.0, 4);
    s.engine.seekSeconds(0.0);

    s.engine.stepBeats(1);
    CHECK_THAT(s.engine.positionSeconds(), WithinAbs(0.5, 1e-6));
    s.engine.stepBeats(3);
    CHECK_THAT(s.engine.positionSeconds(), WithinAbs(2.0, 1e-6));
    s.engine.stepBeats(-1);
    CHECK_THAT(s.engine.positionSeconds(), WithinAbs(1.5, 1e-6));
    // From part-way between two beats, forward goes to the next one rather than a beat further on.
    s.engine.seekSeconds(1.7);
    s.engine.stepBeats(1);
    CHECK_THAT(s.engine.positionSeconds(), WithinAbs(2.0, 1e-6));

    // With no tempo at all there is nothing to step to, and it says so by not moving.
    s.engine.transport().setTempo(0.0, 4);
    s.engine.seekSeconds(1.7);
    s.engine.stepBeats(1);
    CHECK_THAT(s.engine.positionSeconds(), WithinAbs(1.7, 1e-6));
}

// ---- looping ----------------------------------------------------------------------------------

TEST_CASE("A loop wraps the timeline and the scene comes back with it", "[integration][transport][loop]") {
    Silent s;
    s.engine.timeline().enabled = true;
    addClockTrack(s.engine, 20.0);
    s.engine.refreshTransport();
    REQUIRE(s.engine.transport().setLoop(app::TransportLoop{true, 2.0, 3.0}));
    s.engine.seekSeconds(2.9);
    REQUIRE(s.engine.play().has_value());

    // Ten frames at sixty is a sixth of a second, which crosses the loop end at 3.0.
    s.steps(10);
    CHECK(s.timelineSeconds() < 3.0);
    CHECK(s.timelineSeconds() >= 2.0);
    CHECK(s.engine.isPlaying()); // a wrap is not the end of playback
    // The scene is where the *wrapped* second says, which is what proves the wrap resynchronised the
    // engine rather than only moving a number.
    CHECK_THAT(sceneSeconds(s.engine, 20.0), WithinAbs(s.timelineSeconds(), 2e-2));

    // Round and round without drifting out of the range.
    for (int i = 0; i < 600; ++i) {
        static_cast<void>(s.step());
        REQUIRE(s.timelineSeconds() >= 2.0 - 1e-6);
        REQUIRE(s.timelineSeconds() <= 3.0 + 1e-6);
    }

    // Turning the loop off lets the piece run on past where the loop used to end.
    s.engine.transport().setLoopEnabled(false);
    s.steps(120);
    CHECK(s.timelineSeconds() > 3.0);
}

// ---- rate ---------------------------------------------------------------------------------------

TEST_CASE("A playback rate scales how fast the piece goes past", "[integration][transport]") {
    Silent s;
    s.engine.timeline().enabled = true;
    addClockTrack(s.engine, 60.0);
    s.engine.refreshTransport();
    REQUIRE(s.engine.play().has_value());

    s.steps(60);
    const double atUnit = s.timelineSeconds();
    CHECK_THAT(atUnit, WithinAbs(1.0, 0.05));

    s.engine.transport().setRate(2.0);
    s.steps(60);
    CHECK_THAT(s.timelineSeconds() - atUnit, WithinAbs(2.0, 0.05));

    s.engine.transport().setRate(0.5);
    const double before = s.timelineSeconds();
    s.steps(60);
    CHECK_THAT(s.timelineSeconds() - before, WithinAbs(0.5, 0.05));
}

// ---- offline ------------------------------------------------------------------------------------

TEST_CASE("Offline evaluation ignores the transport's loop, rate and playing state",
          "[integration][transport][offline]") {
    // An export must not be able to pick up a loop somebody set while previewing, and it must not
    // depend on whether the transport happens to be playing.
    app::Engine engine(app::EngineMode::Offline);
    engine.timeline().enabled = true;
    addClockTrack(engine, 100.0);
    engine.refreshTransport();
    engine.transport().setLoop(app::TransportLoop{true, 1.0, 2.0});
    engine.transport().setRate(4.0);
    CHECK_FALSE(engine.isPlaying());

    FixedStepClock clock(30.0);
    double last = -1.0;
    for (int i = 0; i < 300; ++i) { // ten seconds, well past the loop
        const FrameTime time = engine.tick(clock);
        engine.update(time);
        // The timeline is the clock's, frame for frame.
        REQUIRE_THAT(engine.timelineClock().seconds, WithinAbs(time.renderTime, 1e-9));
        REQUIRE(time.renderTime > last);
        last = time.renderTime;
    }
    CHECK_THAT(last, WithinAbs(299.0 / 30.0, 1e-9));
    CHECK_THAT(sceneSeconds(engine, 100.0), WithinAbs(last, 2e-2));
}

TEST_CASE("The same offline frame evaluates the same way however it was reached",
          "[integration][transport][offline]") {
    // Determinism as a property of the position rather than of the route to it: a render that starts
    // at 0 and a render that starts at 7 have to agree about second 7.
    const auto evaluateAt = [](double start, int frames) {
        app::Engine engine(app::EngineMode::Offline);
        engine.timeline().enabled = true;
        addClockTrack(engine, 100.0);
        engine.refreshTransport();
        FixedStepClock clock(30.0);
        clock.restartAt(start);
        engine.seekSeconds(start);
        FrameTime time{};
        for (int i = 0; i < frames; ++i) {
            time = engine.tick(clock);
            engine.update(time);
        }
        return std::pair{time.renderTime, sceneSeconds(engine, 100.0)};
    };

    const auto walked = evaluateAt(0.0, 211);  // frame 210 of a render that began at 0
    const auto jumped = evaluateAt(7.0, 1);    // frame 0 of a render that began at 7
    CHECK_THAT(walked.first, WithinAbs(7.0, 1e-9));
    CHECK_THAT(jumped.first, WithinAbs(7.0, 1e-9));
    CHECK_THAT(walked.second, WithinAbs(jumped.second, 1e-6));
}

// ---- the project ----------------------------------------------------------------------------------

TEST_CASE("The loop is saved with the project and a project without one clears it",
          "[integration][transport][project]") {
    const auto dir = std::filesystem::temp_directory_path() / "avgen_transport_project";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    const auto saved = dir / "looped.avgen.json";
    {
        app::Engine engine(app::EngineMode::Offline);
        engine.timeline().enabled = true;
        addClockTrack(engine, 50.0);
        engine.refreshTransport();
        REQUIRE(engine.transport().setLoop(app::TransportLoop{true, 12.0, 18.0}));
        REQUIRE(engine.saveProject(saved).has_value());
    }

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(saved).has_value());
    CHECK(engine.transport().loop().enabled);
    CHECK_THAT(engine.transport().loop().startSeconds, WithinAbs(12.0, 1e-6));
    CHECK_THAT(engine.transport().loop().endSeconds, WithinAbs(18.0, 1e-6));
    // Loading parks the playhead at the start of the piece rather than carrying the last one's.
    CHECK_THAT(engine.positionSeconds(), WithinAbs(12.0, 1e-6)); // the loop start is the play start
    CHECK_FALSE(engine.isPlaying());

    // A project with no transport block clears the loop rather than inheriting the one already set:
    // a range from another piece must not quietly govern this one.
    const auto plain = dir / "plain.avgen.json";
    {
        app::Engine other(app::EngineMode::Offline);
        REQUIRE(other.saveProject(plain).has_value());
    }
    REQUIRE(engine.loadProject(plain).has_value());
    CHECK_FALSE(engine.transport().loop().enabled);
    CHECK_FALSE(engine.transport().loop().usable());

    // A project that never had a loop does not grow a "transport" key, so files written before this
    // existed round-trip byte for byte.
    std::ifstream in(plain);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find("\"transport\"") == std::string::npos);

    std::filesystem::remove_all(dir);
}

TEST_CASE("The project's frame rate is the one it renders at", "[integration][transport][project]") {
    // Deliberately one number, not two: a frame step that moved by something other than an exported
    // frame would be a stepper that lies about which frame you are looking at.
    app::Engine engine(app::EngineMode::Offline);
    engine.renderSettings().fps = 24.0;
    engine.refreshTransport();
    CHECK(engine.transport().frameRate() == app::FrameRate{24, 1});
    CHECK_THAT(engine.transport().secondsOfFrame(24), WithinAbs(1.0, 1e-12));

    engine.renderSettings().fps = 59.94;
    engine.refreshTransport();
    CHECK(engine.transport().frameRate() == app::FrameRate{60000, 1001});
}

// ---- cost ---------------------------------------------------------------------------------------

TEST_CASE("Refreshing the transport costs nothing per frame", "[integration][transport][performance]") {
    // `refreshTransport` runs once per tick, and it walks the timeline's tracks and the sequence's
    // shots, actors, overlays and events to find the longest thing in the project. That is a scan,
    // per frame, and a scan per frame is exactly the kind of thing that is free in a test project and
    // expensive in a real one -- so it is measured against a piece far larger than any real one
    // rather than assumed.
    app::Engine engine(app::EngineMode::Offline);
    for (int t = 0; t < 400; ++t) {
        params::Track track;
        track.target = "orb/scale";
        for (int k = 0; k < 50; ++k) {
            track.addKey({.time = static_cast<double>(k) * 2.0, .value = {1.0f}});
        }
        engine.timeline().addTrack(std::move(track));
    }
    for (int i = 0; i < 400; ++i) {
        seq::Shot shot;
        shot.startSeconds = static_cast<double>(i);
        shot.durationSeconds = 2.0;
        engine.sequence().shots.push_back(shot);
    }
    REQUIRE(engine.timeline().tracks().size() == 400);

    constexpr int kCalls = 2000;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kCalls; ++i) {
        engine.refreshTransport();
    }
    const double micros =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / kCalls;
    WARN("refreshTransport over 400 tracks x 50 keys and 400 shots: " << micros << " us");
    // A frame at sixty is 16,667 us. A hundred is six thousandths of one, on a piece an order of
    // magnitude larger than night-shift -- and the ceiling is what says so if that ever stops
    // being true.
    CHECK(micros < 100.0);
}

TEST_CASE("Marker navigation jumps between places, not beats", "[integration][transport]") {
    Silent s;
    s.engine.timeline().enabled = true;
    addClockTrack(s.engine, 60.0);
    s.engine.refreshTransport();

    s.engine.sequence().markers.push_back({.timeSeconds = 5.0, .name = "verse", .kind = seq::MarkerKind::Section});
    s.engine.sequence().markers.push_back({.timeSeconds = 20.0, .name = "drop", .kind = seq::MarkerKind::Cue});
    // The beat grid: thousands of these get drawn on the strip, and "the next marker" does not mean
    // "the next beat". They are skipped.
    for (int i = 0; i < 100; ++i) {
        s.engine.sequence().markers.push_back(
            {.timeSeconds = static_cast<double>(i) * 0.5, .name = {}, .kind = seq::MarkerKind::Beat});
    }

    s.engine.seekSeconds(0.0);
    s.engine.stepMarkers(1);
    CHECK_THAT(s.engine.positionSeconds(), WithinAbs(5.0, 1e-6));
    s.engine.stepMarkers(1);
    CHECK_THAT(s.engine.positionSeconds(), WithinAbs(20.0, 1e-6));
    // Past the last one, it stays put rather than running off the end.
    s.engine.stepMarkers(1);
    CHECK_THAT(s.engine.positionSeconds(), WithinAbs(20.0, 1e-6));
    s.engine.stepMarkers(-1);
    CHECK_THAT(s.engine.positionSeconds(), WithinAbs(5.0, 1e-6));
    s.engine.stepMarkers(-1);
    CHECK_THAT(s.engine.positionSeconds(), WithinAbs(5.0, 1e-6));

    // And the scene moved with it, because a marker jump is a seek like any other.
    s.steps(1);
    CHECK_THAT(sceneSeconds(s.engine, 60.0), WithinAbs(5.0, 2e-2));
}
