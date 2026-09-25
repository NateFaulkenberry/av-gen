#include "pathtrace/trace_job.hpp"

#include "app/engine.hpp"
#include "core/log.hpp"
#include "core/time.hpp"

#include <fmt/format.h>

namespace avgen::pathtrace {
namespace {

// How much of the overall bar each stage is worth. These are not measurements -- they are a
// declared split, and the only one that carries a real sub-fraction is rendering, which dominates
// every real trace anyway.
constexpr float kSceneShare = 0.05f;
constexpr float kBvhShare = 0.05f;
constexpr float kRenderShare = 0.80f;
constexpr float kDenoiseShare = 0.07f;

} // namespace

std::string_view traceJobStateName(TraceJobState s) {
    switch (s) {
    case TraceJobState::Queued: return "queued";
    case TraceJobState::BuildingScene: return "building scene";
    case TraceJobState::BuildingAcceleration: return "building acceleration";
    case TraceJobState::Rendering: return "rendering";
    case TraceJobState::Denoising: return "denoising";
    case TraceJobState::Writing: return "writing";
    case TraceJobState::Complete: return "complete";
    case TraceJobState::Cancelled: return "cancelled";
    case TraceJobState::Failed: return "failed";
    }
    return "unknown";
}

bool traceJobStateIsTerminal(TraceJobState s) {
    return s == TraceJobState::Complete || s == TraceJobState::Cancelled || s == TraceJobState::Failed;
}

Result<void> TraceJobRequest::validate() const {
    if (project.empty()) return fail("pathtrace: no project given");
    if (!std::filesystem::exists(project)) {
        return fail("pathtrace: project '{}' does not exist", project.string());
    }
    if (output.empty()) return fail("pathtrace: no output path given");
    if (!std::isfinite(seconds) || seconds < 0.0) {
        return fail("pathtrace: timeline second {} is not a valid time", seconds);
    }
    return settings.validate();
}

TraceJob::TraceJob(TraceJobRequest request) : request_(std::move(request)) {}

TraceJob::~TraceJob() {
    // A destructor must never leave a thread running against freed state, and must never kill one
    // mid-render either. Ask it to stop, then wait.
    cancel();
    if (thread_.joinable()) thread_.join();
}

void TraceJob::setState(TraceJobState s) { state_.store(s); }

void TraceJob::cancel() { cancel_.store(true); }

void TraceJob::start() {
    if (thread_.joinable()) return;
    thread_ = std::thread([this] { (void)execute(); });
}

void TraceJob::wait() {
    if (thread_.joinable()) thread_.join();
}

Result<void> TraceJob::run() { return execute(); }

TraceProgress TraceJob::progress() const {
    TraceProgress p;
    p.state = state_.load();
    p.stage = std::string(traceJobStateName(p.state));
    p.samplesDone = samplesDone_.load();
    p.samplesTotal = request_.settings.samplesPerPixel;
    if (started_.time_since_epoch().count() != 0) {
        p.elapsedSeconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
    }
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        p.error = error_;
    }

    switch (p.state) {
    case TraceJobState::Queued:
        p.fraction = 0.0f;
        p.fractionKnown = true;
        break;
    case TraceJobState::BuildingScene:
    case TraceJobState::BuildingAcceleration:
        // Neither stage emits an intermediate signal, so neither gets a made-up one. Spec section 36
        // asks for real progress; a bar that creeps while nothing is known is the thing it forbids.
        p.fraction = p.state == TraceJobState::BuildingScene ? 0.0f : kSceneShare;
        p.fractionKnown = false;
        break;
    case TraceJobState::Rendering: {
        const float within = p.samplesTotal > 0
                                 ? static_cast<float>(p.samplesDone) / static_cast<float>(p.samplesTotal)
                                 : 0.0f;
        p.fraction = kSceneShare + kBvhShare + kRenderShare * within;
        p.fractionKnown = true;   // a count of finished samples is a real measurement
        break;
    }
    case TraceJobState::Denoising:
        p.fraction = kSceneShare + kBvhShare + kRenderShare;
        p.fractionKnown = false;  // OIDN is one opaque call
        break;
    case TraceJobState::Writing:
        p.fraction = kSceneShare + kBvhShare + kRenderShare + kDenoiseShare;
        p.fractionKnown = false;
        break;
    case TraceJobState::Complete:
        p.fraction = 1.0f;
        p.fractionKnown = true;
        break;
    case TraceJobState::Cancelled:
    case TraceJobState::Failed:
        p.fractionKnown = true;
        break;
    }
    return p;
}

