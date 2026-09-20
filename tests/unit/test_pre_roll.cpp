// The shared bounded pre-roll and the step classification (ADR-397).
//
// Two subsystems re-run a bounded number of frames after a discontinuity: ADR-360's particle pools
// and the temporal-media history buffers. What they share is the SCHEDULE, which is the part that
// is easy to get subtly wrong and the part that can be checked without a device.
//
// The classification is here for a specific reason. `ao_renderer.cpp` carries the scar of
// SYM-TERRAIN-1, where a REPEAT (the same frame rendered twice, which has to reproduce) and a JUMP
// (a seek, which has to drop its history) were treated the same. Every consumer that inherits this
// header inherits that distinction instead of re-deriving it -- so these cases are written from
// both sides: what each classification must be, and what it must NOT be.

#include "core/pre_roll.hpp"
#include "core/time.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace avgen;

namespace {

FrameTime frame(double renderTime, double deltaTime, std::uint64_t index) {
    FrameTime t{};
    t.renderTime = renderTime;
    t.deltaTime = deltaTime;
    t.frameIndex = index;
    return t;
}

constexpr double kDt = 1.0 / 60.0;

} // namespace

TEST_CASE("a repeat and a jump are not the same step", "[core][preroll][determinism]") {
    const FrameTime previous = frame(2.0, kDt, 120);

    // The same frame, again: same index, same second. Re-rendering a frame has to reproduce it.
    CHECK(classifyStep(true, previous, frame(2.0, kDt, 120)) == TimelineStep::Repeat);

    // The frame after it.
    CHECK(classifyStep(true, previous, frame(2.0 + kDt, kDt, 121)) == TimelineStep::Continuous);

    // A seek in the LIVE application, which is the case that a classification on the frame index
    // alone gets wrong: `RealtimeClock::seek` moves the second and leaves the counter climbing, so
    // this is index + 1 carrying a second from somewhere else entirely.
    CHECK(classifyStep(true, previous, frame(41.0, kDt, 121)) == TimelineStep::Jump);

    // A seek BACKWARDS, same shape.
    CHECK(classifyStep(true, previous, frame(0.5, kDt, 121)) == TimelineStep::Jump);

    // A skipped frame.
    CHECK(classifyStep(true, previous, frame(2.0 + 2 * kDt, kDt, 122)) == TimelineStep::Jump);

    // The same index at a different second is not a repeat of anything -- there is no earlier
    // render of THIS frame to reproduce.
    CHECK(classifyStep(true, previous, frame(9.0, kDt, 120)) == TimelineStep::Jump);

    // Nothing rendered yet.
    CHECK(classifyStep(false, previous, frame(0.0, 0.0, 0)) == TimelineStep::First);
}

TEST_CASE("the step tolerance is float slop and not a window", "[core][preroll][determinism]") {
    // A window wide enough to absorb a real seek would classify a small one as motion, and the
    // smear across it is exactly what the classification exists to prevent. Half a frame is a
    // discontinuity, not a rounding error.
    const FrameTime previous = frame(2.0, kDt, 120);
    CHECK(classifyStep(true, previous, frame(2.0 + kDt * 0.5, kDt, 121)) == TimelineStep::Jump);
    // ...while an ulp of accumulated double is not.
    const double nudged = std::nextafter(std::nextafter(2.0 + kDt, 3.0), 3.0);
    CHECK(classifyStep(true, previous, frame(nudged, kDt, 121)) == TimelineStep::Continuous);
}

TEST_CASE("a pre-roll is off unless asked for, and capped when it is", "[core][preroll]") {
    const FrameTime arriving = frame(2.0, kDt, 120);

    // The default. Every caller that has not opted in pays nothing.
    CHECK(planPreRoll(PreRoll{}, arriving).frames.empty());

    PreRoll roll;
    roll.frames = 8;
    CHECK(planPreRoll(roll, arriving).frames.size() == 8);

    // The bound is the point: an unbounded re-simulation is the thing ADR-360 refused to add.
    roll.frames = 100000;
    roll.cap = 240;
    CHECK(planPreRoll(roll, arriving).frames.size() == 240);
}

