// The arithmetic of the frame GPU timeline (gpu/timeline_math.hpp).
//
// The instrument this replaced measured each pass with its own begin/end pair, which does not
// measure that pass: the begin timestamp is written when the pass is reached, not when its work
// starts, so a pass behind a heavy one absorbed the drain of everything ahead of it. The volume
// pass reported 39.4 ms of a 46 ms frame for 5.4 ms of work. What is tested here is the property
// that fixes it -- consecutive pass ends on one timeline partition the frame exactly -- and the
// driver quirk that nearly broke it.

#include "gpu/timeline_math.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <numeric>
#include <string>

using Catch::Matchers::WithinAbs;
using avgen::gpu::timelineIntervals;

namespace {

double sumOf(const avgen::gpu::TimelineSpan& span) {
    return std::accumulate(span.passes.begin(), span.passes.end(), 0.0,
                           [](double acc, const auto& p) { return acc + p.ms; });
}

} // namespace

TEST_CASE("frame timeline charges each pass the interval since the previous pass ended", "[frame-timeline]") {
    // Origin, then three pass ends at 1 ms, 5 ms and 6 ms after it (timestamps are nanoseconds).
    const std::array<std::uint64_t, 4> ts = {1'000'000'000ull, 1'001'000'000ull, 1'005'000'000ull,
                                             1'006'000'000ull};
    const std::array<std::string, 4> labels = {"", "shadow", "scene", "tonemap"};

    const auto span = timelineIntervals(ts.data(), labels.data(), ts.size());
    REQUIRE(span.passes.size() == 3);
    CHECK(span.passes[0].label == "shadow");
    CHECK_THAT(span.passes[0].ms, WithinAbs(1.0, 1e-9));
    CHECK(span.passes[1].label == "scene");
    CHECK_THAT(span.passes[1].ms, WithinAbs(4.0, 1e-9));
    CHECK(span.passes[2].label == "tonemap");
    CHECK_THAT(span.passes[2].ms, WithinAbs(1.0, 1e-9));
    CHECK(span.unwritten == 0);

    // The point of the whole design: the passes partition the frame, so they sum to it. A per-pass
    // begin/end instrument has no such guarantee and did not have it in practice.
    CHECK_THAT(span.frameMs, WithinAbs(6.0, 1e-9));
    CHECK_THAT(sumOf(span), WithinAbs(span.frameMs, 1e-9));
}

TEST_CASE("frame timeline survives a slot the driver never wrote", "[frame-timeline]") {
    // Metal writes no end-of-pass timestamp for a render pass that issues no draws, and the slot
    // resolves as a literal zero. Subtracting the next timestamp from it yields a raw counter
    // value: the world scene's depth prepass once reported 228,832,448 ms this way.
    const std::array<std::uint64_t, 5> ts = {1'000'000'000ull, 1'001'000'000ull, 0ull, 1'004'000'000ull,
                                             1'005'000'000ull};
    const std::array<std::string, 5> labels = {"", "shadow", "background", "depth", "tonemap"};

    const auto span = timelineIntervals(ts.data(), labels.data(), ts.size());
    REQUIRE(span.passes.size() == 4);
    CHECK(span.unwritten == 1);
    // The empty pass is charged nothing...
    CHECK(span.passes[1].label == "background");
    CHECK_THAT(span.passes[1].ms, WithinAbs(0.0, 1e-9));
    // ...and the boundary stays where it was, so the next pass covers both rather than the frame
    // origin being mistaken for a pass end.
    CHECK(span.passes[2].label == "depth");
    CHECK_THAT(span.passes[2].ms, WithinAbs(3.0, 1e-9));
    // The frame is still the whole span and the passes still sum to it.
    CHECK_THAT(span.frameMs, WithinAbs(5.0, 1e-9));
    CHECK_THAT(sumOf(span), WithinAbs(span.frameMs, 1e-9));
}

TEST_CASE("frame timeline reports nothing rather than nonsense when there is nothing to report",
          "[frame-timeline]") {
    const std::array<std::uint64_t, 1> one = {42ull};
    CHECK(timelineIntervals(one.data(), nullptr, 1).passes.empty());
    CHECK(timelineIntervals(one.data(), nullptr, 1).frameMs < 0.0);
    CHECK(timelineIntervals(nullptr, nullptr, 8).passes.empty());

    // A counter that ran backwards (a reset, a wrapped domain) is not a negative pass.
    const std::array<std::uint64_t, 3> backwards = {1'000'000'000ull, 900'000'000ull, 1'002'000'000ull};
    const auto span = timelineIntervals(backwards.data(), nullptr, backwards.size());
    REQUIRE(span.passes.size() == 2);
    CHECK_THAT(span.passes[0].ms, WithinAbs(0.0, 1e-9));
    CHECK_THAT(span.passes[1].ms, WithinAbs(2.0, 1e-9));
    CHECK(span.unwritten == 1);
    CHECK_THAT(sumOf(span), WithinAbs(span.frameMs, 1e-9));
}
