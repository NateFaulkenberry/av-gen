// Milestone 1.0: offline render jobs (ADR-020) — sequences on disk, exact frame count and size,
// bit-identical re-renders, stepping, cancellation, and video output when a backend exists.

#include "app/engine.hpp"
#include "app/render_job.hpp"
#include "assets/exr.hpp"
#include "assets/image.hpp"
#include "assets/video_writer.hpp"
#include "audio/audio_file.hpp"
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <chrono>
#include <tuple>
#include <memory>
#include <set>

using namespace avgen;
using Catch::Matchers::ContainsSubstring;
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
        // A hot emissive so the orb exceeds 1.0 in linear light (the EXR test looks for it).
        params::Track glow;
        glow.target = "orb/emissive";
        glow.addKey({.time = 0.0, .value = {6.0f}});
        engine.timeline().addTrack(glow);
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

app::RenderSettings smallSettingsExr(const fs::path& out) {
    auto s = smallSettings(out);
    s.output = app::RenderOutput::ExrSequence;
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

TEST_CASE("Render job readback ring matches the synchronous path frame for frame", "[gpu][render]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    ProjectFixture f;
    auto settings = smallSettings(f.dir / "ring");
    settings.endSeconds = 2.5; // 20 frames at 10 fps
    std::vector<std::uint64_t> ringHashes;
    {
        app::RenderJob job(*ctx, shaders, loadOffline(f.project), settings, f.dir);
        REQUIRE(job.run().has_value());
        ringHashes = job.frameHashes();
        CHECK(job.progress().framesReadBack == 20);
    }
    REQUIRE(ringHashes.size() == 20);

    // The classic path: a fresh engine and renderer, one renderToImage (submit + wait) per frame.
    // The comparison renderer has to be at the job's tier. Until ADR-147 a job never called
    // setQuality at all, so it ran at the renderer's default and this test passed by comparing two
    // Realtime frames -- which is precisely the §5.9 violation Phase G measured: the deliverable
    // was byte-identical to an interactive frame and differed from Offline. What this test is for
    // is the readback ring matching the synchronous path, not the tier, so it names the tier.
    auto engine = loadOffline(f.project);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    rendering::QualityTier jobTier = rendering::QualityTier::Offline;
    REQUIRE(rendering::qualityTierFromName(settings.tier, jobTier));
    renderer.setQuality(jobTier);
    REQUIRE(renderer.resize(settings.width, settings.height).has_value());
    FixedStepClock clock(settings.fps);
    clock.restartAt(settings.startSeconds);
    engine->seekSeconds(settings.startSeconds);
    for (std::size_t i = 0; i < ringHashes.size(); ++i) {
        const FrameTime time = engine->tick(clock);
        engine->update(time);
        const rendering::ShaderFrameInputs inputs{&engine->shaderLayers(),
                                                  engine->hasFrame() ? &engine->latestFrame() : nullptr};
        auto image = renderer.renderToImage(engine->scene(), time, settings.width, settings.height, &inputs);
        REQUIRE(image.has_value());
        INFO("frame " << i);
        CHECK(gpu::hashImage(*image) == ringHashes[i]);
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Render job writes a scene-linear EXR sequence deterministically", "[gpu][render][exr]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    ProjectFixture f;
    auto settings = smallSettings(f.dir / "exr");
    settings.output = app::RenderOutput::ExrSequence;
    app::RenderProgress p;
    {
        app::RenderJob job(*ctx, shaders, loadOffline(f.project), settings, f.dir);
        REQUIRE(job.start().has_value());
        CHECK(job.settings().pattern == "frame_{:06d}.exr");
        REQUIRE(job.run().has_value());
        p = job.progress();
    }
    CHECK(p.framesRendered == 10);
    CHECK(p.framesWritten == 10);
    CHECK(p.error.empty());
    float brightest = 0.0f;
    for (int i = 0; i < 10; ++i) {
        const auto file = f.dir / "exr" / fmt::format("frame_{:06d}.exr", i);
        INFO(file.string());
        REQUIRE(fs::exists(file));
        auto image = assets::readExr(file);
        REQUIRE(image.has_value());
        CHECK(image->width == 96);
        CHECK(image->height == 64);
        for (const float v : assets::floatPixels(*image)) {
            brightest = std::max(brightest, v);
        }
    }
    CHECK_FALSE(fs::exists(f.dir / "exr" / "frame_000010.exr"));
    CHECK_FALSE(fs::exists(f.dir / "exr" / "frame_000000.png"));
    // Scene-linear light before tone mapping: the emissive orb is brighter than anything an 8-bit
    // PNG can hold.
    CHECK(brightest > 1.0f);

    // The sync float readback agrees with the ring on frame 0.
    {
        auto engine = loadOffline(f.project);
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        // ADR-147: at the job's tier, for the reason given on the ring test above.
        rendering::QualityTier jobTier = rendering::QualityTier::Offline;
        REQUIRE(rendering::qualityTierFromName(settings.tier, jobTier));
        renderer.setQuality(jobTier);
        FixedStepClock clock(settings.fps);
        clock.restartAt(settings.startSeconds);
        engine->seekSeconds(settings.startSeconds);
        const FrameTime time = engine->tick(clock);
        engine->update(time);
        const rendering::ShaderFrameInputs inputs{&engine->shaderLayers(),
                                                  engine->hasFrame() ? &engine->latestFrame() : nullptr};
        auto image = renderer.renderToImageFloat(engine->scene(), time, settings.width, settings.height, &inputs);
        REQUIRE(image.has_value());
        CHECK(image->rgba.size() == 96u * 64u * 4u);
        auto first = assets::readExr(f.dir / "exr" / "frame_000000.exr");
        REQUIRE(first.has_value());
        const auto px = assets::floatPixels(*first);
        REQUIRE(px.size() == image->rgba.size());
        std::size_t mismatches = 0;
        for (std::size_t i = 0; i < px.size(); ++i) {
            if (px[i] != image->rgba[i]) {
                ++mismatches;
            }
        }
        CHECK(mismatches == 0);
    }

    // A second EXR render: same hashes and byte-identical files.
    app::RenderJob again(*ctx, shaders, loadOffline(f.project), smallSettingsExr(f.dir / "exr2"), f.dir);
    REQUIRE(again.run().has_value());
    CHECK(again.progress().sequenceHash == p.sequenceHash);
    CHECK(again.progress().lastFrameHash == p.lastFrameHash);
    std::ifstream a(f.dir / "exr" / "frame_000005.exr", std::ios::binary), b(f.dir / "exr2" / "frame_000005.exr", std::ios::binary);
    std::string sa((std::istreambuf_iterator<char>(a)), {}), sb((std::istreambuf_iterator<char>(b)), {});
    CHECK(sa.size() > 0);
    CHECK(sa == sb);
    CHECK(ctx->errorCount() == 0);
}

// ADR-137 step 2/3: the scene renders below the output resolution and the tonemap upscales into it.
// No tier sets `renderScale` below 1 yet -- turning it on for a tier is an image decision with a
// §50 gate, separate from the mechanism existing -- so nothing else in the suite exercises this
// path. A mechanism no test drives is a mechanism nobody has run.
TEST_CASE("a scaled scene target still fills the output the caller asked for", "[gpu][render][scale]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    ProjectFixture f;
    auto engine = loadOffline(f.project);

    constexpr std::uint32_t kW = 320;
    constexpr std::uint32_t kH = 200;

    const auto renderAt = [&](float scale) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        rendering::QualitySettings q =
            rendering::QualitySettings::forTier(rendering::QualityTier::Realtime);
        q.renderScale = scale;
        renderer.setQualitySettings(q);
        REQUIRE(renderer.resize(kW, kH).has_value());
        FixedStepClock clock(30.0);
        clock.restartAt(0.0);
        engine->seekSeconds(0.0);
        const FrameTime time = engine->tick(clock);
        engine->update(time);
        const rendering::ShaderFrameInputs inputs{&engine->shaderLayers(),
                                                  engine->hasFrame() ? &engine->latestFrame() : nullptr};
        auto image = renderer.renderToImage(engine->scene(), time, kW, kH, &inputs);
        REQUIRE(image.has_value());
        return std::pair{std::move(*image), renderer.stats()};
    };

    const auto [full, fullStats] = renderAt(1.0f);
    const auto [half, halfStats] = renderAt(0.5f);

    // The output is what the caller asked for, whatever the scene rendered at. If this were the
    // scene size instead, every consumer downstream would silently get a smaller picture.
    CHECK(full.width == kW);
    CHECK(full.height == kH);
    CHECK(half.width == kW);
    CHECK(half.height == kH);

    // The stats report the *scene* resolution, because that is what every per-pixel number in the
    // harness is a number about.
    CHECK(fullStats.width == kW);
    CHECK(halfStats.width == kW / 2);
    CHECK(halfStats.height == kH / 2);

    // The scaled frame is a real picture, not a blank or a garbage read: it differs from the full
    // one (or the scale did nothing) while still resembling it (or the upscale is broken). Checked
    // as mean absolute difference over the luma channel, in 0..255.
    REQUIRE(full.rgba.size() == half.rgba.size());
    double sum = 0.0;
    std::size_t lit = 0;
    for (std::size_t i = 0; i + 3 < full.rgba.size(); i += 4) {
        sum += std::abs(static_cast<double>(full.rgba[i]) - static_cast<double>(half.rgba[i]));
        if (full.rgba[i] > 8) {
            ++lit;
        }
    }
    const double meanDelta = sum / static_cast<double>(full.rgba.size() / 4);
    INFO("mean |full - half| = " << meanDelta << " over " << lit << " lit pixels");
    CHECK(lit > (kW * kH) / 20);  // the fixture actually drew something
    CHECK(meanDelta > 0.0);       // half resolution changed the image
    CHECK(meanDelta < 40.0);      // and it is still the same picture, not noise
    CHECK(ctx->errorCount() == 0);
}

// ADR-242: AOV export. The renderer has written these auxiliary targets every frame since ADR-035
// and nothing outside a debug view has ever read them; this is the consumer, so this test is what
// establishes that each file contains what its name says rather than a plausible-looking wrong
// buffer -- which is the exact failure this repository keeps writing ADRs about.
//
// Every assertion below is a property only the RIGHT target has:
//   normal    a unit vector, checked where there is geometry to have one -- the target is
//             OCT-ENCODED on the GPU, so this assertion is what proves the export decodes it
//             rather than shipping an encoded pair no compositor could read
//   depth     the orb is nearer than the background, and the background is the far plane
//   emission  exceeds 1.0, because the fixture drives the orb's emissive to 6
//   velocity  is not all zero, because the fixture scales the orb over the rendered range
//   id        has more than one value, and agrees with depth about where the orb is
//
// The last one is the point: `id` and `depth` are separate targets read back separately, so their
// agreement about which pixels are the orb is a cross-check neither could fake alone.
TEST_CASE("Render job exports auxiliary passes that contain what they claim", "[gpu][render][aov]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    ProjectFixture f;
    auto settings = smallSettings(f.dir / "aov");
    settings.aovs = "normal,emission,depth,velocity,id";
    app::RenderProgress p;
    {
        app::RenderJob job(*ctx, shaders, loadOffline(f.project), settings, f.dir);
        REQUIRE(job.start().has_value());
        REQUIRE(job.run().has_value());
        p = job.progress();
    }
    CHECK(p.error.empty());
    CHECK(p.framesRendered == 10);
    // Ten beauty frames plus five passes each. A count that only reached ten would mean the AOVs
    // were enqueued and silently dropped.
    CHECK(p.framesWritten == 10 + 10 * 5);

    const auto read = [&](const char* aov, int frame) {
        const auto file = f.dir / "aov" / fmt::format("frame_{:06d}.{}.exr", frame, aov);
        INFO(file.string());
        REQUIRE(fs::exists(file));
        auto image = assets::readExr(file);
        REQUIRE(image.has_value());
        CHECK(image->width == 96);
        CHECK(image->height == 64);
        return std::move(*image);
    };

    const auto normal = read("normal", 5);
    const auto depth = read("depth", 5);
    const auto emission = read("emission", 5);
    const auto velocity = read("velocity", 5);
    const auto ids = read("id", 5);
    const auto nPx = assets::floatPixels(normal);
    const auto dPx = assets::floatPixels(depth);
    const auto ePx = assets::floatPixels(emission);
    const auto vPx = assets::floatPixels(velocity);
    const auto iPx = assets::floatPixels(ids);
    REQUIRE(nPx.size() == 96u * 64u * 4u);

    // `id`: more than one value, or the pass is a constant and everything below is vacuous.
    std::set<int> distinct;
    for (std::size_t i = 0; i < iPx.size(); i += 4) {
        distinct.insert(static_cast<int>(iPx[i]));
    }
    INFO("distinct identifiers: " << distinct.size());
    CHECK(distinct.size() >= 2);

    // The orb is whichever identifier is not the most common one (the background).
    std::map<int, int> histogram;
    for (std::size_t i = 0; i < iPx.size(); i += 4) {
        ++histogram[static_cast<int>(iPx[i])];
    }
    const int background = std::max_element(histogram.begin(), histogram.end(),
                                            [](const auto& a, const auto& b) { return a.second < b.second; })
                               ->first;

    int geometry = 0;
    int unitNormals = 0;
    double orbDepth = 0.0;
    int orbDepthSamples = 0;
    double backgroundDepth = 0.0;
    int backgroundDepthSamples = 0;
    for (std::size_t px = 0; px < 96u * 64u; ++px) {
        const std::size_t i = px * 4;
        const bool isBackground = static_cast<int>(iPx[i]) == background;
        if (!isBackground) {
            ++geometry;
            const double len = std::sqrt(static_cast<double>(nPx[i]) * nPx[i] +
                                         static_cast<double>(nPx[i + 1]) * nPx[i + 1] +
                                         static_cast<double>(nPx[i + 2]) * nPx[i + 2]);
            // Roughness rides in alpha after the decode, and is a roughness.
            CHECK(nPx[i + 3] >= 0.0f);
            CHECK(nPx[i + 3] <= 1.0f);
            // Half precision and interpolation across a face both move this a little; a buffer
            // that is not a normal misses it by far more than a percent.
            if (std::abs(len - 1.0) < 0.05) {
                ++unitNormals;
            }
            orbDepth += dPx[i];
            ++orbDepthSamples;
        } else {
            backgroundDepth += dPx[i];
            ++backgroundDepthSamples;
        }
    }
    REQUIRE(geometry > 20); // the orb is on screen at all
    INFO("unit-length normals on " << unitNormals << " of " << geometry << " geometry pixels");
    CHECK(unitNormals > geometry * 9 / 10);

    // Depth: the orb is in front of the background, and by a lot. This is the cross-check -- `id`
    // decided which pixels are the orb and `depth`, read back from a different target, agrees.
    REQUIRE(orbDepthSamples > 0);
    REQUIRE(backgroundDepthSamples > 0);
    const double orbMean = orbDepth / orbDepthSamples;
    const double bgMean = backgroundDepth / backgroundDepthSamples;
    INFO("mean depth: orb " << orbMean << ", background " << bgMean);
    CHECK(orbMean > 0.0);
    CHECK(orbMean < bgMean);

    // Emission: the fixture drives the orb's emissive to 6, so the pass must exceed what a
    // display-referred buffer could hold. A tone-mapped frame written here by mistake could not.
    float brightestEmission = 0.0f;
    for (std::size_t i = 0; i < ePx.size(); i += 4) {
        brightestEmission = std::max({brightestEmission, ePx[i], ePx[i + 1], ePx[i + 2]});
    }
    INFO("brightest emission " << brightestEmission);
    CHECK(brightestEmission > 1.0f);

    // Velocity: the orb scales across the rendered range, so something moved. All-zero would mean
    // the target was read before anything wrote it.
    double motion = 0.0;
    for (std::size_t i = 0; i < vPx.size(); i += 4) {
        motion += std::abs(static_cast<double>(vPx[i])) + std::abs(static_cast<double>(vPx[i + 1]));
    }
    INFO("total screen-space motion " << motion);
    CHECK(motion > 0.0);
}

TEST_CASE("the shadow AOV is exported without changing the frame it describes",
          "[gpu][render][aov][adr255]") {
    // ADR-255. Unlike the other five, this target is not written every frame: at `high` and
    // `offline` the engine builds no shadow mask at all, so `--aov shadow` turns on a dedicated
    // full-resolution pass that would not otherwise run. Two things have to be true of it, and
    // the second is the one an AOV lives or dies by.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    ProjectFixture f;

    std::vector<std::uint64_t> withoutAov;
    {
        auto settings = smallSettings(f.dir / "noshadowaov");
        app::RenderJob job(*ctx, shaders, loadOffline(f.project), settings, f.dir);
        REQUIRE(job.start().has_value());
        REQUIRE(job.run().has_value());
        withoutAov = job.frameHashes();
    }
    REQUIRE_FALSE(withoutAov.empty());

    app::RenderProgress p;
    std::vector<std::uint64_t> withAov;
    {
        auto settings = smallSettings(f.dir / "shadowaov");
        settings.aovs = "shadow";
        app::RenderJob job(*ctx, shaders, loadOffline(f.project), settings, f.dir);
        REQUIRE(job.start().has_value());
        REQUIRE(job.run().has_value());
        withAov = job.frameHashes();
        p = job.progress();
    }
    CHECK(p.error.empty());
    CHECK(p.framesWritten == 10 + 10); // ten beauty frames and ten shadow planes

    // **The property that makes it an AOV of the deliverable rather than a different render.** The
    // export runs a pass the lit frame does not read, so every beauty frame must come back with
    // the hash it had without the flag. Anything else and the file describes a frame nobody shipped.
    CHECK(withAov == withoutAov);

    const auto file = f.dir / "shadowaov" / "frame_000005.shadow.exr";
    INFO(file.string());
    REQUIRE(fs::exists(file));
    auto image = assets::readExr(file);
    REQUIRE(image.has_value());
    CHECK(image->width == 96);
    CHECK(image->height == 64);

    // **The arm that separates "the pass ran" from "I read the placeholder".** When no mask is
    // built, `output()` is a 1x1 white texel and every channel of it is a constant -- including
    // alpha, which is 0. So the depth channel is the question to ask: a pass that ran writes the
    // view depth it computed each pixel at, and that varies across an orb against a background.
    //
    // ⚠ **The visibility channel on THIS scene is the constant 1.0, and that is correct.** The
    // fixture is one convex orb over nothing: a sphere has no receiver to cast onto and its own
    // far side is back-facing, which `shadowFactor` returns lit for before it looks anything up.
    // The first version of this test asserted the plane varied and failed, which is the same
    // lesson ADR-254 records from the scene side -- a scene that cannot show the artifact cannot
    // exercise the probe that looks for it. The non-constant arm lives where there is geometry to
    // cast: on Glowmere the exported plane marks 4.55% of the frame shadowed, and 28.9% when the
    // shadow passes are disabled, which is why that combination is now refused (ADR-255).
    float minVisibility = 2.0f;
    float maxVisibility = -1.0f;
    float minDepth = 1e9f;
    float maxDepth = 0.0f;
    const auto px = assets::floatPixels(*image);
    for (std::size_t i = 0; i < px.size(); i += 4) {
        minVisibility = std::min(minVisibility, px[i]);
        maxVisibility = std::max(maxVisibility, px[i]);
        minDepth = std::min(minDepth, px[i + 3]);
        maxDepth = std::max(maxDepth, px[i + 3]);
    }
    INFO("visibility " << minVisibility << ".." << maxVisibility << ", depth " << minDepth << ".."
                       << maxDepth);
    CHECK(maxVisibility > 0.5f); // something is lit
    CHECK(maxDepth > 0.0f);      // the alpha channel is a view depth, not the placeholder's zero
    CHECK(maxDepth > minDepth);  // and it varies, so this is a rendered plane and not a constant
}

TEST_CASE("an AOV export is refused where it cannot be resolved", "[render][aov]") {
    app::RenderSettings s;
    s.outputPath = "out";
    s.aovs = "normal";
    CHECK(s.validate().has_value());
    // ADR-242. The auxiliary targets are sized to the scaled resolution, and three of the five have
    // no correct downsample: averaging two identifiers is a third object, averaging two normals is
    // not a normal, and averaging two depths across a silhouette is a surface that is not there.
    s.supersample = 2.0f;
    auto refused = s.validate();
    REQUIRE_FALSE(refused.has_value());
    CHECK_THAT(refused.error().message, ContainsSubstring("supersample"));
    // And the combination is only refused when an AOV is actually asked for.
    s.aovs.clear();
    CHECK(s.validate().has_value());
}

// ADR-251. A supersampled EXR was the top-left QUARTER of the frame, at the right size and in the
// right format, so nothing about the file said it was wrong.
//
// The assertion that catches it is a *positional* one. "The supersampled frame differs from the
// plain one" passes against the bug -- a crop differs too. What only the correct image can do is
// agree with the plain render everywhere at once: a crop matches one corner and disagrees with the
// rest, so comparing the whole frame against the whole frame is what separates them.
TEST_CASE("a supersampled EXR is the whole frame, not a corner of it", "[gpu][render][exr][scale]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    ProjectFixture f;

    struct Frame {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<float> rgba;
        [[nodiscard]] const float* pixel(std::uint32_t x, std::uint32_t y) const {
            return rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
        }
    };
    const auto render = [&](const fs::path& dir, float supersample) {
        auto settings = smallSettings(dir);
        settings.output = app::RenderOutput::ExrSequence;
        settings.supersample = supersample;
        app::RenderJob job(*ctx, shaders, loadOffline(f.project), settings, f.dir);
        REQUIRE(job.start().has_value());
        REQUIRE(job.run().has_value());
        CHECK(job.progress().error.empty());
        auto image = assets::readExr(dir / "frame_000000.exr");
        REQUIRE(image.has_value());
        Frame out;
        out.width = image->width;
        out.height = image->height;
        const std::span<const float> pixels = assets::floatPixels(*image);
        out.rgba.assign(pixels.begin(), pixels.end());
        return out;
    };

    const Frame plain = render(f.dir / "exr-plain", 1.0f);
    const Frame scaled = render(f.dir / "exr-ss2", 2.0f);

    // Same file shape either way: supersampling is a sampling rate, not an output size.
    CHECK(scaled.width == plain.width);
    CHECK(scaled.height == plain.height);
    REQUIRE(scaled.rgba.size() == plain.rgba.size());

    // **Where the light is**, as a luminance-weighted centroid in normalised frame coordinates.
    //
    // This is the measure the defect actually calls for. The first version of this test compared
    // radiance channel by channel and failed at 58% -- correctly, because a supersampled render is
    // *supposed* to differ from a plain one at every antialiased edge, and an absolute threshold of
    // 0.05 on HDR values reaching 10.0 flags all of them. That measured resolving, not framing.
    //
    // A crop does something a resolve never does: it moves the content. Magnifying the top-left
    // quarter about the origin drags the centre of light toward the corner, and no amount of extra
    // sampling does that.
    const auto centroid = [](const Frame& image) {
        double sum = 0.0;
        double cx = 0.0;
        double cy = 0.0;
        for (std::uint32_t y = 0; y < image.height; ++y) {
            for (std::uint32_t x = 0; x < image.width; ++x) {
                const float* px = image.pixel(x, y);
                const double l = 0.2126 * px[0] + 0.7152 * px[1] + 0.0722 * px[2];
                sum += l;
                cx += l * (static_cast<double>(x) + 0.5);
                cy += l * (static_cast<double>(y) + 0.5);
            }
        }
        REQUIRE(sum > 0.0);
        return std::pair{cx / sum / image.width, cy / sum / image.height};
    };
    const auto [px, py] = centroid(plain);
    const auto [sx, sy] = centroid(scaled);

    // **The control arm, synthesised here rather than assumed.** A peak-brightness guard could not
    // do this job on this fixture -- the bright orb sits near the top-left, so the quarter's peak is
    // 9.81 against the whole frame's 10.08 and a crop would have passed on luck. So the crop the
    // defect produced is built from the good frame and measured with the same instrument. If it does
    // not move the centroid, the assertion below cannot detect the bug and the test is vacuous.
    Frame cropped;
    cropped.width = plain.width;
    cropped.height = plain.height;
    cropped.rgba.resize(plain.rgba.size());
    for (std::uint32_t y = 0; y < cropped.height; ++y) {
        for (std::uint32_t x = 0; x < cropped.width; ++x) {
            const float* src = plain.pixel(x / 2, y / 2);
            float* dst = cropped.rgba.data() + (static_cast<std::size_t>(y) * cropped.width + x) * 4;
            for (int c = 0; c < 4; ++c) {
                dst[c] = src[c];
            }
        }
    }
    const auto [qx, qy] = centroid(cropped);
    const double cropShift = std::hypot(qx - px, qy - py);
    INFO("a top-left crop moves the centre of light by " << cropShift << " of the frame");
    REQUIRE(cropShift > 0.05);   // the instrument can see a crop, so not seeing one means something

    const double shift = std::hypot(sx - px, sy - py);
    INFO("supersampled centre of light moved " << shift << ", against " << cropShift << " for a crop");
    CHECK(shift < cropShift / 4.0);

    CHECK(ctx->errorCount() == 0);
}

// ---- ADR-320: the frames the render is writing, shown while it writes them ----------------------
//
// The whole value of this feature is that the picture in the panel is the deliverable's own pixels
// rather than a second render of the same moment, so "the preview exists" is not a claim worth
// testing (ADR-182). What these check is the thing that would make the feature a lie: that the
// bytes handed to the UI are the bytes in the file, that the frame they came from is the frame
// they say they came from, and -- ADR-250 -- that the file is not one byte different for having
// been watched.

namespace {

// The preview's own downscale rule, restated here on purpose. If `capturePreview` changes which
// source pixel a preview pixel comes from, these tests have to disagree with it rather than follow
// it: a helper shared with the implementation would make every comparison below tautological.
std::uint32_t previewStep(std::uint32_t width, std::uint32_t height) {
    const std::uint32_t longest = std::max(width, height);
    return std::max<std::uint32_t>(1, (longest + app::RenderJob::kPreviewMaxDimension - 1) /
                                          app::RenderJob::kPreviewMaxDimension);
}

} // namespace

TEST_CASE("The previewed pixels are the pixels in the file", "[gpu][render][preview]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    ProjectFixture f;
    const auto out = f.dir / "watched";
    auto settings = smallSettings(out);

    app::RenderJob job(*ctx, shaders, loadOffline(f.project), settings, f.dir);
    job.setPreviewEnabled(true);
    REQUIRE(job.start().has_value());

    // Collected while the render runs, exactly as the editor collects them: one frame at a time,
    // whatever happens to be newest when the caller comes back. Some frames will be missed -- that
    // is the drop policy -- and every one that is caught has to be right.
    std::map<std::uint64_t, app::RenderJob::FramePreview> caught;
    app::RenderJob::FramePreview frame;
    while (!job.step(1)) {
        if (job.takePreview(frame)) {
            caught[frame.index] = frame;
        }
    }
    if (job.takePreview(frame)) {
        caught[frame.index] = frame;
    }
    const auto progress = job.progress();
    REQUIRE(progress.error.empty());
    REQUIRE(progress.framesWritten == 10);
    // A render of ten frames stepped one at a time must have produced previews; zero here would
    // mean the tap never fired and every check below would pass vacuously.
    REQUIRE(caught.size() >= 3);
    INFO("previews caught: " << caught.size() << " of 10");

    const auto& hashes = job.frameHashes();
    for (const auto& [index, preview] : caught) {
        INFO("frame " << index);
        // The frame says which frame of the sequence it is, and the hash it carries is the hash the
        // job recorded for that frame. A preview built from a re-render, from a stale buffer, or
        // from the frame before this one fails here.
        REQUIRE(index < hashes.size());
        CHECK(preview.hash == hashes[index]);
        CHECK(preview.sourceWidth == 96);
        CHECK(preview.sourceHeight == 64);
        CHECK_FALSE(preview.linearSource);
        CHECK(preview.step == previewStep(96, 64));
        CHECK(preview.width == 96 / preview.step);
        CHECK(preview.height == 64 / preview.step);

        auto file = assets::loadImage(settings.frameFile(out, index), false);
        REQUIRE(file.has_value());
        REQUIRE(file->width == 96);
        REQUIRE(file->height == 64);
        // Every preview pixel against the pixel of the PNG it was point-sampled from. Not a
        // tolerance and not a statistic: the RGBA8 path is a byte copy of what tonemap.wgsl wrote
        // and what `writePng` was handed, so anything but equality means the preview is of
        // something else.
        std::size_t differing = 0;
        for (std::uint32_t y = 0; y < preview.height; ++y) {
            for (std::uint32_t x = 0; x < preview.width; ++x) {
                const std::uint8_t* got = preview.rgba.data() +
                                          (static_cast<std::size_t>(y) * preview.width + x) * 4;
                const std::uint8_t* want =
                    file->data.data() + (static_cast<std::size_t>(y * preview.step) * 96 +
                                         x * preview.step) * 4;
                if (got[0] != want[0] || got[1] != want[1] || got[2] != want[2]) {
                    ++differing;
                }
            }
        }
        CHECK(differing == 0);
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Watching a render does not change it", "[gpu][render][preview]") {
    // ADR-250: the instrument is not the engine. This is not a formality -- the obvious way to
    // build the EXR preview without an allocation is to clamp and encode `frame.imageF` in place,
    // and that image is the one the encoder is about to write. An in-place conversion would pass
    // every other test in this file and silently ruin every EXR the editor ever produced with the
    // panel open, which is why the EXR arm is here and not just the PNG one.
    //
    // **And the hash comparison below is not what catches it.** That was measured, by writing the
    // in-place conversion and running this: 20 assertions failed and every one of them was the
    // file comparison; `sequenceHash` and `frameHashes` matched exactly. They have to -- the hash
    // is taken in `handleFrame` BEFORE the tap runs, so a tap that corrupts the image afterwards
    // corrupts the file and the hash agrees with it. A determinism check that runs upstream of the
    // thing it is checking proves nothing about it. The bytes on disk are the evidence.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    ProjectFixture f;

    for (const bool exr : {false, true}) {
        INFO(std::string(exr ? "exr" : "png"));
        const auto blind = f.dir / (exr ? "blind_exr" : "blind_png");
        const auto watched = f.dir / (exr ? "watched_exr" : "watched_png");
        auto settingsA = exr ? smallSettingsExr(blind) : smallSettings(blind);
        auto settingsB = exr ? smallSettingsExr(watched) : smallSettings(watched);
        // `start()` does this to the job's own copy; these are the test's copies and it has to be
        // done to them too, or `frameFile` names a .png beside a directory full of .exr.
        settingsA.normalisePattern();
        settingsB.normalisePattern();

        app::RenderJob unwatched(*ctx, shaders, loadOffline(f.project), settingsA, f.dir);
        REQUIRE(unwatched.run().has_value());
        CHECK(unwatched.previewTapped() == 0); // off by default, and off means nothing happened

        app::RenderJob observed(*ctx, shaders, loadOffline(f.project), settingsB, f.dir);
        observed.setPreviewEnabled(true);
        REQUIRE(observed.run().has_value());
        CHECK(observed.previewTapped() == 10);

        CHECK(observed.progress().sequenceHash == unwatched.progress().sequenceHash);
        CHECK(observed.frameHashes() == unwatched.frameHashes());
        // The hashes are over the images in memory; this is over the bytes on disk, which is the
        // thing the owner receives.
        for (std::uint64_t i = 0; i < 10; ++i) {
            const auto a = settingsA.frameFile(blind, i);
            const auto b = settingsB.frameFile(watched, i);
            INFO(a.string() << " vs " << b.string());
            REQUIRE(fs::exists(a));
            REQUIRE(fs::exists(b));
            std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
            const std::string sa((std::istreambuf_iterator<char>(fa)), {});
            const std::string sb((std::istreambuf_iterator<char>(fb)), {});
            CHECK(sa == sb);
        }
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("A render nobody is watching keeps one frame and drops the rest", "[gpu][render][preview]") {
    // The UI polls at its own rate and the render produces at its own, so the interesting case is
    // the one where nobody comes for a frame at all: a closed panel, a stalled editor, a headless
    // `--render`. Nothing may accumulate, and what is finally handed over must be the newest frame
    // rather than the oldest one still queued.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    ProjectFixture f;
    app::RenderJob job(*ctx, shaders, loadOffline(f.project), smallSettings(f.dir / "unwatched"), f.dir);
    job.setPreviewEnabled(true);
    REQUIRE(job.run().has_value());

    CHECK(job.previewTapped() == 10);
    CHECK(job.previewDropped() == 9);
    app::RenderJob::FramePreview frame;
    REQUIRE(job.takePreview(frame));
    CHECK(frame.index == 9); // the newest, not the first one that arrived
    CHECK(frame.hash == job.frameHashes().back());
    CHECK_FALSE(job.takePreview(frame)); // and nothing behind it
}

TEST_CASE("The EXR preview is the file's own numbers, said plainly", "[gpu][render][preview]") {
    // An EXR is scene-linear and has no display appearance, so the preview cannot be a byte copy
    // the way the PNG one is: something has to map it. What it must not do is apply a *different*
    // map from the file's and let that pass as the file. This pins the map to the one the panel
    // claims -- clamp to 0-1, sRGB encode, nothing else -- against the floats actually written.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    ProjectFixture f;
    const auto out = f.dir / "exr_watched";
    auto settings = smallSettingsExr(out);
    settings.normalisePattern();
    app::RenderJob job(*ctx, shaders, loadOffline(f.project), settings, f.dir);
    job.setPreviewEnabled(true);
    REQUIRE(job.run().has_value());

    app::RenderJob::FramePreview frame;
    REQUIRE(job.takePreview(frame));
    CHECK(frame.linearSource); // the panel branches on this to say the tone map is not applied
    CHECK(frame.hash == job.frameHashes().back());

    auto file = assets::readExr(settings.frameFile(out, frame.index));
    REQUIRE(file.has_value());
    REQUIRE(file->format == scene::TextureFormat::Rgba32Float);
    REQUIRE(file->width == 96);
    const float* pixels = reinterpret_cast<const float*>(file->data.data());

    std::size_t differing = 0;
    float brightest = 0.0f;
    for (std::uint32_t y = 0; y < frame.height; ++y) {
        for (std::uint32_t x = 0; x < frame.width; ++x) {
            const std::uint8_t* got = frame.rgba.data() +
                                      (static_cast<std::size_t>(y) * frame.width + x) * 4;
            const float* want = pixels + (static_cast<std::size_t>(y * frame.step) * 96 +
                                          x * frame.step) * 4;
            for (int c = 0; c < 3; ++c) {
                brightest = std::max(brightest, want[c]);
                const float v = std::clamp(want[c], 0.0f, 1.0f);
                const float s = v <= 0.0031308f ? v * 12.92f
                                                : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
                if (got[c] != static_cast<std::uint8_t>(std::lround(s * 255.0f))) {
                    ++differing;
                }
            }
        }
    }
    CHECK(differing == 0);
    // The fixture's orb is deliberately emissive past 1.0, so this render really does contain
    // values the preview has to clamp. Without that, "clamped and encoded" would be untested and
    // a preview that merely multiplied by 255 would pass.
    CHECK(brightest > 1.0f);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("What watching a render costs it", "[.perf][render][preview]") {
    // ADR-170: minima over repeats, and structural quantities before wall times. The arms are
    // interleaved rather than run in two blocks, so a machine that gets busier partway through
    // loads both of them and not just the second.
    //
    //   avgen_render_tests "[.perf][preview]"
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    ProjectFixture f;
    constexpr int kRepeats = 3;
    constexpr std::uint32_t kW = 1920;
    constexpr std::uint32_t kH = 1080;

    auto arm = [&](bool watched, int run) {
        auto settings = smallSettings(f.dir / fmt::format("perf_{}_{}", watched ? "on" : "off", run));
        settings.width = kW;
        settings.height = kH;
        settings.endSeconds = 2.9; // 24 frames at 10 fps
        app::RenderJob job(*ctx, shaders, loadOffline(f.project), settings, f.dir);
        job.setPreviewEnabled(watched);
        REQUIRE(job.start().has_value());
        const auto began = std::chrono::steady_clock::now();
        REQUIRE(job.run().has_value());
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
        return std::tuple{seconds, job.previewSeconds(), job.progress(), job.previewTapped(),
                          job.previewDropped()};
    };

    double offMin = 1e9;
    double onMin = 1e9;
    double tapMin = 1e9;
    std::uint64_t offHash = 0;
    std::uint64_t onHash = 0;
    std::uint64_t tapped = 0;
    std::uint64_t dropped = 0;
    std::uint64_t frames = 0;
    for (int i = 0; i < kRepeats; ++i) {
        {
            auto [seconds, tap, p, t, d] = arm(false, i);
            offMin = std::min(offMin, seconds);
            offHash = p.sequenceHash;
            frames = p.framesRendered;
            CHECK(t == 0); // off is off: the tap did not run at all
            CHECK(tap == 0.0);
        }
        {
            auto [seconds, tap, p, t, d] = arm(true, i);
            onMin = std::min(onMin, seconds);
            tapMin = std::min(tapMin, tap);
            onHash = p.sequenceHash;
            tapped = t;
            dropped = d;
        }
    }

    // The structural half, which does not move with the load average.
    const std::uint32_t step = previewStep(kW, kH);
    const std::uint64_t framePixels = static_cast<std::uint64_t>(kW) * kH;
    const std::uint64_t readPixels = static_cast<std::uint64_t>(kW / step) * (kH / step);
    std::printf("\n-- ADR-320: what the preview costs (%llu frames at %ux%u, min of %d) --\n",
                static_cast<unsigned long long>(frames), kW, kH, kRepeats);
    std::printf("  source pixels per frame   %llu\n", static_cast<unsigned long long>(framePixels));
    std::printf("  pixels the tap reads      %llu  (1 in %llu; stride %u)\n",
                static_cast<unsigned long long>(readPixels),
                static_cast<unsigned long long>(framePixels / readPixels), step);
    std::printf("  bytes copied per frame    %llu  (of %llu in the frame)\n",
                static_cast<unsigned long long>(readPixels * 4),
                static_cast<unsigned long long>(framePixels * 4));
    std::printf("  frames tapped / dropped   %llu / %llu\n", static_cast<unsigned long long>(tapped),
                static_cast<unsigned long long>(dropped));
    std::printf("  render  preview off       %.3f s\n", offMin);
    std::printf("  render  preview on        %.3f s  (%+.2f%%)\n", onMin,
                100.0 * (onMin - offMin) / offMin);
    std::printf("  inside the tap            %.4f s total, %.3f ms per frame\n", tapMin,
                1000.0 * tapMin / static_cast<double>(std::max<std::uint64_t>(frames, 1)));
    std::printf("  tap as a share of render  %.3f%%\n", 100.0 * tapMin / offMin);

    // ADR-250, asserted rather than printed: the deliverable is the same either way.
    CHECK(onHash == offHash);
    CHECK(tapped == frames);
    // Nothing accumulates when nobody collects: every frame but the one still in hand was dropped.
    CHECK(dropped == frames - 1);
}
