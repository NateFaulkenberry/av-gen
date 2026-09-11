#include "app/render_job.hpp"

#include "assets/exr.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "gpu/context.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace avgen::app {

RenderJob::RenderJob(gpu::Context& context, gpu::ShaderLibrary& shaders, std::unique_ptr<Engine> engine,
                     RenderSettings settings, std::filesystem::path baseDir)
    : context_(context), shaders_(shaders), engine_(std::move(engine)), settings_(std::move(settings)),
      baseDir_(std::move(baseDir)) {}

RenderJob::~RenderJob() {
    cancelled_ = true;
    {
        std::lock_guard lock(mutex_);
        stopEncoders_ = true;
    }
    cv_.notify_all();
    spaceCv_.notify_all();
    for (auto& t : encoders_) {
        if (t.joinable()) {
            t.join();
        }
    }
}

void RenderJob::fail(std::string message) {
    {
        std::lock_guard lock(mutex_);
        if (error_.empty()) {
            error_ = std::move(message);
        }
    }
    spaceCv_.notify_all();
}

Result<void> RenderJob::start() {
    if (started_) {
        return avgen::fail("render job already started");
    }
    if (engine_ == nullptr || engine_->mode() != EngineMode::Offline) {
        return avgen::fail("render job needs an offline engine");
    }
    if (auto r = settings_.validate(); !r) {
        return r;
    }
    output_ = settings_.outputPath.is_absolute() ? settings_.outputPath : (baseDir_ / settings_.outputPath);
    output_ = output_.lexically_normal();
    if (settings_.outputPath.empty()) {
        return avgen::fail("render output path is empty");
    }
    end_ = settings_.resolvedEnd(engine_->durationSeconds(), engine_->timeline().durationSeconds());
    total_ = settings_.frameCount(end_);

    // Warm-up: the first frames drawn with freshly compiled pipelines in a process differ by 1 LSB
    // in a few scattered pixels from every later render (Metal replaces the pipelines' GPU
    // binaries shortly after creation; Dawn caches pipeline objects device-wide, so a throwaway
    // renderer warms the real one's). Rendering two frames first makes the job's output identical
    // to any later job's. AVGEN_NO_WARMUP=1 reproduces the difference (development log,
    // 2026-09-09 investigation).
    if (std::getenv("AVGEN_NO_WARMUP") == nullptr) {
        rendering::SceneRenderer warm(context_, shaders_);
        if (auto r = warm.init(); !r) {
            return r;
        }
        if (auto r = warm.resize(settings_.width, settings_.height); !r) {
            return r;
        }
        // One engine update with deltaTime 0 (no integration drifts), rendered twice.
        FixedStepClock warmClock(settings_.fps);
        warmClock.restartAt(settings_.startSeconds);
        engine_->seekSeconds(settings_.startSeconds);
        const FrameTime t = engine_->tick(warmClock);
        engine_->setViewport(settings_.width, settings_.height);
        engine_->update(t);
        const rendering::ShaderFrameInputs inputs{&engine_->shaderLayers(),
                                                  engine_->hasFrame() ? &engine_->latestFrame() : nullptr};
        for (int i = 0; i < 2; ++i) {
            if (auto img = warm.renderToImage(engine_->scene(), t, settings_.width, settings_.height, &inputs); !img) {
                return std::unexpected(img.error());
            }
        }
    }
    renderer_ = std::make_unique<rendering::SceneRenderer>(context_, shaders_);
    if (auto r = renderer_->init(); !r) {
        return r;
    }
    if (auto r = renderer_->resize(settings_.width, settings_.height); !r) {
        return r;
    }
    compositor_ = std::make_unique<rendering::CompositionRenderer>(context_, shaders_);
    if (auto r = compositor_->init(); !r) {
        return r;
    }
    compositor_->setTimeline(&renderer_->timeline());
    renderer_->setOverlay(compositor_.get());
    if (!engine_->layers().empty() && settings_.output == RenderOutput::ExrSequence) {
        // Worth saying out loud rather than shipping a sequence somebody discovers is missing its
        // captions in a grade. EXR carries the scene-linear image from *before* the tone map, and
        // the composition is display-referred and drawn after it (ADR-083).
        log::warn("render: EXR output is the scene-linear image before tone mapping; the {} "
                  "composition layer(s) are display-referred and will not appear in it",
                  engine_->layers().size());
    }
    {
        wgpu::TextureDescriptor desc{};
        desc.label = "render-job-ldr";
        desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
        desc.dimension = wgpu::TextureDimension::e2D;
        desc.size = {settings_.width, settings_.height, 1};
        desc.format = wgpu::TextureFormat::RGBA8Unorm;
        ldr_ = context_.device().CreateTexture(&desc);
        if (ldr_ == nullptr) {
            return avgen::fail("cannot create the {}x{} render target", settings_.width, settings_.height);
        }
        ldrView_ = ldr_.CreateView();
    }
    ring_ = std::make_unique<gpu::ReadbackRing>(context_, 3);
    std::error_code ec;
    if (isSequence(settings_.output)) {
        settings_.normalisePattern();
        std::filesystem::create_directories(output_, ec);
        if (ec) {
            return avgen::fail("cannot create '{}': {}", output_.string(), ec.message());
        }
    } else {
        std::filesystem::create_directories(output_.parent_path(), ec);
        assets::VideoSettings vs;
        vs.width = settings_.width;
        vs.height = settings_.height;
        vs.fps = settings_.fps;
        vs.codec = settings_.codec;
        vs.backend = settings_.backend;
        vs.quality = settings_.quality;
        if (settings_.muxAudio && engine_->hasAudio()) {
            vs.audio = engine_->audioPath();
            vs.audioOffsetSeconds = settings_.startSeconds;
        }
        auto writer = assets::openVideoWriter(output_, vs);
        if (!writer) {
            return std::unexpected(writer.error());
        }
        video_ = std::move(*writer);
    }

    // Position the offline engine at the start of the range.
    clock_ = std::make_unique<FixedStepClock>(settings_.fps);
    clock_->restartAt(settings_.startSeconds);
    engine_->seekSeconds(settings_.startSeconds);

    int threads = settings_.encoderThreads;
    if (threads <= 0) {
        threads = static_cast<int>(std::thread::hardware_concurrency()) - 1;
    }
    threads = std::clamp(threads, 1, 16);
    if (settings_.output == RenderOutput::Video) {
        threads = 1; // the writer is sequential
    }
    queueLimit_ = static_cast<std::size_t>(threads) * 2;
    for (int i = 0; i < threads; ++i) {
        encoders_.emplace_back([this] { encoderLoop(); });
    }
    startedAt_ = std::chrono::steady_clock::now();
    started_ = true;
    log::info("render: {} frames {}x{} @ {} fps, {:.3f}s..{:.3f}s -> {} ({}, {} encoder thread(s), {} readback slots)",
              total_, settings_.width, settings_.height, settings_.fps, settings_.startSeconds, end_, output_.string(),
              renderOutputName(settings_.output), threads, ring_->slots());
    return {};
}

