// Frame-hash regression for the procedural showcase projects (ADR-023 / definition of done):
// radial (Temple), spiral + noise (Helix), combined deformation and nested scenes (Chamber), and
// an audio-reactive scene, each rendered twice with fresh engines and renderers.

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
                         Case{"hyperspace/hyperspace.json", 3.0}, Case{"lab/lab.json", 0.5}}) {
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
