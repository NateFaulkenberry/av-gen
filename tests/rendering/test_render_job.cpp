// Milestone 1.0: offline render jobs (ADR-020) — sequences on disk, exact frame count and size,
// bit-identical re-renders, stepping, cancellation, and video output when a backend exists.

#include "app/engine.hpp"
#include "app/render_job.hpp"
#include "assets/image.hpp"
#include "assets/video_writer.hpp"
#include "audio/audio_file.hpp"
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        // AVGEN_TEST_LOG=debug prints per-frame hashes (determinism diagnosis).
        const char* level = std::getenv("AVGEN_TEST_LOG");
        log::init(level != nullptr && std::string(level) == "debug" ? log::Level::Debug : log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

// A project with a short bass tone, the orb scene and a timeline track, in a temp folder.
struct ProjectFixture {
    fs::path dir;
    fs::path project;
    ProjectFixture() {
        dir = fs::temp_directory_path() / "avgen_render_job";
        fs::remove_all(dir);
        fs::create_directories(dir);
        constexpr std::uint32_t rate = 48000;
        auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(testsupport::sine(60.0f, rate, rate * 2, 0.8f), 2), 2, rate);
        REQUIRE(file.writeWav(dir / "tone.wav").has_value());
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadAudio(dir / "tone.wav").has_value());
        params::Track track;
        track.target = "orb/scale";
        track.addKey({.time = 0.0, .value = {0.5f}});
        track.addKey({.time = 2.0, .value = {2.0f}});
        engine.timeline().addTrack(track);
        project = dir / "show.json";
        REQUIRE(engine.saveProject(project).has_value());
    }
    ~ProjectFixture() {
        if (std::getenv("AVGEN_TEST_KEEP") == nullptr) {
            fs::remove_all(dir);
        }
    }
};

std::unique_ptr<app::Engine> loadOffline(const fs::path& project) {
    auto engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
    REQUIRE(engine->loadProject(project).has_value());
    return engine;
}

app::RenderSettings smallSettings(const fs::path& out) {
    app::RenderSettings s;
    s.width = 96;
    s.height = 64;
    s.fps = 10.0;
    s.startSeconds = 0.5;
    s.endSeconds = 1.5;
    s.outputPath = out;
    s.encoderThreads = 2;
    return s;
}

} // namespace

TEST_CASE("Render job writes an exact PNG sequence and is bit-identical on re-render", "[gpu][render]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    ProjectFixture f;
    const auto out = f.dir / "frames";

    app::RenderProgress p;
    {
        app::RenderJob job(*ctx, shaders, loadOffline(f.project), smallSettings(out), f.dir);
        REQUIRE(job.start().has_value());
        CHECK(job.resolvedEndSeconds() == 1.5);
        CHECK(job.progress().framesTotal == 10);
        REQUIRE(job.run().has_value());
        p = job.progress();
    }
    CHECK(p.finished);
    CHECK(p.framesRendered == 10);
    CHECK(p.framesWritten == 10);
    CHECK(p.error.empty());
    for (int i = 0; i < 10; ++i) {
        const auto file = out / fmt::format("frame_{:06d}.png", i);
        INFO(file.string());
        REQUIRE(fs::exists(file));
        auto image = assets::loadImage(file, true);
        REQUIRE(image.has_value());
        CHECK(image->width == 96);
        CHECK(image->height == 64);
    }
    CHECK_FALSE(fs::exists(out / "frame_000010.png"));
    // The orb grows along the timeline track: the last frame differs from the first.
    std::ifstream a(out / "frame_000000.png", std::ios::binary), b(out / "frame_000009.png", std::ios::binary);
    std::string sa((std::istreambuf_iterator<char>(a)), {}), sb((std::istreambuf_iterator<char>(b)), {});
    CHECK(sa != sb);

    // Same project, fresh engine and renderer: identical sequence hash.
    app::RenderJob again(*ctx, shaders, loadOffline(f.project), smallSettings(f.dir / "frames2"), f.dir);
    REQUIRE(again.run().has_value());
    CHECK(again.progress().sequenceHash == p.sequenceHash);
    CHECK(again.progress().lastFrameHash == p.lastFrameHash);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Render job steps in bounded chunks and can be cancelled", "[gpu][render]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    ProjectFixture f;
    auto settings = smallSettings(f.dir / "chunks");
    settings.endSeconds = 3.0; // 25 frames
    app::RenderJob job(*ctx, shaders, loadOffline(f.project), settings, f.dir);
    REQUIRE(job.start().has_value());
    CHECK_FALSE(job.step(3));
    CHECK(job.progress().framesRendered == 3);
    CHECK_FALSE(job.step(4));
    CHECK(job.progress().framesRendered == 7);
    job.cancel();
    CHECK(job.step(100));
    const auto p = job.progress();
    CHECK(p.cancelled);
    CHECK(p.finished);
    CHECK(p.framesRendered == 7);
    CHECK(p.framesWritten == 7);
    CHECK(fs::exists(f.dir / "chunks" / "frame_000006.png"));
    CHECK_FALSE(fs::exists(f.dir / "chunks" / "frame_000007.png"));
}

TEST_CASE("Render job rejects bad settings before touching the GPU", "[gpu][render]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    ProjectFixture f;
    auto settings = smallSettings(f.dir / "bad.mov");
    settings.output = app::RenderOutput::Video;
    settings.width = 97; // odd
    app::RenderJob job(*ctx, shaders, loadOffline(f.project), settings, f.dir);
    CHECK_FALSE(job.start().has_value());
    auto live = std::make_unique<app::Engine>(app::EngineMode::Live);
    app::RenderJob wrongMode(*ctx, shaders, std::move(live), smallSettings(f.dir / "x"), f.dir);
    CHECK_FALSE(wrongMode.start().has_value());
}

TEST_CASE("Render job writes a video with audio when a backend is available", "[gpu][render][video]") {
    if (!assets::hasNativeVideo() && assets::findFfmpeg().empty()) {
        SKIP("no video backend on this machine");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    ProjectFixture f;
    auto settings = smallSettings(f.dir / "out.mov");
    settings.output = app::RenderOutput::Video;
    settings.codec = assets::hasNativeVideo() ? "h264" : "libx264";
    settings.backend = assets::hasNativeVideo() ? "native" : "ffmpeg";
    settings.muxAudio = true;
    app::RenderJob job(*ctx, shaders, loadOffline(f.project), settings, f.dir);
    REQUIRE(job.run().has_value());
    CHECK(job.progress().framesWritten == 10);
    REQUIRE(fs::exists(f.dir / "out.mov"));
    CHECK(fs::file_size(f.dir / "out.mov") > 1024);
}
