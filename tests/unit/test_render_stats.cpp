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
#include <cmath>
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

// ---- distributions and the A/B protocol (ADR-113) ----------------------------------------------
//
// The arithmetic below decides whether a renderer change counts as an improvement, so every
// definition in it is pinned against a hand-computed input. The one that matters most is that
// "1% low" and "p99" are different numbers: the games press uses the first name for both, and a
// report that quotes the mean of the slowest frames while calling it a percentile will claim a
// stutter regression that the percentile cannot see, or miss one it can.

TEST_CASE("a percentile is the nearest rank, with no value the sample never took", "[render-stats]") {
    // Ten frames, one per millisecond from 1 to 10. Nearest rank puts q on ceil(q*n): p50 is the
    // fifth frame, p90 the ninth, p99 the tenth. Interpolating between neighbours would report
    // 5.5 ms for p50 -- a frame time no frame in the sample took.
    const std::vector<double> ten = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    CHECK_THAT(percentileOf(ten, 0.50), WithinAbs(5.0, 1e-9));
    CHECK_THAT(percentileOf(ten, 0.90), WithinAbs(9.0, 1e-9));
    CHECK_THAT(percentileOf(ten, 0.95), WithinAbs(10.0, 1e-9));
    CHECK_THAT(percentileOf(ten, 0.99), WithinAbs(10.0, 1e-9));
    // The ends are the ends, not an extrapolation past them.
    CHECK_THAT(percentileOf(ten, 0.0), WithinAbs(1.0, 1e-9));
    CHECK_THAT(percentileOf(ten, 1.0), WithinAbs(10.0, 1e-9));
    // Unsorted input is the normal case -- frame times arrive in the order they happened.
    const std::vector<double> shuffled = {7, 2, 10, 4, 9, 1, 6, 3, 8, 5};
    CHECK_THAT(percentileOf(shuffled, 0.90), WithinAbs(9.0, 1e-9));
    // An empty sample has no percentile; zero is what it returns and `describe` is what a caller
    // should use when it needs to tell that from a zero-millisecond frame.
    CHECK_THAT(percentileOf({}, 0.5), WithinAbs(0.0, 1e-9));
    CHECK_THAT(percentileOf({4.0}, 0.99), WithinAbs(4.0, 1e-9));
}

TEST_CASE("the 1% low is the mean of the slowest frames and is not p99", "[render-stats]") {
    // A hundred frames at 10 ms with one 100 ms stutter in them. This is the case the two
    // statistics disagree about, and the disagreement is the point:
    //   p99         = the 99th of 100 sorted = 10 ms. The stutter is the 100th, so p99 cannot see it.
    //   1% low      = the mean of the slowest ceil(100/100) = 1 frame = 100 ms. It is the stutter.
    // A report that called the first "1% low" would show a perfectly smooth frame time for a run
    // with a visible hitch in it.
    std::vector<double> samples(99, 10.0);
    samples.push_back(100.0);
    const Distribution d = describe(samples);
    REQUIRE(d.count == 100);
    CHECK_THAT(d.p99, WithinAbs(10.0, 1e-9));
    CHECK_THAT(d.low1Percent, WithinAbs(100.0, 1e-9));
    CHECK_THAT(d.max, WithinAbs(100.0, 1e-9));
    CHECK(d.low1Percent >= d.p99); // always, for any sample

    // Two stutters in two hundred frames: the 1% low is their *mean*, not the worst of them.
    std::vector<double> pair(198, 10.0);
    pair.push_back(50.0);
    pair.push_back(90.0);
    const Distribution two = describe(pair);
    REQUIRE(two.count == 200);
    // ceil(200/100) = 2 frames, so (50 + 90) / 2.
    CHECK_THAT(two.low1Percent, WithinAbs(70.0, 1e-9));
    // ceil(200/1000) = 1 frame, so the 0.1% low is the single worst -- which at this sample size
    // is just `max` under another name, and the header says so.
    CHECK_THAT(two.low01Percent, WithinAbs(90.0, 1e-9));
    CHECK_THAT(two.low01Percent, WithinAbs(two.max, 1e-9));
    // p99 of 200 is the 198th sorted frame: still 10 ms. Neither stutter is in it.
    CHECK_THAT(two.p99, WithinAbs(10.0, 1e-9));
}

