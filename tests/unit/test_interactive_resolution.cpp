// The adaptive render-scale controller's control law (§15-§17).
//
// This is the half of the feature a benchmark cannot check. A benchmark can say "the frame got
// faster"; it cannot say the ladder settles rather than oscillating, that it declines to act when
// the main thread is what the frame is waiting for, or that it never moves at all while the budget
// is being met. Those are properties of the decision, and the decision is deliberately a pure
// function of the numbers handed to `note()`.
//
// ADR-182 throughout: every "it did not move" assertion is paired with an arm over the same
// controller in which it does move, so a controller wired to return rung 0 unconditionally fails
// this file rather than passing it.

#include "app/interactive_resolution.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace avgen;

namespace {

app::InteractiveResolutionSettings fast() {
    app::InteractiveResolutionSettings s;
    s.enabled = true;
    s.budgetMs = 16.67;
    // Short enough that a test does not have to feed hundreds of frames, long enough that the
    // window still fills before a decision is taken.
    s.dwellFrames = 8;
    s.windowFrames = 5;
    return s;
}

// Feed `n` frames of a GPU cost that is over budget and clearly the binding constraint (the wall
// clock is the GPU plus a little), and return the controller's rung.
std::size_t feed(app::InteractiveResolution& c, int n, double gpuMs, double wallMs) {
    for (int i = 0; i < n; ++i) {
        c.note(gpuMs, wallMs);
    }
    return c.rung();
}

// The affine cost law measured in `tools/resolution_sweep.sh` on the multicam film: 4.63 ms of
// fixed cost plus 8.45 ms per megapixel, at a canvas of `mpx` megapixels rendered at `scale`.
// Used by the closed-loop test so that the controller is answering a cost curve with the shape of
// the real one rather than one invented to make it converge.
double measuredCost(double mpx, float scale) {
    return 4.63 + 8.45 * mpx * static_cast<double>(scale) * static_cast<double>(scale);
}

} // namespace

TEST_CASE("a frame inside the budget never moves the ladder", "[unit][resolution]") {
    app::InteractiveResolution c;
    c.configure(fast());
    // 300 frames at 10 ms against a 16.67 ms budget. Nothing here is a reason to reduce anything.
    CHECK(feed(c, 300, 10.0, 12.0) == 0);
    CHECK(c.scale() == 1.0f);
    CHECK(c.stats().drops == 0);
    CHECK(c.stats().framesReduced == 0);

    // The control: the same controller, the same number of frames, over budget. If this does not
    // move, the assertion above is vacuous.
    app::InteractiveResolution d;
    d.configure(fast());
    CHECK(feed(d, 300, 60.0, 62.0) > 0);
}

TEST_CASE("the ladder walks down until the budget is met and then stops", "[unit][resolution]") {
    app::InteractiveResolution c;
    app::InteractiveResolutionSettings s = fast();
    c.configure(s);

    // A closed loop: the cost the controller is told about is the cost of the rung it chose, under
    // the measured law, on a 7 megapixel canvas -- the size of a maximised editor on the owner's
    // display, and the configuration the complaint is about.
    constexpr double kCanvasMpx = 7.0;
    for (int i = 0; i < 2000; ++i) {
        const double gpu = measuredCost(kCanvasMpx, c.scale());
        c.note(gpu, gpu + 4.0);
    }
    const double settled = measuredCost(kCanvasMpx, c.scale());
    INFO("settled at rung " << c.rung() << " scale " << c.scale() << " -> " << settled << " ms"
                            << "; drops " << c.stats().drops << " raises " << c.stats().raises);

    // It reached the floor, because on this canvas even a quarter of the pixels is 19.4 ms and the
    // budget is 16.67 -- and it is the floor, not something past it.
    CHECK(c.rung() == s.floorRung);
    CHECK(settled < measuredCost(kCanvasMpx, 1.0f) / 3.0);

    // And it stopped. A controller that keeps changing its mind reallocates a render target and
    // resets the screen-space history every time it does, which is worse than the cost it is
    // chasing. Five decisions is the ladder walking down once (two rungs, then two, then one) with
    // room to spare; a hunting controller would be in the hundreds over 2,000 frames.
    CHECK(c.stats().drops + c.stats().raises <= 5);
}

