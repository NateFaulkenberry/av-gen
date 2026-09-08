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
