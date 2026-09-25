#include "app/director_stills.hpp"

#include "app/directing_context.hpp"
#include "app/engine.hpp"
#include "app/render_source.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"

#include <algorithm>
#include <chrono>
#include <fmt/format.h>
#include <utility>
#include <system_error>
#include <unistd.h>

namespace avgen::app {
namespace {

} // namespace

static double since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

static std::string clockText(double seconds) {
    const double s = std::max(0.0, seconds);
    const auto minutes = static_cast<int>(s / 60.0);
    return fmt::format("{:02d}:{:06.3f}", minutes, s - (minutes * 60.0));
}

std::vector<std::pair<std::string, const seq::Shot*>> proposedShots(const directing::Compilation& c) {
    std::vector<std::pair<std::string, const seq::Shot*>> out;
    for (const directing::PlanShot& ps : c.plan.shots) {
        if (c.validation.isBlocked(ps.key)) {
            continue;
        }
        if (const seq::Shot* shot = c.staged.sequence.shotNamed(ps.name); shot != nullptr) {
            out.emplace_back(ps.key, shot);
        }
    }
    return out;
}

Result<ShotStillsReport> renderShotStills(gpu::Context& context, gpu::ShaderLibrary& shaders, Engine& live,
                                          const directing::Compilation& compilation, std::uint32_t width,
                                          std::uint32_t height, const std::filesystem::path& scratchDir) {
    ShotStillsReport report;
    const auto shots = proposedShots(compilation);
    if (shots.empty()) {
        return report;
    }
    static std::uint64_t serial = 0;
    const RenderSource source = renderSourceFor(live.projectPath(), scratchDir, "director_stills", ++serial,
                                                static_cast<long long>(::getpid()));
    auto start = std::chrono::steady_clock::now();
    if (auto r = live.writeProjectCopy(source.scratch); !r) {
        return fail("stills: cannot write the scratch copy: {}", r.error().message);
    }
    Engine scratch(EngineMode::Offline);
    scratch.setLiveControl(false); // the editor holds the OSC port; a scratch copy listens to nothing
    auto loaded = scratch.loadProject(source.scratch);
    std::error_code ec;
    std::filesystem::remove(source.scratch, ec);
    if (!loaded) {
        return fail("stills: the scratch copy does not load: {}", loaded.error().message);
    }
    if (auto r = installCompilation(scratch, compilation); !r) {
        return fail("stills: the proposal does not install on the scratch copy: {}", r.error().message);
    }
    scratch.setViewport(width, height);
    report.loadMs = since(start);

    start = std::chrono::steady_clock::now();
    rendering::SceneRenderer renderer(context, shaders);
    if (auto r = renderer.init(); !r) {
        return fail("stills: renderer: {}", r.error().message);
    }
    std::uint64_t frame = 0;
    for (const auto& [item, shot] : shots) {
        const double mid = shot->startSeconds + (shot->durationSeconds * 0.5);
        scratch.seekSeconds(mid);
        const FrameTime t{mid, 1.0 / 60.0, ++frame};
        scratch.update(t);
        auto image = renderer.renderToImage(scratch.composition()->scene(), t, width, height);
        if (!image) {
            return fail("stills: rendering '{}': {}", shot->name, image.error().message);
        }
        report.stills.push_back(ShotStill{item, shot->name, mid, std::move(*image)});
    }
    report.renderMs = since(start);
    log::info("director stills: {} shot(s) at {}x{}: scratch session {:.0f} ms, seeks and frames {:.0f} ms",
              report.stills.size(), width, height, report.loadMs, report.renderMs);
    return report;
}

} // namespace avgen::app