void RenderJob::encoderLoop() {
    for (;;) {
        Pending item;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [&] { return stopEncoders_ || !queue_.empty(); });
            if (queue_.empty()) {
                return; // stopEncoders_ and drained
            }
            item = std::move(queue_.front());
            queue_.pop_front();
        }
        spaceCv_.notify_one();
        Result<void> r;
        if (settings_.output == RenderOutput::PngSequence) {
            r = assets::writePng(settings_.frameFile(output_, item.index), item.image.width, item.image.height,
                                 item.image.rgba);
        } else if (settings_.output == RenderOutput::ExrSequence) {
            r = assets::writeExr(settings_.frameFile(output_, item.index), item.imageF.width, item.imageF.height,
                                 item.imageF.rgba, true);
        } else {
            r = video_->writeFrame(item.image.rgba);
        }
        if (!r) {
            fail(fmt::format("frame {}: {}", item.index, r.error().message));
            cancelled_ = true;
        }
        std::lock_guard lock(mutex_);
        ++written_;
    }
}

Result<void> RenderJob::renderOne() {
    const FrameTime time = engine_->tick(*clock_);
    engine_->setViewport(settings_.width, settings_.height);
    engine_->update(time);
    const rendering::ShaderFrameInputs shaderInputs{&engine_->shaderLayers(),
                                                    engine_->hasFrame() ? &engine_->latestFrame() : nullptr};
    // The same clock the live path uses, so frame f lands in the same place either way.
    compositor_->setInput(&engine_->layers(), engine_->timelineClock().seconds);
    // The frame's passes and its readback copy go into one command buffer; the ring submits it
    // and starts the map, and only blocks when all its slots are still on the GPU.
    wgpu::CommandEncoder encoder = context_.device().CreateCommandEncoder();
    const gpu::TargetView target{ldrView_, wgpu::TextureFormat::RGBA8Unorm, settings_.width, settings_.height};
    if (auto r = renderer_->render(encoder, engine_->scene(), time, target, &shaderInputs); !r) {
        return r;
    }
    const bool exr = settings_.output == RenderOutput::ExrSequence;
    const auto& source = exr ? renderer_->hdrOutputTexture() : ldr_;
    const auto format = exr ? gpu::ReadbackRing::Format::Rgba16Float : gpu::ReadbackRing::Format::Rgba8;
    if (auto r = ring_->enqueue(encoder, source, settings_.width, settings_.height, rendered_, format); !r) {
        return r;
    }
    log::trace("render frame {} t={:.4f} submitted ({} in flight)", rendered_, time.renderTime, ring_->inFlight());
    ++rendered_;
    return drain(false);
}

