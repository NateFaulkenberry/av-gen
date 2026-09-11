// Regression: the route-amount slider range must not feed back on the value it edits.
#include "ui/ui_logic.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace avgen;

TEST_CASE("Route amount slider bounds are stable under repeated drags to the end", "[ui][regression]") {
    params::ModRoute route;
    route.amount = 1.0f;
    for (int frame = 0; frame < 10000; ++frame) {
        const auto [lo, hi] = ui::routeAmountBounds(route);
        REQUIRE(lo < hi);
        REQUIRE(std::isfinite(lo));
        REQUIRE(std::isfinite(hi));
        REQUIRE(hi <= ui::kRouteAmountLimit);
        route.amount = hi; // the user drags the grab to the far right every frame
    }
    CHECK(route.amount == ui::kRouteAmountLimit);
    CHECK(std::isfinite(route.amount));
}

TEST_CASE("Non-finite amounts are sanitised before reaching widgets", "[ui][regression]") {
    CHECK(ui::sanitiseFinite(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    CHECK(ui::sanitiseFinite(std::numeric_limits<float>::infinity(), 1.0f) == 1.0f);
    CHECK(ui::sanitiseFinite(2.5f) == 2.5f);
}

TEST_CASE("A loaded shader's inputs are visible on the layer the editor opens on",
          "[ui][regression]") {
    using avgen::ui::AuthoringLayer;
    using avgen::ui::layerShowsPath;

    // The Shaders window tells the user, unconditionally, that a loaded shader's inputs "appear in
    // the Parameters window under 'shader'". `shader/` was in neither prefix list, so that was true
    // only on Advanced -- and the editor opens on Intermediate, so for most people the message
    // named a section that was not there.
    CHECK(layerShowsPath(AuthoringLayer::Intermediate, "shader/bloomcurve/intensity"));
    // Beginner too: a shader layer is not something stumbled into, it is there because somebody
    // loaded a file on purpose, and its inputs are the reason they did.
    CHECK(layerShowsPath(AuthoringLayer::Beginner, "shader/bloomcurve/intensity"));
    CHECK(layerShowsPath(AuthoringLayer::Advanced, "shader/bloomcurve/intensity"));

    // The filter still filters: procedural internals stay off the beginner layer.
    CHECK_FALSE(layerShowsPath(AuthoringLayer::Beginner, "procedural/trees/hierarchy/depth"));
    CHECK(layerShowsPath(AuthoringLayer::Intermediate, "procedural/trees/hierarchy/depth"));
    // And a prefix match is a path-segment match, not a substring: a group merely *containing*
    // "shader" is not the shader group.
    CHECK_FALSE(layerShowsPath(AuthoringLayer::Beginner, "shaders_legacy/x"));
}

// ---- who the viewport's pointer belongs to ------------------------------------------------------

TEST_CASE("The left button belongs to the editor, and the camera is on a modifier", "[ui][viewport]") {
    using ui::ViewportIntent;
    // The gesture this whole rule exists for. A bare left drag over the world used to start a camera
    // orbit, because the camera decided at the button-down and the editor's box selection could not
    // claim the pointer until the drag had travelled. Measured on the scripted editor run before
    // the fix: the camera moved 71.9 m during the box drag and the box caught 0 objects.
    CHECK(ui::viewportIntent(true, false, false, false, false) == ViewportIntent::EditorPointer);

    // Shift still means "add to the selection", so a shift-drag is still the editor's: it extends a
    // box. It must not have stayed a camera pan, which is what it used to be.
    CHECK(ui::viewportIntent(true, false, false, false, true) == ViewportIntent::EditorPointer);

    // The camera lives behind the one modifier, with pan on the same modifier plus shift so that a
    // laptop trackpad -- which has no middle button -- can still pan.
    CHECK(ui::viewportIntent(true, false, false, true, false) == ViewportIntent::CameraOrbit);
    CHECK(ui::viewportIntent(true, false, false, true, true) == ViewportIntent::CameraPan);

    // The other buttons are unconditionally the camera's, whatever the editor is doing, so looking
    // around never stops being possible.
    CHECK(ui::viewportIntent(false, true, false, false, false) == ViewportIntent::CameraPan);
    CHECK(ui::viewportIntent(false, false, true, false, false) == ViewportIntent::CameraLook);
    CHECK(ui::viewportIntent(false, true, false, true, true) == ViewportIntent::CameraPan);
    CHECK(ui::viewportIntent(false, false, true, true, true) == ViewportIntent::CameraLook);

    // No button is no gesture.
    CHECK(ui::viewportIntent(false, false, false, false, false) == ViewportIntent::None);
    CHECK(ui::viewportIntent(false, false, false, true, true) == ViewportIntent::None);
}

TEST_CASE("Every camera gesture stays reachable, and only those are the camera's", "[ui][viewport]") {
    using ui::ViewportIntent;
    // Exhaustive over the five inputs, asserting two properties rather than enumerating outcomes:
    // the editor never loses the bare left button, and the camera is never reached without either a
    // modifier or a different button. A future binding that violates either fails here.
    bool sawOrbit = false;
    bool sawPan = false;
    bool sawLook = false;
    for (int bits = 0; bits < 32; ++bits) {
        const bool left = (bits & 1) != 0;
        const bool middle = (bits & 2) != 0;
        const bool right = (bits & 4) != 0;
        const bool alt = (bits & 8) != 0;
        const bool shift = (bits & 16) != 0;
        const ViewportIntent intent = ui::viewportIntent(left, middle, right, alt, shift);
        INFO("left " << left << " middle " << middle << " right " << right << " alt " << alt
                     << " shift " << shift);
        if (left && !middle && !right && !alt) {
            CHECK(intent == ViewportIntent::EditorPointer); // with or without shift
        }
        if (ui::intentIsCamera(intent)) {
            CHECK((middle || right || alt)); // never the bare left button
        }
        sawOrbit = sawOrbit || intent == ViewportIntent::CameraOrbit;
        sawPan = sawPan || intent == ViewportIntent::CameraPan;
        sawLook = sawLook || intent == ViewportIntent::CameraLook;
    }
    // All three camera moves still have a binding. A rule that made selection work by making the
    // camera unreachable would pass every check above and be useless.
    CHECK(sawOrbit);
    CHECK(sawPan);
    CHECK(sawLook);
}

TEST_CASE("A click on a panel is not a click on the world", "[ui][viewport]") {
    // The bug this encodes: clicking the sequencer's timeline selected something in the world.
    //
    // The routing gate asked one question -- was the canvas hovered -- and that answer is
    // necessarily a frame old, because SDL events are read before the frame is laid out. At 60 Hz a
    // frame is 16 ms and nobody outruns it. In a heavy scene a frame is 80 ms or more, which is
    // ample time to move off the canvas onto the timeline and click while the stale answer still
    // says "the world". The heavier the scene, the more reliably it happened, which is exactly the
    // wrong way round for anyone trying to reproduce it.
    SECTION("a stale hover does not survive the pointer being somewhere else") {
        CHECK_FALSE(ui::viewportOwnsPointer(true, false, false));
    }
    SECTION("the ordinary case still works") {
        CHECK(ui::viewportOwnsPointer(true, true, false));
    }
    SECTION("inside the rectangle is not enough on its own") {
        // A panel floating over the canvas is inside the rectangle too. There the hover state is the
        // only thing that knows better, so it still has to agree.
        CHECK_FALSE(ui::viewportOwnsPointer(false, true, false));
    }
    SECTION("a gesture keeps the mouse wherever it travels") {
        // Releasing a drag over a panel must still reach the viewport, or the orbit never ends and
        // the next click anywhere is treated as part of it.
        CHECK(ui::viewportOwnsPointer(false, false, true));
        CHECK(ui::viewportOwnsPointer(true, false, true));
    }
}
