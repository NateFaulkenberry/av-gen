#include "app/render_job.hpp"

#include <sstream>

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

    // ADR-186: the distance reductions this render is under. Told to the *engine*, not the
    // renderer, because two of the three live in the simulation rather than in a pass -- the rig
    // pose rate and the entity world's behaviour bands -- and the third reads the scene the engine
    // hands over. This is the same lesson ADR-147 taught about the tier: a policy set on the
    // interactive side only is a policy the deliverable never gets.
    {
        const scene::DetailLimits resolved = settings_.resolvedLimits();
        engine_->setDetailLimits(resolved);
        if (resolved.anyLifted()) {
            log::info("render: lifting live detail limits -- procedural distance cull {}, LOD rungs "
                      "{}, rig pose rate {}, entity behaviour bands {}",
                      resolved.proceduralDistanceCull ? "kept" : "off",
                      resolved.proceduralLodRungs ? "kept" : "off",
                      resolved.rigDistanceRate ? "kept" : "off",
                      resolved.entityDistanceCull ? "kept" : "off");
        }
    }

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
    // ADR-147 / §5.9: the tier the deliverable is rendered at. Without this the job ran at
    // whatever the renderer defaults to -- Realtime -- and a batch frame came out byte-identical
    // to an interactive one, so every promise the Offline tier makes (no temporal shortcut, no
    // representation or shading shortcut, full-resolution auxiliary passes) was stated in the tier
    // table and not kept by the path that produces the output anyone ships.
    {
        rendering::QualityTier tier = rendering::QualityTier::Offline;
        if (!rendering::qualityTierFromName(settings_.tier, tier)) {
            return std::unexpected(Error{fmt::format(
                "render: unknown tier '{}' (preview|realtime|high|offline)", settings_.tier)});
        }
        renderer_->setQuality(tier);
    }
    // ADR-212: supersampling. **Before the resize**, and after the tier, because `resize()` is what
    // consumes `renderScale` -- it sizes the HDR target to `output * renderScale` -- and
    // `setQuality(tier)` replaces the whole settings object. Set it after the resize and the log
    // line says 2.00x while the target is still the output size: the first version of this did
    // exactly that and produced a byte-identical sequence hash, which is the only reason it was
    // caught (ADR-182 -- a probe must prove it established the state it measures).
    if (settings_.supersample > 1.0f) {
        rendering::QualitySettings quality = renderer_->qualitySettings();
        quality.renderScale = std::min(settings_.supersample, 2.0f);
        renderer_->setQualitySettings(quality);
    }
    if (auto r = renderer_->resize(settings_.width, settings_.height); !r) {
        return r;
    }
    // Quality arms, applied before the pass toggles because an arm sets a policy field and a
    // toggle removes a pass from whatever policy chose.
    if (!settings_.qualityArms.empty()) {
        rendering::QualitySettings quality = renderer_->qualitySettings();
        std::istringstream stream(settings_.qualityArms);
        std::string name;
        std::string applied;
        while (std::getline(stream, name, ',')) {
            const auto begin = name.find_first_not_of(" \t");
            const auto end = name.find_last_not_of(" \t");
            if (begin == std::string::npos) {
                continue;
            }
            name = name.substr(begin, end - begin + 1);
            if (!rendering::SceneRenderer::setQualityArm(quality, name)) {
                return std::unexpected(Error{fmt::format(
                    "render: unknown quality arm '{}' (one of: {})", name,
                    rendering::SceneRenderer::qualityArmNames())});
            }
            applied += applied.empty() ? name : ", " + name;
        }
        renderer_->setQualitySettings(quality);
        log::warn("render: this is a DIAGNOSTIC render -- quality arm(s): {}", applied);
    }
    // The diagnostic arms, which used to stop at the interactive renderer (ADR-182). Applied after
    // the tier, because a tier is a policy and this is a removal from whatever policy chose.
    if (!settings_.disablePasses.empty()) {
        rendering::SceneRenderer::PassToggles toggles = renderer_->passToggles();
        std::istringstream stream(settings_.disablePasses);
        std::string name;
        std::vector<std::string> applied;
        while (std::getline(stream, name, ',')) {
            const auto begin = name.find_first_not_of(" \t");
            const auto end = name.find_last_not_of(" \t");
            if (begin == std::string::npos) {
                continue;
            }
            name = name.substr(begin, end - begin + 1);
            if (!rendering::SceneRenderer::setPassArm(toggles, name, false)) {
                return std::unexpected(Error{fmt::format(
                    "render: unknown phase '{}' to disable (one of: {})", name,
                    rendering::SceneRenderer::passArmNames())});
            }
            applied.push_back(name);
        }
        renderer_->setPassToggles(toggles);
        std::string list;
        for (const std::string& a : applied) {
            list += list.empty() ? a : ", " + a;
        }
        // Loud, because this frame is not the deliverable and a sequence that quietly came out
        // without its water is the kind of file somebody ships.
        log::warn("render: this is a DIAGNOSTIC render -- phase(s) disabled: {}", list);
    }
    compositor_ = std::make_unique<rendering::CompositionRenderer>(context_, shaders_);
    if (auto r = compositor_->init(); !r) {
        return r;
    }
    compositor_->setTimeline(&renderer_->timeline());
    compositor_->setInputProvider([this] {
        return rendering::CompositionRenderer::Input{&engine_->layers(),
                                                     engine_->timelineClock().seconds};
    });
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

    // ADR-242: AOV export. The auxiliary targets have been written every frame since ADR-035 and
    // nothing outside a debug view has ever read them; this is the consumer. Resolved here rather
    // than per frame so an unknown name fails at `start()` and not two hours into a sequence.
    if (auto list = settings_.aovList(); !list) {
        return std::unexpected(list.error());
    } else if (!list->empty()) {
        using Format = gpu::ReadbackRing::Format;
        for (const std::string& name : *list) {
            AovSource src;
            src.name = name;
            if (name == "normal") {
                // One target, so one readback: roughness travels with the normal rather than
                // costing a second copy to write the same bytes twice. It is oct-encoded on the
                // GPU and decoded on the way to the file -- see `decodeNormalRoughness`.
                src.texture = &renderer_->normalRoughnessTexture();
                src.format = Format::Rgba16Float;
                src.decodeNormal = true;
            } else if (name == "emission") {
                src.texture = &renderer_->emissionTexture();
                src.format = Format::Rgba16Float;
            } else if (name == "depth") {
                src.texture = &renderer_->linearDepthTexture();
                src.format = Format::R32Float;
                src.half = false; // metres against a far plane; a half would quantise it
            } else if (name == "velocity") {
                src.texture = &renderer_->velocityTexture();
                src.format = Format::Rg16Float;
            } else if (name == "id") {
                src.texture = &renderer_->identifierTexture();
                src.format = Format::R32Uint;
                src.half = false; // an identifier is an integer; half is exact only to 2048
            } else {
                return avgen::fail("render: unhandled aov '{}'", name); // aovList() validates
            }
            aovs_.push_back(std::move(src));
        }
        // One slot per AOV, so a frame's copies are all in flight at once and -- because the ring
        // hands slots out in order -- each slot sees the same format every frame instead of
        // resizing its staging buffer as the formats rotate past it.
        aovRing_ = std::make_unique<gpu::ReadbackRing>(
            context_, static_cast<std::uint32_t>(std::max<std::size_t>(3, aovs_.size())));
        aovDir_ = isSequence(settings_.output) ? output_ : output_.parent_path();
        std::error_code aovEc;
        std::filesystem::create_directories(aovDir_, aovEc);
        if (aovEc) {
            return avgen::fail("cannot create '{}' for AOVs: {}", aovDir_.string(), aovEc.message());
        }
        log::info("render: exporting {} AOV(s) beside the frames: {}", aovs_.size(), settings_.aovs);
    }
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

