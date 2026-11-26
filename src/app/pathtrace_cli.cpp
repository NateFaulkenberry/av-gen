#include "app/pathtrace_cli.hpp"

#include "app/application.hpp"
#include "app/render_settings.hpp"
#include "app/trace_sequence.hpp"
#include "core/log.hpp"
#include "pathtrace/denoise.hpp"
#include "pathtrace/trace_job.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <thread>

namespace avgen::app {
namespace {

// The two blocks of the project this command reads, by the readers `Engine::loadProject` uses: `render` (the codec,
// backend and quality a traced movie is encoded with) and `pathtrace` (ADR-366). A project with neither gets the
// defaults, as a load does.
struct ProjectSettings {
    RenderSettings render;
    PathTraceSettings trace;
};

[[nodiscard]] Result<ProjectSettings> readProjectSettings(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) return fail("cannot open project '{}'", file.string());
    const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (!doc.is_object()) return fail("'{}' is not a project (not a JSON object)", file.string());
    ProjectSettings out;
    if (doc.contains("render")) {
        auto r = RenderSettings::fromJson(doc["render"]);
        if (!r) return std::unexpected(r.error());
        out.render = *r;
    }
    if (doc.contains("pathtrace")) {
        auto r = PathTraceSettings::fromJson(doc["pathtrace"]);
        if (!r) return std::unexpected(r.error());
        out.trace = *r;
    }
    return out;
}

// ADR-383. A path-traced range, through `app::TraceSequence`: one project load, the shared `FrameSequenceDriver`, and
// either an EXR per frame or -- via the CPU output transform -- a movie.
int runSequence(const AppOptions& options, const std::filesystem::path& projectFile, const ProjectSettings& project,
                const PathTraceSettings& authored) {
    // The SAME decision the Render panel makes, through the same function: name it .mov and get a movie, name it
    // anything else and get a folder of scene-linear EXRs.
    RenderSettings video = project.render;
    if (options.codec) video.codec = *options.codec;
    TraceSequenceRequest request =
        traceSequenceRequestFrom(projectFile, authored, options.width, options.height, *options.pathtrace, video);
    request.reuseAcceleration = !options.ptRebuildBvh;
    if (auto ok = request.validate(); !ok) {
        log::error("pathtrace: {}", ok.error().message);
        return 2;
    }

    TraceSequence sequence(std::move(request));
    if (auto ok = sequence.start(); !ok) {
        log::error("pathtrace: {}", ok.error().message);
        return 2;
    }
    log::info("pathtrace: denoise {}", pathtrace::denoiseVersion());

    // Progress that is honest at both levels: which frame, and how far into it.
    std::uint64_t lastFrame = ~0ull;
    while (!sequence.step(1)) {
        const SequenceProgress p = sequence.progress();
        if (p.framesSubmitted != lastFrame) {
            lastFrame = p.framesSubmitted;
            const double remaining = p.estimatedRemainingSeconds;
            log::info("pathtrace: frame {}/{} ({:.0f}%), {:.1f}s elapsed{}", p.framesSubmitted, p.framesTotal,
                      p.fraction() * 100.0, p.elapsedSeconds,
                      remaining >= 0.0 ? fmt::format(", about {:.0f}s left", remaining) : std::string());
        }
    }
    const SequenceProgress p = sequence.progress();
    if (!p.error.empty()) {
        log::error("pathtrace: {}", p.error);
        return 4;
    }
    if (p.cancelled) {
        log::warn("pathtrace: cancelled after {} frame(s)", p.framesWritten);
        return 3;
    }
    log::info("pathtrace: {} frame(s) written, sequence hash {:016x}", p.framesWritten, p.sequenceHash);
    return 0;
}

} // namespace

int runPathTraceCommand(const AppOptions& options) {
    if (!options.project) {
        log::error("--pathtrace needs a project: pass --project <file.json>");
        return 2;
    }
    const std::filesystem::path projectFile = *options.project;
    auto project = readProjectSettings(projectFile);
    if (!project) {
        log::error("pathtrace: {}", project.error().message);
        return 2;
    }

    // Start from what the project says and override with what was typed (ADR-366).
    PathTraceSettings authored = project->trace;
    if (options.ptSeconds) authored.seconds = *options.ptSeconds;
    if (options.ptSamples) authored.samplesPerPixel = std::max(1u, *options.ptSamples);
    if (options.ptDepth) authored.maxDepth = *options.ptDepth;
    if (options.ptSeed) authored.seed = *options.ptSeed;
    if (options.ptThreads) authored.threads = *options.ptThreads;
    if (options.ptDenoise) authored.denoise = *options.ptDenoise;
    if (options.ptAovs) authored.writeAovs = *options.ptAovs;
    if (options.ptProbe) authored.albedoProbe = *options.ptProbe;
    // ADR-383: `--range a:b` makes it a sequence -- the same flag `--render` uses, and `--fps` likewise.
    if (options.rangeStart) authored.seconds = *options.rangeStart;
    if (options.rangeEnd) authored.endSeconds = *options.rangeEnd;
    if (options.fpsGiven && options.offlineFps > 0.0) authored.fps = options.offlineFps;

    if (authored.isSequence()) {
        return runSequence(options, projectFile, *project, authored);
    }

    pathtrace::TraceJobRequest request;
    request.project = projectFile;
    request.seconds = authored.seconds;
    request.output = *options.pathtrace;
    request.writeAovs = authored.writeAovs;
    request.denoise = authored.denoise;
    request.settings = traceSettingsFrom(authored, options.width, options.height);

    log::info("pathtrace: {} at second {:.3f}, {}x{} at {} spp, depth {}, seed {}", projectFile.string(),
              request.seconds, request.settings.width, request.settings.height, request.settings.samplesPerPixel,
              request.settings.maxDepth, request.settings.seed);
    log::info("pathtrace: denoise {}", pathtrace::denoiseVersion());

    pathtrace::TraceJob job(std::move(request));
    // Started on the job's own thread rather than run inline, so this path exercises exactly the code a UI would.
    job.start();

    pathtrace::TraceJobState lastState = pathtrace::TraceJobState::Queued;
    std::uint32_t lastSamples = 0;
    while (!job.done()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        const auto p = job.progress();
        if (p.state != lastState) {
            lastState = p.state;
            log::info("pathtrace: {}", p.stage);
        } else if (p.state == pathtrace::TraceJobState::Rendering && p.samplesDone != lastSamples) {
            lastSamples = p.samplesDone;
            log::info("pathtrace: {}/{} samples ({:.0f}%), {:.1f} s elapsed", p.samplesDone, p.samplesTotal,
                      p.fraction * 100.0f, p.elapsedSeconds);
        }
    }
    job.wait();

    const auto final = job.progress();
    if (final.state == pathtrace::TraceJobState::Complete) return 0;
    if (final.state == pathtrace::TraceJobState::Cancelled) {
        log::warn("pathtrace: cancelled");
        return 8;
    }
    log::error("pathtrace: {}", final.error.empty() ? "failed" : final.error);
    return 7;
}

} // namespace avgen::app
