// ADR-1090..1093: the live profiler's logic -- no device, no window, no clock.

#include "app/live_profile.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace avgen;
using namespace avgen::app;
using Catch::Approx;

TEST_CASE("live profile: the command line takes its own flags and leaves the rest", "[live-profile]") {
    const auto a = parseLiveProfileArgs({"avgen", "--live-profile", "--project", "p.json", "--mode", "live",
                                         "--target-fps", "90", "--size", "1280x720", "--start", "60", "--json",
                                         "o.json", "--tier", "realtime", "--verify-candidates", "3", "--no-audio"});
    REQUIRE(a.error.empty());
    CHECK(a.options.enabled);
    CHECK(a.options.mode == LiveProfileMode::Live);
    CHECK(a.options.targetFps == Approx(90.0));
    CHECK(a.options.outputWidth == 1280);
    CHECK(a.options.outputHeight == 720);
    CHECK(a.options.startSeconds == Approx(60.0));
    CHECK(a.options.verifyCandidates == 3);
    CHECK_FALSE(a.options.audio);
    REQUIRE(a.options.json);
    CHECK(a.options.json->string() == "o.json");
    CHECK(a.rest == std::vector<std::string>{"avgen", "--project", "p.json", "--tier", "realtime"});
}

TEST_CASE("live profile: fast and deep defaults, and bad values are refused", "[live-profile]") {
    const auto fast = parseLiveProfileArgs({"avgen", "--live-profile"});
    CHECK(fast.options.warmupSeconds == Approx(3.0));
    CHECK(fast.options.measureSeconds == Approx(5.0));
    const auto deep = parseLiveProfileArgs({"avgen", "--live-profile", "--deep"});
    CHECK(deep.options.warmupSeconds == Approx(5.0));
    CHECK(deep.options.measureSeconds == Approx(20.0));
    const auto deepShort = parseLiveProfileArgs({"avgen", "--live-profile", "--deep", "--measure", "8"});
    CHECK(deepShort.options.measureSeconds == Approx(8.0));
    CHECK_FALSE(parseLiveProfileArgs({"avgen", "--live-profile", "--mode", "fast"}).error.empty());
    CHECK_FALSE(parseLiveProfileArgs({"avgen", "--live-profile", "--size", "big"}).error.empty());
    CHECK_FALSE(parseLiveProfileArgs({"avgen", "--live-profile", "--target-fps", "500"}).error.empty());
    CHECK_FALSE(parseLiveProfileArgs({"avgen", "--live-profile", "--measure"}).error.empty());
}

TEST_CASE("live profile: steady state needs a median that stopped moving", "[live-profile]") {
    SteadyStateDetector d(10, 0.05, 2, 20);
    // A frame time still falling (shader warm-up, a settling LOD): never steady.
    for (int i = 0; i < 100; ++i) {
        d.note(40.0 - 0.3 * i);
    }
    CHECK_FALSE(d.steady());
    SteadyStateDetector flat(10, 0.05, 2, 20);
    int steadyAt = -1;
    for (int i = 0; i < 100 && steadyAt < 0; ++i) {
        // Noisy but flat: spikes do not move a median.
        if (flat.note(i % 7 == 0 ? 30.0 : 10.0)) {
            steadyAt = i;
        }
    }
    CHECK(steadyAt >= 19);
    CHECK(steadyAt < 40);
    CHECK(flat.lastMedian() == Approx(10.0));
}

TEST_CASE("live profile: budget statistics and deadline misses in vsync terms", "[live-profile]") {
    // 60 target on a 120 Hz display: 16.67 ms is two vsyncs and meets it; 25 ms is three and misses one.
    const std::vector<double> frames{16.6, 16.8, 8.4, 25.0, 33.3, 16.6};
    const BudgetStats b = budgetStats(frames, 1000.0 / 60.0, 1000.0 / 120.0);
    CHECK(b.frames == 6);
    CHECK(b.overBudget == 3); // 16.8, 25.0, 33.3
    CHECK(b.deadlineMisses == 2); // 16.8 rounds to two vsyncs: not a miss
    CHECK(b.vsyncsMissed == 3);   // 25.0: one, 33.3: two
    CHECK(b.percentUnder == Approx(50.0));
    const BudgetStats headless = budgetStats(frames, 1000.0 / 60.0, 0.0);
    CHECK(headless.deadlineMisses == 0);
    CHECK(headless.refreshMs == 0.0);
}