void RenderJob::decodeNormalRoughness(gpu::ImageF& image) {
    // The inverse of `octEncode`, matching shaders/common.wgsl line for line. Done here rather than
    // in a shader because there is no pass to put it in: the export is a copy, not a draw, and a
    // whole render pipeline to move four floats per pixel would cost more than it saves.
    for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        const float ex = image.rgba[i];
        const float ey = image.rgba[i + 1];
        const float roughness = image.rgba[i + 2];
        float x = ex;
        float y = ey;
        const float z = 1.0f - std::abs(ex) - std::abs(ey);
        if (z < 0.0f) {
            const float sx = x >= 0.0f ? 1.0f : -1.0f;
            const float sy = y >= 0.0f ? 1.0f : -1.0f;
            const float nx = (1.0f - std::abs(y)) * sx;
            const float ny = (1.0f - std::abs(x)) * sy;
            x = nx;
            y = ny;
        }
        const float len = std::sqrt(x * x + y * y + z * z);
        // A texel no geometry wrote is (0,0,0,0), which decodes to a length of exactly one in z --
        // a normal pointing at the camera, everywhere there is sky. Left as zero instead, because
        // a matte of "where is there a surface" is half of what this pass is for.
        const bool empty = ex == 0.0f && ey == 0.0f && roughness == 0.0f;
        if (empty || len < 1e-6f) {
            image.rgba[i] = image.rgba[i + 1] = image.rgba[i + 2] = 0.0f;
            image.rgba[i + 3] = 0.0f;
            continue;
        }
        image.rgba[i] = x / len;
        image.rgba[i + 1] = y / len;
        image.rgba[i + 2] = z / len;
        image.rgba[i + 3] = roughness;
    }
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
        if (!item.aov.empty()) {
            // An AOV, whatever the beauty output is -- including a video, where the frame itself
            // has no file. `written_` counts it like any other unit of work, so a progress bar
            // that reaches the end still means the work is done.
            r = assets::writeExr(settings_.aovFile(aovDir_, item.index, item.aov), item.imageF.width,
                                 item.imageF.height, item.imageF.rgba, item.aovHalf);
            if (!r) {
                fail(fmt::format("frame {} aov {}: {}", item.index, item.aov, r.error().message));
                cancelled_ = true;
            }
            std::lock_guard lock(mutex_);
            ++written_;
            continue;
        }
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
    // The auxiliary copies go after the beauty enqueue, which finished and submitted the frame's
    // command buffer -- so these use the overload that makes its own, which is exactly what it is
    // for: a texture whose contents are already on the GPU. The index encodes (frame, slot) so a
    // completed copy knows which AOV of which frame it is without a side table.
    for (std::size_t i = 0; i < aovs_.size(); ++i) {
        const AovSource& a = aovs_[i];
        if (a.texture == nullptr || *a.texture == nullptr) {
            continue; // a target the renderer has not created at this size
        }
        if (auto r = aovRing_->enqueue(*a.texture, settings_.width, settings_.height,
                                       rendered_ * aovs_.size() + i, a.format);
            !r) {
            return r;
        }
    }
    if (const scene::Composition* comp = engine_->composition()) {
        const auto counts = comp->entityWorld().counts();
        entityFullMax_ = std::max(entityFullMax_, counts.full);
        entityCoarseMax_ = std::max(entityCoarseMax_, counts.coarse);
        entitySkippedMax_ = std::max(entitySkippedMax_, counts.skipped);
        const scene::RigStats& rig = comp->rigStats();
        rigPosedMax_ = std::max(rigPosedMax_, rig.posed);
        rigRateLimitedMax_ = std::max(rigRateLimitedMax_, rig.rateLimited);
        rigCulledMax_ = std::max(rigCulledMax_, rig.culled);
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
    // The AOV ring drains beside it and never through `handleFrame`: these images are not the
    // deliverable and must not touch the frame hashes, the sequence hash or the frame count.
    if (aovRing_ != nullptr) {
        if (all) {
            if (auto r = aovRing_->flush(); !r) {
                return r;
            }
        }
        while (auto frame = aovRing_->poll()) {
            const std::size_t slot = static_cast<std::size_t>(frame->index % aovs_.size());
            Pending item;
            item.index = frame->index / aovs_.size();
            item.imageF = std::move(frame->imageF);
            item.aov = aovs_[slot].name;
            item.aovHalf = aovs_[slot].half;
            if (aovs_[slot].decodeNormal) {
                decodeNormalRoughness(item.imageF);
            }
            {
                std::unique_lock lock(mutex_);
                if (queue_.size() >= queueLimit_) {
                    spaceCv_.wait(lock, [&] { return queue_.size() < queueLimit_ || stopEncoders_ || !error_.empty(); });
                }
                if (error_.empty() && !stopEncoders_) {
                    queue_.push_back(std::move(item));
                }
            }
            cv_.notify_one();
        }
        if (!aovRing_->error().empty()) {
            return avgen::fail("aov readback: {}", aovRing_->error());
        }
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
        // The structural check on ADR-186's lift, reported whether or not it looks right. An
        // entity counted coarse or skipped in an offline render is a distant character that froze
        // or stuttered in the deliverable -- the artifact the lift exists to remove.
        log::info("render: entity updates per frame, worst case -- full {}, coarse {}, skipped {}{}",
                  entityFullMax_, entityCoarseMax_, entitySkippedMax_,
                  (entityCoarseMax_ > 0 || entitySkippedMax_ > 0)
                      ? "  <-- the distance limits reached the simulation; distant bodies stuttered or froze"
                      : "  (every body simulated at full rate at every distance)");
    log::info("render: rig poses per frame, worst case -- posed {}, rate-limited {}, culled {}{}",
              rigPosedMax_, rigRateLimitedMax_, rigCulledMax_,
              (rigRateLimitedMax_ > 0 || rigCulledMax_ > 0)
                  ? "  <-- the rig ladder reached the deliverable; distant characters slid or glided"
                  : "  (every rig posed at its authored rate at every distance)");
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
