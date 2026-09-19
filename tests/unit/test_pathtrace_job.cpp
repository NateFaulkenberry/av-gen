// TraceJob (ADR-351, spec sections 36, 37): the thing that makes the path tracer reachable.
//
// Everything below this worked and was tested before this existed, and none of it could be run by a
// person. A subsystem with no caller is not done, and no test says so -- the tests check that the
// thing works, not that anybody can reach it. These are the tests for reaching it.

#include "pathtrace/trace_job.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

std::filesystem::path projectPath() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "world" /
           "glowmere-valley-2-multicam.json";
}

pathtrace::TraceJobRequest smallRequest(const std::filesystem::path& out) {
    pathtrace::TraceJobRequest r;
    r.project = projectPath();
    r.seconds = 12.0;
    r.output = out;
    r.settings.width = 64;
    r.settings.height = 40;
    r.settings.samplesPerPixel = 2;
    r.settings.maxDepth = 1;
    r.settings.samplesPerBatch = 1;
    return r;
}

} // namespace

TEST_CASE("TraceJobRequest refuses what it cannot render", "[unit][pathtrace][job]") {
    pathtrace::TraceJobRequest missingProject;
    missingProject.output = "/tmp/x.exr";
    REQUIRE_FALSE(missingProject.validate().has_value());

    pathtrace::TraceJobRequest noOutput;
    noOutput.project = projectPath();
    if (std::filesystem::exists(noOutput.project)) {
        REQUIRE_FALSE(noOutput.validate().has_value());

        pathtrace::TraceJobRequest badTime = smallRequest("/tmp/x.exr");
        badTime.seconds = -1.0;
        REQUIRE_FALSE(badTime.validate().has_value());

        pathtrace::TraceJobRequest badSize = smallRequest("/tmp/x.exr");
        badSize.settings.width = 0;
        REQUIRE_FALSE(badSize.validate().has_value());

        // CONTROL: the well-formed request these were derived from is accepted.
        REQUIRE(smallRequest("/tmp/x.exr").validate().has_value());
    }
}

TEST_CASE("the state names cover every state and only terminal ones are terminal",
          "[unit][pathtrace][job]") {
    using S = pathtrace::TraceJobState;
    const std::vector<S> all = {S::Queued,    S::BuildingScene, S::BuildingAcceleration,
                                S::Rendering, S::Denoising,     S::Writing,
                                S::Complete,  S::Cancelled,     S::Failed};
    for (S s : all) {
        REQUIRE_FALSE(pathtrace::traceJobStateName(s).empty());
        REQUIRE(pathtrace::traceJobStateName(s) != "unknown");
    }
    REQUIRE(pathtrace::traceJobStateIsTerminal(S::Complete));
    REQUIRE(pathtrace::traceJobStateIsTerminal(S::Cancelled));
    REQUIRE(pathtrace::traceJobStateIsTerminal(S::Failed));
    // And the working states must NOT be terminal, or a poller exits before the job starts.
    REQUIRE_FALSE(pathtrace::traceJobStateIsTerminal(S::Queued));
    REQUIRE_FALSE(pathtrace::traceJobStateIsTerminal(S::Rendering));
    REQUIRE_FALSE(pathtrace::traceJobStateIsTerminal(S::Writing));
}