TEST_CASE("live profile: every timeline label lands in one GPU category, unknown ones in other", "[live-profile]") {
    CHECK(gpuCategoryOf("scene") == "geometry/opaque");
    CHECK(gpuCategoryOf("shadow") == "shadows");
    CHECK(gpuCategoryOf("shadowmask") == "shadows");
    CHECK(gpuCategoryOf("clusters") == "lighting");
    CHECK(gpuCategoryOf("sdf") == "SDF");
    CHECK(gpuCategoryOf("volume.march") == "volumetrics");
    CHECK(gpuCategoryOf("volume.something-new") == "volumetrics");
    CHECK(gpuCategoryOf("post/dof") == "post: depth of field");
    CHECK(gpuCategoryOf("post/motionblur") == "post: motion blur");
    CHECK(gpuCategoryOf("post/bloom") == "post: bloom");
    CHECK(gpuCategoryOf("post/fxaa") == "post: other");
    CHECK(gpuCategoryOf("distort.heat") == "post: other");
    CHECK(gpuCategoryOf("tonemap") == "composite/tonemap");
    CHECK(gpuCategoryOf("particles") == "particles/simulation");
    CHECK(gpuCategoryOf("temporal") == "temporal");
    CHECK(gpuCategoryOf("background") == "other");
    CHECK(gpuCategoryOf("a-label-nobody-has-seen") == "other");
    // Every category the table names is in the report's order, so none can be dropped by the grouping.
    for (const auto& rule : gpuCategoryTable()) {
        const auto& order = gpuCategoryOrder();
        CHECK(std::find(order.begin(), order.end(), rule.category) != order.end());
    }
    const auto groups = groupGpuCategories({{"scene", 5.0}, {"depth", 1.0}, {"post/dof", 2.0}, {"mystery", 0.5}});
    REQUIRE(groups.size() == 3);
    CHECK(groups[0].name == "geometry/opaque");
    CHECK(groups[0].medianMs == Approx(6.0));
    CHECK(groups.back().name == "other");
    double sum = 0.0;
    for (const auto& g : groups) sum += g.medianMs;
    CHECK(sum == Approx(8.5)); // nothing lost, nothing counted twice
}

TEST_CASE("live profile: the critical path names what the frame waits on", "[live-profile]") {
    CHECK(criticalPath(16.7, 15.9, 3.0, 0.5, 16.67, true).verdict == "GPU");
    CHECK(criticalPath(16.7, 5.0, 15.0, 0.5, 16.67, true).verdict == "CPU");
    CHECK(criticalPath(25.0, 10.0, 6.0, 9.0, 16.67, true).verdict == "sync/present");
    CHECK(criticalPath(0.0, 0.0, 0.0, 0.0, 16.67, true).verdict == "unknown");
    CHECK(criticalPath(20.0, 12.0, 5.0, 8.0, 16.67, false).verdict == "GPU");
}

TEST_CASE("live profile: candidates are estimates with a basis, sorted, and name a lever", "[live-profile]") {
    CandidateInputs in;
    in.gpu = groupGpuCategories({{"scene", 12.0}, {"volume.march", 8.0}, {"post/motionblur", 4.3}, {"shadow", 1.2},
                                 {"post/dof", 1.3}});
    in.passes = {{"scene", 12.0}, {"volume.march", 8.0}, {"post/motionblur", 4.3}, {"shadow", 1.2}, {"post/dof", 1.3}};
    in.resources.shadowCasters = 40;
    in.resources.shadowResolution = 2048;
    in.gpuMs = 26.8;
    const auto c = optimizationCandidates(in);
    REQUIRE(c.size() >= 4);
    for (const auto& k : c) {
        CHECK_FALSE(k.verified); // nothing is measured until --verify-candidates runs it
        CHECK(k.estimatedLowMs <= k.estimatedHighMs);
        CHECK(k.estimatedHighMs <= k.costMs + 1e-9);
        CHECK_FALSE(k.estimateBasis.empty());
        CHECK_FALSE(k.lever.empty());
        CHECK((k.risk == "low" || k.risk == "medium" || k.risk == "high"));
    }
    for (std::size_t i = 1; i < c.size(); ++i) {
        CHECK(c[i - 1].estimatedLowMs + c[i - 1].estimatedHighMs >= c[i].estimatedLowMs + c[i].estimatedHighMs);
    }
    // Already at quarter resolution: the fog candidate becomes the steps one.
    in.volumeResolutionScale = 0.25f;
    const auto c2 = optimizationCandidates(in);
    CHECK(std::none_of(c2.begin(), c2.end(), [](const auto& k) { return k.id == "volume-resolution"; }));
    CHECK(std::any_of(c2.begin(), c2.end(), [](const auto& k) { return k.id == "volume-steps"; }));
    // Motion blur already off: no candidate offers to turn it off again.
    in.motionBlur = false;
    const auto c3 = optimizationCandidates(in);
    CHECK(std::none_of(c3.begin(), c3.end(), [](const auto& k) { return k.id.starts_with("motion-blur"); }));
}

