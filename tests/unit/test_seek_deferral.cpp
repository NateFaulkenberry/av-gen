// The interactive seek's deferral policy, as arithmetic.
//
// `Engine::requestSeek` splits a seek into a cheap half applied now (the transport and the timeline
// clock -- what draws the playhead) and an expensive half (`EntityWorld::seek`, which re-integrates
// every entity from `target - 90 s`) deferred while the gesture is still moving. Whether that split
// is honest rests entirely on this function, so it is tested here without an engine, a clock or a
// GPU -- the same treatment `advanceRebuildDeferral` gets in test_composition.cpp, and for the same
// reason: the behaviour that matters is a property of the arithmetic and not of any scene.
//
// Each test states the failure it is guarding against, because a deferral's failure modes are all
// quiet: the world silently one gesture behind, a click that got 90 ms slower for nothing, a drag
// that never refreshes at all.

#include <catch2/catch_test_macros.hpp>

#include "scene/rebuild_deferral.hpp"

using avgen::scene::advanceSeekDeferral;
using avgen::scene::RebuildDeferral;

TEST_CASE("a request that is not part of a held gesture evaluates at once", "[seek][deferral]") {
    RebuildDeferral state;
    // A click. Guarding against: making every single click 90 ms slower in exchange for a deferral
    // that only a drag can benefit from.
    CHECK(advanceSeekDeferral(state, 42.0, 0.0, /*held=*/false, 16.0));
    CHECK_FALSE(state.deferring);
    // ...and a release mid-drag, which is the same thing arriving from the other direction.
    RebuildDeferral held;
    CHECK_FALSE(advanceSeekDeferral(held, 10.0, 0.0, true, 16.0));
    CHECK_FALSE(advanceSeekDeferral(held, 11.0, 0.0, true, 16.0));
    CHECK(advanceSeekDeferral(held, 11.0, 0.0, /*held=*/false, 16.0));
}

TEST_CASE("a held gesture that keeps moving never reaches the evaluation", "[seek][deferral]") {
    RebuildDeferral state;
    state.lastMs = 2000.0; // what a seek on a production project actually costs
    double requested = 5.0;
    int evaluations = 0;
    // Forty frames of a hand moving, at 16 ms each: 640 ms of gesture.
    for (int frame = 0; frame < 40; ++frame) {
        requested += 0.5;
        if (advanceSeekDeferral(state, requested, 0.0, /*held=*/true, 16.0)) {
            ++evaluations;
        }
    }
    // The ceiling is max(90 ms, 4 x 2000 ms) = 8 s, which 640 ms of gesture does not reach, and the
    // settle timer restarts on every frame because the request moves on every frame. Guarding
    // against: a policy that lets a drag sneak an evaluation in between two of its own frames,
    // which is the behaviour it exists to prevent.
    CHECK(evaluations == 0);
}

TEST_CASE("a long held gesture still refreshes, on a ceiling scaled by what it costs",
          "[seek][deferral]") {
    // Guarding against the opposite failure, which the first version of the rebuild policy had: a
    // drag that goes on for seconds showing a world frozen at the second it started from. The
    // settle timer alone cannot express this -- the first thing a moving target does is reset it.
    RebuildDeferral cheap;
    cheap.lastMs = 5.0; // ceiling = max(90, 20) = 90 ms
    double requested = 1.0;
    int cheapEvaluations = 0;
    for (int frame = 0; frame < 60; ++frame) {
        requested += 0.25;
        if (advanceSeekDeferral(cheap, requested, 0.0, true, 16.0)) {
            ++cheapEvaluations;
        }
    }
    CHECK(cheapEvaluations > 0);

    RebuildDeferral expensive;
    expensive.lastMs = 2000.0; // ceiling = 8 s
    requested = 1.0;
    int expensiveEvaluations = 0;
    for (int frame = 0; frame < 60; ++frame) {
        requested += 0.25;
        if (advanceSeekDeferral(expensive, requested, 0.0, true, 16.0)) {
            ++expensiveEvaluations;
        }
    }
    // Same gesture, same number of frames: the cheap seek refreshes during it and the expensive one
    // does not. Nothing spends more than about a fifth of its time evaluating, and a cheap seek is
    // not starved in order to protect an expensive one.
    CHECK(expensiveEvaluations < cheapEvaluations);
}

TEST_CASE("a gesture that stops moving evaluates once it has settled", "[seek][deferral]") {
    RebuildDeferral state;
    state.lastMs = 2000.0;
    CHECK_FALSE(advanceSeekDeferral(state, 30.0, 0.0, true, 16.0)); // first: start deferring
    int evaluations = 0;
    // The pointer is still down but has stopped moving -- somebody holding the playhead still.
    for (int frame = 0; frame < 10 && evaluations == 0; ++frame) {
        if (advanceSeekDeferral(state, 30.0, 0.0, true, 16.0)) {
            ++evaluations;
        }
    }
    // About five frames at 60 Hz. Guarding against: a held-but-still pointer leaving the world one
    // gesture behind for as long as somebody keeps their finger down.
    CHECK(evaluations == 1);
}

TEST_CASE("an already-evaluated position is not re-evaluated", "[seek][deferral]") {
    RebuildDeferral state;
    // Guarding against the most expensive possible bug in this file: a request equal to the
    // evaluated second re-running a two-second re-simulation for a world that is already there.
    CHECK_FALSE(advanceSeekDeferral(state, 12.5, 12.5, true, 16.0));
    CHECK_FALSE(advanceSeekDeferral(state, 12.5, 12.5, false, 16.0));
    // And negative zero is the same second as zero, rather than a second request.
    CHECK_FALSE(advanceSeekDeferral(state, -0.0, 0.0, false, 16.0));
    CHECK_FALSE(advanceSeekDeferral(state, 0.0, -0.0, true, 16.0));
}

TEST_CASE("the timers are clean after an evaluation, so the next gesture starts fresh",
          "[seek][deferral]") {
    RebuildDeferral state;
    state.lastMs = 2000.0;
    CHECK_FALSE(advanceSeekDeferral(state, 20.0, 0.0, true, 16.0));
    CHECK_FALSE(advanceSeekDeferral(state, 21.0, 0.0, true, 16.0));
    REQUIRE(state.deferring);
    // Release.
    CHECK(advanceSeekDeferral(state, 21.0, 0.0, false, 16.0));
    CHECK_FALSE(state.deferring);
    CHECK(state.settledForMs == 0.0);
    CHECK(state.heldForMs == 0.0);
    // The next gesture must therefore take a full settle before its first evaluation, not inherit
    // the previous one's accumulated hold. Guarding against: the second scrub of a session behaving
    // differently from the first.
    CHECK_FALSE(advanceSeekDeferral(state, 30.0, 21.0, true, 16.0));
    CHECK_FALSE(advanceSeekDeferral(state, 31.0, 21.0, true, 16.0));
}
