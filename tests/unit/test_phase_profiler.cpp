// The frame-phase profiler's arithmetic, and in particular its groups.
//
// The groups are the instrument ADR-233 needed and are worth pinning for one reason: every
// before/after number in that pass rests on them. `--ui-ab` interleaves two conditions as blocks of
// one process and reports them side by side, because docs/application-performance.md §3 rule 1
// forbids comparing a frame time from one run against another and this machine's load average makes
// that rule expensive rather than pedantic. If a group quietly included the frames it was supposed
// to exclude -- the settling frames after a switch, say -- the comparison would be wrong in a way
// no reader could see, and the conclusion drawn from it would be wrong with it.

#include "core/phase_profiler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>

using avgen::core::PhaseProfiler;

namespace {

// One frame: a value in `work`, filed under `group`.
void frame(PhaseProfiler& prof, int work, double ms, int group) {
    prof.beginFrame();
    prof.setFrameGroup(group);
    prof.add(work, ms);
    prof.endFrame(ms);
}

} // namespace

TEST_CASE("Phase groups separate interleaved arms", "[core][performance]") {
    PhaseProfiler prof;
    const int work = prof.phase("work");
    prof.nameGroup(0, "A");
    prof.nameGroup(1, "B");

    // A, B, A, B -- interleaved, which is the whole point: the arms share whatever the machine was
    // doing, so the difference between the columns is the difference between the arms.
    for (int block = 0; block < 2; ++block) {
        for (int i = 0; i < 5; ++i) {
            frame(prof, work, 10.0, 0);
        }
        for (int i = 0; i < 5; ++i) {
            frame(prof, work, 20.0, 1);
        }
    }

    SECTION("each group sees only its own frames") {
        CHECK(prof.retained(0) == 10);
        CHECK(prof.retained(1) == 10);
        CHECK(prof.summary(work, 0).median == 10.0);
        CHECK(prof.summary(work, 1).median == 20.0);
        CHECK(prof.frameSummary(0).median == 10.0);
        CHECK(prof.frameSummary(1).median == 20.0);
    }

    SECTION("the ungrouped overloads still see everything") {
        CHECK(prof.retained() == 20);
        CHECK(prof.frames() == 20);
        // Ten 10s and ten 20s: the median of the whole population is one of the two, never a
        // number that no frame had.
        const double median = prof.frameSummary().median;
        CHECK((median == 10.0 || median == 20.0));
        CHECK(prof.frameSummary().mean == 15.0);
    }

    SECTION("spikes are counted per group") {
        CHECK(prof.spikes(16.7, 0) == 0);
        CHECK(prof.spikes(16.7, 1) == 10);
        CHECK(prof.spikes(16.7) == 10);
    }

    SECTION("names travel with the group") {
        CHECK(prof.groupName(0) == "A");
        CHECK(prof.groupName(1) == "B");
        CHECK(prof.groupName(4) == "(unnamed)");
    }

    SECTION("the comparison names both columns and both counts") {
        const std::array<int, 2> groups{0, 1};
        const std::string table = prof.compare("arms", groups);
        CHECK(table.find('A') != std::string::npos);
        CHECK(table.find('B') != std::string::npos);
        CHECK(table.find("10.000") != std::string::npos);
        CHECK(table.find("20.000") != std::string::npos);
    }
}

// The settling frames are the reason `kNoGroup` exists. An arm switched to on frame N inherits its
// predecessor's deferral timers, its warm pipelines and, for a pointer arm, whatever the pointer was
// doing -- so the first frames of a block belong to neither arm. They must be dropped, not folded
// into the arm that follows them: a block of 90 frames carrying 12 of somebody else's is a 13%
// error in one direction, which is larger than most of the differences being looked for.
TEST_CASE("Unlabelled frames belong to no arm", "[core][performance]") {
    PhaseProfiler prof;
    const int work = prof.phase("work");

    for (int i = 0; i < 4; ++i) {
        frame(prof, work, 100.0, PhaseProfiler::kNoGroup); // settling: expensive, and nobody's
    }
    for (int i = 0; i < 6; ++i) {
        frame(prof, work, 8.0, 0);
    }

    CHECK(prof.retained(0) == 6);
    CHECK(prof.summary(work, 0).max == 8.0);
    CHECK(prof.frameSummary(0).max == 8.0);
    CHECK(prof.spikes(50.0, 0) == 0);
    // ...and they are still in the run: dropping them from the arm is not the same as pretending
    // they did not happen, and `retained()` without a group is the honest total.
    CHECK(prof.retained() == 10);
    CHECK(prof.spikes(50.0) == 4);
}

TEST_CASE("A group with no frames reports nothing rather than zero", "[core][performance]") {
    // A summary of an empty population is not "0.000 ms", which would read as "this arm was free".
    // `samples == 0` is what says the column has nothing behind it.
    PhaseProfiler prof;
    const int work = prof.phase("work");
    frame(prof, work, 5.0, 0);

    const PhaseProfiler::Summary empty = prof.summary(work, 3);
    CHECK(empty.samples == 0);
    CHECK(prof.retained(3) == 0);
    CHECK(prof.frameSummary(3).samples == 0);
}

TEST_CASE("The csv carries the group so an interleaved run can be split later", "[core][performance]") {
    PhaseProfiler prof;
    const int work = prof.phase("work");
    frame(prof, work, 1.0, 0);
    frame(prof, work, 2.0, 1);
    const std::string csv = prof.csv();
    CHECK(csv.starts_with("group,frame_ms,work\n"));
    CHECK(csv.find("\n0,1.0000") != std::string::npos);
    CHECK(csv.find("\n1,2.0000") != std::string::npos);
}
