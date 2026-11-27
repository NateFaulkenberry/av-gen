// ADR-2232: a pass whose GPU timestamp slot the driver never wrote reads 0 ms, and the live profile used to report that
// 0 as the pass's cost. On this Dawn/Metal path every compute pass's slot comes back unwritten (Stage A, the
// volumetrics spike: `clusters`, `vfx.inject`, `vfx.integrate` all "cost" 0). The record now says which slots were
// unwritten and in how many frames, in its limits, its JSON and the headless trace -- and a run with none says so too.

#include "app/live_profile.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace avgen::app;

TEST_CASE("live profile: unwritten timestamp slots are reported as unwritten, not as free passes",
          "[live-profile][adr2232]") {
    std::vector<LiveProfileFrame> frames(10);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        LiveProfileFrame& f = frames[i];
        f.frameMs = 20.0;
        f.gpuMs = 12.0;
        f.cpuWorkMs = 6.0;
        f.passes = {{"clusters", 0.0}, {"scene", 9.0}, {"vfx.inject", 0.0}};
        f.unwritten = {"clusters"};
        if (i < 4) {
            f.unwritten.push_back("vfx.inject");
        }
    }
    LiveProfileRecord r;
    r.conditions.budgetMs = 1000.0 / 30.0;
    buildLiveProfile(r, frames, false);
    REQUIRE(r.unwrittenSlots.size() == 2);
    CHECK(r.unwrittenSlots[0].label == "clusters");
    CHECK(r.unwrittenSlots[0].frames == 10);
    CHECK(r.unwrittenSlots[1].label == "vfx.inject");
    CHECK(r.unwrittenSlots[1].frames == 4);
    bool said = false;
    for (const std::string& l : r.limits) {
        said = said || (l.find("unwritten GPU timestamp slots") != std::string::npos &&
                        l.find("clusters (10/10 frames)") != std::string::npos &&
                        l.find("vfx.inject (4/10 frames)") != std::string::npos);
    }
    CHECK(said);
    const nlohmann::json j = liveProfileJson(r);
    REQUIRE(j.contains("unwrittenSlots"));
    CHECK(j["unwrittenSlots"].size() == 2);
    CHECK(j["unwrittenSlots"][1]["unwrittenFrames"] == 4);
    // The trace row counts this frame's unwritten slots in its last column.
    const std::string row = headlessTraceRow('m', 1.0, frames[0], HeadlessTraceCounts{}, 0, false);
    CHECK(row.ends_with(",2\n"));

    // The control: a run where every slot was written reports an empty list and no such limit.
    for (auto& f : frames) {
        f.unwritten.clear();
    }
    LiveProfileRecord clean;
    clean.conditions.budgetMs = 1000.0 / 30.0;
    buildLiveProfile(clean, frames, false);
    CHECK(clean.unwrittenSlots.empty());
    for (const std::string& l : clean.limits) {
        CHECK(l.find("unwritten GPU timestamp") == std::string::npos);
    }
    const nlohmann::json cj = liveProfileJson(clean);
    REQUIRE(cj.contains("unwrittenSlots"));
    CHECK(cj["unwrittenSlots"].empty());
}
