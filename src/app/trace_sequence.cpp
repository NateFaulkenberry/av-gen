#include "app/trace_sequence.hpp"

#include "app/engine.hpp"
#include "assets/exr.hpp"
#include "assets/video_writer.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "pathtrace/denoise.hpp"
#include "pathtrace/path_tracer.hpp"
#include "pathtrace/snapshot.hpp"
#include "scene/tonemap.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace avgen::app {

Result<void> TraceSequenceRequest::validate() const {
    if (project.empty()) {
        return fail("trace sequence: no project");
    }
    if (!std::filesystem::exists(project)) {
        return fail("trace sequence: '{}' does not exist", project.string());
    }
    if (output.empty()) {
        return fail("trace sequence: no output path");
    }
    if (width < 16 || height < 16) {
        return fail("trace sequence: {}x{} is smaller than the 16x16 floor", width, height);
    }
    if (kind == RenderOutput::PngSequence) {
        // Refused rather than quietly tone-mapped into one. A path trace's whole output is
        // scene-linear radiance, and an 8-bit sequence throws that away for no gain a video does
        // not already give -- if somebody wants one, it should be a decision with a reason, not a
        // radio button that happened to be selected.
        return fail("trace sequence: a path trace writes EXR or video, not a PNG sequence");
    }
    return trace.validate();
}

TraceSequenceRequest traceSequenceRequestFrom(std::filesystem::path project,
                                              const PathTraceSettings& trace, std::uint32_t width,
                                              std::uint32_t height, std::filesystem::path output,
                                              const RenderSettings& video) {
    TraceSequenceRequest r;
    r.project = std::move(project);
    r.trace = trace;
    r.width = std::max(16u, width);
    r.height = std::max(16u, height);
    r.output = std::move(output);
    const std::string ext = r.output.extension().string();
    if (ext == ".mov" || ext == ".mp4" || ext == ".m4v" || ext == ".mkv" || ext == ".webm") {
        r.kind = RenderOutput::Video;
        r.codec = video.codec;
        r.backend = video.backend;
        r.quality = video.quality;
        r.muxAudio = video.muxAudio;
    } else {
        r.kind = RenderOutput::ExrSequence;
        // A sequence's output NAMES A FOLDER. `.exr` on it is what a person types out of habit
        // after tracing single frames, and leaving it on would make a hundred files fight over one
        // name -- or, worse, make a folder called `hero.exr` that every tool treats as an image.
        if (!ext.empty()) {
            r.output.replace_extension();
        }
    }
    return r;
}

std::filesystem::path traceSequenceAovFile(const TraceSequenceRequest& request, std::uint64_t index) {
    RenderSettings naming;
    naming.pattern = RenderSettings::defaultPattern(RenderOutput::ExrSequence);
    std::filesystem::path dir = request.output;
    if (request.kind == RenderOutput::Video) {
        dir = request.output.parent_path() / (request.output.stem().string() + "_aovs");
    }
    std::filesystem::path file = naming.frameFile(dir, index);
    file.replace_extension(".aovs.exr");
    return file;
}

namespace {

std::size_t sequenceFrameBytes(const SequenceFrame& f) {
    return f.rgba8.size() + f.rgbaF.size() * sizeof(float);
}

} // namespace

// ---- the source ----------------------------------------------------------------------------------

namespace {

// Bridges the tracer to the driver: evaluates the project at frame f's time, traces it, and hands
// back the pixels. The driver owns the range, the loop, the hashing and the writers; this owns the
// engine and the tracer and nothing else.
class TraceFrameSource final : public FrameSource {
public:
    TraceFrameSource(Engine& engine, const TraceSequenceRequest& request,
                     std::atomic<bool>& cancelled, std::atomic<std::uint32_t>& samplesDone,
                     std::atomic<std::uint32_t>& samplesTotal)
        : engine_(engine), request_(request), cancelled_(cancelled), samplesDone_(samplesDone),
          samplesTotal_(samplesTotal) {
        settings_ = traceSettingsFrom(request_.trace, request_.width, request_.height);
        settings_.reuseAcceleration = request_.reuseAcceleration;
        samplesTotal_.store(settings_.samplesPerPixel);
        // Motion is an AOV, and the AOVs are the only consumer of the previous frame.
        wantsMotion_ = request_.trace.writeAovs;
    }