TEST_CASE("a pre-roll lands on the seconds a full render would have had", "[core][preroll][determinism]") {
    // This is the whole of what the schedule is for. Everything a consumer re-runs over these
    // frames is keyed to the timeline -- particle spawn nonces, anything else that is a function of
    // time -- so a roll that lands on seconds the full render never visited re-runs a different
    // simulation and arrives somewhere else.
    PreRoll roll;
    roll.frames = 4;
    const FrameTime arriving = frame(2.0, kDt, 120);
    const PreRollPlan plan = planPreRoll(roll, arriving);
    REQUIRE(plan.frames.size() == 4);

    for (std::size_t i = 0; i < plan.frames.size(); ++i) {
        const double expected = arriving.renderTime - static_cast<double>(4 - i) * kDt;
        INFO("roll frame " << i);
        CHECK(std::abs(plan.frames[i].renderTime - expected) < 1e-12);
        CHECK(plan.frames[i].deltaTime == kDt);
    }
    // Oldest first, and the newest lands exactly one step before the arriving frame.
    CHECK(plan.frames.front().renderTime < plan.frames.back().renderTime);
    CHECK(std::abs(plan.frames.back().renderTime + kDt - arriving.renderTime) < 1e-12);

    // Consecutive indices ending one below the arriving frame's, so a consumer that accumulates
    // across the roll sees `Continuous` at every step of it AND on arrival. That last one is the
    // part that would be easy to leave out and would quietly discard the roll's whole product.
    bool have = false;
    FrameTime previous{};
    for (const FrameTime& f : plan.frames) {
        const TimelineStep step = classifyStep(have, previous, f);
        INFO("roll frame index " << f.frameIndex << " classified " << timelineStepName(step));
        CHECK((step == TimelineStep::First || step == TimelineStep::Continuous));
        previous = f;
        have = true;
    }
    FrameTime arrival = arriving;
    arrival.frameIndex = plan.arrivalFrameIndex;
    arrival.deltaTime = kDt;
    CHECK(classifyStep(true, previous, arrival) == TimelineStep::Continuous);
    CHECK(plan.arrivalFrameIndex == 120); // there was room below it, so nothing moved
}

TEST_CASE("a pre-roll at the head of a render range still chains", "[core][preroll][determinism]") {
    // The case the whole mitigation exists for: an offline render whose range opens at t = 2 s
    // renders that as frame 0. There is no room below zero for the roll's indices, so the arriving
    // frame's index is shifted up rather than the roll being silently shortened -- a shortened roll
    // at exactly the moment a roll is wanted would be the mitigation quietly not happening.
    PreRoll roll;
    roll.frames = 36;
    const FrameTime arriving = frame(2.0, kDt, 0);
    const PreRollPlan plan = planPreRoll(roll, arriving);
    REQUIRE(plan.frames.size() == 36);
    CHECK(plan.frames.front().frameIndex == 0);
    CHECK(plan.frames.back().frameIndex == 35);
    CHECK(plan.arrivalFrameIndex == 36);

    // The seconds are still the full render's -- shifting an index must not move a frame in time.
    CHECK(std::abs(plan.frames.back().renderTime + kDt - arriving.renderTime) < 1e-12);

    FrameTime arrival = arriving;
    arrival.frameIndex = plan.arrivalFrameIndex;
    CHECK(classifyStep(true, plan.frames.back(), arrival) == TimelineStep::Continuous);

    // The control for the paragraph above: WITHOUT the shift the arriving frame is a Jump, and a
    // consumer would drop the history the roll just spent 36 frames building. If this ever stops
    // being true, `arrivalFrameIndex` has stopped earning its place.
    CHECK(classifyStep(true, plan.frames.back(), arriving) == TimelineStep::Jump);
}

TEST_CASE("a pre-roll takes the arriving frame's own step", "[core][preroll]") {
    PreRoll roll;
    roll.frames = 3;
    // An offline render at 24 fps: the roll must be 1/24 apart, not 1/60, or it lands between the
    // frames the full render drew.
    const PreRollPlan plan = planPreRoll(roll, frame(5.0, 1.0 / 24.0, 120));
    REQUIRE(plan.frames.size() == 3);
    CHECK(std::abs(plan.frames.back().deltaTime - 1.0 / 24.0) < 1e-12);

    // A first frame carries no delta yet. 60 is what every clock in this engine defaults to, and
    // saying so here is better than a division by zero or a roll of length zero in time.
    const PreRollPlan noDelta = planPreRoll(roll, frame(5.0, 0.0, 120));
    CHECK(std::abs(noDelta.frames.back().deltaTime - 1.0 / 60.0) < 1e-12);

    // An explicit step wins over both, which is what a caller that knows its frame rate does.
    PreRoll explicitStep = roll;
    explicitStep.stepSeconds = 0.02;
    CHECK(std::abs(planPreRoll(explicitStep, frame(5.0, kDt, 120)).frames.back().deltaTime - 0.02) < 1e-12);
}