TEST_CASE("live profile: material programs are named as an estimated cost", "[live-profile]") {
    CandidateInputs in;
    in.gpu = groupGpuCategories({{"scene", 6.0}});
    in.resources.materialPrograms = 2;
    in.resources.materialProgramOps = 20;
    const auto c = optimizationCandidates(in);
    const auto it = std::find_if(c.begin(), c.end(), [](const auto& k) { return k.id == "material-programs"; });
    REQUIRE(it != c.end());
    // 10 ops on average: 3 + 0.9 = 3.9 ms at full-frame coverage, 0.39 at 10%.
    CHECK(it->estimatedLowMs == Approx(0.39));
    CHECK(it->estimatedHighMs == Approx(3.9));
    CHECK(it->lever == "noprograms");
}

TEST_CASE("live profile: the record builds, and its JSON keeps estimated and measured apart", "[live-profile]") {
    std::vector<LiveProfileFrame> frames;
    for (int i = 0; i < 120; ++i) {
        LiveProfileFrame f;
        f.atSeconds = i / 60.0;
        f.frameMs = i == 50 ? 40.0 : 16.6;
        f.gpuMs = 12.0;
        f.cpuWorkMs = 4.0;
        f.waitMs = 10.0;
        f.updControlMs = 0.1;
        f.passes = {{"scene", 8.0}, {"volume.march", 3.0}, {"tonemap", 0.2}};
        frames.push_back(f);
    }
    LiveProfileRecord r;
    r.conditions.mode = "live";
    r.conditions.budgetMs = 1000.0 / 60.0;
    r.conditions.displayRefreshHz = 120.0;
    buildLiveProfile(r, frames, true);
    CHECK(r.frameMs.count == 120);
    CHECK(r.budget.deadlineMisses == 1);
    REQUIRE_FALSE(r.worst.empty());
    CHECK(r.worst.front().index == 50);
    CHECK(r.critical.verdict == "GPU");
    CHECK(r.status == "TARGET ACHIEVED");
    REQUIRE(r.gpu.size() == 3);
    CHECK(std::any_of(r.cpu.begin(), r.cpu.end(), [](const auto& c) { return c.basis == "not separately measurable"; }));
    LiveProfileCandidate k;
    k.id = "x";
    k.estimatedLowMs = 1.0;
    k.estimatedHighMs = 2.0;
    k.verified = true;
    k.measuredSavingMs = 0.7;
    r.candidates.push_back(k);
    const nlohmann::json j = liveProfileJson(r);
    CHECK(j["schema"] == "avgen.liveprofile/1");
    CHECK(j.contains("definitions"));
    CHECK(j["candidates"][0]["estimatedSavingMs"]["high"] == 2.0);
    CHECK(j["candidates"][0]["measuredSaving"]["ms"] == 0.7);
    CHECK(j["conditions"]["hardwareSpecific"] == true);
    const std::string text = liveProfileText(r);
    CHECK(text.find("ESTIMATED") != std::string::npos);
    CHECK(text.find("MEASURED") != std::string::npos);
    CHECK(text.find("specific to this machine") != std::string::npos);
}

TEST_CASE("live profile: headless budgets the pipelined frame, not the serial wall clock", "[live-profile]") {
    std::vector<LiveProfileFrame> frames(60);
    for (auto& f : frames) {
        f.frameMs = 20.0; // serial: CPU + queue wait
        f.gpuMs = 12.0;
        f.cpuWorkMs = 6.0;
    }
    LiveProfileRecord r;
    r.conditions.budgetMs = 1000.0 / 60.0;
    buildLiveProfile(r, frames, false);
    CHECK(r.budget.overBudget == 0);
    CHECK(r.status == "TARGET ACHIEVED");
    CHECK(r.critical.verdict == "GPU");
    CHECK(r.budget.refreshMs == 0.0);
}