    Result<void> begin(const FrameRange& range, std::uint64_t frames) override {
        frames_ = frames;
        // The settle `pathtrace::TraceJob` does before a single frame, kept verbatim so a one-frame
        // sequence is the same picture as a single trace: one update at dt 0 and one at a frame's
        // worth, both at the start time. It is not a step -- it lets anything that reacts to being
        // placed somewhere settle before the first frame is measured.
        engine_.seekSeconds(range.startSeconds);
        engine_.update(FrameTime{range.startSeconds, 0.0, 0});
        engine_.update(FrameTime{range.startSeconds, 1.0 / std::max(range.fps, 1e-6), 1});
        return {};
    }

    Result<void> submit(std::uint64_t index, const FrameTime& time) override {
        engine_.update(time);

        // Motion vectors need the frame before this one, and a sequence is the only place they can
        // come from: `buildSnapshot(scene, &previous)` wants two evaluations of the same
        // composition, which a single-frame job can never have. This is a capability the sequence
        // gains for free and the single frame cannot.
        const pathtrace::Snapshot snapshot =
            previous_ ? pathtrace::buildSnapshot(engine_.scene(), &*previous_)
                      : pathtrace::buildSnapshot(engine_.scene());
        if (snapshot.empty()) {
            return fail("trace sequence: frame {} has nothing the tracer can see; refusing to "
                        "write a black frame",
                        index);
        }
        if (index == 0) {
            pathtrace::logCapabilities(snapshot);
        }

        pathtrace::Framebuffer fb;
        samplesDone_.store(0);
        // ONE tracer for the range (ADR-583): it keeps the BVH, and `render` rebuilds only what
        // this frame's snapshot changed. A fresh tracer per frame was a full rebuild per frame --
        // 1.7 s on the Tree of Life, for geometry that had not changed shape.
        pathtrace::PathTracer& tracer = tracer_;
        auto ok = tracer.render(
            snapshot, settings_, fb, [this] { return cancelled_.load(); },
            [this](std::uint32_t done, std::uint32_t) { samplesDone_.store(done); });
        if (!ok) {
            return std::unexpected(ok.error());
        }
        {
            const pathtrace::TraceStats& st = tracer.stats();
            const pathtrace::BvhUpdateStats& b = st.bvh;
            log::info("trace sequence: frame {} bvh {:.1f} ms ({}: {}/{} objects built, {} tri built, "
                      "{} tri kept, top level {} over {} instances, {} transforms moved; compare "
                      "{:.1f} ms, objects {:.1f} ms, top {:.1f} ms; embree holds {:.0f} MB, peak "
                      "{:.0f} MB), render {:.0f} ms",
                      index, st.buildSeconds * 1000.0,
                      settings_.reuseAcceleration ? "reuse" : "rebuild", b.objectsBuilt, b.objects,
                      b.trianglesBuilt, b.trianglesReused, b.topLevelBuilt ? "built" : "kept",
                      b.topLevelInstances, b.transformsChanged, b.compareSeconds * 1000.0,
                      b.objectSeconds * 1000.0, b.topLevelSeconds * 1000.0,
                      static_cast<double>(b.heldBytes) / 1e6, static_cast<double>(b.peakBytes) / 1e6,
                      st.renderSeconds * 1000.0);
        }
        if (cancelled_.load()) {
            // Cancelled between batches. Not an error, and not a frame either: returning here
            // leaves the sequence's last complete frame as the last frame, rather than writing a
            // half-accumulated one that looks like a render.
            return {};
        }

        if (request_.trace.denoise) {
            pathtrace::DenoiseInput in;
            in.width = fb.width;
            in.height = fb.height;
            const auto radiance = fb.resolvedRadiance();
            const auto albedo = fb.resolvedAlbedo();
            const auto normal = fb.resolvedNormal();
            in.color = &radiance;
            if (!albedo.empty()) in.albedo = &albedo;
            if (!normal.empty()) in.normal = &normal;
            std::vector<glm::vec3> denoised;
            if (auto d = pathtrace::denoise(in, denoised); !d) {
                return std::unexpected(d.error());
            }
            fb.radiance = std::move(denoised);
            // `sampleCount = 1` makes the denoised radiance resolve to itself -- and would make
            // every accumulated feature buffer resolve to N times its value in the AOV file. They
            // are folded to one sample with it. (Depth and id are first-sample values already.)
            if (request_.trace.writeAovs) {
                fb.albedo = albedo;
                fb.normal = normal;
                fb.emission = fb.resolvedEmission();
                fb.motion = fb.resolvedMotion();
            }
            fb.sampleCount = 1;
        }

        SequenceFrame frame;
        frame.index = index;
        frame.width = fb.width;
        frame.height = fb.height;
        if (request_.kind == RenderOutput::Video) {
            // THE POINT OF THE CPU TONE MAP. Scene-linear radiance becomes display-referred bytes
            // here, on this thread, with no device -- which is what lets a path-traced movie be
            // rendered while the GPU is busy with something else.
            const std::vector<glm::vec3> radiance = fb.resolvedRadiance();
            frame.rgba8.resize(static_cast<std::size_t>(fb.width) * fb.height * 4);
            scene::tonemapImage(tonemap_, fb.width, fb.height, radiance, frame.rgba8);
        } else {
            frame.rgbaF = fb.resolveRgba();
        }
        ready_.push_back(std::move(frame));

        // The AOVs, written now and released with `fb` at the end of this call. They used to be
        // pushed onto a vector "so the writer can emit the multi-layer EXR beside the beauty
        // frame" -- and no writer ever read it, so a range with AOVs on (the default) kept every
        // frame's seven buffers until the job ended: ~140 MB per 1080p frame, tens of gigabytes
        // over a long shot, and no file on disk to show for it. Written here, on this thread,
        // because a multi-layer EXR costs milliseconds against a frame that costs seconds and it
        // keeps nothing alive past the frame that made it.
        if (request_.trace.writeAovs) {
            const std::filesystem::path aov = traceSequenceAovFile(request_, index);
            std::error_code ec;
            std::filesystem::create_directories(aov.parent_path(), ec);
            if (auto wrote = pathtrace::writeFramebufferAovExr(fb, aov); !wrote) {
                return std::unexpected(wrote.error());
            }
        }

        // The scene as it was, for the next frame's motion pass. A deep copy, which is what
        // `pathtrace::Snapshot`'s own header requires: the controller mutates the scene in place
        // and two timeline times cannot otherwise coexist.
        //
        // Only when something will READ it. `scene::Scene` carries every mesh and every texture,
        // so on the Tree of Life this is tens of megabytes per frame, and the motion pass is the
        // sole consumer -- paying for it on a sequence that writes beauty only would be a cost
        // with no output, which is the kind of thing that hides in a renderer for a year.
        if (wantsMotion_) {
            previous_ = engine_.scene();
        }
        return {};
    }