TEST_CASE("a job renders a real project to an EXR, off the calling thread",
          "[unit][pathtrace][job]") {
    if (!std::filesystem::exists(projectPath())) {
        SUCCEED("project or assets absent from this worktree");
        return;
    }
    const auto out = std::filesystem::temp_directory_path() / "avgen_tracejob.exr";
    std::filesystem::remove(out);

    pathtrace::TraceJob job(smallRequest(out));
    REQUIRE(job.state() == pathtrace::TraceJobState::Queued);
    REQUIRE_FALSE(job.done());

    // `start` must return immediately -- that is the whole point of not blocking the UI thread.
    const auto t0 = std::chrono::steady_clock::now();
    job.start();
    const double startCost =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    INFO("start() took " << startCost << " s");
    REQUIRE(startCost < 1.0);

    // Poll, and record which states we actually observed.
    std::vector<pathtrace::TraceJobState> seen;
    while (!job.done()) {
        const auto p = job.progress();
        if (seen.empty() || seen.back() != p.state) seen.push_back(p.state);
        REQUIRE(p.fraction >= 0.0f);
        REQUIRE(p.fraction <= 1.0f);
        REQUIRE_FALSE(p.stage.empty());
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    job.wait();

    const auto done = job.progress();
    INFO("final state " << done.stage << " error '" << done.error << "'");
    REQUIRE(done.state == pathtrace::TraceJobState::Complete);
    REQUIRE(done.fraction == Approx(1.0f));
    REQUIRE(done.fractionKnown);
    REQUIRE(done.error.empty());
    REQUIRE(done.samplesDone == done.samplesTotal);

    // It really went through the stages rather than jumping from Queued to Complete.
    REQUIRE(seen.size() >= 2);

    // And there is a real file with real pixels behind it.
    REQUIRE(std::filesystem::exists(out));
    REQUIRE(std::filesystem::file_size(out) > 1000);
    REQUIRE_FALSE(job.framebuffer().isBlack());
    REQUIRE(job.framebuffer().width == 64);
    REQUIRE(job.stats().primaryRays > 0);
    // The capability report survives on the job, so a caller can show it without re-snapshotting.
    REQUIRE_FALSE(job.capabilities().entries.empty());
    std::filesystem::remove(out);
}

TEST_CASE("cancelling a job stops it and reports Cancelled, without writing a file",
          "[unit][pathtrace][job]") {
    if (!std::filesystem::exists(projectPath())) {
        SUCCEED("project or assets absent");
        return;
    }
    const auto out = std::filesystem::temp_directory_path() / "avgen_tracejob_cancel.exr";
    std::filesystem::remove(out);

    auto request = smallRequest(out);
    request.settings.samplesPerPixel = 512;   // long enough that cancellation lands mid-render
    request.settings.samplesPerBatch = 1;

    pathtrace::TraceJob job(std::move(request));
    job.start();
    job.cancel();     // immediately: it must be safe at any point, including before a stage begins
    job.wait();

    const auto p = job.progress();
    INFO("state after cancel: " << p.stage);
    REQUIRE(p.state == pathtrace::TraceJobState::Cancelled);
    REQUIRE(job.done());
    // Section 37: cancellation is checked before writing, so a cancelled job leaves no half-file
    // that a downstream tool would happily pick up.
    REQUIRE_FALSE(std::filesystem::exists(out));
}

TEST_CASE("cancelling a job that already finished is harmless", "[unit][pathtrace][job]") {
    if (!std::filesystem::exists(projectPath())) {
        SUCCEED("project or assets absent");
        return;
    }
    const auto out = std::filesystem::temp_directory_path() / "avgen_tracejob_late.exr";
    std::filesystem::remove(out);
    pathtrace::TraceJob job(smallRequest(out));
    REQUIRE(job.run().has_value());
    REQUIRE(job.state() == pathtrace::TraceJobState::Complete);
    job.cancel();   // must not change a terminal state or crash
    REQUIRE(job.state() == pathtrace::TraceJobState::Complete);
    std::filesystem::remove(out);
}

TEST_CASE("progress does not claim to know what it cannot", "[unit][pathtrace][job]") {
    // Spec section 36 asks for real progress. A stage with no intermediate signal must say
    // `fractionKnown == false` rather than interpolate a bar that creeps while nothing is happening.
    if (!std::filesystem::exists(projectPath())) {
        SUCCEED("project or assets absent");
        return;
    }
    const auto out = std::filesystem::temp_directory_path() / "avgen_tracejob_progress.exr";
    std::filesystem::remove(out);
    auto request = smallRequest(out);
    request.settings.samplesPerPixel = 64;
    request.settings.samplesPerBatch = 1;

    pathtrace::TraceJob job(std::move(request));
    job.start();

    bool sawUnknown = false;     // a build stage
    bool sawKnownRender = false; // rendering, which counts samples
    std::uint32_t maxSamples = 0;
    while (!job.done()) {
        const auto p = job.progress();
        if ((p.state == pathtrace::TraceJobState::BuildingScene ||
             p.state == pathtrace::TraceJobState::BuildingAcceleration) &&
            !p.fractionKnown) {
            sawUnknown = true;
        }
        if (p.state == pathtrace::TraceJobState::Rendering) {
            REQUIRE(p.fractionKnown);   // a count of finished samples IS a measurement
            sawKnownRender = true;
            maxSamples = std::max(maxSamples, p.samplesDone);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    job.wait();
    REQUIRE(job.state() == pathtrace::TraceJobState::Complete);
    REQUIRE(sawUnknown);
    REQUIRE(sawKnownRender);
    // Samples really advanced during the render rather than jumping 0 -> all at the end.
    REQUIRE(maxSamples > 0);
    std::filesystem::remove(out);
}