void RenderJob::handleFrame(gpu::ReadbackRing::Frame frame) {
    lastHash_ = frame.format == gpu::ReadbackRing::Format::Rgba16Float ? gpu::hashImage(frame.imageF)
                                                                       : gpu::hashImage(frame.image);
    sequenceHash_ = (sequenceHash_ ^ lastHash_) * 1099511628211ull;
    frameHashes_.push_back(lastHash_);
    ++readBack_;
    log::debug("render frame {} hash={:016x}", frame.index, lastHash_);
    {
        std::unique_lock lock(mutex_);
        // Frames already on the GPU when a cancel arrives are still written (partial output is
        // kept); only a failed encoder drops them, since it stops consuming.
        if (queue_.size() >= queueLimit_) {
            const auto begin = std::chrono::steady_clock::now();
            spaceCv_.wait(lock, [&] { return queue_.size() < queueLimit_ || stopEncoders_ || !error_.empty(); });
            encoderWaitSeconds_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        }
        if (error_.empty() && !stopEncoders_) {
            queue_.push_back(Pending{frame.index, std::move(frame.image), std::move(frame.imageF)});
        }
    }
    cv_.notify_one();
}

Result<void> RenderJob::drain(bool all) {
    if (all) {
        if (auto r = ring_->flush(); !r) {
            return r;
        }
    }
    while (auto frame = ring_->poll()) {
        handleFrame(std::move(*frame));
    }
    if (!ring_->error().empty()) {
        return avgen::fail("{}", ring_->error());
    }
    return {};
}

Result<void> RenderJob::finish() {
    if (ring_) {
        if (auto r = drain(true); !r) {
            fail(r.error().message);
        }
    }
    {
        std::lock_guard lock(mutex_);
        stopEncoders_ = true;
    }
    cv_.notify_all();
    for (auto& t : encoders_) {
        if (t.joinable()) {
            t.join();
        }
    }
    encoders_.clear();
    if (video_) {
        if (auto r = video_->finish(); !r) {
            fail("video: " + r.error().message);
        }
    }
    done_ = true;
    std::string error;
    {
        std::lock_guard lock(mutex_);
        error = error_;
    }
    const auto p = progress();
    if (error.empty()) {
        log::info("render complete: {} frames in {:.1f}s ({:.1f} fps), {} written, sequence hash {:016x}, GPU errors: {}",
                  p.framesReadBack, p.elapsedSeconds, p.renderFps, p.framesWritten, p.sequenceHash,
                  context_.errorCount());
        log::info("render: the render thread waited {:.2f}s for the GPU (readback ring full) and {:.2f}s for the "
                  "encoders (queue full)",
                  ring_ ? ring_->blockedSeconds() : 0.0, encoderWaitSeconds_);
        if (context_.errorCount() != 0) {
            return avgen::fail("{} GPU validation error(s) during the render", context_.errorCount());
        }
        return {};
    }
    return avgen::fail("{}", error);
}

bool RenderJob::step(int maxFrames, double budgetSeconds) {
    if (!started_ || done_) {
        return true;
    }
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < maxFrames; ++i) {
        if (cancelled_ || rendered_ >= total_) {
            break;
        }
        if (auto r = renderOne(); !r) {
            fail(r.error().message);
            cancelled_ = true;
            break;
        }
        if (budgetSeconds > 0.0 &&
            std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count() >= budgetSeconds) {
            break;
        }
    }
    if (cancelled_ || rendered_ >= total_) {
        (void)finish();
        return true;
    }
    return false;
}

Result<void> RenderJob::run() {
    if (!started_) {
        if (auto r = start(); !r) {
            return r;
        }
    }
    auto lastLog = std::chrono::steady_clock::now();
    while (!done_) {
        (void)step(1);
        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - lastLog).count() >= 1.0 && !done_) {
            const auto p = progress();
            log::info("render: {}/{} frames ({:.0f}%), {:.1f} fps, hash {:016x}", p.framesRendered, p.framesTotal,
                      p.fraction() * 100.0, p.renderFps, p.lastFrameHash);
            lastLog = now;
        }
    }
    std::string error;
    {
        std::lock_guard lock(mutex_);
        error = error_;
    }
    if (!error.empty()) {
        return avgen::fail("{}", error);
    }
    if (context_.errorCount() != 0) {
        return avgen::fail("{} GPU validation error(s) during the render", context_.errorCount());
    }
    return {};
}

void RenderJob::cancel() {
    cancelled_ = true;
    spaceCv_.notify_all();
}

RenderProgress RenderJob::progress() const {
    RenderProgress p;
    p.framesRendered = rendered_;
    p.framesReadBack = readBack_;
    p.framesTotal = total_;
    p.lastFrameHash = lastHash_;
    p.sequenceHash = sequenceHash_;
    p.finished = done_;
    p.cancelled = cancelled_;
    if (started_) {
        p.elapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt_).count();
        p.renderFps = p.elapsedSeconds > 0.0 ? static_cast<double>(rendered_) / p.elapsedSeconds : 0.0;
    }
    {
        std::lock_guard lock(const_cast<std::mutex&>(mutex_));
        p.framesWritten = written_;
        p.error = error_;
    }
    return p;
}

} // namespace avgen::app
