// The transport (ADR-102): the playback state machine, the frame arithmetic, the loop and the
// displays.
//
// None of this needs an engine, a device or a window, which is the whole reason `app::Transport`
// takes elapsed time rather than reading a clock. Everything a person can do to the playhead is a
// call in this file.

#include "app/transport.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace avgen;
using app::FrameRate;
using app::Transport;
using app::TransportLoop;
using app::TransportState;
using Catch::Matchers::WithinAbs;

namespace {

Transport made(double duration = 100.0) {
    Transport t;
    t.setDuration(duration);
    return t;
}

} // namespace

// ---- the state machine --------------------------------------------------------------------------

TEST_CASE("Every transport command has an answer in every state", "[transport]") {
    Transport t = made();
    CHECK(t.state() == TransportState::Stopped);
    CHECK_FALSE(t.isPlaying());

    SECTION("stopped -> playing -> paused -> playing") {
        CHECK(t.play());
        CHECK(t.isPlaying());
        CHECK_FALSE(t.play()); // already playing: no change, and it says so
        CHECK(t.pause());
        CHECK(t.state() == TransportState::Paused);
        CHECK_FALSE(t.pause());
        CHECK(t.play());
        CHECK(t.isPlaying());
    }

    SECTION("pause holds the position and stop returns to the start") {
        t.play();
        static_cast<void>(t.advance(3.0));
        CHECK_THAT(t.positionSeconds(), WithinAbs(3.0, 1e-9));
        t.pause();
        CHECK_THAT(t.positionSeconds(), WithinAbs(3.0, 1e-9)); // a pause is not a rewind
        static_cast<void>(t.advance(1.0));
        CHECK_THAT(t.positionSeconds(), WithinAbs(3.0, 1e-9)); // and time does not pass while paused
        CHECK(t.stop());
        CHECK(t.state() == TransportState::Stopped);
        CHECK_THAT(t.positionSeconds(), WithinAbs(0.0, 1e-9));
        CHECK_FALSE(t.stop()); // stopped at the start already
    }

    SECTION("stop in place leaves the playhead where it is") {
        t.play();
        static_cast<void>(t.advance(4.0));
        CHECK(t.stopInPlace());
        CHECK(t.state() == TransportState::Stopped);
        CHECK_THAT(t.positionSeconds(), WithinAbs(4.0, 1e-9));
    }

    SECTION("play from the end starts again rather than playing nothing") {
        t.seek(100.0);
        CHECK(t.play());
        CHECK_THAT(t.positionSeconds(), WithinAbs(0.0, 1e-9));
    }

    SECTION("toggle is play or pause, whichever is not the case") {
        CHECK(t.togglePlay());
        CHECK(t.isPlaying());
        CHECK(t.togglePlay());
        CHECK(t.state() == TransportState::Paused);
    }
}

TEST_CASE("A command that changes nothing does not move the revision", "[transport]") {
    // The revision is what a view refreshes on. One that ticked for every call would refresh every
    // frame and mean nothing.
    Transport t = made();
    t.play();
    const std::uint64_t before = t.revision();
    CHECK_FALSE(t.play());
    CHECK(t.revision() == before);
    CHECK_FALSE(t.setRate(1.0));
    CHECK(t.revision() == before);
    t.seek(t.positionSeconds());
    CHECK(t.revision() == before);

    CHECK(t.setRate(2.0)); // the control: a real change does move it
    CHECK(t.revision() > before);
}

// ---- time ------------------------------------------------------------------------------------

