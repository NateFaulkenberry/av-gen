// The render's clock steps exactly 1/fps (ADR-990).
//
// FixedStepClock used to hand out deltaTime = next instant - previous instant. That differs from
// 1/fps in the last bits, while a seek's replay, the cast trace and the test harness all step
// exactly 1/fps. The autonomous cast is chaotic enough to grow the difference: on Glowmere Valley 3
// a render from 0 was 0.7 mm from a seek of the same film at 6.28 s and up to 114 m by 168 s.

#include "core/time.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace avgen;

TEST_CASE("the render's clock hands out exactly one step per frame", "[time][clock][adr990]") {
    for (const double fps : {24.0, 30.0, 59.94, 60.0, 120.0}) {
        for (const double start : {0.0, 12.5, 168.5}) {
            FixedStepClock clock(fps, start);
            const FrameTime first = clock.tick();
            CHECK(first.deltaTime == 0.0);
            CHECK(first.renderTime == start);
            int differing = 0; // frames where the old delta (a difference of instants) was not 1/fps
            double previous = first.renderTime;
            for (int f = 1; f <= 20000; ++f) {
                const FrameTime t = clock.tick();
                REQUIRE(t.deltaTime == 1.0 / fps);
                REQUIRE(t.renderTime == start + static_cast<double>(f) / fps);
                REQUIRE(t.frameIndex == static_cast<std::uint64_t>(f));
                if (t.renderTime - previous != 1.0 / fps) {
                    ++differing;
                }
                previous = t.renderTime;
            }
            // The control: the old formula really does differ, so this case would have caught it.
            INFO("fps " << fps << " start " << start);
            CHECK(differing > 0);
        }
    }
}

TEST_CASE("a seek or a restart leaves the render's clock stepping exactly 1/fps", "[time][clock][adr990]") {
    FixedStepClock clock(60.0);
    clock.tick();
    clock.tick();
    clock.seek(90.25);
    for (int f = 1; f <= 600; ++f) {
        const FrameTime t = clock.tick();
        REQUIRE(t.deltaTime == 1.0 / 60.0);
        REQUIRE(t.renderTime == 90.25 + static_cast<double>(f) / 60.0);
    }
    clock.restartAt(10.0);
    const FrameTime again = clock.tick();
    CHECK(again.deltaTime == 0.0);
    CHECK(again.renderTime == 10.0);
    CHECK(clock.tick().deltaTime == 1.0 / 60.0);
}
