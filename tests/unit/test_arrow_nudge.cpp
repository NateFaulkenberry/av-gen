// What an arrow key moves the playhead by (ADR-357).
//
// The feature is one sentence -- the step is the active snap mode's unit and Shift is the next unit
// up -- and the whole of it is the *dependence on the mode*. A test that only exercised Beats would
// pass on an implementation that ignored the mode entirely and always stepped a beat, which is why
// the arm below that walks all four modes and demands four different answers is the one that matters
// (ADR-182).

#include "ui/ui_logic.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace avgen;
using Catch::Approx;

namespace {
// The strip's `snapMode_`, which is `seq::SnapMode`'s ordering. Named here so the table below reads
// as the owner's table does; `sequence_panel.cpp` static_asserts these against the enum itself.
constexpr int kOff = 0;
constexpr int kFrames = 1;
constexpr int kBeats = 2;
constexpr int kMarkers = 3;
constexpr double kFps = 60.0;
constexpr double kView = 30.0;
} // namespace

TEST_CASE("the step is the snap mode's own unit", "[ui][nudge]") {
    // **The arm the whole feature rests on.** Four modes, four different units. An implementation
    // that ignored `snapMode` could not pass this however plausible each individual answer looked.
    CHECK(ui::arrowNudge(kFrames, 1, false, kFps, kView).unit == ui::NudgeUnit::Frames);
    CHECK(ui::arrowNudge(kBeats, 1, false, kFps, kView).unit == ui::NudgeUnit::Beats);
    CHECK(ui::arrowNudge(kMarkers, 1, false, kFps, kView).unit == ui::NudgeUnit::Markers);
    CHECK(ui::arrowNudge(kOff, 1, false, kFps, kView).unit == ui::NudgeUnit::Seconds);
}

TEST_CASE("Beats: Shift moves a bar, and a bar is beatsPerBar beats", "[ui][nudge]") {
    const ui::Nudge plain = ui::arrowNudge(kBeats, 1, false, kFps, kView, /*beatsPerBar=*/4);
    const ui::Nudge shifted = ui::arrowNudge(kBeats, 1, true, kFps, kView, /*beatsPerBar=*/4);
    CHECK(plain.count == 1);
    CHECK(shifted.count == 4);
    CHECK(shifted.count == 4 * plain.count);

    SECTION("and it is not a hard-coded four") {
        // The point of taking `beatsPerBar` rather than spelling 4: the day time-signature detection
        // arrives, `seq::BakeOptions` changes and this follows. A test that only ever passed 4 could
        // not tell the difference between reading the parameter and ignoring it.
        CHECK(ui::arrowNudge(kBeats, 1, true, kFps, kView, /*beatsPerBar=*/3).count == 3);
        CHECK(ui::arrowNudge(kBeats, 1, true, kFps, kView, /*beatsPerBar=*/7).count == 7);
        // A nonsense time signature must not produce a step of zero, which would be a dead key.
        CHECK(ui::arrowNudge(kBeats, 1, true, kFps, kView, /*beatsPerBar=*/0).count == 1);
    }
}

TEST_CASE("Frames: Shift moves a second, which is NOT four frames", "[ui][nudge]") {
    // The control the Beats arm needs. "Shift is four times the step" is true of beats and false
    // here, and an implementation that multiplied everything by four would pass the Beats arm alone.
    const ui::Nudge plain = ui::arrowNudge(kFrames, 1, false, kFps, kView);
    const ui::Nudge shifted = ui::arrowNudge(kFrames, 1, true, kFps, kView);
    CHECK(plain.count == 1);
    CHECK(shifted.count == 60);
    CHECK(shifted.count != 4 * plain.count);

    SECTION("a second is a second at other frame rates too") {
        CHECK(ui::arrowNudge(kFrames, 1, true, 24.0, kView).count == 24);
        CHECK(ui::arrowNudge(kFrames, 1, true, 29.97, kView).count == 30);
        // A frame rate of zero must still move by something.
        CHECK(ui::arrowNudge(kFrames, 1, true, 0.0, kView).count == 60);
        CHECK(ui::arrowNudge(kFrames, 1, false, 0.0, kView).count == 1);
    }
}

TEST_CASE("Markers: Shift asks for the section boundaries only", "[ui][nudge]") {
    const ui::Nudge plain = ui::arrowNudge(kMarkers, 1, false, kFps, kView);
    const ui::Nudge shifted = ui::arrowNudge(kMarkers, 1, true, kFps, kView);
    CHECK(plain.count == 1);
    CHECK(shifted.count == 1); // one jump either way; what changes is *which* marks count
    CHECK_FALSE(plain.sectionsOnly);
    CHECK(shifted.sectionsOnly);

    SECTION("the control: no other mode ever asks for sections only") {
        // `sectionsOnly` is meaningless outside Markers, and a stray true would make Shift+arrow in
        // Beats mode silently ask the engine for a marker jump.
        for (const int mode : {kOff, kFrames, kBeats}) {
            INFO("mode " << mode);
            CHECK_FALSE(ui::arrowNudge(mode, 1, true, kFps, kView).sectionsOnly);
        }
    }
}