TEST_CASE("a distribution reports the spread it was asked for, and nothing for an empty run",
          "[render-stats]") {
    // Four frames: 2, 4, 4, 6. Mean 4, population variance ((4+0+0+4)/4) = 2, stddev sqrt(2).
    const Distribution d = describe({4.0, 2.0, 6.0, 4.0});
    REQUIRE(d.count == 4);
    CHECK_THAT(d.mean, WithinAbs(4.0, 1e-9));
    CHECK_THAT(d.variance, WithinAbs(2.0, 1e-9));
    CHECK_THAT(d.stddev, WithinAbs(std::sqrt(2.0), 1e-9));
    CHECK_THAT(d.min, WithinAbs(2.0, 1e-9));
    CHECK_THAT(d.max, WithinAbs(6.0, 1e-9));
    // The measured window is the whole population, not a draw from one, so the divisor is n. With
    // n-1 the variance would read 2.667 and every reported spread would be inflated by ~15% at the
    // sample sizes this harness uses.
    CHECK(d.variance < 2.5);

    // A run whose GPU timestamps never landed has no distribution. `count == 0` says so; reporting
    // 0.00 ms would put an infinitely fast frame in the record.
    const Distribution none = describe({});
    CHECK(!none.valid());
    CHECK(none.count == 0);
}

TEST_CASE("an A/B pairs its blocks so a drifting machine cannot look like an improvement",
          "[render-stats]") {
    // A session that warms up: every block is 1 ms slower than the one before it, on both arms.
    // Unpaired, comparing the two A blocks against the two B blocks that ran after them would
    // credit the arm with the drift. Paired, each B is differenced against the A beside it and the
    // drift cancels, leaving the 2 ms the arm actually saved.
    const auto block = [](double wall, double gpu) {
        AbBlock b;
        b.wallMs = describe({wall, wall, wall});
        b.gpuMs = describe({gpu, gpu, gpu});
        return b;
    };
    const std::vector<AbBlock> baseline = {block(22.0, 18.0), block(23.0, 19.0)};
    const std::vector<AbBlock> arm = {block(20.0, 16.0), block(21.0, 17.0)};

    const AbSummary ab = compareArms("volume", baseline, arm);
    CHECK(ab.arm == "volume");
    CHECK(ab.blocks == 2);
    CHECK_THAT(ab.gpu.deltaMs, WithinAbs(2.0, 1e-9));
    CHECK_THAT(ab.wall.deltaMs, WithinAbs(2.0, 1e-9));
    REQUIRE(ab.gpuBlockDeltaMs.size() == 2);
    // Both pairs agree, which is what makes the result trustworthy; a pair-to-pair disagreement is
    // visible here rather than averaged into the headline.
    CHECK_THAT(ab.gpuBlockDeltaMs[0], WithinAbs(2.0, 1e-9));
    CHECK_THAT(ab.gpuBlockDeltaMs[1], WithinAbs(2.0, 1e-9));
    // Positive means the arm was faster -- the direction an optimisation hopes for.
    CHECK(ab.gpu.deltaMs > 0.0);
    CHECK(ab.gpu.isResult());
}

TEST_CASE("a difference below the measured noise floor is not a result", "[render-stats]") {
    // The floors come from five consecutive Glowmere runs in one session: GPU spread 1.0%, wall
    // 3.0% (docs/renderer-upgrade/01-audit-and-baseline.md). Anything under 2% GPU or 4% wall
    // cannot be told apart from the machine.
    const auto block = [](double wall, double gpu) {
        AbBlock b;
        b.wallMs = describe({wall});
        b.gpuMs = describe({gpu});
        return b;
    };
    // 18.60 -> 18.40: 0.20 ms, 1.08% of the baseline. Real-looking, and below the floor.
    const AbSummary small = compareArms("ao", {block(22.0, 18.60), block(22.0, 18.60)},
                                        {block(21.9, 18.40), block(21.9, 18.40)});
    CHECK_THAT(small.gpu.deltaPercent, WithinAbs(1.0753, 1e-3));
    CHECK(!small.gpu.isResult());
    CHECK_THAT(small.gpu.noiseFloorPercent, WithinAbs(kGpuNoiseFloorPercent, 1e-9));
    // Wall: 0.1 of 22.0 is 0.45%, well under the 4% wall floor.
    CHECK(!small.wall.isResult());

    // A regression is rejected by the same floor, in the same way: the test is on the magnitude,
    // not the sign, or a change could be certified as "not a slowdown" more easily than as a win.
    const AbSummary slower = compareArms("ao", {block(22.0, 18.60), block(22.0, 18.60)},
                                         {block(22.1, 18.80), block(22.1, 18.80)});
    CHECK(slower.gpu.deltaMs < 0.0);
    CHECK(!slower.gpu.isResult());
}