    Result<void> collect(bool, const std::function<void(SequenceFrame)>& sink) override {
        // The tracer is synchronous, so a frame is ready the moment `submit` returns and there is
        // no ring to drain. The interface still has this call because the rasteriser needs it.
        for (auto& f : ready_) {
            sink(std::move(f));
        }
        ready_.clear();
        return {};
    }

    Result<void> end() override { return {}; }

    [[nodiscard]] std::size_t heldBytes() const {
        std::size_t n = 0;
        for (const auto& f : ready_) n += sequenceFrameBytes(f);
        return n;
    }

    void setTonemap(const scene::TonemapInputs& t) { tonemap_ = t; }

private:
    Engine& engine_;
    const TraceSequenceRequest& request_;
    std::atomic<bool>& cancelled_;
    std::atomic<std::uint32_t>& samplesDone_;
    std::atomic<std::uint32_t>& samplesTotal_;
    pathtrace::TraceSettings settings_{};
    scene::TonemapInputs tonemap_{};
    std::uint64_t frames_ = 0;
    std::vector<SequenceFrame> ready_;
    bool wantsMotion_ = false;
    std::optional<scene::Scene> previous_;
    pathtrace::PathTracer tracer_;   // outlives a frame so its BVH does (ADR-583)
};

} // namespace

// ---- the job ---------------------------------------------------------------------------------

struct TraceSequence::Impl {
    TraceSequenceRequest request;
    std::unique_ptr<Engine> engine;
    std::unique_ptr<TraceFrameSource> source;
    std::unique_ptr<FrameSequenceDriver> driver;
    std::unique_ptr<assets::VideoWriter> video;
    std::filesystem::path outputDir;
    std::string pattern;
    std::atomic<bool> cancelled{false};
    std::atomic<std::uint32_t> samplesDone{0};
    std::atomic<std::uint32_t> samplesTotal{0};
};

TraceSequence::TraceSequence(TraceSequenceRequest request) : impl_(std::make_unique<Impl>()) {
    impl_->request = std::move(request);
}

TraceSequence::~TraceSequence() {
    if (impl_ && impl_->driver) {
        impl_->cancelled = true;
        impl_->driver->cancel();
    }
}