Result<void> TraceJob::execute() {
    started_ = std::chrono::steady_clock::now();

    const auto failWith = [&](Error e) -> Result<void> {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            error_ = e.message;
        }
        setState(TraceJobState::Failed);
        log::error("pathtrace: {}", e.message);
        return std::unexpected(std::move(e));
    };
    const auto cancelled = [&] {
        if (!cancel_.load()) return false;
        setState(TraceJobState::Cancelled);
        log::info("pathtrace: cancelled");
        return true;
    };

    if (auto ok = request_.validate(); !ok) return failWith(ok.error());

    // ---- scene ---------------------------------------------------------------------------------
    setState(TraceJobState::BuildingScene);
    if (cancelled()) return {};

    // Offline mode, which is GPU-free: the path tracer needs no device, so a trace runs on a
    // machine with no working GPU and never contends for one.
    app::Engine engine(app::EngineMode::Offline);
    engine.setLiveControl(false); // a trace listens to nothing live, and never takes the editor's port
    if (!engine.loadProject(request_.project)) {
        return failWith(Error{fmt::format("could not load project '{}'", request_.project.string())});
    }
    // A warm-up step at dt 0 then the real one, which is what app::RenderJob does: some systems
    // need a frame to settle, and a snapshot of frame zero is not a snapshot of the scene.
    engine.update(FrameTime{request_.seconds, 0.0, 0});
    engine.update(FrameTime{request_.seconds, 1.0 / 60.0, 1});
    if (cancelled()) return {};

    const Snapshot snapshot = buildSnapshot(engine.scene());
    capabilities_ = snapshot.capabilities;
    logCapabilities(snapshot);
    if (snapshot.empty()) {
        return failWith(Error{fmt::format(
            "the scene at second {} has no geometry the tracer can see; refusing to write a black frame",
            request_.seconds)});
    }
    if (cancelled()) return {};

    // ---- render (the BVH build happens inside, and reports itself) ------------------------------
    setState(TraceJobState::BuildingAcceleration);
    PathTracer tracer;
    tracer.setStageCallback([this](std::string_view stage) {
        if (stage == "rendering") setState(TraceJobState::Rendering);
    });
    TraceSettings settings = request_.settings;
    if (request_.denoise) settings.captureFeatures = true;   // the denoiser needs the feature AOVs

    auto ok = tracer.render(
        snapshot, settings, framebuffer_, [this] { return cancel_.load(); },
        [this](std::uint32_t done, std::uint32_t total) {
            samplesDone_.store(done);
            (void)total;
        });
    if (!ok) return failWith(ok.error());
    stats_ = tracer.stats();
    if (cancelled()) return {};   // section 37: before denoising

    // ---- denoise -------------------------------------------------------------------------------
    std::vector<glm::vec3> denoised;
    if (request_.denoise) {
        setState(TraceJobState::Denoising);
        if (!denoiseAvailable()) {
            return failWith(Error{"denoising was requested but this build has no denoiser; "
                                  "configure with -DAVGEN_PATHTRACE_DENOISE=ON (ADR-353)"});
        }
        const std::vector<glm::vec3> colour = framebuffer_.resolvedRadiance();
        const std::vector<glm::vec3> albedo = framebuffer_.resolvedAlbedo();
        const std::vector<glm::vec3> normal = framebuffer_.resolvedNormal();
        DenoiseInput in;
        in.width = framebuffer_.width;
        in.height = framebuffer_.height;
        in.color = &colour;
        if (!albedo.empty()) in.albedo = &albedo;
        if (!normal.empty()) in.normal = &normal;
        if (auto d = denoise(in, denoised); !d) return failWith(d.error());
        // Fold the denoised result back in as a one-sample image, so the EXR writer's divide by
        // sampleCount still yields exactly these values rather than re-averaging them.
        framebuffer_.radiance = denoised;
        framebuffer_.sampleCount = 1;
        if (cancelled()) return {};   // section 37: before writing
    }

    // ---- write ---------------------------------------------------------------------------------
    setState(TraceJobState::Writing);
    std::error_code ec;
    if (request_.output.has_parent_path()) {
        std::filesystem::create_directories(request_.output.parent_path(), ec);
    }
    auto wrote = request_.writeAovs ? writeFramebufferAovExr(framebuffer_, request_.output)
                                    : writeFramebufferExr(framebuffer_, request_.output, false);
    if (!wrote) return failWith(wrote.error());

    setState(TraceJobState::Complete);
    log::info("pathtrace: wrote {} ({}x{}, {} spp) in {:.2f} s -- scene {} tri, bvh {:.0f} ms, render {:.0f} ms",
              request_.output.string(), framebuffer_.width, framebuffer_.height,
              request_.settings.samplesPerPixel,
              std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count(),
              snapshot.visibleTriangleCount(), stats_.buildSeconds * 1000.0,
              stats_.renderSeconds * 1000.0);
    return {};
}

} // namespace avgen::pathtrace
