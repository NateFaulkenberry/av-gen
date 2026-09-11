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