TEST_CASE("the ladder settles on the rung that fits rather than the floor", "[unit][resolution]") {
    app::InteractiveResolution c;
    c.configure(fast());
    // A 2.74 Mpx canvas -- the one the sweep was taken at. 4.63 + 8.45*2.74 = 27.8 ms at rung 0;
    // 0.71 halves the pixels and brings it to 16.2, inside the budget.
    constexpr double kCanvasMpx = 2.74;
    for (int i = 0; i < 2000; ++i) {
        const double gpu = measuredCost(kCanvasMpx, c.scale());
        c.note(gpu, gpu + 4.0);
    }
    INFO("settled at rung " << c.rung() << " scale " << c.scale() << " -> "
                            << measuredCost(kCanvasMpx, c.scale()) << " ms");
    CHECK(measuredCost(kCanvasMpx, c.scale()) <= 16.67);
    // Not the floor: it stopped as soon as the budget was met, which is the whole difference
    // between an adaptive scale and a fixed one.
    CHECK(c.rung() < c.settings().floorRung);
}

TEST_CASE("a CPU-bound frame is not made smaller", "[unit][resolution]") {
    app::InteractiveResolution c;
    c.configure(fast());
    // A wall clock far above the GPU frame. The GPU is over budget, so the drop branch is entered
    // -- that is what makes this a test of the guard rather than of the budget comparison above it
    // -- but two thirds of the frame is the main thread, and halving the pixels would buy 10 ms off
    // a 60 ms frame while softening the picture. The honest answer is to leave it alone.
    // (The measured Tree of Life shape, wall 18.42 against GPU 14.68, does not even reach here:
    // 14.68 is inside the budget and the controller has nothing to say about it at all.)
    CHECK(feed(c, 400, 20.0, 60.0) == 0);
    CHECK(c.stats().heldByCpu > 0);
    CHECK(c.stats().drops == 0);

    // The control, and the reason this test is not simply asserting that nothing happens: the same
    // GPU cost, over budget, with the wall clock where a GPU-bound frame puts it.
    app::InteractiveResolution d;
    d.configure(fast());
    CHECK(feed(d, 400, 40.0, 43.0) > 0);
}

TEST_CASE("one stalled frame does not move the ladder", "[unit][resolution]") {
    app::InteractiveResolution c;
    c.configure(fast());
    // Nineteen good frames and one 400 ms frame -- another agent's process landing on the machine,
    // or a shader compiling. The median of the window is what decides, so this is not evidence.
    for (int block = 0; block < 20; ++block) {
        for (int i = 0; i < 19; ++i) {
            c.note(10.0, 12.0);
        }
        c.note(400.0, 402.0);
    }
    CHECK(c.rung() == 0);
    CHECK(c.stats().drops == 0);
}

TEST_CASE("a frame the GPU timeline could not report is not a free frame", "[unit][resolution]") {
    app::InteractiveResolution c;
    c.configure(fast());
    // Drive it to a reduced rung first.
    feed(c, 200, 60.0, 62.0);
    const std::size_t reduced = c.rung();
    REQUIRE(reduced > 0);
    // `FrameTimeline` has no completed frame for the first frames of a session and none at all on
    // a build without timestamp queries. Counting those as 0 ms would walk the ladder straight
    // back to the top on a machine that had just got slower.
    for (int i = 0; i < 500; ++i) {
        c.note(-1.0, 62.0);
    }
    CHECK(c.rung() == reduced);
    CHECK(c.stats().raises == 0);
}

TEST_CASE("the ladder climbs back when the frame gets cheap again", "[unit][resolution]") {
    app::InteractiveResolution c;
    c.configure(fast());
    feed(c, 400, 60.0, 62.0);
    REQUIRE(c.rung() > 0);
    // The expensive shot ends. A frame this cheap fits at every rung, so the controller should
    // give the resolution back rather than leaving the editor permanently soft.
    feed(c, 600, 2.0, 5.0);
    CHECK(c.rung() == 0);
    CHECK(c.scale() == 1.0f);
    CHECK(c.stats().raises > 0);
}

TEST_CASE("a disabled controller is the identity", "[unit][resolution]") {
    app::InteractiveResolution c;
    app::InteractiveResolutionSettings s = fast();
    s.enabled = false;
    c.configure(s);
    CHECK(feed(c, 1000, 200.0, 220.0) == 0);
    CHECK(c.scale() == 1.0f);
    CHECK(c.stats().framesSeen == 0);
}

TEST_CASE("the floor is honoured", "[unit][resolution]") {
    app::InteractiveResolution c;
    app::InteractiveResolutionSettings s = fast();
    s.floorRung = 1; // one rung of reduction and no more
    c.configure(s);
    CHECK(feed(c, 2000, 500.0, 520.0) == 1);
    CHECK(c.scale() == app::kRenderScaleRungs[1]);
}
