// Regression: the route-amount slider range must not feed back on the value it edits.
#include <set>
#include "ui/ui_logic.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

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

TEST_CASE("Recent files are labelled by what tells them apart", "[ui][recent]") {
    // "Open Recent" listed `night-shift.json` twice and Dear ImGui warned about a duplicate id.
    // They were not duplicates: one lived in the checkout and one in an agent's worktree. The list
    // was right; the label told the reader nothing and gave two menu items the same identity.
    SECTION("a unique file name stays a file name") {
        const std::vector<std::filesystem::path> paths = {"/a/b/one.json", "/c/d/two.json"};
        const auto labels = ui::uniqueFileLabels(paths);
        CHECK(labels[0] == "one.json");
        CHECK(labels[1] == "two.json");
    }

    SECTION("a shared name grows by as much as it takes, and only where it is needed") {
        const std::vector<std::filesystem::path> paths = {
            "/Users/x/av-gen/examples/city/night-shift.json",
            "/Users/x/av-gen-wt-seqaudio/examples/city/night-shift.json",
            "/Users/x/av-gen/examples/world/terrain.json"};
        const auto labels = ui::uniqueFileLabels(paths);
        // The one that was never ambiguous is untouched -- the whole menu must not become paths.
        CHECK(labels[2] == "terrain.json");
        // The two that clashed now differ, and each still ends in the file it names.
        CHECK(labels[0] != labels[1]);
        CHECK(labels[0].ends_with("night-shift.json"));
        CHECK(labels[1].ends_with("night-shift.json"));
        // Only as far up as it takes: these separate at the checkout, four segments in, so neither
        // label is the whole absolute path.
        CHECK(labels[0].find("/Users/") == std::string::npos);
        CHECK(labels[1].find("/Users/") == std::string::npos);
        INFO("labels: '" << labels[0] << "' and '" << labels[1] << "'");
        CHECK(labels[0].find("av-gen/") != std::string::npos);
        CHECK(labels[1].find("av-gen-wt-seqaudio/") != std::string::npos);
    }

    SECTION("three sharing a name all separate") {
        const std::vector<std::filesystem::path> paths = {
            "/r/one/city/night-shift.json", "/r/two/city/night-shift.json",
            "/r/three/city/night-shift.json"};
        const auto labels = ui::uniqueFileLabels(paths);
        std::set<std::string> distinct(labels.begin(), labels.end());
        CHECK(distinct.size() == 3);
    }

    SECTION("identical paths do not loop, and are left equal for the caller to scope") {
        // A symlinked checkout can produce these. The rule cannot separate them, so it stops at the
        // longest form rather than spinning; the PushID at the call site keeps them clickable.
        const std::vector<std::filesystem::path> paths = {"/a/b/same.json", "/a/b/same.json"};
        const auto labels = ui::uniqueFileLabels(paths);
        REQUIRE(labels.size() == 2);
        CHECK(labels[0] == labels[1]);
        CHECK(labels[0].ends_with("same.json"));
    }

    SECTION("an empty list is an empty list") {
        CHECK(ui::uniqueFileLabels({}).empty());
    }
}

// ---- the sequencer strip's lanes --------------------------------------------------------------
//
// Extracted from `SequencePanel::drawStrip` after a reported bug (ADR-103): audio clips were drawn
// as draggable blocks *on* the waveform lane, so a click meant to scrub the music moved the music
// instead, and a piece was reported as starting fourteen seconds in with its name showing twice.
// The drawing and the hit testing have to agree about where a lane is, and that is arithmetic.

TEST_CASE("The audio lane is one lane, and it holds nothing draggable", "[ui][sequencer][lanes]") {
    // A clip is a box with its waveform inside it, in one lane. Both of the ways this was got wrong
    // came from separating those: clips drawn as blocks on a full-width waveform stole the click
    // that scrubs, and clips moved to a lane of their own left the waveform floating above the box
    // it belongs to. One lane, and every point of it answers the same way.
    ui::StripLanes lanes{.hasAudio = true, .actorCount = 2, .hasOverlays = true};

    for (float y = lanes.audioTop(); y < lanes.audioTop() + lanes.audioLaneHeight; y += 0.5f) {
        INFO(y);
        REQUIRE(lanes.at(y) == ui::StripLane::Audio);
    }
    // The shots begin after it, with a gap that belongs to nothing.
    CHECK(lanes.shotsTop() > lanes.audioTop() + lanes.audioLaneHeight);
    CHECK(lanes.at(lanes.audioTop() + lanes.audioLaneHeight + 1.0f) == ui::StripLane::None);

    // Taller than the others, because it carries a picture rather than a label.
    CHECK(lanes.audioLaneHeight > lanes.laneHeight);
}

