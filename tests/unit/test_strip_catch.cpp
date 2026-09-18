// Catching the playhead, and zooming without losing what you were looking at.
//
// The panel cannot be tested -- there is no ImGui in a unit test -- so the whole of the behaviour
// lives in two pure functions next to `stripLanesFor`, which was extracted for the same reason and
// after a sharper lesson: a bulk rename once made every lane zero pixels tall and the entire suite
// stayed green, because no test could see the number.

#include "ui/ui_logic.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace avgen::ui;
using Catch::Matchers::WithinAbs;

TEST_CASE("a caught view pages rather than crawling", "[ui][strip][catch]") {
    const double duration = 200.0;
    const double span = 20.0;

    // The control, and the reason this is not "keep it centred": for most of a screen the view does
    // not move at all. A centring implementation would return a different number on every one of
    // these, which is the crawl this is written to avoid.
    for (const double now : {5.0, 8.0, 12.0, 17.0}) {
        CHECK_THAT(caughtView(0.0, span, now, duration), WithinAbs(0.0, 1e-9));
    }

    // At the edge it jumps, and lands the playhead a tenth of the way in -- so what comes next is
    // most of the screen, which is the point of following at all.
    const double paged = caughtView(0.0, span, 18.5, duration);
    CHECK(paged > 0.0);
    CHECK_THAT(paged, WithinAbs(18.5 - span * 0.1, 1e-9));

    // A backwards jump is caught too: Return sends the playhead to zero, and a view still at 2:14
    // shows no playhead anywhere.
    CHECK_THAT(caughtView(134.0, span, 0.0, duration), WithinAbs(0.0, 1e-9));

    // Never past the end of the piece.
    CHECK_THAT(caughtView(0.0, span, 199.0, duration), WithinAbs(duration - span, 1e-9));
}

TEST_CASE("zooming keeps the anchor where it was on screen", "[ui][strip][catch]") {
    const double duration = 300.0;

    // The playhead sits a quarter of the way across a 40 s window; after zooming to 20 s it must
    // still sit a quarter of the way across.
    const double view = 100.0;
    const double anchor = 110.0; // (110 - 100) / 40 = 0.25
    const double zoomed = viewAfterZoom(view, 40.0, 20.0, anchor, duration);
    CHECK_THAT((anchor - zoomed) / 20.0, WithinAbs(0.25, 1e-9));

    // And out again returns to where it started, so the gesture is reversible.
    CHECK_THAT(viewAfterZoom(zoomed, 20.0, 40.0, anchor, duration), WithinAbs(view, 1e-9));

    // The control: the old behaviour anchored on the left edge, which is what this replaces. If
    // `viewAfterZoom` ever degenerates to that, the anchor's screen fraction moves and this fails.
    CHECK(zoomed != view);

    // Near the start the anchor is still honoured rather than snapped to zero: 2 s sits a twentieth
    // of the way across 40 s, and a twentieth of 20 s is 1 s, so the view is 1 s and the anchor has
    // not moved on screen. Clamping here would be the left-edge behaviour creeping back in.
    CHECK_THAT(viewAfterZoom(0.0, 40.0, 20.0, 2.0, duration), WithinAbs(1.0, 1e-9));

    // Clamped only where it would actually leave the piece.
    CHECK_THAT(viewAfterZoom(0.0, 40.0, 20.0, 0.5, duration), WithinAbs(0.25, 1e-9));
    CHECK(viewAfterZoom(280.0, 20.0, 40.0, 295.0, duration) <= duration - 40.0 + 1e-9);
    CHECK(viewAfterZoom(0.0, 20.0, 40.0, 1.0, duration) >= 0.0);
}
