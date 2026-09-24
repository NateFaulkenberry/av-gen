// ADR-710: the march's sample schedule (`world::marchSchedule`, the CPU twin of
// `shaders/march_schedule.wgsl`).
//
// What is asserted is the contract the march relies on, not a particular set of numbers:
//   * with no medium on the ray the environment fog is marched EXACTLY as it always was, so every
//     scene without a placed medium -- and every pixel whose ray misses every medium -- is unmoved;
//   * the authored step count lands inside the media's intervals, which is the whole change;
//   * the segments tile the ray in order with no overlap, so transmittance accumulates front to
//     back and nothing is integrated twice.

#include "world/march_schedule.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using Catch::Approx;
using namespace avgen;

namespace {

constexpr glm::vec2 kNone(1.0f, -1.0f);

std::array<glm::vec2, world::kMarchMaxIntervals> only(glm::vec2 a, glm::vec2 b = kNone, glm::vec2 c = kNone,
                                                      glm::vec2 d = kNone) {
    return {a, b, c, d, kNone, kNone, kNone, kNone};
}

// The segments are in order, do not overlap, and (when `ambient`) tile [0, maxDistance] exactly.
void requireTiled(const world::MarchSchedule& s, float maxDistance, bool ambient) {
    float cursor = 0.0f;
    for (std::uint32_t i = 0; i < s.segmentCount; ++i) {
        const world::MarchSegment& g = s.segments[i];
        REQUIRE(g.count > 0);
        REQUIRE(g.step > 0.0f);
        if (ambient) {
            CHECK(g.start == Approx(cursor).margin(1e-3));
        } else {
            CHECK(g.start >= cursor - 1e-3f);
        }
        cursor = g.start + g.step * static_cast<float>(g.count);
    }
    if (ambient) {
        CHECK(cursor == Approx(maxDistance).margin(1e-2));
    }
    CHECK(cursor <= maxDistance + 1e-2f);
}

int mediumSamples(const world::MarchSchedule& s) {
    int n = 0;
    for (std::uint32_t i = 0; i < s.segmentCount; ++i) {
        n += s.segments[i].medium ? s.segments[i].count : 0;
    }
    return n;
}

} // namespace

TEST_CASE("with no medium on the ray the environment fog is marched exactly as before", "[volume][bound]") {
    const float maxDistance = 320.0f;
    const int steps = 12;
    const world::MarchSchedule s = world::marchSchedule(only(kNone), maxDistance, steps, true);
    REQUIRE(s.segmentCount == 1);
    // Bit-exact rather than approximate: the legacy loop computed `maxDistance / steps` and placed
    // sample i at `(i + jitter) * stepLength`, and `0 + x` is `x`.
    CHECK(s.segments[0].start == 0.0f);
    CHECK(s.segments[0].step == maxDistance / static_cast<float>(steps));
    CHECK(s.segments[0].count == steps);
    CHECK_FALSE(s.segments[0].medium);
}

TEST_CASE("with no medium and no environment fog the ray has nothing to march", "[volume][bound]") {
    const world::MarchSchedule s = world::marchSchedule(only(kNone), 4000.0f, 32, false);
    CHECK(s.segmentCount == 0);
    CHECK(world::marchSampleCount(s) == 0);
}

TEST_CASE("the authored steps land inside the medium, not across the whole ray", "[volume][bound]") {
    // ADR-708's hero: 32 steps over 4 km, a funnel bound about 400 m deep on this ray. The legacy
    // grid put 125 m between samples, so about three landed in the bound.
    const float maxDistance = 4000.0f;
    const int steps = 32;
    const glm::vec2 funnel(3800.0f - 400.0f, 3800.0f);
    const float legacy = maxDistance / static_cast<float>(steps);

    SECTION("with the environment fog off, every sample is in the medium") {
        const world::MarchSchedule s = world::marchSchedule(only(funnel), maxDistance, steps, false);
        REQUIRE(s.segmentCount == 1);
        CHECK(s.segments[0].medium);
        CHECK(s.segments[0].count == steps);
        CHECK(s.segments[0].start == funnel.x);
        CHECK(s.segments[0].step == Approx(400.0f / 32.0f));
        requireTiled(s, maxDistance, false);
    }
    SECTION("with the environment fog on, the gaps keep the legacy grid and the medium still gets the steps") {
        const world::MarchSchedule s = world::marchSchedule(only(funnel), maxDistance, steps, true);
        requireTiled(s, maxDistance, true);
        CHECK(mediumSamples(s) >= steps);
        for (std::uint32_t i = 0; i < s.segmentCount; ++i) {
            const world::MarchSegment& g = s.segments[i];
            if (g.medium) {
                CHECK(g.step <= legacy);
            } else {
                // A gap is whole legacy cells: its step is the legacy step and it starts on the grid.
                CHECK(g.step == Approx(legacy).epsilon(1e-4));
                const float cells = g.start / legacy;
                CHECK(std::abs(cells - std::round(cells)) < 1e-3f);
            }
        }
    }
}

