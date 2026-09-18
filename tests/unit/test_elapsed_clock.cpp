// A duration a person reads off a progress line.
//
// `%.0f s elapsed` stops being a duration somewhere around a minute: "4231 s" is a number you have
// to do arithmetic on before it means anything, and a render long enough to print that is exactly
// when you want to know at a glance.

#include "ui/ui_logic.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace avgen::ui;

TEST_CASE("an elapsed duration reads as a clock", "[ui][clock]") {
    // Under an hour there is no hours field, so a short render is not padded with a leading zero
    // that never changes.
    CHECK(elapsedClock(0.0) == "0:00");
    CHECK(elapsedClock(9.0) == "0:09");
    CHECK(elapsedClock(59.9) == "0:59");   // truncates rather than rounding up into the next minute
    CHECK(elapsedClock(60.0) == "1:00");
    CHECK(elapsedClock(599.0) == "9:59");

    // The case that prompted this: an hour-plus render used to print its whole duration in seconds.
    CHECK(elapsedClock(3600.0) == "1:00:00");
    CHECK(elapsedClock(3661.0) == "1:01:01");
    CHECK(elapsedClock(4231.0) == "1:10:31");
    CHECK(elapsedClock(7530.0) == "2:05:30");

    // The control that the minute field is genuinely two digits and not just short: 2:05:30 and
    // 2:50:30 must not collapse to the same string, which a "%d:%d:%02d" would do at 2:5:30.
    CHECK(elapsedClock(7530.0) != elapsedClock(10230.0));
    CHECK(elapsedClock(10230.0) == "2:50:30");

    // What the old m:ss helper next door still does, and the reason this one exists: ninety minutes
    // reads as an hour and a half rather than as 125 minutes.
    CHECK(elapsedClock(7530.0) != "125:30");

    // Not a duration at all. A render that has not started, or a NaN out of a division by zero
    // frames, must not print "0:00" as though it had run for no time.
    CHECK(elapsedClock(-1.0) == "--:--");
    CHECK(elapsedClock(std::nan("")) == "--:--");
    CHECK(elapsedClock(std::numeric_limits<double>::infinity()) == "--:--");
}