namespace avgen::app {

StillsSession::StillsSession(gpu::Context& context, gpu::ShaderLibrary& shaders, std::filesystem::path scratchDir)
    : context_(context), shaders_(shaders), scratchDir_(std::move(scratchDir)) {}

StillsSession::~StillsSession() {
    cancel_ = true;
    {
        std::lock_guard lock(mutex_);
        stage_ = Stage::Failed; // release a worker waiting for a render that will not come
    }
    cv_.notify_all();
    join();
}

void StillsSession::join() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool StillsSession::request(Engine& live, const std::string& key, std::string task, directing::Compilation compilation,
                            std::uint32_t width, std::uint32_t height) {
    {
        std::lock_guard lock(mutex_);
        if (stage_ == Stage::Working || stage_ == Stage::Ready || stage_ == Stage::Rendered) {
            return false;
        }
        finishIfDone();
    }
    join();
    const auto start = std::chrono::steady_clock::now();
    started_ = start;
    compilation_ = std::move(compilation);
    shots_ = proposedShots(compilation_);
    width_ = width;
    height_ = height;
    task_ = std::move(task);
    fresh_.clear();
    done_ = 0;
    error_.clear();
    cancel_ = false;
    needLoad_ = scratch_ == nullptr || key != key_;
    loadedThisRequest_ = needLoad_;
    if (needLoad_) {
        // The copy is the only part that reads the live engine, so it is the only part of loading
        // done here. Every other step of the session is the worker's.
        static std::uint64_t serial = 0;
        copy_ = renderSourceFor(live.projectPath(), scratchDir_, "director_stills", ++serial,
                                static_cast<long long>(::getpid()))
                    .scratch;
        if (auto r = live.writeProjectCopy(copy_); !r) {
            std::lock_guard lock(mutex_);
            stage_ = Stage::Failed;
            error_ = "cannot write the scratch copy: " + r.error().message;
            return true;
        }
        scratch_.reset(); // the old session goes before the new one loads: one scratch copy in memory
        key_ = key;
    }
    mainMs_ = since(start);
    {
        std::lock_guard lock(mutex_);
        stage_ = Stage::Working;
        phase_ = needLoad_ ? "loading a scratch copy of the project" : "reusing the scratch copy";
    }
    worker_ = std::thread([this] { work(); });
    return true;
}

void StillsSession::work() {
    const auto failWith = [&](std::string message) {
        std::lock_guard lock(mutex_);
        stage_ = Stage::Failed;
        error_ = std::move(message);
        key_.clear(); // a session that failed half-way is not a cache
    };
    if (needLoad_) {
        auto engine = std::make_unique<Engine>(EngineMode::Offline);
        engine->setLiveControl(false); // the editor holds the OSC port; a scratch copy listens to nothing
        auto loaded = engine->loadProject(copy_);
        std::error_code ec;
        std::filesystem::remove(copy_, ec);
        if (!loaded) {
            failWith("the scratch copy does not load: " + loaded.error().message);
            return;
        }
        engine->setViewport(width_, height_);
        scratch_ = std::move(engine);
        loads_.fetch_add(1);
    }
    // Wholesale, like every install: the sequence, the cameras and the effect list are replaced by
    // the compilation's staging copy, so a proposal installed on a reused session replaces the last.
    if (auto r = installCompilation(*scratch_, compilation_); !r) {
        failWith("the proposal does not install on the scratch copy: " + r.error().message);
        return;
    }
    scratch_->setViewport(width_, height_);
    for (std::size_t i = 0; i < shots_.size(); ++i) {
        if (cancel_) {
            return;
        }
        const seq::Shot* shot = shots_[i].second;
        const double mid = shot->startSeconds + (shot->durationSeconds * 0.5);
        {
            std::lock_guard lock(mutex_);
            phase_ = fmt::format("simulating to {} for shot {} of {}", clockText(mid), i + 1, shots_.size());
        }
        scratch_->seekSeconds(mid);
        scratch_->update(FrameTime{mid, 1.0 / 60.0, static_cast<std::uint64_t>(i + 1)});
        std::unique_lock lock(mutex_);
        current_ = i;
        seconds_ = mid;
        stage_ = Stage::Ready;
        phase_ = fmt::format("rendering shot {} of {}", i + 1, shots_.size());
        cv_.wait(lock, [&] { return stage_ != Stage::Ready; });
        if (stage_ == Stage::Failed) {
            return;
        }
        stage_ = Stage::Working;
    }
    std::lock_guard lock(mutex_);
    stage_ = Stage::Done;
    phase_ = "done";
}

// Called with the lock held. Records a request's cost once, whichever of `step` and the next
// `request` sees it finished first -- the worker can finish between the two in one frame.
void StillsSession::finishIfDone() {
    if ((stage_ == Stage::Done || stage_ == Stage::Failed) && started_ != std::chrono::steady_clock::time_point{}) {
        wallMs_ = since(started_);
        started_ = {};
        log::info("director stills: request finished in {:.0f} ms, {:.0f} ms of it on the editor's frames ({})",
                  wallMs_, mainMs_, loadedThisRequest_ ? "loaded a scratch copy" : "reused the scratch copy");
    }
}

StillsSession::Progress StillsSession::step() {
    Progress p;
    std::unique_lock lock(mutex_);
    p.task = task_;
    p.total = shots_.size();
    if (stage_ == Stage::Ready) {
        // The worker is parked until this frame is taken: the scratch engine is the main thread's now.
        const auto start = std::chrono::steady_clock::now();
        const std::size_t i = current_;
        const double mid = seconds_;
        lock.unlock();
        if (renderer_ == nullptr) {
            renderer_ = std::make_unique<rendering::SceneRenderer>(context_, shaders_);
            if (auto r = renderer_->init(); !r) {
                renderer_.reset();
                lock.lock();
                stage_ = Stage::Failed;
                error_ = "renderer: " + r.error().message;
                cv_.notify_all();
                p.failed = true;
                p.phase = error_;
                return p;
            }
        }
        const FrameTime t{mid, 1.0 / 60.0, i + 1};
        auto image = renderer_->renderToImage(scratch_->composition()->scene(), t, width_, height_);
        lock.lock();
        if (image) {
            fresh_.push_back(ShotStill{shots_[i].first, shots_[i].second->name, mid, std::move(*image)});
            ++done_;
            stage_ = Stage::Rendered;
        } else {
            stage_ = Stage::Failed;
            error_ = fmt::format("rendering '{}': {}", shots_[i].second->name, image.error().message);
        }
        mainMs_ += since(start);
        cv_.notify_all();
    }
    p.done = done_;
    switch (stage_) {
    case Stage::Idle: p.phase = ""; break;
    case Stage::Done: p.phase = "done"; break;
    case Stage::Failed:
        p.failed = true;
        p.phase = error_;
        break;
    default:
        p.busy = true;
        p.phase = phase_;
        break;
    }
    finishIfDone();
    return p;
}

std::vector<ShotStill> StillsSession::takeNew() {
    std::lock_guard lock(mutex_);
    return std::exchange(fresh_, {});
}

} // namespace avgen::app