TEST_CASE("a medium never gets fewer samples than the legacy grid gave it", "[volume][bound]") {
    // A fog bank that fills most of the ray: the spacing inside it is capped at the legacy step, so
    // redistribution can only make a medium finer, never coarser.
    const world::MarchSchedule s = world::marchSchedule(only({10.0f, 3990.0f}), 4000.0f, 32, true);
    requireTiled(s, 4000.0f, true);
    for (std::uint32_t i = 0; i < s.segmentCount; ++i) {
        CHECK(s.segments[i].step <= 4000.0f / 32.0f + 1e-3f);
    }
}

TEST_CASE("overlapping intervals merge, disjoint ones share the budget by length", "[volume][bound]") {
    SECTION("overlapping") {
        const world::MarchSchedule s =
            world::marchSchedule(only({100.0f, 300.0f}, {250.0f, 500.0f}), 4000.0f, 32, false);
        REQUIRE(s.segmentCount == 1);
        CHECK(s.segments[0].start == 100.0f);
        CHECK(s.segments[0].start + s.segments[0].step * s.segments[0].count == Approx(500.0f));
    }
    SECTION("disjoint and out of order: sorted, and a three-times-longer span gets three times the samples") {
        const world::MarchSchedule s =
            world::marchSchedule(only({1000.0f, 1300.0f}, {100.0f, 200.0f}), 4000.0f, 32, false);
        REQUIRE(s.segmentCount == 2);
        CHECK(s.segments[0].start == 100.0f);
        CHECK(s.segments[1].start == 1000.0f);
        CHECK(s.segments[0].count == 8);
        CHECK(s.segments[1].count == 24);
        CHECK(s.segments[0].step == Approx(s.segments[1].step));
    }
    SECTION("eight disjoint intervals with the environment fog on use all seventeen segments") {
        std::array<glm::vec2, world::kMarchMaxIntervals> eight{};
        for (std::size_t i = 0; i < eight.size(); ++i) {
            // Out of order, on purpose.
            const float a = 250.0f + 450.0f * static_cast<float>((i * 5) % eight.size());
            eight[i] = glm::vec2(a, a + 100.0f);
        }
        const world::MarchSchedule s = world::marchSchedule(eight, 4000.0f, 32, true);
        CHECK(s.segmentCount == world::kMarchMaxSegments);
        requireTiled(s, 4000.0f, true);
    }
}

TEST_CASE("the march's cost is bounded by twice the authored steps plus one per medium", "[volume][bound]") {
    // The worst case: the environment fog on (the whole legacy grid, less the cells the media take)
    // and the media taking the whole budget again. A grazing ray through a sliver of a bound is the
    // shape that drives the in-medium count to its cap.
    const int steps = 32;
    for (const float sliver : {0.01f, 1.0f, 50.0f, 900.0f}) {
        const world::MarchSchedule s =
            world::marchSchedule(only({700.0f, 700.0f + sliver}, {2500.0f, 2500.0f + sliver}), 4000.0f, steps, true);
        INFO("sliver " << sliver);
        CHECK(world::marchSampleCount(s) <= 2 * steps + 4);
        CHECK(mediumSamples(s) <= steps + 4);
    }
}

// ADR-710's seam, as a property. The spacing is the union's length over the steps, so as a ray
// sweeps across a funnel and starts to graze a cloud far behind it, the spacing must change
// smoothly. A HULL of the two jumps by the whole gap between them at the first graze -- the diagonal
// seam the first version drew through the hero -- and a union does not.
TEST_CASE("the medium spacing is continuous as a second interval appears", "[volume][bound]") {
    const glm::vec2 funnel(3000.0f, 3600.0f);
    float previous = -1.0f;
    float worstJump = 0.0f;
    for (int i = 0; i <= 200; ++i) {
        // The cloud's chord grows from nothing, 800 m behind the funnel.
        const float chord = 2.0f * static_cast<float>(i);
        const glm::vec2 cloud(4400.0f - 0.5f * chord, 4400.0f + 0.5f * chord);
        const world::MarchSchedule s = world::marchSchedule(only(funnel, cloud), 6000.0f, 32, false);
        REQUIRE(s.segmentCount >= 1);
        const float spacing = s.segments[0].step; // the funnel's
        if (previous > 0.0f) {
            worstJump = std::max(worstJump, std::abs(spacing - previous) / previous);
        }
        previous = spacing;
    }
    INFO("worst frame-to-frame change in the funnel's spacing: " << worstJump * 100.0f << "%");
    // Each step adds 2 m of cloud to 600 m of funnel, a third of a percent. What remains is the
    // integer sample count: a span's spacing is its length over a whole number of samples, so it
    // moves by up to 1/count when that number changes (measured 5.0% at the smallest counts here).
    // A hull jumps by 130% at the first graze.
    CHECK(worstJump < 0.08f);
}