TEST_CASE("Seeking clamps to the piece and works in every state", "[transport]") {
    Transport t = made(10.0);
    CHECK_THAT(t.seek(-5.0), WithinAbs(0.0, 1e-9));
    CHECK_THAT(t.seek(4.0), WithinAbs(4.0, 1e-9));
    CHECK_THAT(t.seek(1e6), WithinAbs(10.0, 1e-9));
    CHECK_THAT(t.seek(std::nan("")), WithinAbs(10.0, 1e-9)); // refused, not propagated

    t.play();
    CHECK_THAT(t.seek(2.0), WithinAbs(2.0, 1e-9));
    CHECK(t.isPlaying()); // seeking does not stop playback
    t.pause();
    CHECK_THAT(t.seekRelative(1.5), WithinAbs(3.5, 1e-9));
    CHECK_THAT(t.seekRelative(-100.0), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Frame and time convert exactly at every rate the engine supports", "[transport]") {
    struct Case {
        double fps;
        int numerator;
        int denominator;
    };
    const Case cases[] = {{24.0, 24, 1},        {25.0, 25, 1},   {29.97, 30000, 1001},
                          {30.0, 30, 1},        {50.0, 50, 1},   {59.94, 60000, 1001},
                          {60.0, 60, 1},        {23.976, 24000, 1001}};
    for (const Case& c : cases) {
        INFO(c.fps);
        const FrameRate rate = FrameRate::fromFps(c.fps);
        // A rate is the rational it is, not the decimal it is spelled with.
        CHECK(rate.numerator == c.numerator);
        CHECK(rate.denominator == c.denominator);

        Transport t = made(10000.0);
        t.setFrameRate(rate);

        CHECK(t.frameOf(0.0) == 0);
        CHECK_THAT(t.secondsOfFrame(0), WithinAbs(0.0, 1e-12));

        for (std::int64_t frame : {1, 2, 29, 30, 1000, 100000}) {
            INFO(frame);
            const double seconds = t.secondsOfFrame(frame);
            // The round trip, which is the property everything else rests on.
            CHECK(t.frameOf(seconds) == frame);
            // Inside the frame is still the frame; a hair before its start is the one before.
            CHECK(t.frameOf(seconds + 0.4 / rate.fps()) == frame);
            CHECK(t.frameOf(seconds - 1e-7) == frame - 1);
        }
    }
}

TEST_CASE("Frame stepping walks one frame at a time and comes back", "[transport]") {
    Transport t = made(100.0);
    t.setFrameRate(FrameRate::fromFps(29.97));
    t.seek(10.0);
    const std::int64_t start = t.frame();
    for (int i = 0; i < 5; ++i) {
        t.stepFrames(1);
    }
    CHECK(t.frame() == start + 5);
    for (int i = 0; i < 5; ++i) {
        t.stepFrames(-1);
    }
    CHECK(t.frame() == start);

    // From part-way through a frame, forward lands on the next one rather than on this one again.
    t.seek(t.secondsOfFrame(start) + 0.5 / 29.97);
    CHECK(t.frame() == start);
    t.stepFrames(1);
    CHECK(t.frame() == start + 1);

    // And it stops at the ends rather than running off them.
    t.seek(0.0);
    t.stepFrames(-10);
    CHECK_THAT(t.positionSeconds(), WithinAbs(0.0, 1e-9));
    t.seek(100.0);
    t.stepFrames(10);
    CHECK_THAT(t.positionSeconds(), WithinAbs(100.0, 1e-9));
}

TEST_CASE("A long piece keeps frame accuracy at its far end", "[transport]") {
    // Three hours at 59.94. A float position would have lost a whole frame here by about the
    // twenty-minute mark; this is the check that says why the position is a double.
    Transport t = made(3.0 * 3600.0);
    t.setFrameRate(FrameRate::fromFps(59.94));
    const std::int64_t frame = t.frameOf(3.0 * 3600.0 - 1.0);
    const double seconds = t.secondsOfFrame(frame);
    CHECK(t.frameOf(seconds) == frame);
    t.seek(seconds);
    t.stepFrames(1);
    CHECK(t.frame() == frame + 1);
}

// ---- advancing --------------------------------------------------------------------------------

TEST_CASE("Playback advances by elapsed time and stops at the end", "[transport]") {
    Transport t = made(2.0);
    t.play();
    auto tick = t.advance(0.5);
    CHECK_THAT(tick.positionSeconds, WithinAbs(0.5, 1e-9));
    CHECK_FALSE(tick.reachedEnd);

    tick = t.advance(5.0);
    CHECK_THAT(tick.positionSeconds, WithinAbs(2.0, 1e-9));
    CHECK(tick.reachedEnd);
    // Parked at the end rather than still claiming to play.
    CHECK(t.state() == TransportState::Paused);
    CHECK_FALSE(t.isPlaying());
}

TEST_CASE("A rate scales how fast the piece goes past", "[transport]") {
    Transport t = made(100.0);
    t.play();
    t.setRate(2.0);
    static_cast<void>(t.advance(1.0));
    CHECK_THAT(t.positionSeconds(), WithinAbs(2.0, 1e-9));
    t.setRate(0.25);
    static_cast<void>(t.advance(1.0));
    CHECK_THAT(t.positionSeconds(), WithinAbs(2.25, 1e-9));
    // Out-of-range rates are clamped rather than taken: a rate of zero is a stalled transport that
    // still claims to be playing, and a negative one is a feature nothing else here implements.
    t.setRate(0.0);
    CHECK(t.rate() > 0.0);
    t.setRate(1e9);
    CHECK(t.rate() <= 16.0);
}

TEST_CASE("Following a master takes its position, not a delta", "[transport]") {
    // Audio is the master clock while it runs (ADR-012). The transport does not integrate then; it
    // reads the play-head, so a device that stalls or jitters cannot make the visuals drift.
    Transport t = made(100.0);
    t.play();
    auto tick = t.follow(3.25);
    CHECK_THAT(tick.positionSeconds, WithinAbs(3.25, 1e-9));
    tick = t.follow(3.30);
    CHECK_THAT(tick.positionSeconds, WithinAbs(3.30, 1e-9));
    // While paused a master report is ignored: the device is not running, and whatever it last said
    // must not drag the playhead off where the user put it.
    t.pause();
    t.seek(50.0);
    static_cast<void>(t.follow(3.30));
    CHECK_THAT(t.positionSeconds(), WithinAbs(50.0, 1e-9));
}

// ---- looping ----------------------------------------------------------------------------------

TEST_CASE("A loop wraps, repeatedly, without losing time", "[transport][loop]") {
    Transport t = made(100.0);
    REQUIRE(t.setLoop(TransportLoop{true, 10.0, 12.0}));
    t.seek(11.5);
    t.play();

    auto tick = t.advance(1.0); // 12.5 -> wraps to 10.5
    CHECK(tick.looped);
    CHECK_THAT(tick.positionSeconds, WithinAbs(10.5, 1e-9));
    CHECK(t.isPlaying()); // a loop does not end playback

    // Twenty laps of a short loop in one step. The wrap is a modulo rather than a jump to the start,
    // so the overshoot is kept: discarding it loses time on every lap and a loop runs slow.
    t.seek(10.0);
    tick = t.advance(40.5);
    CHECK(tick.looped);
    CHECK_THAT(tick.positionSeconds, WithinAbs(10.5, 1e-9));

    // And a hundred small steps land exactly where the arithmetic says, not a little short.
    t.seek(10.0);
    for (int i = 0; i < 100; ++i) {
        static_cast<void>(t.advance(0.03));
    }
    const double expected = 10.0 + std::fmod(100 * 0.03, 2.0);
    CHECK_THAT(t.positionSeconds(), WithinAbs(expected, 1e-6));
}

TEST_CASE("A degenerate loop is no loop", "[transport][loop]") {
    Transport t = made(100.0);
    // Zero length: enabling it would wrap an unbounded number of times in one frame.
    t.setLoop(TransportLoop{true, 5.0, 5.0});
    CHECK_FALSE(t.loop().usable());
    CHECK_THAT(t.playEndSeconds(), WithinAbs(100.0, 1e-9));

    // Backwards: a range dragged right to left is still a range.
    t.setLoop(TransportLoop{true, 30.0, 10.0});
    CHECK_THAT(t.loop().startSeconds, WithinAbs(10.0, 1e-9));
    CHECK_THAT(t.loop().endSeconds, WithinAbs(30.0, 1e-9));
    CHECK(t.loop().usable());

    // Past the end of the piece: trimmed to it.
    t.setLoop(TransportLoop{true, 90.0, 1000.0});
    CHECK_THAT(t.loop().endSeconds, WithinAbs(100.0, 1e-9));

    // Negative start.
    t.setLoop(TransportLoop{true, -50.0, 20.0});
    CHECK_THAT(t.loop().startSeconds, WithinAbs(0.0, 1e-9));
}

TEST_CASE("Toggling and moving a loop during playback is safe", "[transport][loop]") {
    Transport t = made(100.0);
    t.setLoop(TransportLoop{true, 10.0, 20.0});
    t.play();
    t.seek(50.0); // outside the loop: allowed, and playback continues from there
    auto tick = t.advance(1.0);
    CHECK_FALSE(tick.looped);
    CHECK_THAT(tick.positionSeconds, WithinAbs(51.0, 1e-9));
    // Past the loop end but outside it -- the wrap only applies from inside, so a playhead parked
    // beyond a loop runs to the end of the piece the way it would with no loop at all.

    t.seek(19.5);
    tick = t.advance(1.0);
    CHECK(tick.looped);

    // Turning the loop off mid-play leaves the playhead where it is and lets the piece run on.
    CHECK(t.setLoopEnabled(false));
    t.seek(19.5);
    tick = t.advance(1.0);
    CHECK_FALSE(tick.looped);
    CHECK_THAT(tick.positionSeconds, WithinAbs(20.5, 1e-9));

    // Stop with a loop on parks at the loop start, which is where Play will begin.
    t.setLoopEnabled(true);
    t.stop();
    CHECK_THAT(t.positionSeconds(), WithinAbs(10.0, 1e-9));
}

TEST_CASE("A piece that gets shorter does not strand the playhead past its end", "[transport]") {
    // Re-baking a sequence while it plays is the normal way to work here, and a bake can shorten it.
    Transport t = made(100.0);
    t.seek(80.0);
    t.setLoop(TransportLoop{true, 60.0, 90.0});
    t.setDuration(30.0);
    CHECK_THAT(t.positionSeconds(), WithinAbs(30.0, 1e-9));
    CHECK(t.loop().endSeconds <= 30.0);
    CHECK(t.loop().startSeconds <= 30.0);
}

// ---- offline ------------------------------------------------------------------------------------

TEST_CASE("Offline, the fixed-step clock is the authority", "[transport][offline]") {
    Transport t = made(30.0);
    t.setMode(app::TransportMode::Offline);
    t.setLoop(TransportLoop{true, 1.0, 2.0});
    t.play();

    // A render of 0..120 s against 30 s of audio renders 120 seconds: offline positions are taken
    // literally, with no clamp to the duration.
    t.setOfflinePosition(90.0);
    CHECK_THAT(t.positionSeconds(), WithinAbs(90.0, 1e-9));

    // And neither the loop nor elapsed wall time can touch an offline position. A loop set for
    // previewing must not silently become part of an export.
    const auto tick = t.advance(10.0);
    CHECK_THAT(tick.positionSeconds, WithinAbs(90.0, 1e-9));
    CHECK_FALSE(tick.looped);
    static_cast<void>(t.follow(5.0));
    CHECK_THAT(t.positionSeconds(), WithinAbs(90.0, 1e-9));
}

// ---- displays -----------------------------------------------------------------------------------

TEST_CASE("The time displays say what the position is", "[transport]") {
    CHECK(app::formatClockTime(0.0) == "0:00.000");
    CHECK(app::formatClockTime(83.456) == "1:23.456");
    CHECK(app::formatClockTime(3723.5) == "1:02:03.500");

    const FrameRate thirty = FrameRate::fromFps(30.0);
    CHECK(app::formatTimecode(0.0, thirty) == "00:00:00:00");
    CHECK(app::formatTimecode(1.0, thirty) == "00:00:01:00");
    CHECK(app::formatTimecode(1.0 + 14.0 / 30.0, thirty) == "00:00:01:14");
    CHECK(app::formatTimecode(3661.0, thirty) == "01:01:01:00");

    // 29.97 non-drop: the frame count runs 0..29 and the label drifts from wall time, which is what
    // non-drop means. The last frame of a labelled second must not be pushed into the next one.
    const FrameRate ntsc = FrameRate::fromFps(29.97);
    CHECK(app::formatTimecode(0.0, ntsc) == "00:00:00:00");
    CHECK(app::formatTimecode(29.0 * 1001.0 / 30000.0, ntsc) == "00:00:00:29");
    CHECK(app::formatTimecode(30.0 * 1001.0 / 30000.0, ntsc) == "00:00:01:00");

    CHECK(app::formatBarsBeats(0.0, 120.0, 4) == "1.1");
    CHECK(app::formatBarsBeats(0.5, 120.0, 4) == "1.2");   // 120 bpm: half a second is one beat
    CHECK(app::formatBarsBeats(2.0, 120.0, 4) == "2.1");
    // No tempo, no claim.
    CHECK(app::formatBarsBeats(2.0, 0.0, 4).empty());
}

TEST_CASE("A snapshot is one coherent moment", "[transport]") {
    Transport t = made(42.0);
    t.setFrameRate(FrameRate::fromFps(25.0));
    t.setTempo(90.0, 3);
    t.setLoop(TransportLoop{true, 1.0, 5.0});
    t.seek(4.0);
    t.play();

    const auto snap = t.snapshot();
    CHECK(snap.playing());
    CHECK_THAT(snap.positionSeconds, WithinAbs(4.0, 1e-9));
    CHECK_THAT(snap.durationSeconds, WithinAbs(42.0, 1e-9));
    CHECK(snap.frame == 100);
    CHECK(snap.frameRate == FrameRate{25, 1});
    CHECK_THAT(snap.tempoBpm, WithinAbs(90.0, 1e-9));
    CHECK(snap.beatsPerBar == 3);
    CHECK(snap.loop.usable());
    CHECK(snap.revision == t.revision());
}

TEST_CASE("A piece with no stated length plays forward without end", "[transport]") {
    // A world open in the editor with no sequence and no audio has nothing to say how long it is.
    // That is "unbounded", not "zero seconds": pinning the playhead at zero would make Play do
    // nothing, which is what the engine did before it had a transport.
    Transport t; // no setDuration
    CHECK_THAT(t.durationSeconds(), WithinAbs(0.0, 1e-9));
    t.play();
    auto tick = t.advance(5.0);
    CHECK_THAT(tick.positionSeconds, WithinAbs(5.0, 1e-9));
    CHECK_FALSE(tick.reachedEnd);
    CHECK(t.isPlaying());
    tick = t.advance(1000.0);
    CHECK_THAT(tick.positionSeconds, WithinAbs(1005.0, 1e-9));
    CHECK(t.isPlaying());
    // Still no going backwards past the beginning.
    CHECK_THAT(t.seek(-1.0), WithinAbs(0.0, 1e-9));
    t.seek(1005.0);
    CHECK_THAT(t.positionSeconds(), WithinAbs(1005.0, 1e-9)); // unbounded forwards

    // The control: give it a length and the end rule applies again, and the playhead comes back
    // inside the piece rather than being stranded past the end of it.
    t.setDuration(10.0);
    CHECK_THAT(t.positionSeconds(), WithinAbs(10.0, 1e-9));
    t.seek(9.0);
    t.play();
    tick = t.advance(5.0);
    CHECK(tick.reachedEnd);
}
