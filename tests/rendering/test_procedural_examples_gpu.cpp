// Frame-hash regression for the showcase projects (ADR-023 / ADR-025..029, definition of done).
// Every world is rendered twice with fresh engines and renderers and must be bit-identical, and
// a later start time must differ (the worlds move on their own). The second case walks the
// flagship world across its arc (frames at 0, 30, 60 and 120 in its own timeline) so a change in
// any subsystem it touches (fields, effectors, splines, hierarchy, SDF, states, culling) shows up
// as a hash change rather than silently altering the piece.

#include "app/engine.hpp"
#include "app/render_job.hpp"
#include "app/render_settings.hpp"
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {
std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

std::uint64_t renderSequenceHash(gpu::Context& ctx, gpu::ShaderLibrary& shaders, const fs::path& project,
                                 const fs::path& out, double start, int frames) {
    auto engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
    REQUIRE(engine->loadProject(project).has_value());
    app::RenderSettings s;
    s.width = 192;
    s.height = 108;
    s.fps = 30.0;
    s.startSeconds = start;
    s.endSeconds = start + frames / 30.0;
    s.outputPath = out;
    s.encoderThreads = 1;
    app::RenderJob job(ctx, shaders, std::move(engine), s, project.parent_path());
    REQUIRE(job.run().has_value());
    REQUIRE(job.progress().framesRendered == static_cast<std::uint64_t>(frames));
    return job.progress().sequenceHash;
}
} // namespace

TEST_CASE("Showcase projects render bit-identically across fresh engines and renderers", "[gpu][procedural][examples]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    const fs::path examples = fs::path(AVGEN_SOURCE_DIR) / "examples";
    const auto tmp = fs::temp_directory_path() / "avgen_proc_examples";
    fs::remove_all(tmp);
    struct Case {
        const char* project;
        double start;
    };
    for (const Case c : {Case{"temple/temple.json", 1.0}, Case{"helix/helix.json", 2.0}, Case{"chamber/chamber.json", 0.5},
                         Case{"hyperspace/hyperspace.json", 3.0}, Case{"lab/lab.json", 0.5},
                         Case{"machine/machine.json", 1.5}, Case{"infinite/infinite.json", 2.0},
                         Case{"cathedral/cathedral.json", 1.0}, Case{"worlds/worlds.json", 4.0},
                         Case{"stress/stress.json", 0.5}}) {
        const auto project = examples / c.project;
        if (!fs::exists(project)) {
            continue;
        }
        INFO(project.string());
        const auto a = renderSequenceHash(*ctx, shaders, project, tmp / "a", c.start, 4);
        const auto b = renderSequenceHash(*ctx, shaders, project, tmp / "b", c.start, 4);
        CHECK(a == b);
        // A different start time must give different frames (the worlds move on their own).
        const auto later = renderSequenceHash(*ctx, shaders, project, tmp / "c", c.start + 2.0, 4);
        CHECK(later != a);
        CHECK(ctx->errorCount() == 0);
    }
    fs::remove_all(tmp);
#endif
}

TEST_CASE("The flagship world is reproducible at fixed points along its arc", "[gpu][procedural][examples]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    const fs::path project = fs::path(AVGEN_SOURCE_DIR) / "examples" / "infinite" / "infinite.json";
    if (!fs::exists(project)) {
        SKIP("the flagship example is not present");
    }
    const auto tmp = fs::temp_directory_path() / "avgen_arc_hashes";
    fs::remove_all(tmp);
    std::vector<std::uint64_t> hashes;
    // Frames 0, 30, 60 and 120 of the arc: Dormant, into Awakening, Expansion and Ascension.
    for (const double start : {0.0, 1.0, 2.0, 4.0}) {
        const auto a = renderSequenceHash(*ctx, shaders, project, tmp / "a", start, 2);
        const auto b = renderSequenceHash(*ctx, shaders, project, tmp / "b", start, 2);
        INFO("start " << start);
        CHECK(a == b); // reproducible
        hashes.push_back(a);
    }
    // The arc actually moves: no two sampled points render the same frames.
    for (std::size_t i = 0; i < hashes.size(); ++i) {
        for (std::size_t j = i + 1; j < hashes.size(); ++j) {
            INFO("arc points " << i << " and " << j);
            CHECK(hashes[i] != hashes[j]);
        }
    }
    CHECK(ctx->errorCount() == 0);
    fs::remove_all(tmp);
#endif
}
