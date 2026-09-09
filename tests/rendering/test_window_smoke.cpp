// Milestone 1.2: window smoke tests. Only what can run without a user present: display
// enumeration (skipped when no video subsystem is available).
#include "platform/window.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace avgen;

TEST_CASE("Window::displays lists at least one display with bounds", "[platform][window]") {
    const auto displays = platform::Window::displays();
    if (displays.empty()) {
        SKIP("no display available (headless)");
    }
    bool primary = false;
    for (const auto& d : displays) {
        INFO("display " << d.index << " '" << d.name << "' " << d.width << "x" << d.height << " @" << d.refreshRate);
        CHECK(d.index >= 0);
        CHECK(d.width > 0);
        CHECK(d.height > 0);
        CHECK(d.scale > 0.0f);
        primary = primary || d.primary;
    }
    CHECK(primary);
    CHECK(displays[0].index == 0);
    CHECK(platform::Window::liveCount() == 0);
}