TEST_CASE("Every lane is where the strip's own height says it is", "[ui][sequencer][lanes]") {
    // The lanes tile the strip in order and none of them runs off the end of it: a lane drawn past
    // the strip's height is one the mouse can never reach.
    for (bool audio : {false, true}) {
        for (std::size_t actors : {std::size_t{0}, std::size_t{1}, std::size_t{4}}) {
            for (bool overlays : {false, true}) {
                ui::StripLanes lanes{.hasAudio = audio, .actorCount = actors, .hasOverlays = overlays};
                INFO("audio=" << audio << " actors=" << actors << " overlays=" << overlays);

                CHECK(lanes.at(0.0f) == ui::StripLane::Ruler);
                CHECK(lanes.at(lanes.lanesTop() - 0.1f) == ui::StripLane::Ruler);
                CHECK(lanes.at(lanes.shotsTop() + 1.0f) == ui::StripLane::Shots);
                CHECK(lanes.shotsTop() + lanes.laneHeight <= lanes.height());
                if (audio) {
                    CHECK(lanes.at(lanes.audioTop() + 1.0f) == ui::StripLane::Audio);
                } else {
                    // With no audio the shots are the first lane, exactly where the audio would have
                    // been -- so a project without audio loses no space to a lane it has not got.
                    CHECK(lanes.shotsTop() == lanes.lanesTop());
                }
                if (actors > 0) {
                    CHECK(lanes.at(lanes.actorsTop() + 1.0f) == ui::StripLane::Actors);
                    CHECK(lanes.at(lanes.overlaysTop() - lanes.gap - 1.0f) == ui::StripLane::Actors);
                }
                if (overlays) {
                    CHECK(lanes.at(lanes.overlaysTop() + 1.0f) == ui::StripLane::Overlays);
                    CHECK(lanes.overlaysTop() + lanes.laneHeight <= lanes.height());
                }
                // Past the bottom is nothing, rather than the last lane extended forever.
                CHECK(lanes.at(lanes.height() + 10.0f) == ui::StripLane::None);
            }
        }
    }
}

TEST_CASE("The audio lane costs nothing when there is no audio", "[ui][sequencer][lanes]") {
    const ui::StripLanes without{.hasAudio = false, .actorCount = 1, .hasOverlays = false};
    const ui::StripLanes with{.hasAudio = true, .actorCount = 1, .hasOverlays = false};
    CHECK(with.height() > without.height());
    CHECK_THAT(static_cast<double>(with.height() - without.height()),
               Catch::Matchers::WithinAbs(static_cast<double>(with.audioLaneHeight + with.gap), 1e-4));
}

// A camera is placed one of three ways and each has its own parameters; the other two families are
// still registered, still exposed and still drag under the mouse without reaching the picture. On
// Glowmere -- a free camera -- that is `camera/distance`, `camera/height` and `camera/orbitSpeed`,
// and it was reported as "changing them doesn't appear to change anything", which is exactly what
// it looks like.
TEST_CASE("The panel can tell which camera parameters the current mode ignores", "[ui][camera]") {
    constexpr int kOrbit = 0;
    constexpr int kFree = 1;
    constexpr int kSpline = 2;

    SECTION("the orbit camera's parameters are ignored in free mode, and only there") {
        for (const char* path : {"camera/distance", "camera/height", "camera/orbitSpeed"}) {
            INFO(path);
            CHECK_FALSE(ui::parameterInertness(path, kOrbit, true).inert);
            CHECK(ui::parameterInertness(path, kFree, true).inert);
            // Spline mode falls back to the orbit placement when the scene names no camera spline,
            // so claiming these are ignored there would be claiming more than is true.
            CHECK_FALSE(ui::parameterInertness(path, kSpline, true).inert);
        }
        const auto inert = ui::parameterInertness("camera/height", kFree, true);
        CHECK(inert.because == "the camera is in free mode");
        CHECK(inert.belongsTo == "the orbit camera");
        // The explanation is also the fix: what to write, and what to call it.
        CHECK(inert.fixPath == "camera/mode");
        CHECK(inert.fixValue == 0.0f);
        CHECK(!inert.fixLabel.empty());
    }

    SECTION("the free camera's are ignored in the other two") {
        for (const char* path : {"camera/position", "camera/target"}) {
            INFO(path);
            CHECK_FALSE(ui::parameterInertness(path, kFree, true).inert);
            CHECK(ui::parameterInertness(path, kOrbit, true).inert);
            CHECK(ui::parameterInertness(path, kSpline, true).inert);
            CHECK(ui::parameterInertness(path, kOrbit, true).fixValue == 1.0f);
        }
    }

    SECTION("the spline camera's are ignored unless the camera is riding one") {
        for (const char* path : {"camera/splineT", "camera/lookAhead", "camera/splineOffset"}) {
            INFO(path);
            CHECK_FALSE(ui::parameterInertness(path, kSpline, true).inert);
            CHECK(ui::parameterInertness(path, kFree, true).inert);
            CHECK(ui::parameterInertness(path, kOrbit, true).fixValue == 2.0f);
        }
    }

    SECTION("an explicit field of view is ignored while the lens is deciding it") {
        CHECK_FALSE(ui::parameterInertness("camera/fov", kFree, true).inert);
        const auto lens = ui::parameterInertness("camera/fov", kFree, false);
        CHECK(lens.inert);
        CHECK(lens.fixPath == "camera/lens/useExplicitFov");
        CHECK(lens.fixValue == 1.0f);
        // ...but the lens's own numbers are never marked: `focalLength` still sets the depth of
        // field through the circle of confusion whichever way the field of view is decided, so
        // calling it ignored would be false.
        CHECK_FALSE(ui::parameterInertness("camera/lens/focalLength", kFree, true).inert);
        CHECK_FALSE(ui::parameterInertness("camera/lens/focalLength", kFree, false).inert);
    }

    SECTION("everything else is left alone") {
        for (const char* path : {"post/bloom/intensity", "nodes/valley/position", "audio/inputGain",
                                 "camera/mode", "camera/shake/amplitude"}) {
            INFO(path);
            for (int mode : {kOrbit, kFree, kSpline}) {
                CHECK_FALSE(ui::parameterInertness(path, mode, true).inert);
                CHECK_FALSE(ui::parameterInertness(path, mode, false).inert);
            }
        }
    }
}
