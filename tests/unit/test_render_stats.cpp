// The arithmetic behind the frame's statistics (rendering/render_stats.hpp, ADR-077).
//
// None of this needs a device, and all of it is the part that can be quietly wrong. A submitted
// triangle count that wraps at 2^32 looks like a plausible small number rather than an obviously
// broken one; a removal A/B whose per-label deltas do not sum to the frame delta will confidently
// attribute a saving to the wrong pass; a median that fills a missing label with zero turns "this
// pass did not run" into "this pass was free". Each of those is pinned here.

#include "rendering/render_stats.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using avgen::gpu::TimelineInterval;
using namespace avgen::rendering;

namespace {

double deltaFor(const RemovalAttribution& a, const std::string& label) {
    const auto it = std::find_if(a.byLabel.begin(), a.byLabel.end(),
                                 [&](const auto& e) { return e.label == label; });
    return it == a.byLabel.end() ? 0.0 : it->ms;
}

double sumOfDeltas(const RemovalAttribution& a) {
    double total = 0.0;
    for (const auto& e : a.byLabel) {
        total += e.ms;
    }
    return total;
}

} // namespace

TEST_CASE("a submitted triangle count does not wrap at four billion", "[render-stats]") {
    // 100,000 triangles of source against 50,000 instances is five billion triangles submitted.
    // That is not a realistic frame; it is a realistic *mistake*, and a 32-bit product of the two
    // reports 705,032,704, which is both wrong and entirely believable.
    CHECK(drawTriangles(300'000u, 50'000u) == 5'000'000'000ull);
    // The everyday case, and the one the audit quoted: a 132-triangle source scattered 114,296
    // times is 15.1 M, which fits in 32 bits and must come out identical either way.
    CHECK(drawTriangles(396u, 114'296u) == 15'087'072ull);
    // Index counts come from meshes, which are triangle lists; a count that is not a multiple of
    // three is a malformed mesh, and rounding it up would invent geometry.
    CHECK(drawTriangles(5u, 2u) == 2ull);
    CHECK(drawTriangles(0u, 1000u) == 0ull);
}

TEST_CASE("a draw whose instance count is unknown adds no triangles and still counts as a draw",
          "[render-stats]") {
    // The instance counts behind indirect draws are written by the cull pass on the GPU. Before the
    // first readback lands the CPU has no number at all, and the temptation is to charge the draw
    // the object's whole record count -- which puts geometry in the frame that no pass drew.
    SubmittedGeometry g;
    g.record(300u, 10u, false); // a direct draw: exact
    g.record(300u, 7u, true);   // an indirect draw against the last completed cull readback
    g.recordUnmeasured();       // an indirect draw with no readback behind it yet

    CHECK(g.draws == 3u);
    CHECK(g.estimatedDraws == 1u);
    CHECK(g.unmeasuredDraws == 1u);
    // Only the two draws with counts contribute; the third is visible in the draw total and
    // nowhere else, so `triangles` is a floor and says so.
    CHECK(g.triangles == 100ull * 17ull);
    CHECK(g.instances == 17ull);

    // The per-pass budgets are added up in one place, and every field has to survive it -- a
    // `total()` that forgot `unmeasuredDraws` would report a frame whose geometry is complete.
    GeometryCounters counters;
    counters.camera = g;
    counters.depth = g;
    counters.shadow = g;
    const SubmittedGeometry all = counters.total();
    CHECK(all.draws == 9u);
    CHECK(all.estimatedDraws == 3u);
    CHECK(all.unmeasuredDraws == 3u);
    CHECK(all.triangles == 3ull * 100ull * 17ull);
}

TEST_CASE("a removal A/B's per-label deltas account for the whole frame delta", "[render-stats]") {
    // The measured case this exists for. `--disable volume` took 1.44 ms out of a 24.25 ms frame
    // while the volume pass reported 0.85 -- and because the timeline's intervals partition the
    // frame, the other 0.59 ms cannot have gone missing. It is in the other labels.
    const std::vector<TimelineInterval> baseline = {
        {"scene", 20.71}, {"shadow", 0.92}, {"volume", 0.85}, {"depth", 0.33}, {"tonemap", 0.07}};
    const std::vector<TimelineInterval> arm = {
        {"scene", 20.30}, {"shadow", 0.90}, {"depth", 0.31}, {"tonemap", 0.07}};

    const RemovalAttribution a = attributeRemoval(baseline, arm, "volume");
    CHECK_THAT(a.frameDeltaMs, WithinAbs(1.30, 1e-9));
    CHECK_THAT(a.removedPassMs, WithinAbs(0.85, 1e-9));
    CHECK_THAT(a.elsewhereMs, WithinAbs(0.45, 1e-9));
    // The property that makes the attribution worth anything: nothing is dropped, so the deltas
    // sum to the frame delta and "where did the rest of the saving come from" has an answer.
    CHECK_THAT(sumOfDeltas(a), WithinAbs(a.frameDeltaMs, 1e-9));
    CHECK_THAT(deltaFor(a, "volume"), WithinAbs(0.85, 1e-9));
    CHECK_THAT(deltaFor(a, "scene"), WithinAbs(0.41, 1e-9));
    // Largest first: the top line is the pass that gave up the most.
    REQUIRE(a.byLabel.size() == 5);
    CHECK(a.byLabel.front().label == "volume");
}

TEST_CASE("a pass that only the arm has is not silently dropped from a removal A/B", "[render-stats]") {
    // Switching a phase off can bring a pass into existence -- a fallback path, a clear that no
    // longer has anything in front of it. Counting only the labels the baseline had would leave
    // that cost out of the attribution and make the deltas stop summing to the frame delta, which
    // is exactly the failure this whole file exists to prevent.
    const std::vector<TimelineInterval> baseline = {{"scene", 10.0}, {"volume", 2.0}};
    const std::vector<TimelineInterval> arm = {{"scene", 10.0}, {"fallback", 0.5}};

    const RemovalAttribution a = attributeRemoval(baseline, arm, "volume");
    CHECK_THAT(a.frameDeltaMs, WithinAbs(1.5, 1e-9));
    CHECK_THAT(deltaFor(a, "fallback"), WithinAbs(-0.5, 1e-9));
    CHECK_THAT(sumOfDeltas(a), WithinAbs(a.frameDeltaMs, 1e-9));
    // The phase cost more than it was charged for by -0.5 ms: removing it added work elsewhere.
    CHECK_THAT(a.elsewhereMs, WithinAbs(-0.5, 1e-9));
}

TEST_CASE("passes sharing a label are summed before they are compared", "[render-stats]") {
    // Two shadow cascades, a bloom pyramid and a scene pass split around the SDF raymarch all mark
    // the timeline more than once under one name. A comparison that took the first occurrence
    // would report a two-cascade frame as costing one cascade.
    const std::vector<TimelineInterval> frame = {
        {"shadow", 0.4}, {"shadow", 0.5}, {"scene", 20.0}, {"post/bloom", 0.2}, {"post/bloom", 0.3}};
    const auto summed = sumByLabel(frame);
    REQUIRE(summed.size() == 3);
    CHECK(summed[0].label == "shadow");
    CHECK_THAT(summed[0].ms, WithinAbs(0.9, 1e-9));
    CHECK(summed[1].label == "scene"); // submission order, not alphabetical or by cost
    CHECK_THAT(summed[2].ms, WithinAbs(0.5, 1e-9));
}

TEST_CASE("a label missing from a frame contributes no sample rather than a zero", "[render-stats]") {
    // "This pass did not run" and "this pass cost nothing" are different claims, and a median that
    // pads the first into the second halves the reported cost of anything intermittent -- a debug
    // pass, a shadow view that only some frames need, a post layer that switches itself off.
    const std::vector<std::vector<TimelineInterval>> frames = {
        {{"scene", 20.0}, {"debug", 1.0}},
        {{"scene", 20.0}},
        {{"scene", 20.0}},
        {{"scene", 20.0}, {"debug", 1.0}},
        {{"scene", 20.0}, {"debug", 1.2}},
    };
    const auto medians = medianByLabel(frames);
    REQUIRE(medians.size() == 2);
    CHECK(medians[0].label == "scene");
    CHECK_THAT(medians[0].ms, WithinAbs(20.0, 1e-9));
    CHECK(medians[1].label == "debug");
    // Three samples -- 1.0, 1.0, 1.2 -- and a median of 1.0. Padded with two zeros it would be 1.0
    // as well by luck, so the value that distinguishes the two is checked with an even count too.
    CHECK_THAT(medians[1].ms, WithinAbs(1.0, 1e-9));

    const std::vector<std::vector<TimelineInterval>> sparse = {
        {{"scene", 20.0}, {"debug", 4.0}}, {{"scene", 20.0}}, {{"scene", 20.0}}, {{"scene", 20.0}}};
    const auto sparseMedians = medianByLabel(sparse);
    REQUIRE(sparseMedians.size() == 2);
    // One sample of 4.0 ms. Zero-filled to four samples the median would be 0.0, and the pass
    // would read as free in every report built on it.
    CHECK_THAT(sparseMedians[1].ms, WithinAbs(4.0, 1e-9));
}

TEST_CASE("the CPU stages account for the frame they were measured inside", "[render-stats]") {
    CpuFrameBreakdown cpu;
    cpu.uploadsMs = 0.10;
    cpu.lightsMs = 0.20;
    cpu.objectsMs = 0.30;
    cpu.sceneEncodeMs = 0.40;
    cpu.totalMs = 1.05;
    CHECK_THAT(cpu.stagesMs(), WithinAbs(1.00, 1e-9));
    CHECK_THAT(cpu.unattributedMs(), WithinAbs(0.05, 1e-9));

    // The stages are timed by their own clock reads inside the interval the total is timed over,
    // so rounding can put the sum a nanosecond past it. A residual of -0.0000001 ms reads as a
    // stage being double-counted, which it is not.
    cpu.totalMs = 0.999'999'999;
    CHECK_THAT(cpu.unattributedMs(), WithinAbs(0.0, 1e-9));
    CHECK(cpu.unattributedMs() >= 0.0);
}