TEST_CASE("a session noisier than the calibration raises its own noise floor", "[render-stats]") {
    // The constants were calibrated on a quiet machine. If this session's own baseline blocks
    // disagree by 6%, a 3% difference between the arms is inside the session's noise and must not
    // be certified -- whatever the constant says.
    const auto block = [](double gpu) {
        AbBlock b;
        b.wallMs = describe({gpu * 1.2});
        b.gpuMs = describe({gpu});
        return b;
    };
    const std::vector<AbBlock> baseline = {block(18.0), block(19.1)}; // ~6% apart
    const std::vector<AbBlock> arm = {block(17.5), block(18.5)};      // ~3% faster each

    const AbSummary ab = compareArms("shadowMask", baseline, arm);
    CHECK(ab.gpuSpreadPercent > 5.0);
    CHECK(ab.gpu.noiseFloorPercent > kGpuNoiseFloorPercent);
    CHECK_THAT(ab.gpu.noiseFloorPercent, WithinAbs(ab.gpuSpreadPercent, 1e-9));
    CHECK(ab.gpu.deltaMs > 0.0);
    CHECK(!ab.gpu.isResult()); // real-looking, and this session cannot tell it from drift
}

TEST_CASE("an A/B with no completed pair makes no claim", "[render-stats]") {
    AbBlock b;
    b.wallMs = describe({22.0});
    b.gpuMs = describe({18.0});
    // One arm never ran: no pair, no delta, and nothing that could be read as "no difference".
    const AbSummary none = compareArms("volume", {b}, {});
    CHECK(none.blocks == 0);
    CHECK(!none.gpu.isResult());
    CHECK_THAT(none.gpu.deltaMs, WithinAbs(0.0, 1e-9));
    // An odd number of blocks pairs what it can and drops the widow: an unpaired block carries the
    // drift that pairing exists to remove.
    const AbSummary odd = compareArms("volume", {b, b, b}, {b, b});
    CHECK(odd.blocks == 2);
}

TEST_CASE("the benchmark JSON carries the conditions that make a number comparable", "[render-stats]") {
    // The failure this guards against is the one the audit found: two Glowmere figures 28% apart
    // with nothing recorded that could explain the gap. A record that omits its conditions invites
    // exactly that comparison.
    BenchmarkRecord record;
    record.conditions.scene = "examples/world/glowmere-stylized.scene.json";
    record.conditions.arm = "baseline";
    record.conditions.width = 1280;
    record.conditions.height = 800;
    record.conditions.gitRevision = "abc123def456";
    record.conditions.sessionId = "session-1";
    record.conditions.warmupFrames = 12;
    record.conditions.measuredFrames = 108;
    record.wallMs = describe({22.2, 22.3, 22.1});
    record.gpuMs = describe({18.6, 18.5, 18.7});
    record.counters.draws = 141.0;
    record.counters.triangles = 430231.0;
    record.passMedianMs = {{"scene", 15.73}, {"volume", 0.66}};

    const std::string json = benchmarkJson({record}, nullptr);
    // Conditions.
    CHECK(json.find("glowmere-stylized") != std::string::npos);
    CHECK(json.find("abc123def456") != std::string::npos);
    CHECK(json.find("sessionId") != std::string::npos);
    CHECK(json.find("warmupFrames") != std::string::npos);
    // The statistics, under names that carry their own definitions.
    CHECK(json.find("low1Percent_meanOfSlowest1Pct") != std::string::npos);
    CHECK(json.find("\"p99\"") != std::string::npos);
    CHECK(json.find("varianceMs2") != std::string::npos);
    // Workload and pass split.
    CHECK(json.find("submittedTriangles") != std::string::npos);
    CHECK(json.find("\"scene\"") != std::string::npos);
    // No A/B was run, so nothing may look like one.
    CHECK(json.find("\"ab\"") == std::string::npos);
    // A block with no GPU timing reports null, never a zero-millisecond frame.
    BenchmarkRecord noGpu = record;
    noGpu.gpuMs = describe({});
    CHECK(benchmarkJson({noGpu}, nullptr).find("\"gpuMs\": null") != std::string::npos);
}
