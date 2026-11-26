#include "app/offline_render.hpp"

#include "app/engine.hpp"
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/pipeline_compile.hpp"
#include "gpu/shader_library.hpp"
#include "platform/window.hpp"
#include "rendering/debug_visualizer.hpp"

#include <fmt/format.h>

#include <cstdlib>
#include <string>

namespace avgen::app {

namespace fs = std::filesystem;

Result<std::unique_ptr<Engine>> makeOfflineRenderEngine(const fs::path& projectFile) {
    auto offline = std::make_unique<Engine>(EngineMode::Offline);
    offline->setLiveControl(false); // a render is a pure function of its file, not of the OSC port
    if (auto r = offline->loadProject(projectFile); !r) {
        return std::unexpected(r.error());
    }
    // ADR-1288: a render of the default scene in place of the project's is not a render of the project. It
    // used to write every frame and exit 0, which made a broken scene file look like a finished deliverable.
    if (auto r = offline->requireProjectScene(); !r) {
        return std::unexpected(r.error());
    }
    for (const auto& w : offline->projectWarnings()) {
        log::warn("render project: {}", w);
    }
    return offline;
}

namespace {

// One render: a RenderJob over a fresh engine, built when the host starts it.
class GpuJob final : public api::GpuRenderJob {
public:
    GpuJob(GpuRenderBackend& backend, std::unique_ptr<Engine> engine, RenderSettings settings, fs::path base,
           RenderHistory history, rendering::DebugViewOptions debug, int maxFramesPerStep,
           std::function<void(std::shared_ptr<RenderJob>)> onStarted)
        : backend_(backend), engine_(std::move(engine)), settings_(std::move(settings)), base_(std::move(base)),
          history_(history), debug_(debug), maxFrames_(maxFramesPerStep), onStarted_(std::move(onStarted)) {}

