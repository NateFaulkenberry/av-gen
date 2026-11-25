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

Result<std::unique_ptr<RenderJob>> makeOfflineRenderJob(gpu::Context& context, gpu::ShaderLibrary& shaders,
                                                        const fs::path& projectFile, RenderSettings settings,
                                                        const fs::path& outputBase,
                                                        const rendering::DebugViewOptions& debug) {
    auto offline = makeOfflineRenderEngine(projectFile);
    if (!offline) {
        return std::unexpected(offline.error());
    }
    auto job = std::make_unique<RenderJob>(context, shaders, std::move(*offline), std::move(settings),
                                           outputBase.empty() ? fs::absolute(projectFile).parent_path() : outputBase);
    job->setDebugOptions(debug);
    if (auto r = job->start(); !r) {
        return std::unexpected(r.error());
    }
    return job;
}

namespace {

class GpuBackend;

// One API render: a RenderJob over a fresh engine, built when the host starts it.
class GpuJob final : public api::GpuRenderJob {
public:
    GpuJob(GpuBackend& backend, std::unique_ptr<Engine> engine, RenderSettings settings, fs::path base,
           RenderHistory history)
        : backend_(backend), engine_(std::move(engine)), settings_(std::move(settings)), base_(std::move(base)),
          history_(history) {}

    Result<void> start(const api::CompileWaitFn& onCompileWait) override;
    bool step(double budgetSeconds) override {
        if (!job_) {
            return true;
        }
        return job_->step(1 << 20, budgetSeconds > 0.0 ? budgetSeconds : 0.05);
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
    GpuBackend& backend_;
    std::unique_ptr<Engine> engine_;
    RenderSettings settings_;
    fs::path base_;
    RenderHistory history_;
    std::unique_ptr<RenderJob> job_;
    std::uint64_t compiled_ = 0;
    std::uint64_t errorsBefore_ = 0;
};

class GpuBackend final : public api::RenderBackend {
public:
    explicit GpuBackend(GpuBackendOptions options) : options_(std::move(options)) {}
    ~GpuBackend() override {
        shaders_.reset();
        context_.reset();
    }

    Result<void> ensureDevice() override {
        if (context_) {
            return {};
        }
        gpu::ContextDesc desc;
        desc.label = "avgen-engine-host";
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
    bool deviceCreated() const override { return context_ != nullptr; }

    Result<std::unique_ptr<api::GpuRenderJob>> create(const api::RenderRequest& q) override {
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
        const RenderHistory history =
            q.history == api::RenderHistory::Play ? RenderHistory::Play : RenderHistory::Seek;
        return std::unique_ptr<api::GpuRenderJob>(
            std::make_unique<GpuJob>(*this, std::move(*engine), std::move(s), q.outputDir, history));
    }

    gpu::Context& context() { return *context_; }
    gpu::ShaderLibrary& shaders() { return *shaders_; }

private:
    GpuBackendOptions options_;
    std::unique_ptr<gpu::Context> context_;
    std::unique_ptr<gpu::ShaderLibrary> shaders_;
};

Result<void> GpuJob::start(const api::CompileWaitFn& onCompileWait) {
    errorsBefore_ = backend_.context().errorCount();
    const gpu::PipelineCompileProgress before = gpu::pipelineCompileProgress();
    if (onCompileWait) {
        gpu::setPipelineWaitHook([&onCompileWait](const gpu::PipelineCompileProgress& p) {
            onCompileWait(api::CompileProgress{p.started, p.landed, p.waitingFor});
        });
    }
    job_ = std::make_unique<RenderJob>(backend_.context(), backend_.shaders(), std::move(engine_), settings_, base_);
    job_->setHistory(history_);
    auto r = job_->start();
    gpu::setPipelineWaitHook({});
    compiled_ = gpu::pipelineCompileProgress().landed - before.landed;
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

std::unique_ptr<api::RenderBackend> makeGpuRenderBackend(GpuBackendOptions options) {
    return std::make_unique<GpuBackend>(std::move(options));
}

api::RenderBackendFactory gpuRenderBackendFactory(GpuBackendOptions options) {
    return [options]() { return makeGpuRenderBackend(options); };
}

} // namespace avgen::app
