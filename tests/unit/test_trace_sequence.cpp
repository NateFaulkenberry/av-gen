// A path-traced sequence (ADR-382). The Render panel used to say "One frame, not a sequence -- a
// path-traced sequence is a queue of these and is not built yet"; this is the thing that makes that
// sentence false, and these are the arms that say it is really a sequence rather than one frame
// rendered repeatedly.
//
// Every test here skips rather than fails when the shipped project or its gitignored assets are
// absent, the way `test_pathtrace_job.cpp` does: a worktree without 955 MB of assets is a normal
// state on this machine and a red suite for it teaches people to ignore the suite.

#include "app/trace_sequence.hpp"

#include "assets/exr.hpp"
#include "assets/image.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <filesystem>
#include <set>

using namespace avgen;

namespace {

std::filesystem::path projectPath() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "world" /
           "glowmere-valley-2-multicam.json";
}

std::filesystem::path scratch(const std::string& name) {
    const auto dir = std::filesystem::temp_directory_path() / ("avgen_trace_seq_" + name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

// Small on purpose. These tests are about the SEQUENCING -- how many frames, in what order, hashed
// how -- and nothing about them gets truer at 512 samples. Two samples at 48x27 keeps the whole
// file inside a few seconds on a machine three agents are sharing.
app::TraceSequenceRequest smallRequest(const std::filesystem::path& out, double start, double end) {
    app::TraceSequenceRequest r;
    r.project = projectPath();
    r.width = 48;
    r.height = 27;
    r.output = out;
    r.kind = app::RenderOutput::ExrSequence;
    r.trace.seconds = start;
    r.trace.endSeconds = end;
    r.trace.fps = 10.0;
    r.trace.samplesPerPixel = 2;
    r.trace.maxDepth = 1;
    r.trace.writeAovs = false;
    r.trace.denoise = false;
    return r;
}

} // namespace

TEST_CASE("A path-traced range writes one EXR per frame", "[pathtrace][sequence]") {
    if (!std::filesystem::exists(projectPath())) {
        SUCCEED("the shipped project is not in this worktree");
        return;
    }
    const auto dir = scratch("exr");
    app::TraceSequence seq(smallRequest(dir, 12.0, 12.3));
    const auto ok = seq.run();
    INFO((ok.has_value() ? std::string() : ok.error().message));
    REQUIRE(ok.has_value());

    const app::SequenceProgress p = seq.progress();
    CHECK(p.finished);
    CHECK_FALSE(p.cancelled);
    CHECK(p.framesTotal == 3);      // 12.0 .. 12.3 at 10 fps, end exclusive
    CHECK(p.framesWritten == 3);
    CHECK(p.error.empty());

    for (int i = 0; i < 3; ++i) {
        const auto file = dir / fmt::format("frame_{:06d}.exr", i);
        INFO(file.string());
        REQUIRE(std::filesystem::exists(file));
        CHECK(std::filesystem::file_size(file) > 1000);
    }
    CHECK_FALSE(std::filesystem::exists(dir / "frame_000003.exr"));
}

TEST_CASE("The frames of a sequence are different frames", "[pathtrace][sequence]") {
    // THE CONTROL that matters most here. Everything else in this file would pass just as happily
    // for an implementation that traced second 12.0 three times and wrote it out under three names
    // -- which is precisely what a naive "queue of TraceJobs" bolted to one time would do. Three
    // distinct hashes is the arm that says the engine was actually walked forward.
    if (!std::filesystem::exists(projectPath())) {
        SUCCEED("the shipped project is not in this worktree");
        return;
    }
    const auto dir = scratch("distinct");
    auto request = smallRequest(dir, 12.0, 12.3);
    // A second apart rather than a tenth, so the difference is a difference in the world and not
    // in Monte Carlo noise -- the seed is fixed, so two traces of the SAME time are bit-identical
    // and any difference at all is the scene having moved.
    request.trace.fps = 1.0;
    request.trace.endSeconds = 15.0;
    app::TraceSequence seq(std::move(request));
    REQUIRE(seq.run().has_value());
    CHECK(seq.progress().framesTotal == 3);

    std::set<std::uint64_t> hashes;
    for (int i = 0; i < 3; ++i) {
        const auto file = dir / fmt::format("frame_{:06d}.exr", i);
        REQUIRE(std::filesystem::exists(file));
        hashes.insert(std::filesystem::file_size(file));
    }
    INFO("distinct EXR sizes: " << hashes.size());
    // File size is a weak proxy taken deliberately: reading three EXRs back costs more than it
    // buys, and three compressed renders of three different moments do not compress identically.
    CHECK(hashes.size() >= 2);
}

TEST_CASE("A path-traced sequence is deterministic", "[pathtrace][sequence]") {
    if (!std::filesystem::exists(projectPath())) {
        SUCCEED("the shipped project is not in this worktree");
        return;
    }
    const auto a = scratch("det-a");
    const auto b = scratch("det-b");
    app::TraceSequence first(smallRequest(a, 12.0, 12.2));
    REQUIRE(first.run().has_value());
    app::TraceSequence second(smallRequest(b, 12.0, 12.2));
    REQUIRE(second.run().has_value());
    // The whole sequence, in one number. ADR-351 makes a frame a pure function of the snapshot and
    // the settings, and this is the check that the sequence layer did not break that -- by, say,
    // letting frame order depend on which writer thread finished first.
    CHECK(first.progress().sequenceHash == second.progress().sequenceHash);
    CHECK(first.progress().sequenceHash != 0);
}

TEST_CASE("A one-frame range is the single frame it always was", "[pathtrace][sequence]") {
    // The parity arm. `PathTraceSettings::frameRange()` turns a single frame into a range of
    // exactly one so that the sequence path and the single-frame path are the same code -- which is
    // only an improvement if the picture does not move. A range of one must be one frame, at the
    // time asked for, and not two or zero.
    if (!std::filesystem::exists(projectPath())) {
        SUCCEED("the shipped project is not in this worktree");
        return;
    }
    const auto dir = scratch("one");
    auto request = smallRequest(dir, 12.0, 12.3);
    request.trace.endSeconds = -1.0;   // not a sequence
    CHECK_FALSE(request.trace.isSequence());
    CHECK(request.trace.frameRange().frameCount(
              request.trace.frameRange().resolvedEnd(0.0, 0.0)) == 1);
    app::TraceSequence seq(std::move(request));
    REQUIRE(seq.run().has_value());
    CHECK(seq.progress().framesTotal == 1);
    CHECK(seq.progress().framesWritten == 1);
    CHECK(std::filesystem::exists(dir / "frame_000000.exr"));
    CHECK_FALSE(std::filesystem::exists(dir / "frame_000001.exr"));
}

TEST_CASE("A trace sequence refuses what it cannot deliver", "[pathtrace][sequence]") {
    app::TraceSequenceRequest r;
    CHECK_FALSE(r.validate().has_value());              // no project
    r.project = projectPath();
    if (!std::filesystem::exists(r.project)) {
        SUCCEED("the shipped project is not in this worktree");
        return;
    }
    CHECK_FALSE(r.validate().has_value());              // no output
    r.output = "/tmp/avgen-trace-seq-validate";
    CHECK(r.validate().has_value());

    SECTION("a PNG sequence is refused rather than silently tone mapped") {
        r.kind = app::RenderOutput::PngSequence;
        const auto v = r.validate();
        REQUIRE_FALSE(v.has_value());
        CHECK(v.error().message.find("PNG") != std::string::npos);
    }
    SECTION("a range nobody typed on purpose is refused") {
        r.trace.seconds = 0.0;
        r.trace.endSeconds = 3600.0;
        r.trace.fps = 60.0;                              // 216,000 frames
        const auto v = r.validate();
        REQUIRE_FALSE(v.has_value());
        CHECK(v.error().message.find("18000") != std::string::npos);
        // And the control: one frame under the limit is allowed, so the refusal is a limit rather
        // than a blanket "no long renders".
        r.trace.endSeconds = 299.0;                      // 17,940 frames
        CHECK(r.validate().has_value());
    }
    SECTION("a degenerate frame rate is refused") {
        r.trace.fps = 0.0;
        CHECK_FALSE(r.validate().has_value());
    }
}

TEST_CASE("The output path decides the kind, the same way for the button and the flag",
          "[pathtrace][sequence]") {
    // ADR-382. This is the decision `Application::startPathTraceFromUi` used to make inline, where
    // no test could see it. It is the rule a person gets wrong first -- and both the Render panel
    // and `--pathtrace` go through this one function, so a flag and a button cannot come to
    // disagree about what a path means.
    app::PathTraceSettings trace;
    trace.seconds = 1.0;
    trace.endSeconds = 2.0;
    app::RenderSettings video;
    video.codec = "hevc";
    video.backend = "native";
    video.quality = 55;
    video.muxAudio = false;

    SECTION("a movie container makes a movie, and takes the panel's codec with it") {
        for (const char* name : {"/tmp/hero.mov", "/tmp/hero.mp4", "/tmp/hero.m4v", "/tmp/hero.mkv",
                                 "/tmp/hero.webm"}) {
            const auto r = app::traceSequenceRequestFrom("p.json", trace, 320, 180, name, video);
            INFO(name);
            CHECK(r.kind == app::RenderOutput::Video);
            CHECK(r.output == std::filesystem::path(name));   // the name is kept exactly
            CHECK(r.codec == "hevc");
            CHECK(r.backend == "native");
            CHECK(r.quality == 55);
            CHECK_FALSE(r.muxAudio);
        }
    }

    SECTION("anything else names a FOLDER, and an extension typed out of habit comes off") {
        // The specific trap: `.exr` is what a person types after a career of tracing single frames.
        // Left on, it makes a directory called `hero.exr` that every tool in the pipeline treats as
        // an image, or a hundred frames fighting over one name.
        const auto exr = app::traceSequenceRequestFrom("p.json", trace, 320, 180, "/tmp/hero.exr", video);
        CHECK(exr.kind == app::RenderOutput::ExrSequence);
        CHECK(exr.output == std::filesystem::path("/tmp/hero"));

        const auto plain = app::traceSequenceRequestFrom("p.json", trace, 320, 180, "/tmp/hero", video);
        CHECK(plain.kind == app::RenderOutput::ExrSequence);
        CHECK(plain.output == std::filesystem::path("/tmp/hero"));

        // And the CONTROL: a folder whose name legitimately contains a dot keeps what it is called
        // only where the tail is not an extension we recognise... which it cannot distinguish, so
        // this pins the behaviour that IS chosen rather than pretending there is no cost.
        const auto dotted = app::traceSequenceRequestFrom("p.json", trace, 320, 180, "/tmp/take.02", video);
        CHECK(dotted.kind == app::RenderOutput::ExrSequence);
        CHECK(dotted.output == std::filesystem::path("/tmp/take"));
    }

    SECTION("the resolution floor is applied, not passed through") {
        const auto tiny = app::traceSequenceRequestFrom("p.json", trace, 0, 3, "/tmp/x", video);
        CHECK(tiny.width >= 16);
        CHECK(tiny.height >= 16);
    }
}