    Result<void> start(const api::CompileWaitFn& onCompileWait) override;
    bool step(double budgetSeconds) override {
        if (!job_) {
            return true;
        }
        return job_->step(maxFrames_, budgetSeconds > 0.0 ? budgetSeconds : 0.05);
    }
    void cancel() override {
        if (job_) {
            job_->cancel();
        }
    }
    api::GpuJobProgress progress() const override {
        api::GpuJobProgress g;
        if (!job_) {
            return g;
        }
        const RenderProgress p = job_->progress();
        g.framesDone = p.framesRendered;
        g.framesTotal = p.framesTotal;
        g.historyDone = p.historyFramesRendered;
        g.historyTotal = p.historyFrames;
        g.framesWritten = p.framesWritten;
        g.phase = p.historyFramesRendered < p.historyFrames ? "history" : p.finished ? "finished" : "rendering";
        return g;
    }
    api::GpuJobOutcome outcome() const override;

private:
    GpuRenderBackend& backend_;
    std::unique_ptr<Engine> engine_;
    RenderSettings settings_;
    fs::path base_;
    RenderHistory history_;
    rendering::DebugViewOptions debug_;
    int maxFrames_ = 1 << 20;
    std::function<void(std::shared_ptr<RenderJob>)> onStarted_;
    // Shared: the Render panel keeps reading the job (its last progress, its last preview frame) after the host has
    // finished with it.
    std::shared_ptr<RenderJob> job_;
    std::uint64_t compiled_ = 0;
    std::uint64_t errorsBefore_ = 0;
};

Result<void> GpuJob::start(const api::CompileWaitFn& onCompileWait) {
    errorsBefore_ = backend_.context().errorCount();
    const gpu::PipelineCompileProgress before = gpu::pipelineCompileProgress();
    // The host's hook goes in front of whatever the process had installed (Studio's keeps the window alive while a
    // compile waits), and that one is put back afterwards rather than cleared.
    auto previous = gpu::pipelineWaitHook();
    if (onCompileWait) {
        gpu::setPipelineWaitHook([&onCompileWait, &previous](const gpu::PipelineCompileProgress& p) {
            onCompileWait(api::CompileProgress{p.started, p.landed, p.waitingFor});
            if (previous) {
                previous(p);
            }
        });
    }
    job_ = std::make_shared<RenderJob>(backend_.context(), backend_.shaders(), std::move(engine_), settings_, base_);
    job_->setDebugOptions(debug_);
    job_->setHistory(history_);
    auto r = job_->start();
    if (onCompileWait) {
        gpu::setPipelineWaitHook(std::move(previous));
    }
    compiled_ = gpu::pipelineCompileProgress().landed - before.landed;
    if (r && onStarted_) {
        onStarted_(job_);
    }
    return r;
}

api::GpuJobOutcome GpuJob::outcome() const {
    api::GpuJobOutcome o;
    if (!job_) {
        o.error = "the render never started";
        return o;
    }
    const RenderProgress p = job_->progress();
    o.cancelled = p.cancelled;
    o.error = p.error;
    o.frameHashes = job_->frameHashes();
    o.sequenceHash = p.sequenceHash;
    o.framesWritten = p.framesWritten;
    o.gpuErrors = backend_.context().errorCount() - errorsBefore_;
    o.pipelinesCreated = compiled_;
    if (o.error.empty() && o.gpuErrors != 0) {
        o.error = fmt::format("{} GPU validation error(s) during the render", o.gpuErrors);
    }
    return o;
}

} // namespace

GpuRenderBackend::GpuRenderBackend(GpuBackendOptions options) : options_(std::move(options)) {}

GpuRenderBackend::~GpuRenderBackend() {
    shaders_.reset();
    context_.reset();
}

Result<void> GpuRenderBackend::ensureDevice() {
    if (context_) {
        return {};
    }
    gpu::ContextDesc desc;
    desc.label = options_.label;
    desc.metalLayer = options_.surfaceLayer ? options_.surfaceLayer() : nullptr;
    // ADR-1811's persistent pipeline cache, where the application keeps it, so a host's cold compile happens once
    // per build (72 s measured cold) rather than once per process.
    if (options_.pipelineCache) {
        const char* env = std::getenv("AVGEN_PIPELINE_CACHE");
        const std::string value = env != nullptr ? env : "";
        if (value == "off" || value == "0") {
            log::info("pipeline cache: off (AVGEN_PIPELINE_CACHE={})", value);
        } else if (!value.empty()) {
            desc.pipelineCacheDir = value;
        } else if (const auto dir = platform::cacheDirectory(); !dir.empty()) {
            desc.pipelineCacheDir = dir / "pipeline-cache";
        }
    }
    auto context = gpu::Context::create(desc);
    if (!context) {
        return std::unexpected(context.error());
    }
    context_ = std::move(*context);
    shaders_ = std::make_unique<gpu::ShaderLibrary>(
        *context_, options_.shaderSearchDirs.empty() ? gpu::ShaderLibrary::defaultSearchDirs(options_.executable)
                                                     : options_.shaderSearchDirs);
    log::info("api: GPU device created ({}, {})", context_->capabilities().adapterName,
              context_->capabilities().backendName);
    return {};
}

Result<std::unique_ptr<api::GpuRenderJob>> GpuRenderBackend::create(const api::RenderRequest& q) {
    auto engine = makeOfflineRenderEngine(q.project);
    if (!engine) {
        return std::unexpected(engine.error());
    }
    // The project's render block (warm-up, supersample, limits, codec, audio) with the request on top, as
    // headless --render puts its flags on top of it.
    RenderSettings s = (*engine)->renderSettings();
    s.width = q.width;
    s.height = q.height;
    s.fps = q.fps;
    s.startSeconds = q.startSeconds;
    s.endSeconds = q.endSeconds;
    if (!q.tier.empty()) {
        s.tier = q.tier;
    }
    switch (q.format) {
    case api::RenderFormat::Png:
        s.output = RenderOutput::PngSequence;
        s.pattern = "frame_{:06d}.png";
        s.outputPath = q.outputDir;
        break;
    case api::RenderFormat::Exr:
        s.output = RenderOutput::ExrSequence;
        s.pattern = "frame_{:06d}.exr";
        s.outputPath = q.outputDir;
        break;
    case api::RenderFormat::Video:
        s.output = RenderOutput::Video;
        s.outputPath = q.file;
        break;
    }
    if (auto ok = s.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    const RenderHistory history = q.history == api::RenderHistory::Play ? RenderHistory::Play : RenderHistory::Seek;
    return std::unique_ptr<api::GpuRenderJob>(std::make_unique<GpuJob>(
        *this, std::move(*engine), std::move(s), q.outputDir, history, rendering::DebugViewOptions{}, 1 << 20,
        std::function<void(std::shared_ptr<RenderJob>)>{}));
}

Result<std::unique_ptr<api::GpuRenderJob>> GpuRenderBackend::createLocal(LocalRenderJob job) {
    auto engine = makeOfflineRenderEngine(job.project);
    if (job.onLoaded) {
        job.onLoaded();
    }
    if (!engine) {
        return std::unexpected(engine.error());
    }
    // Exactly what `Application::makeRenderJob` built: the settings as given, a relative output against the project
    // file's folder unless a base is named, the debug overlays, the default (seek) history.
    const fs::path base = job.outputBase.empty() ? fs::absolute(job.project).parent_path() : job.outputBase;
    return std::unique_ptr<api::GpuRenderJob>(std::make_unique<GpuJob>(
        *this, std::move(*engine), std::move(job.settings), base, RenderHistory::Seek, job.debug,
        std::max(1, job.maxFramesPerStep), std::move(job.onStarted)));
}

std::unique_ptr<api::RenderBackend> makeGpuRenderBackend(GpuBackendOptions options) {
    return std::make_unique<GpuRenderBackend>(std::move(options));
}

api::RenderBackendFactory gpuRenderBackendFactory(GpuBackendOptions options) {
    return [options]() { return makeGpuRenderBackend(options); };
}

} // namespace avgen::app