Result<void> TraceSequence::start() {
    if (auto ok = impl_->request.validate(); !ok) {
        return ok;
    }
    // ONCE. This is the measured reason this is not a queue of `TraceJob`s: that class loads the
    // project inside its own body, so a hundred frames would parse the project and rebuild every
    // mesh and texture a hundred times over.
    impl_->engine = std::make_unique<Engine>(EngineMode::Offline);
    if (auto ok = impl_->engine->loadProject(impl_->request.project); !ok) {
        return fail("trace sequence: {}", ok.error().message);
    }

    const FrameRange range = impl_->request.trace.frameRange();

    scene::TonemapInputs tm;
    tm.op = impl_->engine->scene().post.tonemap;
    tm.vignette = impl_->engine->scene().post.vignette;
    tm.chromaRetention = impl_->engine->scene().post.chromaRetention;
    // Grain is deliberately left off for a path-traced video. It is a per-pixel hash of the
    // fragment's uv, it cannot agree with the GPU's (`scene/tonemap.hpp` says why), and adding
    // noise to an image somebody has just spent minutes per frame denoising is the wrong default.
    tm.grain = 0.0f;

    impl_->source = std::make_unique<TraceFrameSource>(*impl_->engine, impl_->request,
                                                       impl_->cancelled, impl_->samplesDone,
                                                       impl_->samplesTotal);
    impl_->source->setTonemap(tm);

    FrameWriter writer;
    if (impl_->request.kind == RenderOutput::Video) {
        assets::VideoSettings vs;
        vs.width = impl_->request.width;
        vs.height = impl_->request.height;
        vs.fps = range.fps;
        vs.codec = impl_->request.codec;
        vs.backend = impl_->request.backend;
        vs.quality = impl_->request.quality;
        if (impl_->request.muxAudio && impl_->engine->hasAudio()) {
            vs.audio = impl_->engine->audioPath();
            vs.audioOffsetSeconds = range.startSeconds;
        }
        auto opened = assets::openVideoWriter(impl_->request.output, vs);
        if (!opened) {
            return std::unexpected(opened.error());
        }
        impl_->video = std::move(*opened);
        writer = [this](const SequenceFrame& f) -> Result<void> {
            return impl_->video->writeFrame(f.rgba8);
        };
    } else {
        std::error_code ec;
        std::filesystem::create_directories(impl_->request.output, ec);
        impl_->outputDir = impl_->request.output;
        impl_->pattern = RenderSettings::defaultPattern(RenderOutput::ExrSequence);
        writer = [this](const SequenceFrame& f) -> Result<void> {
            RenderSettings naming;
            naming.pattern = impl_->pattern;
            return assets::writeExr(naming.frameFile(impl_->outputDir, f.index), f.width, f.height,
                                    f.rgbaF, /*half=*/false);
        };
    }

    // One writer thread for a video because the muxer is sequential; for an EXR sequence the
    // driver's default is a thread per core less one, and EXR compression is the slow part.
    const unsigned threads = impl_->request.kind == RenderOutput::Video ? 1u : 0u;
    impl_->driver = std::make_unique<FrameSequenceDriver>(*impl_->source, std::move(writer), threads);
    const double audio = impl_->engine->durationSeconds();
    const double timeline = impl_->engine->timeline().durationSeconds();
    if (auto ok = impl_->driver->start(range, audio, timeline); !ok) {
        return ok;
    }
    log::info("trace sequence: {} frame(s) {}x{} at {} spp, depth {} -> {}",
              impl_->driver->frameCount(), impl_->request.width, impl_->request.height,
              impl_->request.trace.samplesPerPixel, impl_->request.trace.maxDepth,
              impl_->request.output.string());
    return {};
}

bool TraceSequence::step(int maxFrames, double budgetSeconds) {
    if (!impl_->driver) {
        return true;
    }
    const bool complete = impl_->driver->step(maxFrames, budgetSeconds);
    if (complete && impl_->video) {
        if (auto r = impl_->video->finish(); !r) {
            log::error("trace sequence: {}", r.error().message);
        }
        impl_->video.reset();
    }
    return complete;
}

Result<void> TraceSequence::run() {
    if (auto ok = start(); !ok) {
        return ok;
    }
    while (!step(1)) {
    }
    const SequenceProgress p = impl_->driver->progress();
    if (!p.error.empty()) {
        return fail("{}", p.error);
    }
    return {};
}

void TraceSequence::cancel() {
    impl_->cancelled = true;
    if (impl_->driver) {
        impl_->driver->cancel();
    }
}

SequenceProgress TraceSequence::progress() const {
    return impl_->driver ? impl_->driver->progress() : SequenceProgress{};
}

std::uint32_t TraceSequence::frameSamplesDone() const { return impl_->samplesDone.load(); }
std::uint32_t TraceSequence::frameSamplesTotal() const { return impl_->samplesTotal.load(); }
std::size_t TraceSequence::heldFrameBytes() const {
    return impl_->source ? impl_->source->heldBytes() : 0;
}

} // namespace avgen::app
