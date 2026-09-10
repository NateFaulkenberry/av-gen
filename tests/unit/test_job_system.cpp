// The job system (ADR-064). The interesting tests are the promises: honest progress, prompt
// cancellation, a failing job that does not take the queue with it, and a destructor that does not
// hang on work that would have run for another ten minutes.

#include "app/job_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>

using namespace avgen;
using namespace std::chrono_literals;

TEST_CASE("A job runs, reports stages and completes", "[app][jobs]") {
    app::JobSystem jobs(2);
    std::atomic<int> ran{0};
    const auto id = jobs.submit({"test", "staged", [&ran](app::JobContext& ctx) -> Result<void> {
        ctx.setStages({"prepare", "work", "finish"});
        for (int stage = 0; stage < 3; ++stage) {
            ctx.beginStage(stage);
            for (int i = 0; i < 4; ++i) {
                ctx.setStageProgress(static_cast<float>(i + 1) / 4.0f);
                ctx.setOperation("step " + std::to_string(i + 1) + " / 4");
            }
        }
        ctx.log("done");
        ran = 1;
        return {};
    }});
    REQUIRE(jobs.waitFor(id, 5s));
    app::JobStatus s;
    REQUIRE(jobs.status(id, s));
    CHECK(s.state == app::JobState::Completed);
    CHECK(ran == 1);
    CHECK(s.progressKnown);
    CHECK(s.progress == 1.0f);
    CHECK(s.stageCount == 3);
    CHECK(s.elapsedSeconds >= 0.0);
    REQUIRE(!s.logs.empty());
    CHECK(s.logs.back() == "done");
}

TEST_CASE("Progress is unknown until a stage measures itself", "[app][jobs]") {
    // The rule this system exists to enforce: a fabricated bar is indistinguishable from a real
    // one, so a job that cannot measure itself must not produce a number.
    app::JobSystem jobs(1);
    std::atomic<bool> release{false};
    std::atomic<bool> inside{false};
    const auto id = jobs.submit({"test", "unmeasured", [&](app::JobContext& ctx) -> Result<void> {
        ctx.setStages({"thinking"});
        ctx.beginStage(0);
        ctx.setOperation("no idea how long this takes");
        inside = true;
        while (!release.load()) {
            std::this_thread::sleep_for(1ms);
        }
        return {};
    }});
    while (!inside.load()) {
        std::this_thread::sleep_for(1ms);
    }
    app::JobStatus s;
    REQUIRE(jobs.status(id, s));
    CHECK(s.state == app::JobState::Running);
    CHECK(!s.progressKnown);
    CHECK(s.estimatedRemainingSeconds < 0.0);   // negative means "do not format me"
    CHECK(s.stageName == "thinking");
    CHECK(s.currentOperation == "no idea how long this takes"); // separate from the stage name
    release = true;
    REQUIRE(jobs.waitFor(id, 5s));
}

TEST_CASE("Cancellation stops a running job promptly", "[app][jobs]") {
    app::JobSystem jobs(1);
    std::atomic<int> iterations{0};
    std::atomic<bool> started{false};
    const auto id = jobs.submit({"test", "long", [&](app::JobContext& ctx) -> Result<void> {
        ctx.setStages({"grinding"});
        started = true;
        for (int i = 0; i < 100000; ++i) {
            if (ctx.shouldCancel()) {
                return {};   // unwinds; the system marks it Cancelled regardless of what we return
            }
            ++iterations;
            std::this_thread::sleep_for(100us);
        }
        return {};
    }});
    while (!started.load()) {
        std::this_thread::sleep_for(1ms);
    }
    const auto before = iterations.load();
    CHECK(jobs.cancel(id));
    REQUIRE(jobs.waitFor(id, 2s));
    app::JobStatus s;
    REQUIRE(jobs.status(id, s));
    CHECK(s.state == app::JobState::Cancelled);
    // It stopped rather than running to completion.
    CHECK(iterations.load() < 100000);
    CHECK(iterations.load() >= before);
}

TEST_CASE("A queued job is cancelled without ever starting", "[app][jobs]") {
    app::JobSystem jobs(1);
    std::atomic<bool> release{false};
    std::atomic<bool> secondRan{false};
    const auto blocker = jobs.submit({"test", "blocker", [&](app::JobContext&) -> Result<void> {
        while (!release.load()) {
            std::this_thread::sleep_for(1ms);
        }
        return {};
    }});
    const auto queued = jobs.submit({"test", "queued", [&](app::JobContext&) -> Result<void> {
        secondRan = true;
        return {};
    }});
    std::this_thread::sleep_for(20ms);
    CHECK(jobs.cancel(queued));
    app::JobStatus s;
    REQUIRE(jobs.status(queued, s));
    CHECK(s.state == app::JobState::Cancelled);
    release = true;
    REQUIRE(jobs.waitFor(blocker, 5s));
    jobs.waitAll(2s);
    CHECK(!secondRan.load());   // it never ran at all
}