TEST_CASE("Off still moves, and moves by a fraction of what is on screen", "[ui][nudge]") {
    // Turning the grid off must not turn the feature off. It is also the one mode with no unit, so
    // the step is view-relative: the same gesture at a ten-second view and at a four-minute one.
    const ui::Nudge closeUp = ui::arrowNudge(kOff, 1, false, kFps, /*viewSpanSeconds=*/10.0);
    const ui::Nudge wide = ui::arrowNudge(kOff, 1, false, kFps, /*viewSpanSeconds=*/240.0);
    CHECK(closeUp.unit == ui::NudgeUnit::Seconds);
    CHECK(closeUp.seconds == Approx(0.1));
    CHECK(wide.seconds == Approx(2.4));
    // It really is view-dependent, which is the claim.
    CHECK(closeUp.seconds < wide.seconds);

    SECTION("Shift is ten times as far") {
        CHECK(ui::arrowNudge(kOff, 1, true, kFps, 10.0).seconds == Approx(1.0));
        CHECK(ui::arrowNudge(kOff, 1, true, kFps, 240.0).seconds == Approx(24.0));
    }

    SECTION("a step can never round away to nothing") {
        // 1% of a fifth of a second is two milliseconds, well under a frame. An arrow key that moved
        // the playhead by less than one frame is an arrow key that appears not to work.
        const ui::Nudge tiny = ui::arrowNudge(kOff, 1, false, kFps, /*viewSpanSeconds=*/0.2);
        CHECK(tiny.seconds == Approx(1.0 / 60.0));
        CHECK(tiny.seconds > 0.0);
        // And before the strip has ever been drawn there is no span to take a fraction of.
        CHECK(ui::arrowNudge(kOff, 1, false, kFps, /*viewSpanSeconds=*/0.0).seconds == Approx(0.1));
        CHECK(ui::arrowNudge(kOff, 1, true, kFps, /*viewSpanSeconds=*/0.0).seconds == Approx(1.0));
    }
}

TEST_CASE("direction is a sign and nothing else", "[ui][nudge]") {
    for (const int mode : {kOff, kFrames, kBeats, kMarkers}) {
        INFO("mode " << mode);
        const ui::Nudge left = ui::arrowNudge(mode, -1, false, kFps, kView);
        const ui::Nudge right = ui::arrowNudge(mode, 1, false, kFps, kView);
        CHECK(left.unit == right.unit);
        CHECK(left.count == -right.count);
        CHECK(left.seconds == Approx(-right.seconds));
        // Left really is negative: a pair that were both zero would satisfy the line above.
        CHECK((left.count < 0 || left.seconds < 0.0));
    }
}

TEST_CASE("an out-of-range snap mode does something sane rather than nothing", "[ui][nudge]") {
    // `snapMode_` is an int on the panel and is clamped where it is read elsewhere; a mode nobody
    // has heard of must not produce a key that does nothing at all.
    CHECK(ui::arrowNudge(-5, 1, false, kFps, kView).unit == ui::NudgeUnit::Seconds);
    CHECK(ui::arrowNudge(99, 1, false, kFps, kView).unit == ui::NudgeUnit::Markers);
}

TEST_CASE("the playhead stops at both ends of the piece", "[ui][nudge]") {
    // The Off arm computes a time rather than asking the transport for a step, so it is the one that
    // has to clamp for itself.
    CHECK(ui::nudgedTime(5.0, 2.0, 100.0) == Approx(7.0));
    CHECK(ui::nudgedTime(5.0, -2.0, 100.0) == Approx(3.0));

    SECTION("left at zero stops at zero and never runs negative") {
        CHECK(ui::nudgedTime(0.5, -2.0, 100.0) == Approx(0.0));
        CHECK(ui::nudgedTime(0.0, -10.0, 100.0) == Approx(0.0));
    }

    SECTION("right past the end stops at the end and does not wrap") {
        CHECK(ui::nudgedTime(99.0, 5.0, 100.0) == Approx(100.0));
        CHECK(ui::nudgedTime(100.0, 5.0, 100.0) == Approx(100.0));
        // Wrapping would put it at 4.0. Named, because that is the bug the clamp is against.
        CHECK(ui::nudgedTime(99.0, 5.0, 100.0) != Approx(4.0));
    }

    SECTION("a piece with no duration yet pins the playhead at zero") {
        CHECK(ui::nudgedTime(0.0, 1.0, 0.0) == Approx(0.0));
    }
}