TEST_CASE("A failing job records its error and the queue keeps draining", "[app][jobs]") {
    app::JobSystem jobs(1);
    std::atomic<bool> laterRan{false};
    const auto bad = jobs.submit({"test", "bad", [](app::JobContext&) -> Result<void> {
        return fail("model unavailable");
    }});
    const auto good = jobs.submit({"test", "good", [&](app::JobContext&) -> Result<void> {
        laterRan = true;
        return {};
    }});
    REQUIRE(jobs.waitFor(bad, 5s));
    REQUIRE(jobs.waitFor(good, 5s));
    app::JobStatus s;
    REQUIRE(jobs.status(bad, s));
    CHECK(s.state == app::JobState::Failed);
    CHECK(s.error == "model unavailable");
    // An optional job's failure is not the caller's failure, and it is certainly not the queue's.
    CHECK(laterRan.load());
}

TEST_CASE("A job that throws becomes an ordinary failure, not a lost worker", "[app][jobs]") {
    app::JobSystem jobs(1);
    std::atomic<bool> laterRan{false};
    const auto bad = jobs.submit({"test", "thrower", [](app::JobContext&) -> Result<void> {
        throw std::runtime_error("boom");
    }});
    const auto good = jobs.submit({"test", "after", [&](app::JobContext&) -> Result<void> {
        laterRan = true;
        return {};
    }});
    REQUIRE(jobs.waitFor(bad, 5s));
    REQUIRE(jobs.waitFor(good, 5s));
    app::JobStatus s;
    REQUIRE(jobs.status(bad, s));
    CHECK(s.state == app::JobState::Failed);
    CHECK(s.error.find("boom") != std::string::npos);
    CHECK(laterRan.load());   // the worker survived and drained the queue
}

TEST_CASE("Pause holds a job and resume releases it", "[app][jobs]") {
    app::JobSystem jobs(1);
    std::atomic<int> counter{0};
    std::atomic<bool> stop{false};
    const auto id = jobs.submit({"test", "pausable", [&](app::JobContext& ctx) -> Result<void> {
        ctx.setStages({"counting"});
        while (!stop.load()) {
            if (!ctx.waitWhilePaused()) {
                return {};
            }
            if (ctx.shouldCancel()) {
                return {};
            }
            ++counter;
            std::this_thread::sleep_for(1ms);
        }
        return {};
    }});
    std::this_thread::sleep_for(30ms);
    CHECK(jobs.pause(id));
    std::this_thread::sleep_for(20ms);
    const int held = counter.load();
    std::this_thread::sleep_for(40ms);
    CHECK(counter.load() - held <= 2);   // held, give or take the frame it was already in
    CHECK(jobs.resume(id));
    std::this_thread::sleep_for(30ms);
    CHECK(counter.load() > held);
    stop = true;
    static_cast<void>(jobs.cancel(id));
    REQUIRE(jobs.waitFor(id, 2s));
}

TEST_CASE("Destroying the system does not hang on work still running", "[app][jobs]") {
    // The failure this guards is a Cancel button that leaves the application unresponsive: if the
    // destructor waits for a ten-minute job, quitting takes ten minutes.
    const auto start = std::chrono::steady_clock::now();
    {
        app::JobSystem jobs(2);
        for (int i = 0; i < 4; ++i) {
            static_cast<void>(jobs.submit({"test", "endless", [](app::JobContext& ctx) -> Result<void> {
                while (!ctx.shouldCancel()) {
                    std::this_thread::sleep_for(1ms);
                }
                return {};
            }}));
        }
        std::this_thread::sleep_for(20ms);
    }
    CHECK(std::chrono::steady_clock::now() - start < 3s);
}

TEST_CASE("Finished jobs are cleared only when asked", "[app][jobs]") {
    app::JobSystem jobs(1);
    const auto id = jobs.submit({"test", "quick", [](app::JobContext&) -> Result<void> { return {}; }});
    REQUIRE(jobs.waitFor(id, 5s));
    CHECK(jobs.statuses().size() == 1);   // the UI keeps it until somebody dismisses it
    jobs.clearFinished();
    CHECK(jobs.statuses().empty());
}
